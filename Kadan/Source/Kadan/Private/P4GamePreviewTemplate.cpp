#include "P4GamePreviewTemplate.h"
#include "P4GameNames.h"
#include "P4GamePreviewNavigation.h"
#include "P4KadanImport.h"
#include "P4MapKitCore/export_package.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

namespace
{
using namespace P4MapKit;
constexpr int64 Limit = 64 * 1024 * 1024;
const TCHAR* const Section = TEXT("PreviewTemplate");

FString Text(const std::string& Value)
{
    return FString(UTF8_TO_TCHAR(Value.c_str()));
}

FString PackageOf(const FString& Path)
{
    FString Package = Path;
    const int32 Dot = Package.Find(TEXT("."));
    if (Dot != INDEX_NONE)
        Package.LeftInline(Dot);
    return Package;
}

bool GamePackage(const FString& Package)
{
    return Package.StartsWith(TEXT(P4_GAME_ASSET_ROOT "/"), ESearchCase::CaseSensitive) &&
        FPackageName::IsValidLongPackageName(Package);
}

// One converted game package, read in place. Its asset is the export named like the package.
struct FSource
{
    FString Package;
    std::vector<uint8> Header;
    std::vector<uint8> Body;
    FPackageInventory Inventory;
    int32 Asset = INDEX_NONE;

    bool Load(const FP4KadanImportSettings& Settings, const FString& InPackage, FString& Error)
    {
        Package = InPackage;
        FString HeaderFile;
        if (!GamePackage(Package) || !P4AcquireGamePreviewSource(Settings, Package, HeaderFile, Error))
        {
            if (Error.IsEmpty())
                Error = TEXT("The preview template names a package outside the game: ") + Package;
            return false;
        }
        const FString BodyFile = FPaths::ChangeExtension(HeaderFile, TEXT("uexp"));
        TArray<uint8> HeaderBytes;
        TArray<uint8> BodyBytes;
        if (IFileManager::Get().FileSize(*HeaderFile) > Limit || IFileManager::Get().FileSize(*BodyFile) > Limit ||
            !FFileHelper::LoadFileToArray(HeaderBytes, *HeaderFile) ||
            !FFileHelper::LoadFileToArray(BodyBytes, *BodyFile))
        {
            Error = TEXT("A preview template package is missing or too large: ") + Package;
            return false;
        }
        Header.assign(HeaderBytes.GetData(), HeaderBytes.GetData() + HeaderBytes.Num());
        Body.assign(BodyBytes.GetData(), BodyBytes.GetData() + BodyBytes.Num());
        Inventory = ReadPackageHeader(Header, Body.size());
        if (!Inventory.Error.Code.empty())
        {
            Error = TEXT("A preview template package could not be read: ") + Package;
            return false;
        }
        const std::string Leaf = TCHAR_TO_UTF8(*FPackageName::GetShortName(Package));
        for (std::size_t Index = 0; Index < Inventory.Exports.size(); ++Index)
            if (Inventory.Exports[Index].Name == Leaf && Inventory.Exports[Index].Outer == 0)
                Asset = int32(Index);
        if (Asset == INDEX_NONE)
        {
            Error = TEXT("A preview template package has no asset of its own name: ") + Package;
            return false;
        }
        return true;
    }

    FPropertyInventory Properties(int32 Index) const
    {
        const auto& Export = Inventory.Exports[Index];
        return ReadPropertyEnvelopes(Body, Inventory.Names, Export.Offset, Export.Offset + Export.Size);
    }

    FString Class() const
    {
        std::string Path;
        return Inventory.Reference(Inventory.Exports[Asset].Class, Path) ? Text(Path) : FString();
    }

    bool Object(const FPropertyEnvelope& Property, FString& Out) const
    {
        FByteReader Reader(Body, Property.PayloadOffset, Property.PayloadOffset + Property.Size);
        int32 Reference = 0;
        std::string Path;
        if (Property.Type != "ObjectProperty" || !Reader.I32(Reference) || !Inventory.Reference(Reference, Path))
            return false;
        Out = Text(Path);
        return true;
    }

    bool Soft(FByteReader& Reader, FString& Out) const
    {
        std::string Path;
        std::string Sub;
        if (!Reader.Name(Inventory.Names, Path) || !Reader.String(Sub) || !Sub.empty())
            return false;
        Out = Text(Path);
        return true;
    }

    // The packages an object or soft-object array names, in order.
    bool Packages(const FPropertyEnvelope& Property, TArray<FString>& Out) const
    {
        Out.Reset();
        FByteReader Reader(Body, Property.PayloadOffset, Property.PayloadOffset + Property.Size);
        std::size_t Count = 0;
        if (Property.Type != "ArrayProperty" || Property.Detail.size() != 1 || !Reader.Count(Count, 10000))
            return false;
        for (std::size_t Index = 0; Index < Count; ++Index)
        {
            FString Path;
            if (Property.Detail[0] == "SoftObjectProperty")
            {
                if (!Soft(Reader, Path))
                    return false;
            }
            else
            {
                int32 Reference = 0;
                std::string Name;
                if (Property.Detail[0] != "ObjectProperty" || !Reader.I32(Reference) ||
                    !Inventory.Reference(Reference, Name))
                    return false;
                Path = Text(Name);
            }
            Out.Add(PackageOf(Path));
        }
        return Reader.Tell() == Property.PayloadOffset + Property.Size;
    }
};

const FPropertyEnvelope* Find(const FPropertyInventory& List, const char* Name)
{
    for (const auto& Property : List.Properties)
        if (Property.Name == Name)
            return &Property;
    return nullptr;
}

bool ReadScene(const FP4KadanImportSettings& Settings, const FString& Package, FP4GamePreviewScene& Out, FString& Error)
{
    Out = FP4GamePreviewScene();
    FSource Scene;
    if (!Scene.Load(Settings, Package, Error))
        return false;
    const auto Properties = Scene.Properties(Scene.Asset);
    const FPropertyEnvelope* Parent = Find(Properties, "ParentScene");
    const FPropertyEnvelope* Level = Find(Properties, "OpenLevel");
    const FPropertyEnvelope* Preloads = Find(Properties, "LoadAssets");
    const FPropertyEnvelope* Info = Find(Properties, "GameSceneInfo");
    FString World;
    bool Read = Properties.Error.Code.empty() && Parent && Level && Info && Level->Type == "SoftObjectProperty" &&
        Info->Type == "StructProperty" && Scene.Object(*Parent, Out.Parent);
    Out.Class = Scene.Class();
    if (Read)
    {
        FByteReader Reader(Scene.Body, Level->PayloadOffset, Level->PayloadOffset + Level->Size);
        Read = Scene.Soft(Reader, World);
    }
    if (Read && Preloads)
        Read = Scene.Packages(*Preloads, Out.Preloads);
    if (Read)
    {
        const auto Members = ReadPropertyEnvelopes(
            Scene.Body, Scene.Inventory.Names, Info->PayloadOffset, Info->PayloadOffset + Info->Size);
        const FPropertyEnvelope* Counts = Find(Members, "ActorPlacedNumAsset");
        FString CountsPath;
        Read = Members.Error.Code.empty() && Counts && Scene.Object(*Counts, CountsPath);
        Out.Counts = PackageOf(CountsPath);
    }
    Out.World = PackageOf(World);
    if (!Read || !GamePackage(Out.World) || !GamePackage(Out.Counts) || !GamePackage(PackageOf(Out.Parent)))
    {
        Error = TEXT("A preview template scene does not name its world, parent scene and placement counts: ") + Package;
        return false;
    }
    return true;
}

// The class and format version of a placement counts asset.
bool ReadCounts(
    const FP4KadanImportSettings& Settings, const FString& Package, FString& Class, uint64& Version, FString& Error)
{
    FSource Counts;
    if (!Counts.Load(Settings, Package, Error))
        return false;
    Class = Counts.Class();
    Version = 0;
    const auto Properties = Counts.Properties(Counts.Asset);
    const FPropertyEnvelope* Info = Find(Properties, "ActorPlacedNumInfo");
    if (Properties.Error.Code.empty() && Info && Info->Type == "StructProperty")
    {
        const auto Members = ReadPropertyEnvelopes(
            Counts.Body, Counts.Inventory.Names, Info->PayloadOffset, Info->PayloadOffset + Info->Size);
        const FPropertyEnvelope* Field = Find(Members, "AssetVersion");
        if (Members.Error.Code.empty() && Field && Field->Type == "UInt64Property" && Field->Size == 8)
        {
            FByteReader Reader(Counts.Body, Field->PayloadOffset, Field->PayloadOffset + 8);
            Reader.Unsigned(8, Version);
        }
    }
    if (Class.IsEmpty() || Version == 0)
    {
        Error = TEXT("A preview template's placement counts have no class or format version: ") + Package;
        return false;
    }
    return true;
}

// The placement list the world's settings name first.
bool ReadPlacementList(
    const FP4KadanImportSettings& Settings, const FString& WorldPackage, FString& Out, FString& Error)
{
    FSource World;
    if (!World.Load(Settings, WorldPackage, Error))
        return false;
    TArray<FString> Lists;
    int32 Found = 0;
    for (int32 Index = 0; Index < int32(World.Inventory.Exports.size()); ++Index)
    {
        const auto Properties = World.Properties(Index);
        const FPropertyEnvelope* Property = Find(Properties, "ActorPlacementInfoListAssets");
        if (!Properties.Error.Code.empty() || !Property)
            continue;
        ++Found;
        if (!World.Packages(*Property, Lists))
            Found = 0;
    }
    if (Found != 1 || Lists.Num() == 0)
    {
        Error = TEXT("A preview template world does not name one placement list: ") + WorldPackage;
        return false;
    }
    Out = Lists[0];
    return true;
}

bool ReadAssetList(const FP4KadanImportSettings& Settings, const FString& Package, TArray<FString>& Out, FString& Error)
{
    FSource List;
    if (!List.Load(Settings, Package, Error))
        return false;
    const auto Properties = List.Properties(List.Asset);
    const FPropertyEnvelope* Assets = Find(Properties, "AssetList");
    if (!Properties.Error.Code.empty() || !Assets || !List.Packages(*Assets, Out))
    {
        Error = TEXT("A preview template list has no readable asset list: ") + Package;
        return false;
    }
    return true;
}

bool ReadClasses(const FP4KadanImportSettings& Settings, const FString& Package, TArray<FString>& Out, FString& Error)
{
    Out.Reset();
    FSource Placement;
    if (!Placement.Load(Settings, Package, Error))
        return false;
    const auto Properties = Placement.Properties(Placement.Asset);
    const FPropertyEnvelope* Array = Find(Properties, "ActorGeneratorList");
    if (!Properties.Error.Code.empty() || !Array)
        return true;
    FByteReader Reader(Placement.Body, Array->PayloadOffset, Array->PayloadOffset + Array->Size);
    std::size_t Count = 0;
    FPropertyEnvelope Inner;
    if (Array->Type != "ArrayProperty" || !Reader.Count(Count, 100000) ||
        !ReadPropertyEnvelope(Reader, Placement.Inventory.Names, Placement.Body.size(), Inner) ||
        Inner.Type != "StructProperty")
        return true;
    std::size_t At = Inner.PayloadOffset;
    for (std::size_t Index = 0; Index < Count; ++Index)
    {
        const auto Row = ReadPropertyEnvelopes(
            Placement.Body, Placement.Inventory.Names, At, Inner.PayloadOffset + Inner.Size);
        // An asset whose rows can't all be read is not a base; the bootstrap reads its base whole.
        if (!Row.Error.Code.empty() || Row.End <= At)
        {
            Out.Reset();
            return true;
        }
        FString Name;
        if (const FPropertyEnvelope* Class = Find(Row, "SoftRefActorClass"))
        {
            FByteReader Path(Placement.Body, Class->PayloadOffset, Class->PayloadOffset + Class->Size);
            Placement.Soft(Path, Name);
        }
        Out.Add(Name);
        At = Row.End;
    }
    return true;
}

bool ReadRecord(FP4GamePreviewTemplate& Out, FString& Error)
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Kadan"));
    const FString File = Plugin ? Plugin->GetBaseDir() / TEXT("Config/DefaultKadan.ini") : FString();
    FConfigFile Config;
    // Combine, not Read: only Combine applies the record's +BaseClass entries.
    if (!File.IsEmpty() && IFileManager::Get().FileExists(*File))
        Config.Combine(File);
    Config.GetString(Section, TEXT("WorldScene"), Out.WorldScene);
    Config.GetString(Section, TEXT("ArrivalScene"), Out.ArrivalScene);
    Config.GetString(Section, TEXT("GeometryStream"), Out.GeometryStream);
    Config.GetString(Section, TEXT("PlayerStream"), Out.PlayerStream);
    Config.GetArray(Section, TEXT("BaseClass"), Out.BaseClasses);
    for (const FString* Package : {&Out.WorldScene, &Out.ArrivalScene, &Out.GeometryStream, &Out.PlayerStream})
        if (!GamePackage(*Package))
        {
            Error = TEXT("Kadan's preview template record is missing or incomplete: ") + File;
            return false;
        }
    if (Out.BaseClasses.Num() == 0)
    {
        Error = TEXT("Kadan's preview template record names no base classes: ") + File;
        return false;
    }
    return true;
}
} //namespace

bool P4ReadGamePreviewScene(
    const FP4KadanImportSettings& Settings, const FString& Package, FP4GamePreviewScene& Out, FString& Error)
{
    return ReadScene(Settings, Package, Out, Error);
}

bool P4ReadGamePreviewGeneratorClasses(
    const FP4KadanImportSettings& Settings, const FString& Package, TArray<FString>& Out, FString& Error)
{
    return ReadClasses(Settings, Package, Out, Error);
}

FString P4GamePreviewNameToken(const FString& WorldName, const FString& SceneName)
{
    int32 Start = 0;
    int32 Length = 0;
    for (int32 A = 0; A < WorldName.Len(); ++A)
        for (int32 B = 0; B < SceneName.Len(); ++B)
        {
            int32 Same = 0;
            while (
                A + Same < WorldName.Len() && B + Same < SceneName.Len() && WorldName[A + Same] == SceneName[B + Same])
                ++Same;
            if (Same > Length)
            {
                Start = A;
                Length = Same;
            }
        }
    return WorldName.Mid(Start, Length);
}

FString P4GamePreviewRename(const FString& Name, const FString& From, const FString& To)
{
    return From.IsEmpty() ? Name : Name.Replace(*From, *To, ESearchCase::CaseSensitive);
}

bool P4ReadGamePreviewTemplate(const FP4KadanImportSettings& Settings, FP4GamePreviewTemplate& Out, FString& Error)
{
    Out = FP4GamePreviewTemplate();
    Error.Reset();
    if (!ReadRecord(Out, Error))
        return false;
    FP4GamePreviewScene World;
    FP4GamePreviewScene Arrival;
    if (!ReadScene(Settings, Out.WorldScene, World, Error) || !ReadScene(Settings, Out.ArrivalScene, Arrival, Error))
        return false;
    Out.World = World.World;
    Out.SceneClass = World.Class;
    Out.ParentScene = World.Parent;
    Out.Counts = World.Counts;
    if (!ReadCounts(Settings, Out.Counts, Out.CountsClass, Out.CountsVersion, Error))
        return false;
    Out.WorldToken = P4GamePreviewNameToken(
        FPackageName::GetShortName(Out.World), FPackageName::GetShortName(Out.WorldScene));
    Out.ArrivalToken = P4GamePreviewNameToken(
        FPackageName::GetShortName(Arrival.World), FPackageName::GetShortName(Out.ArrivalScene));
    if (Out.WorldToken.IsEmpty() || Out.ArrivalToken.IsEmpty() || Arrival.Preloads.Num() != 1)
    {
        Error = TEXT("The preview template scenes do not share names with their worlds or preload one demo list.");
        return false;
    }
    Out.DemoList = Arrival.Preloads[0];
    TArray<FString> Demos;
    if (!ReadAssetList(Settings, Out.DemoList, Demos, Error))
        return false;
    if (Demos.Num() == 0)
    {
        Error = TEXT("The preview template's demo list is empty: ") + Out.DemoList;
        return false;
    }
    Out.Demo = Demos[0];
    TArray<FString> Placements;
    if (!ReadPlacementList(Settings, Arrival.World, Out.BaseList, Error) ||
        !ReadAssetList(Settings, Out.BaseList, Placements, Error))
        return false;
    for (const FString& Placement : Placements)
    {
        TArray<FString> Classes;
        if (!ReadClasses(Settings, Placement, Classes, Error))
            return false;
        bool All = true;
        for (const FString& Class : Out.BaseClasses)
            All = All && Classes.Contains(Class);
        if (All)
        {
            Out.Base = Placement;
            return true;
        }
    }
    Error = TEXT("No placement asset of the preview template's base list places every base class: ") + Out.BaseList;
    return false;
}

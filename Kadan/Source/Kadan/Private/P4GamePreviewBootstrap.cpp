#include "P4GamePreviewBootstrap.h"
#include "P4GamePaths.h"
#include "P4GameNames.h"
#include "P4GamePreviewNavigation.h"
#include "P4GamePreviewPackage.h"
#include "P4GamePreviewTemplate.h"
#include "P4KadanImport.h"
#include "P4MapKitCore/export_package.h"
#include "P4MapKitCore/level_native.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

namespace
{
using namespace P4MapKit;
constexpr int32 Limit = 64 * 1024 * 1024;

struct FEdit
{
    int32 At = 0, Size = 0;
    TArray<uint8> Bytes;
};
struct FPackage
{
    FP4GamePreviewBootstrapPackage Data;
    std::vector<uint8> H, B;
    FPackageInventory Inventory;
    TMap<int32, TArray<FEdit>> Edits;

    bool Load(const FP4KadanImportSettings& Settings, const FString& Package, FString& Error)
    {
        FString Header;
        if (!P4AcquireGamePreviewSource(Settings, Package, Header, Error))
            return false;
        const FString Stem = FPaths::ChangeExtension(Header, TEXT(""));
        if (IFileManager::Get().FileSize(*Header) > Limit ||
            IFileManager::Get().FileSize(*(Stem + TEXT(".uexp"))) > Limit ||
            IFileManager::Get().FileExists(*(Stem + TEXT(".ubulk"))) ||
            IFileManager::Get().FileExists(*(Stem + TEXT(".uptnl"))) ||
            !FFileHelper::LoadFileToArray(Data.Header, *Header) ||
            !FFileHelper::LoadFileToArray(Data.Body, *(Stem + TEXT(".uexp"))))
        {
            Error = TEXT("The preview bootstrap source is missing, too large, or has external bulk: ") + Package;
            return false;
        }
        Data.Package = Package;
        Data.Extension = FPaths::GetExtension(Header, true);
        H.assign(Data.Header.GetData(), Data.Header.GetData() + Data.Header.Num());
        B.assign(Data.Body.GetData(), Data.Body.GetData() + Data.Body.Num());
        Inventory = ReadPackageHeader(H, B.size());
        if (!Inventory.Error.Code.empty())
        {
            Error = TEXT("Preview bootstrap source was rejected: ") +
                FString(UTF8_TO_TCHAR(Inventory.Error.Code.c_str()));
            return false;
        }
        return true;
    }

    FString Class(int32 Index) const
    {
        std::string Path;
        if (Index < 0 || std::size_t(Index) >= Inventory.Exports.size() ||
            !Inventory.Reference(Inventory.Exports[Index].Class, Path))
            return FString();
        return FString(UTF8_TO_TCHAR(Path.c_str()));
    }
    FString Name(int32 Index) const { return FString(UTF8_TO_TCHAR(Inventory.Exports[Index].Name.c_str())); }
    FPropertyInventory Properties(int32 Index) const
    {
        const auto& E = Inventory.Exports[Index];
        return ReadPropertyEnvelopes(B, Inventory.Names, E.Offset, E.Offset + E.Size);
    }
    int32 UniqueClass(const TCHAR* Path) const
    {
        int32 Found = INDEX_NONE;
        for (std::size_t I = 0; I < Inventory.Exports.size(); ++I)
            if (Class(int32(I)) == Path)
            {
                if (Found != INDEX_NONE)
                    return INDEX_NONE;
                Found = int32(I);
            }
        return Found;
    }
    void Edit(int32 Index, std::size_t At, std::size_t Size, const TArray<uint8>& Bytes)
    {
        Edits.FindOrAdd(Index).Add({int32(At - Inventory.Exports[Index].Offset), int32(Size), Bytes});
    }
    void Integer(int32 Index, std::size_t At, int32 Value)
    {
        TArray<uint8> Bytes;
        for (int32 I = 0; I < 4; ++I)
            Bytes.Add(uint8(uint32(Value) >> (I * 8)));
        Edit(Index, At, 4, Bytes);
    }
    void Value(int32 Index, const FPropertyEnvelope& Property, const TArray<uint8>& Bytes)
    {
        Edit(Index, Property.PayloadOffset, Property.Size, Bytes);
        if (Property.Size != std::size_t(Bytes.Num()))
            Integer(Index, Property.TagOffset + 16, Bytes.Num());
    }
    bool Finish(const FString& RuntimePackage, const TMap<FString, FString>& PackageMap, FString& Error)
    {
        TMap<int32, TArray<uint8>> Replacements;
        for (auto& Item : Edits)
        {
            const auto& E = Inventory.Exports[Item.Key];
            TArray<uint8> Payload;
            Payload.Append(B.data() + E.Offset, int32(E.Size));
            Item.Value.Sort([](const FEdit& A, const FEdit& C) { return A.At > C.At; });
            int32 Previous = Payload.Num();
            for (const auto& Change : Item.Value)
            {
                if (Change.At < 0 || Change.Size < 0 || Change.At > Previous || Change.Size > Previous - Change.At)
                {
                    Error = TEXT("Overlapping or invalid preview bootstrap edits.");
                    return false;
                }
                Payload.RemoveAt(Change.At, Change.Size, false);
                if (Change.Bytes.Num())
                    Payload.Insert(Change.Bytes.GetData(), Change.Bytes.Num(), Change.At);
                Previous = Change.At;
            }
            Replacements.Add(Item.Key, MoveTemp(Payload));
        }
        if (!P4ReplaceGamePreviewExports(Data.Header, Data.Body, Replacements, Error))
            return false;
        std::map<std::string, std::string> Names;
        for (const auto& Old : Inventory.Names)
        {
            const FString OldName(UTF8_TO_TCHAR(Old.c_str()));
            FString NewName = OldName;
            for (const auto& Mapping : PackageMap)
            {
                const FString Leaf = FPackageName::GetShortName(Mapping.Key);
                const FString NewLeaf = FPackageName::GetShortName(Mapping.Value);
                if (OldName == Mapping.Key)
                    NewName = Mapping.Value;
                else if (OldName.StartsWith(Mapping.Key + TEXT(".") + Leaf, ESearchCase::CaseSensitive))
                    NewName = Mapping.Value + TEXT(".") + NewLeaf + OldName.Mid(Mapping.Key.Len() + 1 + Leaf.Len());
                else if (OldName.StartsWith(Mapping.Key + TEXT("."), ESearchCase::CaseSensitive))
                    NewName = Mapping.Value + OldName.Mid(Mapping.Key.Len());
                else if (OldName == Leaf)
                    NewName = NewLeaf;
            }
            if (NewName != OldName)
                Names.emplace(Old, TCHAR_TO_UTF8(*NewName));
        }
        std::vector<uint8> Header(Data.Header.GetData(), Data.Header.GetData() + Data.Header.Num());
        std::vector<uint8> Body(Data.Body.GetData(), Data.Body.GetData() + Data.Body.Num());
        P4MapKit::Export::RewrittenPackage Renamed;
        std::string Why;
        if (!P4MapKit::Export::RewriteCookedNames(Header, Body, Names, Renamed, Why))
        {
            Error = TEXT("Preview bootstrap rename refused: ") + FString(UTF8_TO_TCHAR(Why.c_str()));
            return false;
        }
        Data.Header.Reset(int32(Renamed.Header.size()));
        Data.Header.Append(Renamed.Header.data(), int32(Renamed.Header.size()));
        Data.Body.Reset(int32(Renamed.Body.size()));
        Data.Body.Append(Renamed.Body.data(), int32(Renamed.Body.size()));
        if (!P4SetGamePreviewPackageName(Data.Header, Data.Body, RuntimePackage, Error))
            return false;
        const std::vector<uint8> CheckHeader(Data.Header.GetData(), Data.Header.GetData() + Data.Header.Num());
        const auto Check = ReadPackageHeader(CheckHeader, Data.Body.Num());
        if (!Check.Error.Code.empty() || Check.Package != TCHAR_TO_UTF8(*RuntimePackage))
        {
            Error = TEXT("Preview bootstrap owned package name failed readback.");
            return false;
        }
        Data.Package = RuntimePackage;
        return true;
    }
};

TArray<uint8> Integers(const std::vector<int32>& Values)
{
    TArray<uint8> Bytes;
    const auto Add = [&](uint32 Value)
    {
        for (int32 I = 0; I < 4; ++I)
            Bytes.Add(uint8(Value >> (I * 8)));
    };
    Add(uint32(Values.size()));
    for (const auto Value : Values)
        Add(uint32(Value));
    return Bytes;
}

bool ClearSettings(FPackage& Package, int32 Settings, bool Main, FString& PlacementList, FString& Error)
{
    const auto Props = Package.Properties(Settings);
    if (!Props.Error.Code.empty())
    {
        Error = TEXT("Preview world settings properties could not be read.");
        return false;
    }
    bool Camera = false;
    for (const auto& P : Props.Properties)
    {
        if (P.Name == "CameraClassOverride")
            Camera = true;
        if (P.Name == "FoliageGridData" || P.Name == "MapBuildData")
        {
            if (P.Type != "ObjectProperty" || P.Size != 4)
            {
                Error = TEXT("Preview world has an unsupported stock grid reference.");
                return false;
            }
            Package.Integer(Settings, P.PayloadOffset, 0);
        }
        else if (P.Name == "ActorPlacementInfoListAssetForHeroStory")
        {
            if (P.Type != "SoftObjectProperty")
            {
                Error = TEXT("Preview Hero placement reference has an unsupported layout.");
                return false;
            }
            const auto Found = std::find(Package.Inventory.Names.begin(), Package.Inventory.Names.end(), "None");
            if (Found == Package.Inventory.Names.end())
            {
                Error = TEXT("Preview world has no empty-name identity.");
                return false;
            }
            TArray<uint8> Value;
            const int32 None = int32(Found - Package.Inventory.Names.begin());
            for (int32 I = 0; I < 12; ++I)
                Value.Add(I < 4 ? uint8(uint32(None) >> (I * 8)) : 0);
            Package.Value(Settings, P, Value);
        }
        else if (P.Name == "ActorPlacementInfoListAssets")
        {
            if (P.Type != "ArrayProperty" || P.Detail.size() != 1)
            {
                Error = TEXT("Preview placement list has an unsupported layout.");
                return false;
            }
            FByteReader R(Package.B, P.PayloadOffset, P.PayloadOffset + P.Size);
            std::size_t Count = 0;
            if (!R.Count(Count, 1000))
            {
                Error = TEXT("Preview placement list count is invalid.");
                return false;
            }
            const auto FirstAt = R.Tell();
            std::string Path;
            if (Count && P.Detail[0] == "SoftObjectProperty")
            {
                std::string SubPath;
                if (!R.Name(Package.Inventory.Names, Path) || !R.String(SubPath) || !SubPath.empty())
                {
                    Error = TEXT("Preview placement soft reference is invalid.");
                    return false;
                }
            }
            else if (Count && P.Detail[0] == "ObjectProperty")
            {
                int32 Ref = 0;
                if (!R.I32(Ref) || !Package.Inventory.Reference(Ref, Path))
                {
                    Error = TEXT("Preview placement object reference is invalid.");
                    return false;
                }
            }
            else if (Count)
            {
                Error = TEXT("Preview placement list element type is unsupported.");
                return false;
            }
            if (Main && Count == 0)
            {
                Error = TEXT("Preview template has no placement list slot for its base.");
                return false;
            }
            TArray<uint8> Value = Integers({});
            if (Main)
            {
                Value[0] = 1;
                Value.Append(Package.B.data() + FirstAt, int32(R.Tell() - FirstAt));
                PlacementList = FString(UTF8_TO_TCHAR(Path.c_str()));
                const int32 Dot = PlacementList.Find(TEXT("."));
                if (Dot != INDEX_NONE)
                    PlacementList.LeftInline(Dot);
                if (!FPackageName::IsValidLongPackageName(PlacementList))
                {
                    Error = TEXT("Preview base placement list is not a package reference.");
                    return false;
                }
            }
            Package.Value(Settings, P, Value);
        }
    }
    if (Main && (!Camera || PlacementList.IsEmpty()))
    {
        Error = TEXT("Preview template lacks its game camera or placement list.");
        return false;
    }
    return true;
}

bool PruneLevel(FPackage& Package, bool Player, TArray<FString>& Runtime, int32& Avatar, FString& Error)
{
    const int32 Level = Package.UniqueClass(TEXT("/Script/Engine.Level"));
    if (Level == INDEX_NONE)
    {
        Error = TEXT("Preview bootstrap requires exactly one persistent level.");
        return false;
    }
    const auto Props = Package.Properties(Level);
    const auto& E = Package.Inventory.Exports[Level];
    if (!Props.Error.Code.empty())
    {
        Error = TEXT("Preview level properties are invalid.");
        return false;
    }
    const auto Native = ReadLevelNative(Package.B, Props.End, E.Offset + E.Size);
    if (!Native.Error.Code.empty() || Native.ModelComponents.size() != 0 || Native.Model <= 0)
    {
        Error = TEXT("Preview template has unsupported level-native geometry.");
        return false;
    }
    const int32 Model = Native.Model - 1;
    if (std::size_t(Model) >= Package.Inventory.Exports.size())
    {
        Error = TEXT("Preview template BSP model is invalid.");
        return false;
    }
    const auto ModelProps = Package.Properties(Model);
    const auto& ModelExport = Package.Inventory.Exports[Model];
    const auto ModelNative = ReadModelNative(Package.B, ModelProps.End, ModelExport.Offset + ModelExport.Size);
    if (!ModelProps.Error.Code.empty() || !ModelNative.Error.Code.empty() || !ModelNative.Empty())
    {
        Error = TEXT("Preview bootstrap refuses original BSP geometry.");
        return false;
    }
    std::vector<int32> Keep;
    int32 Settings = 0, Rules = 0, Nav = 0, Bounds = 0, Environments = 0, Skies = 0, Captains = 0;
    for (const int32 Ref : Native.Actors)
    {
        if (Ref <= 0)
            continue;
        if (std::size_t(Ref) > Package.Inventory.Exports.size())
        {
            Error = TEXT("Preview actor reference is invalid.");
            return false;
        }
        const int32 I = Ref - 1;
        const FString Class = Package.Class(I);
        bool Retain = false;
        if (Class == TEXT(P4_GAME_SCRIPT_PACKAGE ".MyWorldSettings"))
        {
            ++Settings;
            Retain = true;
        }
        else if (Player && Class.EndsWith(TEXT(".GAvatar_C")))
        {
            ++Captains;
            Avatar = I;
            Retain = true;
        }
        else if (!Player)
        {
            if (Class.StartsWith(TEXT(P4_GAME_SCRIPT_PACKAGE ".RecastNavMesh")))
            {
                ++Nav;
                Retain = true;
            }
            else if (Class == TEXT("/Script/NavigationSystem.NavMeshBoundsVolume"))
            {
                ++Bounds;
                Retain = true;
            }
            else if (Class.EndsWith(TEXT(".BP_CarrotGameRuleStory_C")))
            {
                ++Rules;
                Retain = true;
            }
            else if (Class.EndsWith(TEXT(".BP_EnvController_C")))
            {
                ++Environments;
                Retain = true;
            }
            else if (Class.EndsWith(TEXT(".BP_CarrotSkySphere_C")))
            {
                ++Skies;
                Retain = true;
            }
        }
        if (Retain)
        {
            Keep.push_back(Ref);
            Runtime.Add(Package.Name(I));
        }
    }
    if (Settings != 1 || Keep.empty() ||
        Package.Class(Keep[0] - 1) != TEXT(P4_GAME_SCRIPT_PACKAGE ".MyWorldSettings") ||
        (Player ? Captains != 1 : Rules != 1 || Nav != 6 || Bounds < 1 || Environments != 1 || Skies != 1))
    {
        Error = TEXT("Preview bootstrap runtime actor census differs from its qualified recipe.");
        return false;
    }
    FByteReader R(Package.B, Props.End, E.Offset + E.Size);
    if (!LevelNative::ObjectId(R))
    {
        Error = TEXT("Preview level object identifier is invalid.");
        return false;
    }
    const auto ArrayAt = R.Tell();
    std::vector<int32> Before;
    if (!LevelNative::References(R, Before))
    {
        Error = TEXT("Preview actor list could not be read.");
        return false;
    }
    Package.Edit(Level, ArrayAt, R.Tell() - ArrayAt, Integers(Keep));
    std::string URL;
    std::size_t Options = 0;
    for (int32 I = 0; I < 4; ++I)
        if (!R.String(URL))
        {
            Error = TEXT("Preview level URL is invalid.");
            return false;
        }
    if (!R.Count(Options, 4096))
        return false;
    for (std::size_t I = 0; I < Options; ++I)
        if (!R.String(URL))
            return false;
    std::vector<int32> Components;
    if (!R.Skip(12) || !LevelNative::References(R, Components))
        return false;
    Package.Integer(Level, R.Tell(), 0); // native LevelScriptActor
    for (const auto& P : Props.Properties)
        if (P.Name == "LevelScriptActor" || P.Name == "MapBuildData")
        {
            if (P.Type != "ObjectProperty" || P.Size != 4)
            {
                Error = TEXT("Preview level metadata is unsupported.");
                return false;
            }
            Package.Integer(Level, P.PayloadOffset, 0);
        }
    return true;
}

bool MoveCaptain(FPackage& Package, int32 Avatar, const FTransform& Start, FString& Error)
{
    const auto Props = Package.Properties(Avatar);
    int32 Root = INDEX_NONE;
    for (const auto& P : Props.Properties)
        if (P.Name == "RootComponent" && P.Type == "ObjectProperty" && P.Size == 4)
        {
            FByteReader R(Package.B, P.PayloadOffset, P.PayloadOffset + 4);
            int32 Ref = 0;
            if (R.I32(Ref) && Ref > 0 && std::size_t(Ref) <= Package.Inventory.Exports.size())
                Root = Ref - 1;
        }
    if (Root == INDEX_NONE || Package.Inventory.Exports[Root].Outer != Avatar + 1)
    {
        Error = TEXT("Preview captain has no owned root component.");
        return false;
    }
    const auto RootProps = Package.Properties(Root);
    bool Moved = false, Rotated = false;
    for (const auto& P : RootProps.Properties)
        if (P.Name == "RelativeLocation" || P.Name == "RelativeRotation")
        {
            if (P.Type != "StructProperty" || P.Detail.size() != 1 || P.Size != 12 ||
                P.Detail[0] != (P.Name == "RelativeLocation" ? "Vector" : "Rotator"))
            {
                Error = TEXT("Preview captain transform has an unsupported layout.");
                return false;
            }
            const FVector Position = Start.GetLocation();
            const FRotator Rotation = Start.Rotator();
            const float Values[] = {P.Name == "RelativeLocation" ? Position.X : Rotation.Pitch,
                P.Name == "RelativeLocation" ? Position.Y : Rotation.Yaw,
                P.Name == "RelativeLocation" ? Position.Z : Rotation.Roll};
            TArray<uint8> Bytes;
            Bytes.Append(reinterpret_cast<const uint8*>(Values), 12);
            Package.Value(Root, P, Bytes);
            if (P.Name == "RelativeLocation")
                Moved = true;
            else
                Rotated = true;
        }
    if (!Moved || !Rotated)
    {
        Error = TEXT("Preview captain root has no serialized location/rotation.");
        return false;
    }
    return true;
}

bool Streams(
    FPackage& Package, const FP4GamePreviewTemplate& Template, FString& Geometry, FString& Player, FString& Error)
{
    const int32 World = Package.UniqueClass(TEXT("/Script/Engine.World"));
    if (World == INDEX_NONE)
    {
        Error = TEXT("Preview template requires one world export.");
        return false;
    }
    const auto Props = Package.Properties(World);
    const auto& E = Package.Inventory.Exports[World];
    const auto Native = ReadWorldNative(Package.B, Props.End, E.Offset + E.Size);
    if (!Props.Error.Code.empty() || !Native.Error.Code.empty())
    {
        Error = TEXT("Preview world-native data is unsupported.");
        return false;
    }
    std::vector<int32> Keep;
    for (int32 Ref : Native.StreamingLevels)
    {
        if (Ref <= 0 || std::size_t(Ref) > Package.Inventory.Exports.size())
        {
            Error = TEXT("Preview stream reference is invalid.");
            return false;
        }
        const int32 Stream = Ref - 1;
        const auto StreamProps = Package.Properties(Stream);
        if (!StreamProps.Error.Code.empty())
        {
            Error = TEXT("Preview streaming properties are invalid.");
            return false;
        }
        FString Path;
        for (const auto& P : StreamProps.Properties)
            if (P.Name == "WorldAsset" && P.Type == "SoftObjectProperty")
            {
                FByteReader R(Package.B, P.PayloadOffset, P.PayloadOffset + P.Size);
                std::string Name, Sub;
                if (!R.Name(Package.Inventory.Names, Name) || !R.String(Sub) || !Sub.empty())
                {
                    Error = TEXT("Preview streaming world path is invalid.");
                    return false;
                }
                Path = FString(UTF8_TO_TCHAR(Name.c_str()));
                const int32 Dot = Path.Find(TEXT("."));
                if (Dot != INDEX_NONE)
                    Path.LeftInline(Dot);
            }
        if (Path == Template.GeometryStream || Path == Template.PlayerStream)
        {
            FString& Destination = Path == Template.PlayerStream ? Player : Geometry;
            if (!Destination.IsEmpty())
            {
                Error = TEXT("Preview template has ambiguous geometry/player streams.");
                return false;
            }
            Destination = Path;
            if (!Package.Class(Stream).EndsWith(TEXT(".LevelStreamingAlwaysLoaded")))
            {
                Error = TEXT("Preview geometry/player stream is not always loaded.");
                return false;
            }
            Keep.push_back(Ref);
        }
    }
    if (Geometry.IsEmpty() || Player.IsEmpty() || Keep.size() != 2)
    {
        Error = TEXT("Preview template does not have exactly one geometry and player stream.");
        return false;
    }
    FByteReader R(Package.B, Props.End, E.Offset + E.Size);
    int32 Level = 0;
    if (!LevelNative::ObjectId(R) || !R.I32(Level))
        return false;
    const auto ExtraAt = R.Tell();
    std::vector<int32> Extra;
    if (!LevelNative::References(R, Extra))
        return false;
    Package.Edit(World, ExtraAt, R.Tell() - ExtraAt, Integers({}));
    const auto StreamsAt = R.Tell();
    if (!LevelNative::References(R, Extra))
        return false;
    Package.Edit(World, StreamsAt, R.Tell() - StreamsAt, Integers(Keep));
    return true;
}

bool Base(FPackage& Objects, FPackage& Listing, const FP4GamePreviewTemplate& Template, const FTransform& Start,
    FString& Error)
{
    if (Objects.Inventory.Exports.size() != 1 || Listing.Inventory.Exports.size() != 1)
    {
        Error = TEXT("Preview base templates must each contain one asset export.");
        return false;
    }
    const auto Props = Objects.Properties(0);
    const FPropertyEnvelope* Array = nullptr;
    for (const auto& P : Props.Properties)
        if (P.Name == "ActorGeneratorList")
            Array = &P;
    if (!Array || !Props.Error.Code.empty() || Array->Type != "ArrayProperty" || Array->Detail.size() != 1 ||
        Array->Detail[0] != "StructProperty")
    {
        Error = TEXT("Preview base generator array is unsupported.");
        return false;
    }
    FByteReader R(Objects.B, Array->PayloadOffset, Array->PayloadOffset + Array->Size);
    std::size_t Count = 0;
    FPropertyEnvelope Inner;
    if (!R.Count(Count, 10000) || !ReadPropertyEnvelope(R, Objects.Inventory.Names, Objects.B.size(), Inner) ||
        Inner.Type != "StructProperty" || Inner.Detail.size() != 1 || Inner.Detail[0] != "ActorGenerator" ||
        R.Tell() != Array->PayloadOffset + Array->Size)
    {
        Error = TEXT("Preview base generator descriptor is unsupported.");
        return false;
    }
    TArray<uint8> Rows;
    std::size_t At = Inner.PayloadOffset;
    TSet<FString> Selected;
    for (std::size_t I = 0; I < Count; ++I)
    {
        const auto Row = ReadPropertyEnvelopes(
            Objects.B, Objects.Inventory.Names, At, Inner.PayloadOffset + Inner.Size);
        if (!Row.Error.Code.empty() || Row.End <= At)
        {
            Error = TEXT("Preview base generator row is invalid.");
            return false;
        }
        FString RowClass;
        for (const auto& Property : Row.Properties)
            if (Property.Name == "SoftRefActorClass")
            {
                FByteReader Path(Objects.B, Property.PayloadOffset, Property.PayloadOffset + Property.Size);
                std::string Name;
                std::string Sub;
                if (!Path.Name(Objects.Inventory.Names, Name) || !Path.String(Sub) || !Sub.empty())
                {
                    Error = TEXT("Preview base class reference is invalid.");
                    return false;
                }
                RowClass = FString(UTF8_TO_TCHAR(Name.c_str()));
            }
        if (Template.BaseClasses.Contains(RowClass) && !Selected.Contains(RowClass))
        {
            Selected.Add(RowClass);
            TArray<uint8> Bytes;
            Bytes.Append(Objects.B.data() + At, int32(Row.End - At));
            const auto Patch = [&](std::size_t Offset, const void* Data, int32 Size)
            {
                if (Offset < At || Offset + Size > Row.End)
                    return false;
                FMemory::Memcpy(Bytes.GetData() + Offset - At, Data, Size);
                return true;
            };
            bool Initial = false, Transform = false, NavPosition = false, Unique = false;
            const FVector P = Start.GetLocation();
            const FRotator A = Start.Rotator();
            const FQuat Q = Start.GetRotation();
            const float Position[] = {P.X, P.Y, P.Z};
            const float Rotation[] = {Q.X, Q.Y, Q.Z, Q.W};
            const float Euler[] = {A.Pitch, A.Yaw, A.Roll};
            for (const auto& Property : Row.Properties)
            {
                if (Property.Name == "InitTransform" || Property.Name == "Transform")
                {
                    const auto Members = ReadPropertyEnvelopes(Objects.B, Objects.Inventory.Names,
                        Property.PayloadOffset, Property.PayloadOffset + Property.Size);
                    bool Translation = false, Rotated = false;
                    if (!Members.Error.Code.empty())
                    {
                        Error = TEXT("Preview base transform is invalid.");
                        return false;
                    }
                    for (const auto& M : Members.Properties)
                    {
                        if (M.Name == "Translation" && M.Size == 12)
                            Translation = Patch(M.PayloadOffset, Position, 12);
                        if (M.Name == "Rotation" && M.Size == 16)
                            Rotated = Patch(M.PayloadOffset, Rotation, 16);
                    }
                    if (Property.Name == "InitTransform")
                        Initial = Translation && Rotated;
                    else
                        Transform = Translation && Rotated;
                }
                else if (Property.Name == "LastNavPos" && Property.Size == 12)
                    NavPosition = Patch(Property.PayloadOffset, Position, 12);
                else if (Property.Name == "LastNavRotator" && Property.Size == 12)
                    Patch(Property.PayloadOffset, Euler, 12);
                else if (Property.Name == "GenerateInfo")
                {
                    const auto Members = ReadPropertyEnvelopes(Objects.B, Objects.Inventory.Names,
                        Property.PayloadOffset, Property.PayloadOffset + Property.Size);
                    if (!Members.Error.Code.empty())
                    {
                        Error = TEXT("Preview base generator identity is invalid.");
                        return false;
                    }
                    const FGuid Guid = FGuid::NewGuid();
                    const uint64 Id = ((uint64(Guid.A) << 32 | Guid.B) & 0x7fffffffffffffffull) | 1;
                    for (const auto& M : Members.Properties)
                        if (M.Name == "DebugUniqueId" && M.Size == 8)
                            Unique = Patch(M.PayloadOffset, &Id, 8);
                }
            }
            if (!Initial || !Transform || !NavPosition || !Unique)
            {
                Error = TEXT("Preview pod/camp recipe differs from the qualified templates.");
                return false;
            }
            Rows.Append(Bytes);
        }
        At = Row.End;
    }
    if (Selected.Num() != Template.BaseClasses.Num() || At != Inner.PayloadOffset + Inner.Size)
    {
        Error = TEXT("Preview base row census differs.");
        return false;
    }
    TArray<uint8> Value = Integers({});
    Value[0] = uint8(Selected.Num());
    Value.Append(Objects.B.data() + Array->PayloadOffset + 4, int32(Inner.PayloadOffset - Array->PayloadOffset - 4));
    const int32 SizeAt = int32(Inner.TagOffset + 16 - Array->PayloadOffset);
    for (int32 I = 0; I < 4; ++I)
        Value[SizeAt + I] = uint8(uint32(Rows.Num()) >> (I * 8));
    Value.Append(Rows);
    Objects.Value(0, *Array, Value);

    const auto ListProps = Listing.Properties(0);
    bool Listed = false;
    for (const auto& PList : ListProps.Properties)
        if (PList.Name == "AssetList" && PList.Type == "ArrayProperty" && PList.Detail.size() == 1 &&
            PList.Detail[0] == "ObjectProperty")
        {
            FByteReader List(Listing.B, PList.PayloadOffset, PList.PayloadOffset + PList.Size);
            std::size_t Entries = 0;
            if (!List.Count(Entries, 10000))
            {
                Error = TEXT("Preview base asset list is invalid.");
                return false;
            }
            int32 SelectedRef = 0;
            for (std::size_t I = 0; I < Entries; ++I)
            {
                int32 Ref = 0;
                std::string Path;
                if (!List.I32(Ref) || !Listing.Inventory.Reference(Ref, Path))
                    return false;
                if (FString(UTF8_TO_TCHAR(Path.c_str())) == P4ObjectPathOf(Template.Base))
                {
                    if (SelectedRef)
                    {
                        Error = TEXT("Preview base asset list contains a duplicate object list.");
                        return false;
                    }
                    SelectedRef = Ref;
                }
            }
            if (!SelectedRef || List.Tell() != PList.PayloadOffset + PList.Size)
            {
                Error = TEXT("Preview base asset list does not name its object list.");
                return false;
            }
            Listing.Value(0, PList, Integers({SelectedRef}));
            Listed = true;
        }
    if (!Listed || !ListProps.Error.Code.empty())
    {
        Error = TEXT("Preview base asset list layout is unsupported.");
        return false;
    }
    return true;
}

bool CheckBaseGround(const FP4GamePreviewBootstrapPackage& Data, const FVector& Ground, int32 Rows, FString& Error)
{
    const std::vector<uint8> Header(Data.Header.GetData(), Data.Header.GetData() + Data.Header.Num());
    const std::vector<uint8> Body(Data.Body.GetData(), Data.Body.GetData() + Data.Body.Num());
    const FPackageInventory Package = ReadPackageHeader(Header, Body.size());
    const auto Fail = [&]()
    {
        Error = TEXT("Preview pod/camp ground anchors failed cooked readback.");
        return false;
    };
    if (!Package.Error.Code.empty() || Package.Exports.size() != 1)
        return Fail();
    const auto& Export = Package.Exports[0];
    const auto Properties = ReadPropertyEnvelopes(Body, Package.Names, Export.Offset, Export.Offset + Export.Size);
    if (!Properties.Error.Code.empty())
        return Fail();
    const FPropertyEnvelope* Array = nullptr;
    for (const auto& P : Properties.Properties)
        if (P.Name == "ActorGeneratorList")
            Array = &P;
    if (!Array)
        return Fail();
    FByteReader R(Body, Array->PayloadOffset, Array->PayloadOffset + Array->Size);
    std::size_t Count = 0;
    FPropertyEnvelope Inner;
    if (!R.Count(Count, std::size_t(Rows)) || Count != std::size_t(Rows) ||
        !ReadPropertyEnvelope(R, Package.Names, Body.size(), Inner) || Inner.Type != "StructProperty" ||
        R.Tell() != Array->PayloadOffset + Array->Size)
        return Fail();
    const float Expected[] = {Ground.X, Ground.Y, Ground.Z};
    const auto AtGround = [&](const FPropertyEnvelope& Value)
    {
        return Value.Size == sizeof(Expected) && Value.PayloadOffset <= Body.size() &&
            Value.Size <= Body.size() - Value.PayloadOffset &&
            FMemory::Memcmp(Body.data() + Value.PayloadOffset, Expected, sizeof(Expected)) == 0;
    };
    std::size_t At = Inner.PayloadOffset;
    for (std::size_t I = 0; I < Count; ++I)
    {
        const auto Row = ReadPropertyEnvelopes(Body, Package.Names, At, Inner.PayloadOffset + Inner.Size);
        if (!Row.Error.Code.empty() || Row.End <= At)
            return Fail();
        bool Initial = false, Transform = false, Nav = false;
        for (const auto& P : Row.Properties)
        {
            if (P.Name == "LastNavPos")
                Nav = AtGround(P);
            else if (P.Name == "InitTransform" || P.Name == "Transform")
            {
                const auto Members = ReadPropertyEnvelopes(
                    Body, Package.Names, P.PayloadOffset, P.PayloadOffset + P.Size);
                if (!Members.Error.Code.empty())
                    return Fail();
                for (const auto& M : Members.Properties)
                    if (M.Name == "Translation")
                    {
                        if (P.Name == "InitTransform")
                            Initial = AtGround(M);
                        else
                            Transform = AtGround(M);
                    }
            }
        }
        if (!Initial || !Transform || !Nav)
            return Fail();
        At = Row.End;
    }
    return At == Inner.PayloadOffset + Inner.Size ? true : Fail();
}

bool Arrival(FPackage& Demo, FPackage& Listing, const FP4GamePreviewTemplate& Template, const FString& AreaName,
    FString& Renamed, FString& Error)
{
    if (Demo.Inventory.Exports.size() != 1 || Listing.Inventory.Exports.size() != 1 ||
        Demo.Class(0) != TEXT(P4_GAME_SCRIPT_PACKAGE ".DemoPlayParam") ||
        Listing.Class(0) != TEXT(P4_GAME_SCRIPT_PACKAGE ".DemoPlayParamListAsset"))
    {
        Error = TEXT("Preview arrival requires the genuine CourseIn and demo-list classes.");
        return false;
    }
    const auto Properties = Demo.Properties(0);
    bool Message = false, Sequence = false, Relative = false;
    for (const auto& P : Properties.Properties)
    {
        if (P.Name == "OverrideMsgId")
        {
            if (P.Type != "StrProperty")
            {
                Error = TEXT("Preview CourseIn message has an unsupported type.");
                return false;
            }
            FByteReader Original(Demo.B, P.PayloadOffset, P.PayloadOffset + P.Size);
            std::string Text;
            if (!Original.String(Text) || Original.Tell() != P.PayloadOffset + P.Size)
            {
                Error = TEXT("Preview CourseIn message could not be read.");
                return false;
            }
            Renamed = P4GamePreviewRename(FString(UTF8_TO_TCHAR(Text.c_str())), Template.ArrivalToken, AreaName);
            if (Renamed == FString(UTF8_TO_TCHAR(Text.c_str())))
            {
                Error = TEXT("Preview CourseIn message does not name its template area.");
                return false;
            }
            std::vector<uint8> Raw;
            P4MapKit::Export::PackageRewriteDetail::String(Raw, TCHAR_TO_UTF8(*Renamed));
            TArray<uint8> Value;
            Value.Append(Raw.data(), int32(Raw.size()));
            Demo.Value(0, P, Value);
            Message = true;
        }
        else if (P.Name == "SequenceAsset")
        {
            FByteReader Original(Demo.B, P.PayloadOffset, P.PayloadOffset + P.Size);
            std::string Path, Sub;
            Sequence = P.Type == "SoftObjectProperty" && Original.Name(Demo.Inventory.Names, Path) &&
                Original.String(Sub) && Sub.empty() &&
                FPackageName::IsValidLongPackageName(
                    FPackageName::ObjectPathToPackageName(UTF8_TO_TCHAR(Path.c_str())));
        }
        else if (P.Name == "BindBaseName")
        {
            FByteReader Original(Demo.B, P.PayloadOffset, P.PayloadOffset + P.Size);
            std::string Name;
            Relative = P.Type == "StrProperty" && Original.String(Name) && !Name.empty();
        }
    }
    if (!Properties.Error.Code.empty() || !Message || !Sequence || !Relative)
    {
        Error = TEXT("Preview CourseIn does not match its established relative arrival recipe.");
        return false;
    }

    const auto ListProperties = Listing.Properties(0);
    bool Found = false;
    for (const auto& P : ListProperties.Properties)
        if (P.Name == "AssetList" && P.Type == "ArrayProperty" && P.Detail.size() == 1 &&
            P.Detail[0] == "ObjectProperty")
        {
            FByteReader R(Listing.B, P.PayloadOffset, P.PayloadOffset + P.Size);
            std::size_t Count = 0;
            if (!R.Count(Count, 1000))
            {
                Error = TEXT("Preview arrival list count is invalid.");
                return false;
            }
            int32 Match = 0;
            for (std::size_t I = 0; I < Count; ++I)
            {
                int32 Ref = 0;
                std::string Path;
                if (!R.I32(Ref) || !Listing.Inventory.Reference(Ref, Path))
                {
                    Error = TEXT("Preview arrival list contains an invalid reference.");
                    return false;
                }
                if (FString(UTF8_TO_TCHAR(Path.c_str())) == P4ObjectPathOf(Template.Demo))
                {
                    if (Match)
                    {
                        Error = TEXT("Preview arrival list repeats its CourseIn demo.");
                        return false;
                    }
                    Match = Ref;
                }
            }
            if (!Match || R.Tell() != P.PayloadOffset + P.Size)
            {
                Error = TEXT("Preview arrival list does not contain its genuine day CourseIn demo.");
                return false;
            }
            Listing.Value(0, P, Integers({Match}));
            Found = true;
        }
    if (!Found || !ListProperties.Error.Code.empty())
    {
        Error = TEXT("Preview arrival list layout is unsupported.");
        return false;
    }
    return true;
}

bool CheckArrival(const FP4GamePreviewBootstrapPackage& Demo, const FP4GamePreviewBootstrapPackage& List,
    const FString& Message, FString& Error)
{
    for (const auto* Item : {&Demo, &List})
    {
        const std::vector<uint8> Header(Item->Header.GetData(), Item->Header.GetData() + Item->Header.Num());
        const std::vector<uint8> Body(Item->Body.GetData(), Item->Body.GetData() + Item->Body.Num());
        const auto Package = ReadPackageHeader(Header, Body.size());
        if (!Package.Error.Code.empty() || Package.Exports.size() != 1 ||
            Package.Package != TCHAR_TO_UTF8(*Item->Package))
        {
            Error = TEXT("Preview arrival owned package failed readback.");
            return false;
        }
        const auto& E = Package.Exports[0];
        const auto Properties = ReadPropertyEnvelopes(Body, Package.Names, E.Offset, E.Offset + E.Size);
        if (!Properties.Error.Code.empty())
        {
            Error = TEXT("Preview arrival properties failed readback.");
            return false;
        }
        bool Verified = false;
        for (const auto& P : Properties.Properties)
        {
            if (Item == &Demo && P.Name == "OverrideMsgId" && P.Type == "StrProperty")
            {
                FByteReader R(Body, P.PayloadOffset, P.PayloadOffset + P.Size);
                std::string Text;
                Verified = R.String(Text) && Text == TCHAR_TO_UTF8(*Message);
            }
            else if (Item == &List && P.Name == "AssetList" && P.Type == "ArrayProperty")
            {
                FByteReader R(Body, P.PayloadOffset, P.PayloadOffset + P.Size);
                std::size_t Count = 0;
                int32 Ref = 0;
                std::string Path;
                Verified = R.Count(Count, 1) && Count == 1 && R.I32(Ref) && Package.Reference(Ref, Path) &&
                    R.Tell() == P.PayloadOffset + P.Size && Path == TCHAR_TO_UTF8(*P4ObjectPathOf(Demo.Package));
            }
        }
        if (!Verified)
        {
            Error = TEXT("Preview arrival message/list linkage failed readback.");
            return false;
        }
    }
    return true;
}
} //namespace

bool P4GamePreviewBaseAnchor(
    const FTransform& Start, const FVector& FloorPoint, const FVector& FloorNormal, FTransform& Out, FString& Error)
{
    Error.Reset();
    Out = FTransform::Identity;
    if (Start.ContainsNaN() || FloorPoint.ContainsNaN() || FloorNormal.ContainsNaN() ||
        FloorPoint.GetAbsMax() > 1000000.f || FloorNormal.GetSafeNormal().Z < 0.71f ||
        !FMath::IsNearlyEqual(FloorPoint.X, Start.GetLocation().X, 1.f) ||
        !FMath::IsNearlyEqual(FloorPoint.Y, Start.GetLocation().Y, 1.f) || FloorPoint.Z > Start.GetLocation().Z + 1.f)
    {
        Error = TEXT("The preview base needs the selected start's validated walkable ground contact.");
        return false;
    }
    Out = FTransform(FRotator(0, Start.Rotator().Yaw, 0), FloorPoint, FVector::OneVector);
    return true;
}

bool P4BuildGamePreviewBootstrap(const FP4KadanImportSettings& Settings, const FP4GamePreviewTemplate& Template,
    const FString& PreviewRoot, const FString& WorldName, const FString& AreaName, const FString& AuthoredWorldPackage,
    const FTransform& Start, const FVector& FloorPoint, const FVector& FloorNormal, FP4GamePreviewBootstrapResult& Out,
    FString& Error)
{
    Out = FP4GamePreviewBootstrapResult();
    if (!IsInGameThread() || AreaName.IsEmpty() ||
        !PreviewRoot.StartsWith(TEXT(P4_PREVIEW_PACKAGE_ROOT), ESearchCase::CaseSensitive) ||
        !FPackageName::IsValidLongPackageName(PreviewRoot / WorldName) ||
        !AuthoredWorldPackage.StartsWith(PreviewRoot + TEXT("/"), ESearchCase::CaseSensitive) || Start.ContainsNaN() ||
        Start.GetLocation().GetAbsMax() > 1000000.0f)
    {
        Error = TEXT("Invalid private preview bootstrap request.");
        return false;
    }
    FTransform BaseAnchor;
    if (!P4GamePreviewBaseAnchor(Start, FloorPoint, FloorNormal, BaseAnchor, Error))
        return false;
    FPackage World, Player, BaseObjects, BaseListing, Demo, DemoListing;
    if (!World.Load(Settings, Template.World, Error))
        return false;
    FString GeometrySource, PlayerSource;
    if (!Streams(World, Template, GeometrySource, PlayerSource, Error) || !Player.Load(Settings, PlayerSource, Error))
        return false;
    int32 Avatar = INDEX_NONE, Unused = INDEX_NONE;
    TArray<FString> Actors;
    if (!PruneLevel(World, false, Actors, Unused, Error) || !PruneLevel(Player, true, Actors, Avatar, Error) ||
        !MoveCaptain(Player, Avatar, Start, Error))
        return false;
    FString PlacementSource, Ignored;
    const int32 WorldSettings = World.UniqueClass(TEXT(P4_GAME_SCRIPT_PACKAGE ".MyWorldSettings"));
    const int32 PlayerSettings = Player.UniqueClass(TEXT(P4_GAME_SCRIPT_PACKAGE ".MyWorldSettings"));
    if (WorldSettings == INDEX_NONE || PlayerSettings == INDEX_NONE ||
        !ClearSettings(World, WorldSettings, true, PlacementSource, Error) ||
        !ClearSettings(Player, PlayerSettings, false, Ignored, Error))
        return false;
    if (!BaseObjects.Load(Settings, Template.Base, Error) || !BaseListing.Load(Settings, Template.BaseList, Error) ||
        !Base(BaseObjects, BaseListing, Template, BaseAnchor, Error))
        return false;
    FString Message;
    if (!Demo.Load(Settings, Template.Demo, Error) || !DemoListing.Load(Settings, Template.DemoList, Error) ||
        !Arrival(Demo, DemoListing, Template, AreaName, Message, Error))
        return false;
    const auto Renamed = [&](const FString& Package, const FString& Token)
    { return PreviewRoot / P4GamePreviewRename(FPackageName::GetShortName(Package), Token, AreaName); };
    Out.WorldPackage = PreviewRoot / WorldName;
    Out.PlayerPackage = Renamed(PlayerSource, Template.WorldToken);
    const FString BasePackage = PreviewRoot / TEXT("AP_PreviewBase");
    const FString ListPackage = PreviewRoot / TEXT("APL_PreviewBase");
    const FString DemoPackage = Renamed(Template.Demo, Template.ArrivalToken);
    const FString DemoListPackage = Renamed(Template.DemoList, Template.ArrivalToken);
    TMap<FString, FString> Mapping;
    Mapping.Add(Template.World, Out.WorldPackage);
    Mapping.Add(GeometrySource, AuthoredWorldPackage);
    Mapping.Add(PlayerSource, Out.PlayerPackage);
    Mapping.Add(Template.Base, BasePackage);
    Mapping.Add(Template.BaseList, ListPackage);
    Mapping.Add(PlacementSource, ListPackage);
    Mapping.Add(Template.Demo, DemoPackage);
    Mapping.Add(Template.DemoList, DemoListPackage);
    // Generator SubLevelName is a bare FName used by the game's placement
    // manager. It must agree with the reserved scene, as in owned_names().
    Mapping.Add(Template.WorldToken, AreaName);
    Mapping.Add(Template.ArrivalToken, AreaName);
    if (!World.Finish(Out.WorldPackage, Mapping, Error) || !Player.Finish(Out.PlayerPackage, Mapping, Error) ||
        !BaseObjects.Finish(BasePackage, Mapping, Error) || !BaseListing.Finish(ListPackage, Mapping, Error) ||
        !Demo.Finish(DemoPackage, Mapping, Error) || !DemoListing.Finish(DemoListPackage, Mapping, Error) ||
        !CheckArrival(Demo.Data, DemoListing.Data, Message, Error) ||
        !CheckBaseGround(BaseObjects.Data, BaseAnchor.GetLocation(), Template.BaseClasses.Num(), Error))
        return false;
    Out.Packages.Add(MoveTemp(World.Data));
    Out.Packages.Add(MoveTemp(Player.Data));
    Out.Packages.Add(MoveTemp(BaseObjects.Data));
    Out.Packages.Add(MoveTemp(BaseListing.Data));
    Out.Packages.Add(MoveTemp(Demo.Data));
    Out.Packages.Add(MoveTemp(DemoListing.Data));
    Out.LoadAssets.Add(P4ObjectPathOf(DemoListPackage));
    Out.RuntimeActors = MoveTemp(Actors);
    Error.Reset();
    return true;
}

bool P4WriteGamePreviewBootstrap(const FP4GamePreviewBootstrapResult& Bootstrap, const FString& Stage,
    TArray<FString>& ExtraPackages, FString& Error)
{
    if (Bootstrap.Packages.Num() < 2 || Bootstrap.WorldPackage.IsEmpty())
    {
        Error = TEXT("Preview bootstrap is incomplete.");
        return false;
    }
    const FString Content = FPaths::ConvertRelativePathToFull(Stage / TEXT(P4_GAME_CONTENT_FOLDER));
    TSet<FString> Seen;
    for (const auto& Package : Bootstrap.Packages)
    {
        if (!Package.Package.StartsWith(TEXT(P4_PREVIEW_PACKAGE_ROOT), ESearchCase::CaseSensitive) ||
            !FPackageName::IsValidLongPackageName(Package.Package) || Seen.Contains(Package.Package) ||
            !(Package.Extension == TEXT(".umap") || Package.Extension == TEXT(".uasset")) ||
            Package.Header.Num() == 0 || Package.Body.Num() == 0)
        {
            Error = TEXT("Preview bootstrap contains an invalid staged package.");
            return false;
        }
        Seen.Add(Package.Package);
        const FString Stem = FPaths::ConvertRelativePathToFull(Content / P4ContentRelativePath(Package.Package));
        if (!FPaths::IsUnderDirectory(Stem, Content))
        {
            Error = TEXT("Preview bootstrap destination escaped its stage.");
            return false;
        }
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Stem), true);
        if (!FFileHelper::SaveArrayToFile(Package.Header, *(Stem + Package.Extension)) ||
            !FFileHelper::SaveArrayToFile(Package.Body, *(Stem + TEXT(".uexp"))))
        {
            Error = TEXT("Could not stage the preview bootstrap: ") + Package.Package;
            return false;
        }
        ExtraPackages.AddUnique(Package.Package);
    }
    Error.Reset();
    return true;
}

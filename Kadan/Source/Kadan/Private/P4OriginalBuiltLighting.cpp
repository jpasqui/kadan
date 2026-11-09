#include "P4OriginalBuiltLighting.h"
#include "P4GameLighting.h"
#include "P4KadanReadOnly.h"
#include "P4NativeConstructionPreview.h"
#include "P4OriginalRecord.h"
#include "P4PackageSave.h"
#include "P4StaticMeshBuild.h"
#include "P4TextureBuild.h"
#include "P4OriginalImportSave.h"
#include "Containers/Ticker.h"
#include "AssetRegistryModule.h"
#include "ComponentRecreateRenderStateContext.h"
#include "Components/LightComponent.h"
#include "Components/LightComponentBase.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Level.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "UObject/MetaData.h"
#include "Engine/World.h"
#include "Engine/Texture.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"
#include "Serialization/CustomVersion.h"
#include "Serialization/MemoryReader.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
const TCHAR* const RegistryClass = TEXT("/Script/Engine.MapBuildDataRegistry");
const TCHAR* const Schema = TEXT("built-lighting-reference-v1");

FString Text(const std::string& Value)
{
    return UTF8_TO_TCHAR(Value.c_str());
}

bool ReadFile(const FString& Path, std::vector<std::uint8_t>& Out)
{
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *Path, FILEREAD_Silent))
        return false;
    Out.assign(Bytes.GetData(), Bytes.GetData() + Bytes.Num());
    return true;
}

// One export of a cooked package read with the engine's own serializer: names from the package's name table, object
// references to the objects made for its exports (another reference reads as none), and the package's versions.
class FExportReader : public FMemoryReader
{
public:
    FExportReader(const TArray<uint8>& Bytes, const P4MapKit::FPackageInventory& InPackage,
        const TMap<int32, UObject*>& InObjects)
        : FMemoryReader(Bytes, true), Package(InPackage), Objects(InObjects)
    {
        SetIsLoading(true);
        SetIsPersistent(true);
        // A cooked package holds no editor-only data.
        SetFilterEditorOnly(true);
        // A cooked package is saved without versions; the engine reads such a package with its own, as here.
        SetUE4Ver(InPackage.UE4Version ? InPackage.UE4Version : GPackageFileUE4Version);
        SetLicenseeUE4Ver(InPackage.UE4Version ? InPackage.LicenseeVersion : GPackageFileLicenseeUE4Version);
        SetCustomVersions(FCurrentCustomVersions::GetAll());
        // A count larger than the export is a misread: it ends the read instead of looping over it.
        ArMaxSerializeSize = Bytes.Num();
    }
    virtual FString GetArchiveName() const override { return TEXT("Kadan built lighting reader"); }
    virtual FArchive& operator<<(FName& Name) override
    {
        int32 Index = 0, Number = 0;
        *this << Index << Number;
        if (Index < 0 || Index >= int32(Package.Names.size()))
        {
            SetError();
            Name = NAME_None;
            return *this;
        }
        Name = FName(*Text(Package.Names[std::size_t(Index)]), Number);
        return *this;
    }
    virtual FArchive& operator<<(UObject*& Object) override
    {
        int32 Index = 0;
        *this << Index;
        Object = Objects.FindRef(Index);
        if (Index != 0 && !Object)
            ++Unresolved;
        return *this;
    }
    int32 Unresolved = 0;

private:
    const P4MapKit::FPackageInventory& Package;
    const TMap<int32, UObject*>& Objects;
};
// The registry's mesh count, read before the engine reads the registry: past its tagged properties, its object guid
// flag and its strip flags. A count the export cannot hold means the layout is not the one this editor reads, and the
// engine would loop over it.
bool MeshCountFits(const TArray<uint8>& Bytes, const P4MapKit::FPackageInventory& Package)
{
    int64 At = 0;
    auto Int = [&](int32& Value)
    {
        if (At + 4 > Bytes.Num())
            return false;
        FMemory::Memcpy(&Value, Bytes.GetData() + At, 4);
        At += 4;
        return true;
    };
    auto Name = [&](FString& Value)
    {
        int32 Index = 0, Number = 0;
        if (!Int(Index) || !Int(Number) || Index < 0 || Index >= int32(Package.Names.size()))
            return false;
        Value = Text(Package.Names[std::size_t(Index)]);
        return true;
    };
    for (int32 Tags = 0;; ++Tags)
    {
        FString Tag, Type, Extra;
        int32 Size = 0, ArrayIndex = 0;
        if (Tags > 4096 || !Name(Tag))
            return false;
        if (Tag == TEXT("None"))
            break;
        if (!Name(Type) || !Int(Size) || !Int(ArrayIndex) || Size < 0)
            return false;
        if (Type == TEXT("StructProperty"))
        {
            if (!Name(Extra))
                return false;
            At += 16;
        }
        else if (Type == TEXT("BoolProperty"))
            At += 1;
        else if (Type == TEXT("ByteProperty") || Type == TEXT("EnumProperty") || Type == TEXT("ArrayProperty") ||
                 Type == TEXT("SetProperty"))
        {
            if (!Name(Extra))
                return false;
        }
        else if (Type == TEXT("MapProperty") && (!Name(Extra) || !Name(Extra)))
            return false;
        if (At + 1 > Bytes.Num())
            return false;
        At += Bytes[At] ? 17 : 1;
        At += Size;
    }
    int32 HasGuid = 0, Count = 0;
    if (!Int(HasGuid))
        return false;
    At += (HasGuid ? 16 : 0) + 2;
    // Each mesh's entry is a guid and a light map type at least.
    return Int(Count) && Count >= 0 && int64(Count) * 20 <= Bytes.Num() - At;
}
// The registry the import made for a level's lighting, at the path the level's file names. Null when the project has
// none, or holds another asset there.
UMapBuildDataRegistry* GameRegistry(const FString& Path)
{
    FString Package = Path, Name;
    if (!Package.Split(TEXT("."), &Package, &Name))
        Name = FPackageName::GetShortName(Package);
    if (Package.IsEmpty() || (!FindPackage(nullptr, *Package) && !FPackageName::DoesPackageExist(Package)))
        return nullptr;
    UMapBuildDataRegistry* Registry =
        LoadObject<UMapBuildDataRegistry>(nullptr, *(Package + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
    // Only the game's lighting as this import made it; a registry of another kind is the creator's.
    const FP4OriginalStatus Status = Registry ? P4QueryOriginalAsset(Registry) : FP4OriginalStatus();
    return Registry && Status.Error.IsEmpty() && Status.Record.ComparisonSchema == Schema ? Registry : nullptr;
}

// The game draws every LOD of a mesh whose LODs share lighting with the first LOD's, which the editor's build of the
// game's mesh cannot know (P4StaticMeshBuild.h). For a mesh built before that was kept, the game's component says it:
// lighting for fewer LODs than the mesh has is drawn shared.
void DrawSharedLighting(UStaticMeshComponent* Part, int32 LightingLods)
{
    UStaticMesh* Mesh = Part->GetStaticMesh();
    if (!Mesh || !Mesh->RenderData || Mesh->RenderData->bLODsShareStaticLighting)
        return;
    const FString Kept = Mesh->GetOutermost()->GetMetaData()->GetValue(Mesh, P4LODsShareStaticLightingKey);
    if (Kept == TEXT("true") || (Kept.IsEmpty() && LightingLods < Mesh->RenderData->LODResources.Num()))
        Mesh->RenderData->bLODsShareStaticLighting = true;
}
} // namespace

FP4OriginalBuiltLightingResult P4ImportOriginalBuiltLighting(
    const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk)
{
    FP4OriginalBuiltLightingResult Result;
    auto Fail = [&](const FString& Why)
    {
        Result.Error = Why;
        Result.Registry = nullptr;
        return Result;
    };
    if (!IsInGameThread())
        return Fail(TEXT("built-lighting-import-requires-editor-thread"));
    std::vector<std::uint8_t> Header, Body;
    const FString BodyPath = BodyFile.IsEmpty() ? FPaths::ChangeExtension(HeaderFile, TEXT("uexp")) : BodyFile;
    if (!ReadFile(HeaderFile, Header) || !ReadFile(BodyPath, Body))
        return Fail(TEXT("built-lighting-source-unreadable"));
    const P4MapKit::FPackageInventory Package = P4MapKit::ReadPackage(Header, Body);
    if (!Package.Error.Code.empty())
        return Fail(TEXT("built-lighting-package-unreadable: ") + Text(Package.Error.Code));
    Result.Package = Text(Package.Package);
    int32 RegistryIndex = 0;
    for (int32 Index = 0; Index < int32(Package.Exports.size()); ++Index)
    {
        std::string Class;
        if (Package.Reference(Package.Exports[std::size_t(Index)].Class, Class) && Text(Class) == RegistryClass)
        {
            if (RegistryIndex)
                return Fail(TEXT("built-lighting-package-holds-more-than-one-registry"));
            RegistryIndex = Index + 1;
        }
    }
    if (!RegistryIndex)
        return Fail(TEXT("built-lighting-package-holds-no-registry"));
    const P4MapKit::FPackageExport& RegistryExport = Package.Exports[std::size_t(RegistryIndex - 1)];
    const FString RegistryName = Text(RegistryExport.Name);

    // An earlier import is kept as it is: the game's lighting does not change.
    const FString ObjectPath = Result.Package + TEXT(".") + RegistryName;
    if (FPackageName::DoesPackageExist(Result.Package) || FindPackage(nullptr, *Result.Package))
    {
        Result.Registry = LoadObject<UMapBuildDataRegistry>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
        FP4OriginalRecord Earlier;
        FString Why;
        if (!Result.Registry || !P4ReadOriginalRecord(Result.Registry, Earlier, Why) || !Earlier.ReferenceOnly ||
            Earlier.ComparisonSchema != Schema)
            return Fail(TEXT("built-lighting-destination-holds-another-asset"));
        return Result;
    }

    UPackage* Out = CreatePackage(*Result.Package);
    auto* Registry =
        NewObject<UMapBuildDataRegistry>(Out, FName(*RegistryName), RF_Public | RF_Standalone | RF_Transactional);
    TStrongObjectPtr<UMapBuildDataRegistry> Held(Registry);
    TMap<int32, UObject*> Objects;
    Objects.Add(RegistryIndex, Registry);
    // The lightmap and shadow map textures, with the game's mips as their source.
    for (int32 Index = 0; Index < int32(Package.Exports.size()); ++Index)
    {
        const P4MapKit::FPackageExport& Export = Package.Exports[std::size_t(Index)];
        if (Index + 1 == RegistryIndex)
            continue;
        FP4TextureBuildReport Report;
        FString Why;
        UTexture* Texture = P4BuildOriginalPackageTexture(HeaderFile, Body, Package, &Export, Report, Why, false);
        if (!Texture)
            return Fail(FString::Printf(TEXT("built-lighting-texture-unbuilt: %s: %s"), *Text(Export.Name), *Why));
        UObject* Outer = Export.Outer == RegistryIndex ? static_cast<UObject*>(Registry) : static_cast<UObject*>(Out);
        Texture->Rename(*Text(Export.Name), Outer, REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
        Texture->ClearFlags(RF_Transient);
        Texture->SetFlags(RF_Transactional);
        Texture->PostEditChange();
        Objects.Add(Index + 1, Texture);
        ++Result.Textures;
    }
    // The registry's own data, with the engine's serializer.
    if (RegistryExport.Offset > Body.size() || RegistryExport.Size > Body.size() - RegistryExport.Offset)
        return Fail(TEXT("built-lighting-registry-outside-the-package"));
    TArray<uint8> Bytes(Body.data() + RegistryExport.Offset, int32(RegistryExport.Size));
    if (!MeshCountFits(Bytes, Package))
        return Fail(TEXT("built-lighting-registry-layout-unknown"));
    FExportReader Reader(Bytes, Package, Objects);
    Registry->Serialize(Reader);
    if (Reader.IsError() || Reader.Tell() != Bytes.Num())
        return Fail(FString::Printf(TEXT("built-lighting-registry-unread: read %lld of %d bytes"), Reader.Tell(),
            Bytes.Num()));
    if (Reader.Unresolved)
        return Fail(FString::Printf(TEXT("built-lighting-registry-names-%d-objects-outside-its-package"), Reader.Unresolved));
    Registry->SetupLightmapResourceClusters();
    Registry->InitializeClusterRenderingResources(GMaxRHIFeatureLevel);

    FP4OriginalRecord Record;
    Record.Package = Result.Package;
    Record.Class = RegistryClass;
    Record.Kind = TEXT("built-lighting");
    Record.ReferenceOnly = true;
    Record.ReadOnly = true;
    Record.Fidelity = TEXT("game-built-lighting");
    Record.SourceDigestKind = TEXT("not-recovered");
    Record.ComparisonSchema = Schema;
    FString Why;
    if (!P4MarkOriginalAsset(Registry, Record, Why))
        return Fail(Why);
    FAssetRegistryModule::AssetCreated(Registry);
    Registry->MarkPackageDirty();
    if (SaveToDisk)
    {
        if (!P4SaveAssetPackage(
                Registry,
                [&]()
                {
                    const FP4OriginalStatus Status = P4QueryOriginalAsset(Registry);
                    return Status.Error.IsEmpty() && Status.Record.ReferenceOnly && Status.Record.ComparisonSchema == Schema;
                },
                Why))
            return Fail(Why);
        Result.Saved = true;
    }
    P4Kadan::UpdateReadOnlyAsset(Registry);
    Result.Registry = Registry;
    return Result;
}

bool P4HoldOriginalBuiltLighting(UWorld* Owner)
{
    if (!IsInGameThread() || !IsValid(Owner) || !Owner->PersistentLevel)
        return false;
    ULevel* Level = Owner->PersistentLevel;
    FP4OriginalLevelLighting Lighting;
    FString Why;
    if (!P4ReadOriginalLevelLighting(Owner, Lighting, Why))
        return false;
    const FString Prefix = Owner->GetName() + TEXT(".") + Level->GetName() + TEXT(".");
    for (const auto& Component : Lighting.Components)
    {
        if (!Component.Key.StartsWith(Prefix))
            continue;
        UStaticMeshComponent* Part = FindObject<UStaticMeshComponent>(Level, *Component.Key.Mid(Prefix.Len()));
        if (!P4IsRetainedLightingObject(Level, Part))
            continue;
        Part->SetLODDataCount(Component.Value.Num(), Component.Value.Num());
        for (int32 Lod = 0; Lod < Component.Value.Num(); ++Lod)
            Part->LODData[Lod].MapBuildDataId = Component.Value[Lod];
        DrawSharedLighting(Part, Component.Value.Num());
    }
    for (const auto& Light : Lighting.Lights)
    {
        if (!Light.Value.IsValid() || !Light.Key.StartsWith(Prefix))
            continue;
        ULightComponentBase* Part = FindObject<ULightComponentBase>(Level, *Light.Key.Mid(Prefix.Len()));
        if (P4IsRetainedLightingObject(Level, Part))
            Part->LightGuid = Light.Value;
    }
    if (!Level->MapBuildData)
        if (UMapBuildDataRegistry* Registry = GameRegistry(Lighting.Registry))
        {
            const bool Drawn = Level->bIsVisible;
            if (Drawn)
                Level->ReleaseRenderingResources();
            Level->MapBuildData = Registry;
            if (Drawn)
                Level->InitializeRenderingResources();
        }
    return Level->MapBuildData != nullptr;
}

void P4HoldOriginalBuiltLightingOfRegistry(UMapBuildDataRegistry* Registry)
{
    if (!IsInGameThread() || !IsValid(Registry))
        return;
    FString Package = Registry->GetOutermost()->GetName();
    if (!Package.RemoveFromEnd(TEXT("_BuiltData")) ||
        (!FindPackage(nullptr, *Package) && !FPackageName::DoesPackageExist(Package)))
        return;
    UWorld* Owner = LoadObject<UWorld>(
        nullptr, *(Package + TEXT(".") + FPackageName::GetShortName(Package)), nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (!IsValid(Owner) || !Owner->PersistentLevel || Owner->PersistentLevel->MapBuildData ||
        !P4HoldOriginalBuiltLighting(Owner) || Owner->PersistentLevel->MapBuildData != Registry)
        return;
    FString Why;
    if (!P4SaveAssetPackage(Owner, nullptr, Why))
        UE_LOG(LogTemp, Warning, TEXT("%s keeps its built lighting unsaved: %s"), *Package, *Why);
}

int32 P4ApplyOriginalBuiltLightingPreviews(UWorld* World)
{
    if (!World || !IsInGameThread())
        return 0;
    struct FPlan
    {
        ULevel* Level = nullptr;
        UMapBuildDataRegistry* Registry = nullptr;
        FP4OriginalLevelLighting Lighting;
    };
    TArray<FPlan> Plans;
    for (ULevel* Level : World->GetLevels())
    {
        UWorld* Owner = Level ? Cast<UWorld>(Level->GetOuter()) : nullptr;
        FPlan Plan;
        FString Why;
        if (!Owner || Level->MapBuildData || !P4ReadOriginalLevelLighting(Owner, Plan.Lighting, Why) ||
            Plan.Lighting.Registry.IsEmpty())
            continue;
        Plan.Registry = GameRegistry(Plan.Lighting.Registry);
        if (!Plan.Registry)
            continue;
        Plan.Level = Level;
        Plans.Add(MoveTemp(Plan));
    }
    if (!Plans.Num())
        return 0;
    // A preview is not an edit: every level keeps whether it had unsaved changes.
    TMap<UPackage*, bool> WasDirty;
    for (ULevel* Level : World->GetLevels())
        if (Level)
            WasDirty.Add(Level->GetOutermost(), Level->GetOutermost()->IsDirty());
    ON_SCOPE_EXIT
    {
        for (const auto& Package : WasDirty)
            Package.Key->SetDirtyFlag(Package.Value);
    };
    FGlobalComponentRecreateRenderStateContext RenderStates;
    FProperty* const RegistryProperty = FindFProperty<FProperty>(ULevel::StaticClass(), TEXT("MapBuildData"));
    FProperty* const LightProperty = FindFProperty<FProperty>(ULightComponent::StaticClass(), TEXT("LightGuid"));
    for (FPlan& Plan : Plans)
    {
        ULevel* Level = Plan.Level;
        UWorld* Owner = CastChecked<UWorld>(Level->GetOuter());
        const FString Prefix = Owner->GetName() + TEXT(".") + Level->GetName() + TEXT(".");
        Level->ReleaseRenderingResources();
        P4RememberNativePreviewValue(Level, RegistryProperty);
        Level->MapBuildData = Plan.Registry;
        for (const auto& Component : Plan.Lighting.Components)
        {
            if (!Component.Key.StartsWith(Prefix))
                continue;
            UStaticMeshComponent* Part = FindObject<UStaticMeshComponent>(Level, *Component.Key.Mid(Prefix.Len()));
            if (!P4IsRetainedLightingObject(Level, Part))
                continue;
            // The level's lighting ids and LOD count, exchanged with the preview's each time.
            TSharedRef<TArray<FGuid>> Other = MakeShared<TArray<FGuid>>(Component.Value);
            TWeakObjectPtr<UStaticMeshComponent> Weak(Part);
            auto Exchange = [Weak, Other]()
            {
                UStaticMeshComponent* Mesh = Weak.Get();
                if (!Mesh)
                    return;
                TArray<FGuid> Held;
                for (const FStaticMeshComponentLODInfo& Lod : Mesh->LODData)
                    Held.Add(Lod.MapBuildDataId);
                Mesh->SetLODDataCount(Other->Num(), Other->Num());
                for (int32 Lod = 0; Lod < Other->Num(); ++Lod)
                    Mesh->LODData[Lod].MapBuildDataId = (*Other)[Lod];
                *Other = MoveTemp(Held);
                Mesh->MarkRenderStateDirty();
            };
            // The level's own are what the component holds now; the preview's are the import's.
            P4RememberNativePreviewExchange(Part, TEXT("LODData.MapBuildDataId"), Exchange);
            Exchange();
            DrawSharedLighting(Part, Component.Value.Num());
        }
        for (const auto& Light : Plan.Lighting.Lights)
        {
            if (!Light.Value.IsValid() || !Light.Key.StartsWith(Prefix))
                continue;
            ULightComponent* Part = FindObject<ULightComponent>(Level, *Light.Key.Mid(Prefix.Len()));
            if (!P4IsRetainedLightingObject(Level, Part) || Part->LightGuid == Light.Value)
                continue;
            P4RememberNativePreviewValue(Part, LightProperty);
            Part->LightGuid = Light.Value;
        }
        Level->InitializeRenderingResources();
    }
    return Plans.Num();
}

namespace
{
FDelegateHandle DirtyHandle;

// A registry Kadan made from the game's built lighting, as opposed to one a creator's own level owns.
bool IsGameRegistry(UMapBuildDataRegistry* Registry)
{
    if (!IsValid(Registry))
        return false;
    const FP4OriginalStatus Status = P4QueryOriginalAsset(Registry);
    return Status.Error.IsEmpty() && Status.Record.ReferenceOnly && Status.Record.ComparisonSchema == Schema;
}
} // namespace

bool P4KeepGameBuiltLightingOutOfSave(UWorld* LevelWorld)
{
    if (!IsInGameThread() || !LevelWorld || !LevelWorld->PersistentLevel)
        return false;
    ULevel* Level = LevelWorld->PersistentLevel;
    UMapBuildDataRegistry* Registry = Level->MapBuildData;
    if (!Registry || Registry->GetOutermost() == LevelWorld->GetOutermost() || Registry->GetOutermost()->IsDirty() ||
        !IsGameRegistry(Registry))
        return false;
    // The level is already written with its reference to the registry; only the editor's follow-up save reads this.
    Level->MapBuildData = nullptr;
    const TWeakObjectPtr<ULevel> Lent(Level);
    const TWeakObjectPtr<UMapBuildDataRegistry> Held(Registry);
    FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
        [Lent, Held](float)
        {
            ULevel* Back = Lent.Get();
            if (Back && !Back->MapBuildData && Held.IsValid())
                Back->MapBuildData = Held.Get();
            return false;
        }));
    return true;
}

void P4StartGameBuiltLightingGuard()
{
    if (DirtyHandle.IsValid())
        return;
    DirtyHandle = UPackage::PackageMarkedDirtyEvent.AddLambda(
        [](UPackage* Package, bool WasDirty)
        {
            if (WasDirty || !Package || !IsInGameThread() || P4ImportPublication::IsImporting())
                return;
            UMapBuildDataRegistry* Registry =
                FindObject<UMapBuildDataRegistry>(Package, *FPackageName::GetShortName(Package));
            FString File;
            if (!IsGameRegistry(Registry) || !FPackageName::DoesPackageExist(Package->GetName(), nullptr, &File))
                return;
            IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
            File = FPaths::ConvertRelativePathToFull(File);
            if (Files.IsReadOnly(*File))
                Files.SetReadOnly(*File, false);
        });
}

void P4StopGameBuiltLightingGuard()
{
    UPackage::PackageMarkedDirtyEvent.Remove(DirtyHandle);
    DirtyHandle.Reset();
}

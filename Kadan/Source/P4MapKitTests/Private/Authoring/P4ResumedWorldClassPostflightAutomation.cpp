#include "P4KadanImport.h"
#include "P4KadanAcquisitionPolicy.h"
#include "P4KadanImportSources.h"
#include "P4KadanContainerIndex.h"
#include "P4MapKitCore/package_reader.h"
#include "P4StrictNativeClassSources.h"
#include "P4BlueprintImport.h"
#include "P4OriginalRecord.h"
#include "P4NativeHash.h"
#include "P4ExportContainer.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/GCObject.h"
#include "UObject/Class.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
namespace
{
struct FResumedClassWorldSnapshot
{
    UWorld* World = nullptr;
    FString Package, Record, Baseline, Custody, SourceDigest;
    bool Dirty = false;
    TSet<FString> RequiredClasses;
};
struct FResumedClassPreservedProperty
{
    FProperty* Property;
    void* Value;
    FResumedClassPreservedProperty(FProperty* InProperty, UObject* Asset) : Property(InProperty)
    {
        Value = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
        Property->InitializeValue(Value);
        Property->CopyCompleteValue(Value, Property->ContainerPtrToValuePtr<void>(Asset));
    }
    ~FResumedClassPreservedProperty()
    {
        Property->DestroyValue(Value);
        FMemory::Free(Value);
    }
};
struct FResumedClassPreservedDependency
{
    UObject* Asset = nullptr;
    FString Package, RecordKey, Record;
    bool Dirty = false;
    TArray<TUniquePtr<FResumedClassPreservedProperty>> Properties;
};
struct FResumedClassRecoveredBlueprint
{
    UBlueprint* Asset = nullptr;
    UClass* Class = nullptr;
    UObject* DefaultObject = nullptr;
    FString Package, Record, SourceMembers, ImportVersion;
    bool Dirty = false;
};
struct FResumedClassWorldHold : FGCObject
{
    TArray<FResumedClassWorldSnapshot> Worlds;
    TArray<FResumedClassPreservedDependency> UnqualifiedDependencies;
    TArray<FResumedClassRecoveredBlueprint> RecoveredBlueprints;
    void AddReferencedObjects(FReferenceCollector& Collector) override
    {
        for (auto& World : Worlds)
            Collector.AddReferencedObject(World.World);
        for (auto& Dependency : UnqualifiedDependencies)
            Collector.AddReferencedObject(Dependency.Asset);
        for (auto& Blueprint : RecoveredBlueprints)
        {
            Collector.AddReferencedObject(Blueprint.Asset);
            Collector.AddReferencedObject(Blueprint.Class);
            Collector.AddReferencedObject(Blueprint.DefaultObject);
        }
    }
    FString GetReferencerName() const override { return TEXT("P4ResumedWorldClassPostflightControl"); }
};
FString ResumedClassNormalize(FString Path)
{
    FPaths::NormalizeFilename(Path);
    Path = FPaths::ConvertRelativePathToFull(Path);
    FPaths::CollapseRelativeDirectories(Path);
    FPaths::NormalizeDirectoryName(Path);
    return Path;
}
bool ResumedClassReadJson(const FString& Text, TSharedPtr<FJsonObject>& Json)
{
    return FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(Text), Json) && Json.IsValid();
}
const TCHAR* const ResumedClassAcquisitionOnlyParticles[] = {TEXT("Effects/Cmn/PS_eft_Cmn_In00"),
    TEXT("Effects/Objects/Gimmicks/CrackPot/PS_eft_CrackPotL_Break00"),
    TEXT("Effects/Objects/Gimmicks/CrackPot/PS_eft_CrackPotS_Break00"),
    TEXT("Effects/Objects/Items/ShugoFlag/PS_eft_ShugoExFlag_Set00"),
    TEXT("Effects/Objects/Madori/PS_eft_DungeonExit_Light01"),
    TEXT("Effects/Objects/Madori/PS_eft_DungeonExit_Light02"), TEXT("Effects/Objects/Pod/PS_eft_Pod_Collect00"),
    TEXT("Effects/Objects/Pod/PS_eft_Pod_Collect01"), TEXT("Effects/Objects/Pod/PS_eft_Pod_Light00"),
    TEXT("Effects/Player/Avatar/PS_eft_Avatar_Recovery00"), TEXT("Effects/Player/PS_eft_Player_ReturnIn00")};
} //namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ActualResumedWorldClassPostflight,
    "P4MapKit.Kadan.ActualResumedWorldClassPostflight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ActualResumedWorldClassPostflight::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RESUME_CLASS_POSTFLIGHT")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "Opt-in: use resume_class_custody.py with P4_NATIVE_RESUME_CLASS_POSTFLIGHT=1; one native GPU refusal and exactly eleven cached kept-game acquisitions are separately checked."));
        return true;
    }
    const FString NativeRoot = ResumedClassNormalize(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT")));
    const FString Repository = FPaths::GetPath(FPaths::GetPath(NativeRoot));
    const FString Project = ResumedClassNormalize(FPaths::ProjectDir());
    FP4KadanImportSettings Settings;
    Settings.GameFolder = ResumedClassNormalize(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_CAVE_GAME")));
    Settings.ConvertedCache = FPaths::ProjectSavedDir() / TEXT("Kadan/ConvertedOriginals");
    Settings.AllowGameBackedLevels = false;
    if (!TestTrue(TEXT("The real existing native fixture and canonical retained source are selected"),
            FPaths::IsSamePath(NativeRoot, Repository / TEXT("private/mapkit-native-workflow")) &&
                FPaths::IsSamePath(Project, Repository / TEXT("private/native-development/project")) &&
                FPaths::IsSamePath(
                    Settings.GameFolder, Repository / TEXT("private/discord-fresh-validation/source/Carrot4/Content"))))
    {
        AddError(FString::Printf(TEXT("Owned fixture guard: native=[%s] project=[%s] source=[%s]"), *NativeRoot,
            *Project, *Settings.GameFolder));
        return false;
    }
    const FString Map = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Cave000_F00_P");
    const FString Env = TEXT("/Game/Carrot4/Core/Graphics/System/BP_EnvController");
    const FString UnsupportedParticle = TEXT("/Game/Carrot4/Effects/Player/PS_eft_Player_ReturnOut00");
    const FString UnsupportedObject = UnsupportedParticle + TEXT(".PS_eft_Player_ReturnOut00");
    Settings.GamePaks = Repository / TEXT("private/game-paks");
    Settings.Retoc = Repository / TEXT("private/retoc/retoc.exe");
    Settings.Repak = Repository / TEXT("private/repak/repak.exe");
    if (!TestTrue(TEXT("The retained selected original containers and tools are available"),
            IFileManager::Get().DirectoryExists(*Settings.GamePaks) &&
                IFileManager::Get().FileExists(*Settings.Retoc) && IFileManager::Get().FileExists(*Settings.Repak)))
        return false;
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit/StrictNative");
    if (!TestTrue(TEXT("One retained report directory is available"), IFileManager::Get().MakeDirectory(*Folder, true)))
        return false;
    auto Evidence = MakeShared<FJsonObject>();
    Evidence->SetStringField(TEXT("stage"), TEXT("preflight"));
    const auto SaveEvidence = [&]()
    {
        Evidence->SetBoolField(TEXT("test_passed"), !HasAnyErrors());
        FString Text;
        FJsonSerializer::Serialize(Evidence, TJsonWriterFactory<TCHAR>::Create(&Text));
        TestTrue(TEXT("Bounded resume qualification control receipt is retained"),
            FFileHelper::SaveStringToFile(Text, *(Folder / TEXT("resumed-class-postflight-proof.json")),
                FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    };
    ON_SCOPE_EXIT
    {
        SaveEvidence();
    };
    FString Cache, CacheError;
    if (!TestTrue(TEXT("Read-only actual conversion identity is current before any importer can refresh the cache"),
            P4KadanRetainedConversionCacheCurrent(Settings, Cache, CacheError)))
    {
        AddError(CacheError);
        return false;
    }
    const FString CacheStampFile = Cache / TEXT("converted-from.txt");
    FString CacheStampBefore;
    int64 CacheStampSize = 0;
    if (!TestTrue(TEXT("The actual retained cache stamp has a bounded preimage"),
            P4HashFile(CacheStampFile, 65536, CacheStampSize, CacheStampBefore)))
        return false;
    const FDateTime CacheStampTime = IFileManager::Get().GetTimeStamp(*CacheStampFile);
    // Exercise the actual production stamp comparison with a changed tool input;
    // this query must refuse without calling the mutating cache getter.
    FP4KadanImportSettings Drift = Settings;
    Drift.Retoc += TEXT(".not-current");
    FString RejectedCache, DriftError;
    if (!TestFalse(TEXT("Read-only cache preflight refuses a changed selected tool"),
            P4KadanRetainedConversionCacheCurrent(Drift, RejectedCache, DriftError)))
        return false;
    FString CacheStampAfter;
    if (!TestTrue(TEXT("A stale-input preflight neither refreshes nor invalidates the actual retained stamp"),
            P4HashFile(CacheStampFile, 65536, CacheStampSize, CacheStampAfter) &&
                P4ExactNativeString(CacheStampBefore, CacheStampAfter) &&
                IFileManager::Get().GetTimeStamp(*CacheStampFile) == CacheStampTime))
        return false;
    const FString OriginalStage = FPaths::ProjectSavedDir() / TEXT("Kadan/OriginalPackages");
    const FString CustodyStage = FPaths::ProjectSavedDir() / TEXT("Kadan/OriginalPackages.ResumeClassCustody");
    FString CustodyText;
    TSharedPtr<FJsonObject> Custody;
    const TSharedPtr<FJsonObject>* CustodyFiles = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* CustodyExpected = nullptr;
    if (!TestTrue(TEXT("An independent timeout-safe host owner holds the original staging before this native boot"),
            FFileHelper::LoadFileToString(CustodyText, *(Folder / TEXT("resume-source-custody.json"))) &&
                ResumedClassReadJson(CustodyText, Custody) && Custody->GetIntegerField(TEXT("schema")) == 1 &&
                Custody->GetStringField(TEXT("owner")) == TEXT("P4MapKit.Kadan.ActualResumedWorldClassPostflight") &&
                Custody->GetStringField(TEXT("phase")) == TEXT("held") &&
                Custody->TryGetObjectField(TEXT("original_files"), CustodyFiles) &&
                (*CustodyFiles)->Values.Num() == 26 &&
                Custody->TryGetArrayField(TEXT("expected_staged_files"), CustodyExpected) &&
                CustodyExpected->Num() == 22 &&
                FPaths::IsSamePath(Custody->GetStringField(TEXT("project")),
                    FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath())) &&
                FPaths::IsSamePath(Custody->GetStringField(TEXT("stage")), ResumedClassNormalize(OriginalStage)) &&
                FPaths::IsSamePath(Custody->GetStringField(TEXT("custody")), ResumedClassNormalize(CustodyStage)) &&
                !IFileManager::Get().DirectoryExists(*OriginalStage) &&
                IFileManager::Get().DirectoryExists(*CustodyStage)))
        return false;
    Evidence->SetBoolField(TEXT("independent_original_staging_custody_required"), true);
    FP4ExportContainerTools Tools;
    Tools.Retoc = Settings.Retoc;
    Tools.Repak = Settings.Repak;
    FP4ExportGameInventory Game;
    FString InventoryError;
    if (!TestTrue(TEXT("The selected real game registry and container independently prove ReturnOut identity"),
            P4ReadExportGameInventory(
                Tools, Settings.GamePaks, Folder / TEXT("resume-game-inventory"), Game, InventoryError) &&
                Game.IsValid() && Game.IsFromDirectory(Settings.GamePaks) &&
                Game.Contains(FName(*UnsupportedParticle))))
    {
        AddError(InventoryError);
        return false;
    }
    Evidence->SetStringField(TEXT("game_inventory_sha256"), Game.InventorySha256());
    Evidence->SetStringField(TEXT("game_registry_sha256"), Game.RegistrySha256());
    TSet<FName> AcquisitionOnly;
    TMap<FString, FString> CachedParticleHeaders;
    for (const TCHAR* Relative : ResumedClassAcquisitionOnlyParticles)
    {
        const FString Package = FString(TEXT("/Game/Carrot4/")) + Relative;
        const FString Object = Package + TEXT(".") + FPackageName::GetShortName(Package);
        const FString Header = Cache / TEXT("Carrot4/Content/Carrot4") / (FString(Relative) + TEXT(".uasset"));
        FString Why;
        TArray<uint8> Bytes;
        if (!TestTrue(TEXT("Each exact absent endpoint is a real selected game identity with existing cached H+B: ") +
                    Package,
                Game.Contains(FName(*Package)) && !FPackageName::DoesPackageExist(Package) &&
                    !FindPackage(nullptr, *Package) && !FindObject<UObject>(nullptr, *Object) &&
                    P4KadanExtractedPackageReady(Header, Package, Why) && FFileHelper::LoadFileToArray(Bytes, *Header)))
        {
            AddError(Why);
            return false;
        }
        const auto Read = P4MapKit::ReadPackageHeader(
            std::vector<uint8>(Bytes.GetData(), Bytes.GetData() + Bytes.Num()),
            std::size_t(IFileManager::Get().FileSize(*FPaths::ChangeExtension(Header, TEXT("uexp")))));
        bool Particle = false;
        for (const auto& Export : Read.Exports)
        {
            std::string Class;
            if (Export.Outer == 0 && Export.Name == TCHAR_TO_UTF8(*FPackageName::GetShortName(Package)) &&
                Read.Reference(Export.Class, Class))
                Particle = Class == "/Script/Engine.ParticleSystem";
        }
        if (!TestTrue(TEXT("The actual cached top-level class is exactly the normal kept-game ParticleSystem kind: ") +
                    Package,
                Read.Error.Code.empty() && Particle && P4KadanKeptAsTheGames(TEXT("ParticleSystem"))))
            return false;
        AcquisitionOnly.Add(FName(*Package));
        CachedParticleHeaders.Add(Package, Header);
    }
    const auto Plan = P4KadanPlanImport(Settings, {Map}, true);
    for (const FString& Problem : Plan.Problems)
        AddError(Problem);
    if (!TestTrue(
            TEXT("Retained ordinary resume plan is valid and nonempty"), !Plan.Problems.Num() && Plan.Assets.Num() > 0))
        return false;
    Evidence->SetNumberField(TEXT("planned_assets"), Plan.Assets.Num());
    FResumedClassWorldHold Held;
    int32 AllowedMissingParticles = 0;
    // Only the evidenced unsupported GPU source may reach its normal native refusal.
    // Any other new/fill/replacement operation blocks before ordinary import starts.
    // Existing non-Blueprint reference-only materials may be preserved, but this
    // test explicitly records their lack of material/whole-asset qualification.
    for (const auto& Source : Plan.Assets)
    {
        if (P4ExactNativeString(Source->Package, UnsupportedParticle))
        {
            ++AllowedMissingParticles;
            if (!TestTrue(TEXT("Only the known genuine GPU source may lack an editor destination"),
                    Source->Kind == TEXT("ParticleSystem") && !FPackageName::DoesPackageExist(UnsupportedParticle) &&
                        !FindPackage(nullptr, *UnsupportedParticle) &&
                        !FindObject<UObject>(nullptr, *UnsupportedObject)))
                return false;
            continue;
        }
        if (!TestTrue(TEXT("Every executable destination already exists: ") + Source->Package,
                FPackageName::DoesPackageExist(Source->Package)))
            return false;
        UObject* Asset = LoadObject<UObject>(nullptr,
            *(Source->Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Source->Package)), nullptr,
            LOAD_NoWarn | LOAD_Quiet);
        // ExistingReal rejects the explicit tag even on a dirty package; the
        // verified-stand-in convenience query alone deliberately excludes dirty assets.
        const bool TaggedStandIn = Asset &&
            FString(Asset->GetOutermost()->GetMetaData()->GetValue(Asset, TEXT("P4BlueprintStandIn"))) == TEXT("1");
        if (!TestTrue(TEXT("Every destination is a complete live asset: ") + Source->Package,
                Asset && !TaggedStandIn && !P4IsVerifiedStandIn(Asset) && !P4BlueprintReconstructionIncomplete(Asset)))
            return false;
        if (Source->Kind == TEXT("Material"))
        {
            FP4OriginalRecord Record;
            FString Error;
            if (!TestTrue(TEXT("An existing original material has a readable source record: ") + Source->Package,
                    P4ReadOriginalRecord(Asset, Record, Error) && P4ExactNativeString(Record.Package, Source->Package)))
            {
                Evidence->SetStringField(TEXT("blocked_package"), Source->Package);
                return false;
            }
            if (Record.ReferenceOnly)
            {
                FResumedClassPreservedDependency Dependency;
                Dependency.Asset = Asset;
                Dependency.Package = Source->Package;
                Dependency.RecordKey = Record.LegacyKey.IsEmpty() ? FString(TEXT("P4OriginalRecord"))
                                                                  : Record.LegacyKey;
                Dependency.Record = Asset->GetOutermost()->GetMetaData()->GetValue(Asset, *Dependency.RecordKey);
                if (!TestTrue(TEXT("Preserved material has its actual current or legacy raw record"),
                        !Dependency.Record.IsEmpty()))
                    return false;
                Dependency.Dirty = Asset->GetOutermost()->IsDirty();
                for (TFieldIterator<FProperty> Field(Asset->GetClass()); Field; ++Field)
                    if (!Field->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient))
                        Dependency.Properties.Add(MakeUnique<FResumedClassPreservedProperty>(*Field, Asset));
                Held.UnqualifiedDependencies.Add(MoveTemp(Dependency));
            }
        }
        else if (Source->Kind.EndsWith(TEXT("Blueprint"), ESearchCase::CaseSensitive))
        {
            FP4OriginalRecord Record;
            FString Error;
            UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
            UClass* Class = Blueprint ? Blueprint->GeneratedClass : nullptr;
            UObject* DefaultObject = Class ? Class->GetDefaultObject(false) : nullptr;
            if (!TestTrue(TEXT("An actual recovered Blueprint has its own generated class and CDO: ") + Source->Package,
                    Blueprint && IsValid(Class) && IsValid(DefaultObject) && Class->ClassGeneratedBy == Blueprint &&
                        Class->GetOutermost() == Asset->GetOutermost() && DefaultObject->GetClass() == Class &&
                        !P4BlueprintReconstructionIncomplete(Class)))
                return false;
            if (!TestTrue(TEXT("Actual recovered+ReferenceOnly policy metadata is distinct from a placeholder: ") +
                        Source->Package,
                    P4ReadOriginalRecord(Asset, Record, Error) &&
                        P4ExactNativeString(Record.Package, Source->Package) &&
                        P4ExactNativeString(Record.Kind, TEXT("blueprint")) &&
                        P4ExactNativeString(Record.Fidelity, TEXT("recovered")) &&
                        P4ExactNativeString(Record.SourceDigestKind, TEXT("blueprint-import")) &&
                        Record.ReferenceOnly && !Record.ReadOnly))
            {
                AddError(Error);
                return false;
            }
            // OriginalMarker deliberately applies ReferenceOnly to completed
            // recovered Blueprints too. Test the actual input's distinct role,
            // not a fabricated metadata dictionary or a relaxed class comparison.
            if (!TestFalse(
                    TEXT("The actual recovered policy marker is not an importer placeholder: ") + Source->Package,
                    UP4BlueprintImport::IsStandIn(Blueprint)))
                return false;
            FResumedClassRecoveredBlueprint Snapshot;
            Snapshot.Asset = Blueprint;
            Snapshot.Class = Class;
            Snapshot.DefaultObject = DefaultObject;
            Snapshot.Package = Source->Package;
            Snapshot.Dirty = Blueprint->GetOutermost()->IsDirty();
            UMetaData* Metadata = Blueprint->GetOutermost()->GetMetaData();
            Snapshot.Record = Metadata->GetValue(Blueprint, TEXT("P4OriginalRecord"));
            Snapshot.SourceMembers = Metadata->GetValue(Blueprint, TEXT("P4BlueprintSourceMembers"));
            Snapshot.ImportVersion = Metadata->GetValue(Blueprint, TEXT("P4BlueprintImportVersion"));
            Held.RecoveredBlueprints.Add(MoveTemp(Snapshot));
        }
    }
    if (!TestEqual(TEXT("Exactly the independently verified known unsupported GPU dependency is attempted"),
            AllowedMissingParticles, 1))
        return false;
    // Initial BringIn discovers exactly these eleven cache-backed kept-game assets.
    // Their kinds prevent executable source additions/dependency expansion. Anything
    // else absent still blocks before the unchanged ordinary importer is invoked.
    TMap<FName, TSharedPtr<FP4KadanSourceAsset>> Known;
    for (const auto& Source : Plan.Scanned)
        Known.Add(FName(*Source->Package), Source);
    TArray<FString> Pending;
    for (const auto& Source : Plan.Assets)
        Pending.Add(Source->Package);
    TSet<FName> Visited;
    TSet<FName> SeenAcquisitions;
    while (Pending.Num())
    {
        const FString Package = Pending.Pop(false);
        const FName Identity(*Package);
        if (Visited.Contains(Identity))
            continue;
        Visited.Add(Identity);
        const auto* Source = Known.Find(Identity);
        if (!Source)
        {
            if (AcquisitionOnly.Contains(Identity))
            {
                SeenAcquisitions.Add(Identity);
                continue;
            }
            if (!TestTrue(TEXT("No unselected absent dependency can trigger acquisition: ") + Package,
                    !Package.StartsWith(TEXT("/Game/"), ESearchCase::CaseSensitive) ||
                        FPackageName::DoesPackageExist(Package)))
                return false;
            continue;
        }
        const bool FollowSoft = (*Source)->Kind == TEXT("World") ||
            (*Source)->Kind == TEXT("ActorPlacementInfoAsset") ||
            (*Source)->Kind == TEXT("ActorPlacementInfoListAsset");
        for (const FString& Dependency : (*Source)->Dependencies)
            if (FollowSoft || !(*Source)->SoftDependencies.Contains(Dependency))
                Pending.Add(Dependency);
    }
    if (!TestEqual(TEXT("The complete current closure contains exactly the eleven independently checked acquisitions"),
            SeenAcquisitions.Num(), 11))
        return false;
    Evidence->SetBoolField(TEXT("material_or_whole_asset_closure_claimed"), false);
    TSet<FString> ExpectedClasses;
    TMap<FString, FString> ExpectedClassSourceDigests;
    TMap<FString, FString> FileDigests;
    TMap<FString, FDateTime> FileTimes;
    const auto SnapshotFile = [&](const FString& Path)
    {
        if (FileDigests.Contains(Path))
            return true;
        FString Digest;
        int64 Size = 0;
        if (!TestTrue(TEXT("Bounded exact planned/invoked bytes are readable: ") + Path,
                P4HashFile(Path, 64 * 1024 * 1024, Size, Digest)))
            return false;
        FileDigests.Add(Path, Digest);
        return true;
    };
    // Hash only source pairs actually planned/invoked, not the unselected source tree.
    for (const auto& Source : Plan.Assets)
        if (!SnapshotFile(Source->Header) || !SnapshotFile(Source->Body))
            return false;
    for (const auto& Cached : CachedParticleHeaders)
        for (const FString& Path : {Cached.Value, FPaths::ChangeExtension(Cached.Value, TEXT("uexp"))})
        {
            if (!SnapshotFile(Path))
                return false;
            FileTimes.Add(Path, IFileManager::Get().GetTimeStamp(*Path));
        }
    const FString Stamp = Cache / TEXT("converted-from.txt");
    if (!SnapshotFile(Stamp))
        return false;
    FileTimes.Add(Stamp, IFileManager::Get().GetTimeStamp(*Stamp));
    for (const auto& Dependency : Held.UnqualifiedDependencies)
    {
        const FString Saved = FPackageName::LongPackageNameToFilename(
            Dependency.Package, FPackageName::GetAssetPackageExtension());
        if (!SnapshotFile(Saved))
            return false;
        const FString Body = FPaths::ChangeExtension(Saved, TEXT("uexp"));
        if (IFileManager::Get().FileExists(*Body) && !SnapshotFile(Body))
            return false;
    }
    for (const auto& Blueprint : Held.RecoveredBlueprints)
    {
        const FString Saved = FPackageName::LongPackageNameToFilename(
            Blueprint.Package, FPackageName::GetAssetPackageExtension());
        if (!SnapshotFile(Saved))
            return false;
        for (const TCHAR* Extension : {TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl")})
        {
            const FString Companion = FPaths::ChangeExtension(Saved, Extension);
            if (IFileManager::Get().FileExists(*Companion) && !SnapshotFile(Companion))
                return false;
        }
    }
    FP4KadanImportSourceScope SourceScope(Plan.Scanned);
    FP4KadanSourceBinding UnsupportedSource;
    FString UnsupportedSourceError;
    if (!TestTrue(TEXT("The one refused GPU source has exact current bounded H+B authority"),
            P4KadanReadBoundSource(UnsupportedParticle, UnsupportedSource, UnsupportedSourceError)))
    {
        AddError(UnsupportedSourceError);
        return false;
    }
    for (const auto& Source : Plan.Assets)
    {
        if (Source->Kind != TEXT("World"))
            continue;
        const auto Inventory = P4ReadStrictWorldClassInventory(Source->Package, Source->Header, Source->Body, true);
        if (!TestTrue(TEXT("Every existing world has a current known source inventory: ") + Source->Package +
                    TEXT(" ") + Inventory.Error,
                Inventory.Known))
            return false;
        UPackage* Package = FindPackage(nullptr, *Source->Package);
        UWorld* World = Package ? UWorld::FindWorldInPackage(Package) : nullptr;
        if (!TestNotNull(TEXT("The inventoried native world remains loaded"), World))
            return false;
        FResumedClassWorldSnapshot Snapshot;
        Snapshot.World = World;
        Snapshot.Package = Source->Package;
        Snapshot.Record = Package->GetMetaData()->GetValue(World, TEXT("P4OriginalRecord"));
        Snapshot.Baseline = Package->GetMetaData()->GetValue(World, TEXT("P4NativeLevelBaseline"));
        Snapshot.Custody = Package->GetMetaData()->GetValue(World, TEXT("P4GameBackedLevelCustody"));
        Snapshot.Dirty = Package->IsDirty();
        Snapshot.SourceDigest = Inventory.WorldSourceDigest;
        if (!TestTrue(TEXT("Every source world retains original record and exact baseline"),
                !Snapshot.Record.IsEmpty() && !Snapshot.Baseline.IsEmpty() && Snapshot.Custody.IsEmpty()))
            return false;
        for (const FString& Class : Inventory.RequiredClassPackages)
        {
            Snapshot.RequiredClasses.Add(Class);
            ExpectedClasses.Add(Class);
        }
        Held.Worlds.Add(MoveTemp(Snapshot));
        const FString SavedMap = FPackageName::LongPackageNameToFilename(
            Source->Package, FPackageName::GetMapPackageExtension());
        if (!SnapshotFile(SavedMap))
            return false;
        for (const TCHAR* Extension : {TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl")})
        {
            const FString Companion = FPaths::ChangeExtension(SavedMap, Extension);
            if (IFileManager::Get().FileExists(*Companion) && !SnapshotFile(Companion))
                return false;
        }
    }
    if (!TestEqual(TEXT("The retained Cave000 fixture supplies all thirteen existing layers"), Held.Worlds.Num(), 13) ||
        !TestTrue(TEXT("The complete required-class union is nonempty and includes actual Env"),
            ExpectedClasses.Num() > 0 && ExpectedClasses.Contains(Env)))
        return false;
    for (const FString& ClassPackage : ExpectedClasses)
    {
        FP4KadanSourceBinding Source;
        UClass* Class = nullptr;
        FString Error;
        if (!TestTrue(
                TEXT("Every required class has a genuine bound source/ancestry: ") + ClassPackage + TEXT(" ") + Error,
                P4ReadStrictNativeClassSource(ClassPackage, Source, Class, Error)))
        {
            AddError(Error);
            return false;
        }
        if (!SnapshotFile(Source.HeaderPath) || !SnapshotFile(Source.BodyPath))
            return false;
        ExpectedClassSourceDigests.Add(ClassPackage, Source.SourceDigest);
    }
    Evidence->SetNumberField(TEXT("worlds"), Held.Worlds.Num());
    Evidence->SetNumberField(TEXT("expected_unique_classes"), ExpectedClasses.Num());
    if (!TestTrue(TEXT("The real retained inputs exercised recovered ReferenceOnly Blueprint policy markers"),
            Held.RecoveredBlueprints.Num() > 0))
        return false;
    bool PreservationChecked = false;
    const auto VerifyPreserved = [&]()
    {
        if (PreservationChecked)
            return;
        PreservationChecked = true;
        bool Same = true;
        Same &= TestTrue(TEXT("The refused GPU asset has no loaded destination package or object and no publication"),
            !FPackageName::DoesPackageExist(UnsupportedParticle) && !FindPackage(nullptr, *UnsupportedParticle) &&
                !FindObject<UObject>(nullptr, *UnsupportedObject));
        FString SourceError;
        Same &= TestTrue(TEXT("The refused GPU source canonical H+B snapshot remains unchanged"),
            P4KadanSourceBindingCurrent(UnsupportedSource, SourceError));
        TArray<TSharedPtr<FJsonValue>> Acquisitions;
        for (const auto& Cached : CachedParticleHeaders)
        {
            const FString Object = Cached.Key + TEXT(".") + FPackageName::GetShortName(Cached.Key);
            const bool Untouched = !FPackageName::DoesPackageExist(Cached.Key) && !FindPackage(nullptr, *Cached.Key) &&
                !FindObject<UObject>(nullptr, *Object);
            Same &= TestTrue(
                TEXT("Cache acquisition publishes/loads no editable destination: ") + Cached.Key, Untouched);
            auto Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("package"), Cached.Key);
            Row->SetBoolField(TEXT("verified_original_game_identity"), Game.Contains(FName(*Cached.Key)));
            Row->SetBoolField(TEXT("qualified"), false);
            Row->SetBoolField(TEXT("no_destination_publication"), Untouched);
            Row->SetStringField(TEXT("kind"), TEXT("ParticleSystem"));
            Row->SetStringField(TEXT("route"),
                TEXT(
                    "Existing converted-cache hardlink staging; normal kept-game acquisition only, no native reconstruction attempted."));
            Acquisitions.Add(MakeShared<FJsonValueObject>(Row));
        }
        Evidence->SetArrayField(TEXT("unqualified_acquisition_only_game_references"), Acquisitions);
        TArray<TSharedPtr<FJsonValue>> Dependencies;
        for (const auto& Dependency : Held.UnqualifiedDependencies)
        {
            bool DependencySame = P4ExactNativeString(Dependency.Record,
                                      Dependency.Asset->GetOutermost()->GetMetaData()->GetValue(
                                          Dependency.Asset, *Dependency.RecordKey)) &&
                Dependency.Asset->GetOutermost()->IsDirty() == Dependency.Dirty;
            for (const auto& Property : Dependency.Properties)
                DependencySame &= Property->Property->Identical(
                    Property->Value, Property->Property->ContainerPtrToValuePtr<void>(Dependency.Asset), PPF_None);
            Same &= TestTrue(TEXT("Preserved reference-only material source/live record stays exact without repair: ") +
                    Dependency.Package,
                DependencySame);
            auto Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("package"), Dependency.Package);
            Row->SetBoolField(TEXT("preserved"), DependencySame);
            Row->SetBoolField(TEXT("qualified"), false);
            Row->SetStringField(TEXT("reason"),
                TEXT(
                    "Existing reference-only material preserved; material and whole-asset closure remain unqualified."));
            Dependencies.Add(MakeShared<FJsonValueObject>(Row));
        }
        Evidence->SetArrayField(TEXT("preserved_unqualified_dependencies"), Dependencies);
        TArray<TSharedPtr<FJsonValue>> BlueprintPolicies;
        for (const auto& Blueprint : Held.RecoveredBlueprints)
        {
            UPackage* Package = Blueprint.Asset->GetOutermost();
            UMetaData* Metadata = Package->GetMetaData();
            const bool BlueprintSame = Blueprint.Asset->GeneratedClass == Blueprint.Class &&
                Blueprint.Class->GetDefaultObject(false) == Blueprint.DefaultObject &&
                Package->IsDirty() == Blueprint.Dirty &&
                P4ExactNativeString(Blueprint.Record, Metadata->GetValue(Blueprint.Asset, TEXT("P4OriginalRecord"))) &&
                P4ExactNativeString(
                    Blueprint.SourceMembers, Metadata->GetValue(Blueprint.Asset, TEXT("P4BlueprintSourceMembers"))) &&
                P4ExactNativeString(
                    Blueprint.ImportVersion, Metadata->GetValue(Blueprint.Asset, TEXT("P4BlueprintImportVersion"))) &&
                !UP4BlueprintImport::IsStandIn(Blueprint.Asset) &&
                !P4BlueprintReconstructionIncomplete(Blueprint.Asset);
            Same &= TestTrue(
                TEXT(
                    "Recovered Blueprint policy/source metadata and class/CDO stay intact without unlock/rebaseline: ") +
                    Blueprint.Package,
                BlueprintSame);
            auto Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("package"), Blueprint.Package);
            Row->SetBoolField(TEXT("policy_reference_only"), true);
            Row->SetBoolField(TEXT("placeholder"), false);
            Row->SetStringField(TEXT("fidelity"), TEXT("recovered"));
            Row->SetStringField(TEXT("source_digest_kind"), TEXT("blueprint-import"));
            Row->SetBoolField(TEXT("preserved"), BlueprintSame);
            Row->SetBoolField(TEXT("requires_separate_current_native_class_proof"), true);
            BlueprintPolicies.Add(MakeShared<FJsonValueObject>(Row));
        }
        Evidence->SetArrayField(TEXT("preserved_recovered_blueprint_policy_markers"), BlueprintPolicies);
        for (const auto& Snapshot : Held.Worlds)
        {
            UPackage* Package = Snapshot.World->GetOutermost();
            Same &= TestTrue(TEXT("Resume never rewrites original world metadata: ") + Snapshot.Package,
                P4ExactNativeString(
                    Snapshot.Record, Package->GetMetaData()->GetValue(Snapshot.World, TEXT("P4OriginalRecord"))));
            Same &= TestTrue(TEXT("Resume never rebaselines the world: ") + Snapshot.Package,
                P4ExactNativeString(Snapshot.Baseline,
                    Package->GetMetaData()->GetValue(Snapshot.World, TEXT("P4NativeLevelBaseline"))));
            Same &= TestTrue(TEXT("Resume never adds game-backed custody"),
                P4ExactNativeString(Snapshot.Custody,
                    Package->GetMetaData()->GetValue(Snapshot.World, TEXT("P4GameBackedLevelCustody"))));
            Same &= TestEqual(
                TEXT("Resume preserves world dirty state: ") + Snapshot.Package, Package->IsDirty(), Snapshot.Dirty);
        }
        for (const auto& File : FileDigests)
        {
            FString Digest;
            int64 Size = 0;
            Same &= TestTrue(TEXT("Source and saved-world bytes remain unchanged: ") + File.Key,
                P4HashFile(File.Key, 64 * 1024 * 1024, Size, Digest) && P4ExactNativeString(Digest, File.Value));
        }
        for (const auto& File : FileTimes)
            Same &= TestTrue(TEXT("Retained converted source/stamp modification time is unchanged: ") + File.Key,
                IFileManager::Get().GetTimeStamp(*File.Key) == File.Value);
        Evidence->SetBoolField(TEXT("world_records_baselines_dirty_and_source_bytes_preserved"), Same);
    };
    ON_SCOPE_EXIT
    {
        VerifyPreserved();
    };
    Evidence->SetStringField(TEXT("stage"), TEXT("ordinary-strict-resume"));
    const auto Result = P4KadanImportMap(Settings, Map, true);
    TestTrue(TEXT("The ordinary import report is retained verbatim"),
        FFileHelper::SaveStringToFile(Result.Report, *(Folder / TEXT("resumed-class-postflight.import.json")),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    TSharedPtr<FJsonObject> Report;
    if (!TestTrue(TEXT("Ordinary strict resume returns actual JSON"), ResumedClassReadJson(Result.Report, Report)))
        return false;
    const TSharedPtr<FJsonObject>* Steps = nullptr;
    if (!TestTrue(TEXT("Ordinary importer exposes actual cache staging counters"),
            Report->TryGetObjectField(TEXT("steps"), Steps)))
        return false;
    const int32 PlacedFromContainers = int32((*Steps)->GetNumberField(TEXT("packages placed from the containers")));
    const int32 CacheHits = int32((*Steps)->GetNumberField(TEXT("of them already converted")));
    TestEqual(TEXT("Only the exact eleven kept-game source pairs were staged"), PlacedFromContainers, 11);
    TestEqual(TEXT("All eleven staged sources reused the retained cache"), CacheHits, 11);
    TestEqual(TEXT("No source conversion process was needed"), PlacedFromContainers - CacheHits, 0);
    Evidence->SetNumberField(TEXT("cache_hit_hardlink_stagings"), CacheHits);
    Evidence->SetNumberField(TEXT("new_conversions"), PlacedFromContainers - CacheHits);
    const TSharedPtr<FJsonObject>* PostflightPointer = nullptr;
    if (!TestTrue(TEXT("Resumed worlds cannot elide final native class postflight"),
            Report->TryGetObjectField(TEXT("native_class_postflight"), PostflightPointer)))
        return false;
    const auto& Postflight = *PostflightPointer;
    const TArray<TSharedPtr<FJsonValue>>* Classes = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Worlds = nullptr;
    if (!TestTrue(TEXT("Postflight supplies actual unique classes and every world"),
            Postflight->TryGetArrayField(TEXT("classes"), Classes) &&
                Postflight->TryGetArrayField(TEXT("worlds"), Worlds)))
        return false;
    TestEqual(TEXT("No selected/resumed world inventory disappeared"), Worlds->Num(), Held.Worlds.Num());
    TestEqual(TEXT("Required class union is complete and compared once"), Classes->Num(), ExpectedClasses.Num());
    TMap<FString, bool> QualifiedClasses;
    const FJsonObject* EnvRow = nullptr;
    bool AnyClassFailure = false;
    for (const auto& Value : *Classes)
    {
        const auto Row = Value->AsObject();
        if (!TestTrue(TEXT("Class proof row is an object"), Row.IsValid()))
            return false;
        const FString Package = Row->GetStringField(TEXT("package"));
        const bool Qualified = Row->GetBoolField(TEXT("qualified"));
        TestTrue(TEXT("Class proof belongs to the exact expected union without duplicates"),
            ExpectedClasses.Contains(Package) && !QualifiedClasses.Contains(Package));
        QualifiedClasses.Add(Package, Qualified);
        if (Package == Env)
            EnvRow = Row.Get();
        const int32 Unheld = int32(Row->GetNumberField(TEXT("values_not_held")));
        const int32 Differences = int32(Row->GetNumberField(TEXT("differences")));
        TestTrue(TEXT("Every class proof is bound to the actual current source snapshot"),
            ExpectedClassSourceDigests.Contains(Package) &&
                P4ExactNativeString(Row->GetStringField(TEXT("source_sha256")), ExpectedClassSourceDigests[Package]));
        if (Unheld != 0 || Differences != 0)
            TestFalse(TEXT("Unheld or unclosed class facts cannot qualify"), Qualified);
        if (Qualified)
        {
            TestTrue(TEXT("Successful class proof has known nonempty compared facts"),
                Row->GetNumberField(TEXT("compared")) > 0);
            TestTrue(TEXT("Successful class proof retains exact native fact SHA"),
                Row->GetStringField(TEXT("native_default_facts_sha256")).Len() == 64);
        }
        AnyClassFailure |= !Qualified;
    }
    if (!TestNotNull(TEXT("The actual Env class gap cannot disappear from the union"), EnvRow))
        return false;
    const int32 EnvUnheld = int32(EnvRow->GetNumberField(TEXT("values_not_held")));
    Evidence->SetNumberField(TEXT("env_values_not_held"), EnvUnheld);
    if (EnvUnheld > 0)
    {
        TestFalse(TEXT("Existing Env unsupported native fields force explicit class refusal"),
            EnvRow->GetBoolField(TEXT("qualified")));
        TestTrue(TEXT("Env refusal carries actual compared coverage and an explicit reason"),
            EnvRow->GetNumberField(TEXT("compared")) > 0 && !EnvRow->GetStringField(TEXT("reason")).IsEmpty());
    }
    else
        TestTrue(TEXT("Zero Env omissions is accepted only after genuine complete native qualification"),
            EnvRow->GetBoolField(TEXT("qualified")) && EnvRow->GetNumberField(TEXT("compared")) > 0 &&
                EnvRow->GetNumberField(TEXT("differences")) == 0);
    const TSharedPtr<FJsonObject>* Assets = nullptr;
    if (!TestTrue(TEXT("Actual per-world native replies remain available"),
            Report->TryGetObjectField(TEXT("assets"), Assets)))
        return false;
    const TSharedPtr<FJsonObject>* GpuReply = nullptr;
    if (!TestTrue(TEXT("The unchanged full map importer reports the exact GPU refusal"),
            (*Assets)->TryGetObjectField(UnsupportedParticle, GpuReply)))
        return false;
    const FString GpuError = (*GpuReply)->GetStringField(TEXT("error"));
    TestTrue(TEXT("Known unsupported editor GPU shape remains the exact NativeDataUnsupported diagnostic"),
        GpuError.StartsWith(TEXT("particle-native-gpu-editor-distribution-unavailable:"), ESearchCase::CaseSensitive));
    TestFalse(
        TEXT("The refused particle is never reconstructed successfully"), (*GpuReply)->GetBoolField(TEXT("valid")));
    TestFalse(TEXT("The refused particle creates no editable asset"), (*GpuReply)->GetBoolField(TEXT("created")));
    TestFalse(TEXT("The refused particle is never saved"), (*GpuReply)->GetBoolField(TEXT("saved")));
    TestTrue(TEXT("The real original identity is kept explicitly as an unqualified game reference"),
        (*GpuReply)->GetBoolField(TEXT("reference_only")) && (*GpuReply)->GetBoolField(TEXT("kept_as_the_games")) &&
            Result.GameReferenceReasons.Contains(UnsupportedParticle) &&
            !Result.Imported.Contains(UnsupportedParticle));
    auto UnqualifiedGameReference = MakeShared<FJsonObject>();
    UnqualifiedGameReference->SetStringField(TEXT("package"), UnsupportedParticle);
    UnqualifiedGameReference->SetStringField(TEXT("error"), GpuError);
    UnqualifiedGameReference->SetStringField(TEXT("source_sha256"), UnsupportedSource.SourceDigest);
    UnqualifiedGameReference->SetStringField(TEXT("expected_native_error"), TEXT("NativeDataUnsupported"));
    UnqualifiedGameReference->SetBoolField(
        TEXT("verified_original_game_identity"), Game.Contains(FName(*UnsupportedParticle)));
    UnqualifiedGameReference->SetBoolField(TEXT("qualified"), false);
    Evidence->SetObjectField(TEXT("unqualified_original_game_reference"), UnqualifiedGameReference);
    for (const auto& Source : Plan.Assets)
    {
        if (Source->Kind == TEXT("World") || P4ExactNativeString(Source->Package, UnsupportedParticle))
            continue;
        const TSharedPtr<FJsonObject>* Preserved = nullptr;
        if (!TestTrue(TEXT("Every other planned executable has a preservation reply: ") + Source->Package,
                (*Assets)->TryGetObjectField(Source->Package, Preserved)))
            return false;
        TestTrue(TEXT("Every other planned executable is skipped intact rather than reconstructed: ") + Source->Package,
            (*Preserved)->GetBoolField(TEXT("valid")) &&
                (*Preserved)->GetStringField(TEXT("state")) == TEXT("existing asset preserved"));
        bool Mutated = false;
        if ((*Preserved)->TryGetBoolField(TEXT("created"), Mutated))
            TestFalse(TEXT("An existing executable is never recreated"), Mutated);
        if ((*Preserved)->TryGetBoolField(TEXT("saved"), Mutated))
            TestFalse(TEXT("An existing executable is never rewritten"), Mutated);
    }
    TSet<FString> SeenWorlds;
    for (const auto& Value : *Worlds)
    {
        const auto Row = Value->AsObject();
        if (!TestTrue(TEXT("World proof row is an object"), Row.IsValid()))
            return false;
        const FString Package = Row->GetStringField(TEXT("package"));
        const auto* Snapshot = Held.Worlds.FindByPredicate(
            [&](const FResumedClassWorldSnapshot& World) { return P4ExactNativeString(World.Package, Package); });
        if (!TestTrue(TEXT("Every proof names a distinct actual existing selected world"),
                Snapshot && !SeenWorlds.Contains(Package)))
            return false;
        SeenWorlds.Add(Package);
        TestTrue(TEXT("Existing worlds are explicitly marked resumed"), Row->GetBoolField(TEXT("resumed")));
        TestTrue(TEXT("World source SHA is the freshly read exact inventory SHA"),
            P4ExactNativeString(Row->GetStringField(TEXT("source_sha256")), Snapshot->SourceDigest));
        TestEqual(TEXT("World required classes retain complete source inventory"),
            int32(Row->GetNumberField(TEXT("required_classes"))), Snapshot->RequiredClasses.Num());
        bool ExpectedQualified = true;
        for (const FString& Class : Snapshot->RequiredClasses)
            ExpectedQualified &= QualifiedClasses.Contains(Class) && QualifiedClasses[Class];
        TestEqual(TEXT("World class qualification reflects every required class"), Row->GetBoolField(TEXT("qualified")),
            ExpectedQualified);
        const TSharedPtr<FJsonObject>* Reply = nullptr;
        if (!TestTrue(TEXT("Existing world has an actual ordinary native resume reply"),
                (*Assets)->TryGetObjectField(Package, Reply)))
            return false;
        TestTrue(TEXT("Resume preserves its narrower placed-level proof"),
            (*Reply)->GetBoolField(TEXT("native_level_valid")));
        TestFalse(TEXT("No resumed world is recreated"), (*Reply)->GetBoolField(TEXT("created")));
        TestFalse(TEXT("No resumed world is saved or rebaselined"), (*Reply)->GetBoolField(TEXT("saved")));
        TestFalse(TEXT("No resumed world silently becomes a partial projection"),
            (*Reply)->GetBoolField(TEXT("game_backed")));
        TestEqual(TEXT("Per-world native reply exposes the same complete class qualification"),
            (*Reply)->GetBoolField(TEXT("native_class_defaults_qualified")), ExpectedQualified);
        TestEqual(TEXT("Final per-world validity requires class coverage too"), (*Reply)->GetBoolField(TEXT("valid")),
            ExpectedQualified);
    }
    TestEqual(TEXT("Overall strict import cannot succeed with any class gap"), Result.Ok, !AnyClassFailure);
    TestEqual(TEXT("Postflight aggregate qualification matches actual class proof"),
        Postflight->GetBoolField(TEXT("qualified")), !AnyClassFailure);
    if (AnyClassFailure)
        TestTrue(TEXT("Strict refusal is surfaced to the ordinary importer caller"), Result.Problems.Num() > 0);
    TestFalse(TEXT("Existing fixture proof never cancels or deletes content"), Result.Cancelled);
    Evidence->SetBoolField(TEXT("strict_result_ok"), Result.Ok);
    Evidence->SetBoolField(TEXT("any_class_failure"), AnyClassFailure);
    Evidence->SetStringField(TEXT("stage"), TEXT("completed"));
    VerifyPreserved();
    return !HasAnyErrors();
}
#endif

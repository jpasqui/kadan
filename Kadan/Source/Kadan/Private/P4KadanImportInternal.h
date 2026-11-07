// Internal to the Kadan import: the run of one import plan (P4KadanRunImport).
#pragma once

#include "P4KadanReimportAsset.h"
#include "P4Common.h"
#include "P4ParticleReference.h"
#include "P4KadanImport.h"
#include "P4CreatorSource.h"
#include "P4OriginalRecord.h"
#include "P4KadanOriginalConversion.h"
#include "P4KadanAcquisitionPolicy.h"
#include "P4KadanAcquisitionVeto.h"
#include "P4KadanImportSources.h"
#include "P4OriginalImportSave.h"
#include "P4AuthoringRows.h"
#include "P4BlueprintImport.h"
#include "P4OriginalTextureImport.h"
#include "P4OriginalMeshImport.h"
#include "P4OriginalAssetImport.h"
#include "P4SoundReference.h"
#include "P4OriginalLevelImport.h"
#include "P4LevelChanges.h"
#include "FileHelpers.h"
#include "PackageTools.h"
#include "Engine/World.h"
#include "P4MaterialRecovery.h"
#include "Materials/Material.h"
#include "P4MaterialDependencyRetention.h"
#include "P4AnimationDependencyRetention.h"
#include "P4MaterialCancelPoll.h"
#include "P4MaterialFamilyAcquireStage.h"
#include "P4StrictNativeClassSources.h"
#include "P4OriginalClassSourceComparison.h"
#include "P4KadanSourceAcquire.h"
#include "P4KadanBlueprintPlans.h"
#include "P4OriginalZenSource.h"
#include "P4OriginalWorldSource.h"
#include "P4ExportProcess.h"
#include "P4ExportContainer.h"
#include "P4NativeHash.h"
#include "P4KadanContainerIndex.h"
#include "P4KadanSourceCatalog.h"
#include "P4MapKitCore/package_reader.h"
#include "AssetRegistryModule.h"
#include "Editor.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/ScopeExit.h"
#include "Misc/SecureHash.h"
#include "Misc/ScopeLock.h"
#include "Async/ParallelFor.h"
#include "Templates/Atomic.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "P4KadanLibrary.h"

// The helpers the import's files share (P4KadanAssetKinds.cpp, P4KadanImportScan.cpp, P4KadanImportReport.cpp).
namespace P4KadanImport
{
bool RetainAcquiredWorldSource(const FP4KadanImportSettings& Settings, const FP4KadanContainerIndex& Index,
    FP4KadanSourceAsset& Asset, FString& Error);
FString JsonText(const TSharedRef<FJsonObject>& Object);
TSharedRef<FJsonObject> NativeImportReport(bool Valid, const FString& Error, const FString& Package,
    const FString& AssetPath, bool Created, bool Saved, bool FilledStandIn, const FString& State);
bool ExistingReal(const FString& Package);
bool RetainedNativeKind(const FName Kind);
bool ExistingRetainedNativeKind(const FString& Package);
bool ExistingLevelPackage(const FString& Package);
bool ExistingBlueprintClass(const FString& Package, bool IncludeWidget = false);
int32 Rank(const FString& Kind);
bool FollowsSoftReferences(const FString& Kind);
bool SoftDependency(const FP4KadanSourceAsset& Asset, const FString& Package);
bool BlueprintKind(const FString& Kind);
FString KindName(const FString& Original);
bool ExtractFromContainers(const FP4KadanImportSettings& Settings, const FP4KadanContainerIndex& Index,
    const FString& Package, const FString& Into, FString& Error);
FString ContainerKey(const FP4KadanImportSettings& Settings);
FString ConfiguredConversionCache(const FP4KadanImportSettings& Settings);
FString CurrentConversionCacheIdentity(const FP4KadanImportSettings& Settings);
bool ConvertOriginalInCache(const FP4KadanImportSettings& Settings, const FP4KadanContainerIndex& Index,
    const FString& Package, const FString& Into, const FString& Cache, FString& Error, bool* FromCache);
} // namespace P4KadanImport

// One run of an import plan (P4KadanRunImport): the plan's packages imported in their order, what they
// need brought in from the game folder and the game's containers, and a report of what was made, kept
// as the game's or left out. Each step of the run is a function below; the state the steps share is
// the members, declared in the order the run makes them, so that they are released in reverse order.
class FP4KadanImportRun
{
public:
    FP4KadanImportRun(const FP4KadanImportSettings& InSettings, const FP4KadanImportPlan& InPlan, bool InResume);
    FP4KadanImportRun(const FP4KadanImportRun&) = delete;
    FP4KadanImportRun& operator=(const FP4KadanImportRun&) = delete;
    FP4KadanImportResult Execute();

private:
    // A material family's sources, stage, recovery and shader-used resources, kept for the rest of the
    // import once its first member has done that work, so that its other members do not do it again: a
    // family of 123 members converted and traced its whole family 123 times. Each family stage has its own
    // folder, so the stages of several families can stay open. The recovery is declared after the stage,
    // so it is released first.
    struct FFamilyRun
    {
        FP4MaterialFamilySourcesPtr CompleteFamily;
        FP4MaterialFamilyPreparedStagePtr PreparedFamily;
        TSharedPtr<FP4MaterialRecovery> Recovery;
        TArray<TSharedPtr<FP4KadanSourceAsset>> RecoverySources, UsedResourceRows;
        TSharedPtr<FP4MaterialUsedResources> FinalUsed;
        TSharedPtr<FJsonObject> Evidence;
        FString Problem;
        bool Refused = false;
    };
    // What the import of one package has found so far, shared by the steps of that import.
    struct FAssetImport
    {
        explicit FAssetImport(const TSharedPtr<FP4KadanSourceAsset>& InAsset) : Asset(InAsset) {}
        const TSharedPtr<FP4KadanSourceAsset>& Asset;
        // A package the asset needs that did not import. Empty when there is none.
        FString Missing;
        bool DeferredMICAdmission = false;
        bool DeferredMasterDependencyAdmission = false;
        // Declared before Reimport, so rollback/Finish and its native save
        // comparison cannot outlive the exact prepared family source scope.
        TUniquePtr<FP4KadanImportSourceScope> MaterialComparisonSources;
        TUniquePtr<FP4KadanReimportAsset> Reimport;
        TSharedPtr<FJsonObject> Reply;
        bool HasNativeResult = false, NativeValid = false, NativeFailureFatal = false, MayKeepGameIdentity = false;
        FString NativeError;
    };
    // A material's family as the material's import reads it. FamilyRecovery is declared after
    // PreparedFamily, so it is released first.
    struct FMaterialImport
    {
        FP4MaterialFamilySourcesPtr CompleteFamily;
        FP4MaterialFamilyPreparedStagePtr PreparedFamily;
        TSharedPtr<FP4MaterialRecovery> FamilyRecovery;
        TArray<TSharedPtr<FP4KadanSourceAsset>> RecoverySources, UsedResourceRows;
        TSharedPtr<FP4MaterialUsedResources> FinalUsed;
        TSharedPtr<FJsonObject> FamilyEvidence;
        FString FamilyProblem;
        bool FamilySourceRefused = false;
        const TSharedPtr<FFamilyRun>* Reused = nullptr;
        FP4MaterialRecovery* Recovery = nullptr;
        TFunction<bool(FString&)> CurrentFamily;
    };
    // Set when a step has ended the import of a package: whether the run goes on. Unset when the
    // package's import goes on to its next step.
    using FImportOutcome = TOptional<bool>;

    // The report (P4KadanImportReport.cpp).
    void Spent(const FString& Step, double Since);
    void WaitForMemory();
    void ReleaseFinishedAssets();
    void Checkpoint(bool Force = false);
    FP4KadanImportResult Finish();
    bool VerifiedGameIdentity(const FP4KadanSourceAsset& Asset, FString& Detail);
    void Failed(
        const FP4KadanSourceAsset& Asset, const FString& Text, bool Fatal = false, bool MayKeepGameIdentity = false);

    // The run (P4KadanImport.cpp).
    static bool AssetCanRemainInGame(EP4OriginalAssetImportError Error);
    TArray<FString> ExecutionDependencies(const FP4KadanSourceAsset& Asset);
    FString NeededAndMissing(const FP4KadanSourceAsset& Asset);
    void SetUpMaterialRecovery();
    bool CheckRequestedSources();
    void StartProgress();
    void ImportAssets();

    // One package that is not a Blueprint (P4KadanImportAsset.cpp, P4KadanImportMaterial.cpp).
    bool ImportOne(const TSharedPtr<FP4KadanSourceAsset>& Asset);
    FImportOutcome AdmitAsset(FAssetImport& Item);
    bool BeginReimport(FAssetImport& Item, bool& Finished);
    FImportOutcome ImportMaterial(FAssetImport& Item);
    void AcquireMaterialShaderArchive();
    FImportOutcome PrepareMaterialFamily(FAssetImport& Item, FMaterialImport& Family);
    void WatchMaterialFamily(FMaterialImport& Family);
    FImportOutcome DiscoverFamilyUsedResources(FAssetImport& Item, FMaterialImport& Family);
    void KeepFamilyRun(FMaterialImport& Family);
    FImportOutcome AdmitExistingMaterial(FAssetImport& Item, FMaterialImport& Family);
    void RecoverMaterial(FAssetImport& Item, FMaterialImport& Family);
    void ImportTexture(FAssetImport& Item);
    void ImportStaticMesh(FAssetImport& Item);
    void ImportWorld(FAssetImport& Item);
    void ReportWorldImport(FAssetImport& Item, const FP4OriginalLevelImportResult& Imported);
    void ImportOtherAsset(FAssetImport& Item);
    void ImportBuiltLighting(FAssetImport& Item);
    bool FinishAssetImport(FAssetImport& Item);

    // What the plan's packages need (P4KadanImportClosure.cpp).
    void KeepForReference(const TSharedPtr<FP4KadanSourceAsset>& Asset);
    TArray<TSharedPtr<FP4KadanSourceAsset>> BringIn(const TArray<FString>& Wanted);
    void ConvertFromContainers(const TArray<FString>& Pending, TSet<FName>& Tried);
    bool BringInSelected();
    bool BringInKeptDependencies();
    bool DiscoverTableClassDeclarations();
    void DeferTableDependents();
    void KeepNamed(const FString& Package, int32 Depth);

    // Blueprints and the classes tables name by type (P4KadanImportBlueprints.cpp).
    bool DeclareAnimationCycles();
    void SnapshotExistingClasses();
    bool HasDeferredDependencies();
    bool ImportDeclaredDependencies(const TMap<FName, UClass*>& Declared, FString& Error);
    bool ImportBlueprintBatch();
    void PlanBlueprints(TArray<TSharedPtr<FP4KadanSourceAsset>>& Selected, TArray<FString>& BlueprintPackages,
        FP4KadanBlueprintPlans& BlueprintPlans, FP4OriginalZenSourceBatch& BlueprintOriginals, bool& PlanFailed);
    bool KeepBlueprintReferences(
        const TArray<TSharedPtr<FP4KadanSourceAsset>>& Selected, const FP4KadanBlueprintPlans& BlueprintPlans);
    // Brings in the kept game assets (effects, sounds, tables) the named packages reference, with those they name.
    bool KeepReferences(TArray<FString> Walk);
    bool RunBlueprintBatch(const TArray<TSharedPtr<FP4KadanSourceAsset>>& Selected,
        const TArray<FString>& BlueprintPackages, const FP4KadanBlueprintPlans& BlueprintPlans);
    void ImportRemainingDeclaredDependencies();

    // Checks of what the run made (P4KadanImportVerify.cpp).
    void ReimportLevelsChangedWhenOpened();
    void FindLevelsChangedWhenOpened(TMap<FString, TSet<FString>>& Unstable, TMap<FString, FString>& UnstableWhy,
        TMap<FString, FString>& UnstableDetails);
    void ProveStrictClassDefaults();
    void ReportStrictClassDefaults(const FP4StrictRequiredClassPostflight& Proof);
    void ReleaseSavedLevels();

    const FP4KadanImportSettings& Settings;
    const FP4KadanImportPlan& Plan;
    const bool Resume;
    P4ImportPublication::FImportScope Import;
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    TSharedRef<FJsonObject> Replies = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Problems;
    TArray<FString> Warnings;
    FP4KadanImportResult Out;
    TSet<FString> SavedWorlds;
    FP4KadanReimportScope ReimportScope;
    TMap<FString, TSharedPtr<FP4KadanSourceAsset>> StrictWorldSources;
    TSet<FString> StrictResumedWorlds;
    bool Cancelled = false;
    const FString Folder;
    const FString File;
    bool ReportWriteFailed = false;
    // Where the run's time goes, by step, in the report's "seconds" (the steps overlap only where named).
    TMap<FString, double> Seconds;
    TMap<FString, int32> Steps;
    // How much memory the editor gained during each step, and how close the computer came to running out.
    TMap<FString, double> MemoryGrowth;
    double LastUsedMemory = 0.0;
    double LowestAvailableMemory = TNumericLimits<double>::Max();
    double MemoryWaitLeft = 120.0;
    double MemoryAtLastRelease = 0.0;
    TSet<FObjectKey> PackagesBeforeRun;
    double RunStarted = 0.0;
    double LastCheckpoint = -1e9;
    // Packages a map import leaves the game's (sound, interface, effects, fonts, media, tables), with the
    // source each was read from. The ones a Blueprint names are kept for reference from the game's file;
    // the others are named when the import ends.
    TSet<FName> KeptAsTheGames;
    TMap<FName, TSharedPtr<FP4KadanSourceAsset>> KeptSources;
    TSet<FName> KeptHandled;
    // Warnings that a package is in neither the game folder nor the project, by that package.
    TMap<FString, FName> MissingWarnings;
    FString Why;
    bool BlueprintsRun = false;
    bool FatalFailure = false;
    // A package that did not import, and every package that needs it, is left out and told. Soft
    // references do not count: a path to a package that is not there is still a path.
    TSet<FName> NotImported, ImportedIdentities, GameBackedPackages;
    // Best effort (the editor's import): a package that cannot be rebuilt stays the game's, and a level
    // keeps the actors that need it in the game. Only a stopped or broken run ends the import.
    const bool BestEffort;
    // Packages whose material execution source could not be read; best effort leaves them the game's.
    TMap<FName, FString> UnreadableSources;
    // Levels this run made complete (best effort), checked once their map is opened at the end.
    TMap<FString, TSharedPtr<FP4KadanSourceAsset>> StrictMade;
    FP4KadanExecutionEdges ExecutionEdges;
    // With game-backed content allowed, a package of a kind a map import keeps as the game's (sounds,
    // interface, fonts, effects, tables) that could not be kept for reference stays the game's, as it
    // did when such packages were not read at all: what names it keeps the game's reference.
    TSet<FName> StaysTheGames;
    // Both optional game-reference inventory and material family queries keep
    // this same explicit run lifetime; the registry factory owns only a weak slot.
    FP4CurrentGameRegistryPtr MaterialRegistry;
    bool MaterialRegistryAttempted = false;
    FString MaterialRegistryProblem;
    FP4ExportGameInventory ReferenceInventory;
    bool ReferenceInventoryAttempted = false;
    FString ReferenceInventoryError;
    FString RecoveryArchive;
    FString RecoveryArchiveProblem;
    bool RecoveryArchiveAttempted = false;
    TUniquePtr<FP4MaterialRecovery> MaterialRecovery;
    // What the Blueprints and the planner read: the scanned game folder, and the packages this import
    // converts from the game's containers for objects a Blueprint names that the game folder lacks.
    TArray<TSharedPtr<FP4KadanSourceAsset>> Sources;
    TUniquePtr<FP4KadanImportSourceScope> SourceScope;
    TSet<FName> CyclicAnimations;
    TArray<TSharedPtr<FP4KadanSourceAsset>> Extracted;
    // Packages the project has whose game source was read only for a material that needs it. They are kept.
    TMap<FName, TArray<FP4OriginalTableClassReference>> DeclarationReferences;
    TSet<FName> DeclarationDeferred;
    TMap<FName, FString> DiscoveredTableSourceDigests;
    TUniquePtr<FP4MaterialRecovery> ExtractedRecovery;
    // Only source metadata enters these scoped material recipes. Reverse MICs
    // never enter Sources, the Blueprint scope, BringIn roots or Plan.Assets.
    FP4MaterialFamilyAcquireContextPtr MaterialSourceContext;
    TMap<FName, TSharedPtr<FFamilyRun>> FamilyRuns;
    TFunction<bool()> MaterialShouldCancel;
    const FString Originals;
    // Game assets this computer imported in earlier projects; a resumed run takes them instead of building them.
    FP4KadanLibrary Library;
    void ScanSeededAssets();
    // The project has the package, or a resumed run took it from the library just now.
    bool ExistingOrFromLibrary(const FString& Package);
    bool OriginalsReady = false;
    // Kept-kind packages (effects requests, tables...) an asset names are kept for reference before it,
    // as for a Blueprint, so its references name an asset in the project. From the point the run's
    // table closure is complete (KeepNamed).
    bool KeepNamedReady = false;
    // The packages of the objects a Blueprint names that the game folder lacks, with what they need by
    // hard reference, in import order. The game folder is read first and the game's containers next.
    FP4KadanContainerIndex ContainerIndex;
    bool ContainerIndexAttempted = false, ContainerIndexReady = false;
    FString ContainerIndexError;
    // Anchored only by used native class-typed table values. This finite hard
    // closure may include a required Widget Blueprint; unrelated kept UI does not.
    TSet<FName> TableDeclarationClosure;
    TArray<TSharedPtr<FP4KadanSourceAsset>> AssetsToImport;
    TSet<FName> KeepVisiting;
    TUniquePtr<FScopedSlowTask> Progress;
    P4MaterialCancel::FPostCallbackPoll MaterialCancelPoll;
    // Freeze only complete, already-saved classes before a declaration batch.
    // Resume can preserve an unloaded Blueprint: load its existing generated
    // class now, rather than confusing absence from memory with failed import.
    TMap<FString, UClass*> ExistingClasses;
    TSet<FName> ResolvedDeferredDependencies;
    bool Stop = false;
};

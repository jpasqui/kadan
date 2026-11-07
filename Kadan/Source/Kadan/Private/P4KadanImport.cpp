// One run of an import plan (P4KadanRunImport): the order of its steps.

#include "P4KadanImportInternal.h"
#include "P4GameBuild.h"
#include "P4ExportContainer.h"

using namespace P4KadanImport;

namespace
{
// The levels of the plan, whose saved state a reimport captures before a dependency changes them. None
// when the run resumes.
TSet<FString> WorldsToSave(const FP4KadanImportPlan& Plan, bool Resume)
{
    TSet<FString> SavedWorlds;
    if (!Resume)
        for (const auto& Asset : Plan.Assets)
            if (Asset.IsValid() && Asset->Kind == TEXT("World"))
                SavedWorlds.Add(Asset->Package);
    return SavedWorlds;
}

} // namespace

FP4KadanImportRun::FP4KadanImportRun(
    const FP4KadanImportSettings& InSettings, const FP4KadanImportPlan& InPlan, bool InResume)
    : Settings(InSettings), Plan(InPlan), Resume(InResume), SavedWorlds(WorldsToSave(InPlan, InResume)),
      ReimportScope(!InResume, SavedWorlds), Folder(FPaths::ProjectSavedDir() / TEXT("Kadan")),
      File(Folder / TEXT("import-result.json")), MissingWarnings(InPlan.MissingWarnings),
      BestEffort(InSettings.AllowGameBackedLevels), ExecutionEdges(InPlan.ExecutionEdges),
      RecoveryArchive(InSettings.ShaderArchive), Sources(InPlan.Scanned.Num() ? InPlan.Scanned : InPlan.Assets),
      CyclicAnimations(InPlan.CyclicAnimations), DeclarationReferences(InPlan.DeclarationReferences),
      DeclarationDeferred(InPlan.DeclarationDeferred), Originals(Folder / TEXT("OriginalPackages")),
      Library(InSettings)
{
    MaterialShouldCancel = [this]() { return Cancelled; };
}

FP4KadanImportResult FP4KadanImportRun::Execute()
{
    if (!ReimportScope.Error.IsEmpty())
    {
        Out.Problems.Add(ReimportScope.Error);
        return Out;
    }

    Result->SetObjectField(TEXT("assets"), Replies);
    Result->SetBoolField(TEXT("allow_game_backed_levels"), Settings.AllowGameBackedLevels);
    IFileManager::Get().MakeDirectory(*Folder, true);
    RunStarted = FPlatformTime::Seconds();
    if (!IsInGameThread() || !GEditor)
        Problems.Add(MakeShared<FJsonValueString>(TEXT("Import must run in the editor on its main thread.")));
    const FString Unsupported = P4GameBuildUnsupported(Settings.GamePaks);
    if (!Unsupported.IsEmpty())
        Problems.Add(MakeShared<FJsonValueString>(Unsupported));
    for (const FString& Problem : Plan.Problems)
        Problems.Add(MakeShared<FJsonValueString>(Problem));
    Warnings.Append(Plan.Warnings);
    if (Problems.Num())
        return Finish();
    Checkpoint();
    if (ReportWriteFailed)
        return Finish();
    for (TObjectIterator<UPackage> It; It; ++It)
        PackagesBeforeRun.Add(FObjectKey(*It));
    SetUpMaterialRecovery();
    if (!CheckRequestedSources())
        return Finish();
    if (!BringInSelected())
        return Finish();
    if (!BringInKeptDependencies())
        return Finish();
    if (!DiscoverTableClassDeclarations())
        return Finish();
    DeferTableDependents();
    KeepNamedReady = true;
    StartProgress();
    ImportAssets();
    ScanSeededAssets();
    ImportRemainingDeclaredDependencies();
    ReimportLevelsChangedWhenOpened();
    ProveStrictClassDefaults();
    const FString Preview = (ExtractedRecovery ? *ExtractedRecovery : *MaterialRecovery).PreviewNote();
    if (!Preview.IsEmpty())
        Warnings.AddUnique(Preview);
    return Finish();
}

bool FP4KadanImportRun::AssetCanRemainInGame(EP4OriginalAssetImportError Error)
{
    switch (Error)
    {
        case EP4OriginalAssetImportError::NativeDataUnsupported:
        case EP4OriginalAssetImportError::ObjectOutsideAsset:
        case EP4OriginalAssetImportError::ClassUnavailable:
        case EP4OriginalAssetImportError::PropertyApplicationFailed:
        case EP4OriginalAssetImportError::NativeFillFailed:
        case EP4OriginalAssetImportError::InvalidRowType:
        case EP4OriginalAssetImportError::RowPropertyUnavailable:
        case EP4OriginalAssetImportError::RowPropertyApplicationFailed:
        case EP4OriginalAssetImportError::CookDiffers:
        case EP4OriginalAssetImportError::NativeComparisonFailed:
            return true;
        default:
            return false; // Source/record/save failures and creator destinations are never hidden.
    }
}

TArray<FString> FP4KadanImportRun::ExecutionDependencies(const FP4KadanSourceAsset& Asset)
{
    TArray<FString> Edges;
    FString Error;
    if (!ExecutionEdges.Dependencies(Asset, Edges, Error))
    {
        Problems.Add(MakeShared<FJsonValueString>(Asset.Package + TEXT(" execution source changed: ") + Error));
        FatalFailure = true;
    }
    return Edges;
}

FString FP4KadanImportRun::NeededAndMissing(const FP4KadanSourceAsset& Asset)
{
    for (const FString& Dependency : ExecutionDependencies(Asset))
        if (NotImported.Contains(FName(*Dependency)) && !StaysTheGames.Contains(FName(*Dependency)) &&
            !SoftDependency(Asset, Dependency))
            return Dependency;
    return FString();
}

void FP4KadanImportRun::SetUpMaterialRecovery()
{
    // A master is merged from every one of its instances in the game folder, not only the ones this
    // import needs, so that its graph is the same whichever map brought it in.
    const double RecoveryStarted = FPlatformTime::Seconds();
    MaterialRecovery = MakeUnique<FP4MaterialRecovery>(
        Settings.GameFolder, Plan.Scanned.Num() ? Plan.Scanned : Plan.Assets, RecoveryArchive);
    Spent(TEXT("material recovery setup"), RecoveryStarted);
    SourceScope = MakeUnique<FP4KadanImportSourceScope>(Sources);
}

bool FP4KadanImportRun::CheckRequestedSources()
{
    if (Plan.bMaterialExecutionDeferred)
    {
        TSet<FName> Identities;
        FString Error;
        if (Settings.GamePaks.IsEmpty() || Plan.RequestedPackages.Num() != Plan.Roots ||
            Plan.RequestedSourceDigests.Num() != Plan.Roots || (Plan.Roots == 0 && Plan.Assets.Num() != 0))
            Problems.Add(MakeShared<FJsonValueString>(
                TEXT("Material execution plan lacks its complete requested-root identity contract.")));
        for (const FString& Package : Plan.RequestedPackages)
        {
            const FName Key(*Package);
            const FString* Expected = Plan.RequestedSourceDigests.Find(Key);
            TSharedPtr<FP4KadanSourceAsset> Found;
            for (const auto& Row : Sources)
                if (Row.IsValid() && FName(*Row->Package) == Key)
                {
                    if (Found.IsValid())
                    {
                        Found.Reset();
                        break;
                    }
                    Found = Row;
                }
            FString Actual;
            if (Identities.Contains(Key) || !Expected || !Found.IsValid() ||
                !Found->Package.Equals(Package, ESearchCase::CaseSensitive) ||
                !P4KadanRequestedSourceDigest(*Found, Actual, Error) || !P4ExactNativeString(Actual, *Expected))
                Problems.Add(MakeShared<FJsonValueString>(
                    Package + TEXT(" requested-root source identity differs before import: ") + Error));
            Identities.Add(Key);
        }
        if (!ExecutionEdges.Current(Error))
            Problems.Add(MakeShared<FJsonValueString>(TEXT("Material execution plan source changed: ") + Error));
        if (Problems.Num())
            return false;
    }
    return true;
}

void FP4KadanImportRun::StartProgress()
{
    Progress = MakeUnique<FScopedSlowTask>(
        AssetsToImport.Num(), FText::FromString(TEXT("Importing selected game content")));
    if (!IsRunningCommandlet())
        Progress->MakeDialog(true);
    MaterialShouldCancel = [this]()
    {
        return MaterialCancelPoll.ShouldCancel(
            Cancelled, []() { return FPlatformTime::Seconds(); }, [this]() { return Progress->ShouldCancel(); });
    };
}

void FP4KadanImportRun::ImportAssets()
{
    for (const auto& Asset : AssetsToImport)
    {
        Progress->EnterProgressFrame(1.f, FText::FromString(Asset->Package));
        if (Progress->ShouldCancel())
        {
            Cancelled = true;
            break;
        }
        if (BlueprintKind(Asset->Kind) && BlueprintsRun)
            continue;
        if (DeclarationDeferred.Contains(FName(*Asset->Package)))
            continue;
        ReleaseFinishedAssets();
        WaitForMemory();
        // Saved assets stay in the project, so the next run picks up from here once there is room.
        const FString DiskFull = P4DiskFullMessage(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
        if (!DiskFull.IsEmpty())
        {
            Problems.Add(MakeShared<FJsonValueString>(DiskFull));
            FatalFailure = true;
            Checkpoint(true);
            break;
        }
        // A warm resume may have no new Blueprint batch at all. Deferred
        // native tables still have to finish before a world consumes them.
        if (Asset->Kind == TEXT("World") && HasDeferredDependencies())
        {
            SnapshotExistingClasses();
            FString Error;
            if (!ImportDeclaredDependencies(TMap<FName, UClass*>(), Error))
            {
                Problems.Add(MakeShared<FJsonValueString>(Error));
                FatalFailure = true;
                Checkpoint(true);
                break;
            }
        }
        if (!BlueprintKind(Asset->Kind))
        {
            if (!ImportOne(Asset))
                break;
            continue;
        }
        if (Resume && ExistingOrFromLibrary(Asset->Package))
        {
            auto Reply = MakeShared<FJsonObject>();
            Reply->SetBoolField(TEXT("valid"), true);
            Reply->SetStringField(TEXT("state"), TEXT("existing asset preserved"));
            Replies->SetObjectField(Asset->Package, Reply);
            // A Blueprint kept from an earlier map or the library still needs the kept game assets it names
            // (an effect its component holds) in this project, or its values lose them.
            TArray<FString> Kept;
            for (const FString& Dependency : Asset->Dependencies)
                if (!SoftDependency(*Asset, Dependency) && KeptSources.Contains(FName(*Dependency)) &&
                    !ExistingReal(Dependency))
                    Kept.Add(Dependency);
            if (Kept.Num() && !KeepReferences(MoveTemp(Kept)))
                break;
            continue;
        }
        if (BlueprintKind(Asset->Kind))
        {
            if (!ImportBlueprintBatch())
                break;
            continue;
        }
    }
}

// An editor keeps every loaded asset while it has RF_Standalone, and nothing collects garbage inside an import, so a
// large map held everything it imported until the end (24 GB for Area006_P). Between assets, each time the editor has
// grown by some gigabytes, the saved packages this run loaded are let go. An asset still needed stays through what
// refers to it, and one needed later loads again from its saved file. Classes and levels stay: other objects are built
// on them. Changed packages and packages loaded before the run are left as they are.
void FP4KadanImportRun::ReleaseFinishedAssets()
{
    constexpr double GigaBytes = 1024.0 * 1024.0 * 1024.0;
    // Sooner when the computer is running out.
    const double GrowthBeforeRelease = (double(P4AvailableCommitBytes()) < 8.0 * GigaBytes ? 0.5 : 2.0) * GigaBytes;
    const double Used = double(FPlatformMemory::GetStats().UsedPhysical);
    if (Used - MemoryAtLastRelease < GrowthBeforeRelease || IsGarbageCollecting() || GIsSavingPackage || IsAsyncLoading())
        return;
    const double Started = FPlatformTime::Seconds();
    TArray<TWeakObjectPtr<UObject>> Cleared;
    for (TObjectIterator<UPackage> It; It; ++It)
    {
        UPackage* Package = *It;
        if (PackagesBeforeRun.Contains(FObjectKey(Package)) || Package == GetTransientPackage() || Package->IsDirty() ||
            Package->IsRooted())
            continue;
        bool Stays = false;
        TArray<UObject*> Standalone;
        ForEachObjectWithPackage(Package,
            [&](UObject* Object)
            {
                if (Object->IsA<UClass>() || Object->IsA<UWorld>() || Object->IsA<UBlueprintCore>())
                {
                    Stays = true;
                    return false;
                }
                if (Object->HasAnyFlags(RF_Standalone))
                    Standalone.Add(Object);
                return true;
            });
        if (Stays)
            continue;
        for (UObject* Object : Standalone)
        {
            Object->ClearFlags(RF_Standalone);
            Cleared.Add(Object);
        }
    }
    if (Cleared.Num())
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    for (const TWeakObjectPtr<UObject>& Kept : Cleared)
        if (UObject* Object = Kept.Get())
            Object->SetFlags(RF_Standalone);
    MemoryAtLastRelease = double(FPlatformMemory::GetStats().UsedPhysical);
    Spent(TEXT("letting go of finished assets"), Started);
}

// The levels the run imported are let go when it ends, as the editor lets go of a level it closes. A level the
// import made is its own world in memory, already shown: its map would find it loaded and never add it to the map's
// world when it opens, and the map would then outlive its world. A level something else still holds stays loaded,
// so it is also put back to loaded and not shown, as a level read from disk is. Levels of the map open in the editor
// and changed levels are left as they are.
void FP4KadanImportRun::ReleaseSavedLevels()
{
    if (!GEditor || IsGarbageCollecting() || GIsSavingPackage || IsAsyncLoading())
        return;
    UWorld* Open = GEditor->GetEditorWorldContext().World();
    TArray<UPackage*> Packages;
    for (const auto& Pair : Out.Imported)
    {
        UPackage* Package = Pair.Value == TEXT("World") ? FindPackage(nullptr, *Pair.Key) : nullptr;
        UWorld* World = Package ? UWorld::FindWorldInPackage(Package) : nullptr;
        if (!World || Package->IsDirty() || World == Open ||
            (Open && Open->GetLevels().Contains(World->PersistentLevel)))
            continue;
        if (World->bIsWorldInitialized)
            World->CleanupWorld();
        if (World->PersistentLevel)
            World->PersistentLevel->bIsVisible = false;
        Packages.Add(Package);
    }
    TArray<TWeakObjectPtr<UPackage>> Released(Packages);
    FText Error;
    if (Packages.Num() && !UPackageTools::UnloadPackages(Packages, Error) && !Error.IsEmpty())
        Warnings.AddUnique(TEXT("The imported levels stay loaded until the editor closes: ") + Error.ToString());
    // Only saved levels were released, so a level that stays loaded has nothing unsaved: what cleaning up its world
    // marked is not an edit, and the map would otherwise ask to be saved and export as an empty edit.
    for (const TWeakObjectPtr<UPackage>& Package : Released)
        if (Package.IsValid())
            Package->SetDirtyFlag(false);
}

FP4KadanImportResult P4KadanRunImport(
    const FP4KadanImportSettings& Settings, const FP4KadanImportPlan& Plan, bool Resume)
{
    FP4KadanImportRun Run(Settings, Plan, Resume);
    return Run.Execute();
}

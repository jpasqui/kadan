// What the plan's packages need that the plan did not have: packages brought in from the game's
// containers, the classes tables name by type, and the packages kept as the game's.

#include "P4KadanImportInternal.h"
#include "P4GamePaths.h"

using namespace P4KadanImport;

// A game asset of a kind a map import leaves the game's, which a Blueprint names. The game's own
// asset is brought in from its file: whole when this editor holds all of it, otherwise kept for
// reference only, read-only, with what it does not hold named, and it cannot go into a mod. Nothing
// is made up in its place: when even that cannot be done the project holds nothing for it, and the
// Blueprint keeps the game's value with a note.
void FP4KadanImportRun::KeepForReference(const TSharedPtr<FP4KadanSourceAsset>& Asset)
{
    const FName Identity(*Asset->Package);
    KeptHandled.Add(Identity);
    if (ImportedIdentities.Contains(Identity) ||
        (ExistingReal(Asset->Package) && (Resume || !RetainedNativeKind(FName(*Asset->Kind)))))
        return;
    // Reimport updates kept-kind assets too. Use the same native retirement,
    // creator-edit protection and cold readback as an explicit selected asset.
    TUniquePtr<FP4KadanReimportAsset> Reimport;
    if (!Resume)
    {
        Reimport = MakeUnique<FP4KadanReimportAsset>(Asset->Package);
        if (!Reimport->IsReady())
        {
            Failed(*Asset, Reimport->GetError());
            Checkpoint();
            return;
        }
        if (Reimport->Preserved())
        {
            auto Reply = NativeImportReport(true, FString(), Asset->Package, FString(), false, false, false,
                TEXT("existing creator content preserved"));
            Reply->SetStringField(TEXT("kind"), Asset->Kind);
            const FString ObjectPath = P4ObjectPathOf(Asset->Package);
            const auto KeptStatus = P4QueryOriginalAsset(FindObject<UObject>(nullptr, *ObjectPath));
            Reply->SetBoolField(TEXT("reference_only"), KeptStatus.IsOriginal() && KeptStatus.Record.ReferenceOnly);
            if (KeptStatus.IsOriginal() && KeptStatus.Record.ReferenceOnly)
                Out.ReferenceOnly.AddUnique(Asset->Package);
            Replies->SetObjectField(Asset->Package, Reply);
            Warnings.AddUnique(
                Asset->Package + TEXT(" keeps its existing content. Its game references were read again."));
            Checkpoint();
            return;
        }
    }
    auto Imported = P4ImportOriginalAssetNative(Asset->Header, Asset->Body, true, false);
    FString First;
    if (!Imported.IsValid())
    {
        First = Imported.ErrorDetail;
        Imported = P4ImportOriginalAssetNative(Asset->Header, Asset->Body, true, true);
    }
    if (!Imported.IsValid())
        Imported = P4KeepUnsupportedParticleReference(Imported, true);
    bool Sound = false;
    if (!Imported.IsValid())
    {
        auto SoundReference = P4KeepUnsupportedSoundReference(Imported, true);
        if (SoundReference.IsValid())
        {
            Imported = MoveTemp(SoundReference);
            Sound = true;
        }
    }
    bool Valid = Imported.IsValid();
    FString FinalError = Imported.ErrorDetail;
    if (Valid && Reimport && !Reimport->Finish(FinalError))
        Valid = false;
    auto Reply = NativeImportReport(Valid, FinalError, Asset->Package, Imported.AssetPath, Imported.Created,
        Imported.Saved, Imported.FilledStandIn, Imported.State);
    Reply->SetStringField(TEXT("kind"), Asset->Kind);
    Reply->SetStringField(TEXT("header"), Asset->Header);
    Reply->SetBoolField(TEXT("reference_only"), Valid && (Imported.ReferenceOnly || Sound));
    Reply->SetBoolField(TEXT("kept_as_the_games"), true);
    Reply->SetNumberField(TEXT("game_fields_kept"), Imported.GameFieldsKept);
    if (Imported.GameFieldsRefused.Num())
    {
        TArray<TSharedPtr<FJsonValue>> Refused;
        for (const FString& Refusal : Imported.GameFieldsRefused)
            Refused.Add(MakeShared<FJsonValueString>(Refusal));
        Reply->SetArrayField(TEXT("game_fields_refused"), Refused);
    }
    if (!First.IsEmpty())
        Reply->SetStringField(TEXT("first_refusal"), First);
    TArray<TSharedPtr<FJsonValue>> NotHeld;
    for (const FString& Value : Imported.ValuesNotHeld)
        NotHeld.Add(MakeShared<FJsonValueString>(Value));
    Reply->SetArrayField(TEXT("values_not_held"), NotHeld);
    Replies->SetObjectField(Asset->Package, Reply);
    const FString Named = Asset->Package + TEXT(" (") + Asset->Kind + TEXT(")");
    if (Valid)
    {
        Out.Imported.Add(Asset->Package, Asset->Kind);
        ImportedIdentities.Add(Identity);
        if (Imported.ReferenceOnly || Sound)
        {
            Out.ReferenceOnly.AddUnique(Asset->Package);
            Warnings.Add(Named +
                TEXT(
                    " is the game's. A Blueprint names it, so the game's own asset was kept for reference only: it is read-only and cannot go into a mod (") +
                (Sound ? FString(TEXT("its sound event payload was not recovered")) : First.Left(300)) + TEXT(")."));
        }
    }
    else
        Failed(*Asset, FinalError, !AssetCanRemainInGame(Imported.Error),
            Imported.Error == EP4OriginalAssetImportError::DecodeFailed ||
                Imported.Error == EP4OriginalAssetImportError::SourceUnreadable ||
                Imported.Error == EP4OriginalAssetImportError::PropertiesUnreadable ||
                Imported.Error == EP4OriginalAssetImportError::TablesUnreadable);
    Checkpoint();
}

TArray<TSharedPtr<FP4KadanSourceAsset>> FP4KadanImportRun::BringIn(const TArray<FString>& Wanted)
{
    TMap<FName, TSharedPtr<FP4KadanSourceAsset>> Known;
    for (const auto& One : Sources)
        Known.Add(FName(*One->Package), One);
    TArray<FString> Pending;
    const bool Containers = !Settings.GamePaks.IsEmpty() && !Settings.Retoc.IsEmpty() &&
        IFileManager::Get().DirectoryExists(*Settings.GamePaks);
    TSet<FName> Tried, Visited;
    auto Discover = [&](TArray<FString> Walk)
    {
        while (Walk.Num())
        {
            const FString Package = Walk.Pop(false);
            const FName Identity(*Package);
            if (Visited.Contains(Identity))
                continue;
            Visited.Add(Identity);
            const auto* One = Known.Find(Identity);
            if (!One)
            {
                // A collection a material reads is read from the game even when the project has it:
                // the material's graph is recovered from its source, and the project's copy is kept.
                // So is a Blueprint class an earlier import brought in.
                if (!Tried.Contains(Identity) &&
                    (!ExistingOrFromLibrary(Package) ||
                        (!Resume && ExistingRetainedNativeKind(Package)) ||
                        ExecutionEdges.SourceNeeded(Package) || ExistingLevelPackage(Package) ||
                        ExistingBlueprintClass(Package, TableDeclarationClosure.Contains(Identity))) &&
                    P4IsValidContentPackageIgnoringCase(Package))
                {
                    Pending.AddUnique(Package);
                }
                continue;
            }
            if (Plan.bMaterialExecutionDeferred)
            {
                FString Error;
                if (!ExecutionEdges.Read(**One, Error))
                {
                    if (BestEffort)
                    {
                        UnreadableSources.Add(
                            Identity, TEXT("Its material execution source could not be read: ") + Error);
                        continue;
                    }
                    Problems.Add(MakeShared<FJsonValueString>(
                        (*One)->Package + TEXT(" invalid material execution source: ") + Error));
                    FatalFailure = true;
                    return;
                }
            }
            for (const FString& Dependency : ExecutionDependencies(**One))
                if (FollowsSoftReferences((*One)->Kind) || !SoftDependency(*(*One), Dependency))
                {
                    if (TableDeclarationClosure.Contains(Identity) && !SoftDependency(*(*One), Dependency))
                        TableDeclarationClosure.Add(FName(*Dependency));
                    Walk.Add(Dependency);
                }
        }
    };
    Discover(Wanted);
    ScanSeededAssets();
    if (FatalFailure || Problems.Num())
        return TArray<TSharedPtr<FP4KadanSourceAsset>>();
    bool Added = false;
    while (Pending.Num() && Containers)
    {
#if P4_KADAN_TEST_SEAMS
        FString Veto;
        if (!P4KadanSourceAcquisitionAllowed(Pending, Veto))
        {
            Problems.Add(MakeShared<FJsonValueString>(Veto));
            return TArray<TSharedPtr<FP4KadanSourceAsset>>();
        }
#endif
        if (!ContainerIndexAttempted)
        {
            const double ListStarted = FPlatformTime::Seconds();
            ContainerIndexAttempted = true;
            ContainerIndexReady = P4KadanReadContainerIndex(Settings, ContainerIndex, ContainerIndexError);
            Spent(TEXT("container index"), ListStarted);
        }
        if (!ContainerIndexReady)
        {
            Warnings.AddUnique(
                TEXT("The game's container index could not be read (") + ContainerIndexError + TEXT(")."));
            break;
        }
        if (!OriginalsReady)
        {
            IFileManager::Get().DeleteDirectory(*Originals, false, true);
            IFileManager::Get().MakeDirectory(*Originals, true);
            OriginalsReady = true;
        }
        ConvertFromContainers(Pending, Tried);
        Pending.Reset();
        TArray<TSharedPtr<FP4KadanSourceAsset>> Found;
        TArray<FString> ScanProblems;
        const double ScanStarted = FPlatformTime::Seconds();
        const bool Scanned = P4KadanScanGame(Originals, Found, ScanProblems);
        Spent(TEXT("converted package scan"), ScanStarted);
        if (!Scanned)
            break;
        TArray<FString> NewPackages;
        for (const auto& One : Found)
        {
            if (Known.Contains(FName(*One->Package)))
                continue;
            // A map import follows what placed things need. Sound, interface, effect, font, media and
            // table packages a Blueprint names stay the game's: they are not brought into the project
            // and the game keeps its own. Each is named in the report.
            if (P4KadanKeptAsTheGames(One->Kind) &&
                !(One->Kind == TEXT("WidgetBlueprint") && TableDeclarationClosure.Contains(FName(*One->Package))))
            {
                KeptAsTheGames.Add(FName(*One->Package));
                if (!KeptSources.Contains(FName(*One->Package)))
                    KeptSources.Add(FName(*One->Package), One);
                continue;
            }
            FString RetainedSourceError;
            if (!RetainAcquiredWorldSource(Settings, ContainerIndex, *One, RetainedSourceError))
            {
                Problems.Add(MakeShared<FJsonValueString>(One->Package +
                    TEXT(" could not retain its exact current converted source: ") + RetainedSourceError));
                FatalFailure = true;
                return TArray<TSharedPtr<FP4KadanSourceAsset>>();
            }
            if (One->Kind == TEXT("WidgetBlueprint") && TableDeclarationClosure.Contains(FName(*One->Package)))
            {
                KeptSources.Remove(FName(*One->Package));
                KeptAsTheGames.Remove(FName(*One->Package));
            }
            Known.Add(FName(*One->Package), One);
            Extracted.Add(One);
            Sources.Add(One);
            SourceScope->AddSources({One});
            Added = true;
            // Revisit only newly found packages: the missing identity was visited before
            // extraction, but its actual source now supplies its dependency edges.
            Visited.Remove(FName(*One->Package));
            NewPackages.Add(One->Package);
        }
        Discover(NewPackages);
        if (FatalFailure || Problems.Num())
            return TArray<TSharedPtr<FP4KadanSourceAsset>>();
    }
    if (Added)
    {
        const double RebuildStarted = FPlatformTime::Seconds();
        ExtractedRecovery = MakeUnique<FP4MaterialRecovery>(Settings.GameFolder, Sources, RecoveryArchive);
        Spent(TEXT("material recovery setup"), RebuildStarted);
    }
    TSet<FString> Roots;
    for (const FString& Package : Wanted)
        if (const auto* KnownSource = Known.Find(FName(*Package)))
            Roots.Add((*KnownSource)->Package);
    TArray<TSharedPtr<FP4KadanSourceAsset>> Ordered;
    if (!Roots.Num())
        return Ordered;
    const double PlanStarted = FPlatformTime::Seconds();
    const FP4KadanImportPlan More = P4KadanPlanImport(Sources, Roots, Resume, Plan.bMaterialExecutionDeferred);
    CyclicAnimations.Append(More.CyclicAnimations);
    Spent(TEXT("plan"), PlanStarted);
    for (const FString& Problem : More.Problems)
        Problems.Add(MakeShared<FJsonValueString>(Problem));
    for (const FString& Warning : More.Warnings)
        Warnings.AddUnique(Warning);
    for (const auto& Pair : More.MissingWarnings)
        MissingWarnings.Add(Pair.Key, Pair.Value);
    if (More.Problems.Num())
        return Ordered;
    for (const auto& Pair : More.DeclarationReferences)
        DeclarationReferences.Add(Pair.Key, Pair.Value);
    DeclarationDeferred.Append(More.DeclarationDeferred);
    for (const auto& One : More.Assets)
        if (!Replies->HasField(One->Package) && !ImportedIdentities.Contains(FName(*One->Package)))
            Ordered.Add(One);
    return Ordered;
}

bool FP4KadanImportRun::ExistingOrFromLibrary(const FString& Package)
{
    if (ExistingReal(Package))
        return true;
    if (!Resume || !Library.Seed(Package))
        return false;
    // Known to the asset registry at once, as the project's own assets are: a level made later in this run finds
    // a Blueprint's templates through it.
    ScanSeededAssets();
    return true;
}

// The packages taken from the library are on disk; the asset registry learns of them in one scan.
void FP4KadanImportRun::ScanSeededAssets()
{
    const TArray<FString> Files = Library.TakeSeededFiles();
    if (!Files.Num())
        return;
    const double Started = FPlatformTime::Seconds();
    FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().ScanFilesSynchronous(Files);
    Steps.FindOrAdd(TEXT("packages taken from the library")) = Library.SeededPackages();
    Spent(TEXT("library"), Started);
}

void FP4KadanImportRun::ConvertFromContainers(const TArray<FString>& Pending, TSet<FName>& Tried)
{
    // A retoc run is mostly its start (reading the container index), so the packages not converted yet go through
    // retoc in one batch per lane, several lanes at once. Placing each package then finds it converted; one a batch
    // did not convert is converted on its own there.
    WaitForMemory();
    const double ConvertStarted = FPlatformTime::Seconds();
    for (const FString& Package : Pending)
        Tried.Add(FName(*Package));
    const FString ConversionCache = P4KadanConversionCache(Settings);
    const int32 Lanes = FMath::Clamp(FPlatformMisc::NumberOfCoresIncludingHyperthreads() / 2, 1, 8);
    int32 Kept = 0;
    if (!ConversionCache.IsEmpty())
    {
        TArray<FString> Missing;
        for (const FString& Package : Pending)
        {
            FString Path, Ignored;
            if (!ContainerIndex.Resolve(Package, Path, Ignored))
                continue;
            if (P4KadanExtractedPackageReady(ConversionCache / Path, Package, Ignored))
                ++Kept;
            else
                Missing.Add(Path);
        }
        const int32 Batches = FMath::Min(Lanes, Missing.Num());
        ParallelFor(Batches,
            [&](int32 Batch)
            {
                TArray<FString> Mine;
                for (int32 Index = Batch; Index < Missing.Num(); Index += Batches)
                    Mine.Add(Missing[Index]);
                FString Ignored;
                P4KadanConvertMountedOriginals(Settings, Mine, ConversionCache, Ignored);
            });
    }
    TArray<FString> Errors;
    Errors.SetNum(Pending.Num());
    TAtomic<bool> OutOfMemory(false);
    ParallelFor(Lanes,
        [&](int32 Lane)
        {
            for (int32 Index = Lane; Index < Pending.Num() && !OutOfMemory; Index += Lanes)
            {
                if (!ConvertOriginalInCache(Settings, ContainerIndex, Pending[Index], Originals, ConversionCache,
                        Errors[Index], nullptr) &&
                    Errors[Index].IsEmpty())
                    Errors[Index] = TEXT("conversion failed");
                if (P4ToolRanOutOfMemory(Errors[Index]))
                    OutOfMemory = true;
            }
        });
    for (int32 Index = 0; Index < Pending.Num(); ++Index)
        if (!Errors[Index].IsEmpty())
        {
            // The computer, not the asset: going on would blame every asset that needs this one.
            if (P4ToolRanOutOfMemory(Errors[Index]))
            {
                Problems.Add(MakeShared<FJsonValueString>(Pending[Index] + TEXT(" could not be read: ") + Errors[Index]));
                FatalFailure = true;
                break;
            }
            Warnings.AddUnique(
                Pending[Index] + TEXT(" could not be read from the game's containers (") + Errors[Index] + TEXT(")."));
        }
    Spent(TEXT("container conversion"), ConvertStarted);
    Steps.FindOrAdd(TEXT("packages placed from the containers")) += Pending.Num();
    Steps.FindOrAdd(TEXT("of them already converted")) += Kept;
}

// Hydrate the actual selected hard-reference closure before Blueprint planning declares
// classes. Parent classes are required for declaration and do not appear in the planner's
// later ReferenceOnlyPaths, so waiting for that list can leave a root without its parent.
bool FP4KadanImportRun::BringInSelected()
{
    TArray<FString> SelectedPackages;
    if (Plan.bMaterialExecutionDeferred)
        SelectedPackages = Plan.RequestedPackages;
    else
        for (const auto& Asset : Plan.Assets)
            SelectedPackages.Add(Asset->Package);
    AssetsToImport = BringIn(SelectedPackages);
    if (Problems.Num())
        return false;
    return true;
}

// The packages of kinds a map import keeps as the game's (effect requests, effects, sounds, tables)
// name others by hard reference: an effect request its effects, a table its sound events and effects.
// Those are read from the game's containers now, in a few batches, so that each one is kept before
// what names it, and the reference names an asset in the project instead of being a value not held.
bool FP4KadanImportRun::BringInKeptDependencies()
{
    TSet<FName> Tried;
    for (int32 Round = 0; Round < 8 && !FatalFailure; ++Round)
    {
        TSet<FName> Known;
        for (const auto& One : Sources)
            if (One.IsValid())
                Known.Add(FName(*One->Package));
        TArray<FString> Walk;
        for (const auto& Pair : KeptSources)
            for (const FString& Dependency : Pair.Value->Dependencies)
            {
                const FName Identity(*Dependency);
                if (SoftDependency(*Pair.Value, Dependency) || KeptSources.Contains(Identity) ||
                    Known.Contains(Identity) || Tried.Contains(Identity) ||
                    (ExistingReal(Dependency) && (Resume || !ExistingRetainedNativeKind(Dependency))))
                    continue;
                Tried.Add(Identity);
                Walk.Add(Dependency);
            }
        if (!Walk.Num())
            break;
        BringIn(Walk);
        if (Problems.Num())
            return false;
    }
    return true;
}

// A retained table can be discovered after AssetsToImport was made. Its
// actually used class-typed fields must join the selected Blueprint batch,
// rather than being acquired later by KeepNamed and silently left unplanned.
bool FP4KadanImportRun::DiscoverTableClassDeclarations()
{
    TSet<FName> InspectedDeclarationTables;
    TSet<FString> TableClassRoots;
    TArray<FString> ImportRoots;
    for (const auto& Asset : AssetsToImport)
        ImportRoots.AddUnique(Asset->Package);
    bool TableClosureComplete = false;
    for (int32 Round = 0; Round < 32 && !FatalFailure; ++Round)
    {
        TMap<FName, TSharedPtr<FP4KadanSourceAsset>> Tables;
        for (const auto& Asset : AssetsToImport)
            if (Asset->Kind == TEXT("DataTable"))
                Tables.Add(FName(*Asset->Package), Asset);
        for (const auto& Pair : KeptSources)
            if (Pair.Value->Kind == TEXT("DataTable"))
                Tables.Add(Pair.Key, Pair.Value);
        bool NewClassRoot = false;
        for (const auto& Pair : Tables)
        {
            if (InspectedDeclarationTables.Contains(Pair.Key) || (Resume && ExistingReal(Pair.Value->Package)))
                continue;
            InspectedDeclarationTables.Add(Pair.Key);
            TArray<FP4OriginalTableClassReference> Fields;
            FString Error, Before, After;
            if (!P4KadanRequestedSourceDigest(*Pair.Value, Before, Error) ||
                !P4OriginalTableUsedClassReferences(Pair.Value->Header, Pair.Value->Body, Fields, Error) ||
                !P4KadanRequestedSourceDigest(*Pair.Value, After, Error) || !P4ExactNativeString(Before, After))
            {
                Problems.Add(MakeShared<FJsonValueString>(
                    Pair.Value->Package + TEXT(" cannot discover class declarations: ") + Error));
                return false;
            }
            if (!Fields.Num())
                continue;
            DeclarationReferences.Add(Pair.Key, Fields);
            DeclarationDeferred.Add(Pair.Key);
            DiscoveredTableSourceDigests.Add(Pair.Key, Before);
            SourceScope->AddSources({Pair.Value});
            for (const auto& Field : Fields)
            {
                const FString Package = FPackageName::ObjectPathToPackageName(Field.ObjectPath);
                if (!TableClassRoots.Contains(Package))
                {
                    TableClassRoots.Add(Package);
                    ImportRoots.AddUnique(Package);
                    NewClassRoot = true;
                }
                TableDeclarationClosure.Add(FName(*Package));
            }
        }
        if (!NewClassRoot)
        {
            TableClosureComplete = true;
            break;
        }
        // Follow only the hard closure rooted in the typed proof. A Widget is
        // promoted out of the old kept-UI policy only inside this closure.
        bool HardClosureComplete = false;
        TSet<FName> TriedHardDependencies;
        for (int32 Depth = 0; Depth < 32 && !FatalFailure; ++Depth)
        {
            TMap<FName, TSharedPtr<FP4KadanSourceAsset>> Known;
            for (const auto& Asset : Sources)
                Known.Add(FName(*Asset->Package), Asset);
            TMap<FName, TSharedPtr<FP4KadanSourceAsset>> All = Known;
            for (const auto& Pair : KeptSources)
                if (!All.Contains(Pair.Key))
                    All.Add(Pair.Key, Pair.Value);
            TArray<FString> Needed;
            bool AddedEdge = false;
            for (const auto& Pair : All)
            {
                if (!TableDeclarationClosure.Contains(Pair.Key))
                    continue;
                for (const FString& Dependency : Pair.Value->Dependencies)
                {
                    if (SoftDependency(*Pair.Value, Dependency))
                        continue;
                    const FName Identity(*Dependency);
                    if (!TableDeclarationClosure.Contains(Identity))
                    {
                        TableDeclarationClosure.Add(Identity);
                        AddedEdge = true;
                    }
                }
            }
            for (const FName& Identity : TableDeclarationClosure)
            {
                const auto* Source = All.Find(Identity);
                if (Source && (Known.Contains(Identity) || (*Source)->Kind != TEXT("WidgetBlueprint")))
                    continue;
                const FString Package = Source ? (*Source)->Package : Identity.ToString();
                if (!TriedHardDependencies.Contains(Identity) &&
                    (!ExistingReal(Package) || (!Resume && ExistingRetainedNativeKind(Package)) ||
                        ExistingBlueprintClass(Package, true)))
                {
                    TriedHardDependencies.Add(Identity);
                    Needed.Add(Package);
                }
            }
            if (!Needed.Num() && !AddedEdge)
            {
                HardClosureComplete = true;
                break;
            }
            if (Needed.Num())
                BringIn(Needed);
            if (Problems.Num() || FatalFailure)
                return false;
        }
        if (!HardClosureComplete)
        {
            Problems.Add(MakeShared<FJsonValueString>(
                TEXT("Table class declaration hard closure exceeded its bounded traversal.")));
            return false;
        }
        for (const FString& Package : TableClassRoots)
        {
            const auto* Source = Sources.FindByPredicate(
                [&](const TSharedPtr<FP4KadanSourceAsset>& One) { return FName(*One->Package) == FName(*Package); });
            if (!Source || !BlueprintKind((*Source)->Kind) || (*Source)->Kind == TEXT("LevelSequence"))
            {
                Problems.Add(MakeShared<FJsonValueString>(
                    Package + TEXT(" is a used table class, but its genuine Blueprint source is unavailable.")));
                return false;
            }
        }
        // Rebuild only the original roots plus proven class roots. Retained table
        // imports are not new roots, so unused table import-map entries add no UI.
        AssetsToImport = BringIn(ImportRoots);
        if (Problems.Num() || FatalFailure)
            return false;
    }
    if (!TableClosureComplete)
    {
        Problems.Add(
            MakeShared<FJsonValueString>(TEXT("Table class declaration discovery exceeded its bounded traversal.")));
        return false;
    }
    return true;
}

// Ordinary native objects needing a deferred table wait at the same boundary.
void FP4KadanImportRun::DeferTableDependents()
{
    for (bool Changed = true; Changed;)
    {
        Changed = false;
        TArray<TSharedPtr<FP4KadanSourceAsset>> Candidates = AssetsToImport;
        for (const auto& Pair : KeptSources)
            Candidates.Add(Pair.Value);
        for (const auto& Asset : Candidates)
        {
            const FName Identity(*Asset->Package);
            if (Rank(Asset->Kind) != 5 || DeclarationDeferred.Contains(Identity))
                continue;
            for (const FString& Dependency : Asset->Dependencies)
                if (!SoftDependency(*Asset, Dependency) && DeclarationDeferred.Contains(FName(*Dependency)))
                {
                    DeclarationDeferred.Add(Identity);
                    Changed = true;
                    break;
                }
        }
    }
}

// A kept package an asset names is kept for reference before the asset (as for a Blueprint), with
// what it needs: its own kept packages first, and the textures, materials and meshes it needs.
void FP4KadanImportRun::KeepNamed(const FString& Package, int32 Depth)
{
    const FName Identity(*Package);
    const auto* Found = KeptSources.Find(Identity);
    if (!Found || KeptHandled.Contains(Identity) || KeepVisiting.Contains(Identity) || Depth > 16 || FatalFailure)
        return;
    const TSharedPtr<FP4KadanSourceAsset> Kept = *Found;
    KeepVisiting.Add(Identity);
    TArray<FString> Needs;
    for (const FString& Dependency : Kept->Dependencies)
    {
        if (SoftDependency(*Kept, Dependency))
            continue;
        const FName Needed(*Dependency);
        if (KeptSources.Contains(Needed))
            KeepNamed(Dependency, Depth + 1);
        else if (!ImportedIdentities.Contains(Needed) && !NotImported.Contains(Needed) &&
            (!ExistingReal(Dependency) || (!Resume && ExistingRetainedNativeKind(Dependency))))
            Needs.Add(Dependency);
    }
    if (Needs.Num() && !FatalFailure)
        for (const auto& Need : BringIn(Needs))
            if (!BlueprintKind(Need->Kind) && !DeclarationDeferred.Contains(FName(*Need->Package)) &&
                !Replies->HasField(Need->Package) && !ImportOne(Need))
                break;
    KeepVisiting.Remove(Identity);
    if (!FatalFailure && !KeptHandled.Contains(Identity) && !DeclarationDeferred.Contains(Identity))
        KeepForReference(Kept);
}

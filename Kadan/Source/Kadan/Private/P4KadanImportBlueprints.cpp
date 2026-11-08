// The Blueprints of an import, made together in one batch, and the packages declared for them first:
// animation cycles and the tables that name a class by type.

#include "P4KadanImportInternal.h"
#include "P4GamePaths.h"

using namespace P4KadanImport;

FP4KadanImportResult P4KadanBlueprintBatchResult(const FP4BlueprintImportRunResult& Result,
    const TArray<TSharedPtr<FP4KadanSourceAsset>>& Selected, const TSet<FName>& VerifiedGameReferencePackages)
{
    FP4KadanImportResult Out;
    Out.Cancelled = Result.bCancelled;
    const FString Error = Result.Errors.Num() ? FString::Join(Result.Errors, TEXT("\n"))
                                              : TEXT("The native Blueprint batch did not complete: ") + Result.Phase;
    const bool Fatal = Result.bCancelled || Result.bInfrastructureFailure;
    if (!Result.bOk && (Fatal || !VerifiedGameReferencePackages.Num()))
        Out.Problems.Add(Error);
    for (const auto& Asset : Selected)
        if (Result.CompletedPackages.Contains(FName(*Asset->Package)) && ExistingReal(Asset->Package))
            Out.Imported.Add(Asset->Package, Asset->Kind);
        else if (!Fatal && VerifiedGameReferencePackages.Contains(FName(*Asset->Package)))
        {
            const FString* OwnError = Result.PackageErrors.Find(Asset->Package);
            const FString& Reason = OwnError ? *OwnError : Error;
            Out.ReferenceOnly.AddUnique(Asset->Package);
            Out.KeptAsTheGames.AddUnique(Asset->Package);
            Out.GameReferenceReasons.Add(Asset->Package, Reason);
            Out.Warnings.Add(Asset->Package +
                TEXT(
                    " could not be reconstructed. Its original game identity is retained; its Blueprint is not editable. ") +
                Reason);
        }
        else
        {
            Out.NotImported.Add(Asset->Package);
            Out.Problems.Add(Asset->Package + TEXT(" was not imported. ") + Error);
        }
    Out.Ok = !Fatal && !Out.Cancelled && !Out.Problems.Num() && (Result.bOk || VerifiedGameReferencePackages.Num() > 0);
    return Out;
}

// Declares the native animation cycle shells.
bool FP4KadanImportRun::DeclareAnimationCycles()
{
    for (const FName& Identity : CyclicAnimations)
    {
        const FString Package = Identity.ToString();
        // Existing creator content, imports and edited stand-ins retain the
        // normal importer admission checks. Never replace one for declaration.
        if (FPackageName::DoesPackageExist(Package))
            continue;
        const auto* Found = Sources.FindByPredicate([&](const TSharedPtr<FP4KadanSourceAsset>& Source)
            { return Source.IsValid() && FName(*Source->Package) == Identity; });
        if (!Found || ((*Found)->Kind != TEXT("AnimSequence") && (*Found)->Kind != TEXT("AnimMontage")))
        {
            Problems.Add(
                MakeShared<FJsonValueString>(TEXT("Animation cycle declaration lacks its native source: ") + Package));
            return false;
        }
        const FString Path = P4ObjectPathOf(Package);
        if (FindObject<UObject>(nullptr, *Path))
            continue;
        const FString Declared = UP4BlueprintImport::StandInAsset(Path, TEXT("/Script/Engine.") + (*Found)->Kind);
        UObject* Asset = FindObject<UObject>(nullptr, *Path);
        if (!Asset || Asset->GetClass()->GetName() != (*Found)->Kind)
        {
            Problems.Add(MakeShared<FJsonValueString>(
                TEXT("Animation cycle declaration refused: ") + Package + TEXT(" ") + Declared));
            return false;
        }
        const FString Saved = UP4BlueprintImport::SaveStandIn(Package);
        if (!P4IsVerifiedStandIn(Asset))
        {
            Problems.Add(MakeShared<FJsonValueString>(
                TEXT("Animation cycle declaration was not saved with an untouched baseline: ") + Package + TEXT(" ") +
                Saved));
            return false;
        }
    }
    return true;
}

void FP4KadanImportRun::SnapshotExistingClasses()
{
    ExistingClasses.Reset();
    TSet<FName> Checked;
    for (const auto& Pair : DeclarationReferences)
        for (const auto& Reference : Pair.Value)
        {
            const FName Identity(*Reference.ObjectPath);
            if (Checked.Contains(Identity))
                continue;
            Checked.Add(Identity);
            const FString Package = FPackageName::ObjectPathToPackageName(Reference.ObjectPath);
            if (!FPackageName::DoesPackageExist(Package) || !ExistingReal(Package))
                continue;
            UClass* Class = LoadObject<UClass>(nullptr, *Reference.ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
            if (IsValid(Class) && ExistingReal(Package))
                ExistingClasses.Add(Reference.ObjectPath, Class);
        }
}

bool FP4KadanImportRun::HasDeferredDependencies()
{
    for (const FName Identity : DeclarationDeferred)
        if (!ResolvedDeferredDependencies.Contains(Identity))
            return true;
    return false;
}

// Resolves deferred declared-class dependencies.
bool FP4KadanImportRun::ImportDeclaredDependencies(const TMap<FName, UClass*>& Declared, FString& Error)
{
    for (const auto& Proof : DiscoveredTableSourceDigests)
    {
        const auto* Source = Sources.FindByPredicate(
            [&](const TSharedPtr<FP4KadanSourceAsset>& One) { return FName(*One->Package) == Proof.Key; });
        const auto* Kept = KeptSources.Find(Proof.Key);
        const TSharedPtr<FP4KadanSourceAsset> Current = Source ? *Source : Kept ? *Kept : nullptr;
        FString Digest;
        if (!Current.IsValid() || !P4KadanRequestedSourceDigest(*Current, Digest, Error) ||
            !P4ExactNativeString(Digest, Proof.Value))
        {
            Error = Proof.Key.ToString() + TEXT(": typed table declaration source changed before native values: ") +
                Error;
            return false;
        }
    }
    // Dynamic BringIn uses this same deferred set. Import in the existing
    // dependency order once, before any Blueprint widget/default values.
    TMap<FName, TSharedPtr<FP4KadanSourceAsset>> ByPackage;
    for (const auto& Source : Sources)
        ByPackage.Add(FName(*Source->Package), Source);
    for (const auto& Pair : KeptSources)
        if (!ByPackage.Contains(Pair.Key))
            ByPackage.Add(Pair.Key, Pair.Value);
    // With game-backed content allowed, one table that cannot be made is left to the game, as
    // any other asset is: it does not stop every Blueprint of the import.
    TSet<FName> LeftToGame;
    const auto LeaveToGame = [&](FName Identity, const FString& Why)
    {
        LeftToGame.Add(Identity);
        if (const auto* Found = ByPackage.Find(Identity))
        {
            if (!NotImported.Contains(Identity))
                Failed(**Found, Why, true, true);
        }
        else
            Warnings.AddUnique(Why);
    };
    for (const auto& Pair : DeclarationReferences)
        for (const auto& Reference : Pair.Value)
        {
            const FName PackageIdentity(*FPackageName::ObjectPathToPackageName(Reference.ObjectPath));
            UClass* Class = Declared.FindRef(PackageIdentity);
            if (!Class)
                Class = ExistingClasses.FindRef(Reference.ObjectPath);
            UClass* MetaClass = FindObject<UClass>(nullptr, *Reference.MetaClassPath);
            // A class that stays the game's (a widget whose sounds or fonts did not import) is
            // named by the row as the game names it, as when the class is not in the import.
            if (BestEffort && !IsValid(Class) && NotImported.Contains(PackageIdentity))
                continue;
            if (NotImported.Contains(PackageIdentity) || !IsValid(Class) || !IsValid(MetaClass) ||
                !Class->IsChildOf(MetaClass) ||
                !Class->GetPathName().Equals(Reference.ObjectPath, ESearchCase::IgnoreCase))
            {
                Error = Pair.Key.ToString() + TEXT(" ") + Reference.Field +
                    TEXT(": its required class was not successfully declared with the native type: ") +
                    Reference.ObjectPath;
                if (!BestEffort)
                    return false;
                LeaveToGame(Pair.Key, Error);
                Error.Reset();
                break;
            }
        }
    TSet<FName> Done, Active;
    TFunction<bool(FName)> ImportDeferred;
    ImportDeferred = [&](FName Identity)
    {
        if (Done.Contains(Identity))
            return true;
        if (LeftToGame.Contains(Identity))
        {
            Error = Identity.ToString() + TEXT(": the required declared-class dependency did not import completely.");
            return false;
        }
        if (Active.Contains(Identity))
        {
            Error = Identity.ToString() + TEXT(": deferred hard dependencies form a cycle.");
            return false;
        }
        Active.Add(Identity);
        const auto* Found = ByPackage.Find(Identity);
        if (!Found)
        {
            Error = Identity.ToString() + TEXT(": its deferred source is unavailable.");
            return false;
        }
        for (const FString& Dependency : (*Found)->Dependencies)
            if (!SoftDependency(*(*Found), Dependency) && DeclarationDeferred.Contains(FName(*Dependency)))
                if (!ImportDeferred(FName(*Dependency)))
                    return false;
        Progress->EnterProgressFrame(0.f, FText::FromString((*Found)->Package));
        if (Progress->ShouldCancel())
        {
            Cancelled = true;
            Error = TEXT("Declared class dependency import was cancelled.");
            return false;
        }
        const int32 BeforeProblems = Problems.Num();
        if (!ImportOne(*Found) || NotImported.Contains(Identity) || Problems.Num() != BeforeProblems ||
            ((*Found)->Kind == TEXT("DataTable") && Out.ReferenceOnly.Contains((*Found)->Package)))
        {
            Error = (*Found)->Package + TEXT(": the required declared-class dependency did not import completely.");
            return false;
        }
        Active.Remove(Identity);
        Done.Add(Identity);
        return true;
    };
    TArray<FName> Pending = DeclarationDeferred.Array();
    Pending.Sort([](FName A, FName B) { return A.ToString() < B.ToString(); });
    for (const FName Identity : Pending)
        if (!ImportDeferred(Identity))
        {
            if (!BestEffort || Cancelled)
                return false;
            // Everything on the failed chain is left to the game; the rest goes on.
            for (const FName Open : Active)
                LeaveToGame(Open, Error);
            Active.Reset();
            Error.Reset();
        }
    for (const FName Identity : Pending)
        ResolvedDeferredDependencies.Add(Identity);
    return true;
}

bool FP4KadanImportRun::ImportBlueprintBatch()
{
    // All Blueprints of the plan are made together, so that they can name each other. One that
    // needs a package that did not import is left out of the run.
    BlueprintsRun = true;
    TArray<TSharedPtr<FP4KadanSourceAsset>> Selected;
    TArray<FString> BlueprintPackages;
    for (const auto& Other : AssetsToImport)
    {
        if (!BlueprintKind(Other->Kind) || (Resume && ExistingOrFromLibrary(Other->Package)))
            continue;
        const FString Needed = NeededAndMissing(*Other);
        if (!Needed.IsEmpty())
        {
            auto Skipped = NativeImportReport(false, TEXT("Needs a package that did not import."), Other->Package,
                FString(), false, false, false, TEXT("skipped"));
            Skipped->SetStringField(TEXT("kind"), Other->Kind);
            Replies->SetObjectField(Other->Package, Skipped);
            Failed(*Other, TEXT("Not imported because ") + Needed + TEXT(" did not import."));
            continue;
        }
        Selected.Add(Other);
        BlueprintPackages.Add(Other->Package);
    }
    // The Blueprints are planned here, from the game folder's files and this editor's native modules.
    // An object a Blueprint names that the game folder lacks is imported first when the game ships
    // it, with what it needs, and a Blueprint brought in that way is planned with the others.
    FP4KadanBlueprintPlans BlueprintPlans;
    FP4OriginalZenSourceBatch BlueprintOriginals;
    bool PlanFailed = false;
    PlanBlueprints(Selected, BlueprintPackages, BlueprintPlans, BlueprintOriginals, PlanFailed);
    if (Stop || FatalFailure)
        return false;
    if (PlanFailed || !BlueprintPackages.Num())
    {
        Checkpoint();
        if (ReportWriteFailed || FatalFailure)
            return false;
        return true;
    }
    Out.OriginalIdentityPackages += BlueprintPlans.OriginalIdentityPackages;
    // The game assets of kinds a map import leaves the game's that the Blueprints name, with the
    // ones of them those name, are brought in from the game's files before the Blueprints get their
    // values, so that a value names the game's asset and nothing is made up in its place.
    if (!KeepBlueprintReferences(Selected, BlueprintPlans))
        return false;
    return RunBlueprintBatch(Selected, BlueprintPackages, BlueprintPlans);
}

void FP4KadanImportRun::PlanBlueprints(TArray<TSharedPtr<FP4KadanSourceAsset>>& Selected,
    TArray<FString>& BlueprintPackages, FP4KadanBlueprintPlans& BlueprintPlans,
    FP4OriginalZenSourceBatch& BlueprintOriginals, bool& PlanFailed)
{
    FString PlanError;
    TSet<FName> Followed;
    for (;;)
    {
        const double BlueprintPlanStarted = FPlatformTime::Seconds();
        const bool BlueprintsPlanned = !BlueprintPackages.Num() ||
            P4KadanPlanBlueprints(Sources, BlueprintPackages, Folder / TEXT("BlueprintPlans"), BlueprintPlans,
                PlanError, Settings, MaterialRegistry, &BlueprintOriginals);
        Spent(TEXT("blueprint plan"), BlueprintPlanStarted);
        if (!BlueprintsPlanned)
        {
            for (const auto& Made : Selected)
            {
                auto One = NativeImportReport(
                    false, PlanError, Made->Package, FString(), false, false, false, TEXT("failed"));
                One->SetStringField(TEXT("kind"), Made->Kind);
                Replies->SetObjectField(Made->Package, One);
                Failed(*Made, PlanError, true);
            }
            PlanFailed = true;
            break;
        }
        for (const FString& Module : BlueprintPlans.ModulesNotLoaded)
            Warnings.AddUnique(P4KadanUnloadedModuleNote(Module, BlueprintPlans.ModuleUsers.FindRef(Module)));
        for (const auto& Refusal : BlueprintPlans.Refused)
            if (!BlueprintPackages.Contains(Refusal.Key))
                Warnings.AddUnique(Refusal.Key + TEXT(" was not planned. ") + Refusal.Value);
        for (int32 Index = Selected.Num(); Index-- > 0;)
            if (const FString* Unplanned = BlueprintPlans.Refused.Find(Selected[Index]->Package))
            {
                auto One = NativeImportReport(
                    false, *Unplanned, Selected[Index]->Package, FString(), false, false, false, TEXT("failed"));
                One->SetStringField(TEXT("kind"), Selected[Index]->Kind);
                Replies->SetObjectField(Selected[Index]->Package, One);
                const bool SourceInputProblem = BlueprintPlans.SourceProblems.Contains(Selected[Index]->Package);
                Failed(*Selected[Index], *Unplanned, SourceInputProblem, SourceInputProblem);
                BlueprintPackages.Remove(Selected[Index]->Package);
                Selected.RemoveAt(Index);
            }
        // A Blueprint that needs one that was not planned is left out too.
        for (bool Changed = true; Changed;)
        {
            Changed = false;
            for (int32 Index = Selected.Num(); Index-- > 0;)
            {
                const FString Needed = NeededAndMissing(*Selected[Index]);
                if (Needed.IsEmpty())
                    continue;
                auto One = NativeImportReport(false, TEXT("Needs a package that did not import."),
                    Selected[Index]->Package, FString(), false, false, false, TEXT("skipped"));
                One->SetStringField(TEXT("kind"), Selected[Index]->Kind);
                Replies->SetObjectField(Selected[Index]->Package, One);
                Failed(*Selected[Index], TEXT("Not imported because ") + Needed + TEXT(" did not import."));
                BlueprintPackages.Remove(Selected[Index]->Package);
                Selected.RemoveAt(Index);
                Changed = true;
            }
        }
        if (!BlueprintPackages.Num())
            break;
        TArray<FString> Referenced;
        for (const FString& Reference : BlueprintPlans.ReferenceOnlyPaths)
        {
            const FString Package = FPackageName::ObjectPathToPackageName(Reference);
            if (Followed.Contains(FName(*Package)))
                continue;
            Followed.Add(FName(*Package));
            if ((!ExistingReal(Package) || (!Resume && ExistingRetainedNativeKind(Package))) &&
                !ImportedIdentities.Contains(FName(*Package)))
                Referenced.Add(Package);
        }
        if (!Referenced.Num())
            break;
        bool NewBlueprints = false;
        for (const auto& One : BringIn(Referenced))
        {
            if (BlueprintKind(One->Kind))
            {
                if (!BlueprintPackages.Contains(One->Package))
                {
                    Selected.Add(One);
                    BlueprintPackages.Add(One->Package);
                    NewBlueprints = true;
                }
            }
            else if (!DeclarationDeferred.Contains(FName(*One->Package)) && !ImportOne(One))
            {
                Stop = true;
                break;
            }
        }
        if (Stop || !NewBlueprints)
            break;
    }
}

bool FP4KadanImportRun::KeepBlueprintReferences(
    const TArray<TSharedPtr<FP4KadanSourceAsset>>& Selected, const FP4KadanBlueprintPlans& BlueprintPlans)
{
    TArray<FString> Walk;
    for (const auto& Made : Selected)
        for (const FString& Dependency : Made->Dependencies)
            if (!SoftDependency(*Made, Dependency))
                Walk.Add(Dependency);
    for (const FString& Reference : BlueprintPlans.ReferenceOnlyPaths)
        Walk.Add(FPackageName::ObjectPathToPackageName(Reference));
    return KeepReferences(MoveTemp(Walk));
}

bool FP4KadanImportRun::KeepReferences(TArray<FString> Walk)
{
    TSet<FName> Wanted;
    while (Walk.Num())
    {
        const FName Identity(*Walk.Pop(false));
        const auto* Found = KeptSources.Find(Identity);
        if (!Found || Wanted.Contains(Identity) || (*Found)->Kind == TEXT("WidgetBlueprint"))
            continue;
        Wanted.Add(Identity);
        for (const FString& Dependency : (*Found)->Dependencies)
            if (!SoftDependency(*(*Found), Dependency))
                Walk.Add(Dependency);
    }
    // A kept asset that another one names comes before it.
    TArray<TSharedPtr<FP4KadanSourceAsset>> Ordered;
    TSet<FName> Placed, Visiting;
    TFunction<void(const TSharedPtr<FP4KadanSourceAsset>&)> Place = [&](const TSharedPtr<FP4KadanSourceAsset>& One)
    {
        const FName Identity(*One->Package);
        if (Placed.Contains(Identity) || Visiting.Contains(Identity))
            return;
        Visiting.Add(Identity);
        for (const FString& Dependency : One->Dependencies)
            if (!SoftDependency(*One, Dependency) && Wanted.Contains(FName(*Dependency)))
                Place(KeptSources[FName(*Dependency)]);
        Visiting.Remove(Identity);
        Placed.Add(Identity);
        Ordered.Add(One);
    };
    TArray<FName> Names = Wanted.Array();
    Names.Sort([](FName A, FName B) { return A.ToString() < B.ToString(); });
    for (const FName Identity : Names)
        Place(KeptSources[Identity]);
    for (const auto& One : Ordered)
    {
        Progress->EnterProgressFrame(0.f, FText::FromString(One->Package));
        if (Progress->ShouldCancel())
        {
            Cancelled = true;
            break;
        }
        // What a kept asset needs that is not kept (the textures, materials and meshes of a
        // particle system) is brought in and imported before it: the asset is made from its
        // game file with them, and without them it does not say what the game's says.
        TArray<FString> Needs;
        for (const FString& Dependency : One->Dependencies)
        {
            const FName Needed(*Dependency);
            if (!SoftDependency(*One, Dependency) && !KeptSources.Contains(Needed) &&
                !ImportedIdentities.Contains(Needed) && !NotImported.Contains(Needed) &&
                (!ExistingReal(Dependency) || (!Resume && ExistingRetainedNativeKind(Dependency))))
                Needs.Add(Dependency);
        }
        if (Needs.Num())
        {
            const int32 ProblemsBefore = Problems.Num();
            for (const auto& Need : BringIn(Needs))
                if (!BlueprintKind(Need->Kind) && !DeclarationDeferred.Contains(FName(*Need->Package)) &&
                    !ImportOne(Need))
                {
                    Stop = true;
                    break;
                }
            if (Stop || Problems.Num() != ProblemsBefore || FatalFailure)
            {
                Stop = true;
                break;
            }
        }
        if (!DeclarationDeferred.Contains(FName(*One->Package)))
            KeepForReference(One);
        if (ReportWriteFailed || FatalFailure)
            break;
    }
    if (Stop || Cancelled || ReportWriteFailed || FatalFailure)
    {
        Checkpoint();
        return false;
    }
    return true;
}

bool FP4KadanImportRun::RunBlueprintBatch(const TArray<TSharedPtr<FP4KadanSourceAsset>>& Selected,
    const TArray<FString>& BlueprintPackages, const FP4KadanBlueprintPlans& BlueprintPlans)
{
    SnapshotExistingClasses();
    // Counted without the deferred dependencies it imports through ImportOne (those count by kind).
    const double BlueprintRunStarted = FPlatformTime::Seconds();
    double InsideBefore = 0;
    for (const auto& Pair : Seconds)
        if (Pair.Key.StartsWith(TEXT("import ")) || Pair.Key == TEXT("report"))
            InsideBefore += Pair.Value;
    const FP4BlueprintImportRunResult BlueprintResult = UP4BlueprintImport::RunSelectedNative(BlueprintPlans.Folder,
        Folder / TEXT("blueprint-import.json"), BlueprintPackages, !Resume,
        DeclarationDeferred.Num() ? TFunction<bool(const TMap<FName, UClass*>&, FString&)>(
                                        [this](const TMap<FName, UClass*>& Declared, FString& Error)
                                        { return ImportDeclaredDependencies(Declared, Error); })
                                  : TFunction<bool(const TMap<FName, UClass*>&, FString&)>(),
        true, BlueprintPlans.ValidateWidgetSource);
    double InsideAfter = 0;
    for (const auto& Pair : Seconds)
        if (Pair.Key.StartsWith(TEXT("import ")) || Pair.Key == TEXT("report"))
            InsideAfter += Pair.Value;
    Spent(TEXT("blueprint import"), BlueprintRunStarted + (InsideAfter - InsideBefore));
    // A game asset the project does not hold stays the game's: the Blueprint import made nothing in
    // its place, and the value that names it keeps the game's value with a note.
    auto KeptReference = [&](const FString& Reference, const FString& Where)
    {
        const FName Package(*FPackageName::ObjectPathToPackageName(Reference));
        const auto* Source = KeptSources.Find(Package);
        KeptHandled.Add(Package);
        Out.KeptAsTheGames.AddUnique(Reference);
        Warnings.AddUnique(Reference + (Source ? TEXT(" (") + (*Source)->Kind + TEXT(")") : FString()) +
            TEXT(" stays the game's, and the project holds nothing in its place. ") +
            (Where.IsEmpty() ? FString(TEXT("A Blueprint names it")) : Where + TEXT(" names it")) +
            TEXT(": the Blueprint keeps the game's value with a note, and the editor shows the reference empty."));
    };
    for (const auto& Pair : BlueprintResult.KeptAsTheGames)
        KeptReference(Pair.Key, Pair.Value);
    // An object the game does not ship stays an empty reference, as it is in the game.
    for (const FString& Reference : BlueprintPlans.ReferenceOnlyPaths)
        if (!FindObject<UObject>(nullptr, *Reference) &&
            !ExistingReal(FPackageName::ObjectPathToPackageName(Reference)))
        {
            if (KeptSources.Contains(FName(*FPackageName::ObjectPathToPackageName(Reference))))
            {
                KeptReference(Reference, FString());
                continue;
            }
            Out.EmptyReferences.AddUnique(Reference);
            Warnings.AddUnique(TEXT("A Blueprint names ") + Reference +
                TEXT(", which the game does not ship. The reference is empty, as it is in the game."));
        }
    auto Batch = MakeShared<FJsonObject>();
    Batch->SetBoolField(TEXT("ok"), BlueprintResult.bOk);
    Batch->SetBoolField(TEXT("cancelled"), BlueprintResult.bCancelled);
    Batch->SetStringField(TEXT("phase"), BlueprintResult.Phase);
    Batch->SetNumberField(TEXT("count"), BlueprintResult.Count);
    if (BlueprintResult.Errors.Num())
        Batch->SetStringField(TEXT("error"), FString::Join(BlueprintResult.Errors, TEXT("\n")));
    TArray<TSharedPtr<FJsonValue>> BlueprintProblems;
    for (const FString& Problem : BlueprintResult.Problems)
    {
        BlueprintProblems.Add(MakeShared<FJsonValueString>(Problem));
        Warnings.AddUnique(TEXT("Blueprint import note. ") + Problem);
    }
    Batch->SetArrayField(TEXT("problems"), BlueprintProblems);
    Result->SetObjectField(TEXT("blueprints"), Batch);
    Cancelled |= BlueprintResult.bCancelled;
    if (BlueprintResult.bCancelled)
    {
        Checkpoint();
        return false;
    }
    // A generated class can exist after declaration even when source values or
    // the dependency callback failed. Only completed, saved native rows count.
    TSet<FName> VerifiedBlueprintSources;
    for (const auto& Source : Selected)
        if (BlueprintPlans.Planned.Contains(Source->Package) &&
            !BlueprintPlans.SourceProblems.Contains(Source->Package))
            VerifiedBlueprintSources.Add(FName(*Source->Package));
    FP4KadanImportResult Admission = P4KadanBlueprintBatchResult(BlueprintResult, Selected, VerifiedBlueprintSources);
    if (BestEffort && !BlueprintResult.bCancelled && !BlueprintResult.bInfrastructureFailure)
    {
        // A class that did not import stays the game's; levels keep the actors that place it in the game.
        for (const FString& Problem : Admission.Problems)
            Warnings.AddUnique(Problem + TEXT(" Levels keep the actors that need it in the game."));
        Admission.Problems.Reset();
    }
    for (const FString& Problem : Admission.Problems)
        Problems.Add(MakeShared<FJsonValueString>(Problem));
    for (const FString& Warning : Admission.Warnings)
        Warnings.AddUnique(Warning);
    for (const auto& Reference : Admission.GameReferenceReasons)
    {
        GameBackedPackages.Add(FName(*Reference.Key));
        NotImported.Add(FName(*Reference.Key));
        Out.ReferenceOnly.AddUnique(Reference.Key);
        Out.GameReferenceReasons.Add(Reference.Key, Reference.Value);
    }
    for (const FString& Reference : Admission.KeptAsTheGames)
        Out.KeptAsTheGames.AddUnique(Reference);
    const FString BatchError = BlueprintResult.Errors.Num() ? FString::Join(BlueprintResult.Errors, TEXT("\n"))
                                                            : TEXT("The Blueprint import did not make this asset.");
    if (BlueprintResult.bInfrastructureFailure && !Admission.Problems.Num())
    {
        Problems.Add(MakeShared<FJsonValueString>(TEXT("The Blueprint batch infrastructure failed: ") + BatchError));
        FatalFailure = true;
    }
    for (const auto& Made : Selected)
    {
        const bool There = Admission.Imported.Contains(Made->Package);
        const FString* OwnError = BlueprintResult.PackageErrors.Find(Made->Package);
        auto One = NativeImportReport(There,
            There          ? FString()
                : OwnError ? *OwnError
                           : BatchError,
            Made->Package, FString(), There, There, false, There ? TEXT("imported") : TEXT("failed"));
        One->SetStringField(TEXT("kind"), Made->Kind);
        One->SetStringField(TEXT("header"), Made->Header);
        Replies->SetObjectField(Made->Package, One);
        if (There)
        {
            Out.Imported.Add(Made->Package, Made->Kind);
            ImportedIdentities.Add(FName(*Made->Package));
        }
        else if (!Admission.GameReferenceReasons.Contains(Made->Package))
        {
            NotImported.Add(FName(*Made->Package));
            Out.NotImported.AddUnique(Made->Package);
            if (BestEffort)
                GameBackedPackages.Add(FName(*Made->Package));
        }
        else
        {
            One->SetStringField(TEXT("state"), TEXT("original game reference"));
            One->SetBoolField(TEXT("reference_only"), true);
        }
    }
    Checkpoint();
    if (ReportWriteFailed || FatalFailure || Admission.Problems.Num() || BlueprintResult.bInfrastructureFailure)
        return false;
    return true;
}

void FP4KadanImportRun::ImportRemainingDeclaredDependencies()
{
    // Table-only requests have no world or Blueprint boundary to trigger this.
    if (HasDeferredDependencies() && !Stop && !Problems.Num() && !Cancelled && !FatalFailure && !ReportWriteFailed)
    {
        SnapshotExistingClasses();
        FString Error;
        if (!ImportDeclaredDependencies(TMap<FName, UClass*>(), Error))
        {
            Problems.Add(MakeShared<FJsonValueString>(Error));
            FatalFailure = true;
            Checkpoint(true);
        }
    }
}

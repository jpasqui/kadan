// The import plan: the packages a request needs, in the order they import, and what they need first.

#include "P4KadanImportInternal.h"
#include "P4GamePaths.h"

using namespace P4KadanImport;

FP4KadanImportPlan P4KadanPlanImport(const TArray<TSharedPtr<FP4KadanSourceAsset>>& Assets, const TSet<FString>& Roots,
    bool Resume, bool DeferMaterialExecution)
{
    FP4KadanImportPlan Plan;
    Plan.Scanned = Assets;
    Plan.bMaterialExecutionDeferred = DeferMaterialExecution;
    TUniquePtr<FP4KadanImportSourceScope> ExecutionScope;
    if (DeferMaterialExecution)
        ExecutionScope = MakeUnique<FP4KadanImportSourceScope>(Assets);
    TMap<FName, TSharedPtr<FP4KadanSourceAsset>> ByPackage;
    for (const auto& Asset : Assets)
    {
        if (!Asset.IsValid())
            continue;
        const FName Key(*Asset->Package);
        if (DeferMaterialExecution && ByPackage.Contains(Key))
        {
            Plan.Problems.Add(Asset->Package + TEXT(" has ambiguous scanned source rows."));
            continue;
        }
        ByPackage.Add(Key, Asset);
    }
    TSet<FName> Wanted, Missing, GameReferences, RootIdentities;
    for (const FString& Root : Roots)
    {
        const FName Key(*Root);
        if (DeferMaterialExecution && (!P4IsValidContentPackage(Root) || RootIdentities.Contains(Key)))
            Plan.Problems.Add(Root + TEXT(" is not a unique canonical requested package."));
        const auto* Found = ByPackage.Find(Key);
        // Container spelling and the package header can differ in case. Select by Unreal's
        // package identity, then keep the scanned header's spelling for every source proof.
        RootIdentities.Add(Key);
        Plan.RequestedPackages.Add(Found ? (*Found)->Package : Root);
        if (DeferMaterialExecution)
        {
            FString Digest, Error;
            if (!Found)
                Error = TEXT("No scanned source has this package identity.");
            if (!Found || !P4KadanRequestedSourceDigest(**Found, Digest, Error))
                Plan.Problems.Add(Root + TEXT(" requested source identity was not verified: ") + Error);
            else
                Plan.RequestedSourceDigests.Add(Key, Digest);
        }
    }
    Plan.RequestedPackages.Sort();
    auto ExecutionDependencies = [&](const FP4KadanSourceAsset& Asset)
    {
        TArray<FString> Out;
        FString Error;
        if (!Plan.ExecutionEdges.Dependencies(Asset, Out, Error))
            Plan.Problems.AddUnique(Asset.Package + TEXT(" execution source changed: ") + Error);
        return Out;
    };
    Plan.Roots = RootIdentities.Num();
    TArray<FString> Pending = Roots.Array();
    while (Pending.Num())
    {
        const FString Package = Pending.Pop(false);
        const FName Identity(*Package);
        if (Wanted.Contains(Identity))
            continue;
        const auto* Found = ByPackage.Find(Identity);
        if (!Found)
        {
            if (!ExistingReal(Package) && !Missing.Contains(Identity))
            {
                Missing.Add(Identity);
                if (RootIdentities.Contains(Identity))
                    Plan.Problems.Add(
                        Package + TEXT(" is required, but it is in neither this game folder nor the project."));
            }
            continue;
        }
        if (DeferMaterialExecution)
        {
            FString Error;
            if (!Plan.ExecutionEdges.Read(**Found, Error))
            {
                Plan.Problems.Add((*Found)->Package + TEXT(" material execution source is invalid: ") + Error);
                continue;
            }
        }
        Wanted.Add(Identity);
        // A package the project has already is kept, and what it needs that the project lacks is still
        // brought in: a placed class's mesh is never left a stand-in when the game folder holds it.
        for (const FString& Dependency : ExecutionDependencies(**Found))
            if (FollowsSoftReferences((*Found)->Kind) || !SoftDependency(*(*Found), Dependency))
                Pending.Add(Dependency);
    }
    // A native class-typed table column needs the genuine generated class, not its
    // completed defaults. Prove this from used serialized values and actual native
    // FClassProperty schemas; the import map also contains unused CDO/template links.
    for (const FName& Identity : Wanted)
    {
        const auto& Asset = ByPackage[Identity];
        // Tables, and any other asset of the generic rank (an animation whose notify names a camera
        // shake Blueprint class, for one), wait for the Blueprint declarations they name by class.
        if (Rank(Asset->Kind) != 5 || (Resume && ExistingReal(Asset->Package)))
            continue;
        TSet<FName> Targets;
        for (const FString& Dependency : ExecutionDependencies(*Asset))
        {
            const FName RequiredIdentity(*Dependency);
            if (SoftDependency(*Asset, Dependency) || !Wanted.Contains(RequiredIdentity) || ExistingReal(Dependency))
                continue;
            const auto& Required = ByPackage[RequiredIdentity];
            if (Required->Kind != TEXT("LevelSequence") && BlueprintKind(Required->Kind) &&
                Rank(Required->Kind) > Rank(Asset->Kind))
                Targets.Add(RequiredIdentity);
        }
        if (!Targets.Num())
            continue;
        TArray<FP4OriginalTableClassReference> Fields;
        FString Why;
        if (P4OriginalTableDeclarationReferences(Asset->Header, Asset->Body, Targets, Fields, Why))
        {
            Plan.DeclarationReferences.Add(Identity, MoveTemp(Fields));
            Plan.DeclarationDeferred.Add(Identity);
        }
        else
            Plan.Problems.Add(Asset->Package + TEXT(" cannot follow class declarations: ") + Why);
    }
    // Preserve early textures/materials/meshes needed by declaration. Only the
    // ordinary rank-5 object/table hard dependents join the proven table boundary.
    for (bool Changed = true; Changed;)
    {
        Changed = false;
        for (const FName& Identity : Wanted)
        {
            const auto& Asset = ByPackage[Identity];
            if (Rank(Asset->Kind) != 5 || Plan.DeclarationDeferred.Contains(Identity))
                continue;
            for (const FString& Dependency : ExecutionDependencies(*Asset))
                if (!SoftDependency(*Asset, Dependency) && Plan.DeclarationDeferred.Contains(FName(*Dependency)))
                {
                    Plan.DeclarationDeferred.Add(Identity);
                    Changed = true;
                    break;
                }
        }
    }
    TMap<FName, int32> References;
    for (const FName& Identity : Wanted)
    {
        const auto& Asset = ByPackage[Identity];
        const FString& Package = Asset->Package;
        Plan.Assets.Add(Asset);
        Plan.SourceBytes += Asset->SourceBytes;
        if (ExistingReal(Package))
            ++Plan.AlreadyPresent;
        else
            Plan.RemainingSourceBytes += Asset->SourceBytes;
        const bool Preserved = Resume && ExistingReal(Package);
        for (const FString& Dependency : ExecutionDependencies(*Asset))
        {
            const bool Soft = SoftDependency(*Asset, Dependency);
            if (Soft && !FollowsSoftReferences(Asset->Kind))
                continue;
            const FName DependencyIdentity(*Dependency);
            if (Missing.Contains(DependencyIdentity) && !Preserved)
            {
                const FString Warning = Soft ? Package + TEXT(" names ") + Dependency +
                        TEXT(
                            " by a soft reference, and that package is in neither the game folder nor the project. The reference is kept as a path.")
                                             : Package + TEXT(" needs ") + Dependency +
                        TEXT(
                            ", which is in neither the game folder nor the project. Its importer decides whether it can do without it.");
                Plan.Warnings.Add(Warning);
                Plan.MissingWarnings.Add(Warning, DependencyIdentity);
                continue;
            }
            if (!Wanted.Contains(DependencyIdentity))
                continue;
            ++References.FindOrAdd(DependencyIdentity);
            const auto& Required = ByPackage[DependencyIdentity];
            const bool DeclaredClassEdge = Plan.DeclarationReferences.Contains(Identity) &&
                BlueprintKind(Required->Kind) && Required->Kind != TEXT("LevelSequence");
            if (!Soft && !Preserved && !ExistingReal(Dependency) && Rank(Required->Kind) > Rank(Asset->Kind) &&
                !DeclaredClassEdge)
                Plan.Problems.AddUnique(Package + TEXT(" needs the ") + Required->Kind + TEXT(" ") + Dependency +
                    TEXT(", which imports later, so it cannot be imported yet."));
        }
    }
    for (const FName& Identity : Wanted)
        if (!RootIdentities.Contains(Identity))
            ++Plan.Dependencies;
    if (DeferMaterialExecution)
        for (const FName& Identity : Missing)
            if (!RootIdentities.Contains(Identity))
                ++Plan.NotYetRead;
    Plan.GameReferences = GameReferences.Num();
    for (const auto& Pair : References)
        if (Pair.Value > 1)
            ++Plan.SharedDependencies;
    // Stable dependency-first traversal within each importer rank. Soft references are followed for the
    // order too, but a cycle that goes through one is allowed. A same-rank cycle of required packages is
    // reported instead of guessed through retries. Blueprint cycles are handled by native class predeclaration.
    Plan.Assets.Sort(
        [](const TSharedPtr<FP4KadanSourceAsset>& A, const TSharedPtr<FP4KadanSourceAsset>& B)
        {
            const int32 Left = Rank(A->Kind), Right = Rank(B->Kind);
            return Left == Right ? A->Package < B->Package : Left < Right;
        });
    TArray<TSharedPtr<FP4KadanSourceAsset>> Ordered;
    TSet<FName> Done;
    // The packages being visited, in path order, and whether each was reached by a soft reference.
    TArray<FName> Path;
    TArray<bool> ReachedSoftly;
    TFunction<void(const TSharedPtr<FP4KadanSourceAsset>&, bool)> Visit;
    Visit = [&](const TSharedPtr<FP4KadanSourceAsset>& Asset, bool Softly)
    {
        const FName Identity(*Asset->Package);
        if (Done.Contains(Identity))
            return;
        const int32 At = Path.Find(Identity);
        if (At != INDEX_NONE)
        {
            bool SoftCycle = Softly;
            for (int32 Step = At + 1; Step < ReachedSoftly.Num(); ++Step)
                SoftCycle |= ReachedSoftly[Step];
            if (!SoftCycle && !BlueprintKind(Asset->Kind))
            {
                bool NativeAnimationCycle = true;
                for (int32 Step = At; Step < Path.Num(); ++Step)
                {
                    const FString& Kind = ByPackage[Path[Step]]->Kind;
                    NativeAnimationCycle &= Kind == TEXT("AnimSequence") || Kind == TEXT("AnimMontage");
                }
                if (NativeAnimationCycle)
                    for (int32 Step = At; Step < Path.Num(); ++Step)
                        Plan.CyclicAnimations.Add(Path[Step]);
                else
                    Plan.Problems.Add(Asset->Package +
                        TEXT(
                            " is in a loop of packages that need each other, which this import cannot order for its kind."));
            }
            return;
        }
        Path.Add(Identity);
        ReachedSoftly.Add(Softly);
        for (const FString& Dependency : ExecutionDependencies(*Asset))
        {
            const auto* Found = ByPackage.Find(FName(*Dependency));
            if (Found && Wanted.Contains(FName(*Dependency)) && Rank((*Found)->Kind) <= Rank(Asset->Kind))
                Visit(*Found, SoftDependency(*Asset, Dependency));
        }
        Path.Pop();
        ReachedSoftly.Pop();
        Done.Add(Identity);
        Ordered.Add(Asset);
    };
    for (const auto& Asset : Plan.Assets)
        Visit(Asset, false);
    Plan.Assets = MoveTemp(Ordered);
    return Plan;
}

FP4KadanImportResult P4KadanImportMap(const FString& GameFolder, const FString& MapPackage, bool Resume)
{
    FP4KadanImportSettings Settings;
    Settings.GameFolder = GameFolder;
    return P4KadanImportMap(Settings, MapPackage, Resume);
}

FP4KadanImportResult P4KadanImportMap(const FP4KadanImportSettings& Settings, const FString& MapPackage, bool Resume)
{
    return P4KadanRunImport(Settings, P4KadanPlanImport(Settings, TSet<FString>{MapPackage}, Resume), Resume);
}

FP4KadanImportPlan P4KadanPlanImport(const FP4KadanImportSettings& Settings, const TSet<FString>& Roots, bool Resume)
{
    const bool CatalogSource = P4KadanIsCatalogGameFolder(Settings.GameFolder);
    if (CatalogSource)
    {
        FString Error;
        if (!P4KadanPrepareCatalogImport(Settings, Error))
        {
            FP4KadanImportPlan Plan;
            Plan.Problems.Add(Error);
            return Plan;
        }
    }
    TArray<TSharedPtr<FP4KadanSourceAsset>> Assets;
    TArray<FString> ScanProblems;
    if (!P4KadanScanGame(Settings.GameFolder, Assets, ScanProblems))
    {
        // A first catalog import has a valid owned cache but no converted
        // headers yet. Acquire the selected roots below; actual corrupt or
        // arbitrary advanced/extracted folders remain scanner errors.
        TArray<FString> Headers;
        if (CatalogSource)
        {
            IFileManager::Get().FindFilesRecursive(Headers, *Settings.GameFolder, TEXT("*.uasset"), true, false);
            IFileManager::Get().FindFilesRecursive(Headers, *Settings.GameFolder, TEXT("*.umap"), true, false, false);
        }
        if (!CatalogSource || Headers.Num())
        {
            FP4KadanImportPlan Plan;
            Plan.Problems = ScanProblems;
            return Plan;
        }
        ScanProblems.Reset();
    }
    TSet<FName> Known;
    for (const auto& Asset : Assets)
        Known.Add(FName(*Asset->Package));
    const bool Containers = !Settings.GamePaks.IsEmpty() && !Settings.Retoc.IsEmpty() &&
        IFileManager::Get().DirectoryExists(*Settings.GamePaks);
    const FString Into = FPaths::ProjectSavedDir() / TEXT("Kadan/RequestedOriginals");
    TSet<FName> Read;
    TArray<FString> ExtractionProblems;
    FP4KadanContainerIndex ContainerIndex;
    bool ContainerIndexAttempted = false, ContainerIndexReady = false;
    FString ContainerIndexError;
    for (const FString& Package : Roots)
    {
        if (Known.Contains(FName(*Package)))
            continue;
        if (!P4IsValidContentPackage(Package))
        {
            ExtractionProblems.Add(TEXT("Choose a canonical game package path."));
            continue;
        }
        if (!Containers)
            continue;
        if (!ContainerIndexAttempted)
        {
            ContainerIndexAttempted = true;
            ContainerIndexReady = P4KadanReadContainerIndex(Settings, ContainerIndex, ContainerIndexError);
        }
        if (!ContainerIndexReady)
        {
            ExtractionProblems.Add(
                TEXT("The game's container index could not be read (") + ContainerIndexError + TEXT(")."));
            break;
        }
        if (!IFileManager::Get().MakeDirectory(*Into, true))
        {
            ExtractionProblems.Add(TEXT("The requested original source folder could not be prepared."));
            break;
        }
        // Remove only this request's prior extracted files, so a package absent from the actual
        // selected game cannot be mistaken for one left by an earlier source input.
        const FString Base = Into / P4ContainerRelativePath(Package);
        for (const TCHAR* Suffix : {TEXT(".uasset"), TEXT(".umap"), TEXT(".uexp"), TEXT(".ubulk")})
            if (IFileManager::Get().FileExists(*(Base + Suffix)) && !IFileManager::Get().Delete(*(Base + Suffix)))
                ExtractionProblems.Add(Package + TEXT(" could not refresh its original source."));
        if (ExtractionProblems.Num())
            continue;
        FString Error;
        if (!ExtractFromContainers(Settings, ContainerIndex, Package, Into, Error))
        {
            ExtractionProblems.Add(
                Package + TEXT(" could not be read from the game's containers (") + Error + TEXT(")."));
            continue;
        }
        Read.Add(FName(*Package));
    }
    if (Read.Num())
    {
        TArray<TSharedPtr<FP4KadanSourceAsset>> Extracted;
        TArray<FString> Problems;
        if (P4KadanScanGame(Into, Extracted, Problems))
            for (const auto& Asset : Extracted)
                if (Read.Contains(FName(*Asset->Package)))
                {
                    FString RetainedSourceError;
                    if (!RetainAcquiredWorldSource(Settings, ContainerIndex, *Asset, RetainedSourceError))
                    {
                        ExtractionProblems.Add(Asset->Package +
                            TEXT(" could not retain its exact current converted source: ") + RetainedSourceError);
                        continue;
                    }
                    Assets.Add(Asset);
                }
        ScanProblems.Append(Problems);
    }
    FP4KadanImportPlan Plan = P4KadanPlanImport(Assets, Roots, Resume, Containers);
    Plan.Problems.Append(ExtractionProblems);
    for (const FString& Problem : ScanProblems)
        Plan.Warnings.Add(TEXT("Game folder note. ") + Problem);
    return Plan;
}

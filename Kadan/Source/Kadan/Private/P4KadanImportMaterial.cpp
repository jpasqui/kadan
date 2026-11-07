// The import of a game material or material instance: its family's sources and shader-used resources,
// read once per family, and the material recovered from them.

#include "P4KadanImportInternal.h"
#include "P4Common.h"

using namespace P4KadanImport;

FP4KadanImportRun::FImportOutcome FP4KadanImportRun::ImportMaterial(FAssetImport& Item)
{
    const TSharedPtr<FP4KadanSourceAsset>& Asset = Item.Asset;
    // Package conversion intentionally uses --no-shaders. Only a fresh
    // material resource or source-bound old raw MIC reaches here.
    // Acquire its real shader library once; no whole material closure is
    // reconverted, and a failed library remains an individual refusal.
    AcquireMaterialShaderArchive();
    FMaterialImport Family;
    Family.FamilySourceRefused = !RecoveryArchiveProblem.IsEmpty() && Plan.bMaterialExecutionDeferred;
    Family.Reused = FamilyRuns.Find(FName(*Asset->Package));
    if (const FImportOutcome Outcome = PrepareMaterialFamily(Item, Family))
        return Outcome;
    Family.Recovery = Family.FamilyRecovery ? Family.FamilyRecovery.Get()
        : ExtractedRecovery                 ? ExtractedRecovery.Get()
                                            : MaterialRecovery.Get();
    WatchMaterialFamily(Family);
    if (const FImportOutcome Outcome = DiscoverFamilyUsedResources(Item, Family))
        return Outcome;
    KeepFamilyRun(Family);
    if (Item.DeferredMICAdmission || Item.DeferredMasterDependencyAdmission)
    {
        if (const FImportOutcome Outcome = AdmitExistingMaterial(Item, Family))
            return Outcome;
    }
    RecoverMaterial(Item, Family);
    // FamilyRecovery dies before PreparedFamily, so no recipe can read
    // released stage paths. The next material consumes its stage serially.
    return {};
}

void FP4KadanImportRun::AcquireMaterialShaderArchive()
{
    if (!RecoveryArchiveAttempted && !Settings.GamePaks.IsEmpty() && !Settings.Retoc.IsEmpty())
    {
        RecoveryArchiveAttempted = true;
        const FString Cache = ConfiguredConversionCache(Settings);
        FString CurrentCache, CacheError;
        const bool CacheExists = !Cache.IsEmpty() &&
            (IFileManager::Get().DirectoryExists(*Cache) || IFileManager::Get().FileExists(*Cache));
        const bool SafeCache = !Cache.IsEmpty() &&
            (!CacheExists || P4KadanRetainedConversionCacheCurrent(Settings, CurrentCache, CacheError));
        if (!SafeCache)
            RecoveryArchiveProblem = TEXT("Material recovery cannot replace an invalid existing conversion cache: ") +
                CacheError;
        else if (P4KadanAcquireShaderArchive(Settings, RecoveryArchive, RecoveryArchiveProblem))
        {
            MaterialRecovery = MakeUnique<FP4MaterialRecovery>(Settings.GameFolder, Sources, RecoveryArchive);
            if (ExtractedRecovery)
                ExtractedRecovery = MakeUnique<FP4MaterialRecovery>(Settings.GameFolder, Sources, RecoveryArchive);
        }
    }
}

FP4KadanImportRun::FImportOutcome FP4KadanImportRun::PrepareMaterialFamily(FAssetImport& Item, FMaterialImport& Family)
{
    const TSharedPtr<FP4KadanSourceAsset>& Asset = Item.Asset;
    FP4MaterialFamilySourcesPtr& CompleteFamily = Family.CompleteFamily;
    FP4MaterialFamilyPreparedStagePtr& PreparedFamily = Family.PreparedFamily;
    TSharedPtr<FP4MaterialRecovery>& FamilyRecovery = Family.FamilyRecovery;
    TArray<TSharedPtr<FP4KadanSourceAsset>>& RecoverySources = Family.RecoverySources;
    TArray<TSharedPtr<FP4KadanSourceAsset>>& UsedResourceRows = Family.UsedResourceRows;
    TSharedPtr<FP4MaterialUsedResources>& FinalUsed = Family.FinalUsed;
    TSharedPtr<FJsonObject>& FamilyEvidence = Family.FamilyEvidence;
    FString& FamilyProblem = Family.FamilyProblem;
    bool& FamilySourceRefused = Family.FamilySourceRefused;
    const TSharedPtr<FFamilyRun>* const Reused = Family.Reused;
    if (Reused)
    {
        const FFamilyRun& Run = **Reused;
        CompleteFamily = Run.CompleteFamily;
        PreparedFamily = Run.PreparedFamily;
        FamilyRecovery = Run.Recovery;
        RecoverySources = Run.RecoverySources;
        UsedResourceRows = Run.UsedResourceRows;
        FinalUsed = Run.FinalUsed;
        FamilyEvidence = Run.Evidence;
        FamilyProblem = Run.Problem;
        FamilySourceRefused = Run.Refused;
    }
    else if (RecoveryArchiveProblem.IsEmpty() && !Settings.GamePaks.IsEmpty())
    {
        // Hold one actual current registry/input epoch for this run.
        if (!MaterialRegistry.IsValid() && !MaterialRegistryAttempted)
        {
            MaterialRegistryAttempted = true;
            FP4ExportContainerTools Tools;
            Tools.Retoc = Settings.Retoc;
            Tools.Repak = Settings.Repak;
#if P4_KADAN_TEST_SEAMS
            FString RetainedProblem;
            if (!P4ReadRetainedCurrentGameRegistry(Tools, Settings.GamePaks, MaterialRegistry, RetainedProblem))
            {
                if (P4KadanSourceAcquisitionAllowed({Asset->Package}, MaterialRegistryProblem))
                    P4AcquireCurrentGameRegistry(Tools, Settings.GamePaks, Folder / TEXT("MaterialFamilyRegistry"),
                        MaterialRegistry, MaterialRegistryProblem);
                else
                    MaterialRegistryProblem += TEXT(" ") + RetainedProblem;
            }
#else
            P4AcquireCurrentGameRegistry(Tools, Settings.GamePaks, Folder / TEXT("MaterialFamilyRegistry"),
                MaterialRegistry, MaterialRegistryProblem);
#endif
        }
        if (!MaterialRegistry.IsValid())
            FamilyProblem = MaterialRegistryProblem;
        else
        {
            // Complete registered families need neither a cache nor a list.
            TArray<FString> MissingFamily;
            const auto Read = P4ReadMaterialFamilySources(Settings, MaterialRegistry, Asset->Package, Sources, nullptr,
                CompleteFamily, MissingFamily, FamilyProblem);
            if (Read == EP4MaterialFamilySourceRead::MissingSources)
            {
                if (!MaterialSourceContext.IsValid())
                    P4CreateMaterialFamilyAcquireContext(
                        Settings, MaterialRegistry, MaterialSourceContext, FamilyProblem);
                if (MaterialSourceContext.IsValid())
                {
                    const auto Prepared = P4PrepareMissingMaterialFamilyStage(MaterialSourceContext, Asset->Package,
                        Sources, nullptr, MaterialShouldCancel, PreparedFamily, FamilyProblem);
                    if (Prepared == EP4MaterialFamilyStageResult::Cancelled)
                    {
                        Cancelled = true;
                        Replies->SetObjectField(Asset->Package,
                            NativeImportReport(false, FamilyProblem, Asset->Package, FString(), false, false, false,
                                TEXT("cancelled")));
                        return false;
                    }
                    if (Prepared == EP4MaterialFamilyStageResult::Ready)
                        CompleteFamily = PreparedFamily->Sources();
                }
            }
        }
        if (!CompleteFamily.IsValid())
        {
            FamilySourceRefused = true;
            if (FamilyProblem.IsEmpty())
                FamilyProblem = TEXT("The complete current material family source set was not validated.");
        }
        else
        {
            TArray<TSharedPtr<FP4KadanSourceAsset>> FamilySources;
            CompleteFamily->AppendMetadataSources(FamilySources);
            RecoverySources = Sources;
            // Collections/resources remain resolvable, but do not make
            // this exact recipe rediscover every unrelated scanned MIC.
            RecoverySources.RemoveAll(
                [](const TSharedPtr<FP4KadanSourceAsset>& Existing)
                {
                    return Existing.IsValid() &&
                        (Existing->Kind == TEXT("Material") || Existing->Kind == TEXT("MaterialInstanceConstant"));
                });
            for (const auto& Member : FamilySources)
            {
                RecoverySources.RemoveAll([&](const TSharedPtr<FP4KadanSourceAsset>& Existing)
                    { return Existing.IsValid() && FName(*Existing->Package) == FName(*Member->Package); });
                RecoverySources.Add(Member);
            }
            // New recipe/source state, explicitly shared already-parsed archive.
            FamilyRecovery = MakeShared<FP4MaterialRecovery>(*MaterialRecovery, RecoverySources);
            FamilyEvidence = MakeShared<FJsonObject>();
            FamilyEvidence->SetStringField(TEXT("registry_sha256"), CompleteFamily->RegistrySha256());
            FamilyEvidence->SetStringField(TEXT("registry_rows_sha256"), CompleteFamily->RegistryRowsSha256());
            FamilyEvidence->SetStringField(TEXT("source_set_sha256"), CompleteFamily->SourceSetSha256());
            FamilyEvidence->SetNumberField(TEXT("members"), CompleteFamily->Bindings().Num());
            FamilyEvidence->SetNumberField(TEXT("shader_owners"), CompleteFamily->ShaderOwnerCount());
            FamilyEvidence->SetNumberField(
                TEXT("converted_packages"), PreparedFamily.IsValid() ? PreparedFamily->ConvertedPackages() : 0);
            FamilyEvidence->SetBoolField(TEXT("metadata_only"), true);
            FamilyEvidence->SetBoolField(TEXT("cache_publication"), false);
        }
    }
    return {};
}

// The check that a material family's sources are still the ones its import read.
void FP4KadanImportRun::WatchMaterialFamily(FMaterialImport& Family)
{
    if (Family.CompleteFamily.IsValid())
        Family.CurrentFamily = [this, &Family](FString& Error)
        {
            FP4MaterialFamilySourcesPtr& CompleteFamily = Family.CompleteFamily;
            FP4MaterialFamilyPreparedStagePtr& PreparedFamily = Family.PreparedFamily;
            TSharedPtr<FP4MaterialUsedResources>& FinalUsed = Family.FinalUsed;
            TArray<TSharedPtr<FP4KadanSourceAsset>>& UsedResourceRows = Family.UsedResourceRows;
            bool& FamilySourceRefused = Family.FamilySourceRefused;
            const double ValidationStarted = FPlatformTime::Seconds();
            // Nested in the material import phase; do not add this to that phase's total.
            ON_SCOPE_EXIT
            {
                Spent(TEXT("material source validation (nested)"), ValidationStarted);
            };
            bool Current = PreparedFamily.IsValid() ? PreparedFamily->Current(Error)
                                                    : CompleteFamily->Current(Settings, Error);
            if (Current && FinalUsed)
                for (const auto& Request : FinalUsed->Requests)
                {
                    const TSharedPtr<FP4KadanSourceAsset>* Row = UsedResourceRows.FindByPredicate(
                        [&](const TSharedPtr<FP4KadanSourceAsset>& One)
                        { return One->Package.Equals(Request.Package, ESearchCase::CaseSensitive); });
                    if (!Row || !P4KadanMaterialResourceReady(Request, **Row, Error))
                    {
                        Current = false;
                        break;
                    }
                }
            FamilySourceRefused |= !Current;
            return Current;
        };
}

FP4KadanImportRun::FImportOutcome FP4KadanImportRun::DiscoverFamilyUsedResources(
    FAssetImport& Item, FMaterialImport& Family)
{
    const TSharedPtr<FP4KadanSourceAsset>& Asset = Item.Asset;
    const TSharedPtr<FFamilyRun>* const Reused = Family.Reused;
    FString& FamilyProblem = Family.FamilyProblem;
    FP4MaterialFamilySourcesPtr& CompleteFamily = Family.CompleteFamily;
    TSharedPtr<FP4MaterialRecovery>& FamilyRecovery = Family.FamilyRecovery;
    TFunction<bool(FString&)>& CurrentFamily = Family.CurrentFamily;
    bool& FamilySourceRefused = Family.FamilySourceRefused;
    TArray<TSharedPtr<FP4KadanSourceAsset>>& RecoverySources = Family.RecoverySources;
    FP4MaterialFamilyPreparedStagePtr& PreparedFamily = Family.PreparedFamily;
    FP4MaterialRecovery*& Recovery = Family.Recovery;
    TSharedPtr<FJsonObject>& FamilyEvidence = Family.FamilyEvidence;
    TSharedPtr<FP4MaterialUsedResources>& FinalUsed = Family.FinalUsed;
    TArray<TSharedPtr<FP4KadanSourceAsset>>& UsedResourceRows = Family.UsedResourceRows;
    if (!Reused && Plan.bMaterialExecutionDeferred && FamilyProblem.IsEmpty() && RecoveryArchiveProblem.IsEmpty())
    {
        if (!CompleteFamily.IsValid() || !FamilyRecovery || !CurrentFamily)
        {
            FamilyProblem = TEXT("Deferred material execution requires a complete current all-owner source family.");
            FamilySourceRefused = true;
        }
        else
        {
            // Compiled uniforms need actual cached collection layouts and referenced texture
            // metadata before graph discovery. These rows are raw recipe inputs only.
            bool BindingMetadataRequired = false;
            if (!P4MaterialFamilyBindingMetadataRequired(
                    Settings, *CompleteFamily, RecoverySources, BindingMetadataRequired, FamilyProblem))
                FamilySourceRefused = true;
            if (BindingMetadataRequired && !FamilySourceRefused && !PreparedFamily.IsValid())
            {
                if (!MaterialSourceContext.IsValid())
                    P4CreateMaterialFamilyAcquireContext(
                        Settings, MaterialRegistry, MaterialSourceContext, FamilyProblem);
                if (MaterialSourceContext.IsValid())
                {
                    const auto Prepared = P4PrepareMissingMaterialFamilyStage(MaterialSourceContext, Asset->Package,
                        Sources, nullptr, MaterialShouldCancel, PreparedFamily, FamilyProblem);
                    if (Prepared == EP4MaterialFamilyStageResult::Cancelled)
                    {
                        Cancelled = true;
                        return false;
                    }
                }
            }
            if (BindingMetadataRequired && !PreparedFamily.IsValid())
                FamilySourceRefused = true;
            if (BindingMetadataRequired && PreparedFamily.IsValid())
            {
                TArray<TSharedPtr<FP4KadanSourceAsset>> BindingMetadata;
                const auto Prepared = PreparedFamily->PrepareBindingMetadata(
                    RecoverySources, MaterialShouldCancel, BindingMetadata, FamilyProblem);
                if (Prepared == EP4MaterialFamilyStageResult::Cancelled)
                {
                    Cancelled = true;
                    return false;
                }
                if (Prepared == EP4MaterialFamilyStageResult::Refused)
                    FamilySourceRefused = true;
                if (Prepared == EP4MaterialFamilyStageResult::Ready && BindingMetadata.Num())
                {
                    for (const auto& Row : BindingMetadata)
                    {
                        RecoverySources.RemoveAll([&](const auto& Existing)
                            { return Existing.IsValid() && FName(*Existing->Package) == FName(*Row->Package); });
                        RecoverySources.Add(Row);
                    }
                    FamilyRecovery = MakeShared<FP4MaterialRecovery>(*MaterialRecovery, RecoverySources);
                    Recovery = FamilyRecovery.Get();
                    if (FamilyEvidence.IsValid())
                    {
                        FamilyEvidence->SetNumberField(TEXT("binding_metadata_packages"), BindingMetadata.Num());
                        FamilyEvidence->SetStringField(TEXT("binding_metadata_scope"),
                            TEXT(
                                "cached typed collection layouts and anonymous sampler metadata only; no executable or shader-used admission"));
                    }
                }
            }
            if (!FamilySourceRefused)
            {
                const auto FirstUsed = Recovery->DiscoverUsedResources(*CompleteFamily, CurrentFamily);
                if (FamilyEvidence.IsValid())
                {
                    FamilyEvidence->SetNumberField(TEXT("used_owner_trace_calls"), FirstUsed.OwnerTraceCalls);
                    FamilyEvidence->SetNumberField(TEXT("first_parallel_owner_traces"), FirstUsed.ParallelOwnerTraces);
                    FamilyEvidence->SetStringField(TEXT("archive_sha256"), FirstUsed.ArchiveSha256);
                }
                // A shader whose reads cannot all be followed (an engine texture, a value the trace
                // does not model) leaves the family's sources as they were read: the material is not
                // recovered, and the game's file of it is kept for reference only, as for any other
                // material that cannot be recovered. A source that failed its checks keeps nothing.
                if (FirstUsed.Status != EP4MaterialUsedResourceStatus::Complete)
                {
                    FamilyProblem = FirstUsed.Reason.IsEmpty()
                        ? FString(TEXT("the shaders' resource reads were not all followed"))
                        : FirstUsed.Reason;
                    FamilySourceRefused = FirstUsed.Status != EP4MaterialUsedResourceStatus::Incomplete ||
                        FirstUsed.RefusalCategory == TEXT("material used native archive");
                }
                else if (FirstUsed.Requests.Num() == 0)
                {
                    // No source metadata changed: retain this exact current
                    // discovery and avoid another four-owner shader trace.
                    FinalUsed = MakeShared<FP4MaterialUsedResources>(FirstUsed);
                }
                else
                {
                    bool CanExecute = true;
                    for (const auto& Request : FirstUsed.Requests)
                        if (!P4KadanMaterialResourceCanExecute(Request, FamilyProblem))
                        {
                            CanExecute = false;
                            break;
                        }
                    if (!CanExecute)
                        FamilySourceRefused = true;
                    if (CanExecute && !PreparedFamily.IsValid())
                    {
                        if (!MaterialSourceContext.IsValid())
                            P4CreateMaterialFamilyAcquireContext(
                                Settings, MaterialRegistry, MaterialSourceContext, FamilyProblem);
                        if (MaterialSourceContext.IsValid())
                        {
                            const auto Prepared = P4PrepareMissingMaterialFamilyStage(MaterialSourceContext,
                                Asset->Package, Sources, nullptr, MaterialShouldCancel, PreparedFamily, FamilyProblem);
                            if (Prepared == EP4MaterialFamilyStageResult::Cancelled)
                            {
                                Cancelled = true;
                                return false;
                            }
                        }
                    }
                    if (CanExecute && PreparedFamily.IsValid())
                    {
                        const auto Prepared = PreparedFamily->PrepareUsedResources(
                            FirstUsed, RecoverySources, MaterialShouldCancel, UsedResourceRows, FamilyProblem);
                        if (Prepared == EP4MaterialFamilyStageResult::Cancelled)
                        {
                            Cancelled = true;
                            return false;
                        }
                        if (Prepared == EP4MaterialFamilyStageResult::Ready)
                        {
                            for (const auto& Row : UsedResourceRows)
                            {
                                RecoverySources.RemoveAll([&](const TSharedPtr<FP4KadanSourceAsset>& Existing)
                                    { return Existing.IsValid() && FName(*Existing->Package) == FName(*Row->Package); });
                                RecoverySources.Add(Row);
                            }
                            // Same immutable archive owner, exact local family + resource paths.
                            FamilyRecovery = MakeShared<FP4MaterialRecovery>(*MaterialRecovery, RecoverySources);
                            Recovery = FamilyRecovery.Get();
                            auto Verified = Recovery->DiscoverUsedResources(*CompleteFamily, CurrentFamily);
                            if (FamilyEvidence.IsValid())
                                FamilyEvidence->SetNumberField(TEXT("used_owner_trace_calls"),
                                    FirstUsed.OwnerTraceCalls + Verified.OwnerTraceCalls);
                            if (!P4KadanMaterialDiscoveryAfterHydration(FirstUsed, Verified, FamilyProblem))
                                FamilySourceRefused = true;
                            else
                            {
                                // Each executable resource is an actual typed shader requirement.
                                // Reverse MIC metadata never enters this execution list.
                                for (const auto& Row : UsedResourceRows)
                                {
                                    const auto* Request = Verified.Requests.FindByPredicate(
                                        [&](const FP4MaterialUsedResourceRequest& One)
                                        { return One.Package.Equals(Row->Package, ESearchCase::CaseSensitive); });
                                    bool NeedsImport = false;
                                    if (!CurrentFamily(FamilyProblem) || !Request ||
                                        !P4KadanMaterialResourceNeedsImport(*Request, *Row, NeedsImport, FamilyProblem))
                                    {
                                        FamilySourceRefused = true;
                                        break;
                                    }
                                    if (NeedsImport)
                                    {
                                        if (!ImportOne(Row))
                                        {
                                            FamilySourceRefused = true;
                                            break;
                                        }
                                    }
                                    else
                                    {
                                        auto Preserved = NativeImportReport(true, FString(), Row->Package,
                                            Request->Object, false, false, false,
                                            TEXT("existing typed shader resource preserved"));
                                        Preserved->SetStringField(TEXT("kind"), Row->Kind);
                                        Replies->SetObjectField(Row->Package, Preserved);
                                    }
                                    if (NotImported.Contains(FName(*Row->Package)) ||
                                        !P4KadanMaterialResourceReady(*Request, *Row, FamilyProblem))
                                    {
                                        FamilySourceRefused = true;
                                        break;
                                    }
                                }
                                if (!FamilySourceRefused)
                                    FinalUsed = MakeShared<FP4MaterialUsedResources>(MoveTemp(Verified));
                            }
                        }
                        else
                            FamilySourceRefused = true;
                    }
                    else
                        FamilySourceRefused = true;
                }
            }
            if (FamilySourceRefused && FamilyProblem.IsEmpty())
                FamilyProblem = TEXT("Complete typed shader-used resource admission was not proved.");
        }
    }
    return {};
}

void FP4KadanImportRun::KeepFamilyRun(FMaterialImport& Family)
{
    const TSharedPtr<FFamilyRun>* const Reused = Family.Reused;
    FP4MaterialFamilySourcesPtr& CompleteFamily = Family.CompleteFamily;
    TSharedPtr<FP4MaterialRecovery>& FamilyRecovery = Family.FamilyRecovery;
    FP4MaterialFamilyPreparedStagePtr& PreparedFamily = Family.PreparedFamily;
    TArray<TSharedPtr<FP4KadanSourceAsset>>& RecoverySources = Family.RecoverySources;
    TArray<TSharedPtr<FP4KadanSourceAsset>>& UsedResourceRows = Family.UsedResourceRows;
    TSharedPtr<FP4MaterialUsedResources>& FinalUsed = Family.FinalUsed;
    TSharedPtr<FJsonObject>& FamilyEvidence = Family.FamilyEvidence;
    FString& FamilyProblem = Family.FamilyProblem;
    bool& FamilySourceRefused = Family.FamilySourceRefused;
    if (!Reused && CompleteFamily.IsValid() && FamilyRecovery)
    {
        auto Run = MakeShared<FFamilyRun>();
        Run->CompleteFamily = CompleteFamily;
        Run->PreparedFamily = PreparedFamily;
        Run->Recovery = FamilyRecovery;
        Run->RecoverySources = RecoverySources;
        Run->UsedResourceRows = UsedResourceRows;
        Run->FinalUsed = FinalUsed;
        Run->Evidence = FamilyEvidence;
        Run->Problem = FamilyProblem;
        Run->Refused = FamilySourceRefused;
        for (const auto& Member : CompleteFamily->Bindings())
            FamilyRuns.Add(FName(*Member.Package), Run);
    }
    // Retain the already verified used-resource ledger before original admission;
    // a preserved original has the same completed discovery evidence as a new import.
    if (FamilyEvidence.IsValid())
    {
        if (FinalUsed)
        {
            TArray<TSharedPtr<FJsonValue>> Resources;
            for (const auto& Request : FinalUsed->Requests)
            {
                auto Resource = MakeShared<FJsonObject>();
                Resource->SetStringField(TEXT("package"), Request.Package);
                Resource->SetStringField(TEXT("object"), Request.Object);
                Resource->SetStringField(TEXT("native_class"), Request.NativeClass);
                Resource->SetStringField(TEXT("source_sha256"), Request.SourceSha256);
                Resource->SetStringField(TEXT("sampler_type"), Request.SamplerType);
                Resources.Add(MakeShared<FJsonValueObject>(Resource));
            }
            FamilyEvidence->SetArrayField(TEXT("typed_used_resources"), Resources);
        }
    }
}

FP4KadanImportRun::FImportOutcome FP4KadanImportRun::AdmitExistingMaterial(FAssetImport& Item, FMaterialImport& Family)
{
    const TSharedPtr<FP4KadanSourceAsset>& Asset = Item.Asset;
    bool& FamilySourceRefused = Family.FamilySourceRefused;
    FString& FamilyProblem = Family.FamilyProblem;
    FP4MaterialFamilySourcesPtr& CompleteFamily = Family.CompleteFamily;
    TSharedPtr<FP4MaterialRecovery>& FamilyRecovery = Family.FamilyRecovery;
    TFunction<bool(FString&)>& CurrentFamily = Family.CurrentFamily;
    TUniquePtr<FP4KadanImportSourceScope>& MaterialComparisonSources = Item.MaterialComparisonSources;
    TArray<TSharedPtr<FP4KadanSourceAsset>>& RecoverySources = Family.RecoverySources;
    FP4MaterialRecovery*& Recovery = Family.Recovery;
    bool& DeferredMICAdmission = Item.DeferredMICAdmission;
    TSharedPtr<FJsonObject>& FamilyEvidence = Family.FamilyEvidence;
    bool& DeferredMasterDependencyAdmission = Item.DeferredMasterDependencyAdmission;
    // Ready-family preparation remains the ordinary current-source
    // path. A read-only Recover here (never in ObjectState) supplies
    // the existing merged-recipe/read witness before old-MIC
    // comparison, including a preserved recovered parent.
    FString WitnessProblem;
    bool RecoveredWitness = false;
    FString RecoveredRecipeMd5;
    if (!FamilySourceRefused && FamilyProblem.IsEmpty() && CompleteFamily.IsValid() && FamilyRecovery &&
        CurrentFamily && CurrentFamily(WitnessProblem))
    {
        MaterialComparisonSources = MakeUnique<FP4KadanImportSourceScope>(RecoverySources);
        const auto ParentRecipe = Recovery->Recover(CompleteFamily->Master());
        RecoveredWitness = ParentRecipe.bOk && ParentRecipe.Kind == TEXT("Material") && CurrentFamily(WitnessProblem);
        if (!RecoveredWitness && WitnessProblem.IsEmpty())
            WitnessProblem = ParentRecipe.Reason;
        if (RecoveredWitness)
            RecoveredRecipeMd5 = FMD5::HashAnsiString(*ParentRecipe.Source);
        if (DeferredMICAdmission && FamilyEvidence.IsValid() && RecoveredWitness)
            FamilyEvidence->SetStringField(TEXT("original_binding_parent_recipe_md5"), RecoveredRecipeMd5);
    }
    if (WitnessProblem.IsEmpty() && !RecoveredWitness)
        WitnessProblem = FamilyProblem.IsEmpty()
            ? TEXT("The complete current parent recovery/read witness is unavailable.")
            : FamilyProblem;
    if (DeferredMICAdmission && FamilyEvidence.IsValid())
    {
        FamilyEvidence->SetBoolField(TEXT("original_binding_parent_recipe_recovered"), RecoveredWitness);
        FamilyEvidence->SetBoolField(TEXT("original_binding_witness_read_only"), true);
        if (!WitnessProblem.IsEmpty())
            FamilyEvidence->SetStringField(TEXT("original_binding_witness_error"), WitnessProblem);
    }
    if (DeferredMasterDependencyAdmission && RecoveredWitness)
    {
        UObject* Retained = nullptr;
        FString RetentionError;
        if (P4RetainUnchangedRecoveredMaterialDependency(
                Plan, *Asset, RecoveredRecipeMd5, CurrentFamily, Retained, RetentionError))
        {
            auto Kept = NativeImportReport(true, FString(), Asset->Package, Retained->GetPathName(), false, false,
                false, TEXT("existing unchanged source dependency retained"));
            Kept->SetStringField(TEXT("kind"), Asset->Kind);
            Kept->SetBoolField(TEXT("reference_only"), true);
            if (FamilyEvidence.IsValid())
            {
                FamilyEvidence->SetBoolField(TEXT("existing_dependency_recipe_recovered"), true);
                FamilyEvidence->SetStringField(TEXT("existing_dependency_recipe_md5"), RecoveredRecipeMd5);
                Kept->SetObjectField(TEXT("material_family_sources"), FamilyEvidence);
            }
            Replies->SetObjectField(Asset->Package, Kept);
            Out.Imported.Add(Asset->Package, Asset->Kind);
            Out.ReferenceOnly.AddUnique(Asset->Package);
            ImportedIdentities.Add(FName(*Asset->Package));
            Checkpoint();
            return !ReportWriteFailed && !FatalFailure;
        }
    }
    if (DeferredMICAdmission && !RecoveredWitness)
    {
        // Never retire an original when preparation cannot supply
        // proof. A failed witness preserves its existing custody.
        auto Kept = NativeImportReport(true, FString(), Asset->Package, FString(), false, false, false,
            TEXT("existing creator content preserved"));
        Kept->SetStringField(TEXT("kind"), Asset->Kind);
        Kept->SetStringField(TEXT("original_binding_witness_error"), WitnessProblem);
        if (FamilyEvidence.IsValid())
            Kept->SetObjectField(TEXT("material_family_sources"), FamilyEvidence);
        Replies->SetObjectField(Asset->Package, Kept);
        Warnings.AddUnique(Asset->Package + TEXT(" keeps its existing content: ") + WitnessProblem);
        return true;
    }
    bool Finished = false;
    const bool Continue = BeginReimport(Item, Finished);
    if (Finished)
    {
        const TSharedPtr<FJsonObject>* Kept = nullptr;
        if (FamilyEvidence.IsValid() && Replies->TryGetObjectField(Asset->Package, Kept))
            (*Kept)->SetObjectField(TEXT("material_family_sources"), FamilyEvidence);
        return Continue;
    }
    return {};
}

void FP4KadanImportRun::RecoverMaterial(FAssetImport& Item, FMaterialImport& Family)
{
    const TSharedPtr<FP4KadanSourceAsset>& Asset = Item.Asset;
    FString& FamilyProblem = Family.FamilyProblem;
    TUniquePtr<FP4KadanImportSourceScope>& MaterialComparisonSources = Item.MaterialComparisonSources;
    FP4MaterialFamilySourcesPtr& CompleteFamily = Family.CompleteFamily;
    TSharedPtr<FP4MaterialRecovery>& FamilyRecovery = Family.FamilyRecovery;
    TArray<TSharedPtr<FP4KadanSourceAsset>>& RecoverySources = Family.RecoverySources;
    FP4MaterialRecovery*& Recovery = Family.Recovery;
    TFunction<bool(FString&)>& CurrentFamily = Family.CurrentFamily;
    TSharedPtr<FJsonObject>& Reply = Item.Reply;
    bool& HasNativeResult = Item.HasNativeResult;
    bool& NativeValid = Item.NativeValid;
    FString& NativeError = Item.NativeError;
    bool& FamilySourceRefused = Family.FamilySourceRefused;
    bool& NativeFailureFatal = Item.NativeFailureFatal;
    bool& MayKeepGameIdentity = Item.MayKeepGameIdentity;
    TSharedPtr<FJsonObject>& FamilyEvidence = Family.FamilyEvidence;
    TSharedPtr<FP4MaterialUsedResources>& FinalUsed = Family.FinalUsed;
    FP4MaterialFamilyPreparedStagePtr& PreparedFamily = Family.PreparedFamily;
    FP4MaterialRecoveryResult Made;
    // A game master's shaders compile while the import goes on, as the editor compiles any material:
    // nothing the import makes needs them, and the editor tells about a graph that does not compile.
    if (RecoveryArchiveProblem.IsEmpty() && FamilyProblem.IsEmpty())
    {
        // Fresh imports need the same exact family bindings as deferred admission.
        // Keep this row-owned scope through Finish/save and rollback.
        if (!MaterialComparisonSources && CompleteFamily.IsValid() && FamilyRecovery)
            MaterialComparisonSources = MakeUnique<FP4KadanImportSourceScope>(RecoverySources);
        Made = Recovery->Import(Asset->Package, true, CurrentFamily, false);
    }
    else if (!FamilyProblem.IsEmpty())
    {
        Made.Kind = Asset->Kind;
        Made.Reason = FamilyProblem;
        Made.RefusalName = TEXT("the complete current material family sources are unavailable");
    }
    else
    {
        Made.Kind = Asset->Kind;
        Made.Reason = RecoveryArchiveProblem;
        Made.RefusalName = TEXT("the game's shader library is unavailable");
    }
    if (FamilyRecovery)
    {
        const FString Preview = FamilyRecovery->PreviewNote();
        if (!Preview.IsEmpty())
            Warnings.AddUnique(Preview);
    }
    Reply = P4ParseJsonObject(Made.Report);
    const TSharedPtr<FJsonObject> MaterialRecoveryReply = Reply;
    if (!Reply.IsValid())
        Reply = NativeImportReport(Made.bOk, Made.Reason, Asset->Package, FString(), false, false, false, FString());
    HasNativeResult = true;
    NativeValid = Made.bOk;
    NativeError = Made.Reason;
    // A game material that cannot be recovered, or whose recovered graph this editor's renderer cannot
    // compile, is kept for reference only from the game's file, so that the meshes and levels that use
    // it can still be made.
    FString Refusal = Made.Reason;
    if (!Made.bOk && Refusal.IsEmpty())
        Reply->TryGetStringField(TEXT("error"), Refusal);
    if (!Made.bOk && !FamilySourceRefused)
    {
        const auto Kept = P4ImportOriginalAssetNative(Asset->Header, Asset->Body, true, true);
        if (Kept.IsValid())
        {
            Out.ReferenceOnly.Add(Asset->Package);
            Warnings.Add(Asset->Package + TEXT(" was not recovered as an editable material (") + Refusal +
                TEXT("), so the game's material was kept for reference only and cannot go into a mod."));
            HasNativeResult = true;
            NativeValid = true;
            NativeError.Empty();
            Reply = NativeImportReport(true, FString(), Kept.Package, Kept.AssetPath, Kept.Created, Kept.Saved,
                Kept.FilledStandIn, Kept.State);
            Reply->SetBoolField(TEXT("reference_only"), true);
            Reply->SetStringField(TEXT("recovered_graph_error"), Refusal);
        }
        else
        {
            HasNativeResult = true;
            NativeValid = false;
            NativeFailureFatal = !AssetCanRemainInGame(Kept.Error);
            MayKeepGameIdentity = Kept.Error == EP4OriginalAssetImportError::DecodeFailed ||
                Kept.Error == EP4OriginalAssetImportError::SourceUnreadable ||
                Kept.Error == EP4OriginalAssetImportError::PropertiesUnreadable ||
                Kept.Error == EP4OriginalAssetImportError::TablesUnreadable;
            NativeError = Refusal + TEXT(". Keeping it for reference only failed too (") + Kept.ErrorDetail +
                TEXT(").");
        }
    }
    // Keep the actual renderer diagnosis even when a reference-only
    // asset replaces the failed recovered graph in the import report.
    if (Reply.IsValid() && MaterialRecoveryReply.IsValid())
    {
        const auto Details = MakeShared<FJsonObject>();
        for (const TCHAR* Key : {TEXT("compiled"), TEXT("compile_deferred"), TEXT("renderer_active"),
                 TEXT("compile_errors"), TEXT("error"), TEXT("refusal")})
            if (const auto* Value = MaterialRecoveryReply->Values.Find(Key))
                Details->SetField(Key, *Value);
        Reply->SetObjectField(TEXT("material_recovery"), Details);
    }
    if (Reply.IsValid() && FamilyEvidence.IsValid())
    {
        FamilyEvidence->SetBoolField(TEXT("source_custody_held"), !FamilySourceRefused);
        FamilyEvidence->SetNumberField(TEXT("archive_read_attempts"), Recovery->ShaderArchiveReadAttempts());
        FamilyEvidence->SetNumberField(TEXT("family_recovery_builds"), Recovery->FamilyRecoveryBuilds());
        FamilyEvidence->SetNumberField(TEXT("archive_digest_computes"), Recovery->ArchiveDigestComputes());
        FamilyEvidence->SetNumberField(TEXT("owner_graph_reuses"), Recovery->OwnerGraphReuses());
        if (FinalUsed)
            FamilyEvidence->SetNumberField(TEXT("parallel_owner_traces"), FinalUsed->ParallelOwnerTraces);
        FamilyEvidence->SetNumberField(
            TEXT("converted_packages"), PreparedFamily.IsValid() ? PreparedFamily->ConvertedPackages() : 0);
        Reply->SetObjectField(TEXT("material_family_sources"), FamilyEvidence);
    }
}

#include "P4KadanBlueprintPlans.h"
#include "P4KadanTestSeams.h"
#include "P4KadanImport.h"
#include "P4BlueprintImport.h"
#include "P4BoundedSourceRead.h"
THIRD_PARTY_INCLUDES_START
#include "P4MapKitCore/package_reader.h"
THIRD_PARTY_INCLUDES_END
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
// Blueprints of Cave000 planned in C++ from the game folder's files, with their parents, in the layout the
// Blueprint import reads. A package the folder does not hold is refused by name.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanBlueprintPlanNativeTest, "P4MapKit.Kadan.BlueprintPlanNative",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FP4KadanBlueprintPlanNativeTest::RunTest(const FString&)
{
    P4_REQUIRE_KADAN_TEST_SEAMS();
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    if (Root.IsEmpty())
    {
        AddError(TEXT("The Blueprint plan test needs P4_NATIVE_ROOT."));
        return false;
    }
    const FString Game = Root / TEXT("host/Saved/P4MapKit/FullNativeCave000/game");
    TArray<TSharedPtr<FP4KadanSourceAsset>> Scanned;
    TArray<FString> Problems;
    if (!TestTrue(TEXT("The game folder is read"), P4KadanScanGame(Game, Scanned, Problems)))
        return false;
    const FString Baby = TEXT("/Game/Carrot4/Placeables/Teki/GBaby"),
                  Base = TEXT("/Game/Carrot4/Placeables/Teki/Base/GTekiBase");
    const FString Terrain = TEXT("/Game/Carrot4/Terrain/Cave/Type/Soil/BlueprintActor/CMA_T02_DC_DirtPile01");
    const FString Absent = TEXT("/Game/__P4KadanPlannerTest/NoSuchBlueprint");
    const FString Folder = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("P4MapKit/Tests/BlueprintPlanNative"));
    FP4KadanBlueprintPlans Plans;
    FString Error;
    const bool Planned = P4KadanPlanBlueprints(Scanned, {Baby, Terrain, Absent}, Folder, Plans, Error);
    ON_SCOPE_EXIT
    {
        IFileManager::Get().DeleteDirectory(*Folder, false, true);
    };
    if (!TestTrue(TEXT("The planner runs: ") + Error, Planned))
        return false;
    for (const auto& Refusal : Plans.Refused)
        AddInfo(TEXT("Not planned. ") + Refusal.Key + TEXT(". ") + Refusal.Value);
    AddInfo(FString::Printf(TEXT("%d planned, %d refused, modules %s, not loaded %s."), Plans.Planned.Num(),
        Plans.Refused.Num(), *FString::Join(Plans.Modules, TEXT(", ")),
        *FString::Join(Plans.ModulesNotLoaded, TEXT(", "))));
    TestTrue(TEXT("A package the game folder does not hold is refused by name"), Plans.Refused.Contains(Absent));
    TestTrue(TEXT("missing source is a real source error, not an unsupported reconstruction"),
        Plans.SourceProblems.Contains(Absent) && !Plans.ReferenceOnlyReasons.Contains(Absent));
    for (const FString& Asked : {Baby, Terrain, Base})
        TestTrue(TEXT("Planned or refused with a reason: ") + Asked,
            Plans.Planned.Contains(Asked) || (Plans.Refused.Contains(Asked) && !Plans.Refused[Asked].IsEmpty()));
    TestTrue(TEXT("A Blueprint's parent is planned with it"),
        Plans.Planned.Contains(Baby) ? Plans.Planned.Contains(Base) : true);
    TestTrue(TEXT("The modules include the game's and the engine's"),
        Plans.Modules.Contains(TEXT("Carrot4")) && Plans.Modules.Contains(TEXT("Engine")));
    FString Text;
    TSharedPtr<FJsonObject> Index;
    FString Format;
    TestTrue(TEXT("The index is written in the import's format"),
        FFileHelper::LoadFileToString(Text, *(Folder / TEXT("index.json"))) &&
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Index) && Index.IsValid() &&
            Index->TryGetStringField(TEXT("format"), Format) && Format == TEXT("p4-blueprint-plans"));
    for (const FString& Package : Plans.Planned)
    {
        TSharedPtr<FJsonObject> Plan;
        FString Named;
        const bool Read = FFileHelper::LoadFileToString(
                              Text, *(Folder / TEXT("import") / Package.Mid(1) + TEXT(".json"))) &&
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Plan) && Plan.IsValid() &&
            Plan->TryGetStringField(TEXT("package"), Named);
        TestTrue(TEXT("The import plan names its package: ") + Package, Read && Named == Package);
        TestTrue(TEXT("The evidence part is written: ") + Package,
            IFileManager::Get().FileExists(*(Folder / TEXT("evidence") / Package.Mid(1) + TEXT(".json"))));
    }
    // Reuse one real source's bytes; a caller cannot relabel their identity and
    // then use an editable-planner exception to approve the foreign package.
    const auto* Original = Scanned.FindByPredicate(
        [&](const TSharedPtr<FP4KadanSourceAsset>& Source) { return Source->Package == Baby; });
    TSharedPtr<FP4KadanSourceAsset> Missing;
    if (Original)
    {
        auto Wrong = MakeShared<FP4KadanSourceAsset>(**Original);
        Wrong->Package = TEXT("/Game/__P4KadanPlannerTest/WrongSourceIdentity");
        FP4KadanBlueprintPlans Invalid;
        FString Why;
        P4KadanPlanBlueprints({Wrong}, {Wrong->Package}, Folder / TEXT("identity-check"), Invalid, Why);
        TestTrue(TEXT("exact inventory identity mismatch remains a source error"),
            Invalid.SourceProblems.Contains(Wrong->Package) && !Invalid.ReferenceOnlyReasons.Contains(Wrong->Package));
        Missing = MakeShared<FP4KadanSourceAsset>(**Original);
        Missing->Body = Folder / TEXT("absent-source.uexp");
    }
    // A fully resolved source with no scanned Blueprint parents needs no raw
    // original acquisition even when explicit selected tool paths are invalid.
    // These are the actual scanned source descriptors, never manufactured deps.
    TSharedPtr<FP4KadanSourceAsset> Resolved;
    for (const auto& Candidate : Scanned)
    {
        if (Candidate->Kind != TEXT("Blueprint") || Candidate->Package == Baby || Candidate->Package == Base ||
            !Plans.Planned.Contains(Candidate->Package))
            continue;
        TArray<uint8> H, B;
        FString Why;
        if (!P4ReadBoundedSourceFile(Candidate->Header, 64 * 1024 * 1024, H, Why) ||
            !P4ReadBoundedSourceFile(Candidate->Body, 64 * 1024 * 1024, B, Why))
            continue;
        const auto Inventory = P4MapKit::ReadPackage(std::vector<uint8>(H.GetData(), H.GetData() + H.Num()),
            std::vector<uint8>(B.GetData(), B.GetData() + B.Num()));
        if (!Inventory.Error.Code.empty())
            continue;
        bool NeedsOriginal = false, HasBlueprintParent = false;
        for (size_t I = 0; I < Inventory.Imports.size(); ++I)
        {
            const auto& Import = Inventory.Imports[I];
            std::string Path;
            NeedsOriginal |= Import.Name == "UnknownExport" && Inventory.Reference(-std::int32_t(I) - 1, Path) &&
                Path == "/Engine/UnknownPackage.UnknownExport";
            if (Import.Outer == 0)
                for (const auto& Other : Scanned)
                    if (Other->Package == FString(UTF8_TO_TCHAR(Import.Name.c_str())) &&
                        (Other->Kind == TEXT("Blueprint") || Other->Kind == TEXT("AnimBlueprint") ||
                            Other->Kind == TEXT("WidgetBlueprint")))
                        HasBlueprintParent = true;
        }
        if (!NeedsOriginal && !HasBlueprintParent)
        {
            Resolved = Candidate;
            break;
        }
    }
    if (!TestTrue(TEXT("Actual fixture includes a resolved source with native parent"), Resolved.IsValid()))
        return false;
    FP4KadanImportSettings UnusedTools;
    UnusedTools.GamePaks = Folder / TEXT("never-created-containers");
    UnusedTools.Retoc = Folder / TEXT("never-created-retoc.exe");
    UnusedTools.Repak = Folder / TEXT("never-created-repak.exe");
    FP4CurrentGameRegistryPtr UnusedReader;
    FP4KadanBlueprintPlans ResolvedPlan;
    FString ResolvedError;
    const auto HasIndexEntry = [&](const FString& PlanFolder, const FString& ExpectedPackage)
    {
        FString Saved;
        TSharedPtr<FJsonObject> SavedIndex;
        const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
        if (!FFileHelper::LoadFileToString(Saved, *(PlanFolder / TEXT("index.json"))) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Saved), SavedIndex) || !SavedIndex.IsValid() ||
            !SavedIndex->TryGetArrayField(TEXT("order"), Entries) || !Entries)
            return false;
        for (const auto& Entry : *Entries)
        {
            const auto Row = Entry->AsObject();
            FString Found;
            if (Row.IsValid() && Row->TryGetStringField(TEXT("package"), Found) && Found == ExpectedPackage)
                return true;
        }
        return false;
    };
    TestTrue(TEXT("resolved package planning never acquires a reader or dumps an original"),
        P4KadanPlanBlueprints(Scanned, {Resolved->Package}, Folder / TEXT("resolved-no-original"), ResolvedPlan,
            ResolvedError, UnusedTools, UnusedReader) &&
            !UnusedReader && ResolvedPlan.OriginalIdentityPackages == 0 &&
            ResolvedPlan.Planned.Contains(Resolved->Package) && HasIndexEntry(ResolvedPlan.Folder, Resolved->Package) &&
            !ResolvedPlan.SourceProblems.Contains(Resolved->Package) &&
            !IFileManager::Get().DirectoryExists(*UnusedTools.GamePaks));
    if (!TestTrue(TEXT("Actual broken source control exists and is distinct from the independent plan"),
            Missing.IsValid() && Missing->Package != Resolved->Package))
        return false;
    auto MixedSources = Scanned;
    MixedSources.RemoveAll(
        [&](const TSharedPtr<FP4KadanSourceAsset>& Row) { return Row->Package == Missing->Package; });
    MixedSources.Add(Missing);
    FP4KadanBlueprintPlans Mixed;
    FP4CurrentGameRegistryPtr NoReader;
    FP4KadanImportSettings NoTools;
    FString MixedError;
    TestTrue(
        TEXT(
            "initial unreadable source refuses only that asset while an independent actual Blueprint is still planned"),
        P4KadanPlanBlueprints(MixedSources, {Resolved->Package, Missing->Package}, Folder / TEXT("per-asset-source"),
            Mixed, MixedError, NoTools, NoReader) &&
            Mixed.SourceProblems.Contains(Missing->Package) && Mixed.Refused.Contains(Missing->Package) &&
            !Mixed.ReferenceOnlyReasons.Contains(Missing->Package) && !Mixed.Planned.Contains(Missing->Package) &&
            !HasIndexEntry(Mixed.Folder, Missing->Package) && Mixed.Planned.Contains(Resolved->Package) &&
            HasIndexEntry(Mixed.Folder, Resolved->Package) && !Mixed.SourceProblems.Contains(Resolved->Package) &&
            !NoReader);
    const auto* Parent = Scanned.FindByPredicate(
        [&](const TSharedPtr<FP4KadanSourceAsset>& Row) { return Row->Package == Base; });
    if (!TestTrue(TEXT("Actual source parent exists for loader refusal control"), Parent != nullptr))
        return false;
    auto BrokenParent = MakeShared<FP4KadanSourceAsset>(**Parent);
    BrokenParent->Body = Folder / TEXT("absent-parent-source.uexp");
    auto ParentSources = Scanned;
    ParentSources.RemoveAll([&](const TSharedPtr<FP4KadanSourceAsset>& Row) { return Row->Package == Base; });
    ParentSources.Add(BrokenParent);
    FP4KadanBlueprintPlans ParentFailure;
    FString ParentError;
    FP4KadanBlueprintWrappedSourceControl Wrap(Baby, Base);
    TestTrue(
        TEXT(
            "real broken dependency read wrapped as semantic exception stays source-attributed and independent class survives"),
        P4KadanPlanBlueprints(ParentSources, {Resolved->Package, Baby}, Folder / TEXT("per-asset-parent-source"),
            ParentFailure, ParentError, NoTools, NoReader) &&
            ParentFailure.SourceProblems.Contains(Base) && ParentFailure.SourceProblems.Contains(Baby) &&
            !ParentFailure.ReferenceOnlyReasons.Contains(Baby) && !ParentFailure.Planned.Contains(Base) &&
            !ParentFailure.Planned.Contains(Baby) && !HasIndexEntry(ParentFailure.Folder, Baby) &&
            ParentFailure.Planned.Contains(Resolved->Package) &&
            HasIndexEntry(ParentFailure.Folder, Resolved->Package) && Wrap.WrappedFailures() == 1 && !NoReader);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanBlueprintGameReferenceAdmission,
    "P4MapKit.Kadan.BlueprintGameReferenceAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanBlueprintGameReferenceAdmission::RunTest(const FString&)
{
    FP4KadanBlueprintPlans Plans;
    Plans.Refused.Add(TEXT("Unsupported"), TEXT("unsupported-construct"));
    Plans.ReferenceOnlyReasons.Add(TEXT("Unsupported"), TEXT("unsupported-construct"));
    const TMap<FString, TArray<FString>> Required = {
        {TEXT("Child"), {TEXT("Unsupported")}}, {TEXT("Grandchild"), {TEXT("Child")}}, {TEXT("Independent"), {}}};
    const TSet<FString> Excluded = P4KadanBlueprintReferenceClosure(Required, Plans);
    TestTrue(TEXT("unsupported Blueprint dependencies are transitively excluded from editable plans"),
        Excluded.Num() == 2 && Excluded.Contains(TEXT("Child")) && Excluded.Contains(TEXT("Grandchild")));
    TestTrue(TEXT("valid children retain game-reference reasons"),
        Plans.ReferenceOnlyReasons.Contains(TEXT("Child")) && Plans.ReferenceOnlyReasons.Contains(TEXT("Grandchild")));
    TestFalse(TEXT("independent recovered Blueprint remains editable"), Excluded.Contains(TEXT("Independent")));
    Plans.Refused.Add(TEXT("Corrupt"), TEXT("truncated-source-header"));
    Plans.SourceProblems.Add(TEXT("Corrupt"), TEXT("truncated-source-header"));
    P4KadanBlueprintReferenceClosure({{TEXT("Mixed"), {TEXT("Unsupported"), TEXT("Corrupt")}}}, Plans);
    TestTrue(TEXT("source corruption outranks an unsupported dependency"),
        Plans.SourceProblems.Contains(TEXT("Mixed")) && !Plans.ReferenceOnlyReasons.Contains(TEXT("Mixed")));
    auto Asset = MakeShared<FP4KadanSourceAsset>();
    Asset->Package = TEXT("/Game/P4GameReferenceAdmission/Unsupported");
    Asset->Kind = TEXT("Blueprint");
    // Native Persist finishes ordinary per-asset failures with phase=failed.
    // That label alone must never turn reconstruction into infrastructure.
    FP4BlueprintImportRunResult Batch;
    Batch.Phase = TEXT("failed");
    Batch.Errors.Add(TEXT("unsupported-reconstructed-source-property"));
    const TArray<TSharedPtr<FP4KadanSourceAsset>> Selected = {Asset};
    const TSet<FName> Verified = {FName(*Asset->Package)};
    const auto Strict = P4KadanBlueprintBatchResult(Batch, Selected);
    TestFalse(TEXT("standalone reconstruction remains strict"), Strict.Ok);
    TestEqual(TEXT("strict failure remains named"), Strict.NotImported.Num(), 1);
    const auto Kept = P4KadanBlueprintBatchResult(Batch, Selected, Verified);
    TestTrue(TEXT("verified source identity can remain in the game after an ordinary failed-phase batch"), Kept.Ok);
    TestEqual(TEXT("no reconstructed asset is claimed"), Kept.Imported.Num(), 0);
    TestEqual(TEXT("unsupported reconstruction is not a map failure"), Kept.NotImported.Num(), 0);
    TestEqual(TEXT("original identity is retained"), Kept.ReferenceOnly.Num(), 1);
    TestTrue(TEXT("per-asset unsupported reason is retained"),
        Kept.GameReferenceReasons.FindRef(Asset->Package).Contains(TEXT("unsupported-reconstructed-source-property")));
    TestTrue(TEXT("the caller gets an explicit warning"), Kept.Warnings.Num() == 1 && Kept.Problems.Num() == 0);
    Batch.PackageErrors.Add(Asset->Package, TEXT("this asset's exact source member refused"));
    const auto Precise = P4KadanBlueprintBatchResult(Batch, Selected, Verified);
    TestEqual(TEXT("game references retain their own refusal instead of the whole batch"),
        Precise.GameReferenceReasons.FindRef(Asset->Package),
        FString(TEXT("this asset's exact source member refused")));
    const auto Unverified = P4KadanBlueprintBatchResult(Batch, Selected, {FName(TEXT("/Game/AnotherSource"))});
    TestFalse(TEXT("unverified/missing source cannot become a game-reference fallback"), Unverified.Ok);
    TestEqual(TEXT("unverified source is still a real error"), Unverified.NotImported.Num(), 1);
    Batch.bCancelled = true;
    const auto Cancelled = P4KadanBlueprintBatchResult(Batch, Selected, Verified);
    TestFalse(TEXT("cancellation cannot be downgraded"), Cancelled.Ok);
    TestEqual(TEXT("cancelled batch is not accepted as a fallback"), Cancelled.ReferenceOnly.Num(), 0);
    Batch.bCancelled = false;
    Batch.bInfrastructureFailure = true;
    const auto Infrastructure = P4KadanBlueprintBatchResult(Batch, Selected, Verified);
    TestFalse(TEXT("result/read/infrastructure failure cannot be downgraded"), Infrastructure.Ok);
    TestEqual(TEXT("infrastructure failure is not accepted as a fallback"), Infrastructure.ReferenceOnly.Num(), 0);
    return !HasAnyErrors();
}
#endif

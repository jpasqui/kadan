#include "P4KadanImport.h"
#include "P4KadanSourceCatalog.h"
#include "P4KadanTestSeams.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if WITH_DEV_AUTOMATION_TESTS
// Times one map import the way the Kadan panel runs it from the game's Paks folder: the catalog
// source (this project's Saved/Kadan/ConvertedOriginals), resume on, nothing prepared beforehand.
// Opt-in through P4_KADAN_BENCH_MAP. The timings and the import report are kept in Saved/Kadan/Bench.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanImportBenchTest, "P4MapKit.Kadan.ImportBench",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4KadanImportBenchTest::RunTest(const FString&)
{
    const FString Map = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_BENCH_MAP"));
    const FString Paks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    const FString Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    const FString Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    FString Label = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_BENCH_LABEL"));
    if (Map.IsEmpty() || Paks.IsEmpty() || Retoc.IsEmpty() || Repak.IsEmpty())
    {
        AddWarning(TEXT(
            "P4MapKit test not run. Set P4_KADAN_BENCH_MAP, P4_NATIVE_ORIGINAL_CONTAINERS, P4_NATIVE_RETOC and P4_NATIVE_REPAK."));
        return true;
    }
    if (Label.IsEmpty())
        Label = TEXT("run");
    const double Started = FPlatformTime::Seconds();
    FP4KadanSourceCatalog Catalog;
    FString Error;
    if (!TestTrue(
            TEXT("The game's Paks catalog reads. ") + Error, P4KadanReadSourceCatalog(Paks, Retoc, Catalog, Error)))
        return false;
    FP4KadanImportSettings Settings = Catalog.ImportSettings;
    Settings.Repak = Repak;
    // Only for comparison with the earlier fallback-policy measurements. Normal benchmark
    // runs retain the product's strict native policy, and the report records this choice.
    Settings.AllowGameBackedLevels = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_BENCH_ALLOW_GAME_BACKED")) ==
        TEXT("1");
    // P4_KADAN_SCENE_RACE_UNGUARDED=1 stops the scene teardown guard (P4SceneTeardown.h) for this import:
    // the control run for the race it prevents.
    const FP4KadanSceneTeardownTestSeams* Teardown = P4KadanSceneTeardownSeams();
    const bool Unguarded = Teardown &&
        FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_SCENE_RACE_UNGUARDED")) == TEXT("1");
    if (Unguarded)
    {
        Teardown->Stop();
        AddInfo(TEXT("The scene teardown guard is stopped for this import."));
    }
    ON_SCOPE_EXIT
    {
        if (Unguarded)
            Teardown->Start();
    };
    const double CatalogRead = FPlatformTime::Seconds();
    const FP4KadanImportPlan Plan = P4KadanPlanImport(Settings, TSet<FString>{Map}, true);
    const double Planned = FPlatformTime::Seconds();
    const FP4KadanImportResult Result = P4KadanRunImport(Settings, Plan, true);
    const double Imported = FPlatformTime::Seconds();
    auto Bench = MakeShared<FJsonObject>();
    Bench->SetStringField(TEXT("map"), Map);
    Bench->SetStringField(TEXT("label"), Label);
    Bench->SetBoolField(TEXT("allow_game_backed_levels"), Settings.AllowGameBackedLevels);
    Bench->SetBoolField(TEXT("catalog_index_from_cache"), Catalog.IndexFromCache);
    Bench->SetNumberField(TEXT("catalog_seconds"), CatalogRead - Started);
    Bench->SetNumberField(TEXT("plan_seconds"), Planned - CatalogRead);
    Bench->SetNumberField(TEXT("import_seconds"), Imported - Planned);
    Bench->SetNumberField(TEXT("total_seconds"), Imported - Started);
    Bench->SetNumberField(TEXT("plan_assets"), Plan.Assets.Num());
    Bench->SetNumberField(TEXT("plan_scanned"), Plan.Scanned.Num());
    Bench->SetNumberField(TEXT("plan_roots"), Plan.Roots);
    Bench->SetNumberField(TEXT("plan_dependencies"), Plan.Dependencies);
    Bench->SetNumberField(TEXT("plan_already_present"), Plan.AlreadyPresent);
    Bench->SetNumberField(TEXT("plan_problems"), Plan.Problems.Num());
    Bench->SetBoolField(TEXT("ok"), Result.Ok);
    Bench->SetBoolField(TEXT("cancelled"), Result.Cancelled);
    Bench->SetNumberField(TEXT("imported"), Result.Imported.Num());
    Bench->SetNumberField(TEXT("not_imported"), Result.NotImported.Num());
    Bench->SetNumberField(TEXT("reference_only"), Result.ReferenceOnly.Num());
    Bench->SetNumberField(TEXT("problems"), Result.Problems.Num());
    Bench->SetNumberField(TEXT("warnings"), Result.Warnings.Num());
    Bench->SetNumberField(TEXT("level_objects"), Result.LevelObjects);
    FString Text;
    FJsonSerializer::Serialize(Bench, TJsonWriterFactory<>::Create(&Text));
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("Kadan/Bench");
    IFileManager::Get().MakeDirectory(*Folder, true);
    TestTrue(TEXT("The bench timings are saved"),
        FFileHelper::SaveStringToFile(
            Text, *(Folder / (Label + TEXT("-bench.json"))), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    TestTrue(TEXT("The import report is kept"),
        FFileHelper::SaveStringToFile(Result.Report, *(Folder / (Label + TEXT("-import-result.json"))),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    AddInfo(FString::Printf(
        TEXT("Kadan import bench %s: catalog %.1f s, plan %.1f s, import %.1f s, %d imported, %d not imported, ok %d"),
        *Label, CatalogRead - Started, Planned - CatalogRead, Imported - Planned, Result.Imported.Num(),
        Result.NotImported.Num(), Result.Ok ? 1 : 0));
    TestEqual(TEXT("The timed plan has no problems"), Plan.Problems.Num(), 0);
    TestFalse(TEXT("The timed import was not cancelled"), Result.Cancelled);
    TestTrue(TEXT("The timed import completed successfully"), Result.Ok);
    return !HasAnyErrors();
}
#endif

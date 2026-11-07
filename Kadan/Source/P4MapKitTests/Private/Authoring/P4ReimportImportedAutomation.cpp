#if P4_MAPKIT_TESTS
#include "P4KadanImport.h"
#include "P4KadanProjectSetup.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"

// What the Import page does for "Select imported" then "Reimport": every game package the project already
// has, planned and imported again without reusing existing imports, best effort (AllowGameBackedLevels),
// as after updating Kadan. P4_NATIVE_REIMPORT_GAME names the game folder and P4_NATIVE_REIMPORT_MAP a map
// whose closure gives the catalog of game packages. The report goes to Saved/P4MapKit/Reimport/report.json.
// A diagnostic run by name, not a check.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ReimportImportedTest, "P4MapKit.Kadan.ReimportImported",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ReimportImportedTest::RunTest(const FString&)
{
    const FString Game = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REIMPORT_GAME"));
    const FString Map = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REIMPORT_MAP"));
    if (Game.IsEmpty() || Map.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. Set P4_NATIVE_REIMPORT_GAME and P4_NATIVE_REIMPORT_MAP."));
        return true;
    }
    const FString NativeRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    const FString Repo = FPaths::GetPath(FPaths::GetPath(NativeRoot));
    FP4KadanImportSettings Settings;
    Settings.GameFolder = Game;
    Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    Settings.Retoc = Repo / TEXT("private/retoc/retoc.exe");
    Settings.Repak = Repo / TEXT("private/repak/repak.exe");
    Settings.AllowGameBackedLevels = true;
    FString Why;
    if (!TestTrue(TEXT("the project is set up for Kadan: ") + Why, P4KadanProjectReady(Why)))
        return false;
    const FP4KadanImportPlan Catalog = P4KadanPlanImport(Settings, TSet<FString>{Map}, true);
    TSet<FString> Packages, Imported;
    for (const auto& Asset : Catalog.Scanned)
        if (Asset.IsValid())
            Packages.Add(Asset->Package);
    if (!TestTrue(TEXT("previously imported packages are found"),
            P4KadanFindImportedPackages(Packages, Imported) && Imported.Num() > 0))
        return false;
    AddInfo(
        FString::Printf(TEXT("%d game packages in the catalog, %d already imported"), Packages.Num(), Imported.Num()));
    const FP4KadanImportPlan Plan = P4KadanPlanImport(Settings, Imported, false);
    for (const FString& Problem : Plan.Problems)
        AddInfo(TEXT("Plan problem: ") + Problem);
    const FP4KadanImportResult Result = P4KadanRunImport(Settings, Plan, false);
    const FString Out = FPaths::ProjectSavedDir() / TEXT("P4MapKit/Reimport/report.json");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Out), true);
    FFileHelper::SaveStringToFile(Result.Report, *Out, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    AddInfo(FString::Printf(TEXT("reimport ok %d, %d imported, %d not imported, %d problems"), Result.Ok,
        Result.Imported.Num(), Result.NotImported.Num(), Result.Problems.Num()));
    for (int32 At = 0; At < FMath::Min(Result.Problems.Num(), 40); ++At)
        AddInfo(TEXT("Problem: ") + Result.Problems[At]);
    return true;
}
#endif

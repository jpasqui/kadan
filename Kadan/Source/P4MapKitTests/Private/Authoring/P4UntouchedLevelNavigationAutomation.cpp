#include "P4ExportDriver.h"
#include "P4ExportContainer.h"
#include "P4ExportSelection.h"
#include "P4ExportNavigation.h"
#include "P4KadanImport.h"
#include "P4KadanSaveMod.h"
#include "P4LevelChanges.h"
#include "P4OriginalRecord.h"

#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4UntouchedLevelNavigation,
    "P4MapKit.Export.UntouchedLevelNavigation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FP4UntouchedLevelNavigation::RunTest(const FString&)
{
    const FString Map = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Cave000_F00_P");
    if (!TestFalse(TEXT("The owned project starts without this imported map"),
            FPackageName::DoesPackageExist(Map)))
    {
        return false;
    }
    const FString GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_ROUND_C_GAME_PAKS"));
    const FString Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    const FString Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    if (!TestTrue(TEXT("The real game and conversion tools are available"),
            !GamePaks.IsEmpty() && !Retoc.IsEmpty() && !Repak.IsEmpty()))
    {
        return false;
    }
    FP4KadanImportSettings Settings;
    Settings.GameFolder = FPaths::ProjectSavedDir() / TEXT("Kadan/ConvertedOriginals");
    Settings.GamePaks = GamePaks;
    Settings.Retoc = Retoc;
    Settings.Repak = Repak;
    Settings.AllowGameBackedLevels = true;
    const FP4KadanImportResult Import = P4KadanImportMap(Settings, Map, true);
    if (!TestTrue(TEXT("The normal importer constructs the untouched map"), Import.Ok))
    {
        AddError(Import.Problems.Num() ? Import.Problems[0] : TEXT("The import had no diagnostic."));
        return false;
    }
    TArray<FP4ExportRoot> Roots;
    int32 Checked = 0;
    for (const auto& Asset : Import.Imported)
    {
        if (Asset.Value != TEXT("World"))
        {
            continue;
        }
        const FString Package = Asset.Key;
        UWorld* World = LoadObject<UWorld>(nullptr,
            *(Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package)));
        if (!TestNotNull(TEXT("An imported original level loads"), World))
        {
            return false;
        }
        const FP4LevelChangesResult Changes = P4AnalyzeOriginalLevelChanges(World);
        if (!TestTrue(TEXT("The freshly imported level has no edit. ") + Package,
                Changes.IsValid() && Changes.Unchanged))
        {
            AddError(Changes.ErrorDetail);
            return false;
        }
        FString Cause;
        TestFalse(TEXT("The untouched level does not request navigation. ") + Package,
            P4EditChangesNavigation(Changes, Cause));
        const FP4OriginalDiagnostic Diagnostic = P4DiagnoseOriginalAsset(World);
        TestEqual(TEXT("The original-asset comparison agrees that the level is unchanged. ") + Package,
            Diagnostic.Comparison, EP4OriginalComparison::Equal);
        FP4ExportRoot Root;
        Root.Kind = EP4ExportRootKind::Package;
        Root.Path = Package;
        Roots.Add(Root);
        ++Checked;
    }
    if (!TestTrue(TEXT("At least one original level was checked"), Checked > 0))
    {
        return false;
    }
    FP4ModDescriptor Mod;
    Mod.Id = TEXT("native.untouched.nav");
    Mod.Name = TEXT("Untouched navigation test");
    Mod.Version = TEXT("1.0.0");
    FString Error;
    if (!TestTrue(TEXT("The test mod identity is saved"),
            P4WriteModDescriptor(FPaths::ProjectDir(), Mod, nullptr, Error)))
    {
        AddError(Error);
        return false;
    }
    FP4ExportModRequest Request;
    Request.Mod = Mod;
    Request.Roots = Roots;
    Request.MapPackage = Map;
    Request.GamePaksDirectory = GamePaks;
    Request.ExtractedGameDirectory = Settings.GameFolder;
    Request.ProjectDirectory = FPaths::ProjectDir();
    Request.OutputDirectory = FPaths::ProjectSavedDir() / TEXT("P4MapKit/UntouchedNavigation");
    Request.WorkDirectory = FPaths::ProjectIntermediateDir() / TEXT("P4MapKit/UntouchedNavigation");
    Request.Tools.Retoc = Retoc;
    Request.Tools.Repak = Repak;
    FP4ExportGameInventory Inventory;
    if (!TestTrue(TEXT("The game inventory is readable for the untouched review"),
            P4ReadExportGameInventory(Request.Tools, GamePaks,
                Request.WorkDirectory, Inventory, Error)))
    {
        AddError(Error);
        return false;
    }
    FP4ExportSelectionPreview Preview;
    const TMap<FName, FString> NoOtherModClaims;
    if (!TestTrue(TEXT("The untouched selection can be reviewed"),
            P4PreviewExportSelection(Mod, Roots, Inventory, NoOtherModClaims,
                Preview, Error)))
    {
        AddError(Error);
        return false;
    }
    for (const FP4ExportSelectionRow& Row : Preview.Rows)
    {
        if (Import.Imported.FindRef(Row.Package) == TEXT("World"))
        {
            TestEqual(TEXT("An untouched imported level has no GameEdit row. ") + Row.Package,
                Row.Disposition, EP4ExportDisposition::UnchangedGame);
        }
    }
    FP4ExportModResult Exported;
    if (!TestTrue(TEXT("The untouched imported map exports without a navigation build"),
            P4ExportMod(Request, Exported, Error)))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("The unchanged map publishes"), Exported.Published);
    TestEqual(TEXT("No phantom original packages ship"), Exported.Packages.Num(), 0);
    const bool HasNavigation = Exported.Files.ContainsByPredicate([](const FString& File)
    {
        return File.EndsWith(TEXT(".nav"), ESearchCase::IgnoreCase);
    });
    TestFalse(TEXT("No navigation file was rebuilt"), HasNavigation);
    AddInfo(FString::Printf(TEXT("Checked %d freshly imported levels."), Checked));
    return !HasAnyErrors();
}
#endif

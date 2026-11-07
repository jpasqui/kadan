#if P4_MAPKIT_TESTS
#include "P4ExportDriverTestShared.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4Area001ImportOptions, "P4MapKit.Export.Area001ImportAndPlacementOptions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4Area001ImportOptions::RunTest(const FString&)
{
    using namespace P4ExportDriverTest;
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    FP4ExportModRequest Request;
    FP4ModNames Names;
    if (!P4ExportDriverTest::Request(*this, TEXT("native.area001.test"), TEXT("Native Area001 Test"), Request, Names))
        return false;
    const FString Map = TEXT("/Game/Carrot4/Maps/Main/Area/Area001/Area001_P");
    FImported Owned(TEXT("Area001ImportAndPlacementOptions"));
    if (!TestTrue(TEXT("The Area001 cleanup list is ready"), Owned.Ready) ||
        !TestFalse(TEXT("The project has no Area001 before the test"), FPackageName::DoesPackageExist(Map)))
        return false;
    FP4KadanImportSettings Settings;
    // The selected root is read from the original containers through the normal import planner.
    Settings.GameFolder = Root / TEXT("host/Saved/P4MapKit/FullNativeCave000/game");
    Settings.GamePaks = Request.GamePaksDirectory;
    Settings.Retoc = Request.Tools.Retoc;
    Settings.Repak = Request.Tools.Repak;
    const FP4KadanImportResult Imported = P4KadanImportMap(Settings, Map, false);
    if (!KeepImport(*this, TEXT("Area001ImportAndPlacementOptions"), Imported))
        return false;
    for (const FString& Problem : Imported.Problems)
        AddInfo(TEXT("Not imported. ") + Problem);
    if (!TestTrue(TEXT("The persistent Area001 map is imported"),
            Imported.Imported.Contains(Map) && FPackageName::DoesPackageExist(Map)))
        return false;
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Map);
    if (!TestNotNull(TEXT("Area001 opens through the normal editor map loader"), World))
        return false;
    TestTrue(TEXT("Area001 has its streaming levels"), World->GetStreamingLevels().Num() > 0);
    // The dependency closure is the normal importer's reused OriginalPackages source cache.
    const FString Sources = FPaths::ProjectSavedDir() / TEXT("Kadan/OriginalPackages");
    const FString Lists = P4GamePlacementFolder(Sources, Map);
    TArray<FP4GamePlacementRow> Rows;
    FString Error;
    if (!TestTrue(TEXT("The actual Area001 placement sources can be read. ") + Error,
            !Lists.IsEmpty() && P4ReadGamePlacementLists(Lists, Rows, Error) && Rows.Num() > 0))
        return false;
    int32 Loaded = 0, Checked = 0;
    for (ULevel* Level : World->GetLevels())
    {
        if (!Level)
            continue;
        ++Loaded;
        UWorld* LevelWorld = Cast<UWorld>(Level->GetOuter());
        TestTrue(TEXT("The imported original level is unchanged. ") + Level->GetOutermost()->GetName(),
            LevelWorld && P4AnalyzeOriginalLevelChanges(LevelWorld).Unchanged);
        for (AActor* Actor : Level->Actors)
        {
            const FP4GamePlacementRow* Row = Actor ? P4GamePlacementRowOf(Actor, Rows) : nullptr;
            if (!Row)
                continue;
            ++Checked;
            const TArray<FString> Differences = P4PlacementOptionDifferences(Actor, *Row);
            for (const FString& Difference : Differences)
                AddInfo(Actor->GetPathName() + TEXT(" ") + Difference);
            TestEqual(TEXT("The original actor holds its imported placement options. ") + Actor->GetPathName(),
                Differences.Num(), 0);
        }
    }
    AddInfo(FString::Printf(
        TEXT("Area001 opened with %d loaded levels and %d original placement actors checked."), Loaded, Checked));
    TestTrue(TEXT("Area001 loads its original sublevels"), Loaded > 1);
    TestTrue(TEXT("Area001 restores original placement actors"), Checked > 0);
    return !HasAnyErrors();
}

// The sweep configures exactly one map before starting each independent editor process.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4UnchangedMapExport, "P4MapKit.Export.UnchangedOriginalMap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4UnchangedMapExport::RunTest(const FString&)
{
    using namespace P4ExportDriverTest;
    FImported Owned(TEXT("UnchangedOriginalMap"));
    if (!TestTrue(TEXT("The clean import cleanup list is ready"), Owned.Ready))
        return false;
    FP4ExportModRequest Request;
    FP4ModNames Names;
    FString Error;
    if (!P4ExportDriverTest::Request(*this, TEXT("native.unchanged.test"), TEXT("Unchanged Map Test"), Request, Names))
        return false;
    FP4KadanImportSettings Settings;
    Settings.GameFolder = Request.ExtractedGameDirectory;
    Settings.GamePaks = Request.GamePaksDirectory;
    Settings.Retoc = Request.Tools.Retoc;
    Settings.Repak = Request.Tools.Repak;
    const auto Import = P4KadanImportMap(Settings, Cave000, false);
    if (!KeepImport(*this, TEXT("UnchangedOriginalMap"), Import) ||
        !TestTrue(TEXT("The normal importer supplies the original map"), Import.Ok && !Import.Cancelled))
        return false;
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Cave000);
    if (!TestNotNull(TEXT("The normal editor loader opens the original map"), World))
        return false;
    Request.MapPackage = Cave000;
    for (ULevel* Level : World->GetLevels())
        if (Level)
            Request.Roots.Add({EP4ExportRootKind::Package, Level->GetOutermost()->GetName()});
    FP4ExportGameInventory Inventory;
    FP4ExportSelectionPreview Preview;
    if (!TestTrue(TEXT("The real game inventory is read. ") + Error,
            P4ReadExportGameInventory(
                Request.Tools, Request.GamePaksDirectory, Request.WorkDirectory, Inventory, Error)) ||
        !TestTrue(TEXT("The panel previews the original levels. ") + Error,
            P4PreviewExportSelection(Request.Mod, Request.Roots, Inventory, {}, Preview, Error)))
        return false;
    const auto Review = P4ReviewCreatorSelection(Request.Mod, Preview, World);
    TestTrue(TEXT("The same panel readiness check admits the unchanged original map"),
        Review.CanExport() && Review.ManifestOnly);
    TestEqual(TEXT("Readiness does not pretend any packages ship"), Review.Ships, 0);
    TestFalse(TEXT("Original levels require their original map to be open"),
        P4ReviewCreatorSelection(Request.Mod, Preview, nullptr).CanExport());
    FP4ExportSelectionPreview Foreign = Preview;
    Foreign.UnchangedLevelCandidates.Add(TEXT("/Game/Carrot4/Maps/OutsideSelectedMap"));
    TestFalse(
        TEXT("An out-of-map candidate is refused"), P4ReviewCreatorSelection(Request.Mod, Foreign, World).CanExport());
    FP4ExportModResult Exported;
    if (!TestTrue(
            TEXT("The normal exporter publishes the unchanged map. ") + Error, P4ExportMod(Request, Exported, Error)))
        return false;
    TestTrue(TEXT("The unchanged export is published"), Exported.Published);
    TestEqual(TEXT("No package is shipped"), Exported.Packages.Num(), 0);
    TestEqual(TEXT("No added level is shipped"), Exported.Levels.Num(), 0);
    const FString Relative = TEXT("romfs/tsuchi/mods/native.unchanged.test/mod.json");
    TestTrue(
        TEXT("The declared file list contains exactly the descriptor"), Exported.Files == TArray<FString>{Relative});
    TArray<FString> ActualFiles;
    IFileManager::Get().FindFilesRecursive(ActualFiles, *Exported.Directory, TEXT("*"), true, false);
    TestEqual(TEXT("The actual output contains exactly one file"), ActualFiles.Num(), 1);
    FString Before;
    if (!TestTrue(TEXT("Read the actual exported descriptor"),
            FFileHelper::LoadFileToString(Before, *(Exported.Directory / Relative))))
        return false;
    const auto Manifest = ReadJson(Exported.Directory / Relative);
    if (!TestTrue(TEXT("The actual descriptor is JSON"), Manifest.IsValid()))
        return false;
    TestEqual(TEXT("Descriptor retains the persisted ID"), Manifest->GetStringField(TEXT("id")), Request.Mod.Id);
    TestEqual(TEXT("Descriptor retains the persisted name"), Manifest->GetStringField(TEXT("name")), Request.Mod.Name);
    TestEqual(TEXT("Descriptor retains the persisted version"), Manifest->GetStringField(TEXT("version")),
        Request.Mod.Version);
    TestFalse(TEXT("No map edit is invented"), Manifest->HasField(TEXT("maps")));
    TestFalse(TEXT("No table edit is invented"), Manifest->HasField(TEXT("tables")));
    TestFalse(TEXT("No new content is invented"), Manifest->HasField(TEXT("content")));
    Request.Roots.Reset();
    FP4ExportModResult Empty;
    TestFalse(TEXT("A genuinely empty selection is still refused"), P4ExportMod(Request, Empty, Error));
    FString After;
    TestTrue(TEXT("The refusal preserves the prior actual output"),
        FFileHelper::LoadFileToString(After, *(Exported.Directory / Relative)) && Before == After);
    return !HasAnyErrors();
}
#endif

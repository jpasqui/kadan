#if P4_MAPKIT_TESTS
#include "P4ExportDriverTestShared.h"

// The fields of a placement row that the game's serializers take from an object's components (its sound emitter's
// place, its nav links, its spline's points, its trigger boxes) are what Kadan's fillers take from the imported
// object's components, for every gameplay object of the caves below that has a row in the game's lists.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PlacementBlobsFromActorsTest, "P4MapKit.Editor.PlacementBlobsFromActors",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PlacementBlobsFromActorsTest::RunTest(const FString&)
{
    using namespace P4ExportDriverTest;
    // Caves chosen so that every class with a filled field has rows in them.
    // Between them they hold rows of every class with a filled field: fences, geysers, conveyors and sprinklers,
    // moving floors, pushed boxes, route splines and narrow spaces, portals and nav mesh triggers, trigger doors,
    // sound triggers, dirt mounds and crystal crabs, demo areas, the flexible bridge and the sidecut wall.
    static const TCHAR* const Maps[] = {
        TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Cave000_F00_P"),
        TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave002/Cave002_F01/Cave002_F01_P"),
        TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave005/Cave005_F00/Cave005_F00_P"),
        TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave005/Cave005_F01/Cave005_F01_P"),
        TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave006/Cave006_F02/Cave006_F02_P"),
        TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave008/Cave008_F00/Cave008_F00_P"),
        TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave011/Cave011_F00/Cave011_F00_P"),
        TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave011/Cave011_F01/Cave011_F01_P"),
    };
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    if (Root.IsEmpty() || FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS")).IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. PlacementBlobsFromActors was not exercised. Set P4_NATIVE_ROOT and ")
                       TEXT("P4_NATIVE_ORIGINAL_CONTAINERS."));
        return true;
    }
    FP4ExportModRequest Request;
    FP4ModNames Names;
    if (!P4ExportDriverTest::Request(*this, TEXT("native.fill.test"), TEXT("Native Fill Test"), Request, Names))
        return false;
    const FString Game = Root / TEXT("host/Saved/P4MapKit/FullNativeCave000/game");
    TUniquePtr<FImported> Imported;
    TMap<FString, int32> Compared;
    int32 Total = 0;
    for (const TCHAR* Map : Maps)
    {
        if (!FPackageName::DoesPackageExist(Map))
        {
            if (!Imported)
            {
                Imported = MakeUnique<FImported>(TEXT("PlacementBlobsFromActors"));
                if (!TestTrue(TEXT("the test can keep its import cleanup list"), Imported->Ready))
                    return false;
            }
            FP4KadanImportSettings Settings;
            Settings.GameFolder = Game;
            Settings.GamePaks = Request.GamePaksDirectory;
            Settings.Retoc = Request.Tools.Retoc;
            Settings.Repak = Request.Tools.Repak;
            const FP4KadanImportResult Import = P4KadanImportMap(Settings, Map, true);
            for (const FString& Problem : Import.Problems)
                AddInfo(TEXT("Not imported. ") + Problem);
            if (!KeepImport(*this, TEXT("PlacementBlobsFromActors"), Import))
                return false;
        }
        UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Map);
        if (!TestNotNull(FString(TEXT("the map opens: ")) + Map, World))
            return false;
        TArray<FP4GamePlacementRow> Rows;
        FString Error;
        const FString Lists = P4MapPlacementFolder(Game, Map);
        if (!TestTrue(FString(TEXT("the map's placement lists are read: ")) + Error,
                !Lists.IsEmpty() && P4ReadGamePlacementLists(Lists, Rows, Error) && Rows.Num() > 0))
            return false;
        for (ULevel* Level : World->GetLevels())
            for (AActor* Actor : Level ? Level->Actors : TArray<AActor*>())
            {
                const FP4GamePlacementRow* Row = Actor && P4IsGameplayActor(Actor) ? P4GamePlacementRowOf(Actor, Rows) : nullptr;
                if (!Row)
                    continue;
                TArray<FString> Differences;
                const int32 Here = P4PlacementFilledFields(Actor, *Row, Differences);
                if (!Here)
                    continue;
                Compared.FindOrAdd(Actor->GetClass()->GetName()) += Here;
                Total += Here;
                for (const FString& Line : Differences)
                    AddError(FString::Printf(TEXT("%s (%s): %s"), *Actor->GetName(), *Actor->GetClass()->GetName(), *Line));
            }
    }
    Compared.KeySort(TLess<FString>());
    for (const auto& Pair : Compared)
        AddInfo(FString::Printf(TEXT("%s: %d filled fields compared"), *Pair.Key, Pair.Value));
    return TestTrue(TEXT("filled fields were compared"), Total > 0);
}
#endif

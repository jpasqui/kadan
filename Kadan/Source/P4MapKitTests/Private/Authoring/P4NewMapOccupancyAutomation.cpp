#if P4_MAPKIT_TESTS
#include "P4NewMapOccupancy.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SelectedSaveOccupancyTest, "P4MapKit.Export.SelectedSaveOccupancy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4SelectedSaveOccupancyTest::RunTest(const FString&)
{
    const FString Root = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("P4MapKit/SelectedSaveCensusTest"));
    if (IFileManager::Get().DirectoryExists(*Root) && !IFileManager::Get().DeleteDirectory(*Root, false, true))
    {
        AddError(TEXT("Cannot reset owned selected-save fixture"));
        return false;
    }
    if (!IFileManager::Get().MakeDirectory(*(Root / TEXT("tsuchi/tsuchi.content")), true))
        return false;
    const FString First = Root / TEXT("tsuchi/tsuchi.content/new_maps.json");
    const FString Second = Root / TEXT("tsuchi/tsuchi.content/new_maps.2.json");
    const FString Legacy = Root / TEXT("tsuchi/new_maps.json");
    auto Save = [&](const FString& File, const FString& Text)
    { return FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM); };
    FP4NewMapOccupancy Empty;
    FString Error;
    TestTrue(
        TEXT("Explicit existing selected save without records is empty"), P4ReadNewMapOccupancy(Root, Empty, Error));
    TestEqual(TEXT("Empty selected save has zero retained allocations"), Empty.Occupied().Num(), 0);
    TestTrue(TEXT("Unchanged selected snapshot can be revalidated"), P4RevalidateNewMapOccupancy(Empty, Error));
    if (!Save(Legacy,
            TEXT("{\"areas\":{\"5\":{\"map\":\"proof.course:course\",\"used\":false}},\"unknown\":\"retained\"}")))
        return false;
    TestFalse(
        TEXT("A newly present legacy record invalidates empty snapshot"), P4RevalidateNewMapOccupancy(Empty, Error));
    FP4NewMapOccupancy Retained;
    TestTrue(TEXT("Legacy source is read only when canonical copies are absent"),
        P4ReadNewMapOccupancy(Root, Retained, Error));
    TestEqual(TEXT("Unused retained allocation is still occupied for authoring"), Retained.Occupied().Num(), 1);
    if (Retained.Occupied().Num())
        TestEqual(
            TEXT("Exact retained map identity"), Retained.Occupied()[0].Map, FString(TEXT("proof.course:course")));
    if (!Save(First, TEXT("{\"counter\":3,\"areas\":{\"7\":{\"map\":\"proof.floor:floor\",\"used\":true}}}")) ||
        !Save(Second, TEXT("{\"counter\":3,\"areas\":{\"8\":{\"map\":\"proof.other:other\",\"used\":true}}}")))
        return false;
    TestFalse(
        TEXT("Equal-counter conflicting allocations refuse context"), P4ReadNewMapOccupancy(Root, Retained, Error));
    TestEqual(TEXT("Failed census preserves prior retained claims"), Retained.Occupied().Num(), 1);
    if (!Save(Second, TEXT("broken")))
        return false;
    FP4NewMapOccupancy Recovered;
    TestTrue(
        TEXT("Qualified valid canonical copy recovers damaged sibling"), P4ReadNewMapOccupancy(Root, Recovered, Error));
    TestEqual(TEXT("Recovery selects actual generation"), Recovered.Counter(), uint64(3));
    TestTrue(TEXT("Both exact record snapshots revalidate"), P4RevalidateNewMapOccupancy(Recovered, Error));
    if (!Save(Second, TEXT("broken but changed")))
        return false;
    TestFalse(TEXT("Even changed damaged sibling invalidates publication snapshot"),
        P4RevalidateNewMapOccupancy(Recovered, Error));
    if (!Save(First, TEXT("broken")))
        return false;
    TestFalse(TEXT("Damaged canonical pair never falls back to legacy/free allocation"),
        P4ReadNewMapOccupancy(Root, Retained, Error));
    TestFalse(TEXT("Nonexistent selected directory cannot prove an empty save"),
        P4ReadNewMapOccupancy(Root / TEXT("not-created"), Retained, Error));
    return true;
}
#endif

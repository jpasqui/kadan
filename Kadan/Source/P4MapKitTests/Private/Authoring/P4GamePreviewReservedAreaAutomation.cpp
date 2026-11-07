#if P4_MAPKIT_TESTS
#include "P4GamePreviewTemplate.h"
#include "P4GamePreviewTestGame.h"
#include "P4KadanContainerIndex.h"
#include "Misc/AutomationTest.h"

// A new level's area comes from the game's reservation (world and scene names) instead of Kadan's own
// list of free areas 5, 7, 8 and 9. Over the old list the reservation's names give the same area names;
// over the game's files those areas have no world or scene of the game's. Area 0 has none either, so
// the game's files alone do not give the list: Tsuchi's table, behind the reservation, does.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GamePreviewReservedAreaTest, "P4MapKit.Game.PreviewReservedArea",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GamePreviewReservedAreaTest::RunTest(const FString&)
{
    const uint32 OldAreas[] = {5, 7, 8, 9};
    for (const uint32 Area : OldAreas)
    {
        const FString Name = FString::Printf(TEXT("Area%03u"), Area);
        TestEqual(TEXT("the reservation's names give the old area name"),
            P4GamePreviewNameToken(Name + TEXT("_P"), TEXT("SD_") + Name), Name);
    }
    int32 Admitted = 0;
    for (uint32 Area = 0; Area < 1000; ++Area)
    {
        const FString Name = FString::Printf(TEXT("Area%03u"), Area);
        Admitted += P4GamePreviewNameToken(Name + TEXT("_P"), TEXT("SD_") + Name) == Name ? 1 : 0;
    }
    AddInfo(
        FString::Printf(TEXT("the reservation rule names %d of 1000 area numbers; the old rule admitted 4"), Admitted));

    using namespace P4GamePreviewTestGame;
    FP4KadanImportSettings Settings;
    FString Error;
    FP4KadanContainerIndex Index;
    TArray<FP4KadanContainerPackage> Packages;
    if (!GameSettings(Settings, Error) || !P4KadanReadContainerIndex(Settings, Index, Error) ||
        !Index.Enumerate(Packages, Error))
    {
        AddError(Error);
        return false;
    }
    TSet<FString> Leaves;
    for (const FP4KadanContainerPackage& Entry : Packages)
        Leaves.Add(FPackageName::GetShortName(Entry.Package));
    TArray<FString> Free;
    for (uint32 Area = 0; Area < 10; ++Area)
    {
        const FString Name = FString::Printf(TEXT("Area%03u"), Area);
        if (!Leaves.Contains(Name + TEXT("_P")) && !Leaves.Contains(TEXT("SD_") + Name))
            Free.Add(FString::FromInt(int32(Area)));
    }
    AddInfo(TEXT("areas below 10 without a game world or scene: ") + FString::Join(Free, TEXT(",")));
    for (const uint32 Area : OldAreas)
        TestTrue(FString::Printf(TEXT("old free area %u has no game world or scene"), Area),
            Free.Contains(FString::FromInt(int32(Area))));
    return true;
}
#endif

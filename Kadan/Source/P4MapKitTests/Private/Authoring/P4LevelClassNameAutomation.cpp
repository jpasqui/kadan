#if P4_MAPKIT_TESTS
#include "P4KadanTestSeams.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4LevelClassNameComparisonTest, "P4MapKit.Levels.ClassNameComparison",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4LevelClassNameComparisonTest::RunTest(const FString&)
{
    const auto* Seams = P4KadanTestSeams();
    if (!TestTrue(TEXT("Actual level comparison is accessible"), Seams && Seams->LevelClassLine != nullptr))
        return false;
    const FString Source = TEXT("/Game/Carrot4/Placeables/Objects/Otakara/GOtaGaraGara.GOtaGaragara_C");
    const FString Cook = TEXT("/Game/Carrot4/Placeables/Objects/Otakara/GOtaGaraGara.GOtaGaraGara_C");
    TestTrue(TEXT("Class imports differing only in FName display case describe the same actor"),
             Seams->LevelClassLine(Source, true).Equals(Seams->LevelClassLine(Cook, true), ESearchCase::CaseSensitive));
    TestFalse(
        TEXT("Exact source state retains the original spelling"),
        Seams->LevelClassLine(Source, false).Equals(Seams->LevelClassLine(Cook, false), ESearchCase::CaseSensitive));
    TestTrue(TEXT("A different actor class never compares equal"),
             !Seams->LevelClassLine(Source, true)
                  .Equals(Seams->LevelClassLine(TEXT("/Game/AnotherClass.AnotherClass_C"), true),
                          ESearchCase::CaseSensitive));
    return !HasAnyErrors();
}
#endif

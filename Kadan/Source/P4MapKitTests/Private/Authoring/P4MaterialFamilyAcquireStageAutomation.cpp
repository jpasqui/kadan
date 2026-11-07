#if WITH_DEV_AUTOMATION_TESTS
#include "P4MaterialFamilyAcquireStage.h"
#include "P4KadanTestSeams.h"
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialFamilyStageCustodyTest, "P4MapKit.Kadan.MaterialFamilyStageCustody",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialFamilyStageCustodyTest::RunTest(const FString&)
{
    P4_REQUIRE_KADAN_TEST_SEAMS();
#if !PLATFORM_WINDOWS
    AddWarning(TEXT("P4MapKit test not run. ") TEXT("Material stage custody requires the Windows immutable-file API"));
    return true;
#else
    TArray<FString> Errors;
    const bool Okay = P4MaterialFamilyStageControlsForTest(Errors);
    for (const FString& Error : Errors)
        AddError(Error);
    return Okay && !HasAnyErrors();
#endif
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "P4MaterialCancelPoll.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialCancelPollTest, "P4MapKit.Kadan.MaterialCancelPoll",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialCancelPollTest::RunTest(const FString&)
{
    P4MaterialCancel::FPostCallbackPoll Gate;
    double Time = 10.0;
    int32 Draws = 0, ClockReads = 0;
    bool Cancelled = false, Clicked = false;
    const auto Clock = [&]()
    {
        ++ClockReads;
        return Time;
    };
    const auto UI = [&]()
    {
        ++Draws;
        Time += 0.6;
        return Clicked;
    };
    TestFalse(TEXT("First poll draws and observes no cancellation"), Gate.ShouldCancel(Cancelled, Clock, UI));
    TestEqual(TEXT("First poll is not delayed"), Draws, 1);
    for (int32 I = 0; I != 1000; ++I)
        TestFalse(TEXT("Repeated work polls do not redraw immediately after slow UI"),
            Gate.ShouldCancel(Cancelled, Clock, UI));
    TestEqual(TEXT("Slow UI duration does not consume the next work interval"), Draws, 1);
    Time += 0.199;
    TestFalse(TEXT("Before the post-callback deadline no UI poll occurs"), Gate.ShouldCancel(Cancelled, Clock, UI));
    TestEqual(TEXT("Deadline is measured from UI return"), Draws, 1);
    Time = 10.6 + 0.2;
    TestFalse(TEXT("At the deadline UI is polled again"), Gate.ShouldCancel(Cancelled, Clock, UI));
    TestEqual(TEXT("Later work remains responsive"), Draws, 2);
    Clicked = true;
    Time += 0.2;
    TestTrue(TEXT("Later UI cancellation is observed"), Gate.ShouldCancel(Cancelled, Clock, UI));
    TestTrue(TEXT("Observed cancellation updates the caller flag"), Cancelled);
    const int32 ReadsAfterCancel = ClockReads, DrawsAfterCancel = Draws;
    Cancelled = false;
    TestTrue(TEXT("UI cancellation remains sticky if caller clears its flag"), Gate.ShouldCancel(Cancelled, Clock, UI));
    TestEqual(TEXT("Sticky cancellation does not read clock"), ClockReads, ReadsAfterCancel);
    TestEqual(TEXT("Sticky cancellation does not draw"), Draws, DrawsAfterCancel);

    P4MaterialCancel::FPostCallbackPoll OtherRun;
    Cancelled = false;
    Clicked = false;
    TestFalse(TEXT("New run has independent state"), OtherRun.ShouldCancel(Cancelled, Clock, UI));
    Cancelled = true;
    const int32 BeforeExternalReads = ClockReads, BeforeExternalDraws = Draws;
    TestTrue(TEXT("External cancellation bypasses deadline immediately"), OtherRun.ShouldCancel(Cancelled, Clock, UI));
    TestEqual(TEXT("External cancellation does not read clock"), ClockReads, BeforeExternalReads);
    TestEqual(TEXT("External cancellation does not draw"), Draws, BeforeExternalDraws);
    Cancelled = false;
    TestTrue(TEXT("External cancellation is sticky"), OtherRun.ShouldCancel(Cancelled, Clock, UI));

    P4MaterialCancel::FPostCallbackPoll ReentrantCancel;
    Cancelled = false;
    TestTrue(TEXT("External cancellation raised while UI runs is observed on return"),
        ReentrantCancel.ShouldCancel(Cancelled, Clock,
            [&]()
            {
                Cancelled = true;
                return false;
            }));
    return true;
}
#endif

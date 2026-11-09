#include "Misc/AutomationTest.h"
#include "P4ExportProgress.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKadanExportProgress, "P4MapKit.Kadan.ExportProgress",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FKadanExportProgress::RunTest(const FString&)
{
    int32 OuterReports = 0, InnerReports = 0, Pulses = 0;
    TestFalse(TEXT("Ordinary calls have no progress observer"), P4HasExportProgressObserver());
    FString Why;
    {
        FP4ExportProgressScope Outer(
            [&](EP4ExportStage Stage, const FString& Subject, bool CanCancel) {
                ++OuterReports;
                TestEqual(TEXT("The current asset is reported without changing its path"), Subject,
                          FString(TEXT("/Game/Mods/Test/Asset")));
                TestEqual(TEXT("Publication disables cancellation"), CanCancel,
                          Stage != EP4ExportStage::Publishing);
                return true;
            },
            [&]() {
                ++Pulses;
            });
        TestTrue(TEXT("Cooking can report its actual asset"),
                 P4ReportExportStage(EP4ExportStage::Cooking, TEXT("/Game/Mods/Test/Asset"), Why));
        TestTrue(TEXT("Lighting can use the active progress surface"), P4HasExportProgressObserver());
        P4PulseExportProgress();
        {
            FP4ExportProgressScope Inner(
                [&](EP4ExportStage, const FString&, bool) {
                    ++InnerReports;
                    return false;
                },
                nullptr);
            TestFalse(TEXT("Cancellation stops work at a stage boundary"),
                      P4ReportExportStage(EP4ExportStage::Packing, TEXT("test.mod"), Why));
            TestTrue(TEXT("Cancellation has a typed status for the panel"),
                P4ExportWasCancelled());
            TestFalse(TEXT("Cancellation retains a readable explanation"), Why.IsEmpty());
        }
        TestFalse(TEXT("The outer observer did not cancel"), P4ExportWasCancelled());
        TestTrue(TEXT("Between steps an export without a cancel goes on"), P4ContinueExport(Why));
        {
            // The creator clicked Cancel while a step ran: the next check between steps stops the export.
            bool Clicked = false;
            FP4ExportProgressScope Clicking(
                [&](EP4ExportStage, const FString&, bool) { return !Clicked; }, nullptr, [&]() { return Clicked; });
            TestFalse(TEXT("No cancel is asked for yet"), P4ExportCancelRequested());
            TestTrue(TEXT("The export goes on before the click"), P4ContinueExport(Why));
            Clicked = true;
            TestTrue(TEXT("A running container tool sees the cancel"), P4ExportCancelRequested());
            TestFalse(TEXT("The next check between steps stops the export"), P4ContinueExport(Why));
            TestTrue(TEXT("The stop counts as a cancel"), P4ExportWasCancelled());
            TestEqual(TEXT("The stop says it was canceled"), Why, FString(P4ExportCanceledMessage()));
        }
        TestFalse(TEXT("The outer observer is not canceled by the inner one"), P4ExportCancelRequested());
        TestTrue(TEXT("The outer observer is restored"),
                 P4ReportExportStage(EP4ExportStage::Publishing, TEXT("/Game/Mods/Test/Asset"), Why));
    }
    TestTrue(TEXT("Ordinary exports need no UI observer"),
             P4ReportExportStage(EP4ExportStage::Review, TEXT("test.mod"), Why));
    TestFalse(TEXT("The observer is removed after its scope"), P4HasExportProgressObserver());
    TestEqual(TEXT("Outer observer receives its two real calls"), OuterReports, 2);
    TestEqual(TEXT("Nested observer receives only its own call"), InnerReports, 1);
    // One from the subprocess pulse, one from the check between steps that repaints the window.
    TestEqual(TEXT("Subprocess pulses reach the active observer"), Pulses, 2);
    return true;
}
#endif

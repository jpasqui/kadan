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
        TestTrue(TEXT("The outer observer is restored"),
                 P4ReportExportStage(EP4ExportStage::Publishing, TEXT("/Game/Mods/Test/Asset"), Why));
    }
    TestTrue(TEXT("Ordinary exports need no UI observer"),
             P4ReportExportStage(EP4ExportStage::Review, TEXT("test.mod"), Why));
    TestFalse(TEXT("The observer is removed after its scope"), P4HasExportProgressObserver());
    TestEqual(TEXT("Outer observer receives its two real calls"), OuterReports, 2);
    TestEqual(TEXT("Nested observer receives only its own call"), InnerReports, 1);
    TestEqual(TEXT("Subprocess pulses reach the active observer"), Pulses, 1);
    return true;
}
#endif

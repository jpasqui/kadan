#if P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopedSlowTask.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "P4KadanExportProgress.h"

// An export step that is all Unreal's own work (loading packages, compiling shaders, saving) reports no stage of
// Kadan's. The progress window still repaints during it, through the progress Unreal reports, so its clock moves and
// Cancel can be clicked.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ExportProgressRepaintTest, "P4MapKit.Kadan.ExportProgressRepaints",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ExportProgressRepaintTest::RunTest(const FString&)
{
    if (!FSlateApplication::IsInitialized())
    {
        AddInfo(TEXT("No Slate application in this run, so there is no window to repaint."));
        return true;
    }
    FFeedbackContext* const Before = GWarn;
    double SlateBefore = 0, SlateAfter = 0;
    bool Swapped = false;
    P4KadanProgressOperation(
        [&]()
        {
            Swapped = GWarn != Before;
            SlateBefore = FSlateApplication::Get().GetCurrentTime();
            // Unreal's own long work, as LoadPackage and the shader compiler report it.
            FScopedSlowTask Work(8.f);
            for (int32 Frame = 0; Frame < 8; ++Frame)
            {
                Work.EnterProgressFrame(1.f);
                FPlatformProcess::Sleep(0.15f);
            }
            SlateAfter = FSlateApplication::Get().GetCurrentTime();
        });
    TestTrue(TEXT("The export's feedback context is in place while it runs"), Swapped);
    TestTrue(TEXT("The editor's feedback context is back afterwards"), GWarn == Before);
    TestTrue(TEXT("Slate ticked during Unreal's own work"), SlateAfter > SlateBefore);
    return !HasAnyErrors();
}
#endif

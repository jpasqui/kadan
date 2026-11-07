#include "P4ExportProgress.h"

namespace
{
thread_local FP4ExportProgressScope* Current = nullptr;
}

FP4ExportProgressScope::FP4ExportProgressScope(FReport InReport, TFunction<void()> InPulse)
    : Report(MoveTemp(InReport)), Pulse(MoveTemp(InPulse)), Previous(Current)
{
    Current = this;
}

FP4ExportProgressScope::~FP4ExportProgressScope()
{
    Current = Previous;
}

bool P4ReportExportStage(EP4ExportStage Stage, const FString& Subject, FString& Error)
{
    if (!Current || Current->ReportStage(Stage, Subject, Stage != EP4ExportStage::Publishing))
        return true;
    Error =
        TEXT("The export was canceled before the next stage. The previous published export is unchanged.");
    return false;
}

void P4PulseExportProgress()
{
    if (Current)
        Current->Tick();
}

bool P4HasExportProgressObserver()
{
    return Current != nullptr;
}

bool P4ExportWasCancelled()
{
    return Current && Current->WasCancelled();
}

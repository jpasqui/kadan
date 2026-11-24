#include "P4ExportProgress.h"

namespace
{
thread_local FP4ExportProgressScope* Current = nullptr;
}

FP4ExportProgressScope::FP4ExportProgressScope(
    FReport InReport, TFunction<void()> InPulse, TFunction<bool()> InCancelRequested)
    : Report(MoveTemp(InReport)), Pulse(MoveTemp(InPulse)), CancelQuery(MoveTemp(InCancelRequested)), Previous(Current)
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
    Error = P4ExportCanceledMessage();
    return false;
}

const TCHAR* P4ExportCanceledMessage()
{
    return TEXT("The export was canceled. The previous published export is unchanged.");
}

bool P4ExportCancelRequested()
{
    return Current && Current->CancelRequested();
}

bool P4ContinueExport(FString& Error)
{
    if (!Current)
        return true;
    Current->Tick();
    if (!Current->CancelRequested())
        return true;
    Current->MarkCancelled();
    Error = P4ExportCanceledMessage();
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

#pragma once
#include "CoreMinimal.h"

enum class EP4ExportStage : uint8
{
    Review,
    Cooking,
    Lighting,
    Navigation,
    Packing,
    Checking,
    Publishing,
    // Compiling the shaders of the mod's materials, between Review and the cook.
    Materials
};

// One synchronous export owns its observer. Nested scopes restore the previous observer.
class KADAN_API FP4ExportProgressScope
{
  public:
    using FReport = TFunction<bool(EP4ExportStage, const FString&, bool)>;
    // CancelRequested, when given, says whether the creator asked to cancel since the last stage, so an export can
    // stop between its steps and stop a container tool it is waiting on.
    FP4ExportProgressScope(FReport InReport, TFunction<void()> InPulse, TFunction<bool()> InCancelRequested = nullptr);
    ~FP4ExportProgressScope();
    FP4ExportProgressScope(const FP4ExportProgressScope&) = delete;
    FP4ExportProgressScope& operator=(const FP4ExportProgressScope&) = delete;
    bool ReportStage(EP4ExportStage Stage, const FString& Subject, bool CanCancel)
    {
        if (!Report || Report(Stage, Subject, CanCancel))
        {
            return true;
        }
        Cancelled = true;
        return false;
    }
    bool WasCancelled() const { return Cancelled; }
    bool CancelRequested() const { return Cancelled || (CancelQuery && CancelQuery()); }
    void MarkCancelled() { Cancelled = true; }
    void Tick()
    {
        if (Pulse)
            Pulse();
    }

  private:
    FReport Report;
    TFunction<void()> Pulse;
    TFunction<bool()> CancelQuery;
    FP4ExportProgressScope* Previous = nullptr;
    bool Cancelled = false;
};

// Cancellation is honored before entering a stage. Publication is not interruptible.
KADAN_API bool P4ReportExportStage(EP4ExportStage Stage, const FString& Subject, FString& Error);
KADAN_API void P4PulseExportProgress();

KADAN_API bool P4HasExportProgressObserver();
KADAN_API bool P4ExportWasCancelled();
// Whether the creator asked to cancel. A container tool the export waits on is stopped when this turns true.
KADAN_API bool P4ExportCancelRequested();
// Between two steps of an export: repaints the progress window and, when the creator asked to cancel, fails with
// the cancellation's explanation. Publication does not call it.
KADAN_API bool P4ContinueExport(FString& Error);
KADAN_API const TCHAR* P4ExportCanceledMessage();

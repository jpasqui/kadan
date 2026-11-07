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
    Publishing
};

// One synchronous export owns its observer. Nested scopes restore the previous observer.
class KADAN_API FP4ExportProgressScope
{
  public:
    using FReport = TFunction<bool(EP4ExportStage, const FString&, bool)>;
    FP4ExportProgressScope(FReport InReport, TFunction<void()> InPulse);
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
    void Tick()
    {
        if (Pulse)
            Pulse();
    }

  private:
    FReport Report;
    TFunction<void()> Pulse;
    FP4ExportProgressScope* Previous = nullptr;
    bool Cancelled = false;
};

// Cancellation is honored before entering a stage. Publication is not interruptible.
KADAN_API bool P4ReportExportStage(EP4ExportStage Stage, const FString& Subject, FString& Error);
KADAN_API void P4PulseExportProgress();

KADAN_API bool P4HasExportProgressObserver();
KADAN_API bool P4ExportWasCancelled();

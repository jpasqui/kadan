#include "P4ExportPublication.h"
#include "HAL/FileManager.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

bool P4PublishPreparedExport(const FString& Prepared, const FString& Target, bool HadTarget,
                             TArray<FString>& Told, FString& Error)
{
    const auto Fail = [&Error](const FString& Why) {
        Error = Why;
        return false;
    };
    const FString Previous = Target + TEXT(".previous");
    if (IFileManager::Get().DirectoryExists(*Previous) || IFileManager::Get().FileExists(*Previous))
        return Fail(TEXT("A previous export transaction needs recovery before another publication"));
#if PLATFORM_WINDOWS
    const auto MoveFailure = [](const TCHAR* Operation, DWORD Code, const FString& Source,
                                const FString& Destination) {
        TCHAR Detail[512] = {};
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, Code, 0, Detail,
                       uint32(sizeof(Detail) / sizeof(Detail[0])), nullptr);
        FString Reason(Detail);
        Reason.TrimStartAndEndInline();
        if (Reason.IsEmpty())
            Reason = TEXT("System message unavailable");
        // Paths belong only in the local diagnostic, never the exported mod.
        UE_LOG(LogTemp, Warning, TEXT("Kadan export %s: Windows error %u: %s. Source: %s. Destination: %s."),
               Operation, uint32(Code), *Reason, *Source, *Destination);
        return FString::Printf(TEXT("%s (Windows error %u: %s)."), Operation, uint32(Code), *Reason);
    };
    if (HadTarget && !MoveFileExW(*Target, *Previous, 0))
    {
        const DWORD Code = GetLastError();
        return Fail(
            MoveFailure(TEXT("Cannot retain previous export for publication"), Code, Target, Previous));
    }
    if (!MoveFileExW(*Prepared, *Target, 0))
    {
        const DWORD PublishCode = GetLastError();
        const FString PublishFailure =
            MoveFailure(TEXT("Cannot publish the prepared export"), PublishCode, Prepared, Target);
        if (HadTarget && !MoveFileExW(*Previous, *Target, 0))
        {
            const DWORD RollbackCode = GetLastError();
            return Fail(
                PublishFailure + TEXT(" ") +
                MoveFailure(
                    TEXT("Previous export could not be restored and remains at the .previous recovery path"),
                    RollbackCode, Previous, Target));
        }
        return Fail(PublishFailure);
    }
    if (HadTarget && !IFileManager::Get().DeleteDirectory(*Previous, false, true))
        Told.Add(TEXT("The export was published and the previous output is still in its .previous folder."));
#endif
    return true;
}

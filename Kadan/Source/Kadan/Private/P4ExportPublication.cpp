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
    if (IFileManager::Get().FileExists(*Previous))
        return Fail(TEXT("A file is in the way of the export: ") + Previous + TEXT(". Move or delete it, then export again."));
    if (IFileManager::Get().DirectoryExists(*Previous))
    {
        // An earlier export was published but could not remove the copy it replaced, often because the game had
        // its files open. That copy is the old export, so it goes now.
        if (!HadTarget)
            return Fail(TEXT("An earlier export of this mod stopped while replacing ") + Target + TEXT(", and its "
                "previous copy is in ") + Previous + TEXT(". Rename that folder back, or delete it, then export again."));
        if (!IFileManager::Get().DeleteDirectory(*Previous, false, true))
            return Fail(TEXT("Kadan could not remove the old copy of this mod's earlier export, ") + Previous +
                TEXT(". Close the game if it is running, delete that folder, then export again."));
        Told.Add(TEXT("Removed the old copy an earlier export left in ") + Previous + TEXT("."));
    }
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
        Told.Add(TEXT("The export was published. Its old copy is still in ") + Previous +
            TEXT(", probably because the game has its files open; the next export removes it."));
#endif
    return true;
}

TUniquePtr<FArchive> P4HoldLockFile(const FString& Path)
{
    TUniquePtr<FArchive> Held(IFileManager::Get().CreateFileWriter(*Path, FILEWRITE_NoReplaceExisting));
    // A held lock is open without sharing, so Windows refuses to delete it; only a stale one goes.
    if (!Held && IFileManager::Get().FileExists(*Path) && IFileManager::Get().Delete(*Path, false, false, true))
    {
        UE_LOG(LogTemp, Display, TEXT("Kadan: took over the lock %s, which an earlier session left behind."), *Path);
        Held.Reset(IFileManager::Get().CreateFileWriter(*Path, FILEWRITE_NoReplaceExisting));
    }
    return Held;
}

#include "P4ExportReadbackContext.h"
#include "P4ExportProcess.h"
#include "P4NativeHash.h"
#include "P4GameBuild.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
bool NoLinks(const FString& Path, FString& Error)
{
#if PLATFORM_WINDOWS
    FString At = FPaths::ConvertRelativePathToFull(Path);
    while (!At.IsEmpty())
    {
        const DWORD Attributes = GetFileAttributesW(*At);
        if (Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        {
            Error = TEXT("Packed readback paths cannot contain filesystem links");
            return false;
        }
        const FString Parent = FPaths::GetPath(At);
        if (Parent == At)
            break;
        At = Parent;
    }
    return true;
#else
    Error = TEXT("Packed readback requires the supported Windows hard-link boundary");
    return false;
#endif
}
struct FInput
{
    FString Source, Link, Digest;
    int64 Size = 0, Maximum = 0;
    void* Held = nullptr;
};
bool Hash(FInput& Input, FString& Error, bool Compare)
{
    int64 Size = 0;
    FString Digest;
    if (!NoLinks(Input.Source, Error) || !P4HashFile(Input.Source, Input.Maximum, Size, Digest) || Size <= 0)
    {
        if (Error.IsEmpty())
            Error = TEXT("Packed readback input is unavailable or exceeds its bound");
        return false;
    }
    if (Compare && (Size != Input.Size || !P4ExactNativeString(Digest, Input.Digest)))
    {
        Error = TEXT("Packed container or global script metadata changed during readback");
        return false;
    }
    if (!Compare)
    {
        Input.Size = Size;
        Input.Digest = MoveTemp(Digest);
    }
    return true;
}
} //namespace

bool P4ExtractExportReadback(const FP4ExportContainerTools& Tools, const FString& Utoc, const FString& GamePaks,
    const FString& Work, const FString& Destination, const FString& Filter, FString& Error)
{
    TArray<FString> Filters;
    if (!Filter.IsEmpty())
        Filters.Add(Filter);
    return P4ExtractExportReadback(Tools, Utoc, GamePaks, Work, Destination, Filters, Error);
}

bool P4ExtractExportReadback(const FP4ExportContainerTools& Tools, const FString& Utoc, const FString& GamePaks,
    const FString& Work, const FString& Destination, const TArray<FString>& Filters, FString& Error)
{
#if !PLATFORM_WINDOWS
    Error = TEXT("Packed readback requires the supported Windows hard-link boundary");
    return false;
#else
    if (Work.IsEmpty() || Destination.IsEmpty() || FPaths::GetExtension(Utoc) != TEXT("utoc") ||
        FPaths::GetBaseFilename(Utoc).Equals(TEXT("global"), ESearchCase::IgnoreCase))
    {
        Error = TEXT("Packed readback requires a mod container and private scratch");
        return false;
    }
    const FString Root = FPaths::ConvertRelativePathToFull(Work),
                  Output = FPaths::ConvertRelativePathToFull(Destination);
    const FString Containers = FPaths::ConvertRelativePathToFull(GamePaks),
                  Packed = FPaths::ConvertRelativePathToFull(Utoc);
    if (FPaths::IsSamePath(Root, Containers) || FPaths::IsUnderDirectory(Root, Containers) ||
        FPaths::IsUnderDirectory(Containers, Root) || FPaths::IsSamePath(Output, Containers) ||
        FPaths::IsUnderDirectory(Output, Containers) || FPaths::IsUnderDirectory(Containers, Output) ||
        FPaths::IsSamePath(Output, FPaths::GetPath(Packed)) ||
        FPaths::IsUnderDirectory(Output, FPaths::GetPath(Packed)) ||
        FPaths::IsUnderDirectory(FPaths::GetPath(Packed), Output))
    {
        Error = TEXT("Readback scratch must be independent of the original game and packed inputs");
        return false;
    }
    if (!NoLinks(Root, Error) || !NoLinks(Output, Error) || !NoLinks(Containers, Error))
        return false;
    const FString Context = Root / (TEXT("readback-input-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
    if (IFileManager::Get().DirectoryExists(*Context) || !IFileManager::Get().MakeDirectory(*Context, true))
    {
        Error = TEXT("Cannot create unique packed readback context");
        return false;
    }
    TArray<FInput> Inputs;
    for (const TCHAR* Extension : {TEXT("utoc"), TEXT("ucas"), TEXT("pak")})
    {
        FInput Input;
        Input.Source = FPaths::ChangeExtension(Packed, Extension);
        Input.Link = Context / FPaths::GetCleanFilename(Input.Source);
        Input.Maximum = int64(1024) * 1024 * 1024;
        Inputs.Add(MoveTemp(Input));
    }
    for (const TCHAR* Name : {TEXT("global.utoc"), TEXT("global.ucas")})
    {
        FInput Input;
        Input.Source = Containers / Name;
        Input.Link = Context / Name;
        Input.Maximum = 64 * 1024 * 1024;
        Inputs.Add(MoveTemp(Input));
    }
    struct FCleanup
    {
        FString Directory;
        TArray<FInput>& Files;
        ~FCleanup()
        {
            // Unlink only this factory's five exact paths; never change attributes
            // or recursively remove an unexpected tree sharing original files.
            for (const FInput& File : Files)
                if (File.Held)
                    CloseHandle(File.Held);
            for (const FInput& File : Files)
                DeleteFileW(*File.Link);
            RemoveDirectoryW(*Directory);
        }
    } Cleanup{Context, Inputs};
    for (FInput& Input : Inputs)
    {
        if (!NoLinks(Input.Source, Error))
            return false;
        const HANDLE Held = CreateFileW(*Input.Source, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (Held == INVALID_HANDLE_VALUE)
        {
            Error = TEXT("Cannot hold read-only container/script inputs; a writer may still be active");
            return false;
        }
        Input.Held = Held;
        if (!Hash(Input, Error, false))
            return false;
    }
    // The global container has to be a known build's byte for byte (core/game_builds.h).
    if (!P4IsKnownGlobalContainer(Inputs[3].Size, Inputs[3].Digest, Inputs[4].Size, Inputs[4].Digest))
    {
        Error = TEXT("Original global script metadata is not a game build Kadan knows");
        return false;
    }
    for (const FInput& Input : Inputs)
        if (!CreateHardLinkW(*Input.Link, *Input.Source, nullptr))
        {
            Error = TEXT("Cannot hard-link readback inputs on this volume; game data is never copied");
            return false;
        }
    TArray<FString> Args;
    Args.Add(TEXT("to-legacy"));
    Args.Add(Context);
    Args.Add(Output);
    Args.Add(TEXT("--version"));
    Args.Add(P4GameEngineFor(Containers));
    for (const FString& Filter : Filters)
        if (!Filter.IsEmpty())
        {
            Args.Add(TEXT("--filter"));
            Args.Add(Filter);
        }
    Args.Add(TEXT("--no-shaders"));
    Args.Add(TEXT("--no-script-objects"));
    TArray<uint8> Reply;
    if (!P4RunExportTool(Tools.Retoc, Args, Tools.TimeoutSeconds, Reply, Error))
        return false;
    for (FInput& Input : Inputs)
    {
        if (!Hash(Input, Error, true))
            return false;
        FInput Held = Input;
        Held.Source = Input.Link;
        if (!Hash(Held, Error, true))
            return false;
    }
    // The caller verifies exact expected exports and packed content. A tool's
    // zero exit code can still mean that an asset failed conversion.
    return true;
#endif
}

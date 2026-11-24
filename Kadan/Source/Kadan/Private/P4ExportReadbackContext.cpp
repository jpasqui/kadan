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
// The drive a full path is on ("D:"), or empty for a network or unknown path, as the export's work folder tells it.
FString Drive(const FString& Path)
{
    return Path.Len() >= 3 && FChar::IsAlpha(Path[0]) && Path[1] == TCHAR(':') && Path[2] == TCHAR('/')
        ? Path.Left(2) : FString();
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
    // The game's global.utoc and global.ucas are linked into the context, never copied, and a link cannot cross
    // drives. When the work folder is on another drive than the game's files, the context is made on the game's
    // drive instead, in the folder Kadan's export work uses at a drive's root, and the mod's own containers are
    // copied there.
    FString ContextRoot = Root, MadeRoot;
    const FString GameDrive = Drive(Containers), WorkDrive = Drive(Root);
    if (!GameDrive.IsEmpty() && !WorkDrive.IsEmpty() && !GameDrive.Equals(WorkDrive, ESearchCase::IgnoreCase))
    {
        ContextRoot = GameDrive + TEXT("/.kadan-export-work");
        const FString Unplaceable = TEXT("The game's files are on drive ") + GameDrive +
            TEXT(" and this export's work folder is on drive ") + WorkDrive +
            TEXT(". To check the textures it packed, Kadan links the game's global.utoc and global.ucas beside them, "
                 "and never copies them, so it needs a folder of its own on ") +
            GameDrive + TEXT(", and it could not use ") + ContextRoot +
            TEXT(". Put the game dump on the same drive as the mod's export folder, or give your account permission "
                 "to make that folder.");
        if (FPaths::IsSamePath(ContextRoot, Containers) || FPaths::IsUnderDirectory(ContextRoot, Containers) ||
            FPaths::IsUnderDirectory(Containers, ContextRoot) || FPaths::IsUnderDirectory(Output, ContextRoot) ||
            FPaths::IsUnderDirectory(FPaths::GetPath(Packed), ContextRoot) || !NoLinks(ContextRoot, Error))
        {
            Error = Unplaceable;
            return false;
        }
        if (!IFileManager::Get().DirectoryExists(*ContextRoot))
        {
            if (!IFileManager::Get().MakeDirectory(*ContextRoot, false))
            {
                Error = Unplaceable;
                return false;
            }
            MadeRoot = ContextRoot;
        }
    }
    const FString Context = ContextRoot / (TEXT("readback-input-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
    if (IFileManager::Get().DirectoryExists(*Context) || !IFileManager::Get().MakeDirectory(*Context, true))
    {
        if (!MadeRoot.IsEmpty())
            RemoveDirectoryW(*MadeRoot);
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
        FString Directory, MadeRoot;
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
            // The folder on the game's drive goes too when this call made it and nothing else is in it.
            if (!MadeRoot.IsEmpty())
                RemoveDirectoryW(*MadeRoot);
        }
    } Cleanup{Context, MadeRoot, Inputs};
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
    for (int32 Index = 0; Index < Inputs.Num(); ++Index)
    {
        const FInput& Input = Inputs[Index];
        if (CreateHardLinkW(*Input.Link, *Input.Source, nullptr))
            continue;
        // The first three are the mod's own containers, which may be copied; the game's two files never are.
        if (Index < 3 && IFileManager::Get().Copy(*Input.Link, *Input.Source) == COPY_OK)
            continue;
        Error = Index < 3
            ? TEXT("Kadan could not place the mod's packed container ") + Input.Source + TEXT(" in ") + Context +
                TEXT(" to check its textures.")
            : TEXT("Kadan could not link the game's ") + FPaths::GetCleanFilename(Input.Source) + TEXT(" from ") +
                Containers + TEXT(" into ") + Context +
                TEXT(" to check the textures it packed. A link needs both on one drive, and the game's files are "
                     "never copied. Put the game dump on the same drive as the mod's export folder.");
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

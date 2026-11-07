#include "P4CreatorMaterialReadback.h"
#include "P4ExportProcess.h"
#include "P4NativeHash.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "P4MapKitCore/package_reader.h"
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
    bool HashContent = true;
#if PLATFORM_WINDOWS
    BY_HANDLE_FILE_INFORMATION State{};
#endif
};
bool Hash(FInput& Input, FString& Error, bool Compare)
{
    int64 Size = 0;
    FString Digest;
#if PLATFORM_WINDOWS
    if (!Input.HashContent)
    {
        if (!NoLinks(Input.Source, Error))
            return false;
        const HANDLE File = CreateFileW(*Input.Source, FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (File == INVALID_HANDLE_VALUE)
        {
            Error = TEXT("Original material readback context input is unavailable");
            return false;
        }
        BY_HANDLE_FILE_INFORMATION State{};
        const bool Read = GetFileInformationByHandle(File, &State) != 0;
        CloseHandle(File);
        Size = (int64(State.nFileSizeHigh) << 32) | State.nFileSizeLow;
        if (!Read || Size <= 0 || Size > Input.Maximum)
        {
            Error = TEXT("Original material readback context input exceeds its bound");
            return false;
        }
        if (Compare &&
            (State.dwFileAttributes != Input.State.dwFileAttributes ||
                State.dwVolumeSerialNumber != Input.State.dwVolumeSerialNumber ||
                State.nFileIndexHigh != Input.State.nFileIndexHigh ||
                State.nFileIndexLow != Input.State.nFileIndexLow || State.nFileSizeHigh != Input.State.nFileSizeHigh ||
                State.nFileSizeLow != Input.State.nFileSizeLow ||
                CompareFileTime(&State.ftCreationTime, &Input.State.ftCreationTime) != 0 ||
                CompareFileTime(&State.ftLastWriteTime, &Input.State.ftLastWriteTime) != 0))
        {
            Error = TEXT("Original material context file identity or metadata changed during readback");
            return false;
        }
        if (!Compare)
        {
            Input.Size = Size;
            Input.State = State;
        }
        return true;
    }
#endif
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

bool P4ExtractCreatorMaterialReadback(const FP4ExportContainerTools& Tools, const FString& Utoc,
    const FString& GamePaks, const FString& Work, const FString& Destination, const FString& OwnRoot,
    const TArray<FString>& ExpectedPackages, FString& Error)
{
#if !PLATFORM_WINDOWS
    Error = TEXT("Packed readback requires the supported Windows hard-link boundary");
    return false;
#else
    TSet<FString> Expected;
    if (!OwnRoot.StartsWith(TEXT("/Game/Mods/"), ESearchCase::CaseSensitive) ||
        !FPackageName::IsValidLongPackageName(OwnRoot) || ExpectedPackages.Num() == 0 || ExpectedPackages.Num() > 1024)
    {
        Error = TEXT("Material readback needs a bounded exact creator package set");
        return false;
    }
    for (const FString& Package : ExpectedPackages)
    {
        if (!Package.StartsWith(OwnRoot + TEXT("/"), ESearchCase::CaseSensitive) ||
            !FPackageName::IsValidLongPackageName(Package) || Expected.Contains(Package))
        {
            Error = TEXT("Material readback cannot admit a game or duplicate package");
            return false;
        }
        Expected.Add(Package);
    }
    const FString Filter = TEXT("Carrot4/Content/") + OwnRoot.Mid(6) + TEXT("/");
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
    // Do not allow the conversion tool to overwrite an unexpected file or
    // a hard link in the reusable output folder.
    TArray<FString> ExistingFiles;
    IFileManager::Get().FindFilesRecursive(ExistingFiles, *Output, TEXT("*"), true, false);
    for (const FString& File : ExistingFiles)
    {
        FString Relative = File;
        if (!NoLinks(File, Error) || !FPaths::MakePathRelativeTo(Relative, *(Output + TEXT("/"))) ||
            !Relative.StartsWith(TEXT("Carrot4/Content/"), ESearchCase::CaseSensitive))
        {
            Error = TEXT("Material readback output already contains an unexpected path");
            return false;
        }
        const FString Extension = FPaths::GetExtension(Relative);
        const FString Package = TEXT("/Game/") + FPaths::GetBaseFilename(Relative.Mid(16), false);
        const HANDLE FileHandle = CreateFileW(*File, FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
            nullptr);
        BY_HANDLE_FILE_INFORMATION State{};
        const bool Independent = FileHandle != INVALID_HANDLE_VALUE && GetFileInformationByHandle(FileHandle, &State) &&
            State.nNumberOfLinks == 1;
        if (FileHandle != INVALID_HANDLE_VALUE)
            CloseHandle(FileHandle);
        if (!Expected.Contains(Package) || !Independent ||
            (Extension != TEXT("uasset") && Extension != TEXT("umap") && Extension != TEXT("uexp") &&
                Extension != TEXT("ubulk") && Extension != TEXT("uptnl")))
        {
            Error = TEXT("Material readback output contains an unselected file or hard link");
            return false;
        }
    }
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
    // Only the supported original asset trio provides external export names.
    // It remains read-only and is never emitted by the nonempty own-root filter.
    for (const TCHAR* Name : {TEXT("Carrot4-Switch.utoc"), TEXT("Carrot4-Switch.ucas"), TEXT("Carrot4-Switch.pak")})
    {
        FInput Input;
        Input.Source = Containers / Name;
        Input.Link = Context / Name;
        Input.Maximum = int64(64) * 1024 * 1024 * 1024;
        Input.HashContent = false;
        Inputs.Add(MoveTemp(Input));
    }
    struct FCleanup
    {
        FString Directory;
        TArray<FInput>& Files;
        ~FCleanup()
        {
            // Unlink only this factory's exact paths; never change attributes
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
    if (Inputs[3].Size != 573 || Inputs[4].Size != 1396336 ||
        !P4ExactNativeString(
            Inputs[3].Digest, TEXT("68f12a6d9496faea1e44d31161d7ec4c7abbfaca1397e6aeceab98d78a5ef7db")) ||
        !P4ExactNativeString(
            Inputs[4].Digest, TEXT("05040e0443f5a661d6a310c5384a7714815a20098321ffe83b703a83281ef52d")))
    {
        Error = TEXT("Original global script metadata differs from the supported 1.1.1 representation");
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
    Args.Add(TEXT("UE4_26"));
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
    // Every extracted asset must be one of the finished own container's exact
    // expected packages. External metadata cannot supply a fallback asset.
    TArray<FString> Files;
    IFileManager::Get().FindFilesRecursive(Files, *Output, TEXT("*"), true, false);
    TSet<FString> Found;
    for (const FString& File : Files)
    {
        FString Relative = File;
        if (!NoLinks(File, Error) || !FPaths::MakePathRelativeTo(Relative, *(Output + TEXT("/"))) ||
            !Relative.StartsWith(TEXT("Carrot4/Content/"), ESearchCase::CaseSensitive))
        {
            Error = TEXT("Material readback contains an unexpected output path");
            return false;
        }
        const FString Extension = FPaths::GetExtension(Relative);
        if (Extension != TEXT("uasset") && Extension != TEXT("umap") && Extension != TEXT("uexp") &&
            Extension != TEXT("ubulk") && Extension != TEXT("uptnl"))
        {
            Error = TEXT("Material readback emitted a file outside its asset contract");
            return false;
        }
        const FString Package = TEXT("/Game/") + FPaths::GetBaseFilename(Relative.Mid(16), false);
        const HANDLE FileHandle = CreateFileW(*File, FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
            nullptr);
        BY_HANDLE_FILE_INFORMATION State{};
        const bool Independent = FileHandle != INVALID_HANDLE_VALUE && GetFileInformationByHandle(FileHandle, &State) &&
            State.nNumberOfLinks == 1;
        if (FileHandle != INVALID_HANDLE_VALUE)
            CloseHandle(FileHandle);
        if (!Expected.Contains(Package) || !Independent)
        {
            Error = TEXT("Material readback extracted an unselected or linked package");
            return false;
        }
        if (Extension != TEXT("uasset") && Extension != TEXT("umap"))
            continue;
        TArray<uint8> Header;
        const int64 Body = IFileManager::Get().FileSize(*FPaths::ChangeExtension(File, TEXT("uexp")));
        if (Body < 0 || Body > int64(1024) * 1024 * 1024 || IFileManager::Get().FileSize(*File) > 64 * 1024 * 1024 ||
            !FFileHelper::LoadFileToArray(Header, *File))
        {
            Error = TEXT("Material readback package is unavailable or exceeds its bounds");
            return false;
        }
        const auto Inventory = P4MapKit::ReadPackageHeader(
            std::vector<uint8>(Header.GetData(), Header.GetData() + Header.Num()), size_t(Body));
        if (!Inventory.Error.Code.empty() || FString(UTF8_TO_TCHAR(Inventory.Package.c_str())) != Package ||
            Found.Contains(Package))
        {
            Error = TEXT("Material readback package identity differs from the exact selected container set");
            return false;
        }
        Found.Add(Package);
    }
    if (Found.Num() != Expected.Num())
    {
        Error = TEXT("Material readback did not reconstruct every expected own package");
        return false;
    }
    Error.Reset();
    return true;
#endif
}

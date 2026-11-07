#include "P4ExportContainer.h"
#include "P4GameBuild.h"
#include "P4GamePaths.h"
#include "P4GameNames.h"
#include "P4CurrentGameRegistry.h"
#include "P4ExportProcess.h"
#include "P4ExportProgress.h"
#include "P4NativeHash.h"
#include "P4MapKitCore/export_container.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Hash/CityHash.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#elif PLATFORM_UNIX
#include <fcntl.h>
#include <unistd.h>
#endif

namespace
{
const TCHAR* const ToolOutOfMemory = TEXT("memory allocation of");
const TCHAR* const ComputerOutOfMemory = TEXT("The computer ran out of memory.");

bool RunOnce(const FString& Executable, const TArray<FString>& Args, double Timeout, TArray<uint8>& Out, FString& Error,
    const TFunction<bool(const uint8*, int32, FString&)>* Sink, int64 ByteLimit)
{
    if (!IFileManager::Get().FileExists(*Executable) || !FMath::IsFinite(Timeout) || Timeout <= 0 || Timeout > 600)
    {
        Error = TEXT("Container tool is missing or timeout is invalid");
        return false;
    }
    FString Command;
    for (const FString& Arg : Args)
    {
        if (Arg.Contains(TEXT("\"")))
        {
            Error = TEXT("Container tool argument contains a quote");
            return false;
        }
        for (TCHAR C : Arg)
            if (C == 0)
            {
                Error = TEXT("Container tool argument contains NUL");
                return false;
            }
        // Escape trailing backslashes before the closing quote for Windows argv parsing.
        FString Quoted = Arg;
        int32 At = Arg.Len() - 1;
        while (At >= 0 && Arg[At--] == TEXT('\\'))
            Quoted += TEXT('\\');
        if (!Command.IsEmpty())
            Command += TEXT(" ");
        Command += TEXT("\"") + Quoted + TEXT("\"");
    }
    void* Read = nullptr;
    void* Write = nullptr;
    if (!FPlatformProcess::CreatePipe(Read, Write))
    {
        Error = TEXT("Cannot create container tool pipe");
        return false;
    }
    FProcHandle Process = FPlatformProcess::CreateProc(
        *Executable, *Command, false, true, true, nullptr, 0, nullptr, Write);
    if (!Process.IsValid())
    {
        FPlatformProcess::ClosePipe(Read, Write);
        Error = TEXT("Cannot launch container tool");
        return false;
    }
    TArray<uint8> Bytes, LastBytes;
    const double Deadline = FPlatformTime::Seconds() + Timeout;
    bool Okay = true;
    int64 Total = 0;
    auto Drain = [&]()
    {
        TArray<uint8> Part;
        FPlatformProcess::ReadPipeToArray(Read, Part);
        // Binary output is never shown, but the last bytes can still say the tool ran out of memory.
        LastBytes.Append(Part);
        if (LastBytes.Num() > 512)
            LastBytes.RemoveAt(0, LastBytes.Num() - 512, false);
        if (!Okay)
            return;
        if (Part.Num() > ByteLimit - Total)
        {
            Error = TEXT("The container tool printed more than this export reads.");
            Okay = false;
        }
        else
        {
            Total += Part.Num();
            if (Sink)
            {
                if (Part.Num() && !(*Sink)(Part.GetData(), Part.Num(), Error))
                    Okay = false;
            }
            else
                Bytes.Append(Part);
        }
    };
    while (FPlatformProcess::IsProcRunning(Process))
    {
        P4PulseExportProgress();
        Drain();
        if (!Okay || FPlatformTime::Seconds() >= Deadline)
        {
            if (Okay)
                Error = TEXT("Container tool timed out");
            Okay = false;
            FPlatformProcess::TerminateProc(Process, true);
            break;
        }
        FPlatformProcess::Sleep(0.001f);
    }
    Drain();
    int32 Code = -1;
    if (!FPlatformProcess::GetProcReturnCode(Process, &Code) || Code != 0)
    {
        if (Okay)
            Error = TEXT("Container tool failed with exit code ") + FString::FromInt(Code);
        Okay = false;
    }
    FPlatformProcess::CloseProc(Process);
    FPlatformProcess::ClosePipe(Read, Write);
    if (!Okay)
    {
        const FString Action = FPaths::GetCleanFilename(Executable).Left(64) +
            (Args.Num() ? TEXT(" ") + Args[0].Left(64) : FString());
        Error = Action + TEXT(": ") + Error.Left(512);
        // UE4.26 merges stderr into this pipe. Text actions can report the
        // bounded diagnostic tail; never decode a binary get or Sink.
        if (!Sink && Args.Num() && Args[0] != TEXT("get") && Bytes.Num())
        {
            int32 Start = FMath::Max(0, Bytes.Num() - 8192);
            while (Start < Bytes.Num() && (Bytes[Start] & 0xc0) == 0x80)
                ++Start;
            TArray<uint8> Tail;
            Tail.Append(Bytes.GetData() + Start, Bytes.Num() - Start);
            for (uint8& Byte : Tail)
                if (!Byte)
                    Byte = ' ';
            if (Tail.Num())
            {
                const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Tail.GetData()), Tail.Num());
                const FString Text(Converted.Length(), Converted.Get());
                FString Diagnostic;
                bool Space = true;
                for (TCHAR Character : Text)
                {
                    if (Character < 32 || Character == 127 || FChar::IsWhitespace(Character))
                    {
                        if (!Space)
                            Diagnostic += TEXT(' ');
                        Space = true;
                    }
                    else
                    {
                        Diagnostic += Character;
                        Space = false;
                    }
                }
                Diagnostic = Diagnostic.Right(2048).TrimStartAndEnd();
                if (!Diagnostic.IsEmpty())
                    Error += TEXT("; output: ") + Diagnostic;
            }
        }
        LastBytes.Add(0);
        for (uint8& Byte : LastBytes)
            if (!Byte)
                Byte = ' ';
        LastBytes.Last() = 0;
        if (!Error.Contains(ToolOutOfMemory) &&
            FString(UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(LastBytes.GetData()))).Contains(ToolOutOfMemory))
            Error += FString(TEXT("; ")) + ToolOutOfMemory;
        return false;
    }
    if (!Sink)
        Out = MoveTemp(Bytes);
    return true;
}

// The tool stops when the computer has no memory to give it, which says nothing about the asset it was reading: it
// gets a few chances as memory frees up, and if it still cannot run the error says what happened.
bool Run(const FString& Executable, const TArray<FString>& Args, double Timeout, TArray<uint8>& Out, FString& Error,
    const TFunction<bool(const uint8*, int32, FString&)>* Sink = nullptr, int64 ByteLimit = 64 * 1024 * 1024)
{
    // A stream has already handed its first bytes on, so running it again would repeat them.
    const TArray<float> Waits = Sink ? TArray<float>() : TArray<float>({15.f, 30.f, 60.f});
    const FString Before = Error;
    for (const float Wait : Waits)
    {
        if (RunOnce(Executable, Args, Timeout, Out, Error, Sink, ByteLimit))
        {
            Error = Before;
            return true;
        }
        if (!Error.Contains(ToolOutOfMemory))
            return false;
        for (float Waited = 0; Waited < Wait && !P4ExportWasCancelled(); Waited += 1.f)
        {
            P4PulseExportProgress();
            FPlatformProcess::Sleep(1.f);
        }
        if (P4ExportWasCancelled())
            break;
    }
    if (RunOnce(Executable, Args, Timeout, Out, Error, Sink, ByteLimit))
    {
        Error = Before;
        return true;
    }
    if (Error.Contains(ToolOutOfMemory))
        Error = FString(ComputerOutOfMemory) + TEXT(" Close other programs and try again. ") + Error;
    return false;
}

bool Text(const TArray<uint8>& Bytes, FString& Out, FString& Error)
{
    for (uint8 C : Bytes)
        if (C == 0)
        {
            Error = TEXT("Container listing contains binary data");
            return false;
        }
    FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
    Out = FString(Converted.Length(), Converted.Get());
    return true;
}

bool Header(const FP4ExportContainerTools& Tools, const FString& Utoc, const FString& Work,
    P4MapKit::Export::ContainerHeader& Out, FString& Error, FString* Digest = nullptr)
{
    TArray<uint8> Bytes;
    FString Listing;
    if (!Run(Tools.Retoc, {TEXT("list"), Utoc}, Tools.TimeoutSeconds, Bytes, Error) || !Text(Bytes, Listing, Error))
        return false;
    TArray<FString> Lines;
    Listing.ParseIntoArrayLines(Lines);
    FString Chunk;
    for (const FString& Line : Lines)
    {
        TArray<FString> Words;
        Line.ParseIntoArrayWS(Words);
        if (Words.Num() >= 3 && Words[2] == TEXT("ContainerHeader"))
        {
            if (!Chunk.IsEmpty())
            {
                Error = TEXT("Container has multiple header chunks");
                return false;
            }
            Chunk = Words[1];
        }
    }
    if (Chunk.Len() != 24)
    {
        Error = TEXT("Container header chunk is missing");
        return false;
    }
    for (TCHAR C : Chunk)
        if (!FChar::IsHexDigit(C))
        {
            Error = TEXT("Invalid container header chunk ID");
            return false;
        }
    if (!IFileManager::Get().MakeDirectory(*Work, true))
    {
        Error = TEXT("Cannot create inspection scratch");
        return false;
    }
    const FString File = FPaths::Combine(
        Work, TEXT("header-") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".bin"));
    struct FCleanup
    {
        FString File;
        ~FCleanup() { IFileManager::Get().Delete(*File); }
    } Cleanup{File};
    if (!Run(Tools.Retoc, {TEXT("get"), Utoc, Chunk, File}, Tools.TimeoutSeconds, Bytes, Error))
        return false;
    const int64 Size = IFileManager::Get().FileSize(*File);
    if (Size < 0 || Size > 64 * 1024 * 1024 || !FFileHelper::LoadFileToArray(Bytes, *File) || Bytes.Num() != Size)
    {
        Error = TEXT("Container header bytes are missing or unreadable");
        return false;
    }
    std::vector<std::uint8_t> Native(Bytes.GetData(), Bytes.GetData() + Bytes.Num());
    std::string Why;
    if (!P4MapKit::Export::ReadContainerHeader(Native, Out, Why))
    {
        Error = UTF8_TO_TCHAR(Why.c_str());
        return false;
    }
    if (Digest && !P4HashSourceBuffers(Bytes.GetData(), Bytes.Num(), nullptr, 0, *Digest))
    {
        Error = TEXT("Cannot hash actual container header bytes");
        return false;
    }
    return true;
}

uint64 PackageId(const FString& Package)
{
    const FString Lower = Package.ToLower();
    // Unreal's package identity is CityHash64 of lowercase UTF-16 little-endian bytes.
    TArray<uint8> Bytes;
    Bytes.Reserve(Lower.Len() * 2);
    for (TCHAR C : Lower)
    {
        Bytes.Add(uint8(C));
        Bytes.Add(uint8(uint32(C) >> 8));
    }
    return CityHash64(reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num());
}
} //namespace

bool P4ToolRanOutOfMemory(const FString& Error)
{
    return Error.Contains(ComputerOutOfMemory);
}

FString P4DiskFullMessage(const FString& Folder)
{
    constexpr double GigaBytes = 1024.0 * 1024.0 * 1024.0;
    const FString Setting = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_DISK_FLOOR_GB"));
    const double Floor = Setting.IsEmpty() ? 1.0 : FCString::Atod(*Setting);
    uint64 Total = 0, Free = 0;
    if (!FPlatformMisc::GetDiskTotalAndFreeSpace(Folder, Total, Free) || double(Free) / GigaBytes >= Floor)
        return FString();
    return FString::Printf(TEXT("The disk holding %s is almost full (%.1f GB free). Free some space and run this again."),
        *Folder, double(Free) / GigaBytes);
}

bool P4CopyFileWhole(const FString& From, const FString& To)
{
    const FString Part = To + TEXT(".part");
    IFileManager& Files = IFileManager::Get();
    Files.MakeDirectory(*FPaths::GetPath(To), true);
    // Attributes too: Kadan keeps the game's assets read-only in a project.
    if (Files.Copy(*Part, *From, true, true, true) != COPY_OK)
    {
        Files.Delete(*Part, false, true, true);
        return false;
    }
    // Another program can hold the file for a moment. A full disk will not clear up by waiting.
    for (int32 Try = 0; Try < 5; ++Try)
    {
        if (Files.Move(*To, *Part, true, true, false, true))
            return true;
        if (!P4DiskFullMessage(FPaths::GetPath(To)).IsEmpty())
            break;
        FPlatformProcess::Sleep(0.5f);
    }
    Files.Delete(*Part, false, true, true);
    return false;
}

int64 P4AvailableCommitBytes()
{
#if PLATFORM_WINDOWS
    MEMORYSTATUSEX Status;
    Status.dwLength = sizeof(Status);
    if (GlobalMemoryStatusEx(&Status))
        return int64(Status.ullAvailPageFile);
#endif
    return int64(FPlatformMemory::GetStats().AvailablePhysical);
}

bool P4RunExportTool(
    const FString& Executable, const TArray<FString>& Args, double Timeout, TArray<uint8>& Out, FString& Error)
{
    return Run(Executable, Args, Timeout, Out, Error);
}
bool P4RunExportToolStream(const FString& Executable, const TArray<FString>& Args, double Timeout,
    const TFunction<bool(const uint8*, int32, FString&)>& Sink, int64 ByteLimit, FString& Error)
{
    if (ByteLimit <= 0 || ByteLimit > (int64(1) << 31))
    {
        Error = TEXT("Container stream byte limit is invalid");
        return false;
    }
    TArray<uint8> Unused;
    return Run(Executable, Args, Timeout, Unused, Error, &Sink, ByteLimit);
}
bool P4RunExportToolToFile(const FString& Executable, const TArray<FString>& Args, double Timeout, const FString& File,
    int64 MaximumBytes, FString& Error)
{
    if (MaximumBytes <= 0 || MaximumBytes > (int64(1) << 31))
    {
        Error = TEXT("The binary tool output limit is invalid.");
        return false;
    }
    const FString Full = FPaths::ConvertRelativePathToFull(File);
#if PLATFORM_WINDOWS
    // IFileManager's NoReplaceExisting flag is not an exclusive Windows open.
    // CREATE_NEW makes the existence check and creation one filesystem action.
    FString Native = Full.Replace(TEXT("/"), TEXT("\\"));
    if (!Native.StartsWith(TEXT("\\\\?\\")))
    {
        const DWORD Required = GetFullPathNameW(*Native, 0, nullptr, nullptr);
        if (!Required || Required > 32768)
        {
            Error = TEXT("The private binary output path cannot be resolved.");
            return false;
        }
        TArray<WCHAR> Resolved;
        Resolved.SetNumUninitialized(int32(Required));
        const DWORD Written = GetFullPathNameW(*Native, Required, Resolved.GetData(), nullptr);
        if (!Written || Written >= Required)
        {
            Error = TEXT("The private binary output path cannot be resolved.");
            return false;
        }
        Native = Resolved.GetData();
        Native = Native.StartsWith(TEXT("\\\\")) ? TEXT("\\\\?\\UNC\\") + Native.Mid(2) : TEXT("\\\\?\\") + Native;
    }
    const HANDLE Handle = CreateFileW(*Native, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (Handle == INVALID_HANDLE_VALUE)
    {
        Error = TEXT("Cannot exclusively create the private binary tool output.");
        return false;
    }
#elif PLATFORM_UNIX
    const FTCHARToUTF8 Native(*Full);
    const int Handle = ::open(Native.Get(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (Handle < 0)
    {
        Error = TEXT("Cannot exclusively create the private binary tool output.");
        return false;
    }
#else
    Error = TEXT("Exclusive binary tool output is unavailable on this platform.");
    return false;
#endif
    const TFunction<bool(const uint8*, int32, FString&)> Sink = [&](const uint8* Part, int32 Size, FString& Why)
    {
        int32 Done = 0;
        while (Done < Size)
        {
#if PLATFORM_WINDOWS
            DWORD Written = 0;
            if (!WriteFile(Handle, Part + Done, DWORD(Size - Done), &Written, nullptr) || !Written)
            {
                Why = TEXT("Cannot stream the binary tool output.");
                return false;
            }
            Done += int32(Written);
#elif PLATFORM_UNIX
            const ssize_t Written = ::write(Handle, Part + Done, size_t(Size - Done));
            if (Written <= 0)
            {
                Why = TEXT("Cannot stream the binary tool output.");
                return false;
            }
            Done += int32(Written);
#else
            return false;
#endif
        }
        return true;
    };
    TArray<uint8> Unused;
    const bool Extracted = Run(Executable, Args, Timeout, Unused, Error, &Sink, MaximumBytes);
#if PLATFORM_WINDOWS
    const bool Flushed = !Extracted || FlushFileBuffers(Handle) != 0;
    const bool Closed = CloseHandle(Handle) != 0;
#elif PLATFORM_UNIX
    const bool Flushed = !Extracted || ::fsync(Handle) == 0;
    const bool Closed = ::close(Handle) == 0;
#else
    const bool Flushed = false, Closed = false;
#endif
    if (Extracted && (!Flushed || !Closed))
        Error = TEXT("Cannot finish the private binary tool output.");
    return Extracted && Flushed && Closed;
}
uint64 P4ExportPackageId(const FString& Package)
{
    return PackageId(Package);
}

bool FP4ExportGameInventory::IsFromDirectory(const FString& Directory) const
{
    FString Canonical = FPaths::ConvertRelativePathToFull(Directory);
    FPaths::NormalizeDirectoryName(Canonical);
    return Valid && FPaths::IsSamePath(SourceDirectory, Canonical);
}

bool P4ReadExportGameInventory(const FP4ExportContainerTools& Tools, const FString& Paks, const FString& Work,
    FP4ExportGameInventory& Out, FString& Error)
{
    const FString Utoc = FPaths::Combine(Paks, P4GameContainerIn(Paks) + TEXT(".utoc"));
    int64 UtocSize = 0;
    FString BeforeUtoc;
    if (!P4HashFile(Utoc, int64(512) * 1024 * 1024, UtocSize, BeforeUtoc) || UtocSize <= 0)
    {
        Error = TEXT("Actual game container index is unreadable or exceeds its bound");
        return false;
    }
    FP4CurrentGameRegistryPtr Registry;
    TArray<FString> Names;
    if (!P4AcquireCurrentGameRegistry(Tools, Paks, Work, Registry, Error) || !Registry->PackageNames(Names, Error))
        return false;
    FP4ExportGameInventory Result;
    Result.RegistryDigest = Registry->RegistrySha256();
    for (const FString& Name : Names)
    {
        if (!P4IsContentPackage(Name))
            continue;
        if (!FPackageName::IsValidLongPackageName(Name))
        {
            Error = TEXT("Invalid game package name");
            return false;
        }
        const uint64 Id = PackageId(Name);
        const FName Package(*Name);
        if (const FName* Existing = Result.Ids.Find(Id))
            if (*Existing != Package)
            {
                Error = TEXT("Game package identity collision");
                return false;
            }
        Result.Names.Add(Package);
        Result.Ids.Add(Id, Package);
    }
    if (!Result.Names.Num())
    {
        Error = TEXT("Game package inventory is unavailable");
        return false;
    }
    P4MapKit::Export::ContainerHeader Actual;
    if (!Header(Tools, Utoc, Work, Actual, Error, &Result.HeaderDigest))
        return false;
    int64 AfterSize = 0;
    if (!P4HashFile(Utoc, int64(512) * 1024 * 1024, AfterSize, Result.UtocDigest) || AfterSize != UtocSize ||
        !P4ExactNativeString(BeforeUtoc, Result.UtocDigest))
    {
        Error = TEXT("Actual game container index changed during inventory inspection");
        return false;
    }
    // The input hashes bind this census. Package contents require their own actual readback proofs.
    if (!P4HashUtf8(TEXT("p4-game-inventory/1\n") + Result.RegistryDigest + TEXT("\n") + Result.HeaderDigest +
                TEXT("\n") + Result.UtocDigest,
            Result.InventoryDigest))
    {
        Error = TEXT("Cannot bind actual game inventory identity");
        return false;
    }
    Result.SourceDirectory = FPaths::ConvertRelativePathToFull(Paks);
    FPaths::NormalizeDirectoryName(Result.SourceDirectory);
    // Keep IDs even when a package has no registry row: the container census must still detect it.
    for (uint64 Id : Actual.Packages)
        if (!Result.Ids.Contains(Id))
            Result.Ids.Add(Id, NAME_None);
    for (const auto& Redirect : Actual.Redirects)
        if (!Result.Ids.Contains(Redirect.first))
            Result.Ids.Add(Redirect.first, NAME_None);
    for (const auto& Redirect : Actual.CultureRedirects)
        if (!Result.Ids.Contains(Redirect.first))
            Result.Ids.Add(Redirect.first, NAME_None);
    if (!Registry->IsCurrent(Tools, Paks, Error))
        return false;
    Result.Valid = true;
    Out = MoveTemp(Result);
    Error.Reset();
    return true;
}

bool P4InspectExportContainer(const FP4ExportContainerTools& Tools, const FString& Utoc, const FString& Work,
    const FP4ExportGameInventory& Game, FP4ExportContainerCensus& Out, FString& Error)
{
    if (!Game.IsValid())
    {
        Error = TEXT("Actual game package inventory is required");
        return false;
    }
    P4MapKit::Export::ContainerHeader HeaderData;
    if (!Header(Tools, Utoc, Work, HeaderData, Error))
        return false;
    FP4ExportContainerCensus Result;
    for (uint64 Id : HeaderData.Packages)
    {
        Result.PackageIds.Add(Id);
        if (Game.PackageIds().Contains(Id))
            Result.GameClaims.AddUnique(Id);
    }
    for (const auto& Redirect : HeaderData.Redirects)
    {
        Result.Redirects.Add(Redirect.first, Redirect.second);
        if (Game.PackageIds().Contains(Redirect.first))
            Result.GameClaims.AddUnique(Redirect.first);
    }
    for (const auto& Redirect : HeaderData.CultureRedirects)
    {
        Result.CultureRedirects.Emplace(Redirect.first, Redirect.second);
        if (Game.PackageIds().Contains(Redirect.first))
            Result.GameClaims.AddUnique(Redirect.first);
    }
    Out = MoveTemp(Result);
    Error.Reset();
    return true;
}

bool P4ValidateExportCensusSelection(
    const FP4ExportContainerCensus& Census, const TArray<FString>& Selected, FString& Error)
{
    P4MapKit::Export::ContainerHeader Header;
    for (uint64 Id : Census.PackageIds)
        Header.Packages.push_back(Id);
    for (const auto& Pair : Census.Redirects)
        Header.Redirects.emplace_back(Pair.Key, Pair.Value);
    for (const auto& Pair : Census.CultureRedirects)
        Header.CultureRedirects.emplace_back(Pair.Key, Pair.Value);
    std::vector<std::uint64_t> Ids;
    for (const FString& Package : Selected)
    {
        if (!P4IsValidContentPackage(Package))
        {
            Error = TEXT("Selected container package is not canonical");
            return false;
        }
        Ids.push_back(PackageId(Package));
    }
    std::string Why;
    if (!P4MapKit::Export::CheckSelectedContainer(Header, Ids, Why))
    {
        Error = UTF8_TO_TCHAR(Why.c_str());
        return false;
    }
    Error.Reset();
    return true;
}

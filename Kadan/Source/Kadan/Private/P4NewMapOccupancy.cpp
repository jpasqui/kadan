#include "P4NewMapOccupancy.h"
#include "P4NativeHash.h"
#include "P4MapKitCore/new_map_census.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
namespace Census = P4MapKit::MapCensus;
bool SavePath(const FString& Input, FString& Root, FString& Error)
{
    if (Input.IsEmpty() || FPaths::IsRelative(Input))
    {
        Error = TEXT("Actual save root must be an explicit absolute directory");
        return false;
    }
    for (TCHAR C : Input)
        if (!C)
        {
            Error = TEXT("Actual save root contains NUL");
            return false;
        }
    Root = FPaths::ConvertRelativePathToFull(Input);
    FPaths::NormalizeDirectoryName(Root);
    if (!FPaths::CollapseRelativeDirectories(Root) || !IFileManager::Get().DirectoryExists(*Root))
    {
        Error = TEXT("Selected actual save root does not exist");
        return false;
    }
#if PLATFORM_WINDOWS
    FString At = Root;
    while (!At.IsEmpty())
    {
        const DWORD Attributes = GetFileAttributesW(*At);
        if (Attributes == INVALID_FILE_ATTRIBUTES || (Attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        {
            Error = TEXT("Actual save root is unavailable or contains filesystem links");
            return false;
        }
        const FString Parent = FPaths::GetPath(At);
        if (Parent == At)
            break;
        At = Parent;
    }
    return true;
#else
    Error = TEXT("Read-only save custody requires the supported Windows filesystem boundary");
    return false;
#endif
}

bool ReadRecord(const FString& Root, Census::Source Source, Census::File& Out, FString& Error)
{
    const FString Relative = UTF8_TO_TCHAR(Census::Path(Source) + 6);
    const FString Path = Root / Relative;
#if PLATFORM_WINDOWS
    FString At = Path;
    bool Missing = false;
    while (!At.IsEmpty())
    {
        const DWORD Attributes = GetFileAttributesW(*At);
        if (Attributes == INVALID_FILE_ATTRIBUTES)
        {
            const DWORD Why = GetLastError();
            if (Why == ERROR_FILE_NOT_FOUND || Why == ERROR_PATH_NOT_FOUND)
                Missing = true;
            else
            {
                Error = TEXT("Actual map allocation record is unavailable");
                return false;
            }
        }
        else if (Attributes & FILE_ATTRIBUTE_REPARSE_POINT)
        {
            Error = TEXT("Actual map allocation record path contains a filesystem link");
            return false;
        }
        const FString Parent = FPaths::GetPath(At);
        if (Parent == At)
            break;
        At = Parent;
    }
    if (Missing)
    {
        Out = Census::File{};
        return true;
    }
#endif
    TUniquePtr<FArchive> File(IFileManager::Get().CreateFileReader(*Path));
    if (!File || File->TotalSize() < 0 || File->TotalSize() > int64(Census::kMaxRecordBytes))
    {
        Error = TEXT("Actual map allocation record is unreadable or exceeds the authoring bound");
        return false;
    }
    const int64 Size = File->TotalSize();
    Census::File Result;
    Result.State = Census::Presence::Present;
    Result.Bytes.resize(size_t(Size));
    if (Size)
        File->Serialize(&Result.Bytes[0], Size);
    if (File->IsError() || File->TotalSize() != Size)
    {
        Error = TEXT("Actual map allocation record changed during read");
        return false;
    }
    FString Digest;
    if (!P4HashSourceBuffers(reinterpret_cast<const uint8*>(Result.Bytes.data()), Size, nullptr, 0, Digest))
    {
        Error = TEXT("Cannot hash actual map allocation record");
        return false;
    }
    Result.Digest = TCHAR_TO_UTF8(*Digest);
    Out = MoveTemp(Result);
    return true;
}

bool Acquire(const FString& Root, Census::Input& Out, FString& Error)
{
    Census::Input Result;
    if (!ReadRecord(Root, Census::Source::First, Result.First, Error) ||
        !ReadRecord(Root, Census::Source::Second, Result.Second, Error))
        return false;
    if (Result.First.State == Census::Presence::Missing && Result.Second.State == Census::Presence::Missing &&
        !ReadRecord(Root, Census::Source::Legacy, Result.Legacy, Error))
        return false;
    Out = MoveTemp(Result);
    return true;
}
bool Same(const Census::File& A, const Census::File& B)
{
    return A.State == B.State && A.Bytes.size() == B.Bytes.size() && A.Digest == B.Digest;
}
bool Same(const Census::Input& A, const Census::Input& B)
{
    return Same(A.First, B.First) && Same(A.Second, B.Second) && Same(A.Legacy, B.Legacy);
}
const TCHAR* Refusal(Census::Status State)
{
    if (State == Census::Status::Ambiguous)
        return TEXT("map-allocation-copies-ambiguous");
    if (State == Census::Status::Damaged)
        return TEXT("map-allocation-records-damaged");
    return TEXT("map-allocation-records-unavailable");
}
} //namespace

struct FP4NewMapOccupancy::FSnapshot
{
    FString Root;
    P4MapKit::MapCensus::Input Files;
};

bool P4ReadNewMapOccupancy(const FString& SaveDirectory, FP4NewMapOccupancy& Out, FString& Error)
{
    FString Root;
    if (!SavePath(SaveDirectory, Root, Error))
        return false;
    Census::Input Input;
    if (!Acquire(Root, Input, Error))
        return false;
    const Census::Result CensusResult = Census::Read(Input);
    if (!Census::IsUsable(CensusResult))
    {
        Error = Refusal(CensusResult.State);
        return false;
    }
    Census::Input Again;
    if (!Acquire(Root, Again, Error) || !Same(Input, Again))
    {
        if (Error.IsEmpty())
            Error = TEXT("Actual map allocation records changed during census");
        return false;
    }
    auto Held = MakeShared<FP4NewMapOccupancy::FSnapshot>();
    Held->Root = Root;
    Held->Files = MoveTemp(Input);
    FP4NewMapOccupancy Result;
    Result.Snapshot = Held;
    Result.Generation = CensusResult.Counter;
    if (CensusResult.State == Census::Status::Valid)
        Result.SnapshotState = EP4MapOccupancyState::Valid;
    else if (CensusResult.State == Census::Status::Recovered)
        Result.SnapshotState = EP4MapOccupancyState::Recovered;
    else if (CensusResult.State == Census::Status::Legacy)
        Result.SnapshotState = EP4MapOccupancyState::Legacy;
    for (const auto& Claim : CensusResult.Occupied)
    {
        FP4OccupiedMapSlot Slot;
        Slot.Area = Claim.Area;
        Slot.Map = UTF8_TO_TCHAR(Claim.Id.c_str());
        Result.Slots.Add(MoveTemp(Slot));
    }
    FString Identity = TEXT("p4-map-allocation-snapshot/1\n");
    for (const Census::File* File : {&Held->Files.First, &Held->Files.Second, &Held->Files.Legacy})
        Identity += FString::FromInt(int32(File->State)) + TEXT(":") +
            FString::Printf(TEXT("%llu"), uint64(File->Bytes.size())) + TEXT(":") +
            UTF8_TO_TCHAR(File->Digest.c_str()) + TEXT("\n");
    if (!P4HashUtf8(Identity, Result.Digest))
    {
        Error = TEXT("Cannot bind map allocation snapshot");
        return false;
    }
    Result.Valid = true;
    Out = MoveTemp(Result);
    Error.Reset();
    return true;
}

bool P4RevalidateNewMapOccupancy(const FP4NewMapOccupancy& Snapshot, FString& Error)
{
    if (!Snapshot.Valid || !Snapshot.Snapshot.IsValid())
    {
        Error = TEXT("Actual map allocation snapshot is required");
        return false;
    }
    FString Root;
    if (!SavePath(Snapshot.Snapshot->Root, Root, Error))
        return false;
    Census::Input Current;
    if (!Acquire(Root, Current, Error))
        return false;
    if (!Same(Current, Snapshot.Snapshot->Files))
    {
        Error = TEXT("Actual map allocation snapshot changed before publication");
        return false;
    }
    Error.Reset();
    return true;
}

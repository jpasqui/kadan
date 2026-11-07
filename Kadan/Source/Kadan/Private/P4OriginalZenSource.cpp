#include "P4OriginalZenSource.h"
#include "P4GamePaths.h"
#include "P4ExportProcess.h"
#include "P4KadanAcquisitionVeto.h"
#include "P4NativeHash.h"
#include "P4RegistryInputLease.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

namespace P4OriginalZenPrivate
{
FString Full(FString Path)
{
    if (Path.IsEmpty() || Path.Contains(TEXT("\"")))
        return FString();
    for (TCHAR C : Path)
        if (!C)
            return FString();
    Path = FPaths::ConvertRelativePathToFull(Path);
    FPaths::NormalizeFilename(Path);
    if (!FPaths::CollapseRelativeDirectories(Path))
        return FString();
    FPaths::NormalizeDirectoryName(Path);
    return Path;
}
bool NoAliases(FString Path)
{
#if PLATFORM_WINDOWS
    while (!Path.IsEmpty())
    {
        const DWORD A = GetFileAttributesW(*Path);
        if (A != INVALID_FILE_ATTRIBUTES && (A & FILE_ATTRIBUTE_REPARSE_POINT))
            return false;
        const FString Parent = FPaths::GetPath(Path);
        if (Parent == Path)
            break;
        Path = Parent;
    }
    return true;
#else
    (void)Path;
    return false;
#endif
}
bool SameNames(const TArray<FString>& A, const TArray<FString>& B)
{
    if (A.Num() != B.Num())
        return false;
    for (int32 I = 0; I < A.Num(); ++I)
        if (!P4ExactNativeString(A[I], B[I]))
            return false;
    return true;
}
// No recursion: this command's output is a single flat package basename.
bool Census(const FString& Root, TArray<FString>& Out, FString& Error)
{
    Out.Reset();
    TArray<FString> Names;
    IFileManager::Get().FindFiles(Names, *(Root / TEXT("*")), true, true);
    if (Names.Num() > 8)
    {
        Error = TEXT("original-zen-output-count");
        return false;
    }
    for (const FString& Name : Names)
    {
        const FString File = Root / Name;
#if PLATFORM_WINDOWS
        const DWORD A = GetFileAttributesW(*File);
        if (A == INVALID_FILE_ATTRIBUTES || (A & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
        {
            Error = TEXT("original-zen-output-not-flat-regular-file");
            return false;
        }
#else
        Error = TEXT("original-zen-immutable-file-custody-unavailable");
        return false;
#endif
        Out.Add(Name);
    }
    Out.Sort();
    return true;
}
struct FOutput
{
    FString File, Digest;
    TArray<uint8> Bytes;
    TUniquePtr<P4RegistryLease::FInput> Lease;
#if PLATFORM_WINDOWS
    BY_HANDLE_FILE_INFORMATION Identity{};
    static bool Same(const BY_HANDLE_FILE_INFORMATION& A, const BY_HANDLE_FILE_INFORMATION& B)
    {
        return A.dwVolumeSerialNumber == B.dwVolumeSerialNumber && A.nFileIndexHigh == B.nFileIndexHigh &&
            A.nFileIndexLow == B.nFileIndexLow && A.dwFileAttributes == B.dwFileAttributes &&
            A.nFileSizeHigh == B.nFileSizeHigh && A.nFileSizeLow == B.nFileSizeLow &&
            A.nNumberOfLinks == B.nNumberOfLinks && CompareFileTime(&A.ftCreationTime, &B.ftCreationTime) == 0 &&
            CompareFileTime(&A.ftLastWriteTime, &B.ftLastWriteTime) == 0;
    }
    bool Info(BY_HANDLE_FILE_INFORMATION& Out) const
    {
        const HANDLE H = CreateFileW(*File, FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (H == INVALID_HANDLE_VALUE)
            return false;
        const bool Okay = GetFileInformationByHandle(H, &Out) != 0;
        CloseHandle(H);
        return Okay;
    }
#endif
    bool Open(const FString& Path, int64 Limit)
    {
        File = Path;
#if PLATFORM_WINDOWS
        Lease = MakeUnique<P4RegistryLease::FInput>();
        if (!Lease->Open(std::wstring(*File)) || Lease->Size() > uint64(Limit) || !Info(Identity) ||
            Identity.nNumberOfLinks != 1 ||
            (Identity.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
            !FFileHelper::LoadFileToArray(Bytes, *File) || uint64(Bytes.Num()) != Lease->Size() ||
            !P4HashSourceBuffers(Bytes.GetData(), Bytes.Num(), nullptr, 0, Digest) || !Current())
            return false;
        return true;
#else
        (void)Limit;
        return false;
#endif
    }
    bool Current() const
    {
#if PLATFORM_WINDOWS
        BY_HANDLE_FILE_INFORMATION Now{};
        return Lease && Lease->Current() && Info(Now) && Same(Identity, Now);
#else
        return false;
#endif
    }
    // Reopen with exclusive byte custody and hash that same deletion handle.
    // Changed/unexpected output is retained, never cleaned by pathname alone.
    bool DeleteKnown()
    {
#if PLATFORM_WINDOWS
        Lease.Reset();
        const HANDLE H = CreateFileW(*File, GENERIC_READ | DELETE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (H == INVALID_HANDLE_VALUE)
            return false;
        BY_HANDLE_FILE_INFORMATION Now{}, After{};
        FString Hash;
        const bool Bound = GetFileInformationByHandle(H, &Now) && Same(Identity, Now);
        const bool Hashed = Bound &&
            P4HashByteStream(
                [&](const TFunction<bool(const uint8*, int32)>& Add)
                {
                    uint8 Buffer[64 * 1024];
                    DWORD Read = 0;
                    int64 Total = 0;
                    for (;;)
                    {
                        if (!ReadFile(H, Buffer, sizeof(Buffer), &Read, nullptr))
                            return false;
                        if (!Read)
                            break;
                        Total += Read;
                        if (Total > Bytes.Num() || !Add(Buffer, int32(Read)))
                            return false;
                    }
                    return Total == Bytes.Num();
                },
                Hash);
        FILE_DISPOSITION_INFO Disposition{1};
        const bool Okay = Hashed && P4ExactNativeString(Hash, Digest) && GetFileInformationByHandle(H, &After) &&
            Same(Now, After) && SetFileInformationByHandle(H, FileDispositionInfo, &Disposition, sizeof(Disposition));
        CloseHandle(H);
        return Okay;
#else
        return false;
#endif
    }
};
} //namespace P4OriginalZenPrivate

struct FP4OriginalZenSource::FImpl
{
    FP4CurrentGameRegistryPtr Reader;
    FP4ExportContainerTools Tools;
    FString Paks, Package, Root, Stem;
    uint64 Id = 0;
    bool Preserve = false;
    TArray<FString> Names;
    TArray<TUniquePtr<P4OriginalZenPrivate::FOutput>> Outputs;
    int32 RawIndex = INDEX_NONE, MetadataIndex = INDEX_NONE;
    TUniquePtr<P4RegistryLease::FInput> DirectoryLease;
    bool Current(FString& Error) const
    {
        if (!Reader || !Reader->IsCurrent(Tools, Paks, Error))
            return false;
        if (!DirectoryLease || !DirectoryLease->Current())
        {
            Error = TEXT("original-zen-custody-directory-changed");
            return false;
        }
        TArray<FString> Now;
        if (!P4OriginalZenPrivate::Census(Root, Now, Error) || !P4OriginalZenPrivate::SameNames(Names, Now))
        {
            if (Error.IsEmpty())
                Error = TEXT("original-zen-custody-members-changed");
            return false;
        }
        for (const auto& File : Outputs)
            if (!File->Current())
            {
                Error = TEXT("original-zen-custody-file-changed");
                return false;
            }
        return true;
    }
    ~FImpl()
    {
        if (Root.IsEmpty() || Preserve)
            return;
        FString Error;
        TArray<FString> Now;
        if (!DirectoryLease || !DirectoryLease->Current() || !P4OriginalZenPrivate::Census(Root, Now, Error) ||
            !P4OriginalZenPrivate::SameNames(Names, Now))
            return;
        for (const auto& File : Outputs)
            if (!File->Current())
                return;
        for (const auto& File : Outputs)
            if (!File->DeleteKnown())
                return;
        DirectoryLease.Reset();
        // Empty own directory only. Parent work directory is never removed.
        IFileManager::Get().DeleteDirectory(*Root, false, false);
    }
};

FP4OriginalZenSource::FP4OriginalZenSource(TUniquePtr<FImpl>&& In) : Impl(MoveTemp(In))
{}
FP4OriginalZenSource::~FP4OriginalZenSource() = default;
bool FP4OriginalZenSource::Current(FString& Error) const
{
    return Impl && Impl->Current(Error);
}
bool FP4OriginalZenSource::Read(
    const TFunction<bool(const TArray<uint8>&, const TArray<uint8>&, FString&)>& Consumer, FString& Error) const
{
    if (!Consumer || !Current(Error))
        return false;
    const bool Accepted = Consumer(
        Impl->Outputs[Impl->RawIndex]->Bytes, Impl->Outputs[Impl->MetadataIndex]->Bytes, Error);
    // Check even when consumer refuses; never turn that refusal into success.
    FString CurrentError;
    const bool StillCurrent = Current(CurrentError);
    if (!StillCurrent)
        Error = CurrentError;
    return Accepted && StillCurrent;
}
const FString& FP4OriginalZenSource::Package() const
{
    return Impl->Package;
}
uint64 FP4OriginalZenSource::PackageId() const
{
    return Impl->Id;
}
const FString& FP4OriginalZenSource::RawSha256() const
{
    return Impl->Outputs[Impl->RawIndex]->Digest;
}
const FString& FP4OriginalZenSource::MetadataSha256() const
{
    return Impl->Outputs[Impl->MetadataIndex]->Digest;
}
const FString& FP4OriginalZenSource::LeaseEpoch() const
{
    return Impl->Reader->LeaseEpoch();
}
const FString& FP4OriginalZenSource::RetocSha256() const
{
    return Impl->Reader->RetocSha256();
}
uint64 FP4OriginalZenSource::HeldOutputBytes() const
{
    uint64 Total = 0;
    for (const auto& File : Impl->Outputs)
        Total += uint64(File->Bytes.Num());
    return Total;
}
const FString& FP4OriginalZenSource::CustodyDirectory() const
{
    return Impl->Root;
}

struct FP4OriginalZenSourceBatch::FImpl
{
    uint32 MaxEntries;
    uint64 MaxBytes, Bytes = 0;
    FP4CurrentGameRegistryPtr Reader;
    FP4ExportContainerTools Tools;
    FString Paks, Epoch;
    TMap<FString, FP4OriginalZenSourcePtr> Sources;
    FImpl(uint32 Count, uint64 Limit) : MaxEntries(Count), MaxBytes(Limit) {}
};
FP4OriginalZenSourceBatch::FP4OriginalZenSourceBatch(uint32 Count, uint64 Limit) : Impl(MakeUnique<FImpl>(Count, Limit))
{}
FP4OriginalZenSourceBatch::~FP4OriginalZenSourceBatch() = default;
uint32 FP4OriginalZenSourceBatch::Entries() const
{
    return uint32(Impl->Sources.Num());
}
uint64 FP4OriginalZenSourceBatch::HeldOutputBytes() const
{
    return Impl->Bytes;
}
bool FP4OriginalZenSourceBatch::Acquire(const FP4CurrentGameRegistryPtr& Reader, const FP4ExportContainerTools& Tools,
    const FString& Paks, const FString& Package, const FString& WorkParent, FP4OriginalZenSourcePtr& Out,
    FString& Error)
{
    Out.Reset();
    Error.Reset();
    if (!Reader)
    {
        Error = TEXT("original-zen-batch-current-reader-required");
        return false;
    }
    if (!Reader->IsCurrent(Tools, Paks, Error))
        return false;
    const FString Selected = P4OriginalZenPrivate::Full(Paks);
    if (Impl->Reader &&
        (Impl->Reader.Get() != Reader.Get() || !P4ExactNativeString(Impl->Epoch, Reader->LeaseEpoch()) ||
            !P4ExactNativeString(Impl->Tools.Retoc, Tools.Retoc) ||
            !P4ExactNativeString(Impl->Tools.Repak, Tools.Repak) || !FPaths::IsSamePath(Impl->Paks, Selected)))
    {
        Error = TEXT("original-zen-batch-selected-inputs-changed");
        return false;
    }
    if (const auto* Prior = Impl->Sources.Find(Package))
    {
        if (!P4ExactNativeString((*Prior)->Package(), Package))
        {
            Error = TEXT("original-zen-batch-package-case-changed");
            return false;
        }
        if (!(*Prior)->Current(Error))
            return false;
        Out = *Prior;
    }
    // Passing the actual prior owner invokes the helper's checked reuse branch;
    // only a real miss reaches its acquisition veto and process launch.
    if (!P4AcquireOriginalZenSource(Reader, Tools, Paks, Package, WorkParent, Out, Error))
        return false;
    if (!Impl->Reader)
    {
        Impl->Reader = Reader;
        Impl->Tools = Tools;
        Impl->Paks = Selected;
        Impl->Epoch = Reader->LeaseEpoch();
    }
    if (!Impl->Sources.Contains(Package) && uint32(Impl->Sources.Num()) < Impl->MaxEntries &&
        Out->HeldOutputBytes() <= Impl->MaxBytes - Impl->Bytes)
    {
        Impl->Bytes += Out->HeldOutputBytes();
        Impl->Sources.Add(Package, Out);
    }
    return true;
}

bool P4AcquireOriginalZenSource(const FP4CurrentGameRegistryPtr& Reader, const FP4ExportContainerTools& Tools,
    const FString& Paks, const FString& Package, const FString& WorkParent, FP4OriginalZenSourcePtr& Out,
    FString& Error, const TFunction<bool()>& Cancel)
{
    auto Prior = MoveTemp(Out);
    Out.Reset();
    Error.Reset();
    if (!Reader || !Reader->IsCurrent(Tools, Paks, Error))
    {
        if (Error.IsEmpty())
            Error = TEXT("original-zen-current-reader-required");
        return false;
    }
    if (!FMath::IsFinite(Tools.TimeoutSeconds) || Tools.TimeoutSeconds <= 0 || Tools.TimeoutSeconds > 600)
    {
        Error = TEXT("original-zen-invalid-tool-timeout");
        return false;
    }
    if (!P4IsValidContentPackage(Package) || Package.Contains(TEXT(".")) || Package.Len() > 1024)
    {
        Error = TEXT("original-zen-canonical-game-package-required");
        return false;
    }
    TArray<FString> MountedNames;
    if (!Reader->PackageNames(MountedNames, Error))
        return false;
    bool Found = false;
    for (const FString& Name : MountedNames)
        if (P4ExactNativeString(Name, Package))
        {
            Found = true;
            break;
        }
    if (!Found)
    {
        Error = TEXT("original-zen-package-not-in-current-registry");
        return false;
    }
    const FString Work = P4OriginalZenPrivate::Full(WorkParent),
                  Saved = P4OriginalZenPrivate::Full(FPaths::ProjectSavedDir());
    const FString Selected = P4OriginalZenPrivate::Full(Paks);
    if (Work.IsEmpty() || !FPaths::IsUnderDirectory(Work, Saved) || FPaths::IsUnderDirectory(Work, Selected) ||
        FPaths::IsUnderDirectory(Selected, Work) || FPaths::IsSamePath(Work, Selected) ||
        !P4OriginalZenPrivate::NoAliases(Work))
    {
        Error = TEXT("original-zen-work-must-be-disjoint-owned-saved-directory");
        return false;
    }
    if (Cancel && Cancel())
    {
        Error = TEXT("original-zen-cancelled-before-acquisition");
        return false;
    }
    if (Prior && Prior->Impl->Reader.Get() == Reader.Get() && P4ExactNativeString(Prior->Package(), Package) &&
        Prior->Impl->Tools.Retoc == Tools.Retoc && Prior->Impl->Tools.Repak == Tools.Repak &&
        FPaths::IsSamePath(Prior->Impl->Paks, Selected) && Prior->Current(Error))
    {
        Out = MoveTemp(Prior);
        return true;
    }
    Prior.Reset();
    Error.Reset();
#if P4_KADAN_TEST_SEAMS
    if (!P4KadanSourceAcquisitionAllowed({Package}, Error))
        return false;
#endif
#if !PLATFORM_WINDOWS
    Error = TEXT("original-zen-immutable-file-custody-unavailable");
    return false;
#else
    auto State = MakeUnique<FP4OriginalZenSource::FImpl>();
    State->Reader = Reader;
    State->Tools = Tools;
    State->Paks = Selected;
    State->Package = Package;
    State->Id = P4ExportPackageId(Package);
    State->Stem = FPackageName::GetShortName(Package);
    if (!IFileManager::Get().MakeDirectory(*Work, true) || !P4OriginalZenPrivate::NoAliases(Work))
    {
        Error = TEXT("original-zen-cannot-create-owned-work-parent");
        return false;
    }
    State->Root = Work / FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if (!CreateDirectoryW(*State->Root, nullptr))
    {
        State->Root.Reset();
        Error = TEXT("original-zen-cannot-create-exclusive-custody-directory");
        return false;
    }
    State->DirectoryLease = MakeUnique<P4RegistryLease::FInput>();
    if (!State->DirectoryLease->Open(std::wstring(*State->Root), true))
    {
        State->Preserve = true;
        Error = TEXT("original-zen-cannot-hold-custody-directory");
        return false;
    }
    if (!Reader->IsCurrent(Tools, Selected, Error))
        return false;
    TArray<uint8> Stdout;
    const bool ToolOkay = P4RunExportTool(P4OriginalZenPrivate::Full(Tools.Retoc),
        {TEXT("dump-test"), Selected, State->Root, FString::Printf(TEXT("%llu"), (unsigned long long)State->Id)},
        FMath::Clamp(Tools.TimeoutSeconds, 0.1, 120.0), Stdout, Error);
    // Capture even failed/timeout output, but never clean unknown members.
    FString CaptureError;
    if (!P4OriginalZenPrivate::Census(State->Root, State->Names, CaptureError))
    {
        State->Preserve = true;
        Error = CaptureError;
        return false;
    }
    const FString Raw = State->Stem + TEXT(".uasset"), Metadata = State->Stem + TEXT(".metadata.json");
    const TArray<FString> Allowed{Raw, Metadata, State->Stem + TEXT(".trace.json"), State->Stem + TEXT(".header.txt")};
    for (const FString& Name : State->Names)
    {
        bool Known = false;
        for (const FString& Candidate : Allowed)
            if (P4ExactNativeString(Name, Candidate))
            {
                Known = true;
                break;
            }
        if (!Known)
        {
            State->Preserve = true;
            Error = TEXT("original-zen-unexpected-output-retained: ") + (State->Root / Name);
            return false;
        }
        auto File = MakeUnique<P4OriginalZenPrivate::FOutput>();
        // The trace and header grow with the package: 2.0.0's ABP_Pikmin_EPIC writes a 5.7 MB trace.
        const int64 Limit = P4ExactNativeString(Name, Raw) ? int64(64) * 1024 * 1024
            : P4ExactNativeString(Name, Metadata)          ? int64(1024) * 1024
                                                           : int64(32) * 1024 * 1024;
        if (!File->Open(State->Root / Name, Limit))
        {
            State->Preserve = true;
            Error = TEXT("original-zen-cannot-bind-bounded-output: ") + Name;
            return false;
        }
        if (P4ExactNativeString(Name, Raw))
            State->RawIndex = State->Outputs.Num();
        if (P4ExactNativeString(Name, Metadata))
            State->MetadataIndex = State->Outputs.Num();
        State->Outputs.Add(MoveTemp(File));
    }
    if (Cancel && Cancel())
    {
        Error = TEXT("original-zen-cancelled-after-tool-completion");
        return false;
    }
    if (!ToolOkay)
        return false;
    if (State->RawIndex == INDEX_NONE || State->MetadataIndex == INDEX_NONE ||
        State->Outputs[State->RawIndex]->Bytes.Num() < 64 || !State->Outputs[State->MetadataIndex]->Bytes.Num())
    {
        Error = TEXT("original-zen-required-raw-metadata-output-missing-or-short");
        return false;
    }
    if (!State->Current(Error))
        return false;
    Out = MakeShareable(new FP4OriginalZenSource(MoveTemp(State)));
    return true;
#endif
}

#include "P4MaterialFamilyAcquireStage.h"
#include "P4GamePaths.h"
#include "P4KadanImport.h"
#include "P4MaterialUsedResources.h"
THIRD_PARTY_INCLUDES_START
#include "P4MapKitCore/material_family_source_validation.h"
#include "P4MapKitCore/material_binding_metadata.h"
THIRD_PARTY_INCLUDES_END
#include "Misc/PackageName.h"
#include "P4KadanContainerIndex.h"
#include "P4KadanOriginalConversion.h"
#include "P4KadanAcquisitionVeto.h"
#include "P4RegistryInputLease.h"
#include "P4ExportProcess.h"
#include "P4NativeHash.h"
#include "P4KadanImportSources.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
FString FamilyStageFull(FString Path)
{
    if (Path.IsEmpty())
        return FString();
    Path = FPaths::ConvertRelativePathToFull(Path);
    FPaths::NormalizeFilename(Path);
    FPaths::NormalizeDirectoryName(Path);
    return Path;
}
FP4ExportContainerTools FamilyStageTools(const FP4KadanImportSettings& S)
{
    FP4ExportContainerTools T;
    T.Retoc = S.Retoc;
    T.Repak = S.Repak;
    return T;
}
bool FamilyStageInside(const FString& Path, const FString& Root)
{
    return FPaths::IsUnderDirectory(FamilyStageFull(Path), FamilyStageFull(Root));
}
bool FamilyStageNoReparseAncestors(FString Path)
{
#if PLATFORM_WINDOWS
    Path = FamilyStageFull(Path);
    while (!Path.IsEmpty())
    {
        const DWORD Attributes = GetFileAttributesW(*Path);
        if (Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_REPARSE_POINT))
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
bool FamilyStageBundleAbsent(const FString& Header)
{
    for (const TCHAR* E : {TEXT("uasset"), TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl"), TEXT("m.ubulk")})
    {
        const FString P = FPaths::ChangeExtension(Header, E);
        if (IFileManager::Get().FileExists(*P) || IFileManager::Get().DirectoryExists(*P))
            return false;
    }
    return true;
}
bool FamilyStageAllowed(
    const TArray<FString>& Packages, const TFunction<bool()>& Cancel, bool& Cancelled, FString& Error)
{
    if (Cancel && Cancel())
    {
        Cancelled = true;
        Error = TEXT("Material family source preparation was cancelled between stages");
        return false;
    }
#if P4_KADAN_TEST_SEAMS
    if (!P4KadanSourceAcquisitionAllowed(Packages, Error))
        return false;
#else
    (void)Packages;
#endif
    return true;
}
struct FFamilyStageFile
{
    FString Digest;
#if PLATFORM_WINDOWS
    BY_HANDLE_FILE_INFORMATION Info{};
#endif
};
#if PLATFORM_WINDOWS
bool FamilyStageSame(const BY_HANDLE_FILE_INFORMATION& A, const BY_HANDLE_FILE_INFORMATION& B)
{
    return A.dwVolumeSerialNumber == B.dwVolumeSerialNumber && A.nFileIndexHigh == B.nFileIndexHigh &&
        A.nFileIndexLow == B.nFileIndexLow && A.nFileSizeHigh == B.nFileSizeHigh && A.nFileSizeLow == B.nFileSizeLow &&
        A.dwFileAttributes == B.dwFileAttributes && A.nNumberOfLinks == B.nNumberOfLinks &&
        CompareFileTime(&A.ftCreationTime, &B.ftCreationTime) == 0 &&
        CompareFileTime(&A.ftLastWriteTime, &B.ftLastWriteTime) == 0;
}
// The file's identity without reading its bytes. A writer still holding it open is refused, as in FamilyStageObserve.
bool FamilyStageIdentity(const FString& File, BY_HANDLE_FILE_INFORMATION& Out)
{
    const HANDLE H = CreateFileW(
        *File, FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (H == INVALID_HANDLE_VALUE)
        return false;
    const bool Okay = GetFileInformationByHandle(H, &Out) != 0;
    CloseHandle(H);
    return Okay;
}
bool FamilyStageObserve(const FString& File, FFamilyStageFile& Out)
{
    const HANDLE H = CreateFileW(
        *File, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (H == INVALID_HANDLE_VALUE)
        return false;
    BY_HANDLE_FILE_INFORMATION After{};
    int64 Size = 0;
    const bool Okay = GetFileInformationByHandle(H, &Out.Info) &&
        !(Out.Info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
        Out.Info.nNumberOfLinks == 1 && P4HashFile(File, 64 * 1024 * 1024, Size, Out.Digest) &&
        GetFileInformationByHandle(H, &After) && FamilyStageSame(Out.Info, After);
    CloseHandle(H);
    return Okay;
}
bool FamilyStageDeleteKnown(const FString& File, const FFamilyStageFile& Expected)
{
    // Hold the exact bytes against writers from hashing through deletion. A
    // pathname hash followed by a separate metadata-only delete has a gap even
    // when a writer restores the original timestamps and size.
    const HANDLE H = CreateFileW(
        *File, GENERIC_READ | DELETE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (H == INVALID_HANDLE_VALUE)
        return false;
    BY_HANDLE_FILE_INFORMATION I{}, After{};
    FILE_DISPOSITION_INFO D{1};
    FString Digest;
    const bool Bound = GetFileInformationByHandle(H, &I) && FamilyStageSame(I, Expected.Info) &&
        !(I.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) && I.nNumberOfLinks == 1 &&
        I.nFileSizeHigh == 0 && I.nFileSizeLow <= 64 * 1024 * 1024;
    const bool Hashed = Bound &&
        P4HashByteStream(
            [&](const TFunction<bool(const uint8*, int32)>& Add)
            {
                uint8 Buffer[64 * 1024];
                uint64 Total = 0;
                DWORD Read = 0;
                for (;;)
                {
                    if (!ReadFile(H, Buffer, sizeof(Buffer), &Read, nullptr))
                        return false;
                    if (!Read)
                        break;
                    Total += Read;
                    if (Total > I.nFileSizeLow || !Add(Buffer, int32(Read)))
                        return false;
                }
                return Total == I.nFileSizeLow;
            },
            Digest);
    const bool Okay = Hashed && P4ExactNativeString(Digest, Expected.Digest) && GetFileInformationByHandle(H, &After) &&
        FamilyStageSame(I, After) && SetFileInformationByHandle(H, FileDispositionInfo, &D, sizeof(D));
    CloseHandle(H);
    return Okay;
}
#else
bool FamilyStageObserve(const FString&, FFamilyStageFile&)
{
    return false;
}
bool FamilyStageDeleteKnown(const FString&, const FFamilyStageFile&)
{
    return false;
}
#endif
// Enumerate only this fresh operation's tree. Reject reparse directories before
// descending; never traverse a converter-created link into unrelated data.
bool FamilyStageCensus(const FString& Root, TArray<FString>& Files, TArray<FString>& Dirs, FString& Error)
{
    Files.Reset();
    Dirs.Reset();
    TArray<FString> Pending{Root};
    while (Pending.Num())
    {
        const FString Directory = Pending.Pop(false);
        Dirs.Add(Directory);
        if (Dirs.Num() > 16384)
        {
            Error = TEXT("Material stage directory census exceeds its bound");
            return false;
        }
        TArray<FString> Names;
        IFileManager::Get().FindFiles(Names, *(Directory / TEXT("*")), true, true);
        for (const FString& Name : Names)
        {
            const FString Path = FamilyStageFull(Directory / Name);
            if (!FamilyStageInside(Path, Root))
            {
                Error = TEXT("Material stage path escapes its owned directory");
                return false;
            }
#if PLATFORM_WINDOWS
            const DWORD A = GetFileAttributesW(*Path);
            if (A == INVALID_FILE_ATTRIBUTES || (A & FILE_ATTRIBUTE_REPARSE_POINT))
            {
                Error = TEXT("Material stage has an unreadable or reparse member");
                return false;
            }
            if (A & FILE_ATTRIBUTE_DIRECTORY)
                Pending.Add(Path);
            else
                Files.Add(Path);
#else
            Error = TEXT("Material stage file custody is unavailable on this platform");
            return false;
#endif
            if (Files.Num() > 16385)
            {
                Error = TEXT("Material stage file census exceeds its bound");
                return false;
            }
        }
    }
    Files.Sort();
    return true;
}
struct FFamilyStageCustody
{
    FString Root, Marker;
    int32 Converted = 0;
    bool Preserve = false;
    TMap<FString, FFamilyStageFile> Files;
    bool Capture(const TArray<FString>& Allowed, FString& Error)
    {
        TArray<FString> Now, Dirs;
        if (!FamilyStageCensus(Root, Now, Dirs, Error))
        {
            Preserve = true;
            return false;
        }
        // Stage families can contain thousands of files. Keep exact spelling,
        // but avoid scanning the entire allowed/census arrays for every member.
        struct FExactPath : BaseKeyFuncs<FString, FString, false>
        {
            static const FString& GetSetKey(const FString& Value) { return Value; }
            static bool Matches(const FString& A, const FString& B) { return P4ExactNativeString(A, B); }
            static uint32 GetKeyHash(const FString& Value) { return FCrc::StrCrc32(*Value); }
        };
        const TSet<FString, FExactPath> AllowedSet(Allowed), NowSet(Now);
        int64 Total = 0;
        for (const FString& File : Now)
        {
            if (!AllowedSet.Contains(File))
            {
                Preserve = true;
                Error = TEXT("Converter produced an unexpected material-stage file: ") + File;
                return false;
            }
#if PLATFORM_WINDOWS
            // A file already hashed here keeps its bytes while its identity is the same, since a write
            // moves its time. Hashing every staged file again at every check was a quarter of an import.
            BY_HANDLE_FILE_INFORMATION Seen{};
            if (const auto* Prior = Files.Find(File))
                if (FamilyStageIdentity(File, Seen) && FamilyStageSame(Prior->Info, Seen))
                {
                    Total += (int64(Seen.nFileSizeHigh) << 32) | Seen.nFileSizeLow;
                    if (Total > 256 * 1024 * 1024)
                    {
                        Preserve = true;
                        Error = TEXT("Material-stage bytes exceed their bound");
                        return false;
                    }
                    continue;
                }
#endif
            FFamilyStageFile Current;
            if (!FamilyStageObserve(File, Current))
            {
                Preserve = true;
                Error = TEXT("Cannot bind material-stage file identity/bytes: ") + File;
                return false;
            }
            Total += IFileManager::Get().FileSize(*File);
            if (Total > 256 * 1024 * 1024)
            {
                Preserve = true;
                Error = TEXT("Material-stage bytes exceed their bound");
                return false;
            }
            if (const auto* Prior = Files.Find(File))
            {
#if PLATFORM_WINDOWS
                if (!FamilyStageSame(Prior->Info, Current.Info) || !P4ExactNativeString(Prior->Digest, Current.Digest))
                {
                    Preserve = true;
                    Error = TEXT("A previous material-stage file changed");
                    return false;
                }
#else
                (void)Prior;
                Preserve = true;
                Error = TEXT("Material stage custody is unavailable on this platform");
                return false;
#endif
            }
            else
                Files.Add(File, MoveTemp(Current));
        }
        for (const auto& Prior : Files)
            if (!NowSet.Contains(Prior.Key))
            {
                Preserve = true;
                Error = TEXT("A material-stage file disappeared");
                return false;
            }
        return true;
    }
    ~FFamilyStageCustody()
    {
        if (Root.IsEmpty() || Preserve)
            return;
        TArray<FString> Allowed;
        Files.GetKeys(Allowed);
        FString Error;
        if (!Capture(Allowed, Error))
            return;
        for (const auto& Pair : Files)
            if (!P4ExactNativeString(Pair.Key, Marker) && !FamilyStageDeleteKnown(Pair.Key, Pair.Value))
            {
                UE_LOG(LogTemp, Warning, TEXT("Retaining changed material-stage custody: %s"), *Root);
                return;
            }
        if (const auto* Owner = Files.Find(Marker))
            if (!FamilyStageDeleteKnown(Marker, *Owner))
                return;
        TArray<FString> Left, Dirs;
        if (!FamilyStageCensus(Root, Left, Dirs, Error) || Left.Num())
            return;
        Dirs.Sort([](const FString& A, const FString& B) { return A.Len() > B.Len(); });
        for (const FString& Dir : Dirs)
            IFileManager::Get().DeleteDirectory(*Dir, false, false);
        IFileManager::Get().DeleteDirectory(
            *FPaths::GetPath(Root), false, false); // the stage folder, once no stage is left in it
    }
};

bool FamilyStageInitialize(FFamilyStageCustody& State, const FP4KadanImportSettings& Settings, const FString& CachePath,
    const FString& Epoch, FString& Error)
{
    if (!State.Root.IsEmpty())
        return true;
    // Each stage has a folder of its own under the stage folder, so an import can keep the stages of
    // several material families open and do each family's work once.
    const FString Base = FamilyStageFull(FPaths::ProjectSavedDir() / TEXT("Kadan/MaterialFamilyAcquireStage"));
    const FString Root = FamilyStageFull(Base / FGuid::NewGuid().ToString(EGuidFormats::Digits));
    const FString Cache = FamilyStageFull(CachePath), Paks = FamilyStageFull(Settings.GamePaks);
    if (!FamilyStageNoReparseAncestors(Base) || !FamilyStageNoReparseAncestors(Cache) ||
        !FamilyStageNoReparseAncestors(Paks) || !FamilyStageInside(Base, FamilyStageFull(FPaths::ProjectSavedDir())) ||
        FPaths::IsSamePath(Base, Cache) || FamilyStageInside(Base, Cache) || FamilyStageInside(Cache, Base) ||
        FPaths::IsSamePath(Base, Paks) || FamilyStageInside(Base, Paks) || FamilyStageInside(Paks, Base))
    {
        Error = TEXT("Material stage must be disjoint from source containers and retained cache");
        return false;
    }
    if (IFileManager::Get().FileExists(*Root) || IFileManager::Get().DirectoryExists(*Root))
    {
        Error = TEXT("Existing material stage custody must be inspected; it is never overwritten: ") + Root;
        return false;
    }
    if (!IFileManager::Get().MakeDirectory(*Base, true) || !FamilyStageNoReparseAncestors(Root))
    {
        Error = TEXT("Cannot create the owned Saved/Kadan stage parent");
        return false;
    }
#if PLATFORM_WINDOWS
    if (!CreateDirectoryW(*Root, nullptr))
#else
    if (true)
#endif
    {
        Error = TEXT("Cannot exclusively create the fresh material source stage");
        return false;
    }
    State.Root = Root;
    State.Marker = Root / TEXT("owned-stage.txt");
    const FString Owner = TEXT("p4-material-family-stage/1\n") + FGuid::NewGuid().ToString(EGuidFormats::Digits) +
        TEXT("\n") + Epoch;
    if (!FFileHelper::SaveStringToFile(Owner, *State.Marker, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        Error = TEXT("Cannot mark fresh material stage custody: ") + Root;
        return false;
    }
    return State.Capture({State.Marker}, Error);
}
struct FFamilyResourceBundle
{
    FP4KadanSourceBinding Binding;
    TArray<FString> Absent;
    TArray<TSharedPtr<P4RegistryLease::FInput>> Leases;
    bool Current(FString& Error) const
    {
        for (const auto& Lease : Leases)
            if (!Lease.IsValid() || !Lease->Current())
            {
                Error = TEXT("Shader-used resource input file identity changed");
                return false;
            }
        for (const FString& Path : Absent)
            if (IFileManager::Get().FileExists(*Path) || IFileManager::Get().DirectoryExists(*Path))
            {
                Error = TEXT("Shader-used resource acquired an unbound companion file");
                return false;
            }
        return P4KadanSourceBindingCurrent(Binding, Error);
    }
};
} //namespace

struct FP4MaterialFamilyAcquireContext::FImpl
{
    FP4KadanImportSettings Settings;
    FP4CurrentGameRegistryPtr Registry;
    FString Cache, StampDigest;
    P4RegistryLease::FInput StampLease;
    FP4KadanContainerIndex FreshIndex;
    bool HasFreshIndex = false;
    bool ReadFreshIndex(FString& Error)
    {
        if (HasFreshIndex)
            return true;
        TArray<uint8> Listing;
        if (!P4RunExportTool(FPaths::ConvertRelativePathToFull(Settings.Retoc),
                {TEXT("list"), FPaths::ConvertRelativePathToFull(Settings.GamePaks), TEXT("--path")}, 600, Listing,
                Error))
            return false;
        if (Listing.Num() == 0 || Listing.Contains(uint8(0)))
        {
            Error = TEXT("Fresh family container index is empty or binary");
            return false;
        }
        FUTF8ToTCHAR Wide(reinterpret_cast<const ANSICHAR*>(Listing.GetData()), Listing.Num());
        const FString Text(Wide.Length(), Wide.Get());
        FTCHARToUTF8 Round(*Text);
        if (Round.Length() != Listing.Num() || FMemory::Memcmp(Round.Get(), Listing.GetData(), Listing.Num()) ||
            !FreshIndex.Read(Text, Error))
        {
            if (Error.IsEmpty())
                Error = TEXT("Fresh family container index is not exact UTF-8");
            return false;
        }
        HasFreshIndex = true;
        return true;
    }
};
FP4MaterialFamilyAcquireContext::FP4MaterialFamilyAcquireContext(TUniquePtr<FImpl>&& In) : Impl(MoveTemp(In))
{}
FP4MaterialFamilyAcquireContext::~FP4MaterialFamilyAcquireContext() = default;
bool FP4MaterialFamilyAcquireContext::Current(FString& Error) const
{
    if (!IsInGameThread())
    {
        Error = TEXT("Material source staging requires the import game thread");
        return false;
    }
    if (!Impl->Registry.IsValid() ||
        !Impl->Registry->IsCurrent(FamilyStageTools(Impl->Settings), Impl->Settings.GamePaks, Error))
        return false;
    FString Cache, Hash;
    int64 Size = 0;
    if (!Impl->StampLease.Current() || !P4KadanRetainedConversionCacheCurrent(Impl->Settings, Cache, Error) ||
        !FPaths::IsSamePath(Cache, Impl->Cache) || !P4HashFile(Cache / TEXT("converted-from.txt"), 65536, Size, Hash) ||
        !P4ExactNativeString(Hash, Impl->StampDigest))
    {
        if (Error.IsEmpty())
            Error = TEXT("Material family retained cache/stamp changed");
        return false;
    }
    return true;
}
bool P4CreateMaterialFamilyAcquireContext(const FP4KadanImportSettings& Settings,
    const FP4CurrentGameRegistryPtr& Registry, FP4MaterialFamilyAcquireContextPtr& Out, FString& Error)
{
    Out.Reset();
    Error.Reset();
    if (!IsInGameThread() || !Registry.IsValid())
    {
        Error = TEXT("Material source context needs the import thread and current registry lease");
        return false;
    }
    auto State = MakeUnique<FP4MaterialFamilyAcquireContext::FImpl>();
    State->Settings = Settings;
    State->Registry = Registry;
    if (!Registry->IsCurrent(FamilyStageTools(Settings), Settings.GamePaks, Error) ||
        !P4KadanRetainedConversionCacheCurrent(Settings, State->Cache, Error))
        return false;
    State->Cache = FamilyStageFull(State->Cache);
    const FString Stamp = FamilyStageFull(State->Cache / TEXT("converted-from.txt"));
    int64 Size = 0;
#if PLATFORM_WINDOWS
    if (!State->StampLease.Open(std::wstring(*Stamp, Stamp.Len())))
#else
    if (true)
#endif
    {
        Error = TEXT("Cannot lease the existing material conversion-cache stamp");
        return false;
    }
    if (!P4HashFile(Stamp, 65536, Size, State->StampDigest))
    {
        Error = TEXT("Cannot hash the existing material conversion-cache stamp");
        return false;
    }
    FP4MaterialFamilyAcquireContextPtr Ready(new FP4MaterialFamilyAcquireContext(MoveTemp(State)));
    if (!Ready->Current(Error))
        return false;
    Out = MoveTemp(Ready);
    return true;
}

struct FP4MaterialFamilyPreparedStage::FImpl : FFamilyStageCustody
{
    FP4MaterialFamilyAcquireContextPtr Context;
    FP4MaterialFamilySourcesPtr Family;
    FP4KadanImportSettings Settings;
    TArray<TSharedPtr<FFamilyResourceBundle>> Resources;
    // The import that last found every staged and source byte unchanged: they stay so while it runs.
    int64 Checked = 0;
};
FP4MaterialFamilyPreparedStage::FP4MaterialFamilyPreparedStage(TUniquePtr<FImpl>&& In) : Impl(MoveTemp(In))
{}
FP4MaterialFamilyPreparedStage::~FP4MaterialFamilyPreparedStage() = default;
const FP4MaterialFamilySourcesPtr& FP4MaterialFamilyPreparedStage::Sources() const
{
    return Impl->Family;
}
const FString& FP4MaterialFamilyPreparedStage::StageDirectory() const
{
    return Impl->Root;
}
int32 FP4MaterialFamilyPreparedStage::ConvertedPackages() const
{
    return Impl->Converted;
}
bool FP4MaterialFamilyPreparedStage::Current(FString& Error) const
{
    const int64 Import = IsInGameThread() ? P4KadanImportGeneration() : 0;
    if (Import && Impl->Checked == Import)
    {
        Error.Reset();
        return true;
    }
    if (!Impl->Context->Current(Error))
        return false;
    for (const auto& Resource : Impl->Resources)
        if (!Resource->Current(Error))
            return false;
    if (!Impl->Family.IsValid())
    {
        Error = TEXT("Material stage lacks a complete source witness");
        return false;
    }
    if (!Impl->Root.IsEmpty())
    {
        TArray<FString> Allowed;
        Impl->Files.GetKeys(Allowed);
        if (!Impl->Capture(Allowed, Error))
            return false;
    }
    // Recheck ALL registered/cache/staged bytes at consumption, not just the
    // stage outputs or retained stamp. Sources() is not a publication.
    if (!Impl->Family->Current(Impl->Settings, Error) || !Impl->Context->Current(Error))
        return false;
    Impl->Checked = Import;
    return true;
}

EP4MaterialFamilyStageResult P4PrepareMissingMaterialFamilyStage(const FP4MaterialFamilyAcquireContextPtr& Context,
    const FString& Selection, const TArray<TSharedPtr<FP4KadanSourceAsset>>& Registered,
    const FP4KadanContainerIndex* ExistingLookup, const TFunction<bool()>& Cancel,
    FP4MaterialFamilyPreparedStagePtr& Out, FString& Error)
{
    Out.Reset();
    Error.Reset();
    bool Cancelled = false;
    const auto Failed = [&]()
    { return Cancelled ? EP4MaterialFamilyStageResult::Cancelled : EP4MaterialFamilyStageResult::Refused; };
    if (!Context.IsValid())
    {
        Error = TEXT("Material family preparation needs a current acquisition context");
        return Failed();
    }
    if (!Context->Current(Error))
        return Failed();
    if (Cancel && Cancel())
    {
        Error = TEXT("Material family preparation cancelled before reading");
        return EP4MaterialFamilyStageResult::Cancelled;
    }
    auto& C = *Context->Impl;
    FP4MaterialFamilySourcesPtr Family;
    TArray<FString> Missing;
    auto Read = P4ReadMaterialFamilySources(
        C.Settings, C.Registry, Selection, Registered, ExistingLookup, Family, Missing, Error);
    if (Read == EP4MaterialFamilySourceRead::Refused)
        return Failed();
    auto State = MakeUnique<FP4MaterialFamilyPreparedStage::FImpl>();
    State->Context = Context;
    State->Settings = C.Settings;
    if (Read == EP4MaterialFamilySourceRead::MissingSources)
    {
        if (!FamilyStageAllowed(Missing, Cancel, Cancelled, Error) || !Context->Current(Error))
            return Failed();
        if (!C.HasFreshIndex)
        {
            const bool Listed = C.ReadFreshIndex(Error);
            const FString ListingError = Error;
            if (!FamilyStageAllowed(Missing, Cancel, Cancelled, Error) || !Context->Current(Error))
                return Failed();
            if (!Listed)
            {
                Error = ListingError;
                return Failed();
            }
        }
        int64 AvailableSourceBytes = 0;
        Read = P4ReadMaterialFamilySources(C.Settings, C.Registry, Selection, Registered, &C.FreshIndex, Family,
            Missing, Error, &AvailableSourceBytes);
        if (Read == EP4MaterialFamilySourceRead::Refused)
            return Failed();
        if (Read == EP4MaterialFamilySourceRead::MissingSources)
        {
            if (!FamilyStageAllowed(Missing, Cancel, Cancelled, Error) || !Context->Current(Error))
                return Failed();
            if (!FamilyStageInitialize(*State, C.Settings, C.Cache, C.Registry->LeaseEpoch(), Error))
                return Failed();
            const FString Root = State->Root;
            TArray<TSharedPtr<FP4KadanSourceAsset>> Rows = Registered;
            const TArray<FString> Pending = Missing;
            P4MapKit::Material::FMaterialRegistryFamily RegistryFamily;
            if (!C.Registry->Family(Selection, RegistryFamily, Error) || !Context->Current(Error))
                return Failed();
            // Every missing member is checked first and then converted in one retoc run (per chunk):
            // one run per member spent most of a cold import starting retoc.
            TArray<FString> Relatives, Headers, Allowed;
            State->Files.GetKeys(Allowed);
            for (const FString& Package : Pending)
            {
                if (!FamilyStageAllowed({Package}, Cancel, Cancelled, Error) || !Context->Current(Error))
                    return Failed();
                FString Relative;
                if (!C.FreshIndex.Resolve(Package, Relative, Error))
                    return Failed();
                if (!P4ExactNativeString(FPaths::GetExtension(Relative), TEXT("uasset")))
                {
                    Error = TEXT("Material family conversion requires an exact mounted uasset header: ") + Package;
                    return Failed();
                }
                const FString Header = FamilyStageFull(Root / Relative);
                if (!FamilyStageInside(Header, Root) || !FamilyStageBundleAbsent(Header) ||
                    !FamilyStageBundleAbsent(C.Cache / Relative))
                {
                    Error = TEXT("Material acquisition requires wholly absent stage/cache bundles: ") + Package;
                    return Failed();
                }
                for (const TCHAR* E : {TEXT("uasset"), TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl"), TEXT("m.ubulk")})
                    Allowed.Add(FPaths::ChangeExtension(Header, E));
                Relatives.Add(Relative);
                Headers.Add(Header);
            }
            {
                if (!FamilyStageAllowed(Pending, Cancel, Cancelled, Error) || !Context->Current(Error))
                    return Failed();
                const bool Converted = P4KadanConvertMountedOriginals(C.Settings, Relatives, Root, Error);
                const FString ConversionError = Error;
                FString CensusError;
                if (!State->Capture(Allowed, CensusError))
                {
                    Error = CensusError + TEXT("; custody retained at ") + Root;
                    return Failed();
                }
                if (!Converted)
                {
                    Error = ConversionError;
                    return Failed();
                }
            }
            for (int32 Member = 0; Member < Pending.Num(); ++Member)
            {
                const FString& Package = Pending[Member];
                const FString& Header = Headers[Member];
                if (!FamilyStageAllowed({Package}, Cancel, Cancelled, Error) || !Context->Current(Error))
                    return Failed();
                if (!P4KadanExtractedPackageReady(Header, Package, Error))
                    return Failed();
                const auto Expected = std::find_if(RegistryFamily.Members.begin(), RegistryFamily.Members.end(),
                    [&](const P4MapKit::Material::FMaterialParentRow& Member)
                    { return P4ExactNativeString(UTF8_TO_TCHAR(Member.Package.c_str()), Package); });
                if (Expected == RegistryFamily.Members.end())
                {
                    Error = TEXT("Converted package is outside the exact current registry family");
                    return Failed();
                }
                auto Row = MakeShared<FP4KadanSourceAsset>();
                Row->Package = Package;
                Row->Header = Header;
                Row->Body = FPaths::ChangeExtension(Header, TEXT("uexp"));
                Row->Kind = UTF8_TO_TCHAR(Expected->Class.c_str());
                const int64 HeaderBytes = IFileManager::Get().FileSize(*Row->Header),
                            BodyBytes = IFileManager::Get().FileSize(*Row->Body);
                if (AvailableSourceBytes < 0 || AvailableSourceBytes > 128 * 1024 * 1024 || HeaderBytes <= 0 ||
                    BodyBytes <= 0 || HeaderBytes > 64 * 1024 * 1024 || BodyBytes > 64 * 1024 * 1024 ||
                    HeaderBytes > 128 * 1024 * 1024 - AvailableSourceBytes ||
                    BodyBytes > 128 * 1024 * 1024 - AvailableSourceBytes - HeaderBytes)
                {
                    Error = TEXT(
                        "Material family converted source exceeds the remaining byte budget or is unavailable");
                    return Failed();
                }
                FP4KadanSourceBinding Binding;
                {
                    FP4KadanImportSourceScope Scope({Row});
                    if (!P4KadanReadBoundSource(Package, Binding, Error))
                        return Failed();
                }
                P4MapKit::Material::FMaterialFamilyRawSource Raw;
                Raw.Header = std::make_shared<const std::vector<uint8>>(
                    Binding.Header.GetData(), Binding.Header.GetData() + Binding.Header.Num());
                Raw.Body = std::make_shared<const std::vector<uint8>>(
                    Binding.Body.GetData(), Binding.Body.GetData() + Binding.Body.Num());
                P4MapKit::Material::FMaterialFamilySourceMember Checked;
                P4MapKit::FReadError SourceError;
                std::size_t Charged = std::size_t(AvailableSourceBytes);
                if (!P4MapKit::Material::ValidateMaterialFamilySourceMember(
                        *Expected, Raw, Charged, Checked, SourceError))
                {
                    Error = UTF8_TO_TCHAR(SourceError.Code.c_str());
                    return Failed();
                }
                if (!P4ExactNativeString(UTF8_TO_TCHAR(Checked.SourceSha256.c_str()), Binding.SourceDigest))
                {
                    Error = TEXT("Converted material member proof differs from its current bound source snapshot");
                    return Failed();
                }
                if (!P4KadanSourceBindingCurrent(Binding, Error) || !Context->Current(Error))
                    return Failed();
                AvailableSourceBytes = int64(Charged);
                Rows.Add(Row);
                ++State->Converted;
            }
            // No partial family has been published. Re-read and prove every
            // registered/cache/staged member and cross-member owner chain once.
            Read = P4ReadMaterialFamilySources(
                C.Settings, C.Registry, Selection, Rows, &C.FreshIndex, Family, Missing, Error);
            if (Read == EP4MaterialFamilySourceRead::Refused)
                return Failed();
        }
    }
    if (Read != EP4MaterialFamilySourceRead::Ready || !Family.IsValid() || !Family->Current(C.Settings, Error) ||
        !Context->Current(Error))
        return Failed();
    if (Cancel && Cancel())
    {
        Error = TEXT("Material family preparation cancelled before witness");
        return EP4MaterialFamilyStageResult::Cancelled;
    }
    State->Family = Family;
    FP4MaterialFamilyPreparedStagePtr Ready(new FP4MaterialFamilyPreparedStage(MoveTemp(State)));
    if (!Ready->Current(Error))
        return Failed();
    Out = MoveTemp(Ready);
    Error.Reset();
    return EP4MaterialFamilyStageResult::Ready;
}

EP4MaterialFamilyStageResult FP4MaterialFamilyPreparedStage::PrepareUsedResources(const FP4MaterialUsedResources& Used,
    const TArray<TSharedPtr<FP4KadanSourceAsset>>& Registered, const TFunction<bool()>& Cancel,
    TArray<TSharedPtr<FP4KadanSourceAsset>>& Out, FString& Error) const
{
    Out.Reset();
    Error.Reset();
    bool Cancelled = false;
    const auto Failed = [&]()
    { return Cancelled ? EP4MaterialFamilyStageResult::Cancelled : EP4MaterialFamilyStageResult::Refused; };
    if (!Current(Error))
        return Failed();
    auto& C = *Impl->Context->Impl;
    if (Used.Status != EP4MaterialUsedResourceStatus::Complete ||
        !P4ExactNativeString(Used.FamilySourceSetSha256, Impl->Family->SourceSetSha256()) ||
        !P4ExactNativeString(Used.RegistryLeaseEpoch, Impl->Family->RegistryLeaseEpoch()) ||
        !P4ExactNativeString(Used.Master, Impl->Family->Master()) ||
        Used.Owners.Num() != Impl->Family->ShaderOwnerCount())
    {
        Error = TEXT("Shader-used requests are not bound to this complete current family");
        return Failed();
    }
    TSet<FString> ExpectedOwners, ObservedOwners;
    for (const auto& Pair : Impl->Family->ShaderOwners())
        ExpectedOwners.Add(Pair.Value);
    for (const auto& Owner : Used.Owners)
    {
        const FP4KadanSourceBinding* Binding = nullptr;
        for (const auto& Source : Impl->Family->Bindings())
            if (Source.Package.Equals(Owner.Owner, ESearchCase::CaseSensitive))
            {
                Binding = &Source;
                break;
            }
        if (!ExpectedOwners.Contains(Owner.Owner) || ObservedOwners.Contains(Owner.Owner) || !Binding ||
            !P4ExactNativeString(Binding->SourceDigest, Owner.OwnerSourceSha256))
        {
            Error = TEXT("Shader-used owner coverage/source binding differs");
            return Failed();
        }
        ObservedOwners.Add(Owner.Owner);
    }
    return PrepareResourceInputs(Used.Requests, Registered, Cancel, Out, Error);
}

namespace
{
bool ReadBindingMetadataRequests(const FP4MaterialFamilySources& Family,
    const TArray<TSharedPtr<FP4KadanSourceAsset>>& Registered, const TFunction<bool()>& Cancel,
    TArray<FP4MaterialUsedResourceRequest>& Inputs, FString& Error)
{
    namespace M = P4MapKit::Material;
    namespace C = P4MapKit::CustomMaterial;
    TMap<FName, const FP4KadanSourceBinding*> Bound;
    for (const auto& Binding : Family.Bindings())
        Bound.Add(FName(*Binding.Package), &Binding);
    TMap<FName, TSharedPtr<FP4KadanSourceAsset>> Rows;
    TSet<FName> Ambiguous;
    for (const auto& Row : Registered)
        if (Row.IsValid())
        {
            const FName Key(*Row->Package);
            if (Rows.Contains(Key))
                Ambiguous.Add(Key);
            else
                Rows.Add(Key, Row);
        }
    FP4KadanImportSourceScope Scope(Registered);
    const M::MaterialOriginalImportRawResolver Raw =
        [&](const std::string& Package, M::MaterialOriginalImportRawAsset& Value)
    {
        const FString Name = UTF8_TO_TCHAR(Package.c_str());
        const FName Key(*Name);
        FP4KadanSourceBinding Read;
        const FP4KadanSourceBinding* Source = nullptr;
        if (const auto* Existing = Bound.Find(Key))
            Source = *Existing;
        else
        {
            if (Ambiguous.Contains(Key))
                throw M::Refused("registered binding metadata is ambiguous", "material binding metadata source");
            if (!Rows.Contains(Key))
                return false;
            FString Why;
            if (!P4KadanReadBoundSource(Name, Read, Why))
                throw M::Refused(TCHAR_TO_UTF8(*Why), "material binding metadata source");
            Source = &Read;
        }
        if (!Source->Package.Equals(Name, ESearchCase::CaseSensitive))
            throw M::Refused("binding metadata source case identity differs", "material binding metadata source");
        Value = {
            std::make_shared<const C::Bytes>(Source->Header.GetData(), Source->Header.GetData() + Source->Header.Num()),
            std::make_shared<const C::Bytes>(Source->Body.GetData(), Source->Body.GetData() + Source->Body.Num())};
        return true;
    };
    TMap<FString, FP4MaterialUsedResourceRequest> Requests;
    TSet<FString> Owners;
    for (const auto& Pair : Family.ShaderOwners())
        Owners.Add(Pair.Value);
    try
    {
        for (const FString& Owner : Owners)
        {
            if (Cancel && Cancel())
            {
                Error = TEXT("Binding metadata acquisition cancelled");
                return false;
            }
            const auto Missing = M::DiscoverOriginalMaterialBindingMetadata(TCHAR_TO_UTF8(*Owner), Raw);
            for (const auto& Required : Missing)
            {
                const FString Package = UTF8_TO_TCHAR(Required.Package.c_str());
                auto& Request = Requests.FindOrAdd(Package);
                const FString Object = UTF8_TO_TCHAR(Required.Object.c_str()),
                              Class = UTF8_TO_TCHAR(Required.NativeClass.c_str());
                if (!Request.Object.IsEmpty() &&
                    (!Request.Object.Equals(Object, ESearchCase::CaseSensitive) ||
                        !Request.NativeClass.Equals(Class, ESearchCase::CaseSensitive)))
                    throw M::Refused("fixed metadata requests conflict", "material binding metadata identity");
                Request.Package = Package;
                Request.Object = Object;
                Request.NativeClass = Class;
                Request.Kind = Class == TEXT("/Script/Engine.MaterialParameterCollection")
                    ? EP4MaterialUsedResourceKind::Collection
                    : EP4MaterialUsedResourceKind::Texture;
                FP4MaterialResourceOwnerUse Use;
                Use.Owner = Owner;
                Use.OwnerSourceSha256 = UTF8_TO_TCHAR(Required.OwnerSourceSha256.c_str());
                for (const auto& Ref : Required.References)
                    Use.References.Add({UTF8_TO_TCHAR(Ref.SourcePackage.c_str()),
                        UTF8_TO_TCHAR(Ref.SourceSha256.c_str()), UTF8_TO_TCHAR(Ref.Property.c_str()),
                        UTF8_TO_TCHAR(Ref.Object.c_str()), UTF8_TO_TCHAR(Ref.NativeClass.c_str()), Ref.ImportIndex});
                Request.Owners.Add(MoveTemp(Use));
            }
        }
    }
    catch (const std::exception& Failure)
    {
        Error = UTF8_TO_TCHAR(Failure.what());
        return false;
    }
    Inputs.Reset();
    Requests.GenerateValueArray(Inputs);
    Inputs.Sort([](const auto& A, const auto& B) { return A.Package < B.Package; });
    return true;
}
} //namespace
bool P4MaterialFamilyBindingMetadataRequired(const FP4KadanImportSettings& Settings,
    const FP4MaterialFamilySources& Family, const TArray<TSharedPtr<FP4KadanSourceAsset>>& Registered, bool& Required,
    FString& Error)
{
    Required = false;
    if (!Family.Current(Settings, Error))
        return false;
    TArray<FP4MaterialUsedResourceRequest> Inputs;
    if (!ReadBindingMetadataRequests(Family, Registered, {}, Inputs, Error) || !Family.Current(Settings, Error))
        return false;
    Required = Inputs.Num() != 0;
    return true;
}
EP4MaterialFamilyStageResult FP4MaterialFamilyPreparedStage::PrepareBindingMetadata(
    const TArray<TSharedPtr<FP4KadanSourceAsset>>& Registered, const TFunction<bool()>& Cancel,
    TArray<TSharedPtr<FP4KadanSourceAsset>>& Out, FString& Error) const
{
    Out.Reset();
    Error.Reset();
    if (!Current(Error))
        return EP4MaterialFamilyStageResult::Refused;
    TArray<FP4MaterialUsedResourceRequest> Inputs;
    if (!ReadBindingMetadataRequests(*Impl->Family, Registered, Cancel, Inputs, Error) || !Current(Error))
        return Cancel && Cancel() ? EP4MaterialFamilyStageResult::Cancelled : EP4MaterialFamilyStageResult::Refused;
    return PrepareResourceInputs(Inputs, Registered, Cancel, Out, Error);
}

// Shared byte acquisition AFTER the caller has established either complete graph evidence or the
// internally decoded anonymous-uniform metadata prerequisite. This helper never imports a UObject.
EP4MaterialFamilyStageResult FP4MaterialFamilyPreparedStage::PrepareResourceInputs(
    const TArray<FP4MaterialUsedResourceRequest>& Requests, const TArray<TSharedPtr<FP4KadanSourceAsset>>& Registered,
    const TFunction<bool()>& Cancel, TArray<TSharedPtr<FP4KadanSourceAsset>>& Out, FString& Error) const
{
    Out.Reset();
    Error.Reset();
    bool Cancelled = false;
    const auto Failed = [&]()
    { return Cancelled ? EP4MaterialFamilyStageResult::Cancelled : EP4MaterialFamilyStageResult::Refused; };
    if (!Current(Error))
        return Failed();
    auto& C = *Impl->Context->Impl;
    TSet<FString> ObservedOwners;
    for (const auto& Pair : Impl->Family->ShaderOwners())
        ObservedOwners.Add(Pair.Value);
    TMap<FName, TSharedPtr<FP4KadanSourceAsset>> Known;
    TSet<FName> Ambiguous, Seen;
    for (const auto& Row : Registered)
        if (Row.IsValid())
        {
            const FName Key(*Row->Package);
            if (Known.Contains(Key))
                Ambiguous.Add(Key);
            else
                Known.Add(Key, Row);
        }
    struct FResourceInput
    {
        const FP4MaterialUsedResourceRequest* Request;
        FString Header, Body;
    };
    TArray<FResourceInput> Inputs;
    TArray<FString> MissingPackages, MissingHeaders, MissingRelatives;
    // Resolve and validate the whole request set before starting a converter. A resource can be
    // shared by many family members; retoc's container startup must not be paid once per texture.
    for (const auto& Request : Requests)
    {
        const FName Key(*Request.Package);
        const bool Texture = Request.Kind == EP4MaterialUsedResourceKind::Texture;
        const bool TextureClass = P4ExactNativeString(Request.NativeClass, TEXT("/Script/Engine.Texture2D")) ||
            P4ExactNativeString(Request.NativeClass, TEXT("/Script/Engine.TextureCube")) ||
            P4ExactNativeString(Request.NativeClass, TEXT("/Script/Engine.Texture2DArray")) ||
            P4ExactNativeString(Request.NativeClass, TEXT("/Script/Engine.VolumeTexture")) ||
            P4ExactNativeString(Request.NativeClass, TEXT("/Script/Engine.TextureRenderTarget2D")) ||
            P4ExactNativeString(Request.NativeClass, TEXT("/Script/MediaAssets.MediaTexture"));
        if (Seen.Contains(Key) || Ambiguous.Contains(Key) || !P4IsValidContentPackage(Request.Package) ||
            !P4ExactNativeString(Request.Object, P4ObjectPathOf(Request.Package)) || (Texture && !TextureClass) ||
            (!Texture && Request.NativeClass != TEXT("/Script/Engine.MaterialParameterCollection")) ||
            !Request.Owners.Num())
        {
            Error = TEXT("Shader-used resource request identity is invalid or ambiguous");
            return Failed();
        }
        for (const auto& Owner : Request.Owners)
            if (!ObservedOwners.Contains(Owner.Owner))
            {
                Error = TEXT("Shader-used resource consumer is outside this validated family");
                return Failed();
            }
        Seen.Add(Key);
        if (!FamilyStageAllowed({Request.Package}, Cancel, Cancelled, Error) || !Current(Error))
            return Failed();
        FString Header, Body;
        if (const auto* Bound = Known.Find(Key))
        {
            if (!P4ExactNativeString((*Bound)->Package, Request.Package))
            {
                Error = TEXT("Registered shader-used resource canonical spelling differs");
                return Failed();
            }
            Header = (*Bound)->Header;
            Body = (*Bound)->Body;
        }
        else
        {
            // A fresh list under the still-held current input epoch, not an
            // old caller index or mtime-only cache entry, locates the pair.
            const bool Listed = C.ReadFreshIndex(Error);
            const FString ListingError = Error;
            if (!FamilyStageAllowed({Request.Package}, Cancel, Cancelled, Error) || !Current(Error))
                return Failed();
            if (!Listed)
            {
                Error = ListingError;
                return Failed();
            }
            FString Relative;
            if (!C.FreshIndex.Resolve(Request.Package, Relative, Error))
                return Failed();
            if (!P4ExactNativeString(FPaths::GetExtension(Relative), TEXT("uasset")))
            {
                Error = TEXT("Shader-used resource requires an exact mounted uasset header");
                return Failed();
            }
            Header = FamilyStageFull(C.Cache / Relative);
            Body = FPaths::ChangeExtension(Header, TEXT("uexp"));
            if (!FamilyStageBundleAbsent(Header))
            {
                if (!P4KadanExtractedPackageReady(Header, Request.Package, Error))
                    return Failed();
                // Partial sidecars/header or wrong identity are never missing.
            }
            else
            {
                if (!FamilyStageAllowed({Request.Package}, Cancel, Cancelled, Error) || !Current(Error) ||
                    !FamilyStageInitialize(*Impl, C.Settings, C.Cache, C.Registry->LeaseEpoch(), Error))
                    return Failed();
                Header = FamilyStageFull(Impl->Root / Relative);
                Body = FPaths::ChangeExtension(Header, TEXT("uexp"));
                if (!FamilyStageInside(Header, Impl->Root) || !FamilyStageBundleAbsent(Header) ||
                    !FamilyStageBundleAbsent(C.Cache / Relative))
                {
                    Error = TEXT("Shader-used conversion requires wholly absent stage/cache bundles");
                    return Failed();
                }
                MissingPackages.Add(Request.Package);
                MissingHeaders.Add(Header);
                MissingRelatives.Add(Relative);
            }
        }
        Inputs.Add({&Request, Header, Body});
    }
    if (MissingPackages.Num())
    {
        TArray<FString> Allowed;
        Impl->Files.GetKeys(Allowed);
        for (int32 Index = 0; Index < MissingHeaders.Num(); ++Index)
        {
            // Recheck every destination after planning. A partial or newly occupied bundle
            // must never become eligible just because it was absent earlier in the call.
            if (!FamilyStageBundleAbsent(MissingHeaders[Index]) ||
                !FamilyStageBundleAbsent(C.Cache / MissingRelatives[Index]))
            {
                Error = TEXT("Shader-used conversion requires wholly absent stage/cache bundles");
                return Failed();
            }
            for (const TCHAR* E : {TEXT("uasset"), TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl"), TEXT("m.ubulk")})
                Allowed.Add(FPaths::ChangeExtension(MissingHeaders[Index], E));
        }
        if (!FamilyStageAllowed(MissingPackages, Cancel, Cancelled, Error) || !Current(Error))
            return Failed();
        const bool Converted = P4KadanConvertMountedOriginals(C.Settings, MissingRelatives, Impl->Root, Error);
        const FString ConversionError = Error;
        FString CensusError;
        if (!Impl->Capture(Allowed, CensusError))
        {
            Error = CensusError + TEXT("; custody retained at ") + Impl->Root;
            return Failed();
        }
        if (!FamilyStageAllowed(MissingPackages, Cancel, Cancelled, Error) || !Current(Error))
            return Failed();
        if (!Converted)
        {
            Error = ConversionError;
            return Failed();
        }
        Impl->Converted += MissingPackages.Num();
    }
    for (const auto& Input : Inputs)
    {
        const auto& Request = *Input.Request;
        const FName Key(*Request.Package);
        const FString& Header = Input.Header;
        const FString& Body = Input.Body;
        if (!FamilyStageAllowed({Request.Package}, Cancel, Cancelled, Error) || !Current(Error))
            return Failed();
        auto Row = MakeShared<FP4KadanSourceAsset>();
        Row->Package = Request.Package;
        Row->Header = Header;
        Row->Body = Body;
        auto Resource = MakeShared<FFamilyResourceBundle>();
        // Lease every actual native-read companion, including mapped bulk.
        // Absent companions are checked again at every consumption boundary.
        for (const TCHAR* E : {TEXT("uasset"), TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl"), TEXT("m.ubulk")})
        {
            const FString Path = FamilyStageFull(FPaths::ChangeExtension(Header, E));
            if (IFileManager::Get().DirectoryExists(*Path) || !FamilyStageNoReparseAncestors(Path))
            {
                Error = TEXT("Shader-used resource companion is a directory or reparse path");
                return Failed();
            }
            if (IFileManager::Get().FileExists(*Path))
            {
                auto Lease = MakeShared<P4RegistryLease::FInput>();
#if PLATFORM_WINDOWS
                if (!Lease->Open(std::wstring(*Path, Path.Len())) || Lease->Size() > 64 * 1024 * 1024)
#else
                if (true)
#endif
                {
                    Error = TEXT("Cannot hold bounded shader-used resource companion bytes");
                    return Failed();
                }
                Resource->Leases.Add(Lease);
                Row->SourceBytes += int64(Lease->Size());
            }
            else
                Resource->Absent.Add(Path);
        }
        {
            FP4KadanImportSourceScope Scope({Row});
            if (!P4KadanReadBoundSource(Request.Package, Resource->Binding, Error))
                return Failed();
        }
        const auto& Binding = Resource->Binding;
        const auto Inventory = P4MapKit::ReadPackage(
            std::vector<uint8>(Binding.Header.GetData(), Binding.Header.GetData() + Binding.Header.Num()),
            std::vector<uint8>(Binding.Body.GetData(), Binding.Body.GetData() + Binding.Body.Num()));
        bool Root = false;
        const FString Leaf = FPackageName::GetShortName(Request.Package);
        for (const auto& Export : Inventory.Exports)
            if (Export.Outer == 0 && P4ExactNativeString(FString(UTF8_TO_TCHAR(Export.Name.c_str())), Leaf))
            {
                std::string Class;
                Root = Inventory.Reference(Export.Class, Class) &&
                    P4ExactNativeString(FString(UTF8_TO_TCHAR(Class.c_str())), Request.NativeClass);
                break;
            }
        if (!Inventory.Error.Code.empty() || !Root ||
            (!Request.SourceSha256.IsEmpty() && !P4ExactNativeString(Request.SourceSha256, Binding.SourceDigest)))
        {
            Error = TEXT("Shader-used resource canonical root/class/current source differs");
            return Failed();
        }
        int32 ClassDot = INDEX_NONE;
        Request.NativeClass.FindLastChar(TEXT('.'), ClassDot);
        Row->Kind = Request.NativeClass.Mid(ClassDot + 1);
        // Preserve the same actual import inventory as the ordinary scanner.
        // Resource execution never manufactures an empty Dependency array.
        TSet<FName> Hard, Soft;
        for (const auto& Import : Inventory.Imports)
        {
            const FString Name = UTF8_TO_TCHAR(Import.Name.c_str());
            const FName Identity(*Name);
            if (Import.Outer == 0 && P4IsContentPackageIgnoringCase(Name) && Identity != Key &&
                !Hard.Contains(Identity))
            {
                Hard.Add(Identity);
                Row->Dependencies.Add(Name);
            }
        }
        for (const auto& Reference : Inventory.SoftPackageReferences)
        {
            const FString Name = UTF8_TO_TCHAR(Reference.c_str());
            const FName Identity(*Name);
            if (P4IsValidContentPackageIgnoringCase(Name) && Identity != Key && !Hard.Contains(Identity) &&
                !Soft.Contains(Identity))
            {
                Soft.Add(Identity);
                Row->Dependencies.Add(Name);
                Row->SoftDependencies.Add(Name);
            }
        }
        Row->Dependencies.Sort();
        Row->SoftDependencies.Sort();
        if (!Resource->Current(Error) || !Current(Error))
            return Failed();
        Impl->Resources.Add(Resource);
        Out.Add(Row);
    }
    if (!Current(Error) || !FamilyStageAllowed({}, Cancel, Cancelled, Error))
    {
        Out.Reset();
        return Failed();
    }
    return EP4MaterialFamilyStageResult::Ready;
}

#if P4_KADAN_TEST_SEAMS
#include "Misc/ScopeExit.h"
bool P4MaterialFamilyStageControlsForTest(TArray<FString>& Errors)
{
#if !PLATFORM_WINDOWS
    Errors.Add(TEXT("Material stage custody is unavailable on this platform"));
    return false;
#else
    const auto TestTrue = [&](const TCHAR* Label, bool Value)
    {
        if (!Value)
            Errors.Add(Label);
        return Value;
    };
    const auto TestFalse = [&](const TCHAR* Label, bool Value) { return TestTrue(Label, !Value); };
    const auto TestEqual = [&](const TCHAR* Label, int32 A, int32 B) { return TestTrue(Label, A == B); };
    // Only tiny independently-created test bytes. No registry/tool/game/cache
    // inputs are required, and cleanup never recursively deletes a directory.
    for (int32 Case = 0; Case < 6; ++Case)
    {
        const FString Root = FamilyStageFull(FPaths::ProjectSavedDir() / TEXT("P4MapKit/MaterialFamilyStageControls") /
            FGuid::NewGuid().ToString(EGuidFormats::Digits));
        const FString Marker = Root / TEXT("owned-stage.txt"), Header = Root / TEXT("tiny.uasset"),
                      Extra = Root / TEXT("unexpected.txt"), Link = Root / TEXT("alias.txt"),
                      Mapped = Root / TEXT("tiny.m.ubulk");
        if (!TestFalse(TEXT("Fresh control root is absent"), IFileManager::Get().DirectoryExists(*Root)) ||
            !TestTrue(TEXT("Create bounded control directory"), IFileManager::Get().MakeDirectory(*Root, true)))
            return false;
        ON_SCOPE_EXIT
        {
            // These exact names were created exclusively by this test.
            for (const FString& File : {Link, Extra, Mapped, Header, Marker})
                if (IFileManager::Get().FileExists(*File))
                    IFileManager::Get().Delete(*File, false, true, true);
            IFileManager::Get().DeleteDirectory(*Root, false, false);
        };
        const auto Write = [](const FString& File, const FString& Text)
        { return FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM); };
        TestTrue(TEXT("A genuinely absent bundle is eligible for missing-only acquisition"),
            FamilyStageBundleAbsent(Header));
        if (Case == 0)
        {
            if (!TestTrue(TEXT("Create a mapped-bulk-only native input control"),
                    Write(Mapped, TEXT("actual-mapped-bulk-control"))))
                return false;
            TestFalse(
                TEXT("A mapped-bulk-only bundle is partial, never wholly absent"), FamilyStageBundleAbsent(Header));
            if (!TestTrue(TEXT("Remove only the exact independently owned mapped control"),
                    IFileManager::Get().Delete(*Mapped)))
                return false;
        }
        if (!TestTrue(TEXT("Create tiny independently owned controls"),
                Write(Marker, TEXT("control-owner")) && Write(Header, TEXT("original-control-bytes"))))
            return false;
        TestFalse(TEXT("A partial existing header is never missing-only"), FamilyStageBundleAbsent(Header));
        auto Custody = MakeUnique<FFamilyStageCustody>();
        Custody->Root = Root;
        Custody->Marker = Marker;
        FString Error;
        if (!TestTrue(TEXT("Bind exact output census and identities"), Custody->Capture({Marker, Header}, Error)))
            return false;
        if (Case == 1)
        {
            if (!TestTrue(TEXT("Change own output for refusal control"), Write(Header, TEXT("changed-control-bytes"))))
                return false;
            TestFalse(TEXT("Changed bytes cannot retain the prior witness"), Custody->Capture({Marker, Header}, Error));
        }
        if (Case == 2)
        {
            if (!TestTrue(TEXT("Create unexpected output control"), Write(Extra, TEXT("unrelated-output"))))
                return false;
            TestFalse(TEXT("An unexpected converter output refuses the whole census"),
                Custody->Capture({Marker, Header}, Error));
        }
        if (Case == 3)
        {
            if (!TestTrue(TEXT("Remove own output for missing control"),
                    IFileManager::Get().Delete(*Header, false, true, true)))
                return false;
            TestFalse(
                TEXT("A disappeared output cannot retain the witness"), Custody->Capture({Marker, Header}, Error));
        }
        if (Case == 4)
        {
            if (!TestTrue(TEXT("Create own hardlink control"), CreateHardLinkW(*Link, *Header, nullptr) != 0))
                return false;
            TestFalse(TEXT("A multiply-linked output refuses exclusive stage custody"),
                Custody->Capture({Marker, Header, Link}, Error));
        }
        if (Case == 5)
        {
            const auto Expected = Custody->Files.FindChecked(Header);
            if (!TestTrue(TEXT("Change own output without changing its byte count"),
                    Write(Header, TEXT("modified-control-bytes"))))
                return false;
            const HANDLE H = CreateFileW(*Header, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL, nullptr);
            if (!TestTrue(TEXT("Open own timestamp restoration control"), H != INVALID_HANDLE_VALUE))
                return false;
            const bool Restored = SetFileTime(
                                      H, &Expected.Info.ftCreationTime, nullptr, &Expected.Info.ftLastWriteTime) != 0;
            CloseHandle(H);
            if (!TestTrue(TEXT("Restore exact prior metadata for byte-only tamper control"),
                    Restored && SetFileAttributesW(*Header, Expected.Info.dwFileAttributes) != 0))
                return false;
            FFamilyStageFile Changed;
            if (!TestTrue(TEXT("Tamper preserves all compared file metadata"),
                    FamilyStageObserve(Header, Changed) && FamilyStageSame(Changed.Info, Expected.Info)))
                return false;
            TestFalse(TEXT("Deletion refuses changed bytes even with exact original metadata"),
                FamilyStageDeleteKnown(Header, Expected));
            TestTrue(TEXT("Byte-only tamper file remains present"), IFileManager::Get().FileExists(*Header));
            // A check trusts an unchanged identity, since an ordinary write moves the time; a writer that puts back the
            // exact times passes it. Deletion above still hashes the bytes and refuses them.
            TestTrue(TEXT("A check keeps the witness of an unchanged file identity"),
                Custody->Capture({Marker, Header}, Error));
        }
        Custody.Reset();
        if (Case == 0)
            TestFalse(TEXT("Only unchanged recognized files and empty own directories are removed"),
                IFileManager::Get().DirectoryExists(*Root));
        else
            TestTrue(TEXT("Any changed/unexpected/missing/link output preserves stage custody"),
                IFileManager::Get().FileExists(*Marker));
        if (Case == 2)
            TestTrue(TEXT("Unexpected bytes are preserved"), IFileManager::Get().FileExists(*Extra));
        if (Case == 4)
            TestTrue(TEXT("Linked bytes are preserved"),
                IFileManager::Get().FileExists(*Header) && IFileManager::Get().FileExists(*Link));
    }
    FString Error;
    bool Cancelled = false;
    TestFalse(TEXT("Cancellation refuses before the next tool or mutation"),
        FamilyStageAllowed({TEXT("/Game/Control")}, []() { return true; }, Cancelled, Error));
    TestTrue(TEXT("Cancellation remains a distinct result"), Cancelled);
    FP4KadanScopedAcquisitionVeto Veto(FPaths::ProjectDir());
    if (!TestTrue(TEXT("Control owns the acquisition veto"), Veto.IsActive()))
        return false;
    Cancelled = false;
    Error.Reset();
    TestFalse(TEXT("Dev acquisition veto refuses the same production stage boundary"),
        FamilyStageAllowed({TEXT("/Game/Control")}, []() { return false; }, Cancelled, Error));
    TestFalse(TEXT("A veto is refusal, not cancellation"), Cancelled);
    TestEqual(TEXT("Veto records the attempted package once"), Veto.DeniedBatches(), 1);
    return Errors.Num() == 0;
#endif
}
#endif

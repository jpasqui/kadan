#include "P4CurrentGameRegistry.h"
#include "P4GameBuild.h"
#include "P4GameNames.h"
#include "P4RegistryInputLease.h"
#include "P4RegistryCanonicalRows.h"
#include "P4ExportProcess.h"
#include "P4NativeHash.h"
#include "P4KadanImportSources.h"
#include "P4MapKitCore/export_registry.h"
#include "P4MapKitCore/material_family_registry.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Misc/SecureHash.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/WindowsHWrapper.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
FString RegistryFullPath(const FString& Path)
{
    if (Path.IsEmpty() || Path.Contains(TEXT("\"")))
        return FString();
    for (TCHAR C : Path)
        if (!C)
            return FString();
    FString Full = FPaths::ConvertRelativePathToFull(Path);
    FPaths::NormalizeFilename(Full);
    if (!FPaths::CollapseRelativeDirectories(Full))
        return FString();
    FPaths::NormalizeDirectoryName(Full);
    return Full;
}
bool RegistryContainerNames(const FString& Paks, TArray<FString>& Out, FString& Error)
{
    Out.Reset();
    for (const TCHAR* Pattern : {TEXT("*.pak"), TEXT("*.utoc"), TEXT("*.ucas")})
    {
        TArray<FString> Found;
        IFileManager::Get().FindFiles(Found, *(Paks / Pattern), true, false);
        Out.Append(Found);
    }
    Out.Sort();
    if (Out.Num() > 256)
    {
        Error = TEXT("Registry source container set exceeds its lease bound");
        return false;
    }
    const FString Stem = P4GameContainerIn(Paks);
    for (const FString& Name : {Stem + TEXT(".pak"), Stem + TEXT(".utoc"), Stem + TEXT(".ucas")})
        if (!Out.Contains(Name) || IFileManager::Get().FileSize(*(Paks / Name)) <= 0)
        {
            Error = TEXT("Registry source requires the actual game pak and matching IoStore pair");
            return false;
        }
    return true;
}
bool SameRegistryNames(const TArray<FString>& A, const TArray<FString>& B)
{
    if (A.Num() != B.Num())
        return false;
    for (int32 I = 0; I < A.Num(); ++I)
        if (!P4ExactNativeString(A[I], B[I]))
            return false;
    return true;
}
#if PLATFORM_WINDOWS
// Getting the registry out of the game's pak and decoding it took about a minute of every import, and every
// editor session did it again. The decoded stream of the same pak file and the same two tools is kept in the
// user's local folder, shared by all projects. A changed pak (size, time or file) or tool gives another file.
const char RegistryCacheMagic[] = "kadan-registry-cache-1\n";
FString RegistryCacheFile(const FString& Pak, const FString& RetocDigest, const FString& RepakDigest)
{
    const HANDLE H = CreateFileW(*Pak, FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (H == INVALID_HANDLE_VALUE)
        return FString();
    BY_HANDLE_FILE_INFORMATION I{};
    const bool Known = GetFileInformationByHandle(H, &I) != 0;
    CloseHandle(H);
    if (!Known)
        return FString();
    const FString Key = FString::Printf(TEXT("%s|%u|%u|%u|%u|%u|%u|%u|%s|%s|%s"), *Pak, I.dwVolumeSerialNumber,
        I.nFileIndexHigh, I.nFileIndexLow, I.nFileSizeHigh, I.nFileSizeLow, I.ftLastWriteTime.dwHighDateTime,
        I.ftLastWriteTime.dwLowDateTime, *RetocDigest, *RepakDigest, FP4CurrentGameRegistry::MaterialSchema());
    return FPaths::Combine(
        FPlatformProcess::UserSettingsDir(), TEXT("Kadan/RegistryCache"), FMD5::HashAnsiString(*Key) + TEXT(".bin"));
}
// The raw registry's digest and the decoded stream, or false when the file is absent or not one of these.
bool ReadRegistryCache(const FString& File, FString& RawDigest, TArray<uint8>& Stream)
{
    TArray<uint8> Data;
    const int32 Magic = int32(sizeof(RegistryCacheMagic)) - 1;
    if (!FFileHelper::LoadFileToArray(Data, *File, FILEREAD_Silent) || Data.Num() < Magic + 65 ||
        FMemory::Memcmp(Data.GetData(), RegistryCacheMagic, Magic) != 0 || Data[Magic + 64] != '\n')
        return false;
    RawDigest = FString(64, reinterpret_cast<const ANSICHAR*>(Data.GetData() + Magic));
    for (TCHAR C : RawDigest)
        if (!FChar::IsHexDigit(C))
            return false;
    Stream.Append(Data.GetData() + Magic + 65, Data.Num() - Magic - 65);
    return Stream.Num() > 0;
}
void WriteRegistryCache(const FString& File, const FString& RawDigest, const TArray<uint8>& Stream)
{
    if (File.IsEmpty() || RawDigest.Len() != 64 || !Stream.Num())
        return;
    TArray<uint8> Data;
    Data.Append(reinterpret_cast<const uint8*>(RegistryCacheMagic), int32(sizeof(RegistryCacheMagic)) - 1);
    const FTCHARToUTF8 Digest(*RawDigest);
    Data.Append(reinterpret_cast<const uint8*>(Digest.Get()), Digest.Length());
    Data.Add('\n');
    Data.Append(Stream);
    const FString Temporary = File + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
    if (FFileHelper::SaveArrayToFile(Data, *Temporary) && !IFileManager::Get().Move(*File, *Temporary, true, true))
        IFileManager::Get().Delete(*Temporary, false, true, true);
}
bool HashRegistryRows(const P4MapKit::Material::FMaterialParentRegistry& Rows, FString& Digest)
{
    return P4HashByteStream([&](const TFunction<bool(const uint8*, int32)>& Update)
        { return P4RegistryLease::CanonicalRows(Rows, Update); }, Digest);
}
#endif
} //namespace

struct FP4CurrentGameRegistry::FImpl
{
    FP4ExportContainerTools Tools;
    FString Paks, RawDigest, RetocDigest, RepakDigest, RowsDigest, RowsError, Epoch;
    TArray<FString> Containers, Names;
    TArray<TUniquePtr<P4RegistryLease::FInput>> Inputs;
    P4MapKit::Material::FMaterialParentRegistry Materials;
    // The import that last found the inputs and containers unchanged: they stay so while it runs.
    mutable int64 Checked = 0;
    bool Current(FString& Error) const
    {
        const int64 Import = IsInGameThread() ? P4KadanImportGeneration() : 0;
        if (Import && Checked == Import)
        {
            Error.Reset();
            return true;
        }
        for (const auto& Input : Inputs)
            if (!Input->Current())
            {
                Error = TEXT("Registry input lease changed or no longer resolves to the selected file");
                return false;
            }
        TArray<FString> Now;
        if (!RegistryContainerNames(Paks, Now, Error))
            return false;
        if (!SameRegistryNames(Containers, Now))
        {
            Error = TEXT("Registry source container set changed during its lease");
            return false;
        }
        Error.Reset();
        Checked = Import;
        return true;
    }
};

FP4CurrentGameRegistry::FP4CurrentGameRegistry(TUniquePtr<FImpl>&& In) : Impl(MoveTemp(In))
{}
FP4CurrentGameRegistry::~FP4CurrentGameRegistry() = default;
const TCHAR* FP4CurrentGameRegistry::MaterialSchema()
{
    return TEXT("p4-retoc-added-dependency-flags-material-parents/1");
}
const FString& FP4CurrentGameRegistry::RegistrySha256() const
{
    return Impl->RawDigest;
}
const FString& FP4CurrentGameRegistry::RetocSha256() const
{
    return Impl->RetocDigest;
}
const FString& FP4CurrentGameRegistry::RepakSha256() const
{
    return Impl->RepakDigest;
}
const FString& FP4CurrentGameRegistry::MaterialRowsSha256() const
{
    return Impl->RowsDigest;
}
const FString& FP4CurrentGameRegistry::MaterialError() const
{
    return Impl->RowsError;
}
const FString& FP4CurrentGameRegistry::LeaseEpoch() const
{
    return Impl->Epoch;
}
bool FP4CurrentGameRegistry::IsCurrent(const FP4ExportContainerTools& Tools, const FString& Paks, FString& Error) const
{
    if (!FPaths::IsSamePath(RegistryFullPath(Paks), Impl->Paks) ||
        !FPaths::IsSamePath(RegistryFullPath(Tools.Retoc), Impl->Tools.Retoc) ||
        !FPaths::IsSamePath(RegistryFullPath(Tools.Repak), Impl->Tools.Repak))
    {
        Error = TEXT("Registry lease belongs to a different selected game or tool");
        return false;
    }
    return Impl->Current(Error);
}
bool FP4CurrentGameRegistry::PackageNames(TArray<FString>& Out, FString& Error) const
{
    Out.Reset();
    if (!Impl->Current(Error))
        return false;
    Out = Impl->Names;
    if (Impl->Current(Error))
        return true;
    Out.Reset();
    return false;
}
bool FP4CurrentGameRegistry::MaterialPackages(
    TArray<FString>& Masters, TArray<FString>& Instances, FString& Error) const
{
    Masters.Reset();
    Instances.Reset();
    if (!Impl->Current(Error))
        return false;
    if (!Impl->RowsError.IsEmpty())
    {
        Error = Impl->RowsError;
        return false;
    }
    for (const auto& Pair : Impl->Materials.Rows())
    {
        const FString Package = UTF8_TO_TCHAR(Pair.second.Package.c_str());
        if (Pair.second.Class == "Material")
            Masters.AddUnique(Package);
        else if (Pair.second.Class == "MaterialInstanceConstant")
            Instances.AddUnique(Package);
    }
    Masters.Sort();
    Instances.Sort();
    return Impl->Current(Error);
}
bool FP4CurrentGameRegistry::Family(
    const FString& Selection, P4MapKit::Material::FMaterialRegistryFamily& Out, FString& Error) const
{
    Out = {};
    if (!Impl->Current(Error))
        return false;
    if (!Impl->RowsError.IsEmpty())
    {
        Error = Impl->RowsError;
        return false;
    }
    std::string Why;
    if (!Impl->Materials.Family(TCHAR_TO_UTF8(*Selection), Out, Why))
    {
        Error = UTF8_TO_TCHAR(Why.c_str());
        return false;
    }
    if (Impl->Current(Error))
        return true;
    Out = {};
    return false;
}

bool P4ReadCurrentGameRegistryImpl(const FP4ExportContainerTools& Tools, const FString& Paks, const FString& Work,
    FP4CurrentGameRegistryPtr& Out, FString& Error, bool AllowAcquisition)
{
#if !PLATFORM_WINDOWS
    (void)Tools;
    (void)Paks;
    (void)Work;
    (void)AllowAcquisition;
    Out.Reset();
    Error = TEXT("Immutable current-registry input leases are unavailable on this platform");
    return false;
#else
    auto Prior = MoveTemp(Out);
    Out.Reset();
    Error.Reset();
    if (!FMath::IsFinite(Tools.TimeoutSeconds) || Tools.TimeoutSeconds <= 0 || Tools.TimeoutSeconds > 600)
    {
        Error = TEXT("Registry tool timeout is invalid");
        return false;
    }
    // Weak ownership retains no locks after the last explicit consumer releases them.
    static FCriticalSection Guard;
    static TWeakPtr<const FP4CurrentGameRegistry, ESPMode::ThreadSafe> Last;
    FScopeLock Lock(&Guard);
    if (Prior && Prior->IsCurrent(Tools, Paks, Error))
    {
        Out = MoveTemp(Prior);
        return true;
    }
    Prior.Reset(); // Invalid ownership must not retain otherwise unneeded leases during reacquisition.
    if (auto Held = Last.Pin())
        if (Held->IsCurrent(Tools, Paks, Error))
        {
            Out = Held;
            return true;
        }
    Error.Reset();
    auto Result = MakeUnique<FP4CurrentGameRegistry::FImpl>();
    Result->Paks = RegistryFullPath(Paks);
    Result->Tools = Tools;
    Result->Tools.Retoc = RegistryFullPath(Tools.Retoc);
    Result->Tools.Repak = RegistryFullPath(Tools.Repak);
    if (Result->Paks.IsEmpty() || Result->Tools.Retoc.IsEmpty() || Result->Tools.Repak.IsEmpty())
    {
        Error = TEXT("Registry source and tools must have valid paths");
        return false;
    }
    const auto Lease = [&](const FString& Path, bool Directory = false)
    {
        auto Input = MakeUnique<P4RegistryLease::FInput>();
        if (!Input->Open(std::wstring(*Path, Path.Len()), Directory))
        {
            Error = TEXT("Cannot obtain immutable registry input lease: ") + Path;
            return false;
        }
        Result->Inputs.Add(MoveTemp(Input));
        return true;
    };
    if (!Lease(Result->Paks, true) || !RegistryContainerNames(Result->Paks, Result->Containers, Error))
        return false;
    for (const FString& Name : Result->Containers)
        if (!Lease(Result->Paks / Name))
            return false;
    if (!Lease(Result->Tools.Retoc) || !Lease(Result->Tools.Repak))
        return false;
    int64 ToolSize = 0;
    if (!P4HashFile(Result->Tools.Retoc, int64(512) * 1024 * 1024, ToolSize, Result->RetocDigest) || ToolSize <= 0 ||
        !P4HashFile(Result->Tools.Repak, int64(512) * 1024 * 1024, ToolSize, Result->RepakDigest) || ToolSize <= 0)
    {
        Error = TEXT("Registry tool bytes are unreadable or exceed their bound");
        return false;
    }
    const FString CacheFile = RegistryCacheFile(
        Result->Paks / (P4GameContainerIn(Result->Paks) + TEXT(".pak")), Result->RetocDigest, Result->RepakDigest);
    {
        TArray<uint8> Stream;
        FString RawDigest;
        if (!CacheFile.IsEmpty() && ReadRegistryCache(CacheFile, RawDigest, Stream))
        {
            P4MapKit::Export::RegistryPackageLines Names;
            bool MaterialOkay = true, Fed = true;
            for (int32 At = 0; At < Stream.Num() && Fed; At += 1024 * 1024)
            {
                const int32 Size = FMath::Min(1024 * 1024, Stream.Num() - At);
                Fed = Names.Feed(Stream.GetData() + At, Size);
                if (Fed && MaterialOkay)
                    MaterialOkay = Result->Materials.Feed(Stream.GetData() + At, Size);
            }
            if (!Fed || !Names.Finish() || !Result->Current(Error))
            {
                if (!AllowAcquisition)
                {
                    if (Error.IsEmpty())
                        Error = TEXT("The retained current registry cache is invalid; acquisition is disabled.");
                    return false;
                }
                // Not the stream a decode gives: drop it and decode the game's registry again.
                IFileManager::Get().Delete(*CacheFile, false, true, true);
                Error.Reset();
                Lock.Unlock();
                return P4AcquireCurrentGameRegistry(Tools, Paks, Work, Out, Error);
            }
            Result->RawDigest = RawDigest;
            for (const auto& Name : Names.Names)
                Result->Names.Add(UTF8_TO_TCHAR(Name.c_str()));
            if (MaterialOkay)
                MaterialOkay = Result->Materials.Finish();
            if (!MaterialOkay)
                Result->RowsError = UTF8_TO_TCHAR(Result->Materials.Error().c_str());
            else if (!HashRegistryRows(Result->Materials, Result->RowsDigest))
                Result->RowsError = TEXT("Cannot bind canonical material registry rows");
            Result->Epoch = FGuid::NewGuid().ToString(EGuidFormats::Digits);
            FP4CurrentGameRegistryPtr Published(new FP4CurrentGameRegistry(MoveTemp(Result)));
            Last = Published;
            Out = MoveTemp(Published);
            return true;
        }
    }
    if (!AllowAcquisition)
    {
        Error = TEXT("The retained current registry cache is unavailable; acquisition is disabled.");
        return false;
    }
    if (!IFileManager::Get().MakeDirectory(*Work, true))
    {
        Error = TEXT("Cannot create registry inspection scratch");
        return false;
    }
    const FString File = FPaths::Combine(
        Work, TEXT("registry-") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".bin"));
    struct FCleanup
    {
        FString File;
        ~FCleanup() { IFileManager::Get().Delete(*File); }
    } Cleanup{File};
    TUniquePtr<FArchive> Writer(IFileManager::Get().CreateFileWriter(*File));
    if (!Writer)
    {
        Error = TEXT("Cannot stage actual game registry");
        return false;
    }
    const TFunction<bool(const uint8*, int32, FString&)> Write = [&](const uint8* Part, int32 Size, FString& Why)
    {
        Writer->Serialize(const_cast<uint8*>(Part), Size);
        if (Writer->IsError())
        {
            Why = TEXT("Cannot stream actual game registry bytes");
            return false;
        }
        return true;
    };
    const bool Extracted = P4RunExportToolStream(Result->Tools.Repak,
        {TEXT("get"), Result->Paks / (P4GameContainerIn(Result->Paks) + TEXT(".pak")), TEXT(P4_GAME_PROJECT "/AssetRegistry.bin")},
        Tools.TimeoutSeconds, Write, int64(256) * 1024 * 1024, Error);
    const bool Written = !Writer->IsError() && Writer->Close() && !Writer->IsError();
    Writer.Reset();
    if (!Extracted)
        return false;
    if (!Written)
    {
        Error = TEXT("Cannot finish actual game registry staging");
        return false;
    }
    P4RegistryLease::FInput Raw;
    int64 RegistrySize = 0;
    if (!Raw.Open(std::wstring(*File, File.Len())) ||
        !P4HashFile(File, int64(256) * 1024 * 1024, RegistrySize, Result->RawDigest) || RegistrySize <= 0)
    {
        Error = TEXT("Actual game registry is empty, unreadable or cannot be leased");
        return false;
    }
    P4MapKit::Export::RegistryPackageLines Names;
    bool MaterialOkay = true;
    TArray<uint8> Decoded;
    const TFunction<bool(const uint8*, int32, FString&)> Decode = [&](const uint8* Part, int32 Size, FString& Why)
    {
        if (Decoded.Num() < int32(512) * 1024 * 1024)
            Decoded.Append(Part, Size);
        if (!Names.Feed(Part, Size))
        {
            Why = UTF8_TO_TCHAR(Names.Error.c_str());
            return false;
        }
        // Strict family failure never changes the established name-only export contract.
        if (MaterialOkay)
            MaterialOkay = Result->Materials.Feed(Part, Size);
        return true;
    };
    if (!P4RunExportToolStream(Result->Tools.Retoc, {TEXT("asset-registry"), File}, Tools.TimeoutSeconds, Decode,
            int64(1024) * 1024 * 1024, Error) ||
        !Names.Finish())
    {
        if (Error.IsEmpty())
            Error = UTF8_TO_TCHAR(Names.Error.c_str());
        return false;
    }
    if (!Raw.Current() || !Result->Current(Error))
    {
        if (Error.IsEmpty())
            Error = TEXT("Actual registry changed during decoding");
        return false;
    }
    if (Decoded.Num() < int32(512) * 1024 * 1024)
        WriteRegistryCache(CacheFile, Result->RawDigest, Decoded);
    for (const auto& Name : Names.Names)
        Result->Names.Add(UTF8_TO_TCHAR(Name.c_str()));
    if (MaterialOkay)
        MaterialOkay = Result->Materials.Finish();
    if (!MaterialOkay)
        Result->RowsError = UTF8_TO_TCHAR(Result->Materials.Error().c_str());
    else if (!HashRegistryRows(Result->Materials, Result->RowsDigest))
        Result->RowsError = TEXT("Cannot bind canonical material registry rows");
    Result->Epoch = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FP4CurrentGameRegistryPtr Published(new FP4CurrentGameRegistry(MoveTemp(Result)));
    Last = Published;
    Out = MoveTemp(Published);
    return true;
#endif
}

// Existing public acquisition retains its original behavior and ABI.
bool P4AcquireCurrentGameRegistry(const FP4ExportContainerTools& Tools, const FString& Paks, const FString& Work,
    FP4CurrentGameRegistryPtr& Out, FString& Error)
{
    return P4ReadCurrentGameRegistryImpl(Tools, Paks, Work, Out, Error, true);
}

bool P4ReadRetainedCurrentGameRegistry(
    const FP4ExportContainerTools& Tools, const FString& Paks, FP4CurrentGameRegistryPtr& Out, FString& Error)
{
    return P4ReadCurrentGameRegistryImpl(Tools, Paks, FString(), Out, Error, false);
}

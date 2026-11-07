#if WITH_DEV_AUTOMATION_TESTS
#include "P4RetainedMaterialFixture.h"
#include "P4KadanTestSeams.h"
#include "P4ExactFixtureMove.h"
#include "P4KadanAcquisitionVeto.h"
#include "P4KadanImport.h"
#include "P4MaterialRecovery.h"
#include "P4MaterialGraph.h"
#include "P4MaterialInstance.h"
#include "P4MaterialCandidateSourceProof.h"
#include "P4NativeHash.h"
#include "P4OriginalRecord.h"
#include "AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MaterialShared.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/SecureHash.h"
#include "PackageTools.h"
#include "RHI.h"
#include "Serialization/JsonSerializer.h"
#include "ShaderCompiler.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include <tlhelp32.h>
#endif

// An opt-in owned-fixture transaction, never an editor startup hook or general
// original-asset upgrade. Silhouette is a preserved input, not a repair target:
// stock UE's material uniform buffer cannot read its original main-stencil input.
namespace P4RetainedShadowRepair
{
namespace Proof = P4RetainedMaterialCandidateProof;
const FString Target = TEXT("/Game/Carrot4/MasterMaterial/chr/M_chr_Shadow_Master");
const FString Object = Target + TEXT(".M_chr_Shadow_Master");
const FString Other = TEXT("/Game/Carrot4/MasterMaterial/Unique/Silhouette/M_Silhouette_00_Master");
const FString Cave = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00");
FString Normalize(FString Path)
{
    Path = FPaths::ConvertRelativePathToFull(Path);
    FPaths::CollapseRelativeDirectories(Path);
    FPaths::NormalizeDirectoryName(Path);
    return Path;
}
FString LedgerFile()
{
    return Normalize(FPaths::ProjectSavedDir() /
        TEXT("P4MapKit/MaterialRecoveryCandidates/RetainedBeforeRepair/shadow-transaction.json"));
}
FString BundleStem(const FString& Package)
{
    return Normalize(FPackageName::LongPackageNameToFilename(Package));
}

bool NoReparse(const FString& File)
{
#if PLATFORM_WINDOWS
    FString At = Normalize(File);
    while (!At.IsEmpty())
    {
        const DWORD Attr = GetFileAttributesW(*At);
        if (Attr != INVALID_FILE_ATTRIBUTES && (Attr & FILE_ATTRIBUTE_REPARSE_POINT))
            return false;
        const FString Parent = FPaths::GetPath(At);
        if (Parent == At)
            break;
        At = Parent;
    }
    return true;
#else
    return false;
#endif
}
bool TransactionPath(const FString& File, bool Directory = false)
{
    const FString Path = Normalize(File), Root = FPaths::GetPath(LedgerFile());
    if (Directory)
        return (Path == Root || Path == Root / TEXT("OldShadow") || Path == Root / TEXT("FailedNew")) &&
            NoReparse(Path);
    if (Path == LedgerFile() || Path == LedgerFile() + TEXT(".tmp"))
        return NoReparse(Path);
    for (const TCHAR* Ext : {TEXT(".uasset"), TEXT(".uexp"), TEXT(".ubulk"), TEXT(".uptnl")})
    {
        const FString Name = FPaths::GetCleanFilename(BundleStem(Target)) + Ext;
        if (Path == BundleStem(Target) + Ext || Path == Root / TEXT("OldShadow") / Name ||
            Path == Root / TEXT("FailedNew") / Name)
            return NoReparse(Path);
    }
    return false;
}
bool MakeTransactionDirectory(const FString& Directory)
{
    return TransactionPath(Directory, true) && !IFileManager::Get().FileExists(*Directory) &&
        IFileManager::Get().MakeDirectory(*Directory, true) && TransactionPath(Directory, true);
}
bool MoveTarget(const FString& From, const FString& To)
{
    const FString A = Normalize(From), B = Normalize(To), Root = FPaths::GetPath(LedgerFile());
    bool Route = false;
    for (const TCHAR* Ext : {TEXT(".uasset"), TEXT(".uexp"), TEXT(".ubulk"), TEXT(".uptnl")})
    {
        const FString Live = BundleStem(Target) + Ext, Name = FPaths::GetCleanFilename(Live),
                      Old = Root / TEXT("OldShadow") / Name, Failed = Root / TEXT("FailedNew") / Name;
        Route = Route || (A == Live && (B == Old || B == Failed)) || (A == Old && B == Live);
    }
    if (!Route || !TransactionPath(From) || !TransactionPath(To) || !IFileManager::Get().FileExists(*From) ||
        IFileManager::Get().FileExists(*To))
        return false;
#if PLATFORM_WINDOWS
    const auto Hash = [](const std::wstring& Path, std::uint64_t Limit, std::string& Out)
    {
        int64 Size = 0;
        FString Digest;
        if (!P4HashFile(FString(Path.c_str()), int64(Limit), Size, Digest))
            return false;
        const FTCHARToUTF8 Utf8(*Digest);
        Out.assign(Utf8.Get(), Utf8.Length());
        return true;
    };
    const std::wstring Source(*A, A.Len()), Destination(*B, B.Len());
    P4ExactFixtureMove::FSnapshot Before;
    if (!P4ExactFixtureMove::Capture(Source, Hash, Before))
    {
        UE_LOG(LogTemp, Display, TEXT("Shadow exact move source capture refused: %s"), *A);
        return false;
    }
    const auto Result = P4ExactFixtureMove::Move(Source, Destination, Before, Hash);
    if (!Result.Exact)
        UE_LOG(LogTemp, Display,
            TEXT("Shadow exact move refused stage=%s moved=%d attributes_restored=%d winerror=%u from=%s to=%s"),
            UTF8_TO_TCHAR(Result.Error.c_str()), Result.Moved, Result.AttrsRestored, Result.WinError, *A, *B);
    return Result.Exact;
#else
    return false;
#endif
}
bool Read(const FString& File, TSharedPtr<FJsonObject>& Json)
{
    FString Text;
    return File == LedgerFile() && TransactionPath(File) && IFileManager::Get().FileSize(*File) > 0 &&
        IFileManager::Get().FileSize(*File) <= 1024 * 1024 && FFileHelper::LoadFileToString(Text, *File) &&
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) && Json.IsValid();
}
bool Save(const FString& File, const TSharedRef<FJsonObject>& Json)
{
#if PLATFORM_WINDOWS
    const FString Temp = File + TEXT(".tmp");
    FString Text;
    if (File != LedgerFile() || !TransactionPath(File) || !TransactionPath(Temp) ||
        IFileManager::Get().FileExists(*Temp) || !FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text)))
        return false;
    if (IFileManager::Get().FileExists(*File))
    {
        TSharedPtr<FJsonObject> Prior;
        if (!Read(File, Prior) || Prior->GetStringField(TEXT("owner")) != TEXT("p4-owned-shadow-only-repair-v1") ||
            Prior->GetStringField(TEXT("project")) != Normalize(FPaths::ProjectDir()) ||
            Prior->GetStringField(TEXT("package")) != Target)
            return false;
    }
    const FTCHARToUTF8 Encoded(*Text);
    if (Encoded.Length() <= 0 || Encoded.Length() > 1024 * 1024 || !MakeTransactionDirectory(FPaths::GetPath(File)) ||
        !FFileHelper::SaveStringToFile(Text, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) ||
        !TransactionPath(Temp))
        return false;
    const HANDLE Written = CreateFileW(
        *Temp, GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (Written == INVALID_HANDLE_VALUE)
        return false;
    const bool Flushed = FlushFileBuffers(Written) != 0;
    CloseHandle(Written);
    // One fixed temporary file; a failed write/rename remains evidence and blocks
    // further phase writes. Never truncate the last valid transaction receipt.
    return Flushed && TransactionPath(Temp) && TransactionPath(File) &&
        MoveFileExW(*Temp, *File, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return false;
#endif
}
bool FileState(const FString& File, TSharedPtr<FJsonObject>& Out)
{
    Out = MakeShared<FJsonObject>();
    Out->SetStringField(TEXT("path"), Normalize(File));
    const bool Exists = IFileManager::Get().FileExists(*File);
    Out->SetBoolField(TEXT("exists"), Exists);
    if (!NoReparse(File))
        return false;
    if (!Exists)
        return true;
#if PLATFORM_WINDOWS
    const HANDLE Handle = CreateFileW(*Normalize(File), FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (Handle == INVALID_HANDLE_VALUE)
        return false;
    BY_HANDLE_FILE_INFORMATION Before{}, After{};
    bool Good = GetFileInformationByHandle(Handle, &Before) != 0;
    int64 Size = 0;
    FString Hash;
    Good = Good && !(Before.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
        P4HashFile(File, 64ll * 1024 * 1024, Size, Hash) && GetFileInformationByHandle(Handle, &After) &&
        Before.dwVolumeSerialNumber == After.dwVolumeSerialNumber && Before.nFileIndexHigh == After.nFileIndexHigh &&
        Before.nFileIndexLow == After.nFileIndexLow && Before.dwFileAttributes == After.dwFileAttributes &&
        CompareFileTime(&Before.ftLastWriteTime, &After.ftLastWriteTime) == 0 &&
        Before.nFileSizeHigh == After.nFileSizeHigh && Before.nFileSizeLow == After.nFileSizeLow;
    CloseHandle(Handle);
    if (!Good)
        return false;
    Out->SetStringField(TEXT("sha256"), Hash);
    Out->SetNumberField(TEXT("bytes"), double(Size));
    Out->SetStringField(TEXT("id"),
        FString::Printf(TEXT("%u:%llu"), Before.dwVolumeSerialNumber,
            (uint64(Before.nFileIndexHigh) << 32) | Before.nFileIndexLow));
    Out->SetStringField(TEXT("written"),
        FString::Printf(TEXT("%llu"),
            (uint64(Before.ftLastWriteTime.dwHighDateTime) << 32) | Before.ftLastWriteTime.dwLowDateTime));
    Out->SetStringField(TEXT("created"),
        FString::Printf(
            TEXT("%llu"), (uint64(Before.ftCreationTime.dwHighDateTime) << 32) | Before.ftCreationTime.dwLowDateTime));
    Out->SetNumberField(TEXT("attributes"), Before.dwFileAttributes);
    Out->SetNumberField(TEXT("links"), Before.nNumberOfLinks);
    return true;
#else
    return false;
#endif
}
bool SameFile(const TSharedPtr<FJsonObject>& Before, const FString& At = FString())
{
    if (!Before.IsValid())
        return false;
    const FString File = At.IsEmpty() ? Before->GetStringField(TEXT("path")) : At;
    TSharedPtr<FJsonObject> After;
    if (!FileState(File, After) || Before->GetBoolField(TEXT("exists")) != After->GetBoolField(TEXT("exists")))
        return false;
    if (!Before->GetBoolField(TEXT("exists")))
        return true;
    for (const TCHAR* Key : {TEXT("sha256"), TEXT("id"), TEXT("written"), TEXT("created")})
        if (Before->GetStringField(Key) != After->GetStringField(Key))
            return false;
    for (const TCHAR* Key : {TEXT("bytes"), TEXT("attributes"), TEXT("links")})
        if (Before->GetNumberField(Key) != After->GetNumberField(Key))
            return false;
    return true;
}
bool SnapshotBundle(const FString& Package, TArray<TSharedPtr<FJsonValue>>& Rows)
{
    for (const TCHAR* Ext : {TEXT(".uasset"), TEXT(".uexp"), TEXT(".ubulk"), TEXT(".uptnl")})
    {
        TSharedPtr<FJsonObject> State;
        if (!FileState(BundleStem(Package) + Ext, State))
            return false;
        Rows.Add(MakeShared<FJsonValueObject>(State));
    }
    return true;
}
bool AllFiles(const TArray<TSharedPtr<FJsonValue>>& Rows)
{
    for (const auto& Row : Rows)
        if (!Row.IsValid() || !SameFile(Row->AsObject()))
            return false;
    return true;
}
bool ExactBundleRows(const TArray<TSharedPtr<FJsonValue>>& Rows)
{
    if (Rows.Num() != 4)
        return false;
    int32 Index = 0;
    for (const TCHAR* Ext : {TEXT(".uasset"), TEXT(".uexp"), TEXT(".ubulk"), TEXT(".uptnl")})
    {
        const auto Value = Rows[Index++];
        if (!Value.IsValid() || Value->Type != EJson::Object)
            return false;
        const auto Row = Value->AsObject();
        FString Path;
        bool Exists = false;
        if (!Row.IsValid() || !Row->TryGetStringField(TEXT("path"), Path) || Path != BundleStem(Target) + Ext ||
            !Row->TryGetBoolField(TEXT("exists"), Exists))
            return false;
        if (Exists)
            for (const TCHAR* Key : {TEXT("sha256"), TEXT("id"), TEXT("written"), TEXT("created")})
                if (!Row->HasTypedField<EJson::String>(Key))
                    return false;
    }
    return Rows[0]->AsObject()->GetBoolField(TEXT("exists"));
}
bool MetadataMatches(UObject* Asset, const TSharedPtr<FJsonObject>& Expected)
{
    const auto Actual = Proof::Metadata(Asset);
    if (!Expected.IsValid() || Actual.Num() != Expected->Values.Num())
        return false;
    for (const auto& Pair : Actual)
        if (!Expected->HasTypedField<EJson::String>(Pair.Key.ToString()) ||
            Expected->GetStringField(Pair.Key.ToString()) != Pair.Value)
            return false;
    return true;
}
bool ExactSource(const TArray<FP4KadanSourceBinding>& Bindings, const TSharedRef<FJsonObject>& Ledger)
{
    FString Why;
    for (const auto& Binding : Bindings)
        if (!P4KadanSourceBindingCurrent(Binding, Why))
            return false;
    const TSharedPtr<FJsonObject>* Before = nullptr;
    TMap<FString, FString> After;
    if (!Ledger->TryGetObjectField(TEXT("source_files"), Before) || !Proof::SourceFiles(Bindings, After) ||
        After.Num() != (*Before)->Values.Num())
        return false;
    for (const auto& Pair : After)
        if (!(*Before)->HasTypedField<EJson::String>(Pair.Key) || (*Before)->GetStringField(Pair.Key) != Pair.Value)
            return false;
    FString Archive;
    return Proof::ArchiveFile(Ledger->GetStringField(TEXT("archive")), Archive) &&
        Archive == Ledger->GetStringField(TEXT("archive_state"));
}
bool ColdNamespace()
{
    for (TObjectIterator<UWorld> It; It; ++It)
        if (It->GetOutermost()->GetName().StartsWith(Cave, ESearchCase::CaseSensitive))
            return false;
    return !FindPackage(nullptr, *Target) && !FindObject<UObject>(nullptr, *Object);
}
bool ExclusiveProcess()
{
#if PLATFORM_WINDOWS
    const HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (Snapshot == INVALID_HANDLE_VALUE)
        return false;
    PROCESSENTRY32W Entry{};
    Entry.dwSize = sizeof(Entry);
    bool Good = true;
    if (!Process32FirstW(Snapshot, &Entry))
    {
        CloseHandle(Snapshot);
        return false;
    }
    do
    {
        const FString Name(Entry.szExeFile);
        const bool Editor = Name.Equals(TEXT("UE4Editor.exe"), ESearchCase::IgnoreCase) ||
            Name.Equals(TEXT("UE4Editor-Cmd.exe"), ESearchCase::IgnoreCase);
        const bool Build = Name.Equals(TEXT("UnrealBuildTool.exe"), ESearchCase::IgnoreCase) ||
            Name.Equals(TEXT("AutomationTool.exe"), ESearchCase::IgnoreCase) ||
            Name.Equals(TEXT("cl.exe"), ESearchCase::IgnoreCase) ||
            Name.Equals(TEXT("link.exe"), ESearchCase::IgnoreCase);
        if (Build || (Editor && Entry.th32ProcessID != FPlatformProcess::GetCurrentProcessId()))
        {
            Good = false;
            break;
        }
    }
    while (Process32NextW(Snapshot, &Entry));
    CloseHandle(Snapshot);
    return Good;
#else
    return false;
#endif
}
bool UnloadTarget(FString* Why = nullptr)
{
    TArray<UPackage*> Packages;
    const auto Family = Proof::Families()[0];
    for (int32 Index = 1; Index < Family.Num(); ++Index)
        if (UPackage* Package = FindPackage(nullptr, *Family[Index]))
        {
            auto* Instance = FindObject<UMaterialInstanceConstant>(
                nullptr, *(Family[Index] + TEXT(".") + FPackageName::GetLongPackageAssetName(Family[Index])));
            if (!Instance || Package->IsDirty() || Instance->GetMaterial() != FindObject<UMaterial>(nullptr, *Object))
            {
                if (Why)
                    *Why = TEXT("Cannot unload the clean source-owned family instance: ") + Family[Index];
                return false;
            }
            Packages.Add(Package); // Only inputs loaded by this cold fixture; no disk/registry deletion.
        }
    // Unloading a live object does not delete the saved asset. Announcing a
    // deletion here would release Kadan's file lock on every hard link before
    // the guarded retirement, even though the original bundle still exists.
    if (UPackage* Package = FindPackage(nullptr, *Target))
        Packages.Add(Package);
    if (Packages.Num())
    {
        FText Error;
        if (!UPackageTools::UnloadPackages(Packages, Error))
        {
            if (Why)
                *Why = TEXT("Stock package unload refused: ") + Error.ToString();
            return false;
        }
    }
    CollectGarbage(RF_NoFlags);
    UPackage* RemainingPackage = FindPackage(nullptr, *Target);
    UObject* RemainingObject = FindObject<UObject>(nullptr, *Object);
    if (!RemainingPackage && !RemainingObject)
        return true;
    if (Why)
        *Why = FString::Printf(
            TEXT("Target remains after stock unload: package=%s flags=%08x rooted=%d; object=%s flags=%08x rooted=%d"),
            *GetPathNameSafe(RemainingPackage), RemainingPackage ? uint32(RemainingPackage->GetFlags()) : 0,
            RemainingPackage ? RemainingPackage->IsRooted() : false, *GetPathNameSafe(RemainingObject),
            RemainingObject ? uint32(RemainingObject->GetFlags()) : 0,
            RemainingObject ? RemainingObject->IsRooted() : false);
    return false;
}

// Enumerate the actual NTFS links, not a filename-derived ownership assumption.
// Both untouched validation projects are mandatory for Silhouette. Any extra
// link outside the three explicit owned fixture namespaces makes repair refuse.
bool LinkCustody(const FString& Package, bool RequireThree, TArray<TSharedPtr<FJsonValue>>& Rows)
{
#if PLATFORM_WINDOWS
    const FString File = BundleStem(Package) + TEXT(".uasset");
    TSharedPtr<FJsonObject> State;
    if (!FileState(File, State) || !State->GetBoolField(TEXT("exists")))
        return false;
    const int32 Links = int32(State->GetNumberField(TEXT("links")));
    if (Links < 1 || Links > 3 || (RequireThree && Links != 3))
        return false;
    const FString Remote = Normalize(FPaths::ProjectDir() / TEXT("../../.."));
    TSet<FString> Expected;
    const FString Relative = TEXT("/Content/") + Package.Mid(6) + TEXT(".uasset");
    for (const TCHAR* Name :
        {TEXT("native-development"), TEXT("discord-fresh-validation"), TEXT("game-references-validation")})
        Expected.Add(Normalize(Remote / TEXT("private") / Name / TEXT("project") + Relative));
    DWORD Length = 32768;
    TArray<WCHAR> Name;
    Name.SetNumUninitialized(Length);
    const HANDLE Search = FindFirstFileNameW(*File, 0, &Length, Name.GetData());
    if (Search == INVALID_HANDLE_VALUE)
        return false;
    TSet<FString> Found;
    bool Good = true;
    do
    {
        const FString Path = Normalize(File.Left(2) + FString(Name.GetData()));
        TSharedPtr<FJsonObject> Link;
        if (!Expected.Contains(Path) || Found.Contains(Path) || !FileState(Path, Link) ||
            !Link->GetBoolField(TEXT("exists")) ||
            Link->GetStringField(TEXT("id")) != State->GetStringField(TEXT("id")) ||
            Link->GetStringField(TEXT("sha256")) != State->GetStringField(TEXT("sha256")))
        {
            Good = false;
            break;
        }
        Found.Add(Path);
        if (Path != File)
            Rows.Add(MakeShared<FJsonValueObject>(Link));
        Length = Name.Num();
    }
    while (FindNextFileNameW(Search, &Length, Name.GetData()));
    const DWORD Last = GetLastError();
    FindClose(Search);
    return Good && Last == ERROR_HANDLE_EOF && Found.Num() == Links && (!RequireThree || Found.Num() == Expected.Num());
#else
    return false;
#endif
}
bool SnapshotWorlds(TArray<TSharedPtr<FJsonValue>>& Rows)
{
    TArray<FString> Worlds;
    IFileManager::Get().FindFilesRecursive(Worlds, *BundleStem(Cave), TEXT("*.umap"), true, false, false);
    if (Worlds.Num() != 13)
        return false;
    Worlds.Sort();
    for (const FString& File : Worlds)
    {
        TSharedPtr<FJsonObject> State;
        if (!FileState(File, State) || !State->GetBoolField(TEXT("exists")))
            return false;
        Rows.Add(MakeShared<FJsonValueObject>(State));
        for (const TCHAR* Extension : {TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl")})
        {
            TSharedPtr<FJsonObject> Sidecar;
            if (!FileState(FPaths::ChangeExtension(File, Extension), Sidecar))
                return false;
            Rows.Add(MakeShared<FJsonValueObject>(Sidecar));
        }
    }
    return true;
}
bool Preserved(const TSharedRef<FJsonObject>& Ledger, const TArray<FP4KadanSourceBinding>& Bindings)
{
    const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
    for (const TCHAR* Key :
        {TEXT("worlds"), TEXT("dependencies"), TEXT("family_instances"), TEXT("silhouette"), TEXT("external_links")})
    {
        if (!Ledger->TryGetArrayField(Key, Rows))
        {
            UE_LOG(LogTemp, Display, TEXT("Shadow preservation missing group: %s"), Key);
            return false;
        }
        for (const auto& Row : *Rows)
            if (!Row.IsValid() || !SameFile(Row->AsObject()))
            {
                FString Before, After;
                TSharedPtr<FJsonObject> Now;
                if (Row.IsValid())
                {
                    FJsonSerializer::Serialize(Row->AsObject().ToSharedRef(), TJsonWriterFactory<>::Create(&Before));
                    if (FileState(Row->AsObject()->GetStringField(TEXT("path")), Now))
                        FJsonSerializer::Serialize(Now.ToSharedRef(), TJsonWriterFactory<>::Create(&After));
                }
                UE_LOG(LogTemp, Display, TEXT("Shadow preservation group=%s before=%s after=%s"), Key,
                    *Before.Left(4096), *After.Left(4096));
                return false;
            }
    }
    if (!ExactSource(Bindings, Ledger))
    {
        UE_LOG(LogTemp, Display, TEXT("Shadow preservation source binding/files/archive differ"));
        return false;
    }
    TArray<uint8> Manifest;
    const bool Match = FFileHelper::LoadFileToArray(Manifest, *Ledger->GetStringField(TEXT("manifest"))) &&
        Proof::Hash(Manifest) == Ledger->GetStringField(TEXT("manifest_sha256"));
    if (!Match)
        UE_LOG(LogTemp, Display, TEXT("Shadow preservation source manifest differs"));
    return Match;
}
bool FamilyShaders(FString& Error, int32* QualifiedCount = nullptr)
{
    if (QualifiedCount)
        *QualifiedCount = 0;
    UMaterial* Master = FindObject<UMaterial>(nullptr, *Object);
    if (!Master)
        return false;
    TArray<UMaterialInstanceConstant*> Instances;
    TArray<FString> Values;
    TArray<TMap<FName, FString>> Metadata;
    const auto Family = Proof::Families()[0];
    for (int32 Index = 1; Index < Family.Num(); ++Index)
    {
        const FString Package = Family[Index];
        // Reverse-family members are required source evidence, not new import
        // targets. Preserve absent destinations and qualify only existing MICs.
        if (!FPackageName::DoesPackageExist(Package))
        {
            if (FindPackage(nullptr, *Package))
            {
                Error = TEXT("An absent family destination unexpectedly has a live package: ") + Package;
                return false;
            }
            continue;
        }
        auto* Instance = LoadObject<UMaterialInstanceConstant>(nullptr,
            *(Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package)), nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (!Instance || Instance->GetMaterial() != Master || Instance->GetOutermost()->IsDirty())
        {
            Error = TEXT("An existing Shadow family instance does not resolve to the new master: ") + Package;
            return false;
        }
        Instances.Add(Instance);
        Values.Add(P4MaterialInstanceJson(Instance, Error));
        Metadata.Add(Proof::Metadata(Instance));
        if (!Error.IsEmpty())
            return false;
    }
    if (!GShaderCompilingManager)
        return false;
    GShaderCompilingManager->FinishAllCompilation();
    for (int32 Index = 0; Index < Instances.Num(); ++Index)
    {
        auto* Instance = Instances[Index];
        const FMaterialResource* Resource = Instance->GetMaterialResource(GMaxRHIFeatureLevel);
        if (!Resource || !Resource->HasValidGameThreadShaderMap() || Resource->GetCompileErrors().Num() ||
            Instance->GetOutermost()->IsDirty() ||
            !Proof::Metadata(Instance).OrderIndependentCompareEqual(Metadata[Index]) ||
            P4MaterialInstanceJson(Instance, Error) != Values[Index] || !Error.IsEmpty())
        {
            Error = TEXT("An existing Shadow family instance has no valid compiled shader map: ") +
                Instance->GetPathName();
            if (Resource)
                Error += TEXT(" ") + FString::Join(Resource->GetCompileErrors(), TEXT("; "));
            return false;
        }
    }
    if (QualifiedCount)
        *QualifiedCount = Instances.Num();
    return true;
}
bool FreshMaterial(const FString& Recipe, FString& Error)
{
    UMaterial* Material = LoadObject<UMaterial>(nullptr, *Object, nullptr, LOAD_NoWarn | LOAD_Quiet);
    FP4OriginalRecord Record;
    if (!Material || Material->GetOutermost()->IsDirty() || !P4ReadOriginalRecord(Material, Record, Error) ||
        Record.Package != Target || Record.Kind != TEXT("material") ||
        Record.Fidelity != TEXT("recovered-pixel-contract") || !Record.ReferenceOnly || !Record.ReadOnly ||
        Record.SourceDigestKind != TEXT("material-graph-v2-md5") ||
        Record.SourceDigest != FMD5::HashAnsiString(*Recipe) || !Proof::HistoricalPins(Recipe, 0, Error) ||
        !P4FreshMaterialCandidateMatchesSource(Material, Recipe, Error))
        return false;
    const auto Metadata = Proof::Metadata(Material);
    TArray<FString> Errors;
    const bool Compiled = !GUsingNullRHI && GMaxRHIFeatureLevel == ERHIFeatureLevel::SM5 &&
        P4WaitForMaterialShaders(Material, Errors, Error) && !Errors.Num();
    if (!Compiled)
    {
        Error += TEXT(" ") + FString::Join(Errors, TEXT("; "));
        return false;
    }
    return !Material->GetOutermost()->IsDirty() && Proof::Metadata(Material).OrderIndependentCompareEqual(Metadata) &&
        P4FreshMaterialCandidateMatchesSource(Material, Recipe, Error);
}

bool Rollback(const TSharedRef<FJsonObject>& Ledger, FString& Error)
{
    UPackage::WaitForAsyncFileWrites();
    const auto& Old = Ledger->GetArrayField(TEXT("old_bundle"));
    const TArray<TSharedPtr<FJsonValue>>* New = nullptr;
    const bool HasNew = Ledger->TryGetArrayField(TEXT("new_bundle"), New);
    if (!ExactBundleRows(Old) || (HasNew && !ExactBundleRows(*New)))
    {
        Error = TEXT("Rollback bundle is not the exact four target paths.");
        return false;
    }
    if (!HasNew && FPackageName::DoesPackageExist(Target) && !AllFiles(Old))
    {
        Error = TEXT("A target without an exact new creation receipt is present; rollback stops closed.");
        return false;
    }
    if (!HasNew && FindPackage(nullptr, *Target) && !FPackageName::DoesPackageExist(Target))
    {
        Error = TEXT("An unreceipted live target is present; rollback cannot retire it.");
        return false;
    }
    if (HasNew && !AllFiles(*New))
    {
        Error = TEXT("New output changed; rollback refuses to overwrite it.");
        return false;
    }
    if (HasNew)
        if (UMaterial* Material = FindObject<UMaterial>(nullptr, *Object))
            if (Material->GetOutermost()->IsDirty() ||
                !MetadataMatches(Material, Ledger->GetObjectField(TEXT("new_metadata"))) ||
                P4MaterialGraphJson(Material, Error) != Ledger->GetStringField(TEXT("new_graph")) || !Error.IsEmpty())
            {
                Error = TEXT("Live new output has edits; rollback refuses to retire it.");
                return false;
            }
    if (!UnloadTarget())
    {
        Error = TEXT("The owned target is still live; rollback stopped.");
        return false;
    }
    if (HasNew)
    {
        const FString Failed = FPaths::GetPath(LedgerFile()) / TEXT("FailedNew");
        for (const auto& Row : *New)
            if (Row->AsObject()->GetBoolField(TEXT("exists")))
            {
                const FString From = Row->AsObject()->GetStringField(TEXT("path")),
                              To = Failed / FPaths::GetCleanFilename(From);
                if (!SameFile(Row->AsObject()) || !MakeTransactionDirectory(Failed) || !MoveTarget(From, To))
                {
                    Error = TEXT("Exact new output could not be preserved for rollback.");
                    return false;
                }
            }
    }
    for (const auto& Row : Old)
    {
        const auto Item = Row->AsObject();
        if (!Item->GetBoolField(TEXT("exists")))
            continue;
        const FString To = Item->GetStringField(TEXT("path")),
                      From = FPaths::GetPath(LedgerFile()) / TEXT("OldShadow") / FPaths::GetCleanFilename(To);
        if (SameFile(Item))
            continue; // A partially completed retirement never moved this link.
        if (!SameFile(Item, From) || !MoveTarget(From, To) || !SameFile(Item))
        {
            Error = TEXT("Original link could not be restored without overwriting a destination.");
            return false;
        }
    }
    Ledger->SetStringField(TEXT("phase"), TEXT("rolled-back"));
    return Save(LedgerFile(), Ledger);
}
} //namespace P4RetainedShadowRepair

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RetainedShadowCustodyTest, "P4MapKit.Kadan.RetainedShadowFileCustody",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RetainedShadowCustodyTest::RunTest(const FString&)
{
#if !PLATFORM_WINDOWS
    AddWarning(TEXT("P4MapKit test not run. ") TEXT("The exact NTFS fixture transaction is unavailable on this host."));
    return true;
#else
    using namespace P4RetainedShadowRepair;
    TestTrue(TEXT("Project directory identity ignores its normal trailing separator"),
        Normalize(FPaths::ProjectDir()) == Normalize(Normalize(FPaths::ProjectDir()) + TEXT("/")) &&
            !Normalize(FPaths::ProjectDir()).EndsWith(TEXT("/")));
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit/ShadowFileCustody");
    const FString File = Folder / TEXT("OnlyOwnedProbe.txt"), Alias = Folder / TEXT("OnlyOwnedAlias.txt");
    const FString Text = TEXT("p4-shadow-custody-control\n");
    FString Existing;
    if (IFileManager::Get().FileExists(*File) && (!FFileHelper::LoadFileToString(Existing, *File) || Existing != Text))
    {
        AddError(TEXT("Unknown prior file occupies the single owned custody control."));
        return false;
    }
    if (!IFileManager::Get().FileExists(*File) &&
        (!IFileManager::Get().MakeDirectory(*Folder, true) ||
            !FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)))
        return false;
    if (!IFileManager::Get().FileExists(*Alias) && !CreateHardLinkW(*Normalize(Alias), *Normalize(File), nullptr))
        return false;
    TSharedPtr<FJsonObject> State, Linked;
    if (!TestTrue(TEXT("Real NTFS identity observes exactly the owned two-link input"),
            FileState(File, State) && FileState(Alias, Linked) && State->GetBoolField(TEXT("exists")) &&
                State->GetStringField(TEXT("id")) == Linked->GetStringField(TEXT("id")) &&
                State->GetNumberField(TEXT("links")) == 2 && SameFile(State) && SameFile(Linked)))
        return false;
    const auto Tamper = [&](const TCHAR* Key, const FString& Replacement)
    {
        auto Changed = MakeShared<FJsonObject>();
        Changed->Values = State->Values;
        Changed->SetStringField(Key, Replacement);
        return SameFile(Changed);
    };
    TestFalse(TEXT("Changed content digest refuses the file"), Tamper(TEXT("sha256"), TEXT("wrong")));
    TestFalse(TEXT("Changed inode refuses the file even with unchanged bytes"), Tamper(TEXT("id"), TEXT("wrong")));
    TestFalse(TEXT("Changed modification timestamp refuses the file"), Tamper(TEXT("written"), TEXT("wrong")));
    auto Attributes = MakeShared<FJsonObject>();
    Attributes->Values = State->Values;
    Attributes->SetNumberField(TEXT("attributes"), State->GetNumberField(TEXT("attributes")) + 1);
    TestFalse(TEXT("Changed attributes refuse the file"), SameFile(Attributes));
    TestFalse(TEXT("The tiny control file cannot become a transaction target"), TransactionPath(File));
    TestFalse(TEXT("An unknown file below the transaction folder is refused"),
        TransactionPath(FPaths::GetPath(LedgerFile()) / TEXT("Other.uasset")));
    TestFalse(TEXT("An unrelated directory is not created by transaction helpers"), TransactionPath(Folder, true));
    TArray<TSharedPtr<FJsonValue>> Bundle;
    TestTrue(TEXT("The actual target bundle has exact four canonical paths"),
        SnapshotBundle(Target, Bundle) && ExactBundleRows(Bundle));
    auto Wrong = MakeShared<FJsonObject>();
    Wrong->Values = Bundle[0]->AsObject()->Values;
    Wrong->SetStringField(TEXT("path"), File);
    auto Bad = Bundle;
    Bad[0] = MakeShared<FJsonValueObject>(Wrong);
    TestFalse(TEXT("A valid owned file cannot be substituted for a target path"), ExactBundleRows(Bad));
    Bad = Bundle;
    Bad.RemoveAt(3);
    TestFalse(TEXT("An incomplete sidecar shape refuses"), ExactBundleRows(Bad));
    Bad = Bundle;
    Bad[0] = nullptr;
    TestFalse(TEXT("Unknown/null row shape fails closed"), ExactBundleRows(Bad));
    TestTrue(TEXT("Every native test file remains exact after negative controls"), SameFile(State) && SameFile(Linked));
    return !HasAnyErrors();
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RetainedShadowRepairTest, "P4MapKit.Kadan.RetainedShadowOrdinaryRepair",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RetainedShadowRepairTest::RunTest(const FString&)
{
    P4_REQUIRE_KADAN_TEST_SEAMS();
    using namespace P4RetainedShadowRepair;
    const FString Mode = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETAINED_SHADOW_REPAIR"));
    if (Mode.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. ")
                TEXT("Owned fixture only: explicit prepare, cold, or rollback. No automatic repair."));
        return true;
    }
    if (!TestTrue(TEXT("Only explicit supported repair modes are admitted"),
            Mode == TEXT("prepare") || Mode == TEXT("cold") || Mode == TEXT("rollback")) ||
        !TestTrue(TEXT("Only the exact owned fixture is admitted: ") + Normalize(FPaths::ProjectDir()),
            Normalize(FPaths::ProjectDir())
                .EndsWith(TEXT("/private/native-development/project"), ESearchCase::CaseSensitive)) ||
        !TestTrue(TEXT("The target and Cave world namespaces must be cold"), ColdNamespace()) ||
        !TestTrue(TEXT("No other Unreal editor or compiler may own the fixture"), ExclusiveProcess()))
        return false;
    FP4KadanImportSettings Settings;
    Settings.GameFolder = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_CAVE_GAME"));
    Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    Settings.Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    Settings.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    Settings.ConvertedCache = FPaths::ProjectSavedDir() / TEXT("Kadan/ConvertedOriginals");
    Settings.ShaderArchive = Settings.ConvertedCache /
        TEXT("Carrot4/Content/ShaderArchive-Carrot4-GLSL_SWITCH.ushaderbytecode");
    TArray<TSharedPtr<FP4KadanSourceAsset>> Sources;
    TArray<FP4KadanSourceBinding> Bindings;
    FString Manifest, ManifestHash, ArchiveState, Error;
    if (!TestTrue(TEXT("Exact twelve-source/qualified archive custody is valid"),
            Proof::FamilyView(Settings.ShaderArchive, Sources, Bindings, Manifest, ManifestHash, ArchiveState, Error)))
    {
        AddError(Error);
        return false;
    }
    FP4KadanImportSourceScope SourceScope(Sources);
    if (Mode != TEXT("prepare"))
    {
        TSharedPtr<FJsonObject> Ledger;
        if (!TestTrue(TEXT("An exact owned pending transaction exists with unchanged protected inputs"),
                Read(LedgerFile(), Ledger) &&
                    Ledger->GetStringField(TEXT("owner")) == TEXT("p4-owned-shadow-only-repair-v1") &&
                    Ledger->GetStringField(TEXT("package")) == Target &&
                    Ledger->GetStringField(TEXT("project")) == Normalize(FPaths::ProjectDir()) &&
                    ExactBundleRows(Ledger->GetArrayField(TEXT("old_bundle"))) &&
                    Preserved(Ledger.ToSharedRef(), Bindings)))
            return false;
        if (Mode == TEXT("rollback"))
        {
            const bool Good = Rollback(Ledger.ToSharedRef(), Error);
            TestTrue(TEXT("Exact owned rollback succeeds"), Good);
            if (!Good)
                AddError(Error);
            return Good;
        }
        const auto Phase = Ledger->GetStringField(TEXT("phase"));
        if (!TestTrue(
                TEXT("Only an explicitly saved pending transaction in a distinct process can enter cold qualification"),
                Phase == TEXT("saved-awaiting-cold") &&
                    Ledger->GetNumberField(TEXT("writer_pid")) != FPlatformProcess::GetCurrentProcessId() &&
                    ExactBundleRows(Ledger->GetArrayField(TEXT("new_bundle")))))
            return false;
        int32 ColdInstances = 0;
        if (!TestTrue(
                TEXT(
                    "A distinct cold process sees only the saved, independently identified new output and every existing family instance"),
                AllFiles(Ledger->GetArrayField(TEXT("new_bundle"))) &&
                    FreshMaterial(Ledger->GetStringField(TEXT("recipe")), Error) &&
                    FamilyShaders(Error, &ColdInstances) &&
                    ColdInstances == Ledger->GetNumberField(TEXT("existing_family_instance_count"))))
        {
            if (!Error.IsEmpty())
                AddError(Error);
            FString Why;
            TestTrue(
                TEXT("Failed cold qualification restores the guarded old bundle"), Rollback(Ledger.ToSharedRef(), Why));
            if (!Why.IsEmpty())
                AddError(Why);
            return false;
        }
        UMaterial* ColdMaterial = FindObject<UMaterial>(nullptr, *Object);
        if (!TestTrue(TEXT("Cold graph and metadata match the exact newly saved receipt"),
                ColdMaterial && MetadataMatches(ColdMaterial, Ledger->GetObjectField(TEXT("new_metadata"))) &&
                    P4MaterialGraphJson(ColdMaterial, Error) == Ledger->GetStringField(TEXT("new_graph")) &&
                    Error.IsEmpty()))
        {
            FString Why;
            TestTrue(TEXT("Cold mismatch is either safely rolled back or reported as a refused restoration"),
                Rollback(Ledger.ToSharedRef(), Why));
            if (!Why.IsEmpty())
                AddError(Why);
            return false;
        }
        Ledger->SetStringField(TEXT("phase"), TEXT("cold-qualified-awaiting-look"));
        Ledger->SetBoolField(TEXT("actual_cold_sm5"), true);
        Ledger->SetNumberField(TEXT("cold_qualified_existing_instance_count"), ColdInstances);
        TestTrue(TEXT("Cold proof preserves all other assets, source bytes, archive and world baselines"),
            Preserved(Ledger.ToSharedRef(), Bindings));
        TestTrue(TEXT("Cold proof is retained; pixel qualification remains pending"),
            Save(LedgerFile(), Ledger.ToSharedRef()));
        return !HasAnyErrors();
    }
    if (!TestTrue(TEXT("No earlier transaction ledger is overwritten"), !IFileManager::Get().FileExists(*LedgerFile())))
        return false;
    FP4MaterialRecovery Recovery(Settings.GameFolder, Sources, Settings.ShaderArchive);
    const auto Recovered = Recovery.Recover(Target);
    const auto Family = Proof::Families()[0];
    bool Covered = Recovered.bOk && Recovered.FamilyMembers.Num() == 5 && Recovered.FamilyUsers.Num() == 5 &&
        Recovered.FamilyOwners.Num() == 4;
    for (const FString& Member : Family)
        Covered = Covered && Recovered.FamilyMembers.Contains(Member);
    if (!TestTrue(TEXT("Shadow ordinary recovery sees its exact five members/four owners and historical pins"),
            Covered && Proof::HistoricalPins(Recovered.Source, 0, Error)))
        return false;
    const auto Plan = P4KadanPlanImport(Sources, {Target}, true);
    if (!TestTrue(TEXT("Ordinary plan retains all metadata but selects exactly one fresh root"),
            Plan.Problems.Num() == 0 && Plan.Roots == 1 && Plan.Scanned.Num() == 12))
        return false;
    auto Ledger = MakeShared<FJsonObject>();
    Ledger->SetStringField(TEXT("owner"), TEXT("p4-owned-shadow-only-repair-v1"));
    Ledger->SetStringField(TEXT("package"), Target);
    Ledger->SetStringField(TEXT("project"), Normalize(FPaths::ProjectDir()));
    Ledger->SetStringField(TEXT("phase"), TEXT("preflight"));
    Ledger->SetStringField(TEXT("recipe"), Recovered.Source);
    Ledger->SetNumberField(TEXT("writer_pid"), FPlatformProcess::GetCurrentProcessId());
    Ledger->SetStringField(TEXT("manifest"), Manifest);
    Ledger->SetStringField(TEXT("manifest_sha256"), ManifestHash);
    Ledger->SetStringField(TEXT("archive"), Settings.ShaderArchive);
    Ledger->SetStringField(TEXT("archive_state"), ArchiveState);
    TMap<FString, FString> SourceFiles;
    if (!TestTrue(TEXT("All twenty-four source files are readable before constructing the ledger"),
            Proof::SourceFiles(Bindings, SourceFiles) && SourceFiles.Num() == 24))
        return false;
    auto SourceJson = MakeShared<FJsonObject>();
    for (const auto& Pair : SourceFiles)
        SourceJson->SetStringField(Pair.Key, Pair.Value);
    Ledger->SetObjectField(TEXT("source_files"), SourceJson);
    TArray<TSharedPtr<FJsonValue>> Worlds, Dependencies, FamilyFiles, OtherFiles, Links, Old;
    if (!TestTrue(TEXT("All thirteen saved world baselines and both Silhouette validation links are captured"),
            SnapshotWorlds(Worlds) && SnapshotBundle(Other, OtherFiles) && LinkCustody(Other, true, Links) &&
                LinkCustody(Target, false, Links) && SnapshotBundle(Target, Old)))
        return false;
    int32 ExistingInstances = 0;
    for (int32 Index = 1; Index < Family.Num(); ++Index)
    {
        if (!TestTrue(TEXT("Present and absent reverse-family MIC bundles are preserved inputs: ") + Family[Index],
                SnapshotBundle(Family[Index], FamilyFiles)))
            return false;
        if (FPackageName::DoesPackageExist(Family[Index]))
            ++ExistingInstances;
    }
    Ledger->SetNumberField(TEXT("existing_family_instance_count"), ExistingInstances);
    const FString OtherFile = BundleStem(Other) + TEXT(".uasset");
    TSharedPtr<FJsonObject> OtherState;
    if (!TestTrue(TEXT("The untouched unmarked Silhouette remains its exact observed file"),
            FileState(OtherFile, OtherState) && OtherState->GetNumberField(TEXT("bytes")) == 7347 &&
                OtherState->GetStringField(TEXT("sha256")) ==
                    TEXT("3633e3014b3a8f01e1c8ba6d95001409d7220c79057765caf5397a906598df13")))
        return false;
    // Same forward walk as BringIn. Reverse family MICs remain source metadata.
    TMap<FName, TSharedPtr<FP4KadanSourceAsset>> Known;
    for (const auto& Source : Sources)
        Known.Add(FName(*Source->Package), Source);
    TArray<FString> Pending{Target};
    TSet<FName> Visited;
    while (Pending.Num())
    {
        const FString Package = Pending.Pop(false);
        const FName Identity(*Package);
        if (Visited.Contains(Identity))
            continue;
        Visited.Add(Identity);
        if (Package != Target)
        {
            if (Package.StartsWith(TEXT("/Game/"), ESearchCase::IgnoreCase))
            {
                UObject* Asset = FPackageName::DoesPackageExist(Package)
                    ? LoadObject<UObject>(nullptr,
                          *(Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package)), nullptr,
                          LOAD_NoWarn | LOAD_Quiet)
                    : nullptr;
                if (!TestTrue(TEXT("Every non-target dependency already has a clean real destination: ") + Package,
                        Asset && !Asset->GetOutermost()->IsDirty() && !P4IsVerifiedStandIn(Asset) &&
                            FString(Asset->GetOutermost()->GetMetaData()->GetValue(
                                Asset, TEXT("P4BlueprintStandIn"))) != TEXT("1")) ||
                    !SnapshotBundle(Package, Dependencies))
                    return false;
            }
        }
        if (const auto* Source = Known.Find(Identity))
            for (const FString& Dependency : (*Source)->Dependencies)
                if (!(*Source)->SoftDependencies.Contains(Dependency))
                    Pending.Add(Dependency);
    }
    Ledger->SetArrayField(TEXT("worlds"), Worlds);
    Ledger->SetArrayField(TEXT("dependencies"), Dependencies);
    Ledger->SetArrayField(TEXT("family_instances"), FamilyFiles);
    Ledger->SetArrayField(TEXT("silhouette"), OtherFiles);
    Ledger->SetArrayField(TEXT("external_links"), Links);
    Ledger->SetArrayField(TEXT("old_bundle"), Old);
    const auto* TargetBinding = Bindings.FindByPredicate(
        [](const FP4KadanSourceBinding& Binding) { return Binding.Package == Target; });
    UMaterial* Original = LoadObject<UMaterial>(nullptr, *Object);
    FP4OriginalRecord Record;
    if (!TestTrue(TEXT("Only the unchanged source-bound graphless protected Shadow is eligible"),
            Original && Original->Expressions.Num() == 0 && !Original->GetOutermost()->IsDirty() &&
                P4ReadOriginalRecord(Original, Record, Error) && Record.Package == Target &&
                Record.Kind == TEXT("object") && Record.ReferenceOnly && Record.ReadOnly &&
                Record.SourceDigestKind == TEXT("header-body-sha256-v1") &&
                Record.SourceDigest == TEXT("50927f006f2349b937567e7e7d922e49cfb2fab47654f6a18acb434b18d1e465") &&
                TargetBinding && Record.SourceDigest == TargetBinding->SourceDigest))
        return false;
    const auto OriginalNative = Proof::Native(Original);
    const auto OriginalMetadata = Proof::Metadata(Original);
    if (!TestTrue(TEXT("The real original diagnostic is equal and leaves its whole native/metadata state exact"),
            P4DiagnoseOriginalAsset(Original).Comparison == EP4OriginalComparison::Equal &&
                Proof::Native(Original).OrderIndependentCompareEqual(OriginalNative) &&
                Proof::Metadata(Original).OrderIndependentCompareEqual(OriginalMetadata) &&
                !Original->GetOutermost()->IsDirty()))
        return false;
    Original = nullptr;
    if (!TestTrue(TEXT("Original package unloads before any retirement"), UnloadTarget(&Error)))
    {
        if (!Error.IsEmpty())
            AddError(Error);
        return false;
    }
    if (!TestTrue(TEXT("All protected source, map, instance and external-link guards remain exact after unload"),
            Preserved(Ledger, Bindings)) ||
        !TestTrue(TEXT("The old target bundle remains exact after unload"), AllFiles(Old)))
        return false;
    const FString Retired = FPaths::GetPath(LedgerFile()) / TEXT("OldShadow");
    if (!TestTrue(TEXT("One bounded old-bundle directory and durable pre-move ledger are created"),
            !IFileManager::Get().DirectoryExists(*Retired) && MakeTransactionDirectory(Retired) &&
                Save(LedgerFile(), Ledger)))
        return false;
    bool PendingRollback = true;
    ON_SCOPE_EXIT
    {
        if (PendingRollback)
        {
            FString Why;
            const bool Restored = Rollback(Ledger, Why);
            TestTrue(TEXT("Failed repair restores only the exactly guarded original namespace"), Restored);
            if (!Restored)
                AddError(Why);
        }
    };
    for (const auto& Row : Old)
        if (Row->AsObject()->GetBoolField(TEXT("exists")))
        {
            const FString From = Row->AsObject()->GetStringField(TEXT("path")),
                          To = Retired / FPaths::GetCleanFilename(From);
            if (!TestTrue(TEXT("Exact original link is renamed without overwriting any inode"),
                    SameFile(Row->AsObject()) && MoveTarget(From, To) && SameFile(Row->AsObject(), To)))
                return false;
        }
    Ledger->SetStringField(TEXT("phase"), TEXT("retired"));
    if (!TestTrue(TEXT("Target destination and loaded namespace are genuinely absent"),
            !FPackageName::DoesPackageExist(Target) && ColdNamespace() && Save(LedgerFile(), Ledger)))
        return false;
    FP4KadanScopedAcquisitionVeto Veto(FPaths::ProjectDir());
    const auto Imported = P4KadanRunImport(Settings, Plan, true);
    UPackage::WaitForAsyncFileWrites();
    Ledger->SetStringField(TEXT("normal_import_report"), Imported.Report);
    // The ordinary graph report has asset/valid/saved/compiled, but no created
    // field. Its builder refuses any occupied destination and always NewObjects.
    // Native reference-fallback reports also provide an explicit created field.
    // Combine the actual saved reply with pre-call absence and the new inode;
    // never fabricate a created field in the importer's original report.
    TSharedPtr<FJsonObject> Report;
    const TSharedPtr<FJsonObject>* Assets = nullptr;
    const TSharedPtr<FJsonObject>* Reply = nullptr;
    bool Created = false, Saved = false, Valid = false, Compiled = false;
    FString AssetPath;
    const bool OwnReply = FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Imported.Report), Report) &&
        Report.IsValid() && Report->TryGetObjectField(TEXT("assets"), Assets) &&
        (*Assets)->TryGetObjectField(Target, Reply) && (*Reply)->TryGetBoolField(TEXT("valid"), Valid) && Valid &&
        (*Reply)->TryGetBoolField(TEXT("saved"), Saved) && Saved &&
        (*Reply)->TryGetStringField(TEXT("asset"), AssetPath) && AssetPath == Object &&
        (((*Reply)->TryGetBoolField(TEXT("created"), Created) && Created) ||
            (!(*Reply)->HasField(TEXT("created")) && (*Reply)->TryGetBoolField(TEXT("compiled"), Compiled) &&
                Compiled));
    TArray<TSharedPtr<FJsonValue>> New;
    if (OwnReply && SnapshotBundle(Target, New))
    {
        Ledger->SetArrayField(TEXT("new_bundle"), New);
        auto Metadata = MakeShared<FJsonObject>();
        if (UMaterial* Material = FindObject<UMaterial>(nullptr, *Object))
        {
            for (const auto& Pair : Proof::Metadata(Material))
                Metadata->SetStringField(Pair.Key.ToString(), Pair.Value);
            FString GraphError;
            Ledger->SetStringField(TEXT("new_graph"), P4MaterialGraphJson(Material, GraphError));
            if (!GraphError.IsEmpty())
                AddError(GraphError);
        }
        Ledger->SetObjectField(TEXT("new_metadata"), Metadata);
        Save(LedgerFile(), Ledger);
    }
    int32 QualifiedInstances = 0;
    if (!TestTrue(TEXT("Only Shadow is freshly Created/Saved by ordinary import with no fallback or acquisition"),
            OwnReply && Imported.Ok && Imported.Imported.Num() == 1 && Imported.Imported.Contains(Target) &&
                !Imported.ReferenceOnly.Num() && !Imported.NotImported.Num() && Veto.DeniedBatches() == 0 &&
                FreshMaterial(Recovered.Source, Error) && FamilyShaders(Error, &QualifiedInstances) &&
                QualifiedInstances == ExistingInstances))
    {
        for (const FString& Problem : Imported.Problems)
            AddError(Problem);
        if (!Error.IsEmpty())
            AddError(Error);
        return false;
    }
    if (!TestTrue(TEXT("New target has an independent file identity; all protected files remain exact"),
            New.Num() == 4 && New[0]->AsObject()->GetBoolField(TEXT("exists")) &&
                New[0]->AsObject()->GetStringField(TEXT("id")) != Old[0]->AsObject()->GetStringField(TEXT("id")) &&
                New[0]->AsObject()->GetNumberField(TEXT("links")) == 1 && Preserved(Ledger, Bindings)))
        return false;
    Ledger->SetStringField(TEXT("phase"), TEXT("saved-awaiting-cold"));
    Ledger->SetBoolField(TEXT("actual_sm5"), true);
    Ledger->SetNumberField(TEXT("qualified_existing_instance_count"), QualifiedInstances);
    Ledger->SetBoolField(TEXT("fresh_creation_proved"), true);
    Ledger->SetBoolField(TEXT("look_proven"), false);
    Ledger->SetBoolField(TEXT("silhouette_repaired"), false);
    if (!TestTrue(TEXT("Pending cold transaction and exact independent file receipt are durable"),
            Save(LedgerFile(), Ledger)))
        return false;
    PendingRollback = false;
    return !HasAnyErrors();
}

namespace P4RetainedMaterialCandidateProof
{
bool QualifiedShadowForLook(UMaterial*& Out, TSharedPtr<FJsonObject>& Receipt, FString& Error)
{
    using namespace P4RetainedShadowRepair;
    Out = nullptr;
    Receipt.Reset();
    if (!Normalize(FPaths::ProjectDir())
            .EndsWith(TEXT("/private/native-development/project"), ESearchCase::CaseSensitive) ||
        !Read(LedgerFile(), Receipt) ||
        Receipt->GetStringField(TEXT("owner")) != TEXT("p4-owned-shadow-only-repair-v1") ||
        Receipt->GetStringField(TEXT("project")) != Normalize(FPaths::ProjectDir()) ||
        Receipt->GetStringField(TEXT("package")) != Target ||
        Receipt->GetStringField(TEXT("phase")) != TEXT("cold-qualified-awaiting-look") ||
        !Receipt->GetBoolField(TEXT("fresh_creation_proved")) || !Receipt->GetBoolField(TEXT("actual_sm5")) ||
        !Receipt->GetBoolField(TEXT("actual_cold_sm5")))
    {
        Error = TEXT("A source-bound saved/cold Shadow transaction is required before LOOK.");
        return false;
    }
    const auto& Old = Receipt->GetArrayField(TEXT("old_bundle"));
    const auto& New = Receipt->GetArrayField(TEXT("new_bundle"));
    if (!ExactBundleRows(Old) || !ExactBundleRows(New) || !AllFiles(New) ||
        New[0]->AsObject()->GetStringField(TEXT("id")) == Old[0]->AsObject()->GetStringField(TEXT("id")) ||
        New[0]->AsObject()->GetNumberField(TEXT("links")) != 1)
    {
        Error = TEXT("The new independent saved Shadow file identity no longer matches its creation receipt.");
        return false;
    }
    for (const auto& Row : Old)
    {
        const FString Path = FPaths::GetPath(LedgerFile()) / TEXT("OldShadow") /
            FPaths::GetCleanFilename(Row->AsObject()->GetStringField(TEXT("path")));
        if (!TransactionPath(Path) || !SameFile(Row->AsObject(), Path))
        {
            Error = TEXT("The retained original Shadow bundle changed.");
            return false;
        }
    }
    TArray<TSharedPtr<FP4KadanSourceAsset>> Sources;
    TArray<FP4KadanSourceBinding> Bindings;
    FString Manifest, ManifestHash, ArchiveState;
    if (!FamilyView(
            Receipt->GetStringField(TEXT("archive")), Sources, Bindings, Manifest, ManifestHash, ArchiveState, Error) ||
        Manifest != Receipt->GetStringField(TEXT("manifest")) ||
        ManifestHash != Receipt->GetStringField(TEXT("manifest_sha256")) || !Preserved(Receipt.ToSharedRef(), Bindings))
        return false;
    FP4KadanImportSourceScope SourceScope(Sources);
    if (!FreshMaterial(Receipt->GetStringField(TEXT("recipe")), Error))
        return false;
    UMaterial* Material = FindObject<UMaterial>(nullptr, *Object);
    if (!Material || !MetadataMatches(Material, Receipt->GetObjectField(TEXT("new_metadata"))) ||
        P4MaterialGraphJson(Material, Error) != Receipt->GetStringField(TEXT("new_graph")) || !Error.IsEmpty())
        return false;
    Out = Material;
    return true;
}
} //namespace P4RetainedMaterialCandidateProof
#endif

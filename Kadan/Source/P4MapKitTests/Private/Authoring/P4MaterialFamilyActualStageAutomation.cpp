#if WITH_DEV_AUTOMATION_TESTS
#include "P4MaterialFamilyAcquireStage.h"
#include "P4KadanTestSeams.h"
#include "P4KadanContainerIndex.h"
#include "P4KadanAcquisitionVeto.h"
#include "P4KadanImport.h"
#include "P4NativeHash.h"
#include "P4RetainedMaterialFixture.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace P4ActualFamilyStageTest
{
namespace Proof = P4RetainedMaterialCandidateProof;
FString Full(FString Path)
{
    if (Path.IsEmpty())
        return FString();
    Path = FPaths::ConvertRelativePathToFull(Path);
    FPaths::NormalizeFilename(Path);
    if (!FPaths::CollapseRelativeDirectories(Path))
        return FString();
    FPaths::NormalizeDirectoryName(Path);
    return Path;
}
bool Same(const FString& A, const FString& B)
{
    return P4ExactNativeString(A, B);
}
bool Inside(const FString& Path, const FString& Root)
{
    return Path.StartsWith(Root + TEXT("/"), ESearchCase::CaseSensitive);
}
bool NoReparse(FString Path)
{
#if PLATFORM_WINDOWS
    Path = Full(Path);
    if (Path.IsEmpty())
        return false;
    while (!Path.IsEmpty())
    {
        const DWORD Attr = GetFileAttributesW(*Path);
        if (Attr == INVALID_FILE_ATTRIBUTES)
        {
            const DWORD Error = GetLastError();
            if (Error != ERROR_FILE_NOT_FOUND && Error != ERROR_PATH_NOT_FOUND)
                return false;
        }
        else if (Attr & FILE_ATTRIBUTE_REPARSE_POINT)
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
struct FReport
{
    FAutomationTestBase& Test;
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    explicit FReport(FAutomationTestBase& In) : Test(In)
    {
        Json->SetStringField(TEXT("owner"), TEXT("p4-actual-missing-material-family-stage-v1"));
        Json->SetStringField(TEXT("status"), TEXT("refused"));
        Json->SetBoolField(TEXT("qualified"), false);
        Json->SetStringField(TEXT("stage"), TEXT("admission"));
    }
    ~FReport()
    {
        if (Test.HasAnyErrors())
        {
            Json->SetStringField(TEXT("status"), TEXT("refused"));
            Json->SetBoolField(TEXT("qualified"), false);
        }
        const FString Leaf = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MATERIAL_FAMILY_STAGE_TWO")) ==
                TEXT("1")
            ? TEXT("proof-two.json")
            : TEXT("proof.json");
        const FString File = FPaths::ProjectSavedDir() / TEXT("P4MapKit/MaterialFamilyActualStage") / Leaf;
        FString Text;
        if (!NoReparse(File))
        {
            Test.AddError(TEXT("Actual stage proof path has foreign reparse custody"));
            return;
        }
        if (IFileManager::Get().FileExists(*File))
        {
            TSharedPtr<FJsonObject> Prior;
            FString Existing, Owner;
            if (IFileManager::Get().FileSize(*File) > 1024 * 1024 || !FFileHelper::LoadFileToString(Existing, *File) ||
                !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Existing), Prior) || !Prior.IsValid() ||
                !Prior->TryGetStringField(TEXT("owner"), Owner) ||
                Owner != TEXT("p4-actual-missing-material-family-stage-v1"))
            {
                Test.AddError(TEXT("Existing actual stage receipt has unknown custody"));
                return;
            }
        }
        if (!FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text)) ||
            !IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true) ||
            !FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            Test.AddError(TEXT("Cannot retain actual material stage proof"));
    }
};
struct FFile
{
    bool Exists = false;
    FString Identity, Digest;
};
// No directory census: only exact admitted names, bytes and handle-derived identity.
bool Observe(const FString& Path, FFile& Out, bool Hash = true, int64 Bound = 64ll * 1024 * 1024)
{
    Out = FFile{};
    if (!NoReparse(Path))
        return false;
#if PLATFORM_WINDOWS
    const DWORD Attr = GetFileAttributesW(*Path);
    if (Attr == INVALID_FILE_ATTRIBUTES)
        return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
    const HANDLE H = CreateFileW(
        *Path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (H == INVALID_HANDLE_VALUE)
        return false;
    BY_HANDLE_FILE_INFORMATION A{}, B{};
    const auto Signature = [](const BY_HANDLE_FILE_INFORMATION& I)
    {
        return FString::Printf(TEXT("%u:%u:%u|%u:%u|%u:%u|%u:%u|%u|%u"), I.dwVolumeSerialNumber, I.nFileIndexHigh,
            I.nFileIndexLow, I.nFileSizeHigh, I.nFileSizeLow, I.ftCreationTime.dwHighDateTime,
            I.ftCreationTime.dwLowDateTime, I.ftLastWriteTime.dwHighDateTime, I.ftLastWriteTime.dwLowDateTime,
            I.dwFileAttributes, I.nNumberOfLinks);
    };
    bool Good = GetFileInformationByHandle(H, &A) &&
        !(A.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
        ((uint64(A.nFileSizeHigh) << 32) | A.nFileSizeLow) <= uint64(Bound);
    int64 Size = 0;
    if (Good && Hash)
        Good = P4HashFile(Path, Bound, Size, Out.Digest);
    Good = Good && GetFileInformationByHandle(H, &B) && Same(Signature(A), Signature(B));
    CloseHandle(H);
    if (Good)
    {
        Out.Exists = true;
        Out.Identity = Signature(A);
    }
    return Good;
#else
    (void)Hash;
    (void)Bound;
    return false;
#endif
}
bool SameFile(const FString& Path, const FFile& Before, bool Hash = true, int64 Bound = 64ll * 1024 * 1024)
{
    FFile After;
    return Observe(Path, After, Hash, Bound) && After.Exists == Before.Exists &&
        Same(After.Identity, Before.Identity) && Same(After.Digest, Before.Digest);
}
TMap<FString, FString> LiveOriginalObjects()
{
    TMap<FString, FString> Out;
    for (TObjectIterator<UObject> It; It; ++It)
    {
        UObject* Object = *It;
        if ((!Object->IsA<UMaterialInterface>() && !Object->IsA<UWorld>() && !Object->IsA<UPackage>()) ||
            !Object->GetOutermost()->GetName().StartsWith(TEXT("/Game/"), ESearchCase::CaseSensitive))
            continue;
        Out.Add(Object->GetPathName(),
            Object->GetClass()->GetPathName() +
                FString::Printf(TEXT("|%p|%08x|dirty=%d"), static_cast<void*>(Object), uint32(Object->GetFlags()),
                    Object->GetOutermost()->IsDirty() ? 1 : 0));
    }
    return Out;
}
bool DescriptorsSame(const TArray<TSharedPtr<FP4KadanSourceAsset>>& Sources, const TArray<FP4KadanSourceAsset>& Before)
{
    if (Sources.Num() != Before.Num())
        return false;
    for (int32 I = 0; I < Sources.Num(); ++I)
    {
        if (!Sources[I].IsValid())
            return false;
        const auto& A = *Sources[I];
        const auto& B = Before[I];
        if (!Same(A.Package, B.Package) || !Same(A.Header, B.Header) || !Same(A.Body, B.Body) ||
            !Same(A.Kind, B.Kind) || A.SourceBytes != B.SourceBytes || A.Dependencies != B.Dependencies ||
            A.SoftDependencies != B.SoftDependencies)
            return false;
    }
    return true;
}
} //namespace P4ActualFamilyStageTest

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ActualMaterialFamilyStage, "P4MapKit.Kadan.ActualMissingMaterialFamilyStage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ActualMaterialFamilyStage::RunTest(const FString&)
{
    P4_REQUIRE_KADAN_TEST_SEAMS();
    using namespace P4ActualFamilyStageTest;
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MATERIAL_FAMILY_STAGE")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "Skipped actual missing-cache converter gate; opt in P4_NATIVE_MATERIAL_FAMILY_STAGE=1. No actual qualification claimed."));
        return true;
    }
#if !PLATFORM_WINDOWS
    AddError(TEXT("Actual material stage requires Windows exact fixture custody"));
    return false;
#else
    FString Error;
    const FString Native = Full(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT")));
    const FString Repo = FPaths::GetPath(FPaths::GetPath(Native));
    if (!TestTrue(TEXT("Only the exact owned native-development fixture and game thread are admitted"),
            !Native.IsEmpty() && IsInGameThread() &&
                Same(Full(FPaths::ProjectDir()), Full(Repo / TEXT("private/native-development/project"))) &&
                NoReparse(FPaths::ProjectDir())))
        return false;
    FReport Report(*this);
    const int32 RequiredMissing =
        FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MATERIAL_FAMILY_STAGE_TWO")) == TEXT("1") ? 2 : 1;
    Report.Json->SetNumberField(TEXT("required_missing_members"), RequiredMissing);
    const auto LiveBefore = LiveOriginalObjects();
    ON_SCOPE_EXIT
    {
        const bool Good = LiveBefore.OrderIndependentCompareEqual(LiveOriginalObjects());
        Report.Json->SetBoolField(TEXT("live_original_identity_flags_dirty_preserved"), Good);
        TestTrue(TEXT("No original material, world or package identity/flags/dirty state changes"), Good);
    };
    FP4KadanImportSettings Settings;
    Settings.GameFolder = Full(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_CAVE_GAME")));
    Settings.GamePaks = Full(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS")));
    Settings.Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    Settings.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    if (Settings.Retoc.IsEmpty())
        Settings.Retoc = Repo / TEXT("private/retoc/retoc.exe");
    if (Settings.Repak.IsEmpty())
        Settings.Repak = Repo / TEXT("private/repak/repak.exe");
    Settings.ConvertedCache = Full(FPaths::ProjectSavedDir() / TEXT("Kadan/ConvertedOriginals"));
    Settings.ShaderArchive = Settings.ConvertedCache /
        TEXT("Carrot4/Content/ShaderArchive-Carrot4-GLSL_SWITCH.ushaderbytecode");
    const FString Stage = Full(FPaths::ProjectSavedDir() / TEXT("Kadan/MaterialFamilyAcquireStage"));
    if (!TestTrue(TEXT("Stage is absent and disjoint from cache/containers/source view"),
            NoReparse(Stage) && !IFileManager::Get().DirectoryExists(*Stage) &&
                !IFileManager::Get().FileExists(*Stage) && !Inside(Stage, Settings.ConvertedCache) &&
                !Inside(Settings.ConvertedCache, Stage) && !Same(Stage, Settings.ConvertedCache)))
        return false;
    TArray<TSharedPtr<FP4KadanSourceAsset>> Sources;
    TArray<FP4KadanSourceBinding> Bindings;
    FString Manifest, ManifestHash, ArchiveState;
    Report.Json->SetStringField(TEXT("stage"), TEXT("existing-source-view"));
    if (!TestTrue(TEXT("Read the existing exact twelve-pair source view and qualified archive once"),
            Proof::FamilyView(Settings.ShaderArchive, Sources, Bindings, Manifest, ManifestHash, ArchiveState, Error) &&
                Sources.Num() == 12 && Bindings.Num() == 12 &&
                Same(ManifestHash, TEXT("cbaf1d27df47f70ca13f02f4a5900981a45c7a8670e40553aac331e637e91180"))))
    {
        AddError(Error);
        return false;
    }
    Report.Json->SetStringField(TEXT("manifest_sha256"), ManifestHash);
    Report.Json->SetStringField(TEXT("archive_initial_full_sha_and_state"), ArchiveState);
    const FString View = Full(FPaths::GetPath(Manifest));
    if (!TestTrue(TEXT("Source view is disjoint from stage and current conversion cache"),
            NoReparse(View) && !Inside(Stage, View) && !Inside(View, Stage) && !Same(Stage, View) &&
                !Inside(View, Settings.ConvertedCache) && !Inside(Settings.ConvertedCache, View) &&
                !Same(View, Settings.ConvertedCache)))
        return false;
    TMap<FString, FFile> Protected;
    TArray<FP4KadanSourceAsset> Descriptors;
    const auto Protect = [&](const FString& File)
    {
        FFile State;
        if (!Observe(Full(File), State))
            return false;
        Protected.Add(Full(File), MoveTemp(State));
        return true;
    };
    for (const auto& Source : Sources)
        Descriptors.Add(*Source);
    for (const auto& Binding : Bindings)
        if (!Protect(Binding.HeaderPath) || !Protect(Binding.BodyPath))
            return false;
    if (!Protect(Manifest) || !Protect(Settings.ConvertedCache / TEXT("converted-from.txt")))
        return false;
    FFile ArchiveBefore;
    if (!Observe(Settings.ShaderArchive, ArchiveBefore, false, int64(1) << 31) || !ArchiveBefore.Exists)
        return false;
    const HANDLE ArchiveLease = CreateFileW(*Settings.ShaderArchive, GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (!TestTrue(TEXT("Hold the already once-hashed archive read-only through the operation"),
            ArchiveLease != INVALID_HANDLE_VALUE))
        return false;
    ON_SCOPE_EXIT
    {
        CloseHandle(ArchiveLease);
    };
    bool BaselinesReady = false;
    const auto Preserved = [&]()
    {
        bool Good = DescriptorsSame(Sources, Descriptors) &&
            SameFile(Settings.ShaderArchive, ArchiveBefore, false, int64(1) << 31);
        for (const auto& Pair : Protected)
            Good = SameFile(Pair.Key, Pair.Value) && Good;
        for (const auto& Binding : Bindings)
            Good = P4KadanSourceBindingCurrent(Binding, Error) && Good;
        return Good;
    };
    ON_SCOPE_EXIT
    {
        if (BaselinesReady)
        {
            const bool Good = Preserved();
            Report.Json->SetBoolField(TEXT("exact_five_cache_bundles_stamp_12pairs_manifest_archive_preserved"), Good);
            TestTrue(TEXT("Exact selected cache/source/archive baselines remain current on every exit"), Good);
        }
    };
    // This manifest mapping is a read-only locator. Fresh retoc list inside the stage
    // operation is the independent authority for actual mounted conversion paths.
    TSharedPtr<FJsonObject> ManifestJson;
    FString ManifestText;
    const TSharedPtr<FJsonObject>* Packages = nullptr;
    if (!FFileHelper::LoadFileToString(ManifestText, *Manifest) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ManifestText), ManifestJson) ||
        !ManifestJson.IsValid() || !ManifestJson->TryGetObjectField(TEXT("packages"), Packages))
        return false;
    const auto Shadow = Proof::Families()[0];
    FString LocatorText;
    TArray<FString> SelectedPackages;
    TArray<TSharedPtr<FJsonValue>> CacheFacts;
    for (const FString& Package : Shadow)
    {
        const TSharedPtr<FJsonObject>* Row = nullptr;
        FString Relative;
        if (!(*Packages)->TryGetObjectField(Package, Row) || !(*Row)->TryGetStringField(TEXT("header"), Relative) ||
            !Relative.StartsWith(TEXT("Carrot4/Content/"), ESearchCase::CaseSensitive) ||
            Relative.Contains(TEXT("..")) || Relative.Contains(TEXT("\\")))
            return false;
        LocatorText += TEXT("0 0 ExportBundleData ../../../") + Relative + TEXT("\n");
        const FString Header = Full(Settings.ConvertedCache / Relative);
        if (!Inside(Header, Settings.ConvertedCache))
            return false;
        int32 Present = 0;
        bool HaveHeader = false, HaveBody = false;
        for (const TCHAR* Extension : {TEXT("uasset"), TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl"), TEXT("m.ubulk")})
        {
            const FString Path = FPaths::ChangeExtension(Header, Extension);
            if (!Protect(Path))
                return false;
            const bool Exists = Protected.FindChecked(Path).Exists;
            Present += Exists ? 1 : 0;
            if (FString(Extension) == TEXT("uasset"))
                HaveHeader = Exists;
            if (FString(Extension) == TEXT("uexp"))
                HaveBody = Exists;
        }
        if (!TestTrue(TEXT("Existing cache bundles are complete or wholly absent; partials refuse: ") + Package,
                Present == 0 || (HaveHeader && HaveBody)))
            return false;
        auto Fact = MakeShared<FJsonObject>();
        Fact->SetStringField(TEXT("package"), Package);
        Fact->SetNumberField(TEXT("present_files"), Present);
        CacheFacts.Add(MakeShared<FJsonValueObject>(Fact));
        if (Present == 0 && SelectedPackages.Num() < RequiredMissing)
            SelectedPackages.Add(Package);
    }
    Report.Json->SetArrayField(TEXT("cache_bundles_before"), CacheFacts);
    BaselinesReady = true;
    FP4KadanContainerIndex Locator;
    if (!TestTrue(TEXT("Bounded five-package manifest locator parses"), Locator.Read(LocatorText, Error)))
        return false;
    FString Cache;
    if (!TestTrue(TEXT("Existing conversion cache stamp is current; never create/reset it"),
            P4KadanRetainedConversionCacheCurrent(Settings, Cache, Error) &&
                Same(Full(Cache), Settings.ConvertedCache)))
    {
        AddError(Error);
        return false;
    }
    if (SelectedPackages.Num() != RequiredMissing)
    {
        Report.Json->SetStringField(TEXT("status"), TEXT("not_eligible_insufficient_absent_shadow_members"));
        Report.Json->SetBoolField(TEXT("qualified"), false);
        Report.Json->SetNumberField(TEXT("converted_packages"), 0);
        Report.Json->SetBoolField(TEXT("stage_absent_after"), true);
        AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "Not eligible: not enough Shadow members are wholly absent from the cache. No cache changes, registry/tool acquisition or stage creation requested."));
        return true;
    }
    SelectedPackages.Sort();
    const FString Selected = SelectedPackages[0];
    Report.Json->SetStringField(TEXT("selected_missing_cache_member"), Selected);
    TArray<TSharedPtr<FJsonValue>> SelectedFacts;
    for (const auto& Package : SelectedPackages)
        SelectedFacts.Add(MakeShared<FJsonValueString>(Package));
    Report.Json->SetArrayField(TEXT("selected_missing_cache_members"), SelectedFacts);
    TArray<TSharedPtr<FP4KadanSourceAsset>> Registered;
    for (const auto& Source : Sources)
        if (!SelectedPackages.Contains(Source->Package))
            Registered.Add(Source);
    if (!TestTrue(TEXT("Only selected retained source-view descriptors are withheld, without altering any source file"),
            Registered.Num() == 12 - RequiredMissing))
        return false;
    FP4ExportContainerTools Tools;
    Tools.Retoc = Settings.Retoc;
    Tools.Repak = Settings.Repak;
    Tools.TimeoutSeconds = 120;
    FP4CurrentGameRegistryPtr Reader;
    Report.Json->SetStringField(TEXT("stage"), TEXT("current-registry"));
    if (!TestTrue(TEXT("Acquire actual selected-container current registry"),
            P4AcquireCurrentGameRegistry(Tools, Settings.GamePaks,
                FPaths::ProjectSavedDir() / TEXT("P4MapKit/MaterialFamilyActualStage/registry-work"), Reader, Error)))
    {
        AddError(Error);
        return false;
    }
    Report.Json->SetStringField(TEXT("registry_sha256"), Reader->RegistrySha256());
    Report.Json->SetStringField(TEXT("lease_epoch"), Reader->LeaseEpoch());
    FP4MaterialFamilyAcquireContextPtr Context;
    if (!TestTrue(TEXT("Actual current registry/cache context is admitted"),
            P4CreateMaterialFamilyAcquireContext(Settings, Reader, Context, Error)))
    {
        AddError(Error);
        return false;
    }
    FP4MaterialFamilySourcesPtr Before;
    TArray<FString> Missing;
    int64 AvailableSourceBytes = -1;
    if (!TestTrue(TEXT("Real registered/cache read requests exactly the selected wholly absent members"),
            P4ReadMaterialFamilySources(Settings, Reader, Shadow[0], Registered, &Locator, Before, Missing, Error,
                &AvailableSourceBytes) == EP4MaterialFamilySourceRead::MissingSources &&
                !Before.IsValid() && Missing == SelectedPackages && AvailableSourceBytes > 0))
    {
        AddError(Error);
        return false;
    }
    int64 ExpectedAvailableBytes = 0;
    for (const auto& Binding : Bindings)
        if (Shadow.Contains(Binding.Package) && !SelectedPackages.Contains(Binding.Package))
            ExpectedAvailableBytes += Binding.Header.Num() + Binding.Body.Num();
    if (!TestEqual(
            TEXT("Partial-read budget includes exact available registered/cache pairs without publishing a witness"),
            AvailableSourceBytes, ExpectedAvailableBytes))
        return false;
    auto InvalidRegistered = Registered;
    for (const auto& Source : Registered)
        if (Shadow.Contains(Source->Package))
        {
            auto Invalid = MakeShared<FP4KadanSourceAsset>(*Source);
            Invalid->Body.Empty();
            InvalidRegistered.Add(Invalid);
            break;
        }
    int64 FailedBytes = 123;
    if (!TestTrue(TEXT("Refused source read resets optional bytes and cannot expose partial success"),
            P4ReadMaterialFamilySources(Settings, Reader, Shadow[0], InvalidRegistered, &Locator, Before, Missing,
                Error, &FailedBytes) == EP4MaterialFamilySourceRead::Refused &&
                FailedBytes == 0 && !Before.IsValid()))
        return false;
    FP4MaterialFamilyPreparedStagePtr InvalidPrepared;
    {
        FP4KadanScopedAcquisitionVeto Veto(Full(FPaths::ProjectDir()));
        if (!TestTrue(TEXT("Invalid-source operation control has an active pre-mutation veto"), Veto.IsActive()))
            return false;
        const auto InvalidResult = P4PrepareMissingMaterialFamilyStage(
            Context, Shadow[0], InvalidRegistered, Locator, []() { return false; }, InvalidPrepared, Error);
        if (!TestTrue(TEXT("Actual stage refuses bad supplied source before acquisition or partial publication"),
                InvalidResult == EP4MaterialFamilyStageResult::Refused && !InvalidPrepared.IsValid() &&
                    Veto.DeniedBatches() == 0 && !IFileManager::Get().DirectoryExists(*Stage) && Preserved()))
            return false;
    }
    auto WrongSettings = Settings;
    WrongSettings.Retoc = Settings.Repak;
    FP4MaterialFamilyAcquireContextPtr WrongContext;
    if (!TestTrue(TEXT("A current reader cannot authorize a context for a different selected tool"),
            !P4CreateMaterialFamilyAcquireContext(WrongSettings, Reader, WrongContext, Error) &&
                !WrongContext.IsValid() && Context->Current(Error) && Preserved()))
        return false;
    FP4MaterialFamilyPreparedStagePtr Prepared;
    bool StageRequested = false;
    ON_SCOPE_EXIT
    {
        Prepared.Reset();
        if (StageRequested)
        {
            const bool Gone = !IFileManager::Get().DirectoryExists(*Stage) && !IFileManager::Get().FileExists(*Stage);
            Report.Json->SetBoolField(TEXT("stage_absent_after_release"), Gone);
            TestTrue(TEXT("Releasing the actual witness cleans only its exact owned stage; unexpected custody refuses"),
                Gone);
        }
    };
    Report.Json->SetStringField(TEXT("stage"), TEXT("actual-selected-package-conversion"));
    StageRequested = true;
    const auto Result = P4PrepareMissingMaterialFamilyStage(
        Context, Shadow[0], Registered, Locator, []() { return false; }, Prepared, Error);
    Report.Json->SetStringField(TEXT("operation_error"), Error);
    Report.Json->SetNumberField(TEXT("operation_result"), int32(Result));
    if (!TestTrue(TEXT("Actual converter produces one complete stage with the exact selected conversion count"),
            Result == EP4MaterialFamilyStageResult::Ready && Prepared.IsValid() &&
                Prepared->ConvertedPackages() == RequiredMissing && Same(Full(Prepared->StageDirectory()), Stage) &&
                Prepared->Current(Error)))
    {
        AddError(Error);
        return false;
    }
    const auto& Ready = Prepared->Sources();
    if (!TestTrue(TEXT("Current source witness contains five Shadow members and four real shader owners"),
            Ready.IsValid() && Ready->Current(Settings, Error) && Ready->Bindings().Num() == 5 &&
                Ready->ShaderOwnerCount() == 4 &&
                Same(Ready->SourceSetSha256(),
                    TEXT("492aa569815adb8fa6a202df1d6319cbab3129ea26c6dd10afa62a397a5b986a")) &&
                Same(Ready->Master(), Shadow[0]) && Same(Ready->RegistryLeaseEpoch(), Reader->LeaseEpoch())))
        return false;
    TArray<TSharedPtr<FJsonValue>> BoundFacts;
    int32 StageBindings = 0;
    for (const auto& Binding : Ready->Bindings())
    {
        const auto* Retained = Bindings.FindByPredicate(
            [&](const FP4KadanSourceBinding& B) { return Same(B.Package, Binding.Package); });
        const bool SelectedBinding = SelectedPackages.Contains(Binding.Package);
        StageBindings += SelectedBinding ? 1 : 0;
        const bool Location = SelectedBinding
            ? (Inside(Full(Binding.HeaderPath), Stage) && Inside(Full(Binding.BodyPath), Stage))
            : (Retained && Same(Full(Binding.HeaderPath), Full(Retained->HeaderPath)) &&
                  Same(Full(Binding.BodyPath), Full(Retained->BodyPath)));
        if (!TestTrue(
                TEXT("Actual binding stays source-current with correct stage/registered custody: ") + Binding.Package,
                Retained && Shadow.Contains(Binding.Package) && Location &&
                    P4KadanSourceBindingCurrent(Binding, Error) && Binding.Header == Retained->Header &&
                    Binding.Body == Retained->Body))
            return false;
        auto Fact = MakeShared<FJsonObject>();
        Fact->SetStringField(TEXT("package"), Binding.Package);
        Fact->SetStringField(TEXT("header"), Binding.HeaderPath);
        Fact->SetStringField(TEXT("body"), Binding.BodyPath);
        Fact->SetStringField(TEXT("source_digest"), Binding.SourceDigest);
        const FString* Owner = Ready->ShaderOwners().Find(Binding.Package);
        const TSharedPtr<FJsonObject>* ExpectedRow = nullptr;
        FString ExpectedOwner;
        if (!TestTrue(TEXT("Actual nearest shader owner matches the retained independent manifest: ") + Binding.Package,
                Owner && (*Packages)->TryGetObjectField(Binding.Package, ExpectedRow) &&
                    (*ExpectedRow)->TryGetStringField(TEXT("shader_owner"), ExpectedOwner) &&
                    Same(*Owner, ExpectedOwner)))
            return false;
        Fact->SetStringField(TEXT("shader_owner"), *Owner);
        BoundFacts.Add(MakeShared<FJsonValueObject>(Fact));
    }
    if (!TestTrue(
            TEXT("Exactly the selected family bindings came from the real stage, and all guarded inputs still match"),
            StageBindings == RequiredMissing && Preserved() && Context->Current(Error) && Prepared->Current(Error)))
        return false;
    Report.Json->SetArrayField(TEXT("bindings"), BoundFacts);
    Report.Json->SetNumberField(TEXT("converted_packages"), Prepared->ConvertedPackages());
    Report.Json->SetBoolField(TEXT("current_hb_class_hard_parent_and_owner_validation"), true);
    Report.Json->SetStringField(TEXT("family_source_set_sha256"), Ready->SourceSetSha256());
    Report.Json->SetStringField(TEXT("scope"),
        TEXT(
            "Positive selected-package converter/complete-witness proof, then before-stage cancel/veto and one extra selected-package conversion canceled between stages, reusing held reader/context/fresh index. No cache publication, ordinary caller enablement, UObject import, shader compile or look qualification."));
    Prepared.Reset();
    if (!TestTrue(TEXT("Stage release completes normal cleanup before success"),
            !IFileManager::Get().DirectoryExists(*Stage) && !IFileManager::Get().FileExists(*Stage) && Preserved() &&
                Context->Current(Error)))
        return false;
    Report.Json->SetBoolField(TEXT("positive_recipe_proof_complete"), true);
    if (RequiredMissing == 1)
        Report.Json->SetBoolField(TEXT("positive_one_package_recipe_proof_complete"), true);
    Report.Json->SetBoolField(TEXT("positive_stage_cleanup_complete"), true);
    const auto StageAbsent = [&]()
    { return !IFileManager::Get().DirectoryExists(*Stage) && !IFileManager::Get().FileExists(*Stage); };
    const auto* HeldReader = Reader.Get();
    const auto* HeldContext = Context.Get();
    const FString HeldEpoch = Reader->LeaseEpoch();
    // These refusals reuse the same live objects, cached fresh index and registered
    // rows. They never request a new registry or shader-archive read.
    FP4MaterialFamilyPreparedStagePtr Cancelled;
    int32 EarlyCalls = 0;
    Report.Json->SetStringField(TEXT("stage"), TEXT("cancel-before-stage"));
    const auto EarlyResult = P4PrepareMissingMaterialFamilyStage(
        Context, Shadow[0], Registered, Locator,
        [&]()
        {
            ++EarlyCalls;
            return true;
        },
        Cancelled, Error);
    auto Early = MakeShared<FJsonObject>();
    Early->SetNumberField(TEXT("result"), int32(EarlyResult));
    Early->SetStringField(TEXT("error"), Error);
    Early->SetNumberField(TEXT("cancel_checks"), EarlyCalls);
    Early->SetBoolField(TEXT("output_witness"), Cancelled.IsValid());
    Early->SetBoolField(TEXT("stage_absent"), StageAbsent());
    Report.Json->SetObjectField(TEXT("before_stage_cancellation"), Early);
    if (!TestTrue(TEXT("Cancellation before stage is distinct and performs no staging/conversion"),
            EarlyResult == EP4MaterialFamilyStageResult::Cancelled && !Cancelled.IsValid() && EarlyCalls == 1 &&
                StageAbsent() && Preserved() && Context->Current(Error)))
        return false;
    Report.Json->SetStringField(TEXT("stage"), TEXT("veto-before-stage"));
    {
        FP4KadanScopedAcquisitionVeto Veto(Full(FPaths::ProjectDir()));
        if (!TestTrue(TEXT("Same-project dev acquisition veto is active"), Veto.IsActive()))
            return false;
        const auto VetoResult = P4PrepareMissingMaterialFamilyStage(
            Context, Shadow[0], Registered, Locator, []() { return false; }, Cancelled, Error);
        auto Denied = MakeShared<FJsonObject>();
        Denied->SetNumberField(TEXT("result"), int32(VetoResult));
        Denied->SetStringField(TEXT("error"), Error);
        Denied->SetNumberField(TEXT("denied_batches"), Veto.DeniedBatches());
        Denied->SetNumberField(TEXT("denied_packages"), Veto.DeniedPackages().Num());
        Denied->SetBoolField(TEXT("output_witness"), Cancelled.IsValid());
        Denied->SetBoolField(TEXT("stage_absent"), StageAbsent());
        Report.Json->SetObjectField(TEXT("before_stage_acquisition_veto"), Denied);
        if (!TestTrue(TEXT("Actual operation veto refuses every selected missing member before staging/conversion"),
                VetoResult == EP4MaterialFamilyStageResult::Refused && !Cancelled.IsValid() &&
                    Veto.DeniedBatches() == 1 && Veto.DeniedPackages().Num() == RequiredMissing &&
                    Veto.DeniedPackages().Contains(Selected) && StageAbsent() && Preserved() &&
                    Context->Current(Error)))
            return false;
    }
    const TSharedPtr<FJsonObject>* SelectedRow = nullptr;
    FString SelectedRelative;
    if (!(*Packages)->TryGetObjectField(Selected, SelectedRow) ||
        !(*SelectedRow)->TryGetStringField(TEXT("header"), SelectedRelative))
        return false;
    const FString SelectedStageHeader = Full(Stage / SelectedRelative),
                  SelectedStageBody = FPaths::ChangeExtension(SelectedStageHeader, TEXT("uexp"));
    if (!TestTrue(TEXT("Late cancellation begins from an absent stage with exact canonical selected bundle paths"),
            StageAbsent() && Inside(SelectedStageHeader, Stage) && Inside(SelectedStageBody, Stage) &&
                NoReparse(SelectedStageHeader) && NoReparse(SelectedStageBody) && Reader.Get() == HeldReader &&
                Context.Get() == HeldContext && Same(Reader->LeaseEpoch(), HeldEpoch) && Context->Current(Error)))
        return false;
    int32 LateCalls = 0;
    bool SawConvertedPair = false;
    FFile ConvertedHeader, ConvertedBody;
    const auto CancelAfterConversion = [&]()
    {
        ++LateCalls;
        if (!IFileManager::Get().FileExists(*SelectedStageHeader) ||
            !IFileManager::Get().FileExists(*SelectedStageBody))
            return false;
        SawConvertedPair = true;
        // Record the exact pair visible at this between-stages boundary. This
        // does not claim cancellation interrupted an already running process.
        Observe(SelectedStageHeader, ConvertedHeader);
        Observe(SelectedStageBody, ConvertedBody);
        return true;
    };
    Report.Json->SetStringField(TEXT("stage"), TEXT("cancel-after-conversion"));
    const auto LateResult = P4PrepareMissingMaterialFamilyStage(
        Context, Shadow[0], Registered, Locator, CancelAfterConversion, Cancelled, Error);
    auto Late = MakeShared<FJsonObject>();
    Late->SetNumberField(TEXT("result"), int32(LateResult));
    Late->SetStringField(TEXT("error"), Error);
    Late->SetNumberField(TEXT("cancel_checks"), LateCalls);
    Late->SetBoolField(TEXT("converted_pair_observed"), SawConvertedPair);
    Late->SetStringField(TEXT("selected_stage_header"), SelectedStageHeader);
    Late->SetStringField(TEXT("header_sha256"), ConvertedHeader.Digest);
    Late->SetStringField(TEXT("body_sha256"), ConvertedBody.Digest);
    Late->SetBoolField(TEXT("output_witness"), Cancelled.IsValid());
    Late->SetBoolField(TEXT("stage_absent_after"), StageAbsent());
    Late->SetStringField(TEXT("scope"),
        TEXT(
            "One extra selected-package conversion, then cancellation between stages after process return; no in-flight cancellation claim."));
    Report.Json->SetObjectField(TEXT("after_conversion_cancellation"), Late);
    const auto* RetainedSelected = Bindings.FindByPredicate(
        [&](const FP4KadanSourceBinding& B) { return Same(B.Package, Selected); });
    if (!TestTrue(
            TEXT(
                "Actual late cancellation occurs only after the selected real pair appears, publishes no witness and cleans its exact stage"),
            LateResult == EP4MaterialFamilyStageResult::Cancelled && !Cancelled.IsValid() && SawConvertedPair &&
                LateCalls > 1 && ConvertedHeader.Exists && ConvertedBody.Exists && RetainedSelected &&
                Same(ConvertedHeader.Digest, Proof::Hash(RetainedSelected->Header)) &&
                Same(ConvertedBody.Digest, Proof::Hash(RetainedSelected->Body)) && StageAbsent() && Preserved() &&
                Reader.Get() == HeldReader && Context.Get() == HeldContext && Same(Reader->LeaseEpoch(), HeldEpoch) &&
                Context->Current(Error)))
        return false;
    Report.Json->SetNumberField(TEXT("positive_completed_conversions"), RequiredMissing);
    Report.Json->SetNumberField(TEXT("late_cancel_conversion_pairs_observed"), 1);
    Report.Json->SetBoolField(TEXT("held_reader_context_epoch_reused"), true);
    Report.Json->SetBoolField(TEXT("source_stage_qualified"), true);
    Report.Json->SetBoolField(TEXT("ordinary_native_uobject_import_qualified"), false);
    Report.Json->SetBoolField(TEXT("completed_family_cache_qualified"), false);
    Report.Json->SetStringField(TEXT("status"), TEXT("complete"));
    Report.Json->SetBoolField(TEXT("qualified"), true);
    Report.Json->SetStringField(TEXT("stage"), TEXT("terminal"));
    return !HasAnyErrors();
#endif
}
#endif

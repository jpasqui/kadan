#if WITH_DEV_AUTOMATION_TESTS
#include "P4KadanImport.h"
#include "P4KadanTestSeams.h"
#include "P4KadanAcquisitionVeto.h"
#include "P4MaterialGraph.h"
#include "P4NativeHash.h"
#include "P4OriginalRecord.h"
#include "P4TextureContent.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Materials/Material.h"
#include "MaterialShared.h"
#include "DynamicRHI.h"
#include "RHI.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace P4OrdinaryMaterialTest
{
const FString Master = TEXT("/Game/Carrot4/MasterMaterial/chr/M_chr_Shadow_Master");
const FString Mask = TEXT("/Game/Carrot4/MasterMaterial/Texture/T_Base_M");
bool ExactArray(const TArray<FString>& A, const TArray<FString>& B)
{
    if (A.Num() != B.Num())
        return false;
    for (int32 I = 0; I < A.Num(); ++I)
        if (!P4ExactNativeString(A[I], B[I]))
            return false;
    return true;
}
bool ExactFiles(const TMap<FString, FString>& A, const TMap<FString, FString>& B)
{
    if (A.Num() != B.Num())
        return false;
    for (const auto& X : A)
    {
        bool Found = false;
        for (const auto& Y : B)
            if (P4ExactNativeString(X.Key, Y.Key) && P4ExactNativeString(X.Value, Y.Value))
            {
                Found = true;
                break;
            }
        if (!Found)
            return false;
    }
    return true;
}
bool ExactMetadata(const TMap<FName, FString>& A, const TMap<FName, FString>& B)
{
    if (A.Num() != B.Num())
        return false;
    for (const auto& X : A)
    {
        const auto* Y = B.Find(X.Key);
        if (!Y || !P4ExactNativeString(X.Value, *Y))
            return false;
    }
    return true;
}
FString Full(FString Path)
{
    Path = FPaths::ConvertRelativePathToFull(Path);
    FPaths::NormalizeFilename(Path);
    FPaths::CollapseRelativeDirectories(Path);
    FPaths::NormalizeDirectoryName(Path);
    return Path;
}
bool NoReparse(FString Path)
{
#if PLATFORM_WINDOWS
    for (Path = Full(Path); !Path.IsEmpty();)
    {
        const DWORD Attr = GetFileAttributesW(*Path);
        if (Attr == INVALID_FILE_ATTRIBUTES)
        {
            const DWORD Code = GetLastError();
            if (Code != ERROR_FILE_NOT_FOUND && Code != ERROR_PATH_NOT_FOUND)
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
    return false;
#endif
}
bool Snapshot(const FString& File, FString& Out)
{
    Out.Reset();
    if (!NoReparse(File) || IFileManager::Get().DirectoryExists(*File))
        return false;
    if (!IFileManager::Get().FileExists(*File))
    {
        Out = TEXT("absent");
        return true;
    }
    int64 Bytes = 0;
    FString Sha;
    if (!P4HashFile(File, 64ll * 1024 * 1024, Bytes, Sha))
        return false;
#if PLATFORM_WINDOWS
    const HANDLE Handle = CreateFileW(
        *File, FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    BY_HANDLE_FILE_INFORMATION Info{};
    const bool Good = Handle != INVALID_HANDLE_VALUE && GetFileInformationByHandle(Handle, &Info);
    if (Handle != INVALID_HANDLE_VALUE)
        CloseHandle(Handle);
    if (!Good)
        return false;
    Out = Sha +
        FString::Printf(TEXT("|%lld|%u:%u:%u|%u:%u|%u:%u|%u|%u"), Bytes, Info.dwVolumeSerialNumber, Info.nFileIndexHigh,
            Info.nFileIndexLow, Info.ftCreationTime.dwHighDateTime, Info.ftCreationTime.dwLowDateTime,
            Info.ftLastWriteTime.dwHighDateTime, Info.ftLastWriteTime.dwLowDateTime, Info.dwFileAttributes,
            Info.nNumberOfLinks);
    return true;
#else
    return false;
#endif
}
bool Tree(const FString& Root, TMap<FString, FString>& Out, const TSet<FString>& Except = {})
{
    Out.Reset();
    if (!NoReparse(Root))
        return false;
    TArray<FString> Files;
    IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*"), true, false);
    if (Files.Num() > 512)
        return false;
    for (const FString& File : Files)
        if (!Except.Contains(Full(File)))
        {
            FString State;
            if (!Snapshot(File, State))
                return false;
            Out.Add(Full(File), State);
        }
    return true;
}
TSet<FString> Bundle(const FString& Package)
{
    TSet<FString> Out;
    const FString Stem = FPackageName::LongPackageNameToFilename(Package);
    for (const TCHAR* Ext : {TEXT(".uasset"), TEXT(".uexp"), TEXT(".ubulk"), TEXT(".uptnl"), TEXT(".m.ubulk")})
        Out.Add(Full(Stem + Ext));
    return Out;
}
bool Absent(const FString& Package)
{
    for (const FString& File : Bundle(Package))
        if (IFileManager::Get().FileExists(*File) || IFileManager::Get().DirectoryExists(*File))
            return false;
    return FindPackage(nullptr, *Package) == nullptr;
}
TSharedPtr<FJsonObject> Read(const FString& File)
{
    FString Text;
    TSharedPtr<FJsonObject> Out;
    if (IFileManager::Get().FileSize(*File) > 2 * 1024 * 1024 || !FFileHelper::LoadFileToString(Text, *File) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out))
        return nullptr;
    return Out;
}
bool Write(const FString& File, const TSharedRef<FJsonObject>& Json)
{
    FString Text;
    return NoReparse(File) && !IFileManager::Get().FileExists(*File) && !IFileManager::Get().DirectoryExists(*File) &&
        FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text)) &&
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true) &&
        FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
} //namespace P4OrdinaryMaterialTest

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialOrdinarySparseTest, "P4MapKit.Kadan.MaterialOrdinarySparse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialOrdinarySparseTest::RunTest(const FString&)
{
    P4_REQUIRE_KADAN_TEST_SEAMS();
    using namespace P4OrdinaryMaterialTest;
    const FString Phase = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MATERIAL_ORDINARY_PHASE"));
    if (Phase.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. ")
                TEXT("Explicit import/cold/resume phase and the existing isolated Env fixture are required"));
        return true;
    }
    if (Phase != TEXT("import") && Phase != TEXT("cold") && Phase != TEXT("resume"))
    {
        AddError(TEXT("Unknown ordinary material phase"));
        return false;
    }
    const FString Native = Full(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT")));
    const FString Repo = FPaths::GetPath(FPaths::GetPath(Native));
    const FString Fixture = Repo / TEXT("private/native-postprocess-env/fixture.json");
    const auto Ledger = Read(Fixture);
    if (!TestTrue(TEXT("Only the existing isolated Env project is admitted"),
            Ledger.IsValid() && NoReparse(FPaths::ProjectDir()) &&
                Full(FPaths::ProjectDir()) == Full(Repo / TEXT("private/native-postprocess-env/project")) &&
                Full(Ledger->GetStringField(TEXT("project"))) == Full(FPaths::GetProjectFilePath()) &&
                Ledger->GetStringField(TEXT("sdk_archive_sha256")) ==
                    TEXT("1dd3e40225a9a7096df0cb845bdce9f6d41b308048355356ab653cd49bb533be")))
        return false;
    const FString ProofDir = FPaths::ProjectSavedDir() / TEXT("P4MapKit/MaterialOrdinarySparse");
    const FString ProofFile = ProofDir / (Phase + TEXT(".json"));
    if (!TestTrue(TEXT("A prior phase receipt is never overwritten"),
            NoReparse(ProofFile) && !IFileManager::Get().FileExists(*ProofFile) &&
                !IFileManager::Get().DirectoryExists(*ProofFile)))
        return false;
    auto Proof = MakeShared<FJsonObject>();
    Proof->SetStringField(TEXT("owner"), TEXT("p4-material-ordinary-sparse-v1"));
    Proof->SetStringField(TEXT("phase"), Phase);
    Proof->SetNumberField(TEXT("process_id"), FPlatformProcess::GetCurrentProcessId());
    ON_SCOPE_EXIT
    {
        Proof->SetBoolField(TEXT("passed"), !HasAnyErrors());
        if (!Write(ProofFile, Proof))
            AddError(TEXT("Cannot retain owned ordinary material phase receipt"));
    };
    const FString View = Full(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MATERIAL_FAMILY_SOURCES")));
    if (!TestTrue(TEXT("Existing source view is explicit and unchanged in place"),
            View == Full(Repo / TEXT("private/material-family-source-view")) && NoReparse(View)))
        return false;
    FP4KadanImportSettings Settings;
    Settings.GameFolder = View / TEXT("Carrot4/Content/Carrot4/MasterMaterial/chr");
    Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    Settings.Retoc = Repo / TEXT("private/retoc/retoc.exe");
    Settings.Repak = Repo / TEXT("private/repak/repak.exe");
    Settings.AllowGameBackedLevels = false;
    Settings.ShaderArchive = Repo /
        TEXT(
            "private/native-development/project/Saved/Kadan/ConvertedOriginals/Carrot4/Content/ShaderArchive-Carrot4-GLSL_SWITCH.ushaderbytecode");
    const FString Cache = FPaths::ProjectSavedDir() / TEXT("Kadan/ConvertedOriginals"),
                  Stamp = Full(Cache / TEXT("converted-from.txt"));
    const FString Stage = FPaths::ProjectSavedDir() / TEXT("Kadan/MaterialFamilyAcquireStage");
    const FString Originals = FPaths::ProjectSavedDir() / TEXT("Kadan/OriginalPackages");
    if (!TestTrue(TEXT("No unowned stage or original-package workspace is removed"),
            NoReparse(Stage) && NoReparse(Originals) && !IFileManager::Get().FileExists(*Stage) &&
                !IFileManager::Get().DirectoryExists(*Stage) && !IFileManager::Get().FileExists(*Originals) &&
                !IFileManager::Get().DirectoryExists(*Originals)))
        return false;
    if (IFileManager::Get().DirectoryExists(*Cache) || IFileManager::Get().FileExists(*Cache))
    {
        FString Current, Error;
        if (!TestTrue(TEXT("Existing cache must already be current; no invalidation"),
                P4KadanRetainedConversionCacheCurrent(Settings, Current, Error)))
        {
            AddError(Error);
            return false;
        }
    }
    TSet<FString> Allowed;
    if (Phase == TEXT("import"))
    {
        Allowed.Append(Bundle(Master));
        Allowed.Append(Bundle(Mask));
    }
    TMap<FString, FString> ContentBefore, SourceBefore, CacheBefore;
    FString FixtureBefore, StampBefore;
    if (!TestTrue(TEXT("Bounded content/source/cache/fixture snapshots"),
            Tree(FPaths::ProjectContentDir(), ContentBefore, Allowed) && Tree(View, SourceBefore) &&
                Tree(Cache, CacheBefore, {Stamp}) && Snapshot(Fixture, FixtureBefore) && Snapshot(Stamp, StampBefore)))
        return false;
    ON_SCOPE_EXIT
    {
        TMap<FString, FString> ContentAfter, SourceAfter, CacheAfter;
        FString FixtureAfter, StampAfter, CurrentCache, CacheError;
        const bool Preserved = Tree(FPaths::ProjectContentDir(), ContentAfter, Allowed) &&
            ExactFiles(ContentBefore, ContentAfter) && Tree(View, SourceAfter) &&
            ExactFiles(SourceBefore, SourceAfter) && Tree(Cache, CacheAfter, {Stamp}) &&
            ExactFiles(CacheBefore, CacheAfter) && Snapshot(Fixture, FixtureAfter) &&
            P4ExactNativeString(FixtureBefore, FixtureAfter) && Snapshot(Stamp, StampAfter) &&
            (StampBefore == TEXT("absent") ? P4KadanRetainedConversionCacheCurrent(Settings, CurrentCache, CacheError)
                                           : P4ExactNativeString(StampBefore, StampAfter));
        Proof->SetBoolField(TEXT("other_content_source_cache_fixture_preserved"), Preserved);
        TestTrue(TEXT("All prior content, exact source view and retained cache packages preserved"), Preserved);
        TestFalse(TEXT("Scoped material stage removed"),
            IFileManager::Get().DirectoryExists(*Stage) || IFileManager::Get().FileExists(*Stage));
        TestFalse(TEXT("Raw forward closure never uses destructive OriginalPackages staging"),
            IFileManager::Get().DirectoryExists(*Originals) || IFileManager::Get().FileExists(*Originals));
    };
    const TArray<FString> Reverse = {TEXT("/Game/Carrot4/MasterMaterial/chr/MI_chr_Shadow"),
        TEXT("/Game/Carrot4/MasterMaterial/chr/MI_chr_Shadow_Dissolve"),
        TEXT("/Game/Carrot4/Characters/Teki/DiscoDamagumo/MI_chr_DiscoDamagumo_00_Shadow"),
        TEXT("/Game/Carrot4/Characters/Teki/HageDamagumo/MI_chr_HageDamagumo_00_Shadow")};
    for (const FString& Package : Reverse)
        if (!TestTrue(TEXT("Reverse MIC metadata has no destination"), Absent(Package)))
            return false;
    TSharedPtr<FJsonObject> ImportProof;
    if (Phase != TEXT("import"))
    {
        ImportProof = Read(ProofDir / TEXT("import.json"));
        if (!TestTrue(TEXT("Import phase actually passed before separate cold/resume"),
                ImportProof.IsValid() && ImportProof->GetBoolField(TEXT("passed")) &&
                    ImportProof->GetStringField(TEXT("owner")) == TEXT("p4-material-ordinary-sparse-v1")))
            return false;
    }
    if (ImportProof.IsValid())
        if (!TestTrue(TEXT("Cold and Resume use a distinct editor process from import"),
                ImportProof->GetNumberField(TEXT("process_id")) > 0 &&
                    ImportProof->GetNumberField(TEXT("process_id")) != FPlatformProcess::GetCurrentProcessId()))
            return false;
    if (Phase == TEXT("import"))
    {
        if (!TestTrue(TEXT("Both native output bundles start absent"), Absent(Master) && Absent(Mask)))
            return false;
        TArray<FString> MissingPackages = Reverse;
        MissingPackages.Add(Master);
        MissingPackages.Add(Mask);
        for (const FString& Package : MissingPackages)
        {
            const FString Header = Cache / TEXT("Carrot4/Content") / (Package.Mid(6) + TEXT(".uasset"));
            for (const TCHAR* Ext : {TEXT("uasset"), TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl"), TEXT("m.ubulk")})
            {
                const FString File = FPaths::ChangeExtension(Header, Ext);
                if (!TestTrue(TEXT("Ordinary gate starts with wholly absent source-cache bundles"),
                        !IFileManager::Get().FileExists(*File) && !IFileManager::Get().DirectoryExists(*File)))
                    return false;
            }
        }
    }
    if (Phase == TEXT("cold"))
        if (!TestTrue(TEXT("Cold process starts with material and texture packages unloaded"),
                !FindPackage(nullptr, *Master) && !FindPackage(nullptr, *Mask)))
            return false;
    if (Phase != TEXT("cold"))
    {
        TUniquePtr<FP4KadanScopedAcquisitionVeto> Veto;
        if (Phase == TEXT("resume"))
            Veto = MakeUnique<FP4KadanScopedAcquisitionVeto>(FPaths::ProjectDir());
        TArray<TSharedPtr<FP4KadanSourceAsset>> Scan;
        TArray<FString> ScanErrors;
        if (!TestTrue(TEXT("Actual existing three-pair folder scans normally"),
                P4KadanScanGame(Settings.GameFolder, Scan, ScanErrors) && ScanErrors.Num() == 0 && Scan.Num() == 3))
            return false;
        const auto* Root = Scan.FindByPredicate(
            [](const TSharedPtr<FP4KadanSourceAsset>& Row) { return Row->Package == Master; });
        if (!TestTrue(TEXT("All thirteen original raw imports remain on the scanned master"),
                Root && (*Root)->Dependencies.Num() == 13))
            return false;
        const FP4KadanSourceAsset RawRoot = **Root;
        const auto Planned = P4KadanPlanImport(Settings, {Master}, true);
        if (!TestTrue(TEXT("Genuine settings planner defers unused material raw dependencies"),
                Planned.Problems.Num() == 0 && Planned.bMaterialExecutionDeferred && Planned.Roots == 1 &&
                    Planned.RequestedPackages.Num() == 1 && Planned.RequestedPackages[0] == Master &&
                    Planned.Assets.Num() == 1 && Planned.Assets[0]->Package == Master))
        {
            for (const FString& Error : Planned.Problems)
                AddError(Error);
            return false;
        }
        const auto* PlannedRoot = Planned.Scanned.FindByPredicate(
            [](const TSharedPtr<FP4KadanSourceAsset>& Row) { return Row->Package == Master; });
        const auto RawRetained = [&]()
        {
            return PlannedRoot && ExactArray((*PlannedRoot)->Dependencies, RawRoot.Dependencies) &&
                ExactArray((*PlannedRoot)->SoftDependencies, RawRoot.SoftDependencies) &&
                ExactArray(Planned.Assets[0]->Dependencies, RawRoot.Dependencies) &&
                ExactArray(Planned.Assets[0]->SoftDependencies, RawRoot.SoftDependencies) &&
                ExactArray((*Root)->Dependencies, RawRoot.Dependencies) &&
                ExactArray((*Root)->SoftDependencies, RawRoot.SoftDependencies);
        };
        if (!TestTrue(TEXT("Actual plan retains every raw hard/soft source dependency"), RawRetained()))
            return false;
        // This is exactly the public map wrapper's settings-aware planner/run
        // pair, retaining the genuine plan so raw inventory mutation is visible.
        const auto Imported = P4KadanRunImport(Settings, Planned, true);
        const auto Report = Read(FPaths::ProjectSavedDir() / TEXT("Kadan/import-result.json"));
        TestTrue(TEXT("Normal import leaves the genuine scanned and planned raw inventory intact"), RawRetained());
        Proof->SetStringField(TEXT("entry_point"), TEXT("public-settings-planner-and-run-import"));
        if (!TestTrue(TEXT("Ordinary public import completes without failed assets or game identity fallback"),
                Report.IsValid() && Report->GetBoolField(TEXT("ok")) && Imported.NotImported.Num() == 0 &&
                    Imported.KeptAsTheGames.Num() == 0 && Imported.OriginalIdentityPackages == 0))
            return false;
        Proof->SetObjectField(TEXT("ordinary_report"), Report);
        const TSharedPtr<FJsonObject>* AssetsField = nullptr;
        const TSharedPtr<FJsonObject>* ReplyField = nullptr;
        if (!TestTrue(TEXT("Ordinary report contains the actual requested root"),
                Report->TryGetObjectField(TEXT("assets"), AssetsField) &&
                    (*AssetsField)->TryGetObjectField(Master, ReplyField)))
            return false;
        const auto Assets = *AssetsField, Reply = *ReplyField;
        if (Phase == TEXT("resume"))
        {
            TestTrue(TEXT("Resume preserves existing asset without acquisition"),
                Veto->IsActive() && Veto->DeniedBatches() == 0 &&
                    Reply->GetStringField(TEXT("state")) == TEXT("existing asset preserved") &&
                    !Reply->HasField(TEXT("material_family_sources")));
        }
        else
        {
            const TSharedPtr<FJsonObject>* FamilyField = nullptr;
            const TArray<TSharedPtr<FJsonValue>>* RequestFields = nullptr;
            if (!TestTrue(TEXT("Ordinary root has real family and typed resource proof"),
                    Reply->TryGetObjectField(TEXT("material_family_sources"), FamilyField) &&
                        (*FamilyField)->TryGetArrayField(TEXT("typed_used_resources"), RequestFields)))
                return false;
            const auto Family = *FamilyField;
            const auto& Requests = *RequestFields;
            TestTrue(TEXT("Only genuine root and shader-used mask execute"),
                Imported.Imported.Num() == 2 && Imported.Imported.Contains(Master) &&
                    Imported.Imported.Contains(Mask) && Assets->Values.Num() == 2);
            TestTrue(TEXT("Native saved graph and real renderer compile"),
                Reply->GetBoolField(TEXT("valid")) && Reply->GetBoolField(TEXT("saved")) &&
                    Reply->GetBoolField(TEXT("compiled")) && Reply->GetBoolField(TEXT("renderer_active")) &&
                    Reply->GetArrayField(TEXT("compile_errors")).Num() == 0 &&
                    !Reply->HasField(TEXT("recovered_graph_error")));
            TestTrue(TEXT("Complete family is metadata only and never published to cache"),
                Family->GetNumberField(TEXT("members")) == 5 && Family->GetNumberField(TEXT("shader_owners")) == 4 &&
                    Family->GetBoolField(TEXT("metadata_only")) && !Family->GetBoolField(TEXT("cache_publication")) &&
                    Family->GetBoolField(TEXT("source_custody_held")));
            TestTrue(TEXT("Exactly two missing reverse metadata pairs plus one genuine used mask converted"),
                Family->GetNumberField(TEXT("converted_packages")) == 3 &&
                    Family->GetStringField(TEXT("source_set_sha256")) ==
                        TEXT("492aa569815adb8fa6a202df1d6319cbab3129ea26c6dd10afa62a397a5b986a"));
            TestTrue(TEXT("One actual archive read/digest, complete initial and hydrated owner traces"),
                Family->GetNumberField(TEXT("archive_read_attempts")) == 1 &&
                    Family->GetNumberField(TEXT("archive_digest_computes")) == 1 &&
                    Family->GetNumberField(TEXT("used_owner_trace_calls")) == 8 &&
                    Family->GetNumberField(TEXT("family_recovery_builds")) == 1 &&
                    Family->GetStringField(TEXT("archive_sha256")) ==
                        TEXT("04a142f5025e51f8ac072d7fce3033706fc18de5ebc07072f51d658cf861792f"));
            TestTrue(TEXT("Actual typed used resource is M; cached D/N/function references are not executed"),
                Requests.Num() == 1 && Requests[0]->AsObject()->GetStringField(TEXT("package")) == Mask &&
                    Requests[0]->AsObject()->GetStringField(TEXT("native_class")) == TEXT("/Script/Engine.Texture2D") &&
                    !Requests[0]->AsObject()->GetStringField(TEXT("sampler_type")).IsEmpty());
        }
    }
    UMaterial* Material = LoadObject<UMaterial>(
        nullptr, *(Master + TEXT(".M_chr_Shadow_Master")), nullptr, LOAD_NoWarn | LOAD_Quiet);
    UTexture* Texture = LoadObject<UTexture>(nullptr, *(Mask + TEXT(".T_Base_M")), nullptr, LOAD_NoWarn | LOAD_Quiet);
    FString Error;
    FP4OriginalRecord Record, TextureRecord;
    if (!TestTrue(TEXT("Saved native material and texture are genuine unchanged original references"),
            Material && Texture && Material->Expressions.Num() == 11 && !Material->GetOutermost()->IsDirty() &&
                !Texture->GetOutermost()->IsDirty() && P4ReadOriginalRecord(Material, Record, Error) &&
                P4ReadOriginalRecord(Texture, TextureRecord, Error) && Record.Package == Master &&
                Record.Class == TEXT("/Script/Engine.Material") && Record.Kind == TEXT("material") &&
                Record.ComparisonSchema == TEXT("material-native-state-v1") &&
                Record.SourceDigestKind == TEXT("material-graph-v2-md5") &&
                Record.SourceDigest == TEXT("10a3568226fe664e596efbbf32e184de") &&
                Record.ComparisonDigest.Len() == 64 &&
                P4ExactNativeString(P4MaterialComparisonState(Material, Error), Record.ComparisonDigest) &&
                Record.Fidelity == TEXT("recovered-pixel-contract") && Record.ReferenceOnly && Record.ReadOnly &&
                TextureRecord.Package == Mask && TextureRecord.Class == TEXT("/Script/Engine.Texture2D") &&
                TextureRecord.Kind == TEXT("texture") && !TextureRecord.ReferenceOnly && TextureRecord.ReadOnly &&
                TextureRecord.SourceDigestKind == TEXT("texture-source-v1") &&
                TextureRecord.ComparisonSchema == TEXT("texture-state-v1")))
    {
        AddError(Error);
        return false;
    }
    const FString Graph = P4MaterialGraphJson(Material, Error);
    const FString TextureState = P4TextureState(Texture);
    TestTrue(TEXT("Actual native texture content still equals its genuine imported baseline"),
        !TextureState.IsEmpty() && P4ExactNativeString(TextureState, TextureRecord.ComparisonDigest));
    TMap<FName, FString> Metadata;
    if (const auto* Values = UMetaData::GetMapForObject(Material))
        Metadata = *Values;
    TArray<FString> CompileErrors;
    TestTrue(TEXT("Actual SM5 shader map without compiler errors"),
        !GUsingNullRHI && GMaxRHIFeatureLevel == ERHIFeatureLevel::SM5 &&
            P4WaitForMaterialShaders(Material, CompileErrors, Error) && CompileErrors.Num() == 0);
    TestTrue(TEXT("Compilation leaves native source record and content unchanged"),
        Error.IsEmpty() && !Graph.IsEmpty() && !Material->GetOutermost()->IsDirty() &&
            P4ExactNativeString(P4MaterialGraphJson(Material, Error), Graph) && UMetaData::GetMapForObject(Material) &&
            ExactMetadata(Metadata, *UMetaData::GetMapForObject(Material)));
    if (ImportProof.IsValid())
        TestTrue(TEXT("Separate process preserves exact saved graph and texture source baseline"),
            P4ExactNativeString(ImportProof->GetStringField(TEXT("native_graph")), Graph) &&
                P4ExactNativeString(ImportProof->GetStringField(TEXT("texture_state")), TextureState) &&
                P4ExactNativeString(ImportProof->GetStringField(TEXT("material_source_digest")), Record.SourceDigest));
    Proof->SetStringField(TEXT("native_graph"), Graph);
    Proof->SetStringField(TEXT("texture_state"), TextureState);
    Proof->SetStringField(TEXT("material_source_digest"), Record.SourceDigest);
    const FString MaterialRecord = Material->GetOutermost()->GetMetaData()->GetValue(
        Material, TEXT("P4OriginalRecord"));
    const FString TextureRecordText = Texture->GetOutermost()->GetMetaData()->GetValue(
        Texture, TEXT("P4OriginalRecord"));
    const FString TextureBaseline = Texture->GetOutermost()->GetMetaData()->GetValue(
        Texture, TEXT("P4NativeTextureBaseline"));
    TestTrue(TEXT("Actual native provenance and source baseline metadata are present"),
        !MaterialRecord.IsEmpty() && !TextureRecordText.IsEmpty() && !TextureBaseline.IsEmpty());
    Proof->SetStringField(TEXT("material_record"), MaterialRecord);
    Proof->SetStringField(TEXT("texture_record"), TextureRecordText);
    Proof->SetStringField(TEXT("texture_baseline"), TextureBaseline);
    if (ImportProof.IsValid())
        TestTrue(TEXT("Complete native provenance records survive unchanged in distinct process"),
            P4ExactNativeString(ImportProof->GetStringField(TEXT("material_record")), MaterialRecord) &&
                P4ExactNativeString(ImportProof->GetStringField(TEXT("texture_record")), TextureRecordText) &&
                P4ExactNativeString(ImportProof->GetStringField(TEXT("texture_baseline")), TextureBaseline));
    auto OutputFiles = MakeShared<FJsonObject>();
    for (const FString& Package : {Master, Mask})
        for (const FString& File : Bundle(Package))
        {
            FString State;
            if (!Snapshot(File, State))
            {
                AddError(TEXT("Cannot bind actual native output bundle"));
                return false;
            }
            OutputFiles->SetStringField(File, State);
            if (ImportProof.IsValid())
                TestTrue(TEXT("Distinct process preserves native output file bytes/identity/flags/time"),
                    P4ExactNativeString(
                        ImportProof->GetObjectField(TEXT("native_output_files"))->GetStringField(File), State));
        }
    Proof->SetObjectField(TEXT("native_output_files"), OutputFiles);
    Proof->SetNumberField(TEXT("native_nodes"), Material->Expressions.Num());
    Proof->SetBoolField(TEXT("actual_sm5"), !GUsingNullRHI && GMaxRHIFeatureLevel == ERHIFeatureLevel::SM5);
    for (const FString& Package : Reverse)
        TestTrue(TEXT("Reverse MIC destinations remain absent after ordinary work"), Absent(Package));
    return !HasAnyErrors();
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "P4MaterialFamilySources.h"
#include "P4KadanTestSeams.h"
#include "P4RetainedMaterialFixture.h"
#include "P4KadanImport.h"
#include "P4KadanAcquisitionVeto.h"
#include "P4NativeHash.h"
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
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace P4FamilySourceTest
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
struct FReport
{
    FAutomationTestBase& Test;
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    explicit FReport(FAutomationTestBase& In) : Test(In)
    {
        Json->SetBoolField(TEXT("passed"), false);
        Json->SetStringField(TEXT("stage"), TEXT("preflight"));
    }
    ~FReport()
    {
        bool Passed = false;
        Json->TryGetBoolField(TEXT("passed"), Passed);
        Json->SetBoolField(TEXT("passed"), Passed && !Test.HasAnyErrors());
        const FString File = FPaths::ProjectSavedDir() / TEXT("P4MapKit/MaterialFamilySources/registered-only.json");
        FString Text;
        auto Writer = TJsonWriterFactory<>::Create(&Text);
        if (!FJsonSerializer::Serialize(Json, Writer) ||
            !IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true) ||
            !FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            Test.AddError(TEXT("Cannot retain registered-only material family proof"));
    }
};
TMap<FString, FString> LiveOriginalObjects()
{
    TMap<FString, FString> Out;
    for (TObjectIterator<UObject> It; It; ++It)
    {
        UObject* Object = *It;
        if (!Object->IsA<UMaterialInterface>() && !Object->IsA<UWorld>() && !Object->IsA<UPackage>())
            continue;
        if (!Object->GetOutermost()->GetName().StartsWith(TEXT("/Game/"), ESearchCase::CaseSensitive))
            continue;
        Out.Add(Object->GetPathName(),
            Object->GetClass()->GetPathName() +
                FString::Printf(TEXT("|address=%p|flags=%08x|dirty=%d"), static_cast<void*>(Object),
                    uint32(Object->GetFlags()), Object->GetOutermost()->IsDirty() ? 1 : 0));
    }
    return Out;
}
bool ContentFiles(const TArray<TArray<FString>>& Families, TMap<FString, FString>& Out)
{
    Out.Reset();
    for (const auto& Family : Families)
        for (const FString& Package : Family)
            if (!Proof::Files(Package, Out))
                return false;
    return true;
}
bool SourcesUnchanged(
    const TArray<TSharedPtr<FP4KadanSourceAsset>>& Sources, const TArray<TSharedPtr<FP4KadanSourceAsset>>& Originals)
{
    if (Sources.Num() != Originals.Num())
        return false;
    for (int32 Index = 0; Index < Sources.Num(); ++Index)
    {
        if (!Sources[Index].IsValid() || !Originals[Index].IsValid())
            return false;
        const auto& A = *Sources[Index];
        const auto& B = *Originals[Index];
        if (!P4ExactNativeString(A.Package, B.Package) || !P4ExactNativeString(A.Kind, B.Kind) ||
            !P4ExactNativeString(A.Header, B.Header) || !P4ExactNativeString(A.Body, B.Body) ||
            A.SourceBytes != B.SourceBytes || A.Dependencies != B.Dependencies ||
            A.SoftDependencies != B.SoftDependencies)
            return false;
    }
    return true;
}
bool SameStrings(const TArray<FString>& A, const TArray<FString>& B)
{
    if (A.Num() != B.Num())
        return false;
    for (int32 Index = 0; Index < A.Num(); ++Index)
        if (!P4ExactNativeString(A[Index], B[Index]))
            return false;
    return true;
}
TSharedPtr<FP4KadanSourceAsset> CopySource(const TSharedPtr<FP4KadanSourceAsset>& Source)
{
    if (!Source.IsValid())
        return TSharedPtr<FP4KadanSourceAsset>();
    return MakeShared<FP4KadanSourceAsset>(*Source);
}
TArray<TSharedPtr<FP4KadanSourceAsset>> ReplaceSource(
    const TArray<TSharedPtr<FP4KadanSourceAsset>>& Sources, const TSharedPtr<FP4KadanSourceAsset>& Replacement)
{
    auto Out = Sources;
    for (auto& Source : Out)
        if (Source.IsValid() && P4ExactNativeString(Source->Package, Replacement->Package))
            Source = Replacement;
    return Out;
}
bool NoReparse(FString Path)
{
#if PLATFORM_WINDOWS
    Path = Full(Path);
    if (Path.IsEmpty())
        return false;
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
// Independent tiny files, never hardlinks or source-cache artifacts. Cleanup
// touches only the exact names/inodes this instance created, no recursive delete.
struct FControlledPair
{
    FString Root;
    struct FFile
    {
        FString Path;
        uint32 Volume = 0, High = 0, Low = 0;
        bool Present = false;
    };
    FFile Header, Body;
    bool DirectoryCreated = false;
    explicit FControlledPair(const FString& In) : Root(Full(In))
    {
        Header.Path = Root / TEXT("source-copy.uasset");
        Body.Path = Root / TEXT("source-copy.uexp");
    }
    bool Initialize()
    {
        const FString Expected = Full(FPaths::ProjectSavedDir() / TEXT("P4MapKit/MaterialFamilySources/controlled"));
        if (!P4ExactNativeString(Root, Expected) || !NoReparse(Root) || IFileManager::Get().FileExists(*Root))
            return false;
        TArray<FString> Existing;
        IFileManager::Get().FindFiles(Existing, *(Root / TEXT("*")), true, true);
        if (Existing.Num() || !IFileManager::Get().MakeDirectory(*Root, true) || !NoReparse(Root))
            return false;
        DirectoryCreated = true;
        return true;
    }
    bool Write(FFile& File, const TArray<uint8>& Bytes)
    {
#if PLATFORM_WINDOWS
        if (!DirectoryCreated || !NoReparse(File.Path) || !(File.Path == Header.Path || File.Path == Body.Path) ||
            Bytes.Num() <= 0 || Bytes.Num() > 1024 * 1024)
            return false;
        const HANDLE Handle = CreateFileW(*File.Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            File.Present ? OPEN_EXISTING : CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (Handle == INVALID_HANDLE_VALUE)
            return false;
        BY_HANDLE_FILE_INFORMATION Info{};
        bool Okay = GetFileInformationByHandle(Handle, &Info) != 0;
        if (Okay && File.Present)
            Okay = Info.dwVolumeSerialNumber == File.Volume && Info.nFileIndexHigh == File.High &&
                Info.nFileIndexLow == File.Low;
        if (Okay && !File.Present)
        {
            File.Volume = Info.dwVolumeSerialNumber;
            File.High = Info.nFileIndexHigh;
            File.Low = Info.nFileIndexLow;
            File.Present = true;
        }
        DWORD Written = 0;
        if (Okay)
            Okay = WriteFile(Handle, Bytes.GetData(), DWORD(Bytes.Num()), &Written, nullptr) &&
                Written == DWORD(Bytes.Num()) && SetEndOfFile(Handle) && FlushFileBuffers(Handle);
        CloseHandle(Handle);
        return Okay;
#else
        (void)File;
        (void)Bytes;
        return false;
#endif
    }
    bool Remove(FFile& File)
    {
        if (!File.Present)
            return true;
#if PLATFORM_WINDOWS
        if (!NoReparse(File.Path))
            return false;
        const HANDLE Handle = CreateFileW(*File.Path, FILE_READ_ATTRIBUTES | DELETE, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (Handle == INVALID_HANDLE_VALUE)
            return false;
        BY_HANDLE_FILE_INFORMATION Info{};
        const bool Same = GetFileInformationByHandle(Handle, &Info) && Info.dwVolumeSerialNumber == File.Volume &&
            Info.nFileIndexHigh == File.High && Info.nFileIndexLow == File.Low;
        FILE_DISPOSITION_INFO Disposition{true};
        const bool Removed = Same &&
            SetFileInformationByHandle(Handle, FileDispositionInfo, &Disposition, sizeof(Disposition));
        CloseHandle(Handle);
        if (!Removed)
            return false;
        File.Present = false;
        return true;
#else
        return false;
#endif
    }
    bool Cleanup()
    {
        bool Okay = Remove(Body);
        Okay = Remove(Header) && Okay;
        if (DirectoryCreated)
        {
            if (!Okay || !NoReparse(Root) || !IFileManager::Get().DeleteDirectory(*Root, false, false))
                return false;
            DirectoryCreated = false;
        }
        return Okay;
    }
    ~FControlledPair() { Cleanup(); }
};
} //namespace P4FamilySourceTest

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ActualMaterialFamilySources, "P4MapKit.Kadan.ActualMaterialFamilySources",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ActualMaterialFamilySources::RunTest(const FString&)
{
    P4_REQUIRE_KADAN_TEST_SEAMS();
    using namespace P4FamilySourceTest;
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MATERIAL_FAMILY_READ")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "Skipped registered-only material family gate; requires P4_NATIVE_MATERIAL_FAMILY_READ=1 and the exact owned twelve-source view"));
        return true;
    }
    FReport Report(*this);
    const FString Native = Full(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT")));
    const FString Repo = FPaths::GetPath(FPaths::GetPath(Native));
    if (!TestTrue(TEXT("Actual source wrapper proof is restricted to the owned native fixture"),
            !Native.IsEmpty() &&
                FPaths::IsSamePath(Full(FPaths::ProjectDir()), Repo / TEXT("private/native-development/project")) &&
                IsInGameThread()))
        return false;
    FP4KadanScopedAcquisitionVeto AcquisitionVeto(Full(FPaths::ProjectDir()));
    if (!TestTrue(TEXT("No-conversion fixture guard is active"), AcquisitionVeto.IsActive()))
        return false;
    ON_SCOPE_EXIT
    {
        const bool NoConversion = AcquisitionVeto.DeniedBatches() == 0 && AcquisitionVeto.DeniedPackages().Num() == 0;
        Report.Json->SetBoolField(TEXT("no_source_conversion_requested"), NoConversion);
        TestTrue(TEXT("Registered-only proof never requests original source conversion"), NoConversion);
    };
    const auto LiveBefore = LiveOriginalObjects();
    ON_SCOPE_EXIT
    {
        const bool Same = LiveBefore.OrderIndependentCompareEqual(LiveOriginalObjects());
        Report.Json->SetBoolField(TEXT("loaded_original_identity_flags_dirty_unchanged"), Same);
        TestTrue(TEXT("Original material/world/package identity, flags and dirty state are unchanged"), Same);
    };
    FP4KadanImportSettings Settings;
    Settings.GamePaks = Full(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS")));
    Settings.Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    Settings.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    if (Settings.Retoc.IsEmpty())
        Settings.Retoc = Repo / TEXT("private/retoc/retoc.exe");
    if (Settings.Repak.IsEmpty())
        Settings.Repak = Repo / TEXT("private/repak/repak.exe");
    Settings.ConvertedCache = FPaths::ProjectSavedDir() / TEXT("Kadan/ConvertedOriginals");
    Settings.ShaderArchive = Settings.ConvertedCache /
        TEXT("Carrot4/Content/ShaderArchive-Carrot4-GLSL_SWITCH.ushaderbytecode");
    TArray<TSharedPtr<FP4KadanSourceAsset>> Sources;
    TArray<FP4KadanSourceBinding> Bindings;
    FString Manifest, ManifestHash, ArchiveState, Error;
    if (!TestTrue(TEXT("Exact existing twelve-source view/manifest/archive is read without planning or acquisition"),
            Proof::FamilyView(Settings.ShaderArchive, Sources, Bindings, Manifest, ManifestHash, ArchiveState, Error)))
    {
        AddError(Error);
        return false;
    }
    const auto Families = Proof::Families();
    TMap<FString, FString> FilesBefore, SourceBefore;
    if (!TestTrue(TEXT("Exactly twelve pairs and twenty-four source files are guarded"),
            Sources.Num() == 12 && Bindings.Num() == 12 && Proof::SourceFiles(Bindings, SourceBefore) &&
                SourceBefore.Num() == 24 && ContentFiles(Families, FilesBefore)))
        return false;
    TArray<TSharedPtr<FP4KadanSourceAsset>> SourceDescriptorsBefore;
    for (const auto& Source : Sources)
        SourceDescriptorsBefore.Add(CopySource(Source));
    FP4KadanImportPlan UnexecutedPlan;
    UnexecutedPlan.Scanned = Sources;
    ON_SCOPE_EXIT
    {
        TMap<FString, FString> FilesAfter, SourceAfter;
        FString ArchiveAfter;
        TArray<uint8> ManifestAfter;
        bool Same = ContentFiles(Families, FilesAfter) && FilesBefore.OrderIndependentCompareEqual(FilesAfter) &&
            Proof::SourceFiles(Bindings, SourceAfter) && SourceBefore.OrderIndependentCompareEqual(SourceAfter) &&
            Proof::ArchiveFile(Settings.ShaderArchive, ArchiveAfter) &&
            P4ExactNativeString(ArchiveAfter, ArchiveState) && FFileHelper::LoadFileToArray(ManifestAfter, *Manifest) &&
            P4ExactNativeString(Proof::Hash(ManifestAfter), ManifestHash) &&
            SourcesUnchanged(Sources, SourceDescriptorsBefore) &&
            SourcesUnchanged(UnexecutedPlan.Scanned, SourceDescriptorsBefore) && UnexecutedPlan.Assets.Num() == 0;
        for (const auto& Binding : Bindings)
            Same = P4KadanSourceBindingCurrent(Binding, Error) && Same;
        Report.Json->SetBoolField(TEXT("original_content_source_archive_manifest_plan_preserved"), Same);
        TestTrue(
            TEXT(
                "All original content/source files, descriptors and empty executable plan remain unchanged on every exit"),
            Same);
    };
    Report.Json->SetStringField(TEXT("manifest"), Manifest);
    Report.Json->SetStringField(TEXT("manifest_sha256"), ManifestHash);
    Report.Json->SetStringField(TEXT("archive_state"), ArchiveState);
    Report.Json->SetNumberField(TEXT("source_pairs"), Bindings.Num());
    Report.Json->SetBoolField(TEXT("cache_branch_qualified"), false);
    Report.Json->SetBoolField(TEXT("acquisition_adapter_enabled"), false);
    FP4ExportContainerTools Tools;
    Tools.Retoc = Settings.Retoc;
    Tools.Repak = Settings.Repak;
    Tools.TimeoutSeconds = 120;
    FP4CurrentGameRegistryPtr Reader, Again;
    const FString Work = FPaths::ProjectSavedDir() / TEXT("P4MapKit/MaterialFamilySources/registry-work");
    Report.Json->SetStringField(TEXT("stage"), TEXT("current-registry"));
    if (!TestTrue(TEXT("Acquire current selected registry once for the actual wrapper"),
            P4AcquireCurrentGameRegistry(Tools, Settings.GamePaks, Work, Reader, Error)))
    {
        AddError(Error);
        return false;
    }
    const FString Epoch = Reader->LeaseEpoch();
    Report.Json->SetStringField(TEXT("registry_sha256"), Reader->RegistrySha256());
    Report.Json->SetStringField(TEXT("material_rows_sha256"), Reader->MaterialRowsSha256());
    Report.Json->SetStringField(TEXT("lease_epoch"), Epoch);
    if (!TestTrue(TEXT("Second request reuses exactly the held object/epoch without another registry acquisition"),
            P4AcquireCurrentGameRegistry(Tools, Settings.GamePaks, Work, Again, Error) && Again.Get() == Reader.Get() &&
                P4ExactNativeString(Again->LeaseEpoch(), Epoch)))
        return false;
    TArray<FP4MaterialFamilySourcesPtr> ReadyFamilies;
    TArray<TSharedPtr<FJsonValue>> FamilyFacts;
    Report.Json->SetStringField(TEXT("stage"), TEXT("registered-ready"));
    for (int32 Index = 0; Index < Families.Num(); ++Index)
    {
        const auto& Family = Families[Index];
        FP4MaterialFamilySourcesPtr Ready;
        TArray<FString> Missing;
        if (!TestTrue(TEXT("Complete actual registered family is ready: ") + Family[0],
                P4ReadMaterialFamilySources(Settings, Reader, Family[0], Sources, nullptr, Ready, Missing, Error) ==
                        EP4MaterialFamilySourceRead::Ready &&
                    Ready.IsValid()))
        {
            AddError(Error);
            return false;
        }
        Report.Json->SetStringField(TEXT("current_master"), Ready->Master());
        Report.Json->SetNumberField(TEXT("current_member_count"), Ready->Bindings().Num());
        Report.Json->SetNumberField(TEXT("current_owner_count"), Ready->ShaderOwnerCount());
        Report.Json->SetStringField(TEXT("current_source_set_sha256"), Ready->SourceSetSha256());
        if (!TestTrue(TEXT("Exact five members, four/three owners and held source epoch"),
                Ready->Bindings().Num() == 5 && Missing.Num() == 0 &&
                    Ready->ShaderOwnerCount() == (Index == 0 ? 4 : 3) && Ready->ShaderOwners().Num() == 5 &&
                    Ready->SourceSetSha256().Len() == 64 && P4ExactNativeString(Ready->Master(), Family[0]) &&
                    P4ExactNativeString(Ready->RegistrySha256(), Reader->RegistrySha256()) &&
                    P4ExactNativeString(Ready->RegistryRowsSha256(), Reader->MaterialRowsSha256()) &&
                    P4ExactNativeString(Ready->RegistryLeaseEpoch(), Epoch) && Ready->Current(Settings, Error)))
            return false;
        auto Fact = MakeShared<FJsonObject>();
        Fact->SetStringField(TEXT("master"), Ready->Master());
        Fact->SetStringField(TEXT("source_set_sha256"), Ready->SourceSetSha256());
        Fact->SetNumberField(TEXT("shader_owner_count"), Ready->ShaderOwnerCount());
        TArray<TSharedPtr<FJsonValue>> Members;
        TSet<FString> UniqueOwners;
        for (const auto& Binding : Ready->Bindings())
        {
            const auto* Original = Bindings.FindByPredicate([&](const FP4KadanSourceBinding& Value)
                { return P4ExactNativeString(Value.Package, Binding.Package); });
            const FString* Owner = Ready->ShaderOwners().Find(Binding.Package);
            const FString* OwnerOfOwner = Owner ? Ready->ShaderOwners().Find(*Owner) : nullptr;
            if (!TestTrue(TEXT("Every exact supplied pair/hash/actual owner is retained"),
                    Original && Owner && OwnerOfOwner && P4ExactNativeString(*Owner, *OwnerOfOwner) &&
                        Family.Contains(Binding.Package) && Family.Contains(*Owner) &&
                        FPaths::IsSamePath(Original->HeaderPath, Binding.HeaderPath) &&
                        FPaths::IsSamePath(Original->BodyPath, Binding.BodyPath) &&
                        P4ExactNativeString(Original->SourceDigest, Binding.SourceDigest) &&
                        P4KadanSourceBindingCurrent(Binding, Error)))
                return false;
            UniqueOwners.Add(*Owner);
            auto Member = MakeShared<FJsonObject>();
            Member->SetStringField(TEXT("package"), Binding.Package);
            Member->SetStringField(TEXT("source_sha256"), Binding.SourceDigest);
            Member->SetStringField(TEXT("header"), Binding.HeaderPath);
            Member->SetStringField(TEXT("body"), Binding.BodyPath);
            Member->SetStringField(TEXT("shader_owner"), *Owner);
            Members.Add(MakeShared<FJsonValueObject>(Member));
        }
        if (!TestEqual(TEXT("Distinct owner-of identities equal the reported owner count"), UniqueOwners.Num(),
                Ready->ShaderOwnerCount()))
            return false;
        TArray<TSharedPtr<FP4KadanSourceAsset>> Metadata;
        Ready->AppendMetadataSources(Metadata);
        if (!TestTrue(TEXT("Only five fresh source-only descriptors, no executable additions or texture rows"),
                Metadata.Num() == 5 && UnexecutedPlan.Assets.Num() == 0 && UnexecutedPlan.Scanned.Num() == 12))
            return false;
        for (const auto& Source : Metadata)
        {
            const auto* Expected = Sources.FindByPredicate([&](const TSharedPtr<FP4KadanSourceAsset>& Value)
                { return P4ExactNativeString(Value->Package, Source->Package); });
            if (!TestTrue(TEXT("Metadata descriptors match source identity and are independently owned"),
                    Expected && Source.Get() != Expected->Get() &&
                        P4ExactNativeString(Source->Kind, (*Expected)->Kind) &&
                        FPaths::IsSamePath(Source->Header, (*Expected)->Header) &&
                        FPaths::IsSamePath(Source->Body, (*Expected)->Body) && Source->Dependencies.Num() == 0 &&
                        Source->SoftDependencies.Num() == 0))
                return false;
        }
        FP4MaterialFamilySourcesPtr Repeat;
        TArray<FString> RepeatMissing;
        if (!TestTrue(TEXT("Held reader source repeat has the identical complete binding"),
                P4ReadMaterialFamilySources(Settings, Reader, Family.Last(), Sources, nullptr, Repeat, RepeatMissing,
                    Error) == EP4MaterialFamilySourceRead::Ready &&
                    Repeat.IsValid() && P4ExactNativeString(Repeat->SourceSetSha256(), Ready->SourceSetSha256()) &&
                    Repeat->Current(Settings, Error)))
            return false;
        Fact->SetArrayField(TEXT("members"), Members);
        FamilyFacts.Add(MakeShared<FJsonValueObject>(Fact));
        ReadyFamilies.Add(Ready);
    }
    Report.Json->SetArrayField(TEXT("families"), FamilyFacts);
    const auto& Shadow = Families[0];
    const auto* Master = Sources.FindByPredicate(
        [&](const TSharedPtr<FP4KadanSourceAsset>& Value) { return P4ExactNativeString(Value->Package, Shadow[0]); });
    const auto* MasterBytes = Bindings.FindByPredicate(
        [&](const FP4KadanSourceBinding& Value) { return P4ExactNativeString(Value.Package, Shadow[0]); });
    const auto* Other = Sources.FindByPredicate([&](const TSharedPtr<FP4KadanSourceAsset>& Value)
        { return P4ExactNativeString(Value->Package, Families[1][0]); });
    if (!TestTrue(TEXT("Controlled negatives identify exact source records by package, never manifest order"),
            Master && MasterBytes && Other))
        return false;
    FControlledPair Controlled(FPaths::ProjectSavedDir() / TEXT("P4MapKit/MaterialFamilySources/controlled"));
    ON_SCOPE_EXIT
    {
        const bool Clean = Controlled.Cleanup();
        Report.Json->SetBoolField(TEXT("controlled_pair_cleanup"), Clean);
        TestTrue(TEXT("Only independent controlled pair files are removed"), Clean);
    };
    if (!TestTrue(TEXT("Create only two bounded independent tiny files"),
            Controlled.Initialize() && Controlled.Write(Controlled.Header, MasterBytes->Header) &&
                Controlled.Write(Controlled.Body, MasterBytes->Body)))
        return false;
    auto Copied = CopySource(*Master);
    Copied->Header = Controlled.Header.Path;
    Copied->Body = Controlled.Body.Path;
    auto CopiedSources = ReplaceSource(Sources, Copied);
    const bool OuterHadSource = P4KadanHasBoundSource(Shadow[0]);
    FP4KadanSourceBinding OuterBefore;
    FString OuterError;
    const bool OuterReadable = OuterHadSource && P4KadanReadBoundSource(Shadow[0], OuterBefore, OuterError);
    {
        FP4KadanImportSourceScope OuterScope(Sources);
        const auto Refuses = [&](const TArray<TSharedPtr<FP4KadanSourceAsset>>& Input, const FString& Label)
        {
            FP4MaterialFamilySourcesPtr Out = ReadyFamilies[0];
            TArray<FString> Missing{TEXT("stale-output")};
            const auto Result = P4ReadMaterialFamilySources(
                Settings, Reader, Shadow[0], Input, nullptr, Out, Missing, Error);
            Report.Json->SetStringField(TEXT("last_refusal_control"), Label);
            Report.Json->SetStringField(TEXT("last_refusal_error"), Error);
            Report.Json->SetNumberField(TEXT("last_refusal_result"), int32(Result));
            Report.Json->SetNumberField(TEXT("last_refusal_missing_count"), Missing.Num());
            Report.Json->SetBoolField(TEXT("last_refusal_has_witness"), Out.IsValid());
            FP4KadanSourceBinding Restored;
            FString ScopeError;
            return TestTrue(Label,
                Result == EP4MaterialFamilySourceRead::Refused && !Out.IsValid() && Missing.Num() == 0 &&
                    !Error.IsEmpty() && P4KadanReadBoundSource(Shadow[0], Restored, ScopeError) &&
                    P4ExactNativeString(Restored.SourceDigest, MasterBytes->SourceDigest));
        };
        Report.Json->SetStringField(TEXT("stage"), TEXT("missing-and-refusal-controls"));
        TArray<FString> ExpectedMissing{Shadow[2], Shadow[3], Shadow[4]};
        ExpectedMissing.Sort();
        TArray<TSharedPtr<FP4KadanSourceAsset>> Sparse;
        for (const auto& Source : Sources)
            if (!ExpectedMissing.Contains(Source->Package))
                Sparse.Add(Source);
        FP4MaterialFamilySourcesPtr MissingResult = ReadyFamilies[0];
        TArray<FString> Missing;
        if (!TestTrue(TEXT("Dropping three reverse members returns the complete exact request and no partial witness"),
                P4ReadMaterialFamilySources(Settings, Reader, Shadow[0], Sparse, nullptr, MissingResult, Missing,
                    Error) == EP4MaterialFamilySourceRead::MissingSources &&
                    !MissingResult.IsValid() && SameStrings(Missing, ExpectedMissing)))
            return false;
        Report.Json->SetNumberField(TEXT("complete_missing_source_count"), Missing.Num());
        auto Conflict = Sources;
        Conflict.Add(Copied);
        if (!Refuses(
                Conflict, TEXT("Conflicting explicit source paths refuse despite valid original and outer bindings")))
            return false;
        auto IdenticalDuplicate = Sources;
        IdenticalDuplicate.Add(*Master);
        FP4MaterialFamilySourcesPtr DuplicateReady;
        TArray<FString> DuplicateMissing;
        if (!TestTrue(TEXT("Identical repeated binding remains one source member, not conflicting coverage"),
                P4ReadMaterialFamilySources(Settings, Reader, Shadow[0], IdenticalDuplicate, nullptr, DuplicateReady,
                    DuplicateMissing, Error) == EP4MaterialFamilySourceRead::Ready &&
                    DuplicateReady.IsValid() && DuplicateReady->Bindings().Num() == 5 &&
                    P4ExactNativeString(DuplicateReady->SourceSetSha256(), ReadyFamilies[0]->SourceSetSha256())))
            return false;
        auto Alias = CopySource(Copied);
        Alias->Package = Alias->Package.ToLower();
        auto AliasSources = Sources;
        AliasSources.Add(Alias);
        if (!Refuses(AliasSources, TEXT("Case-conflicting explicit package binding refuses without fallback")))
            return false;
        auto Partial = CopySource(Copied);
        Partial->Body.Reset();
        if (!Refuses(ReplaceSource(Sources, Partial),
                TEXT("Incomplete supplied header/body refuses rather than falling through")))
            return false;
        auto WrongPackage = CopySource(Copied);
        WrongPackage->Header = (*Other)->Header;
        WrongPackage->Body = (*Other)->Body;
        if (!Refuses(ReplaceSource(Sources, WrongPackage),
                TEXT("Wrong actual package refuses despite valid alternative source")) ||
            !Refuses(ReplaceSource(Sparse, WrongPackage),
                TEXT("Invalid available source refuses independently of missing requests")))
            return false;
        FP4MaterialFamilySourcesPtr CopyReady;
        TArray<FString> CopyMissing;
        if (!TestTrue(TEXT("An exact independent copied pair is a valid current-read binding"),
                P4ReadMaterialFamilySources(Settings, Reader, Shadow[0], CopiedSources, nullptr, CopyReady, CopyMissing,
                    Error) == EP4MaterialFamilySourceRead::Ready &&
                    CopyReady.IsValid() && CopyReady->Current(Settings, Error) &&
                    P4ExactNativeString(CopyReady->SourceSetSha256(), ReadyFamilies[0]->SourceSetSha256())))
            return false;
        Report.Json->SetStringField(TEXT("stage"), TEXT("controlled-current-changes"));
        auto BodyChanged = MasterBytes->Body;
        BodyChanged.Last() ^= 1;
        if (!TestTrue(TEXT("Changed owned body bytes invalidate Current without affecting originals"),
                Controlled.Write(Controlled.Body, BodyChanged) && !CopyReady->Current(Settings, Error) &&
                    ReadyFamilies[0]->Current(Settings, Error)))
            return false;
        if (!TestTrue(TEXT("Restoring exact controlled bytes restores current-read identity, not a historic claim"),
                Controlled.Write(Controlled.Body, MasterBytes->Body) && CopyReady->Current(Settings, Error)))
            return false;
        auto HeaderChanged = MasterBytes->Header;
        HeaderChanged[0] ^= 1;
        if (!TestTrue(TEXT("Changed owned header invalidates Current"),
                Controlled.Write(Controlled.Header, HeaderChanged) && !CopyReady->Current(Settings, Error)) ||
            !Refuses(CopiedSources, TEXT("Corrupt supplied header never falls through to the outer valid source")))
            return false;
        if (!TestTrue(TEXT("Exact original copied header restores readiness"),
                Controlled.Write(Controlled.Header, MasterBytes->Header) && CopyReady->Current(Settings, Error)))
            return false;
        auto ChangedSettings = Settings;
        ChangedSettings.GamePaks = Settings.GamePaks / TEXT("unselected-source");
        if (!TestTrue(TEXT("Changed selected source refuses the old witness and leaves its true epoch current"),
                !CopyReady->Current(ChangedSettings, Error) && CopyReady->Current(Settings, Error)))
            return false;
    }
    FP4KadanSourceBinding OuterAfter;
    FString OuterAfterError;
    const bool OuterReadableAfter = P4KadanHasBoundSource(Shadow[0]) &&
        P4KadanReadBoundSource(Shadow[0], OuterAfter, OuterAfterError);
    if (!TestTrue(TEXT("All nested source scopes restore the caller's preceding registry"),
            P4KadanHasBoundSource(Shadow[0]) == OuterHadSource && OuterReadableAfter == OuterReadable &&
                (!OuterReadable || P4ExactNativeString(OuterAfter.SourceDigest, OuterBefore.SourceDigest))))
        return false;
    for (const auto& Ready : ReadyFamilies)
        if (!TestTrue(TEXT("Every complete original family remains current after all negative controls"),
                Ready->Current(Settings, Error)))
            return false;
    if (!TestTrue(TEXT("Independent controlled files clean without original source changes"), Controlled.Cleanup()))
        return false;
    Report.Json->SetStringField(TEXT("stage"), TEXT("complete"));
    Report.Json->SetStringField(TEXT("scope"),
        TEXT(
            "Registered-only actual source wrapper/current-file/refusal qualification. No cache branch, acquisition adapter, UObject import, shader compilation or generic material caller wiring."));
    Report.Json->SetBoolField(TEXT("passed"), !HasAnyErrors());
    return !HasAnyErrors();
}
#endif

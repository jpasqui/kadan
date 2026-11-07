#include "P4KadanBlueprintPlans.h"
#include "P4KadanImport.h"
#include "P4KadanImportSources.h"
#include "P4NativeHash.h"
#include "P4RegistryInputLease.h"
THIRD_PARTY_INCLUDES_START
#include "P4MapKitCore/package_reader.h"
THIRD_PARTY_INCLUDES_END
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace P4BlueprintOriginalSourceTest
{
FString Full(FString Path)
{
    if (Path.IsEmpty())
        return FString();
    Path = FPaths::ConvertRelativePathToFull(Path);
    FPaths::NormalizeFilename(Path);
    FPaths::CollapseRelativeDirectories(Path);
    FPaths::NormalizeDirectoryName(Path);
    return Path;
}
struct FProof
{
    FAutomationTestBase& Test;
    FString File;
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    explicit FProof(FAutomationTestBase& In)
        : Test(In), File(FPaths::ProjectSavedDir() / TEXT("P4MapKit/BlueprintOriginalSource/current-env.json"))
    {
        Json->SetBoolField(TEXT("passed"), false);
        Json->SetStringField(TEXT("stage"), TEXT("preflight"));
    }
    ~FProof()
    {
        bool Passed = false;
        Json->TryGetBoolField(TEXT("passed"), Passed);
        Json->SetBoolField(TEXT("passed"), Passed && !Test.HasAnyErrors());
        FString Text;
        if (!FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text)) ||
            !IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true) ||
            !FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            Test.AddError(TEXT("Cannot retain current original-planner proof"));
    }
};
struct FFileGuard
{
    FString File, Digest;
    int64 Bytes = -1;
    TUniquePtr<P4RegistryLease::FInput> Lease;
    bool Open(const FString& Path, bool Required)
    {
        File = Full(Path);
        Bytes = IFileManager::Get().FileSize(*File);
        if (Bytes < 0)
            return !Required && !IFileManager::Get().FileExists(*File) && !IFileManager::Get().DirectoryExists(*File);
#if PLATFORM_WINDOWS
        Lease = MakeUnique<P4RegistryLease::FInput>();
        int64 Read = 0;
        return Lease->Open(std::wstring(*File)) && Bytes <= 64 * 1024 * 1024 &&
            P4HashFile(File, 64 * 1024 * 1024, Read, Digest) && Read == Bytes && Lease->Current();
#else
        return false;
#endif
    }
    bool Current() const
    {
        if (Bytes < 0)
            return !IFileManager::Get().FileExists(*File) && !IFileManager::Get().DirectoryExists(*File);
        FString Now;
        int64 Read = 0;
        return Lease && Lease->Current() && P4HashFile(File, 64 * 1024 * 1024, Read, Now) && Read == Bytes &&
            P4ExactNativeString(Now, Digest);
    }
};
} //namespace P4BlueprintOriginalSourceTest

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ActualBlueprintOriginalSource, "P4MapKit.Kadan.ActualBlueprintOriginalSource",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ActualBlueprintOriginalSource::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_BLUEPRINT_ORIGINAL_SOURCE")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "Skipped production unresolved-original planner proof; requires P4_NATIVE_BLUEPRINT_ORIGINAL_SOURCE=1 and explicit retained Env header"));
        return true;
    }
    using namespace P4BlueprintOriginalSourceTest;
    const FString Native = Full(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"))),
                  Repo = FPaths::GetPath(FPaths::GetPath(Native));
    if (!TestTrue(TEXT("Planning-only proof restricted to existing owned native-development project"),
            !Native.IsEmpty() &&
                FPaths::IsSamePath(Full(FPaths::ProjectDir()), Repo / TEXT("private/native-development/project"))))
        return false;
    const FString Receipt = FPaths::ProjectSavedDir() / TEXT("P4MapKit/BlueprintOriginalSource/current-env.json");
    if (!TestTrue(TEXT("Preserve any previous receipt and refuse repeated original acquisition"),
            !IFileManager::Get().FileExists(*Receipt) && !IFileManager::Get().DirectoryExists(*Receipt)))
        return false;
    FProof Proof(*this);
    const FString Package = TEXT("/Game/Carrot4/Core/Graphics/System/BP_EnvController"),
                  Object = Package + TEXT(".BP_EnvController");
    const FString Header = Full(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_ZEN_ENV_HEADER")));
    if (!TestTrue(TEXT("Exact retained Env header explicitly supplied"),
            !Header.IsEmpty() && FPaths::GetExtension(Header) == TEXT("uasset")))
        return false;
    const FString Body = FPaths::ChangeExtension(Header, TEXT("uexp"));
    FString Error;
    TArray<TUniquePtr<FFileGuard>> Guards;
    const auto Hold = [&](const FString& File, bool Required)
    {
        auto Guard = MakeUnique<FFileGuard>();
        if (!Guard->Open(File, Required))
            return false;
        Guards.Add(MoveTemp(Guard));
        return true;
    };
    if (!TestTrue(TEXT("Retained source pair held read-only with original identities/bytes"),
            Hold(Header, true) && Hold(Body, true)))
        return false;
    const FString NativeAsset = Full(
        FPaths::ProjectContentDir() / TEXT("Carrot4/Core/Graphics/System/BP_EnvController.uasset"));
    for (const TCHAR* Extension : {TEXT("uasset"), TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl"), TEXT("m.ubulk")})
        if (!TestTrue(TEXT("Existing or absent Env output preserved: ") + FString(Extension),
                Hold(FPaths::ChangeExtension(NativeAsset, Extension), false)))
            return false;
    UObject* BeforeObject = FindObject<UObject>(nullptr, *Object);
    UPackage* BeforePackage = FindPackage(nullptr, *Package);
    const bool BeforeDirty = BeforePackage && BeforePackage->IsDirty();
    const FString BeforeRecord = BeforeObject
        ? BeforeObject->GetOutermost()->GetMetaData()->GetValue(BeforeObject, TEXT("P4OriginalRecord"))
        : FString();
    const FString BeforeMembers = BeforeObject
        ? BeforeObject->GetOutermost()->GetMetaData()->GetValue(BeforeObject, TEXT("P4BlueprintSourceMembers"))
        : FString();
    ON_SCOPE_EXIT
    {
        bool Same = true;
        for (const auto& Guard : Guards)
            Same = Guard->Current() && Same;
        const bool ObjectSame = FindObject<UObject>(nullptr, *Object) == BeforeObject &&
            FindPackage(nullptr, *Package) == BeforePackage &&
            (!BeforePackage || BeforePackage->IsDirty() == BeforeDirty) &&
            (!BeforeObject ||
                (P4ExactNativeString(BeforeRecord,
                     BeforeObject->GetOutermost()->GetMetaData()->GetValue(BeforeObject, TEXT("P4OriginalRecord"))) &&
                    P4ExactNativeString(BeforeMembers,
                        BeforeObject->GetOutermost()->GetMetaData()->GetValue(
                            BeforeObject, TEXT("P4BlueprintSourceMembers")))));
        Proof.Json->SetBoolField(TEXT("source_and_native_output_files_unchanged"), Same);
        Proof.Json->SetBoolField(TEXT("env_loaded_identity_record_dirty_unchanged"), ObjectSame);
        TestTrue(TEXT("Source/output identities, flags, times, bytes and Env loaded state/record are unchanged"),
            Same && ObjectSame);
    };
    TArray<TSharedPtr<FP4KadanSourceAsset>> Sources;
    TArray<FString> Problems;
    const FString FixtureGame = Native / TEXT("host/Saved/P4MapKit/FullNativeCave000/game");
    if (!TestTrue(TEXT("Reuse the existing bounded native-plan source registry"),
            P4KadanScanGame(FixtureGame, Sources, Problems) && Problems.Num() == 0))
        return false;
    auto Env = MakeShared<FP4KadanSourceAsset>();
    Env->Package = Package;
    Env->Kind = TEXT("Blueprint");
    Env->Header = Header;
    Env->Body = Body;
    Sources.RemoveAll([&](const TSharedPtr<FP4KadanSourceAsset>& Row)
        { return Row.IsValid() && P4ExactNativeString(Row->Package, Package); });
    Sources.Add(Env);
    FP4KadanImportSourceScope Scope(Sources);
    FP4KadanSourceBinding Bound;
    if (!TestTrue(TEXT("Actual Env canonical source H+B validated and hashed"),
            P4KadanReadBoundSource(Package, Bound, Error)))
    {
        AddError(Error);
        return false;
    }
    if (!TestTrue(TEXT("Exact known actual138 Env source retained, without source replacement"),
            Bound.Header.Num() == 13027 && Bound.Body.Num() == 7229 &&
                P4ExactNativeString(
                    Bound.SourceDigest, TEXT("cd58498d7dc774e0ea6018ff445ab28014929713a3ab59f0e2ad664606816152")) &&
                P4ExactNativeString(
                    Guards[0]->Digest, TEXT("6091929d4af92b8d4f2da7e296fadf2528883ae7b98f9fe8153087b95cef50e1")) &&
                P4ExactNativeString(
                    Guards[1]->Digest, TEXT("22b42fa8fc1fc1ef6cc9f0b53331254b871fa415c56a0f226caffcaffac56678"))))
        return false;
    const auto Inventory = P4MapKit::ReadPackage(
        std::vector<uint8>(Bound.Header.GetData(), Bound.Header.GetData() + Bound.Header.Num()),
        std::vector<uint8>(Bound.Body.GetData(), Bound.Body.GetData() + Bound.Body.Num()));
    if (!TestTrue(TEXT("Actual Env export/import inventory is valid"), Inventory.Error.Code.empty()))
        return false;
    TArray<int32> Unknown;
    for (size_t I = 0; I < Inventory.Imports.size(); ++I)
    {
        std::string Path;
        if (Inventory.Imports[I].Name == "UnknownExport" && Inventory.Reference(-std::int32_t(I) - 1, Path) &&
            Path == "/Engine/UnknownPackage.UnknownExport")
            Unknown.Add(-int32(I) - 1);
    }
    const TArray<int32> Expected{-23, -24, -25, -26, -27, -53};
    if (!TestTrue(
            TEXT("Genuine Env has all six exact unresolved source rows before production call"), Unknown == Expected))
        return false;
    TArray<TSharedPtr<FJsonValue>> Indices;
    for (int32 Index : Unknown)
        Indices.Add(MakeShared<FJsonValueNumber>(Index));
    Proof.Json->SetArrayField(TEXT("unresolved_import_indices"), Indices);
    Proof.Json->SetStringField(TEXT("package"), Package);
    Proof.Json->SetStringField(TEXT("source_sha256"), Bound.SourceDigest);
    Proof.Json->SetNumberField(TEXT("header_bytes"), Bound.Header.Num());
    Proof.Json->SetNumberField(TEXT("body_bytes"), Bound.Body.Num());
    FP4KadanImportSettings Settings;
    Settings.GamePaks = Full(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS")));
    Settings.Retoc = Full(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC")));
    Settings.Repak = Full(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK")));
    if (Settings.Retoc.IsEmpty())
        Settings.Retoc = Repo / TEXT("private/retoc/retoc.exe");
    if (Settings.Repak.IsEmpty())
        Settings.Repak = Repo / TEXT("private/repak/repak.exe");
    if (!TestFalse(TEXT("Selected current container directory explicit as in actual138"), Settings.GamePaks.IsEmpty()))
        return false;
    FP4CurrentGameRegistryPtr Reader;
    FP4KadanBlueprintPlans Plans;
    const FString PlanFolder = FPaths::ProjectSavedDir() / TEXT("Kadan/BlueprintOriginalSourcePlans");
    if (!TestTrue(TEXT("Dedicated one-shot plan output does not replace another existing folder"),
            !IFileManager::Get().DirectoryExists(*PlanFolder) && !IFileManager::Get().FileExists(*PlanFolder)))
        return false;
    FP4ExportContainerTools Tools;
    Tools.Retoc = Settings.Retoc;
    Tools.Repak = Settings.Repak;
    Proof.Json->SetStringField(TEXT("stage"), TEXT("acquire-shared-current-reader"));
    if (!TestTrue(TEXT("Acquire one genuinely current reader to reuse as the ordinary shared run context"),
            P4AcquireCurrentGameRegistry(Tools, Settings.GamePaks,
                FPaths::ProjectSavedDir() / TEXT("Kadan/BlueprintOriginalSourceRegistry"), Reader, Error)))
    {
        AddError(Error);
        return false;
    }
    const auto RetainedReader = Reader;
    const FString Epoch = Reader->LeaseEpoch(), Registry = Reader->RegistrySha256();
    Proof.Json->SetStringField(TEXT("lease_epoch"), Epoch);
    Proof.Json->SetStringField(TEXT("registry_sha256"), Registry);
    Proof.Json->SetStringField(TEXT("retoc_sha256"), Reader->RetocSha256());
    Proof.Json->SetStringField(TEXT("repak_sha256"), Reader->RepakSha256());
    Proof.Json->SetStringField(TEXT("stage"), TEXT("production-plan"));
    const bool Planned = P4KadanPlanBlueprints(Sources, {Package}, PlanFolder, Plans, Error, Settings, Reader);
    Proof.Json->SetBoolField(TEXT("planner_completed"), Planned);
    Proof.Json->SetNumberField(TEXT("original_identity_packages"), Plans.OriginalIdentityPackages);
    Proof.Json->SetBoolField(TEXT("env_source_problem"), Plans.SourceProblems.Contains(Package));
    Proof.Json->SetStringField(TEXT("planner_error"), Error);
    if (Reader)
    {
        Proof.Json->SetStringField(TEXT("lease_epoch"), Reader->LeaseEpoch());
        Proof.Json->SetStringField(TEXT("registry_sha256"), Reader->RegistrySha256());
        Proof.Json->SetStringField(TEXT("retoc_sha256"), Reader->RetocSha256());
        Proof.Json->SetStringField(TEXT("repak_sha256"), Reader->RepakSha256());
    }
    if (!TestTrue(
            TEXT("Actual production unresolved path completes with exactly one verified original identity package"),
            Planned && Plans.OriginalIdentityPackages == 1 && !Plans.SourceProblems.Contains(Package) &&
                Plans.SourceProblems.Num() == 0 && Reader.IsValid()))
    {
        AddError(Error);
        return false;
    }
    const bool Admitted = Plans.Planned.Contains(Package);
    const bool Unsupported = Plans.Refused.Contains(Package) && Plans.ReferenceOnlyReasons.Contains(Package) &&
        !Plans.ReferenceOnlyReasons[Package].IsEmpty();
    if (!TestTrue(
            TEXT(
                "Env is either an admitted actual plan or an explicit semantic-only refusal after source identity proof"),
            Admitted != Unsupported))
        return false;
    Proof.Json->SetStringField(TEXT("plan_status"), Admitted ? TEXT("admitted") : TEXT("semantic-unsupported"));
    if (Unsupported)
        Proof.Json->SetStringField(TEXT("semantic_reason"), Plans.ReferenceOnlyReasons[Package]);
    if (!TestTrue(TEXT("Production reader remains same/current and source bytes exact after plan publication"),
            Reader.Get() == RetainedReader.Get() && Reader->IsCurrent(Tools, Settings.GamePaks, Error) &&
                P4ExactNativeString(Reader->LeaseEpoch(), Epoch) &&
                P4ExactNativeString(Reader->RegistrySha256(), Registry) && P4KadanSourceBindingCurrent(Bound, Error)))
        return false;
    Proof.Json->SetBoolField(TEXT("reader_retained_current"), true);
    Proof.Json->SetStringField(TEXT("scope"),
        TEXT(
            "One genuine current Env unresolved-original production planner call only; no Blueprint construction/import/save/cook, map, SDK or optional-reference admission."));
    Proof.Json->SetStringField(TEXT("stage"), TEXT("complete"));
    Proof.Json->SetBoolField(TEXT("passed"), !HasAnyErrors());
    return !HasAnyErrors();
}

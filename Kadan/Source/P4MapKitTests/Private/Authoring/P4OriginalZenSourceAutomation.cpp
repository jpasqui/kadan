#include "P4OriginalZenSource.h"
#include "P4KadanTestSeams.h"
#include "P4ExportProcess.h"
#include "P4KadanAcquisitionVeto.h"
#include "P4KadanImport.h"
#include "P4KadanImportSources.h"
#include "P4NativeHash.h"
#include "P4RegistryInputLease.h"
THIRD_PARTY_INCLUDES_START
#include "P4MapKitCore/blueprint_reference_roles.h"
THIRD_PARTY_INCLUDES_END
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Hash/CityHash.h"
#include "Misc/AutomationTest.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace P4OriginalZenTest
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
    FString Path;
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    FProof(FAutomationTestBase& In, const TCHAR* Name)
        : Test(In),
          Path(FPaths::ProjectSavedDir() / TEXT("P4MapKit/OriginalZenSource") / (FString(Name) + TEXT(".json")))
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
            !IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true) ||
            !FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            Test.AddError(TEXT("Cannot retain original Zen source proof"));
    }
};
bool Write(const FString& File, const FString& Text)
{
    return FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
bool WorkEmpty(const FString& Work)
{
    TArray<FString> Found;
    IFileManager::Get().FindFiles(Found, *(Work / TEXT("*")), true, true);
    return !Found.Num();
}
int32 Calls(const FString& File, const FString& Command)
{
    FString Text;
    if (IFileManager::Get().FileSize(*File) > 16384 || !FFileHelper::LoadFileToString(Text, *File))
        return -1;
    TArray<FString> Lines;
    Text.ParseIntoArrayLines(Lines);
    int32 N = 0;
    for (const FString& Line : Lines)
        if (P4ExactNativeString(Line, Command))
            ++N;
    return N;
}
#if PLATFORM_WINDOWS
bool DeniesWrites(const FString& File)
{
    HANDLE H = CreateFileW(*File, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (H == INVALID_HANDLE_VALUE)
        return true;
    CloseHandle(H);
    return false;
}
#endif
} //namespace P4OriginalZenTest

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalZenControlled, "P4MapKit.Kadan.OriginalZenSourceControls",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalZenControlled::RunTest(const FString&)
{
    P4_REQUIRE_KADAN_TEST_SEAMS();
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_ZEN_CONTROLS")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "Skipped original Zen controls; requires P4_NATIVE_ORIGINAL_ZEN_CONTROLS=1 and checked process fixture"));
        return true;
    }
    using namespace P4OriginalZenTest;
    FProof Proof(*this, TEXT("controlled"));
#if !PLATFORM_WINDOWS
    AddError(TEXT("Original Zen immutable custody controls require Windows"));
    return false;
#else
    const FString Native = Full(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"))),
                  Repo = FPaths::GetPath(FPaths::GetPath(Native));
    if (!TestTrue(TEXT("Controls run in the owned native fixture"),
            !Native.IsEmpty() &&
                FPaths::IsSamePath(Full(FPaths::ProjectDir()), Repo / TEXT("private/native-development/project"))))
        return false;
    FString Toy = Full(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REGISTRY_FIXTURE_TOOL")));
    if (Toy.IsEmpty())
        Toy = Repo / TEXT("private/particle-proof/current-registry-fixture-tool.exe");
    FString ToyHash, BuildText, Expected;
    int64 Size = 0;
    TSharedPtr<FJsonObject> Build;
    bool Passed = false;
    if (!TestTrue(TEXT("Controlled tool bound to an explicit successful build receipt"),
            P4HashFile(Toy, 2 * 1024 * 1024, Size, ToyHash) && Size > 0 &&
                IFileManager::Get().FileSize(*(FPaths::GetPath(Toy) / TEXT("current-registry-tool-build.json"))) <
                    8192 &&
                FFileHelper::LoadFileToString(
                    BuildText, *(FPaths::GetPath(Toy) / TEXT("current-registry-tool-build.json"))) &&
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(BuildText), Build) && Build.IsValid() &&
                Build->TryGetBoolField(TEXT("passed"), Passed) && Passed &&
                Build->TryGetStringField(TEXT("binary_sha256"), Expected) && P4ExactNativeString(Expected, ToyHash)))
        return false;
    const FString Root = Full(FPaths::ProjectSavedDir() / TEXT("P4MapKit/OriginalZenSource/controlled-inputs"));
    if (!TestTrue(TEXT("Controlled input root initially absent"),
            !IFileManager::Get().DirectoryExists(*Root) && !IFileManager::Get().FileExists(*Root)))
        return false;
    const FString Paks = Root / TEXT("Paks"), Bin = Root / TEXT("Bin"), RegistryWork = Paks / TEXT("work"),
                  Work = Root / TEXT("dumps");
    const FString Owner = TEXT("p4-current-registry-controlled-tool/1\n"), Package = TEXT("/Game/M/Master"),
                  Mode = Paks / TEXT("mode.txt");
    const FString Registry = TEXT(
        "  Registry Version: AddedDependencyFlags\n  Assets: 1\nAssetData {\n object_path: \"/Game/M/Master.Master\",\n asset_class: \"Material\",\n package_name: \"/Game/M/Master\",\n legacy_tags: [],\n}\n");
    TArray<FString> Files{Paks / TEXT("controlled-owner.txt"), Paks / TEXT("Carrot4-Switch.pak"),
        Paks / TEXT("Carrot4-Switch.utoc"), Paks / TEXT("Carrot4-Switch.ucas"), Mode, Paks / TEXT("invocations.log"),
        Bin / TEXT("retoc.exe"), Bin / TEXT("repak.exe")};
    FP4CurrentGameRegistryPtr Reader;
    FP4OriginalZenSourcePtr Source;
    ON_SCOPE_EXIT
    {
        Source.Reset();
        Reader.Reset();
        // Never recursively remove an unexpected failed-tool tree.
        bool Clean = WorkEmpty(Work);
        if (Clean)
        {
            for (const FString& File : Files)
                if (IFileManager::Get().FileExists(*File))
                    Clean = IFileManager::Get().Delete(*File, false, false) && Clean;
            for (const FString& Dir : {RegistryWork, Work, Paks, Bin, Root})
                if (IFileManager::Get().DirectoryExists(*Dir))
                    Clean = IFileManager::Get().DeleteDirectory(*Dir, false, false) && Clean;
        }
        TestTrue(TEXT("Controlled fixture cleanup removes only known owned members"),
            Clean && !IFileManager::Get().DirectoryExists(*Root));
        FString After;
        int64 N = 0;
        TestTrue(TEXT("External prepared tool unchanged"),
            P4HashFile(Toy, 2 * 1024 * 1024, N, After) && P4ExactNativeString(After, ToyHash));
    };
    if (!IFileManager::Get().MakeDirectory(*Paks, true) || !IFileManager::Get().MakeDirectory(*Bin, true) ||
        !IFileManager::Get().MakeDirectory(*Work, true) || !Write(Files[0], Owner) ||
        !Write(Files[1], Owner + Registry) || !Write(Files[2], Owner) || !Write(Files[3], Owner) ||
        !Write(Mode, TEXT("")) || IFileManager::Get().Copy(*Files[6], *Toy) != COPY_OK ||
        IFileManager::Get().Copy(*Files[7], *Toy) != COPY_OK)
        return false;
    FP4ExportContainerTools Tools;
    Tools.Retoc = Files[6];
    Tools.Repak = Files[7];
    Tools.TimeoutSeconds = 5;
    FString Error;
    if (!TestTrue(TEXT("One controlled current reader acquired through actual process path"),
            P4AcquireCurrentGameRegistry(Tools, Paks, RegistryWork, Reader, Error)))
    {
        AddError(Error);
        return false;
    }
    const FString Epoch = Reader->LeaseEpoch();
    if (!TestTrue(TEXT("Fresh mounted source acquired"),
            P4AcquireOriginalZenSource(Reader, Tools, Paks, Package, Work, Source, Error)))
    {
        AddError(Error);
        return false;
    }
    const FString First = Source->CustodyDirectory();
    if (!TestTrue(TEXT("Exact computed package ID and held epoch"),
            Source->PackageId() == P4ExportPackageId(Package) && P4ExactNativeString(Source->Package(), Package) &&
                P4ExactNativeString(Source->LeaseEpoch(), Epoch) && Source->Current(Error) &&
                Source->RawSha256().Len() == 64 && Source->MetadataSha256().Len() == 64 &&
                DeniesWrites(First / TEXT("Master.uasset")) && DeniesWrites(First / TEXT("Master.metadata.json")) &&
                DeniesWrites(Tools.Retoc)))
        return false;
    if (!TestTrue(TEXT("Consumer gets full controlled bytes, not a semantic authority"),
            Source->Read([&](const TArray<uint8>& Raw, const TArray<uint8>& Meta, FString&)
                { return Raw.Num() == 84 && Meta.Num() > 0 && Raw[63] == 'Z' && Raw[64] == 'c'; }, Error)))
        return false;
    const auto* FirstOwner = Source.Get();
    if (!TestTrue(TEXT("Explicit current output owner reuses one dump without a hidden cache"),
            P4AcquireOriginalZenSource(Reader, Tools, Paks, Package, Work, Source, Error) &&
                Source.Get() == FirstOwner && Calls(Files[5], TEXT("dump-test")) == 1))
        return false;
    if (!TestFalse(TEXT("Consumer refusal is preserved"),
            Source->Read(
                [](const TArray<uint8>&, const TArray<uint8>&, FString& Why)
                {
                    Why = TEXT("controlled-semantic-refusal");
                    return false;
                },
                Error)) ||
        !TestTrue(TEXT("Refused interpretation leaves immutable source valid"),
            Error == TEXT("controlled-semantic-refusal") && Source->Current(Error)))
        return false;
    const FString Foreign = First / TEXT("unexpected.txt");
    if (!Write(Foreign, TEXT("owned negative control")) ||
        !TestFalse(TEXT("Extra member refuses consumption"), Source->Current(Error)) ||
        !IFileManager::Get().Delete(*Foreign, false, false) ||
        !TestTrue(TEXT("Removing only control member restores exact held census"), Source->Current(Error)))
        return false;
    Source.Reset();
    if (!TestTrue(TEXT("Successful custody removes only exact outputs and own empty directory"), WorkEmpty(Work)))
        return false;
    const int32 Before = Calls(Files[5], TEXT("dump-test"));
    if (!TestFalse(TEXT("Early cancellation refuses without staging/tool"),
            P4AcquireOriginalZenSource(Reader, Tools, Paks, Package, Work, Source, Error, [] { return true; })) ||
        !WorkEmpty(Work))
        return false;
    {
        FP4KadanScopedAcquisitionVeto Veto(FPaths::ProjectDir());
        if (!TestTrue(TEXT("Veto active"), Veto.IsActive()) ||
            !TestFalse(TEXT("Acquisition veto precedes stage/tool"),
                P4AcquireOriginalZenSource(Reader, Tools, Paks, Package, Work, Source, Error)) ||
            Veto.DeniedBatches() != 1 || !WorkEmpty(Work))
            return false;
    }
    if (!TestTrue(TEXT("Invalid/unmounted/case-changed packages do not launch"),
            !P4AcquireOriginalZenSource(Reader, Tools, Paks, TEXT("/Game/M/Missing"), Work, Source, Error) &&
                !P4AcquireOriginalZenSource(Reader, Tools, Paks, TEXT("/Game/M/master"), Work, Source, Error) &&
                Calls(Files[5], TEXT("dump-test")) == Before))
        return false;
    for (const FString& Failure : {FString(TEXT("fail-dump")), FString(TEXT("missing-raw")),
             FString(TEXT("missing-metadata")), FString(TEXT("sleep-dump"))})
    {
        if (!Write(Mode, Failure))
            return false;
        Tools.TimeoutSeconds = Failure == TEXT("sleep-dump") ? 0.25 : 5;
        if (!TestFalse(TEXT("Failed/short/timeout output cannot publish: ") + Failure,
                P4AcquireOriginalZenSource(Reader, Tools, Paks, Package, Work, Source, Error)) ||
            !TestTrue(TEXT("Known failed outputs cleaned exactly"), !Source && WorkEmpty(Work)))
            return false;
    }
    if (!Write(Mode, TEXT("")))
        return false;
    Tools.TimeoutSeconds = 5;
    int32 CancelChecks = 0;
    if (!TestFalse(TEXT("Cancellation after tool completion refuses without publication"),
            P4AcquireOriginalZenSource(
                Reader, Tools, Paks, Package, Work, Source, Error, [&] { return ++CancelChecks == 2; })) ||
        !TestTrue(
            TEXT("Late cancellation captured/cleaned actual outputs"), CancelChecks == 2 && !Source && WorkEmpty(Work)))
        return false;
    if (!Write(Mode, TEXT("unexpected-bulk")))
        return false;
    if (!TestFalse(TEXT("Unqualified bulk refuses and retains custody"),
            P4AcquireOriginalZenSource(Reader, Tools, Paks, Package, Work, Source, Error)) ||
        Source)
        return false;
    TArray<FString> Retained;
    IFileManager::Get().FindFiles(Retained, *(Work / TEXT("*")), false, true);
    if (!TestTrue(TEXT("Exactly one owned refused-output directory retained"),
            Retained.Num() == 1 && Error.Contains(TEXT("unexpected-output-retained"))))
        return false;
    const FString Refused = Work / Retained[0];
    TArray<FString> Members;
    IFileManager::Get().FindFiles(Members, *(Refused / TEXT("*")), true, true);
    const TSet<FString> ExpectedMembers{TEXT("Master.uasset"), TEXT("Master.metadata.json"), TEXT("Master.trace.json"),
        TEXT("Master.header.txt"), TEXT("Master.ubulk")};
    if (!TestEqual(TEXT("All controlled refused members retained"), Members.Num(), ExpectedMembers.Num()))
        return false;
    for (const FString& Member : Members)
        if (!TestTrue(TEXT("Only explicitly known controlled negative output"), ExpectedMembers.Contains(Member)))
            return false;
    for (const FString& Member : Members)
        if (!IFileManager::Get().Delete(*(Refused / Member), false, false))
            return false;
    if (!IFileManager::Get().DeleteDirectory(*Refused, false, false) || !WorkEmpty(Work))
        return false;
    if (!TestTrue(TEXT("All package dumps share one unchanged registry/tool decode"),
            Calls(Files[5], TEXT("get-pak")) == 1 && Calls(Files[5], TEXT("asset-registry")) == 1 &&
                Reader->IsCurrent(Tools, Paks, Error) && P4ExactNativeString(Reader->LeaseEpoch(), Epoch)))
        return false;
    Proof.Json->SetStringField(TEXT("lease_epoch"), Epoch);
    Proof.Json->SetNumberField(TEXT("dump_tool_calls"), Calls(Files[5], TEXT("dump-test")));
    Proof.Json->SetStringField(TEXT("scope"),
        TEXT(
            "Controlled source custody/process/refusal only; toy bytes do not qualify converted headers or reference roles."));
    Proof.Json->SetBoolField(TEXT("passed"), !HasAnyErrors());
    Proof.Json->SetStringField(TEXT("stage"), TEXT("complete"));
    return !HasAnyErrors();
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalZenBatchControlled, "P4MapKit.Kadan.OriginalZenBatchReuse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalZenBatchControlled::RunTest(const FString&)
{
    P4_REQUIRE_KADAN_TEST_SEAMS();
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_ZEN_CONTROLS")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "Skipped original Zen batch controls; requires P4_NATIVE_ORIGINAL_ZEN_CONTROLS=1 and checked process fixture"));
        return true;
    }
    using namespace P4OriginalZenTest;
    FProof Proof(*this, TEXT("batch-reuse"));
#if !PLATFORM_WINDOWS
    AddError(TEXT("Original Zen batch custody requires Windows"));
    return false;
#else
    const FString Native = Full(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"))),
                  Repo = FPaths::GetPath(FPaths::GetPath(Native));
    if (!TestTrue(TEXT("Owned native fixture only"),
            !Native.IsEmpty() &&
                FPaths::IsSamePath(Full(FPaths::ProjectDir()), Repo / TEXT("private/native-development/project"))))
        return false;
    FString Toy = Full(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REGISTRY_FIXTURE_TOOL")));
    if (Toy.IsEmpty())
        Toy = Repo / TEXT("private/particle-proof/current-registry-fixture-tool.exe");
    FString ToyHash, BuildText, Expected;
    int64 Size = 0;
    TSharedPtr<FJsonObject> Build;
    bool Passed = false;
    if (!TestTrue(TEXT("Prepared controlled executable has checked build provenance"),
            P4HashFile(Toy, 2 * 1024 * 1024, Size, ToyHash) && Size > 0 &&
                IFileManager::Get().FileSize(*(FPaths::GetPath(Toy) / TEXT("current-registry-tool-build.json"))) <
                    8192 &&
                FFileHelper::LoadFileToString(
                    BuildText, *(FPaths::GetPath(Toy) / TEXT("current-registry-tool-build.json"))) &&
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(BuildText), Build) && Build.IsValid() &&
                Build->TryGetBoolField(TEXT("passed"), Passed) && Passed &&
                Build->TryGetStringField(TEXT("binary_sha256"), Expected) && P4ExactNativeString(Expected, ToyHash)))
        return false;
    const FString Root = Full(FPaths::ProjectSavedDir() / TEXT("P4MapKit/OriginalZenSource/batch-inputs"));
    if (!TestTrue(TEXT("Fresh owned control directory"),
            !IFileManager::Get().DirectoryExists(*Root) && !IFileManager::Get().FileExists(*Root)))
        return false;
    const FString Paks = Root / TEXT("Paks"), Bin = Root / TEXT("Bin"), RegistryWork = Paks / TEXT("work"),
                  Work = Root / TEXT("dumps");
    const FString Owner = TEXT("p4-current-registry-controlled-tool/1\n"), Package = TEXT("/Game/M/Master");
    const FString Registry = TEXT(
        "  Registry Version: AddedDependencyFlags\n  Assets: 1\nAssetData {\n object_path: \"/Game/M/Master.Master\",\n asset_class: \"Material\",\n package_name: \"/Game/M/Master\",\n legacy_tags: [],\n}\n");
    const TArray<FString> Files{Paks / TEXT("controlled-owner.txt"), Paks / TEXT("Carrot4-Switch.pak"),
        Paks / TEXT("Carrot4-Switch.utoc"), Paks / TEXT("Carrot4-Switch.ucas"), Paks / TEXT("mode.txt"),
        Paks / TEXT("invocations.log"), Bin / TEXT("retoc.exe"), Bin / TEXT("repak.exe")};
    FP4CurrentGameRegistryPtr Reader;
    FP4OriginalZenSourcePtr Source;
    ON_SCOPE_EXIT
    {
        Source.Reset();
        Reader.Reset();
        bool Clean = WorkEmpty(Work);
        if (Clean)
        {
            for (const FString& File : Files)
                if (IFileManager::Get().FileExists(*File))
                    Clean = IFileManager::Get().Delete(*File, false, false) && Clean;
            for (const FString& Dir : {RegistryWork, Work, Paks, Bin, Root})
                if (IFileManager::Get().DirectoryExists(*Dir))
                    Clean = IFileManager::Get().DeleteDirectory(*Dir, false, false) && Clean;
        }
        TestTrue(
            TEXT("Only exact owned control outputs removed"), Clean && !IFileManager::Get().DirectoryExists(*Root));
        FString After;
        int64 N = 0;
        TestTrue(TEXT("Prepared executable unchanged"),
            P4HashFile(Toy, 2 * 1024 * 1024, N, After) && P4ExactNativeString(After, ToyHash));
    };
    if (!IFileManager::Get().MakeDirectory(*Paks, true) || !IFileManager::Get().MakeDirectory(*Bin, true) ||
        !IFileManager::Get().MakeDirectory(*Work, true) || !Write(Files[0], Owner) ||
        !Write(Files[1], Owner + Registry) || !Write(Files[2], Owner) || !Write(Files[3], Owner) ||
        !Write(Files[4], TEXT("")) || IFileManager::Get().Copy(*Files[6], *Toy) != COPY_OK ||
        IFileManager::Get().Copy(*Files[7], *Toy) != COPY_OK)
        return false;
    FP4ExportContainerTools Tools;
    Tools.Retoc = Files[6];
    Tools.Repak = Files[7];
    Tools.TimeoutSeconds = 5;
    FString Error;
    if (!TestTrue(TEXT("One actual controlled reader acquisition"),
            P4AcquireCurrentGameRegistry(Tools, Paks, RegistryWork, Reader, Error)))
    {
        AddError(Error);
        return false;
    }
    uint64 AllOutputBytes = 0;
    {
        FP4OriginalZenSourceBatch Batch;
        if (!TestTrue(TEXT("First batch request acquires one source"),
                Batch.Acquire(Reader, Tools, Paks, Package, Work, Source, Error) &&
                    Calls(Files[5], TEXT("dump-test")) == 1))
            return false;
        const auto* Identity = Source.Get();
        TArray<FString> Names;
        IFileManager::Get().FindFiles(Names, *(Source->CustodyDirectory() / TEXT("*")), true, false);
        for (const FString& Name : Names)
        {
            const int64 N = IFileManager::Get().FileSize(*(Source->CustodyDirectory() / Name));
            if (N < 0)
                return false;
            AllOutputBytes += uint64(N);
        }
        if (!TestTrue(TEXT("Budget counts raw, metadata and both auxiliary outputs exactly"),
                Names.Num() == 4 && AllOutputBytes == Source->HeldOutputBytes() &&
                    AllOutputBytes == Batch.HeldOutputBytes() && Batch.Entries() == 1))
            return false;
        Source.Reset();
        {
            FP4KadanScopedAcquisitionVeto Veto(FPaths::ProjectDir());
            if (!TestTrue(TEXT("Second request passes prior owner through checked helper reuse without acquisition"),
                    Veto.IsActive() && Batch.Acquire(Reader, Tools, Paks, Package, Work, Source, Error) &&
                        Source.Get() == Identity && Source->Current(Error) && Veto.DeniedBatches() == 0 &&
                        Calls(Files[5], TEXT("dump-test")) == 1))
                return false;
        }
        auto Changed = Tools;
        Changed.Repak = Tools.Retoc;
        FP4OriginalZenSourcePtr Rejected;
        if (!TestFalse(TEXT("Changed selected tools cannot reuse or acquire"),
                Batch.Acquire(Reader, Changed, Paks, Package, Work, Rejected, Error)) ||
            Rejected || Calls(Files[5], TEXT("dump-test")) != 1)
            return false;
        const FString Foreign = Source->CustodyDirectory() / TEXT("unexpected.txt");
        if (!Write(Foreign, TEXT("owned currentness control")) ||
            !TestFalse(TEXT("Changed custody refuses without fresh dump"),
                Batch.Acquire(Reader, Tools, Paks, Package, Work, Rejected, Error)) ||
            Calls(Files[5], TEXT("dump-test")) != 1 || !IFileManager::Get().Delete(*Foreign, false, false))
            return false;
        Source.Reset();
    }
    if (!TestTrue(TEXT("Batch destruction releases exact owned outputs"), WorkEmpty(Work)))
        return false;
    for (int32 BudgetCase = 0; BudgetCase < 2; ++BudgetCase)
    {
        FP4OriginalZenSourceBatch Overflow(BudgetCase ? 1 : 0, BudgetCase ? AllOutputBytes - 1 : AllOutputBytes);
        if (!TestTrue(TEXT("Entry or byte budget overflow remains successful uncached acquisition"),
                Overflow.Acquire(Reader, Tools, Paks, Package, Work, Source, Error) && Overflow.Entries() == 0 &&
                    Overflow.HeldOutputBytes() == 0))
            return false;
        Source.Reset();
        const int32 Before = Calls(Files[5], TEXT("dump-test"));
        {
            FP4KadanScopedAcquisitionVeto Veto(FPaths::ProjectDir());
            if (!TestFalse(TEXT("Overflow did not retain an owner or bypass a real miss veto"),
                    Overflow.Acquire(Reader, Tools, Paks, Package, Work, Source, Error)) ||
                Source || Veto.DeniedBatches() != 1 || Calls(Files[5], TEXT("dump-test")) != Before || !WorkEmpty(Work))
                return false;
        }
    }
    if (!TestTrue(TEXT("Three controlled dumps, one current registry decode"),
            Calls(Files[5], TEXT("dump-test")) == 3 && Calls(Files[5], TEXT("get-pak")) == 1 &&
                Calls(Files[5], TEXT("asset-registry")) == 1 && Reader->IsCurrent(Tools, Paks, Error)))
        return false;
    Proof.Json->SetNumberField(TEXT("retained_output_bytes"), double(AllOutputBytes));
    Proof.Json->SetNumberField(TEXT("dump_tool_calls"), 3);
    Proof.Json->SetBoolField(TEXT("repeat_request_no_acquisition"), true);
    Proof.Json->SetBoolField(TEXT("entry_and_byte_overflow_uncached"), true);
    Proof.Json->SetStringField(TEXT("scope"),
        TEXT(
            "Controlled raw-owner reuse/bounds only; actual unresolved production source planning is independently qualified by 140."));
    Proof.Json->SetBoolField(TEXT("passed"), !HasAnyErrors());
    Proof.Json->SetStringField(TEXT("stage"), TEXT("complete"));
    return !HasAnyErrors();
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ActualOriginalZen, "P4MapKit.Kadan.ActualOriginalZenSource",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ActualOriginalZen::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_ZEN_GAME")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "Skipped actual original Zen source; requires P4_NATIVE_ORIGINAL_ZEN_GAME=1 and three explicit retained source headers"));
        return true;
    }
    using namespace P4OriginalZenTest;
    FProof Proof(*this, TEXT("actual-three-packages"));
    const FString Native = Full(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"))),
                  Repo = FPaths::GetPath(FPaths::GetPath(Native));
    if (!TestTrue(TEXT("Actual source-only gate uses owned native-development project"),
            !Native.IsEmpty() &&
                FPaths::IsSamePath(Full(FPaths::ProjectDir()), Repo / TEXT("private/native-development/project"))))
        return false;
    const TArray<FString> Packages{TEXT("/Game/Carrot4/Core/Graphics/System/BP_EnvController"),
        TEXT("/Game/Carrot4/Placeables/Teki/GBaby"), TEXT("/Game/Carrot4/Placeables/Teki/Base/GTekiBase")};
    const TArray<FString> HeaderKeys{TEXT("P4_NATIVE_ORIGINAL_ZEN_ENV_HEADER"),
        TEXT("P4_NATIVE_ORIGINAL_ZEN_BABY_HEADER"), TEXT("P4_NATIVE_ORIGINAL_ZEN_TEKI_HEADER")};
    const TArray<uint64> ExpectedIds{14126029230781431346ull, 14846148918453390439ull, 8308542404112000232ull};
    const TArray<int32> ExpectedReservationBytes{108, 193, 132};
    TArray<TSharedPtr<FP4KadanSourceAsset>> Rows;
    TArray<TUniquePtr<P4RegistryLease::FInput>> SourceLeases;
    for (int32 I = 0; I < Packages.Num(); ++I)
    {
        auto Row = MakeShared<FP4KadanSourceAsset>();
        Row->Package = Packages[I];
        Row->Header = Full(FPlatformMisc::GetEnvironmentVariable(*HeaderKeys[I]));
        Row->Body = FPaths::ChangeExtension(Row->Header, TEXT("uexp"));
        Row->Kind = TEXT("Blueprint");
        if (!TestTrue(TEXT("Explicit retained header supplied: ") + HeaderKeys[I],
                !Row->Header.IsEmpty() && FPaths::GetExtension(Row->Header) == TEXT("uasset")))
            return false;
#if PLATFORM_WINDOWS
        for (const FString& File : {Row->Header, Row->Body})
        {
            auto Lease = MakeUnique<P4RegistryLease::FInput>();
            if (!TestTrue(TEXT("Hold retained source input unchanged: ") + File, Lease->Open(std::wstring(*File))))
                return false;
            SourceLeases.Add(MoveTemp(Lease));
        }
#else
        AddError(TEXT("Actual original source custody unavailable on this platform"));
        return false;
#endif
        Rows.Add(Row);
    }
    FP4KadanImportSourceScope SourceScope(Rows);
    TArray<FP4KadanSourceBinding> Bindings;
    FString Error;
    for (const auto& Row : Rows)
    {
        FP4KadanSourceBinding Binding;
        if (!TestTrue(TEXT("Canonical current converted source: ") + Row->Package,
                P4KadanReadBoundSource(Row->Package, Binding, Error)))
        {
            AddError(Error);
            return false;
        }
        Bindings.Add(MoveTemp(Binding));
    }
    ON_SCOPE_EXIT
    {
        for (const auto& Binding : Bindings)
            TestTrue(TEXT("Retained converted source still exact"), P4KadanSourceBindingCurrent(Binding, Error));
        for (const auto& Lease : SourceLeases)
            TestTrue(TEXT("Retained source file identity/flags/time/size unchanged"), Lease->Current());
    };
    FP4ExportContainerTools Tools;
    Tools.Retoc = Full(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC")));
    Tools.Repak = Full(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK")));
    if (Tools.Retoc.IsEmpty())
        Tools.Retoc = Repo / TEXT("private/retoc/retoc.exe");
    if (Tools.Repak.IsEmpty())
        Tools.Repak = Repo / TEXT("private/repak/repak.exe");
    const FString Paks = Full(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS")));
    FP4CurrentGameRegistryPtr Reader;
    if (!TestTrue(TEXT("One genuinely current selected reader for all three dumps"),
            !Paks.IsEmpty() &&
                P4AcquireCurrentGameRegistry(Tools, Paks,
                    FPaths::ProjectSavedDir() / TEXT("P4MapKit/OriginalZenSource/registry-work"), Reader, Error)))
    {
        AddError(Error);
        return false;
    }
    const FString Epoch = Reader->LeaseEpoch(), Registry = Reader->RegistrySha256(), Retoc = Reader->RetocSha256();
    Proof.Json->SetStringField(TEXT("lease_epoch"), Epoch);
    Proof.Json->SetStringField(TEXT("registry_sha256"), Registry);
    Proof.Json->SetStringField(TEXT("retoc_sha256"), Retoc);
    TArray<TSharedPtr<FJsonValue>> Results;
    for (int32 I = 0; I < Bindings.Num(); ++I)
    {
        const auto& Binding = Bindings[I];
        Proof.Json->SetStringField(TEXT("stage"), TEXT("dump-") + Binding.Package);
        FP4OriginalZenSourcePtr Raw;
        if (!TestTrue(TEXT("Genuine mounted original package: ") + Binding.Package,
                P4AcquireOriginalZenSource(Reader, Tools, Paks, Binding.Package,
                    FPaths::ProjectSavedDir() / TEXT("Kadan/OriginalZenSources"), Raw, Error)))
        {
            AddError(Error);
            return false;
        }
        const FString Custody = Raw->CustodyDirectory();
        if (!TestTrue(TEXT("Actual computed original package ID and reader reused"),
                Raw->PackageId() == ExpectedIds[I] && P4ExactNativeString(Raw->LeaseEpoch(), Epoch) &&
                    P4ExactNativeString(Raw->RetocSha256(), Retoc)))
            return false;
        const std::vector<uint8> Header(Binding.Header.GetData(), Binding.Header.GetData() + Binding.Header.Num()),
            Body(Binding.Body.GetData(), Binding.Body.GetData() + Binding.Body.Num());
        auto Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("package"), Binding.Package);
        Item->SetStringField(TEXT("package_id"), FString::Printf(TEXT("%llu"), (unsigned long long)Raw->PackageId()));
        Item->SetStringField(TEXT("converted_source_sha256"), Binding.SourceDigest);
        Item->SetStringField(TEXT("original_zen_sha256"), Raw->RawSha256());
        Item->SetStringField(TEXT("original_metadata_sha256"), Raw->MetadataSha256());
        Item->SetBoolField(TEXT("original_input_captured"), false);
        Item->SetBoolField(TEXT("reservation_verified"), false);
        Item->SetBoolField(TEXT("without_witness_checked"), false);
        // Publish the private partial row before verification, so a later schema
        // refusal cannot discard the one genuinely current mounted acquisition.
        Results.Add(MakeShared<FJsonValueObject>(Item));
        Proof.Json->SetArrayField(TEXT("packages"), Results);
        if (!TestTrue(TEXT("Portable verifier consumes genuine current original and converted source"),
                Raw->Read(
                    [&](const TArray<uint8>& Zen, const TArray<uint8>& Metadata, FString& Why)
                    {
                        Item->SetNumberField(TEXT("original_zen_bytes"), Zen.Num());
                        Item->SetNumberField(TEXT("original_metadata_bytes"), Metadata.Num());
                        if (Zen.Num() > 128 * 1024 || Metadata.Num() > 128 * 1024)
                        {
                            Why = TEXT(
                                "Actual private original-input capture exceeds its explicit 128 KiB per-buffer bound; no truncation");
                            return false;
                        }
                        Item->SetStringField(TEXT("original_zen_base64"), FBase64::Encode(Zen));
                        Item->SetStringField(TEXT("original_metadata_base64"), FBase64::Encode(Metadata));
                        Item->SetBoolField(TEXT("original_input_captured"), true);
                        try
                        {
                            using namespace P4MapKit::Blueprint;
                            const FImportIdentityBytes Original(Zen.GetData(), Zen.GetData() + Zen.Num()),
                                MetadataBytes(Metadata.GetData(), Metadata.GetData() + Metadata.Num());
                            const auto HashName = [](const std::string& Name)
                            {
                                const FString NativeName(UTF8_TO_TCHAR(Name.c_str()));
                                return std::uint64_t(CityHash64(
                                    reinterpret_cast<const char*>(*NativeName), NativeName.Len() * sizeof(TCHAR)));
                            };
                            const auto ClassSource = [&]() -> FBlueprintClassSource
                            {
                                const auto Inventory = P4MapKit::ReadPackage(Header, Body);
                                if (!Inventory.Error.Code.empty())
                                    throw P4MapKit::Blueprint::Error(Inventory.Error.Code);
                                bool NeedsIdentities = false;
                                for (size_t ImportIndex = 0; ImportIndex < Inventory.Imports.size(); ++ImportIndex)
                                {
                                    std::string ImportPath;
                                    if (Inventory.Imports[ImportIndex].Name == "UnknownExport" &&
                                        Inventory.Reference(-std::int32_t(ImportIndex) - 1, ImportPath) &&
                                        ImportPath == "/Engine/UnknownPackage.UnknownExport")
                                        NeedsIdentities = true;
                                }
                                if (NeedsIdentities)
                                {
                                    const auto Identities = RecoverImportIdentities(Header, Body, Original,
                                        MetadataBytes, std::string(TCHAR_TO_UTF8(*Binding.Package)), HashName);
                                    return FBlueprintClassSource::ReadWithIdentities(
                                        Header, Body, std::string(TCHAR_TO_UTF8(*Binding.Package)), Identities);
                                }
                                return FBlueprintClassSource::Read(Header, Body);
                            }();
                            const auto Without = ReadBlueprintReferenceRoleFacts(ClassSource, Header, Body);
                            Item->SetBoolField(TEXT("without_witness_checked"), true);
                            Item->SetBoolField(TEXT("without_witness_complete"), Without.Complete());
                            if (!TestFalse(TEXT("Without original reservation witness coverage remains incomplete"),
                                    Without.Complete()))
                                return false;
                            const auto Witness = VerifyConvertedHeaderReservation(
                                ClassSource, Header, Body, Original, MetadataBytes);
                            if (!TestTrue(TEXT("Exact expected actual reservation/all-four source hashes"),
                                    Witness.End() - Witness.Begin() == size_t(ExpectedReservationBytes[I]) &&
                                        Witness.MatchesSource(Header, Body, Original, MetadataBytes) &&
                                        P4ExactNativeString(
                                            FString(UTF8_TO_TCHAR(Witness.Package().c_str())), Binding.Package) &&
                                        P4ExactNativeString(FString(UTF8_TO_TCHAR(Witness.OriginalZenSha256().c_str())),
                                            Raw->RawSha256()) &&
                                        P4ExactNativeString(
                                            FString(UTF8_TO_TCHAR(Witness.OriginalMetadataSha256().c_str())),
                                            Raw->MetadataSha256())))
                                return false;
                            const auto Facts = ReadBlueprintReferenceRoleFacts(
                                ClassSource, Header, Body, Witness, Original, MetadataBytes);
                            if (Facts.Complete() &&
                                !TestTrue(TEXT("Complete role facts require all four current source buffers"),
                                    Facts.RequiresOriginalSource() && !Facts.MatchesSource(Header, Body) &&
                                        Facts.MatchesSource(Header, Body, Original, MetadataBytes)))
                                return false;
                            auto ChangedOriginal = Original;
                            ChangedOriginal.back() ^= 1;
                            auto ChangedMetadata = MetadataBytes;
                            ChangedMetadata.push_back(' ');
                            if (!TestTrue(TEXT("Wrong current original/metadata cannot consume this witness"),
                                    !Witness.MatchesSource(Header, Body, ChangedOriginal, MetadataBytes) &&
                                        !Witness.MatchesSource(Header, Body, Original, ChangedMetadata) &&
                                        !ReadBlueprintReferenceRoleFacts(
                                            ClassSource, Header, Body, Witness, ChangedOriginal, MetadataBytes)
                                            .Complete() &&
                                        !ReadBlueprintReferenceRoleFacts(
                                            ClassSource, Header, Body, Witness, Original, ChangedMetadata)
                                            .Complete()))
                                return false;
                            Item->SetNumberField(TEXT("reservation_begin"), double(Witness.Begin()));
                            Item->SetNumberField(TEXT("reservation_end"), double(Witness.End()));
                            Item->SetBoolField(TEXT("reservation_verified"), true);
                            Item->SetBoolField(TEXT("reference_roles_complete"), Facts.Complete());
                            TArray<TSharedPtr<FJsonValue>> Problems;
                            for (const auto& Problem : Facts.Problems())
                                Problems.Add(MakeShared<FJsonValueString>(UTF8_TO_TCHAR(Problem.c_str())));
                            Item->SetArrayField(TEXT("reference_role_problems"), Problems);
                            if (!Facts.Complete())
                                AddInfo(
                                    TEXT(
                                        "Original/header correspondence verified; independent reference-role gap remains for ") +
                                    Binding.Package);
                            return P4KadanSourceBindingCurrent(Binding, Why);
                        }
                        catch (const std::exception& E)
                        {
                            Why = TEXT("Actual original/header proof refused: ") + FString(UTF8_TO_TCHAR(E.what()));
                            return false;
                        }
                    },
                    Error)))
        {
            AddError(Error);
            return false;
        }
        if (!TestTrue(TEXT("Original/source exact at consumption completion"),
                Raw->Current(Error) && P4KadanSourceBindingCurrent(Binding, Error)))
            return false;
        Raw.Reset();
        if (!TestFalse(TEXT("Own raw custody directory cleaned without touching parent/other runs"),
                IFileManager::Get().DirectoryExists(*Custody)))
            return false;
    }
    if (!TestTrue(TEXT("Same selected epoch/tool/raw-registry after all three reads"),
            Reader->IsCurrent(Tools, Paks, Error) && P4ExactNativeString(Reader->LeaseEpoch(), Epoch) &&
                P4ExactNativeString(Reader->RegistrySha256(), Registry) &&
                P4ExactNativeString(Reader->RetocSha256(), Retoc)))
        return false;
    Proof.Json->SetArrayField(TEXT("packages"), Results);
    Proof.Json->SetStringField(TEXT("lease_epoch"), Epoch);
    Proof.Json->SetStringField(TEXT("registry_sha256"), Registry);
    Proof.Json->SetStringField(TEXT("retoc_sha256"), Retoc);
    Proof.Json->SetStringField(TEXT("scope"),
        TEXT(
            "Three genuinely mounted immutable originals, converted-source correspondence and precise header reservations. Independent reference-role gaps reported; no Blueprint import/cook or optional dependency admission."));
    Proof.Json->SetBoolField(TEXT("passed"), !HasAnyErrors());
    Proof.Json->SetStringField(TEXT("stage"), TEXT("complete"));
    return !HasAnyErrors();
}

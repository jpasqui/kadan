#include "P4CurrentGameRegistry.h"
#include "P4NativeHash.h"
#include "P4MapKitCore/material_family_registry.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
const FString RegistryFixtureOwner = TEXT("p4-current-registry-controlled-tool/1\n");
FString RegistryTestFull(FString Path)
{
    if (Path.IsEmpty())
        return FString();
    Path = FPaths::ConvertRelativePathToFull(Path);
    FPaths::NormalizeFilename(Path);
    FPaths::NormalizeDirectoryName(Path);
    return Path;
}
struct FRegistryTestProof
{
    FString File;
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    FAutomationTestBase& Test;
    FRegistryTestProof(FAutomationTestBase& In, const FString& Name)
        : File(FPaths::ProjectSavedDir() / TEXT("P4MapKit/CurrentRegistry") / (Name + TEXT(".json"))), Test(In)
    {
        Json->SetBoolField(TEXT("passed"), false);
        Json->SetStringField(TEXT("stage"), TEXT("preflight"));
    }
    ~FRegistryTestProof()
    {
        bool Passed = false;
        Json->TryGetBoolField(TEXT("passed"), Passed);
        Json->SetBoolField(TEXT("passed"), Passed && !Test.HasAnyErrors());
        FString Text;
        auto Writer = TJsonWriterFactory<>::Create(&Text);
        if (!FJsonSerializer::Serialize(Json, Writer) ||
            !IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true) ||
            !FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            Test.AddError(TEXT("Cannot retain current-registry proof"));
    }
};
struct FRegistryInputStamp
{
    uint32 Volume = 0, High = 0, Low = 0, Attributes = 0;
    uint64 Created = 0, Written = 0, Size = 0;
    bool operator==(const FRegistryInputStamp& B) const
    {
        return Volume == B.Volume && High == B.High && Low == B.Low && Attributes == B.Attributes &&
            Created == B.Created && Written == B.Written && Size == B.Size;
    }
};
bool RegistryStamp(const FString& File, FRegistryInputStamp& Out)
{
#if PLATFORM_WINDOWS
    const HANDLE Handle = CreateFileW(*File, FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (Handle == INVALID_HANDLE_VALUE)
        return false;
    BY_HANDLE_FILE_INFORMATION I{};
    const bool Okay = GetFileInformationByHandle(Handle, &I) != 0;
    CloseHandle(Handle);
    if (!Okay)
        return false;
    const auto Time = [](FILETIME T) { return (uint64(T.dwHighDateTime) << 32) | T.dwLowDateTime; };
    Out.Volume = I.dwVolumeSerialNumber;
    Out.High = I.nFileIndexHigh;
    Out.Low = I.nFileIndexLow;
    Out.Attributes = I.dwFileAttributes;
    Out.Created = Time(I.ftCreationTime);
    Out.Written = Time(I.ftLastWriteTime);
    Out.Size = (uint64(I.nFileSizeHigh) << 32) | I.nFileSizeLow;
    return true;
#else
    (void)File;
    (void)Out;
    return false;
#endif
}
bool RegistrySameInputs(const TMap<FString, FRegistryInputStamp>& Before)
{
    for (const auto& Pair : Before)
    {
        FRegistryInputStamp Now;
        if (!RegistryStamp(Pair.Key, Now) || !(Now == Pair.Value))
            return false;
    }
    return true;
}
bool RegistryDigest(const FString& Value)
{
    if (Value.Len() != 64)
        return false;
    for (TCHAR C : Value)
        if (!((C >= TEXT('a') && C <= TEXT('f')) || (C >= TEXT('0') && C <= TEXT('9'))))
            return false;
    return true;
}
struct FRegistryControlledFiles
{
    FString Root;
    TArray<FString> Files, Directories;
    explicit FRegistryControlledFiles(FString In) : Root(MoveTemp(In)) {}
    bool Directory(const FString& File)
    {
        if (!FPaths::IsUnderDirectory(File, Root))
            return false;
        Directories.AddUnique(File);
        return IFileManager::Get().MakeDirectory(*File, true);
    }
    bool Write(const FString& File, const FString& Text)
    {
        if (!FPaths::IsUnderDirectory(File, Root))
            return false;
        Files.AddUnique(File);
        return FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }
    bool Copy(const FString& File, const FString& Source)
    {
        if (!FPaths::IsUnderDirectory(File, Root) || FPaths::IsSamePath(File, Source))
            return false;
        Files.AddUnique(File);
        return IFileManager::Get().Copy(*File, *Source) == COPY_OK;
    }
    bool Cleanup()
    {
        bool Okay = true;
        for (int32 I = Files.Num() - 1; I >= 0; --I)
            if (IFileManager::Get().FileExists(*Files[I]))
                Okay = IFileManager::Get().Delete(*Files[I], false, true) && Okay;
        for (int32 I = Directories.Num() - 1; I >= 0; --I)
            if (IFileManager::Get().DirectoryExists(*Directories[I]))
                Okay = IFileManager::Get().DeleteDirectory(*Directories[I], false, false) && Okay;
        Files.Reset();
        Directories.Reset();
        return Okay;
    }
    ~FRegistryControlledFiles() { Cleanup(); }
};
FString RegistryControlledText(bool InvalidParent = false)
{
    const FString Master = TEXT(
        "AssetData {\n    object_path: \"/Game/M/Master.Master\",\n    asset_class: \"Material\",\n    package_name: \"/Game/M/Master\",\n    legacy_tags: [],\n}\n");
    const FString Instance =
        TEXT(
            "AssetData {\n    object_path: \"/Game/M/I.I\",\n    asset_class: \"MaterialInstanceConstant\",\n    package_name: \"/Game/M/I\",\n    legacy_tags: [\n        (\n            \"Parent\",\n            \"") +
        FString(InvalidParent ? TEXT("None") : TEXT("Material'/Game/M/Master.Master'")) +
        TEXT("\",\n        ),\n    ],\n}\n");
    return FString(TEXT("  Registry Version: AddedDependencyFlags\n  Assets: 2\n")) +
        (InvalidParent ? Instance + Master : Master + Instance);
}
int32 RegistryInvocations(const FString& Log, const FString& Phase)
{
    if (IFileManager::Get().FileSize(*Log) < 0 || IFileManager::Get().FileSize(*Log) > 16384)
        return -1;
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *Log))
        return -1;
    TArray<FString> Lines;
    Text.ParseIntoArrayLines(Lines);
    int32 Count = 0;
    for (const FString& Line : Lines)
        if (P4ExactNativeString(Line, Phase))
            ++Count;
    return Count;
}
bool RegistryWorkEmpty(const FString& Work)
{
    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *(Work / TEXT("*.bin")), true, false);
    return Files.Num() == 0;
}
} //namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CurrentRegistryControlled, "P4MapKit.Kadan.CurrentRegistryControlledTools",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CurrentRegistryControlled::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REGISTRY_CONTROLS")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. ") TEXT(
            "Skipped controlled registry gate; requires P4_NATIVE_REGISTRY_CONTROLS=1 and its checked process fixture"));
        return true;
    }
#if !PLATFORM_WINDOWS
    AddError(TEXT("Controlled native registry process gates require immutable Windows leases"));
    return false;
#else
    FRegistryTestProof Proof(*this, TEXT("controlled-tools"));
    const FString Native = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    if (!TestFalse(TEXT("Native root is explicit"), Native.IsEmpty()))
        return false;
    const FString Repo = FPaths::GetPath(FPaths::GetPath(RegistryTestFull(Native)));
    FString SourceTool = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REGISTRY_FIXTURE_TOOL"));
    if (SourceTool.IsEmpty())
        SourceTool = Repo / TEXT("private/particle-proof/current-registry-fixture-tool.exe");
    SourceTool = RegistryTestFull(SourceTool);
    FString ToolHash;
    int64 ToolSize = 0;
    FRegistryInputStamp ToolBefore;
    if (!TestTrue(TEXT("Bounded first-party process fixture exists"),
            P4HashFile(SourceTool, 2 * 1024 * 1024, ToolSize, ToolHash) && ToolSize > 0 &&
                RegistryStamp(SourceTool, ToolBefore)))
        return false;
    FString BuildText;
    TSharedPtr<FJsonObject> Build;
    const FString BuildFile = FPaths::GetPath(SourceTool) / TEXT("current-registry-tool-build.json");
    FString ExpectedTool;
    bool BuildPassed = false;
    if (!TestTrue(TEXT("Fixture executable is bound to its host build receipt"),
            IFileManager::Get().FileSize(*BuildFile) > 0 && IFileManager::Get().FileSize(*BuildFile) < 8192 &&
                FFileHelper::LoadFileToString(BuildText, *BuildFile) &&
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(BuildText), Build) && Build.IsValid() &&
                Build->TryGetBoolField(TEXT("passed"), BuildPassed) && BuildPassed &&
                Build->TryGetStringField(TEXT("binary_sha256"), ExpectedTool) &&
                P4ExactNativeString(ExpectedTool, ToolHash)))
        return false;
    ON_SCOPE_EXIT
    {
        FRegistryInputStamp After;
        FString Hash;
        int64 Size = 0;
        const bool Same = RegistryStamp(SourceTool, After) && After == ToolBefore &&
            P4HashFile(SourceTool, 2 * 1024 * 1024, Size, Hash) && P4ExactNativeString(Hash, ToolHash);
        Proof.Json->SetBoolField(TEXT("original_fixture_tool_unchanged"), Same);
        TestTrue(TEXT("External fixture tool remains unchanged"), Same);
    };
    const FString Root = RegistryTestFull(FPaths::ProjectSavedDir() / TEXT("P4MapKit/CurrentRegistry/controlled"));
    if (!TestTrue(TEXT("Controlled fixture is inside Saved"),
            FPaths::IsUnderDirectory(Root, RegistryTestFull(FPaths::ProjectSavedDir()))))
        return false;
    const DWORD RootAttributes = GetFileAttributesW(*Root);
    if (!TestTrue(TEXT("Controlled root is not a reparse alias"),
            RootAttributes == INVALID_FILE_ATTRIBUTES || !(RootAttributes & FILE_ATTRIBUTE_REPARSE_POINT)))
        return false;
    TArray<FString> Existing;
    IFileManager::Get().FindFiles(Existing, *(Root / TEXT("*")), true, true);
    if (!TestEqual(TEXT("Refuse nonempty/stale controlled fixture"), Existing.Num(), 0))
        return false;
    FRegistryControlledFiles Own(Root);
    const FString Paks = Root / TEXT("Paks"), Bin = Root / TEXT("Bin"), Work = Paks / TEXT("work"),
                  Log = Paks / TEXT("invocations.log");
    if (!Own.Directory(Paks) || !Own.Directory(Bin))
        return false;
    Own.Files.Add(Log);
    FP4ExportContainerTools Tools;
    Tools.Retoc = Bin / TEXT("retoc.exe");
    Tools.Repak = Bin / TEXT("repak.exe");
    Tools.TimeoutSeconds = 5;
    if (!Own.Copy(Tools.Retoc, SourceTool) || !Own.Copy(Tools.Repak, SourceTool) ||
        !Own.Write(Paks / TEXT("controlled-owner.txt"), RegistryFixtureOwner) ||
        !Own.Write(Paks / TEXT("Carrot4-Switch.pak"), RegistryFixtureOwner + RegistryControlledText()) ||
        !Own.Write(Paks / TEXT("Carrot4-Switch.utoc"), RegistryFixtureOwner) ||
        !Own.Write(Paks / TEXT("Carrot4-Switch.ucas"), RegistryFixtureOwner))
        return false;
    Own.Directories.Add(Work);
    FString Error;
    FP4CurrentGameRegistryPtr Reader, Again;
    Proof.Json->SetStringField(TEXT("stage"), TEXT("acquire-and-reuse"));
    if (!TestTrue(TEXT("Actual repak/retoc process path acquires controlled current registry"),
            P4AcquireCurrentGameRegistry(Tools, Paks, Work, Reader, Error)))
    {
        AddError(Error);
        return false;
    }
    P4MapKit::Material::FMaterialRegistryFamily Family;
    if (!TestTrue(TEXT("Controlled family is complete"),
            Reader->Family(TEXT("/Game/M/Master"), Family, Error) && Family.Members.size() == 2))
        return false;
    if (!TestTrue(TEXT("Family-only reader performs exactly one get/decode and no container-header work"),
            RegistryInvocations(Log, TEXT("get-pak")) == 1 && RegistryInvocations(Log, TEXT("asset-registry")) == 1 &&
                RegistryInvocations(Log, TEXT("list-header")) == 0 &&
                RegistryInvocations(Log, TEXT("get-header")) == 0))
        return false;
    const FString Epoch = Reader->LeaseEpoch(), Raw = Reader->RegistrySha256(), Rows = Reader->MaterialRowsSha256();
    int64 RawSize = 0;
    FString ExpectedRaw;
    if (!TestTrue(TEXT("Reader hashes exactly the raw bytes emitted by the controlled process"),
            P4HashFile(Paks / TEXT("Carrot4-Switch.pak"), 2 * 1024 * 1024, RawSize, ExpectedRaw) && RawSize > 0 &&
                P4ExactNativeString(Raw, ExpectedRaw)))
        return false;
    if (!TestTrue(TEXT("Raw/tool/row/schema identities are present"),
            RegistryDigest(Raw) && RegistryDigest(Rows) && P4ExactNativeString(Reader->RetocSha256(), ToolHash) &&
                P4ExactNativeString(Reader->RepakSha256(), ToolHash) && !Epoch.IsEmpty() &&
                FString(Reader->MaterialSchema()).Contains(TEXT("/1"))))
        return false;
    Proof.Json->SetStringField(TEXT("initial_raw_sha256"), Raw);
    Proof.Json->SetStringField(TEXT("initial_material_rows_sha256"), Rows);
    Proof.Json->SetStringField(TEXT("initial_tool_sha256"), ToolHash);
    Proof.Json->SetStringField(TEXT("initial_lease_epoch"), Epoch);
    for (const FString& File : {Tools.Retoc, Tools.Repak, Paks / TEXT("Carrot4-Switch.pak"),
             Paks / TEXT("Carrot4-Switch.utoc"), Paks / TEXT("Carrot4-Switch.ucas")})
    {
        const HANDLE Writer = CreateFileW(*File, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        const bool Denied = Writer == INVALID_HANDLE_VALUE;
        if (!Denied)
            CloseHandle(Writer);
        if (!TestTrue(TEXT("Actual held reader denies writes to every input: ") + File, Denied))
            return false;
    }
    if (!TestTrue(TEXT("Live reader reused without another acquisition"),
            P4AcquireCurrentGameRegistry(Tools, Paks, Work, Again, Error) && Again.Get() == Reader.Get()))
        return false;
    if (!TestTrue(TEXT("Valid in-place reader ownership also reuses the held object"),
            P4AcquireCurrentGameRegistry(Tools, Paks, Work, Reader, Error) && Again.Get() == Reader.Get()))
        return false;
    FP4ExportGameInventory Inventory;
    if (!TestTrue(TEXT("Unchanged public export inventory reuses same raw census"),
            P4ReadExportGameInventory(Tools, Paks, Work, Inventory, Error) && Inventory.IsValid() &&
                Inventory.Contains(FName(TEXT("/Game/M/I"))) && P4ExactNativeString(Inventory.RegistrySha256(), Raw)))
    {
        AddError(Error);
        return false;
    }
    if (!TestTrue(TEXT("One raw acquisition/decode, separate real header path only for export inventory"),
            RegistryInvocations(Log, TEXT("get-pak")) == 1 && RegistryInvocations(Log, TEXT("asset-registry")) == 1 &&
                RegistryInvocations(Log, TEXT("list-header")) == 1 &&
                RegistryInvocations(Log, TEXT("get-header")) == 1 && Reader->IsCurrent(Tools, Paks, Error) &&
                P4ExactNativeString(Reader->LeaseEpoch(), Epoch) &&
                P4ExactNativeString(Reader->MaterialRowsSha256(), Rows) && RegistryWorkEmpty(Work)))
        return false;
    Again.Reset();
    Proof.Json->SetStringField(TEXT("stage"), TEXT("epoch-and-release"));
    if (!Own.Write(Paks / TEXT("new_s1.ucas"), TEXT("partition")) ||
        !TestFalse(TEXT("Added partition refuses old family consumption"),
            Reader->Family(TEXT("/Game/M/Master"), Family, Error)) ||
        !TestTrue(TEXT("Failed epoch exposes no family"), Family.Members.empty()))
        return false;
    TWeakPtr<const FP4CurrentGameRegistry, ESPMode::ThreadSafe> Old = Reader;
    if (!TestTrue(TEXT("Invalid sole Prior releases before fresh acquisition"),
            P4AcquireCurrentGameRegistry(Tools, Paks, Work, Reader, Error) && !Old.IsValid() &&
                !P4ExactNativeString(Reader->LeaseEpoch(), Epoch) &&
                P4ExactNativeString(Reader->RegistrySha256(), Raw)))
        return false;
    Reader.Reset();
    Proof.Json->SetStringField(TEXT("stage"), TEXT("material-only-refusal"));
    if (!Own.Write(Paks / TEXT("Carrot4-Switch.pak"), RegistryFixtureOwner + RegistryControlledText(true)) ||
        !Own.Write(Paks / TEXT("mode.txt"), TEXT("split-material")) ||
        !P4AcquireCurrentGameRegistry(Tools, Paks, Work, Reader, Error))
        return false;
    TArray<FString> Names;
    if (!TestTrue(TEXT("Exact Parent refusal preserves later name-only export rows"),
            P4ExactNativeString(Reader->MaterialError(), TEXT("material-registry-parent-invalid")) &&
                Reader->MaterialRowsSha256().IsEmpty() && Reader->PackageNames(Names, Error) && Names.Num() == 2 &&
                !Reader->Family(TEXT("/Game/M/Master"), Family, Error) && Family.Members.empty() &&
                P4ReadExportGameInventory(Tools, Paks, Work, Inventory, Error) &&
                Inventory.Contains(FName(TEXT("/Game/M/Master"))) &&
                P4ExactNativeString(Inventory.RegistrySha256(), Reader->RegistrySha256())))
        return false;
    const FString OldTool = Reader->RetocSha256();
    Reader.Reset();
    if (!Own.Write(Paks / TEXT("Carrot4-Switch.pak"), RegistryFixtureOwner + RegistryControlledText()) ||
        !Own.Write(Paks / TEXT("mode.txt"), TEXT("")))
        return false;
    TArray<uint8> Binary;
    if (!FFileHelper::LoadFileToArray(Binary, *Tools.Retoc))
        return false;
    Binary.Add(0);
    if (!FFileHelper::SaveArrayToFile(Binary, *Tools.Retoc) ||
        !P4AcquireCurrentGameRegistry(Tools, Paks, Work, Reader, Error) ||
        !TestTrue(TEXT("Changed tool requires fresh tool SHA"), !P4ExactNativeString(Reader->RetocSha256(), OldTool)))
        return false;
    Reader.Reset();
    Proof.Json->SetStringField(TEXT("stage"), TEXT("tool-failures"));
    for (const FString& Mode : {FString(TEXT("fail-repak")), FString(TEXT("fail-retoc")), FString(TEXT("sleep-retoc"))})
    {
        if (!Own.Write(Paks / TEXT("mode.txt"), Mode))
            return false;
        Tools.TimeoutSeconds = Mode == TEXT("sleep-retoc") ? 0.25 : 5;
        if (!TestFalse(TEXT("Controlled tool failure/timeout refuses publication: ") + Mode,
                P4AcquireCurrentGameRegistry(Tools, Paks, Work, Reader, Error)) ||
            !TestTrue(
                TEXT("Failure releases reader and own raw scratch"), !Reader.IsValid() && RegistryWorkEmpty(Work)))
            return false;
        const FString Expected = Mode == TEXT("fail-repak") ? TEXT("exit code 23")
            : Mode == TEXT("fail-retoc")                    ? TEXT("exit code 24")
                                                            : TEXT("timed out");
        if (!TestTrue(TEXT("Exact controlled failure reason is surfaced"), Error.Contains(Expected)))
        {
            AddError(Error);
            return false;
        }
    }
    if (!TestTrue(TEXT("Every controlled file closes and removes only owned names"), Own.Cleanup()))
        return false;
    Proof.Json->SetBoolField(TEXT("passed"), !HasAnyErrors());
    Proof.Json->SetStringField(TEXT("stage"), TEXT("complete"));
    Proof.Json->SetStringField(TEXT("scope"),
        TEXT("Controlled tools only; actual public process/acquisition/census path, not genuine game metadata."));
    return !HasAnyErrors();
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ActualCurrentRegistry, "P4MapKit.Kadan.ActualCurrentGameRegistry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ActualCurrentRegistry::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_CURRENT_REGISTRY_GAME")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. ")
                TEXT("Skipped actual registry gate; requires P4_NATIVE_CURRENT_REGISTRY_GAME=1"));
        return true;
    }
    const FString Native = RegistryTestFull(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT")));
    const FString Repo = FPaths::GetPath(FPaths::GetPath(Native));
    if (!TestTrue(TEXT("Actual registry proof is restricted to owned native fixture"),
            !Native.IsEmpty() &&
                FPaths::IsSamePath(
                    RegistryTestFull(FPaths::ProjectDir()), Repo / TEXT("private/native-development/project"))))
        return false;
    FRegistryTestProof Proof(*this, TEXT("actual-selected-game"));
    FP4ExportContainerTools Tools;
    Tools.TimeoutSeconds = 120;
    Tools.Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    Tools.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    if (Tools.Retoc.IsEmpty())
        Tools.Retoc = Repo / TEXT("private/retoc/retoc.exe");
    if (Tools.Repak.IsEmpty())
        Tools.Repak = Repo / TEXT("private/repak/repak.exe");
    const FString Paks = RegistryTestFull(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS")));
    TArray<FString> Inputs{Tools.Retoc, Tools.Repak};
    for (const TCHAR* Pattern : {TEXT("*.pak"), TEXT("*.utoc"), TEXT("*.ucas")})
    {
        TArray<FString> Found;
        IFileManager::Get().FindFiles(Found, *(Paks / Pattern), true, false);
        for (const FString& Name : Found)
            Inputs.Add(Paks / Name);
    }
    if (!TestTrue(TEXT("Selected actual container set is explicit and bounded"),
            !Paks.IsEmpty() && Inputs.Num() > 4 && Inputs.Num() <= 258))
        return false;
    TMap<FString, FRegistryInputStamp> Before;
    for (const FString& File : Inputs)
    {
        FRegistryInputStamp State;
        if (!TestTrue(TEXT("Snapshot original selected input ") + File, RegistryStamp(File, State)))
            return false;
        Before.Add(File, State);
    }
    ON_SCOPE_EXIT
    {
        const bool Same = RegistrySameInputs(Before);
        Proof.Json->SetBoolField(TEXT("source_file_identity_flags_time_size_unchanged"), Same);
        TestTrue(TEXT("All actual selected inputs remain unchanged"), Same);
    };
    FString Error;
    FP4CurrentGameRegistryPtr Reader, Again;
    const FString Work = FPaths::ProjectSavedDir() / TEXT("P4MapKit/CurrentRegistry/game-work");
    Proof.Json->SetStringField(TEXT("stage"), TEXT("acquire-current"));
    if (!TestTrue(TEXT("Acquire or reuse genuinely current selected registry"),
            P4AcquireCurrentGameRegistry(Tools, Paks, Work, Reader, Error)))
    {
        AddError(Error);
        return false;
    }
    if (!TestTrue(TEXT("Current raw/tool/row identities fully known"),
            RegistryDigest(Reader->RegistrySha256()) && RegistryDigest(Reader->RetocSha256()) &&
                RegistryDigest(Reader->RepakSha256()) && RegistryDigest(Reader->MaterialRowsSha256()) &&
                Reader->MaterialError().IsEmpty()))
    {
        AddError(Reader->MaterialError());
        return false;
    }
    const FString Epoch = Reader->LeaseEpoch(), Raw = Reader->RegistrySha256(), Rows = Reader->MaterialRowsSha256(),
                  Retoc = Reader->RetocSha256(), Repak = Reader->RepakSha256();
    FString ActualRetoc, ActualRepak;
    int64 ToolSize = 0;
    if (!TestTrue(TEXT("Reader tool identities match independent current file hashes"),
            P4HashFile(Tools.Retoc, int64(512) * 1024 * 1024, ToolSize, ActualRetoc) && ToolSize > 0 &&
                P4HashFile(Tools.Repak, int64(512) * 1024 * 1024, ToolSize, ActualRepak) && ToolSize > 0 &&
                P4ExactNativeString(Retoc, ActualRetoc) && P4ExactNativeString(Repak, ActualRepak)))
        return false;
    const TArray<FString> Roots{TEXT("/Game/Carrot4/MasterMaterial/chr/M_chr_Shadow_Master"),
        TEXT("/Game/Carrot4/MasterMaterial/Unique/Silhouette/M_Silhouette_00_Master")};
    const TSet<FString> ExpectedMembers{TEXT("/Game/Carrot4/MasterMaterial/chr/M_chr_Shadow_Master"),
        TEXT("/Game/Carrot4/MasterMaterial/chr/MI_chr_Shadow"),
        TEXT("/Game/Carrot4/MasterMaterial/chr/MI_chr_Shadow_Dissolve"),
        TEXT("/Game/Carrot4/Characters/Teki/DiscoDamagumo/MI_chr_DiscoDamagumo_00_Shadow"),
        TEXT("/Game/Carrot4/Characters/Teki/HageDamagumo/MI_chr_HageDamagumo_00_Shadow"),
        TEXT("/Game/Carrot4/MasterMaterial/Unique/Silhouette/M_Silhouette_00_Master"),
        TEXT("/Game/Carrot4/MasterMaterial/Unique/Silhouette/MI_Silhouette_Object_00"),
        TEXT("/Game/Carrot4/MasterMaterial/Unique/Silhouette/MI_Silhouette_Player_00"),
        TEXT("/Game/Carrot4/MasterMaterial/Unique/Silhouette/MI_Silhouette_Teki_00"),
        TEXT("/Game/Carrot4/Characters/Teki/Dodoro/MI_chr_Dodoro_00_Silhouette")};
    TSet<FString> SeenMembers;
    TArray<TSharedPtr<FJsonValue>> Families;
    for (const FString& Root : Roots)
    {
        P4MapKit::Material::FMaterialRegistryFamily Family;
        if (!TestTrue(TEXT("Complete source-bound five-member family ") + Root,
                Reader->Family(Root, Family, Error) && Family.Members.size() == 5 &&
                    P4ExactNativeString(FString(UTF8_TO_TCHAR(Family.Master.Package.c_str())), Root)))
        {
            AddError(Error);
            return false;
        }
        TArray<TSharedPtr<FJsonValue>> Members;
        bool HasOutOfFolderChild = false;
        for (const auto& Member : Family.Members)
        {
            const FString Package = UTF8_TO_TCHAR(Member.Package.c_str());
            Members.Add(MakeShared<FJsonValueString>(Package));
            SeenMembers.Add(Package);
            HasOutOfFolderChild |= Package.StartsWith(TEXT("/Game/Carrot4/Characters/Teki/"));
        }
        if (!TestTrue(TEXT("Reverse child outside master folder is present"), HasOutOfFolderChild))
            return false;
        auto Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("master"), Root);
        Item->SetArrayField(TEXT("members"), Members);
        Families.Add(MakeShared<FJsonValueObject>(Item));
    }
    if (!TestEqual(TEXT("All ten exact known family identities are present"), SeenMembers.Num(), ExpectedMembers.Num()))
        return false;
    for (const FString& Member : ExpectedMembers)
        if (!TestTrue(TEXT("Exact current family member ") + Member, SeenMembers.Contains(Member)))
            return false;
    Proof.Json->SetArrayField(TEXT("families"), Families);
    Proof.Json->SetStringField(TEXT("stage"), TEXT("inventory-reuse"));
    if (!TestTrue(TEXT("Held actual reader is reused"),
            P4AcquireCurrentGameRegistry(Tools, Paks, Work, Again, Error) && Again.Get() == Reader.Get()))
        return false;
    FP4ExportGameInventory Inventory;
    if (!TestTrue(TEXT("Existing public export inventory shares exact current registry"),
            P4ReadExportGameInventory(Tools, Paks, Work, Inventory, Error) && Inventory.IsValid() &&
                P4ExactNativeString(Inventory.RegistrySha256(), Raw) && Inventory.Contains(FName(*Roots[0])) &&
                Inventory.Contains(FName(*Roots[1]))))
    {
        AddError(Error);
        return false;
    }
    if (!TestTrue(TEXT("Held epoch/raw/tool/row identities remain identical"),
            Reader->IsCurrent(Tools, Paks, Error) && P4ExactNativeString(Reader->LeaseEpoch(), Epoch) &&
                P4ExactNativeString(Reader->RegistrySha256(), Raw) &&
                P4ExactNativeString(Reader->MaterialRowsSha256(), Rows) &&
                P4ExactNativeString(Reader->RetocSha256(), Retoc) &&
                P4ExactNativeString(Reader->RepakSha256(), Repak) && Again.Get() == Reader.Get() &&
                RegistryWorkEmpty(Work)))
        return false;
    Proof.Json->SetStringField(TEXT("registry_sha256"), Raw);
    Proof.Json->SetStringField(TEXT("material_rows_sha256"), Rows);
    Proof.Json->SetStringField(TEXT("retoc_sha256"), Retoc);
    Proof.Json->SetStringField(TEXT("repak_sha256"), Repak);
    Proof.Json->SetStringField(TEXT("lease_epoch"), Epoch);
    Proof.Json->SetStringField(TEXT("material_schema"), Reader->MaterialSchema());
    Proof.Json->SetStringField(TEXT("inventory_sha256"), Inventory.InventorySha256());
    Proof.Json->SetStringField(TEXT("scope"),
        TEXT(
            "Current selected registry families and shared census only; no hydration/import/shader-owner or whole-asset closure."));
    Proof.Json->SetBoolField(TEXT("passed"), !HasAnyErrors());
    Proof.Json->SetStringField(TEXT("stage"), TEXT("complete"));
    return !HasAnyErrors();
}

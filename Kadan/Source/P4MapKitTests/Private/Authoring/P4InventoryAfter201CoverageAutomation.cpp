#include "P4AuthoringRows.h"
#include "P4NativeHash.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS && PLATFORM_WINDOWS
#include "AssetRegistryModule.h"
#include "Curves/CurveVector.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/MetaData.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/HideWindowsPlatformTypes.h"

namespace P4Inventory201
{
TSharedPtr<FJsonObject> Parse(const FString& Text)
{
    TSharedPtr<FJsonObject> Out;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out);
    return Out;
}
bool Exact(const FString& A, const FString& B)
{
    return A.Equals(B, ESearchCase::CaseSensitive);
}
TSharedPtr<FJsonObject> ObjectField(const TSharedPtr<FJsonObject>& Parent, const TCHAR* Name)
{
    const TSharedPtr<FJsonObject>* Out = nullptr;
    return Parent.IsValid() && Parent->TryGetObjectField(Name, Out) && Out && Out->IsValid() ? *Out : nullptr;
}
const TArray<TSharedPtr<FJsonValue>>* ArrayField(const TSharedPtr<FJsonObject>& Parent, const TCHAR* Name)
{
    const TArray<TSharedPtr<FJsonValue>>* Out = nullptr;
    return Parent.IsValid() && Parent->TryGetArrayField(Name, Out) ? Out : nullptr;
}
bool Bytes(UObject* Object, TArray<uint8>& Out)
{
    Out.Reset();
    if (!Object)
        return false;
    FObjectWriter Writer(Object, Out, false, false, false, PPF_DuplicateVerbatim);
    return !Writer.IsError() && Out.Num() > 0;
}
FString Nul(const FString& Prefix)
{
    FString Out = Prefix + TEXT("Xtail");
    Out[Prefix.Len()] = TCHAR(0);
    return Out;
}
struct FHeldRead
{
    FString Path;
    HANDLE Handle;
    explicit FHeldRead(const FString& Input)
        : Path(Input), Handle(CreateFileW(*Input, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr))
    {}
    ~FHeldRead()
    {
        if (Handle != INVALID_HANDLE_VALUE)
            CloseHandle(Handle);
    }
    bool Valid() const { return Handle != INVALID_HANDLE_VALUE; }
    bool ReadBytes(TArray<uint8>& Out) const
    {
        Out.Reset();
        LARGE_INTEGER Size, Start;
        Start.QuadPart = 0;
        if (!Valid() || !GetFileSizeEx(Handle, &Size) || Size.QuadPart < 0 || Size.QuadPart > 1024 * 1024 ||
            !SetFilePointerEx(Handle, Start, nullptr, FILE_BEGIN))
            return false;
        Out.SetNumUninitialized(int32(Size.QuadPart));
        for (int32 At = 0; At < Out.Num();)
        {
            const DWORD Count = DWORD(FMath::Min(65536, Out.Num() - At));
            DWORD Read = 0;
            if (!ReadFile(Handle, Out.GetData() + At, Count, &Read, nullptr) || Read != Count)
                return false;
            At += int32(Read);
        }
        return true;
    }
};
struct FFixture
{
    FAutomationTestBase& Test;
    FString Scope, ContentRoot, SentinelPath, Metadata;
    TStrongObjectPtr<UCurveVector> Sentinel;
    TArray<uint8> Before;
    EObjectFlags Flags;
    TMap<FString, TArray<uint8>> Files;
    bool BaselineReady = false;
    int32 Published = 0;
    FDelegateHandle Event;
    explicit FFixture(FAutomationTestBase& In) : Test(In), Flags(RF_NoFlags)
    {
        const FString Guid = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Scope = FPaths::ConvertRelativePathToFull(
            FPaths::ProjectSavedDir() / TEXT("P4NativeQuality/Inventory201") / Guid);
        ContentRoot = TEXT("/Game/NativeQuality/Inventory201_") + Guid;
        IFileManager::Get().MakeDirectory(*Scope, true);
        UPackage* Owner = CreatePackage(*(ContentRoot + TEXT("/Sentinel")));
        Sentinel.Reset(NewObject<UCurveVector>(Owner, TEXT("Sentinel"), RF_Public | RF_Standalone | RF_Transactional));
        Sentinel->FloatCurves[0].AddKey(1.0f, 7.5f);
        Sentinel->FloatCurves[1].AddKey(2.0f, -3.0f);
        Metadata = TEXT("whole native sentinel ") + Guid;
        Owner->GetMetaData()->SetValue(Sentinel.Get(), TEXT("P4Inventory201Owner"), *Metadata);
        Owner->SetDirtyFlag(false);
        BaselineReady = Check(
            TEXT("Mandatory baseline native writer succeeds with nonempty whole bytes"), Bytes(Sentinel.Get(), Before));
        Flags = Sentinel->GetFlags();
        SentinelPath = Sentinel->GetPathName();
        auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
        Event = Registry.OnInMemoryAssetCreated().AddLambda(
            [this](UObject* Object)
            {
                if (Object && Object->GetOutermost()->GetName().StartsWith(ContentRoot, ESearchCase::CaseSensitive))
                    ++Published;
            });
    }
    ~FFixture()
    {
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
            .Get()
            .OnInMemoryAssetCreated()
            .Remove(Event);
        Sentinel->ClearFlags(RF_Public | RF_Standalone);
        Sentinel->MarkPendingKill();
        // Keep the small GUID-owned evidence files; never delete foreign files or recurse into a shared scope.
    }
    bool Check(const FString& Label, bool Value) { return Test.TestTrue(Label, Value); }
    bool Track(const FString& Path, int32 Limit = 1024 * 1024)
    {
        const int64 Size = IFileManager::Get().FileSize(*Path);
        return Check(TEXT("Complete fixture/source file is bounded"), Size >= 0 && Size <= Limit) &&
            Check(TEXT("Complete file bytes retained independently"),
                FFileHelper::LoadFileToArray(Files.Add(Path), *Path));
    }
    bool Write(const FString& Leaf, const TArray<uint8>& Data)
    {
        // Data may belong to Files. Copy before a map insertion/replacement can move it.
        TArray<uint8> Owned = Data;
        const FString Path = Scope / Leaf;
        const bool Written = Check(TEXT("Owned fixture bytes written"), FFileHelper::SaveArrayToFile(Owned, *Path));
        if (Written)
            Files.Add(Path, MoveTemp(Owned));
        return Written;
    }
    bool Text(const FString& Leaf, const FString& Value)
    {
        FTCHARToUTF8 Converted(*Value, Value.Len());
        TArray<uint8> Raw;
        Raw.Append(reinterpret_cast<const uint8*>(Converted.Get()), Converted.Length());
        return Write(Leaf, Raw);
    }
    bool Same(const FHeldRead* Held = nullptr)
    {
        if (!Check(TEXT("Native fixture has a mandatory successful nonempty baseline and live sentinel"),
                BaselineReady && Before.Num() > 0 && Sentinel.IsValid()))
            return false;
        TArray<uint8> CurrentObject;
        bool Good = Check(TEXT("Mandatory current native writer succeeds with nonempty whole bytes"),
            Bytes(Sentinel.Get(), CurrentObject));
        Good = Check(TEXT("Full serialized native sentinel bytes unchanged"), Good && CurrentObject == Before) && Good;
        Good = Check(TEXT("Native pointer/path identity and all flags unchanged"),
                   FindObject<UObject>(nullptr, *SentinelPath) == Sentinel.Get() && Sentinel->GetFlags() == Flags) &&
            Good;
        Good = Check(TEXT("Native package remains clean and metadata presence/value unchanged"),
                   !Sentinel->GetOutermost()->IsDirty() &&
                       Sentinel->GetOutermost()->GetMetaData()->HasValue(Sentinel.Get(), TEXT("P4Inventory201Owner")) &&
                       Exact(Sentinel->GetOutermost()->GetMetaData()->GetValue(
                                 Sentinel.Get(), TEXT("P4Inventory201Owner")),
                           Metadata)) &&
            Good;
        Good = Check(TEXT("Read-only inventory emits no publication callback in the GUID-owned Content scope"),
                   Published == 0) &&
            Good;
        TArray<FString> ContentFiles;
        IFileManager::Get().FindFilesRecursive(
            ContentFiles, *FPackageName::LongPackageNameToFilename(ContentRoot), TEXT("*"), true, false);
        Good = Check(TEXT("Read-only inventory publishes no owned Content files"), ContentFiles.Num() == 0) && Good;
        for (const auto& Pair : Files)
        {
            TArray<uint8> Current;
            const bool Read = Held && Exact(Pair.Key, Held->Path) ? Held->ReadBytes(Current)
                                                                  : FFileHelper::LoadFileToArray(Current, *Pair.Key);
            Good = Check(TEXT("Every complete original and owned fixture file remains byte-identical"),
                       Read && Current == Pair.Value) &&
                Good;
        }
        return Good;
    }
    FString Read(const TFunction<FString()>& ReadApi, const FHeldRead* Held = nullptr)
    {
        // Readiness is mandatory before each API, including each held-file case.
        if (!Same(Held))
            return FString();
        const FString Result = ReadApi();
        return Same(Held) ? Result : FString();
    }
    TSharedPtr<FJsonObject> Verdict(const TFunction<FString()>& ReadApi, bool Expected, const TCHAR* Error = TEXT(""),
        const FHeldRead* Held = nullptr)
    {
        const auto Result = Parse(Read(ReadApi, Held));
        bool Valid = !Expected, Editable = true;
        FString Actual;
        bool Good = Check(TEXT("Public native inventory returns an explicit complete verdict"),
            Result.IsValid() && Result->TryGetBoolField(TEXT("valid"), Valid) && Valid == Expected);
        Good = Check(TEXT("Exact independent failure/success category"),
                   Result.IsValid() && Result->TryGetStringField(TEXT("error"), Actual) && Exact(Actual, Error)) &&
            Good;
        Good = Check(TEXT("Inventory never claims editable admission"),
                   Result.IsValid() && Result->TryGetBoolField(TEXT("editable_import_qualified"), Editable) &&
                       !Editable) &&
            Good;
        if (Result.IsValid() && !Expected)
            Good = Check(
                       TEXT("Refusal never exposes a partial source document"), !Result->HasField(TEXT("document"))) &&
                Good;
        if (Good && Expected)
        {
            if (Result->HasField(TEXT("map_struct_codecs")))
                Good = Check(TEXT("Successful decoded source contains its complete document and codec array"),
                           ObjectField(Result, TEXT("document")).IsValid() &&
                               ArrayField(Result, TEXT("map_struct_codecs"))) &&
                    Good;
            else if (Result->HasField(TEXT("effective_profile_qualified")))
                Good = Check(TEXT("Successful profile contains complete capture/group/source observations"),
                           ObjectField(Result, TEXT("capture_source")).IsValid() &&
                               ArrayField(Result, TEXT("original_groups")) &&
                               ArrayField(Result, TEXT("profile_sources"))) &&
                    Good;
            else
            {
                const auto Exports = ArrayField(Result, TEXT("exports")), Names = ArrayField(Result, TEXT("names"));
                Good =
                    Check(
                        TEXT(
                            "Successful package contains its nonempty actual export/name census and source inventories"),
                        Exports && Exports->Num() > 0 && Names && Names->Num() > 0 &&
                            ArrayField(Result, TEXT("imports")) && ArrayField(Result, TEXT("sidecars")) &&
                            ArrayField(Result, TEXT("header_spans")) && ArrayField(Result, TEXT("body_spans"))) &&
                    Good;
            }
        }
        return Good ? Result : nullptr;
    }
    TSharedPtr<FJsonObject> Profile(const FString& Capture, const TArray<FString>& Configs,
        const TArray<FString>& Sections, bool Expected, const TCHAR* Error = TEXT(""), const FHeldRead* Held = nullptr)
    {
        const auto Result = Verdict([&]()
            { return UP4AuthoringTools::NativeTextureProfileInventory(Capture, Configs, Sections); }, Expected, Error,
            Held);
        bool Good = Result.IsValid();
        if (Result.IsValid())
        {
            for (const TCHAR* Flag : {TEXT("effective_profile_qualified"), TEXT("original_defaults_qualified"),
                     TEXT("runtime_provenance_authenticated"), TEXT("source_files_atomic"),
                     TEXT("native_group_mapping_qualified")})
            {
                bool Qualified = true;
                Good = Check(TEXT("Read-only profile qualification flags remain independently false"),
                           Result->TryGetBoolField(Flag, Qualified) && !Qualified) &&
                    Good;
            }
            if (!Expected)
                Good = Check(TEXT("Refusal exposes no partial sources or original enum inventory"),
                           !Result->HasField(TEXT("capture_source")) && !Result->HasField(TEXT("profile_sources")) &&
                               !Result->HasField(TEXT("original_groups"))) &&
                    Good;
            else
            {
                const auto Groups = ArrayField(Result, TEXT("original_groups")),
                           Sources = ArrayField(Result, TEXT("profile_sources"));
                Good =
                    Check(
                        TEXT(
                            "Every successful profile retains the independently known 65 groups and selected file census"),
                        Groups && Groups->Num() == 65 && Sources && Sources->Num() == Configs.Num()) &&
                    Good;
            }
        }
        return Good ? Result : nullptr;
    }
    bool Capture(FString& Out)
    {
        const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
        const FString Original = Root / TEXT("quality/texture-capture-original-projection.json");
        if (!Check(TEXT("Root-owned actual 65-record evidence projection supplied"), !Root.IsEmpty()) ||
            !Track(Original, 8192))
            return false;
        int64 Size = 0;
        FString Hash;
        if (!Check(TEXT("Original capture exact whole-byte independent binding"),
                P4HashFile(Original, 8192, Size, Hash) && Size == 3522 &&
                    Exact(Hash, TEXT("a21e53f5dd5caa8d899eb395eb6b122e9faa58c1b48e630d0f75ae4088f4f65c"))))
            return false;
        Out = Scope / TEXT("capture.json");
        return Write(TEXT("capture.json"), Files.FindChecked(Original));
    }
    bool Pair(const FString& Original, FString& Header, FString& Body, const TCHAR* Leaf = TEXT("Source"))
    {
        const FString OriginalBody = FPaths::ChangeExtension(Original, TEXT("uexp"));
        if (!Track(Original) || !Track(OriginalBody))
            return false;
        Header = Scope / (FString(Leaf) + TEXT(".") + FPaths::GetExtension(Original));
        Body = FPaths::ChangeExtension(Header, TEXT("uexp"));
        return Write(FPaths::GetCleanFilename(Header), Files.FindChecked(Original)) &&
            Write(FPaths::GetCleanFilename(Body), Files.FindChecked(OriginalBody));
    }
};
const TCHAR* TableInput = TEXT(
    "table-recovery/catalog/originals/Carrot4/Content/Carrot4/Core/PresetStart/DT_PresetDebugUniqueIds.uasset");
const TCHAR* TutorialInput = TEXT(
    "table-recovery/catalog/originals/Carrot4/Content/Carrot4/Core/UI/DT_UITutorialGuide.uasset");
void Word(TArray<uint8>& Raw, int32 At, uint32 Value)
{
    for (int32 Byte = 0; Byte < 4; ++Byte)
        Raw[At + Byte] = uint8(Value >> (8 * Byte));
}
} //namespace P4Inventory201

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4Profile201SnapshotAndUTF8, "P4MapKit.Quality.ProfileInventory201SnapshotAndUTF8",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4Profile201SnapshotAndUTF8::RunTest(const FString&)
{
    using namespace P4Inventory201;
    FFixture F(*this);
    FString Capture;
    if (!F.Capture(Capture) || !F.Text(TEXT("valid.ini"), TEXT("[Windows DeviceProfile]\nDeviceType=Windows\n")))
        return false;
    const FString Config = F.Scope / TEXT("valid.ini");
    const TArray<FString> Sections{TEXT("Windows DeviceProfile")};
    const auto Good = F.Profile(Capture, {Config}, Sections, true);
    const auto Groups = ArrayField(Good, TEXT("original_groups")),
               ProfileSources = ArrayField(Good, TEXT("profile_sources"));
    if (!TestTrue(TEXT("Successful profile has complete independently known record/source census"),
            Groups && Groups->Num() == 65 && ProfileSources && ProfileSources->Num() == 1 &&
                (*ProfileSources)[0].IsValid() && (*ProfileSources)[0]->Type == EJson::Object))
        return false;
    const auto Entries = ArrayField((*ProfileSources)[0]->AsObject(), TEXT("entries"));
    if (!TestTrue(TEXT("Successful profile has the one known complete literal entry"),
            Entries && Entries->Num() == 1 && (*Entries)[0].IsValid() && (*Entries)[0]->Type == EJson::Object))
        return false;
    TestTrue(TEXT("Known literal profile value survives public read-only projection"),
        Exact((*Entries)[0]->AsObject()->GetStringField(TEXT("key")), TEXT("DeviceType")) &&
            Exact((*Entries)[0]->AsObject()->GetStringField(TEXT("value")), TEXT("Windows")));
    FString AboveVolumeRoot = F.Scope;
    for (int32 Parent = 0; Parent < 64; ++Parent)
        AboveVolumeRoot += TEXT("/..");
    AboveVolumeRoot += TEXT("/absent.json");
    FString Collapsed = AboveVolumeRoot;
    if (!TestTrue(TEXT("Runtime-owned absolute traversal exceeds the volume root"),
            !FPaths::IsRelative(AboveVolumeRoot) && !FPaths::CollapseRelativeDirectories(Collapsed)))
        return false;
    for (const FString& Path :
        {FString(), FString(TEXT("relative.json")), Nul(Capture), F.Scope / TEXT("absent.json"), AboveVolumeRoot})
        F.Profile(Path, {Config}, Sections, false, TEXT("texture-profile-capture-snapshot"));
    if (!F.Write(TEXT("empty.json"), {}))
        return false;
    F.Profile(F.Scope / TEXT("empty.json"), {Config}, Sections, false, TEXT("texture-profile-capture-snapshot"));
    if (!F.Text(TEXT("ancestor"), TEXT("ordinary file")))
        return false;
    F.Profile(
        F.Scope / TEXT("ancestor/child.json"), {Config}, Sections, false, TEXT("texture-profile-capture-snapshot"));
    // Actual malformed file bytes reach ProfileUTF8 before any engine replacement conversion.
    const TArray<TArray<uint8>> Invalid{{0}, {0xc0, 0x80}, {0x80}, {0xdf}, {0xe2, 0x28, 0xa1}, {0xe0, 0x80, 0x80},
        {0xed, 0xa0, 0x80}, {0xed, 0xbf, 0xbf}, {0xf0, 0x80, 0x80, 0x80}, {0xf4, 0x90, 0x80, 0x80},
        {0xf5, 0x80, 0x80, 0x80}};
    for (const auto& Bad : Invalid)
    {
        if (!F.Write(TEXT("raw.json"), Bad))
            return false;
        F.Profile(F.Scope / TEXT("raw.json"), {Config}, Sections, false, TEXT("texture-profile-capture-snapshot"));
        TArray<uint8> ConfigBytes = F.Files.FindChecked(Config);
        ConfigBytes.Append(Bad);
        if (!F.Write(TEXT("raw.ini"), ConfigBytes))
            return false;
        F.Profile(Capture, {F.Scope / TEXT("raw.ini")}, Sections, false, TEXT("texture-profile-config-snapshot"));
    }
    {
        FHeldRead Held(Capture);
        if (!TestTrue(TEXT("Owned captured evidence read exclusion acquired"), Held.Valid()))
            return false;
        F.Profile(Capture, {Config}, Sections, false, TEXT("texture-profile-capture-snapshot"), &Held);
    }
    F.Same();
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4Profile201Selections, "P4MapKit.Quality.ProfileInventory201SelectionAndLexical",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4Profile201Selections::RunTest(const FString&)
{
    using namespace P4Inventory201;
    FFixture F(*this);
    FString Capture;
    if (!F.Capture(Capture) || !F.Text(TEXT("config.ini"), TEXT("[Windows DeviceProfile]\nDeviceType=Windows\n")))
        return false;
    const FString Config = F.Scope / TEXT("config.ini");
    const TArray<FString> Sections{TEXT("Windows DeviceProfile")};
    F.Profile(Capture, {Config}, Sections, true);
    TArray<FString> ManyConfigs, ManySections;
    for (int32 At = 0; At < 17; ++At)
        ManyConfigs.Add(Config);
    for (int32 At = 0; At < 65; ++At)
        ManySections.Add(FString::Printf(TEXT("Section%d"), At));
    F.Profile(Capture, ManyConfigs, Sections, false, TEXT("texture-profile-selection-budget"));
    F.Profile(Capture, {Config}, ManySections, false, TEXT("texture-profile-selection-budget"));
    const FString WithNul = Nul(TEXT("Windows"));
    TestTrue(TEXT("Malformed selection keeps the indexed NUL and full suffix"),
        WithNul.Len() == 12 && WithNul[7] == 0 && Exact(WithNul.Right(4), TEXT("tail")));
    F.Profile(Capture, {Config}, {WithNul}, false, TEXT("texture-profile-selection-nul"));
    const TCHAR Noncharacter[] = {TCHAR(0xfffe), 0}, Surrogate[] = {TCHAR(0xd800), 0};
    F.Profile(Capture, {Config}, {FString(Noncharacter)}, false, TEXT("texture-profile-selection-unicode"));
    F.Profile(Capture, {Config}, {FString(Surrogate)}, false, TEXT("texture-profile-selection-unicode"));
    for (const TArray<FString>& Selection : {TArray<FString>{TEXT("")}, TArray<FString>{TEXT("Bad[Section")},
             TArray<FString>{TEXT("Windows DeviceProfile"), TEXT("Windows DeviceProfile")}})
        F.Profile(Capture, {Config}, Selection, false, TEXT("texture-profile-section-selection"));
    for (const FString& Path : {FString(), FString(TEXT("relative.ini")), Nul(Config), F.Scope / TEXT("absent.ini")})
        F.Profile(Capture, {Path}, Sections, false, TEXT("texture-profile-config-snapshot"));
    if (!F.Text(TEXT("bad.ini"), TEXT("[Windows DeviceProfile]\nDeviceType Windows\n")))
        return false;
    F.Profile(Capture, {F.Scope / TEXT("bad.ini")}, Sections, false, TEXT("texture-profile-assignment"));
    // Valid Unicode immediately below/above surrogate range and the maximum scalar
    // is written as actual bytes in an ignored comment, exercising UTF8 acceptance.
    TArray<uint8> Comment = F.Files.FindChecked(Config);
    Comment.Add(';');
    const uint8 Legal[] = {0xdf, 0xbf, 0xed, 0x9f, 0xbf, 0xee, 0x80, 0x80, 0xf4, 0x8f, 0xbf, 0xbf, '\n'};
    Comment.Append(Legal, UE_ARRAY_COUNT(Legal));
    if (!F.Write(TEXT("legal.ini"), Comment))
        return false;
    F.Profile(Capture, {F.Scope / TEXT("legal.ini")}, Sections, true);
    F.Same();
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4Package201ReadBoundaries, "P4MapKit.Quality.PackageInventory201ReadBoundaries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4Package201ReadBoundaries::RunTest(const FString&)
{
    using namespace P4Inventory201;
    FFixture F(*this);
    FString Header, Body;
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    if (!F.Pair(Root / TableInput, Header, Body))
        return false;
    F.Verdict([&]() { return UP4AuthoringTools::NativePackageInventory(Header); }, true);
    F.Verdict([&]() { return UP4AuthoringTools::NativeDecodeOriginalPackage(Header); }, true);
    const TCHAR* Missing = TEXT("missing-oversized-or-unreadable-source");
    for (const FString& Input : {F.Scope / TEXT("Source.txt"), F.Scope / TEXT("missing.uasset")})
    {
        F.Verdict([&]() { return UP4AuthoringTools::NativePackageInventory(Input); }, false, Missing);
        F.Verdict([&]() { return UP4AuthoringTools::NativeDecodeOriginalPackage(Input); }, false, Missing);
    }
    F.Verdict([&]() { return UP4AuthoringTools::NativePackageInventory(Header, F.Scope / TEXT("missing.uexp")); },
        false, Missing);
    F.Verdict([&]() { return UP4AuthoringTools::NativeDecodeOriginalPackage(Header, F.Scope / TEXT("missing.uexp")); },
        false, Missing);
    for (const FString& HeldPath : {Header, Body})
    {
        {
            FHeldRead Held(HeldPath);
            if (!TestTrue(TEXT("Read exclusion acquired only on owned copied source"), Held.Valid()))
                return false;
            TestTrue(TEXT("File census remains real while public source reader is denied"),
                IFileManager::Get().FileSize(*HeldPath) == F.Files.FindChecked(HeldPath).Num());
            for (const bool DecodeOriginal : {false, true})
                F.Verdict(
                    [&]()
                    {
                        return DecodeOriginal ? UP4AuthoringTools::NativeDecodeOriginalPackage(Header)
                                              : UP4AuthoringTools::NativePackageInventory(Header);
                    },
                    false, Missing, &Held);
        }
        F.Same();
    }
    const FString Upper = F.Scope / TEXT("Actual.UASSET");
    if (!F.Write(TEXT("Actual.UASSET"), F.Files.FindChecked(Header)) ||
        !F.Write(TEXT("Actual.uexp"), F.Files.FindChecked(Body)))
        return false;
    F.Verdict([&]() { return UP4AuthoringTools::NativePackageInventory(Upper); }, true);
    F.Verdict([&]() { return UP4AuthoringTools::NativeDecodeOriginalPackage(Upper); }, true);
    const FString Map = Root /
        TEXT("runtime-game-source/Carrot4/Content/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Cave000_F00_P.umap");
    if (!F.Track(Map) || !F.Track(FPaths::ChangeExtension(Map, TEXT("uexp"))))
        return false;
    const auto MapInventory = F.Verdict([&]() { return UP4AuthoringTools::NativePackageInventory(Map); }, true);
    const auto MapExports = ArrayField(MapInventory, TEXT("exports"));
    if (!TestTrue(TEXT("Successful actual world inventory retains complete known export census"),
            MapExports && MapExports->Num() == 78))
        return false;
    TestTrue(TEXT("Actual preserved world has independently known package"),
        Exact(MapInventory->GetStringField(TEXT("package")),
            TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Cave000_F00_P")));
    F.Verdict([&]() { return UP4AuthoringTools::NativeDecodeOriginalPackage(Map); }, false,
        TEXT("expected-one-document-export"));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4Package201ActualCodecs,
    "P4MapKit.Quality.PackageInventory201ActualCodecsAndReferences",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4Package201ActualCodecs::RunTest(const FString&)
{
    using namespace P4Inventory201;
    FFixture F(*this);
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    const FString Tutorial = Root / TutorialInput;
    if (!F.Track(Tutorial) || !F.Track(FPaths::ChangeExtension(Tutorial, TEXT("uexp"))))
        return false;
    auto Schema = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Carrot4.TutorialGuideTable"));
    auto Map = Schema ? CastField<FMapProperty>(Schema->FindPropertyByName(TEXT("HighlightGuideList"))) : nullptr;
    auto Key = Map ? CastField<FStrProperty>(Map->KeyProp) : nullptr;
    auto Value = Map ? CastField<FStructProperty>(Map->ValueProp) : nullptr;
    if (!TestTrue(TEXT("Actual native FString-key HighlightGuideParam map schema supplied"),
            Key && Value && Exact(Value->Struct->GetPathName(), TEXT("/Script/Carrot4.HighlightGuideParam"))))
        return false;
    const auto TutorialVerdict = F.Verdict(
        [&]() { return UP4AuthoringTools::NativeDecodeOriginalPackage(Tutorial); }, true);
    const auto Codecs = ArrayField(TutorialVerdict, TEXT("map_struct_codecs"));
    if (!TestTrue(TEXT("Successful actual tutorial decode has complete native codec observations"),
            Codecs && Codecs->Num() > 0 && Codecs->Num() <= 4096))
        return false;
    bool Found = false;
    for (const auto& Item : *Codecs)
    {
        if (!TestTrue(TEXT("Complete codec entry is a native projected object"),
                Item.IsValid() && Item->Type == EJson::Object))
            return false;
        const auto Codec = Item->AsObject();
        if (Exact(Codec->GetStringField(TEXT("owner")), TEXT("TutorialGuideTable")) &&
            Exact(Codec->GetStringField(TEXT("property")), TEXT("HighlightGuideList")))
        {
            Found = true;
            TestTrue(
                TEXT("Independent actual struct codec projection preserves value type/native bit and empty scalar key"),
                Codec->GetStringField(TEXT("key")).IsEmpty() && !Codec->GetBoolField(TEXT("key_native")) &&
                    Exact(Codec->GetStringField(TEXT("value")), TEXT("HighlightGuideParam")) &&
                    Codec->GetBoolField(TEXT("value_native")) ==
                        !!(Value->Struct->StructFlags & STRUCT_SerializeNative));
        }
    }
    TestTrue(TEXT("Actual captured source discovers its real native map value codec"), Found);
    const FString Counts = Root /
        TEXT(
            "original-scene-asset-inputs/Carrot4/Content/Carrot4/Maps/Main/Area/Area001/ActorPlacedNum/APN_Area001_P.uasset");
    if (!F.Track(Counts) || !F.Track(FPaths::ChangeExtension(Counts, TEXT("uexp"))))
        return false;
    const auto CountsVerdict = F.Verdict(
        [&]() { return UP4AuthoringTools::NativeDecodeOriginalPackage(Counts); }, true);
    const auto CountsDocument = ObjectField(CountsVerdict, TEXT("document"));
    if (!TestTrue(TEXT("Successful native count asset decode contains its complete source document"),
            CountsDocument.IsValid()))
        return false;
    TestTrue(TEXT("Actual non-table source preserves independently known native class and package"),
        Exact(CountsDocument->GetStringField(TEXT("cls")), TEXT("/Script/Carrot4.ActorPlacedNumAsset")) &&
            Exact(CountsDocument->GetStringField(TEXT("package")),
                TEXT("/Game/Carrot4/Maps/Main/Area/Area001/ActorPlacedNum/APN_Area001_P")));
    FString Header, Body;
    if (!F.Pair(Root / TableInput, Header, Body))
        return false;
    const TArray<uint8> Original = F.Files.FindChecked(Body);
    if (!TestTrue(TEXT("Bound actual RowStruct envelope/payload coordinates"),
            Original.Num() == 10260 && Original[0] == 18 && Original[8] == 15 && Original[16] == 4 &&
                Original[25] == 0xfb))
        return false;
    struct FMutation
    {
        int32 At;
        uint32 Value;
        const TCHAR* Error;
    };
    const FMutation Mutations[] = {{0, MAX_int32, TEXT("invalid-name")},
        {0, 9, TEXT("unsupported-table-settings-or-row-type")}, {8, 23, TEXT("truncated")},
        {25, 0, TEXT("unsupported-table-settings-or-row-type")}, {25, MAX_int32, TEXT("invalid-property-reference")}};
    for (const auto& Mutation : Mutations)
    {
        TArray<uint8> Altered = Original;
        Word(Altered, Mutation.At, Mutation.Value);
        if (!F.Write(FPaths::GetCleanFilename(Body), Altered))
            return false;
        // Header/package remains structurally valid; only actual source property bytes differ.
        F.Verdict([&]() { return UP4AuthoringTools::NativePackageInventory(Header); }, true);
        F.Verdict([&]() { return UP4AuthoringTools::NativeDecodeOriginalPackage(Header); }, false, Mutation.Error);
    }
    // Alter the real RowStruct payload length while keeping the whole package and
    // tagged envelope bounded. Padding is balanced by dropping four later row-data
    // bytes, so the preserved header/export ranges and final opaque footer stay put.
    TArray<uint8> Padded = Original;
    Padded.InsertZeroed(29, 4);
    Padded.RemoveAt(Original.Num() - 4, 4);
    Word(Padded, 16, 8);
    if (!F.Write(FPaths::GetCleanFilename(Body), Padded))
        return false;
    const auto PaddedInventory = F.Verdict([&]() { return UP4AuthoringTools::NativePackageInventory(Header); }, true);
    const auto PaddedExports = ArrayField(PaddedInventory, TEXT("exports"));
    if (!TestTrue(TEXT("Successful padded package retains one complete export"),
            PaddedExports && PaddedExports->Num() == 1 && (*PaddedExports)[0].IsValid() &&
                (*PaddedExports)[0]->Type == EJson::Object))
        return false;
    const auto Export = (*PaddedExports)[0]->AsObject();
    const auto Tags = ArrayField(Export, TEXT("tags"));
    if (!TestTrue(TEXT("Successful padded export retains one complete tagged envelope"),
            Tags && Tags->Num() == 1 && (*Tags)[0].IsValid() && (*Tags)[0]->Type == EJson::Object))
        return false;
    TestTrue(TEXT("Actual malformed RowStruct length remains a complete readable tagged envelope"),
        Export->GetBoolField(TEXT("tag_envelopes_valid")) && (*Tags)[0]->AsObject()->GetNumberField(TEXT("size")) == 8);
    F.Verdict([&]() { return UP4AuthoringTools::NativeDecodeOriginalPackage(Header); }, false,
        TEXT("unread-property-payload"));
    if (!F.Write(FPaths::GetCleanFilename(Body), Original))
        return false;
    F.Verdict([&]() { return UP4AuthoringTools::NativeDecodeOriginalPackage(Header); }, true);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4Texture201HeldSidecar, "P4MapKit.Quality.TextureInventory201HeldActualSidecar",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4Texture201HeldSidecar::RunTest(const FString&)
{
    using namespace P4Inventory201;
    FFixture F(*this);
    FString Header, Body;
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    const FString Repo = FPaths::ConvertRelativePathToFull(Root / TEXT("../.."));
    const FString Source = Repo /
        TEXT(
            "private/graphics-audit/sample-assets/Carrot4/Content/Carrot4/Characters/Buddy/Happy/T_chr_Happy_00_T00_D.uasset");
    if (!F.Pair(Source, Header, Body))
        return false;
    const FString Bulk = FPaths::ChangeExtension(Source, TEXT("ubulk")),
                  OwnedBulk = FPaths::ChangeExtension(Header, TEXT("ubulk"));
    if (!F.Track(Bulk) || !F.Write(FPaths::GetCleanFilename(OwnedBulk), F.Files.FindChecked(Bulk)))
        return false;
    const auto Texture = [&](const FString& Text)
    {
        const auto Inventory = Parse(Text);
        bool Valid = false;
        if (!Inventory.IsValid() || !Inventory->TryGetBoolField(TEXT("valid"), Valid) || !Valid)
            return TSharedPtr<FJsonObject>();
        const auto Exports = ArrayField(Inventory, TEXT("exports"));
        return Exports && Exports->Num() == 1 && (*Exports)[0].IsValid() && (*Exports)[0]->Type == EJson::Object
            ? ObjectField((*Exports)[0]->AsObject(), TEXT("texture_payload"))
            : TSharedPtr<FJsonObject>();
    };
    const FString BeforeInventory = F.Read(
        [&]() { return UP4AuthoringTools::NativePackageInventory(Header, Body, true); });
    const auto Before = Texture(BeforeInventory);
    if (!TestTrue(TEXT("Unchanged actual complete captured texture positively decodes before exclusion"),
            Before.IsValid() && Before->GetBoolField(TEXT("metadata_valid")) &&
                Before->GetBoolField(TEXT("pixels_decoded"))))
        return false;
    const auto Platforms = ArrayField(Before, TEXT("platforms"));
    if (!TestTrue(TEXT("Independent captured texture has one complete native platform"),
            Platforms && Platforms->Num() == 1 && (*Platforms)[0].IsValid() && (*Platforms)[0]->Type == EJson::Object))
        return false;
    const auto Platform = (*Platforms)[0]->AsObject();
    const auto Mips = ArrayField(Platform, TEXT("mips"));
    if (!TestTrue(TEXT("Independent original PF_DXT5 dimensions and mip census"),
            Exact(Platform->GetStringField(TEXT("format")), TEXT("PF_DXT5")) &&
                Platform->GetNumberField(TEXT("width")) == 512 && Platform->GetNumberField(TEXT("height")) == 512 &&
                Mips && Mips->Num() == 10 && (*Mips)[0].IsValid() && (*Mips)[0]->Type == EJson::Object))
        return false;
    const auto First = (*Mips)[0]->AsObject();
    TestTrue(TEXT("Independent original external raw payload and Python decoder pixel oracle"),
        Exact(First->GetStringField(TEXT("storage")), TEXT("ubulk")) &&
            Exact(First->GetStringField(TEXT("bytes")), TEXT("262144")) &&
            Exact(First->GetStringField(TEXT("sha256")),
                TEXT("aba6347db22016158a1c66f7125ff8ace645688eb3647e30a6e9b69e966d98e0")) &&
            Exact(First->GetStringField(TEXT("pixel_encoding")), TEXT("RGBA8")) &&
            Exact(First->GetStringField(TEXT("pixel_bytes")), TEXT("1048576")) &&
            Exact(First->GetStringField(TEXT("pixel_sha256")),
                TEXT("a206fc868057fa52b2735c47262450d139328566dd15cac00921187a8ec5fd46")));
    {
        FHeldRead Held(OwnedBulk);
        if (!TestTrue(TEXT("Only owned authentic sidecar is held against readers"), Held.Valid()))
            return false;
        TestEqual(TEXT("Native sidecar census remains its independent actual byte length"),
            IFileManager::Get().FileSize(*OwnedBulk), int64(F.Files.FindChecked(Bulk).Num()));
        for (const bool Decode : {false, true})
        {
            const auto Refused = Texture(
                F.Read([&]() { return UP4AuthoringTools::NativePackageInventory(Header, Body, Decode); }, &Held));
            TestTrue(TEXT("Real held-sidecar public inventory refuses with no partial platform or pixels"),
                Refused.IsValid() && !Refused->GetBoolField(TEXT("metadata_valid")) &&
                    Exact(Refused->GetStringField(TEXT("error")), TEXT("texture-bulk-file-unreadable-or-changed")) &&
                    !Refused->GetBoolField(TEXT("pixels_decoded")) && !Refused->HasField(TEXT("platforms")) &&
                    !Refused->GetBoolField(TEXT("editable_import_qualified")));
        }
    }
    F.Same();
    const FString AfterInventory = F.Read(
        [&]() { return UP4AuthoringTools::NativePackageInventory(Header, Body, true); });
    const auto After = Texture(AfterInventory);
    const FString OriginalInventory = F.Read(
        [&]()
        {
            return UP4AuthoringTools::NativePackageInventory(
                Source, FPaths::ChangeExtension(Source, TEXT("uexp")), true);
        });
    TestTrue(
        TEXT(
            "Released actual sidecar recovers successful baseline and exact untouched original payload/pixel inventory"),
        After.IsValid() && After->GetBoolField(TEXT("metadata_valid")) && After->GetBoolField(TEXT("pixels_decoded")) &&
            !BeforeInventory.IsEmpty() && !AfterInventory.IsEmpty() && !OriginalInventory.IsEmpty() &&
            Exact(AfterInventory, BeforeInventory) && Exact(AfterInventory, OriginalInventory));
    F.Same();
    return !HasAnyErrors();
}
#endif

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS && PLATFORM_WINDOWS
// PRIVATE draft: append before the final #endif in
// P4InventoryAfter201CoverageAutomation.cpp; reuse P4Inventory201::FFixture.
// Root alone owns promotion/build/editor execution. No cooked archive admission.
#include "P4TextureAuthoring.h"
#include "P4TextureProfileCapture.h"
#include "Engine/Texture2D.h"
#include "Misc/ScopeExit.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include <tuple>

namespace P4TextureControls258
{
bool CodeUnits(const FString& A, const FString& B)
{
    return A.Len() == B.Len() && !FMemory::Memcmp(*A, *B, SIZE_T(A.Len()) * sizeof(TCHAR));
}
bool EmptyCapture(const FP4TextureProfileCapture& Value)
{
    return Value.Entries.Num() == 0 && Value.DeclaredSourceSha256.IsEmpty() && Value.DeclaredCapturedAt.IsEmpty() &&
        Value.DeclaredScope.IsEmpty() && !Value.RuntimeProvenanceAuthenticated &&
        !Value.DefaultsProvenanceAuthenticated && !Value.CaptureAtomic;
}
bool SameBudget(const FP4TextureProfileCaptureBudget& A, const FP4TextureProfileCaptureBudget& B)
{
    return std::tie(
               A.MaxCharacters, A.MaxNotations, A.MaxDepth, A.MaxObjectKeys, A.MaxStringCharacters, A.MaxEnumEntries) ==
        std::tie(B.MaxCharacters, B.MaxNotations, B.MaxDepth, B.MaxObjectKeys, B.MaxStringCharacters, B.MaxEnumEntries);
}
bool SamePlatform(const P4MapKit::FTexturePlatformPayload& A, const P4MapKit::FTexturePlatformPayload& B)
{
    if (std::tie(A.Format, A.Layout.Name, A.Layout.BlockX, A.Layout.BlockY, A.Layout.BlockZ, A.Layout.Bytes,
            A.SkipOffset, A.Width, A.Height, A.Packed, A.Slices, A.ExtraData, A.MipsInTail, A.FirstMip, A.Start,
            A.End) !=
            std::tie(B.Format, B.Layout.Name, B.Layout.BlockX, B.Layout.BlockY, B.Layout.BlockZ, B.Layout.Bytes,
                B.SkipOffset, B.Width, B.Height, B.Packed, B.Slices, B.ExtraData, B.MipsInTail, B.FirstMip, B.Start,
                B.End) ||
        A.Mips.size() != B.Mips.size())
        return false;
    for (std::size_t Index = 0; Index < A.Mips.size(); ++Index)
    {
        const auto& X = A.Mips[Index];
        const auto& Y = B.Mips[Index];
        if (std::tie(X.Width, X.Height, X.Depth, X.LogicalWidth, X.LogicalHeight, X.LogicalDepth, X.Bulk.Source,
                X.Bulk.Offset, X.Bulk.Size, X.Bulk.Header.Flags, X.Bulk.Header.Count, X.Bulk.Header.Size,
                X.Bulk.Header.Offset, X.Bulk.Header.OldVersion, X.Bulk.Header.Start, X.Bulk.Header.End) !=
            std::tie(Y.Width, Y.Height, Y.Depth, Y.LogicalWidth, Y.LogicalHeight, Y.LogicalDepth, Y.Bulk.Source,
                Y.Bulk.Offset, Y.Bulk.Size, Y.Bulk.Header.Flags, Y.Bulk.Header.Count, Y.Bulk.Header.Size,
                Y.Bulk.Header.Offset, Y.Bulk.Header.OldVersion, Y.Bulk.Header.Start, Y.Bulk.Header.End))
            return false;
    }
    return true;
}
struct FWholeObject
{
    UObject* Object;
    UObject* Outer;
    UClass* Class;
    UPackage* Package;
    TStrongObjectPtr<UMetaData> Metadata;
    FString Path, PackagePath;
    EObjectFlags Flags, OwnerFlags, MetadataFlags;
    uint32 PackageFlags;
    bool Dirty, Ready;
    TArray<uint8> NativeBytes, MetadataBytes;
    explicit FWholeObject(UObject* In)
        : Object(In), Outer(In->GetOuter()), Class(In->GetClass()), Package(In->GetOutermost()),
          Metadata(Package->GetMetaData()), Path(In->GetPathName()), PackagePath(Package->GetPathName()),
          Flags(In->GetFlags()), OwnerFlags(Package->GetFlags()), MetadataFlags(Metadata->GetFlags()),
          PackageFlags(Package->GetPackageFlags()), Dirty(Package->IsDirty()), Ready(false)
    {
        Ready = P4Inventory201::Bytes(In, NativeBytes) && P4Inventory201::Bytes(Metadata.Get(), MetadataBytes);
    }
    bool Same(FAutomationTestBase& Test) const
    {
        TArray<uint8> Now, MetaNow;
        bool Good = Test.TestTrue(TEXT("Whole native and package-metadata baselines are successful and nonempty"),
            Ready && NativeBytes.Num() > 0 && MetadataBytes.Num() > 0);
        Good = Test.TestTrue(TEXT("Complete native serialization remains exactly unchanged"),
                   P4Inventory201::Bytes(Object, Now) && Now == NativeBytes) &&
            Good;
        Good = Test.TestTrue(TEXT("Complete metadata serialization, including every sibling key, remains exact"),
                   P4Inventory201::Bytes(Metadata.Get(), MetaNow) && MetaNow == MetadataBytes) &&
            Good;
        Good = Test.TestTrue(TEXT("Native object, class, outer, package and metadata pointer identities remain exact"),
                   FindObject<UObject>(nullptr, *Path) == Object && Object->GetOuter() == Outer &&
                       Object->GetClass() == Class && Object->GetOutermost() == Package &&
                       Package->GetMetaData() == Metadata.Get() && CodeUnits(Object->GetPathName(), Path) &&
                       CodeUnits(Package->GetPathName(), PackagePath)) &&
            Good;
        Good = Test.TestTrue(TEXT("All object/package/metadata flags and package dirty state remain exact"),
                   Object->GetFlags() == Flags && Package->GetFlags() == OwnerFlags &&
                       Metadata->GetFlags() == MetadataFlags && Package->GetPackageFlags() == PackageFlags &&
                       Package->IsDirty() == Dirty) &&
            Good;
        return Good;
    }
};
bool SameCapture(const FP4TextureProfileCapture& Out, const TSharedPtr<FJsonObject>& Original)
{
    const auto Records = P4Inventory201::ArrayField(Original, TEXT("enums"));
    if (!Records || Records->Num() != 1 || !(*Records)[0].IsValid() || (*Records)[0]->Type != EJson::Object)
        return false;
    const auto Record = (*Records)[0]->AsObject();
    FString Path;
    const auto Values = P4Inventory201::ArrayField(Record, TEXT("values"));
    FString Sha, At, Scope;
    if (!Record->TryGetStringField(TEXT("path"), Path) || !CodeUnits(Path, TEXT("/Script/Engine.TextureGroup")) ||
        !Values || Values->Num() != 65 || Out.Entries.Num() != 65 ||
        !Original->TryGetStringField(TEXT("source_sha256"), Sha) ||
        !Original->TryGetStringField(TEXT("captured_at"), At) || !Original->TryGetStringField(TEXT("scope"), Scope) ||
        !CodeUnits(Out.DeclaredSourceSha256, Sha) || !CodeUnits(Out.DeclaredCapturedAt, At) ||
        !CodeUnits(Out.DeclaredScope, Scope) || Out.RuntimeProvenanceAuthenticated ||
        Out.DefaultsProvenanceAuthenticated || Out.CaptureAtomic)
        return false;
    for (int32 Index = 0; Index < Values->Num(); ++Index)
    {
        if (!(*Values)[Index].IsValid() || (*Values)[Index]->Type != EJson::Object)
            return false;
        const auto Item = (*Values)[Index]->AsObject();
        FString Name;
        double Value = 0;
        // The bound original projection has 65 small integral ordinals. Avoid
        // treating the engine DOM's double as a general signed64 value oracle.
        if (!Item->TryGetStringField(TEXT("name"), Name) || !Item->TryGetNumberField(TEXT("value"), Value) ||
            !FMath::IsFinite(Value) || Value < -65536.0 || Value > 65536.0 || double(int64(Value)) != Value ||
            !CodeUnits(Out.Entries[Index].Name, Name) || Out.Entries[Index].Value != int64(Value))
            return false;
    }
    return true;
}
} //namespace P4TextureControls258

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CaptureWhitespace258, "P4MapKit.Quality.CaptureWhitespace258",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CaptureWhitespace258::RunTest(const FString&)
{
    using namespace P4TextureControls258;
    P4Inventory201::FFixture F(*this);
    FString Capture, Original;
    if (!F.Capture(Capture) ||
        !TestTrue(TEXT("Bound actual original projection loads"), FFileHelper::LoadFileToString(Original, *Capture)))
        return false;
    const auto Independent = P4Inventory201::Parse(Original);
    double Schema = 0;
    if (!TestTrue(TEXT("Actual original capture has independently parsed schema2"),
            Independent.IsValid() && Independent->TryGetNumberField(TEXT("schema_version"), Schema) && Schema == 2.0))
        return false;
    FWholeObject State(F.Sentinel.Get());
    if (!State.Same(*this) || !F.Same())
        return false;
    FP4TextureProfileCapture Baseline;
    FString Error;
    if (!TestTrue(TEXT("Unmodified original capture succeeds before the whitespace control"),
            P4ReadTextureProfileCapture(Original, {}, {}, Baseline, Error) && Error.IsEmpty() &&
                SameCapture(Baseline, Independent)))
        return false;
    FString Input;
    if (!TestTrue(TEXT("Independent engine serializer retains the complete original observation document"),
            FJsonSerializer::Serialize(Independent.ToSharedRef(),
                TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Input))))
        return false;
    const FString Key(TEXT("\"schema_version\":"));
    const int32 Position = Input.Find(Key, ESearchCase::CaseSensitive);
    if (!TestTrue(TEXT("Derived full capture has exactly one schema key token"),
            Position >= 0 &&
                Input.Find(Key, ESearchCase::CaseSensitive, ESearchDir::FromStart, Position + Key.Len()) == INDEX_NONE))
        return false;
    Input = Input.Left(Position) + TEXT("\"schema_version\" \t :") + Input.Mid(Position + Key.Len());
    if (!F.Text(TEXT("key-colon-whitespace.json"), Input))
        return false;
    const FString Before = Input;
    const FP4TextureProfileCaptureBudget Budget, BeforeBudget = Budget;
    FP4TextureProfileCapture Out;
    Out.Entries.Add({TEXT("stale"), -77});
    Out.DeclaredSourceSha256 = Out.DeclaredCapturedAt = Out.DeclaredScope = TEXT("stale");
    Out.RuntimeProvenanceAuthenticated = Out.DefaultsProvenanceAuthenticated = Out.CaptureAtomic = true;
    Error = TEXT("stale diagnostic");
    int32 Polls = 0;
    bool NoPartial = true;
    const TFunction<bool()> Observe = [&]()
    {
        ++Polls;
        NoPartial = NoPartial && EmptyCapture(Out);
        return false;
    };
    const bool Accepted = P4ReadTextureProfileCapture(Input, Budget, Observe, Out, Error);
    TestTrue(
        TEXT(
            "Whitespace between the quoted original key and colon succeeds with all65 independent ordered values and metadata"),
        Accepted && Error.IsEmpty() && SameCapture(Out, Independent));
    TestTrue(TEXT("Every cooperative parser callback sees cleared output, never partial selected data"),
        Polls > 0 && NoPartial);
    TestTrue(TEXT("Caller snapshot and every budget field remain exact"),
        CodeUnits(Input, Before) && SameBudget(Budget, BeforeBudget));
    FP4TextureProfileCapture Recovery;
    TestTrue(TEXT("Unmodified original capture remains independently exact after the control"),
        P4ReadTextureProfileCapture(Original, {}, {}, Recovery, Error) && Error.IsEmpty() &&
            SameCapture(Recovery, Independent));
    State.Same(*this);
    F.Same();
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TextureSourceClassMismatch258, "P4MapKit.Quality.TextureSourceClassMismatch258",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TextureSourceClassMismatch258::RunTest(const FString&)
{
    using namespace P4TextureControls258;
    using namespace P4MapKit;
    P4Inventory201::FFixture F(*this);
    TStrongObjectPtr<UTexture2D> Producer(
        NewObject<UTexture2D>(F.Sentinel->GetOutermost(), TEXT("PlaneProducer"), RF_Public | RF_Standalone));
    ON_SCOPE_EXIT
    {
        Producer->ClearFlags(RF_Public | RF_Standalone);
        Producer->MarkPendingKill();
    };
    const uint8 Literal[] = {0, 17, 255, 0, 31, 63, 127, 255, 255, 1, 64, 33, 79, 95, 111, 207};
    Producer->Source.Init(2, 2, 1, 1, TSF_BGRA8, Literal);
    TArray64<uint8> Pixels;
    if (!TestTrue(TEXT("Public engine source readback matches every authored pixel/channel/alpha byte"),
            Producer->Source.GetMipData(Pixels, 0) && Pixels.Num() == int64(UE_ARRAY_COUNT(Literal)) &&
                !FMemory::Memcmp(Pixels.GetData(), Literal, sizeof(Literal))))
        return false;
    Producer->GetOutermost()->GetMetaData()->SetValue(Producer.Get(), TEXT("P4TextureControls258Owner"), *F.Scope);
    Producer->GetOutermost()->SetDirtyFlag(false);
    FWholeObject SourceState(Producer.Get()), SentinelState(F.Sentinel.Get());
    const FGuid SourceId = Producer->Source.GetId();
    FTexturePlatformPayload Platform;
    Platform.Format = "PF_B8G8R8A8";
    Platform.Width = 2;
    Platform.Height = 2;
    Platform.Slices = 1;
    FTextureMipPayload Mip;
    Mip.LogicalWidth = 2;
    Mip.LogicalHeight = 2;
    Mip.LogicalDepth = 1;
    Mip.Bulk.Source = EBulkSource::Body;
    Mip.Bulk.Offset = 0;
    Mip.Bulk.Size = uint64(Pixels.Num());
    Platform.Mips.push_back(Mip);
    const FTexturePlatformPayload BeforePlatform = Platform;
    const std::vector<uint8> Held(Pixels.GetData(), Pixels.GetData() + Pixels.Num()), Before = Held;
    int32 Reads = 0, Pre = 0, Post = 0;
    const FDelegateHandle PreHandle = FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddLambda(
        [&](UObject* Target, const FEditPropertyChain&)
        {
            if (Target == Producer.Get())
                ++Pre;
        });
    const FDelegateHandle PostHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddLambda(
        [&](UObject* Target, FPropertyChangedEvent&)
        {
            if (Target == Producer.Get())
                ++Post;
        });
    ON_SCOPE_EXIT
    {
        FCoreUObjectDelegates::OnPreObjectPropertyChanged.Remove(PreHandle);
        FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PostHandle);
    };
    if (!SourceState.Same(*this) || !SentinelState.Same(*this) || !F.Same())
        return false;
    for (const ETextureShape Shape : {ETextureShape::Cube, ETextureShape::Array, ETextureShape::Volume})
    {
        // A genuine plane producer is intentionally requested as another shape.
        // This must fail the class contract before platform validation/reads;
        // it never pretends these bytes are an accepted cube/array/volume archive.
        FP4TextureSourceRequest Request;
        Request.Shape = Shape;
        Request.NativeClass = Producer->GetClass()->GetPathName();
        Request.Platform = &Platform;
        Request.MaxDecodedBytes = uint64(Held.size());
        Request.ReadAt = [&](EBulkSource Source, uint64 At, uint8* Dest, SIZE_T Count)
        {
            ++Reads;
            if (Source != EBulkSource::Body || At > Held.size() || Count > Held.size() - SIZE_T(At))
                return false;
            FMemory::Memcpy(Dest, Held.data() + SIZE_T(At), Count);
            return true;
        };
        FP4TextureSourceResult Out;
        Out.Texture = Producer.Get();
        Out.DecodedBytes = 97;
        Out.SurvivingFirstMip = 19;
        Out.SourceReadbackVerified = true;
        bool ClearedAtPoll = true;
        int32 Polls = 0;
        Request.Cancel = [&]()
        {
            ++Polls;
            ClearedAtPoll = ClearedAtPoll && !Out.Texture && !Out.DecodedBytes && !Out.SurvivingFirstMip &&
                !Out.SourceReadbackVerified;
            return false;
        };
        FReadError Error;
        TestTrue(
            FString::Printf(TEXT("Shape%d refuses the wrong genuine native class with exact category"), int32(Shape)),
            !P4BuildPrivateTextureSource(Request, Out, Error) &&
                Error.Code == "texture-source-native-class-unqualified");
        TestTrue(TEXT("Refusal clears every stale result field before cooperative callbacks and reads no source rows"),
            !Out.Texture && !Out.DecodedBytes && !Out.SurvivingFirstMip && !Out.SourceReadbackVerified && Polls > 0 &&
                ClearedAtPoll && Reads == 0);
        TArray64<uint8> After;
        TestTrue(TEXT("Producer retains exact source GUID/dimensions/mips/format and every byte"),
            Producer->Source.GetId() == SourceId && Producer->Source.GetSizeX() == 2 &&
                Producer->Source.GetSizeY() == 2 && Producer->Source.GetNumSlices() == 1 &&
                Producer->Source.GetNumMips() == 1 && Producer->Source.GetFormat() == TSF_BGRA8 &&
                Producer->Source.GetMipData(After, 0) && After == Pixels && Held == Before &&
                SamePlatform(Platform, BeforePlatform));
        TestTrue(TEXT("Every caller source-request identity, shape, pointer, flags and budget remains exact"),
            Request.Shape == Shape && CodeUnits(Request.NativeClass, Producer->GetClass()->GetPathName()) &&
                Request.Platform == &Platform && Request.LightMapFlags == 0 &&
                Request.MaxDecodedBytes == uint64(Held.size()));
        TestTrue(TEXT("Class refusal changes no resource/platform data and emits no property notifications"),
            Producer->PlatformData == nullptr && Producer->Resource == nullptr && Pre == 0 && Post == 0);
        SourceState.Same(*this);
        SentinelState.Same(*this);
        F.Same();
    }
    FP4TextureSourceRequest Valid;
    Valid.NativeClass = Producer->GetClass()->GetPathName();
    Valid.Platform = &Platform;
    Valid.MaxDecodedBytes = uint64(Held.size());
    Valid.ReadAt = [&](EBulkSource Source, uint64 At, uint8* Dest, SIZE_T Count)
    {
        ++Reads;
        if (Source != EBulkSource::Body || At > Held.size() || Count > Held.size() - SIZE_T(At))
            return false;
        FMemory::Memcpy(Dest, Held.data() + SIZE_T(At), Count);
        return true;
    };
    FP4TextureSourceResult Recovery;
    FReadError RecoveryError;
    const bool Recovered = P4BuildPrivateTextureSource(Valid, Recovery, RecoveryError);
    TStrongObjectPtr<UTexture> Staged(Recovery.Texture);
    ON_SCOPE_EXIT
    {
        if (Staged.IsValid())
        {
            Staged->Source.Init(0, 0, 0, 0, TSF_Invalid);
            Staged->MarkPendingKill();
        }
    };
    TArray64<uint8> Stored;
    TestTrue(
        TEXT(
            "The original qualified plane request succeeds after wrong-class refusals with exact independent source bytes"),
        Recovered && RecoveryError.Code.empty() && Staged.IsValid() && Staged->GetClass() == Producer->GetClass() &&
            Recovery.DecodedBytes == uint64(Held.size()) && Recovery.SurvivingFirstMip == 0 &&
            Recovery.SourceReadbackVerified && Reads > 0 && Staged->Source.GetSizeX() == 2 &&
            Staged->Source.GetSizeY() == 2 && Staged->Source.GetNumSlices() == 1 && Staged->Source.GetNumMips() == 1 &&
            Staged->Source.GetFormat() == TSF_BGRA8 && Staged->Source.GetMipData(Stored, 0) && Stored == Pixels);
    SourceState.Same(*this);
    SentinelState.Same(*this);
    F.Same();
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ProfileSourceDirectory258, "P4MapKit.Quality.ProfileSourceDirectory258",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ProfileSourceDirectory258::RunTest(const FString&)
{
    using namespace P4TextureControls258;
    P4Inventory201::FFixture F(*this);
    FString Capture;
    if (!F.Capture(Capture) || !F.Text(TEXT("config.ini"), TEXT("[Windows DeviceProfile]\nDeviceType=Windows\n")))
        return false;
    const FString Config = F.Scope / TEXT("config.ini");
    const TArray<FString> Sections{TEXT("Windows DeviceProfile")};
    FWholeObject State(F.Sentinel.Get());
    if (!State.Same(*this) || !F.Same())
        return false;
    const FString Baseline = F.Read(
        [&]() { return UP4AuthoringTools::NativeTextureProfileInventory(Capture, {Config}, Sections); });
    if (!TestTrue(TEXT("Actual original capture and complete literal config give a successful public baseline"),
            !Baseline.IsEmpty() && F.Profile(Capture, {Config}, Sections, true).IsValid()))
        return false;
    FString Forbidden = FPaths::ConvertRelativePathToFull(
        FPackageName::LongPackageNameToFilename(F.ContentRoot / TEXT("NeverRead/capture"), TEXT(".json")));
    FPaths::NormalizeFilename(Forbidden);
    FString Collapsed = Forbidden;
    if (!TestTrue(
            TEXT(
                "Source directory control is absolute, normalized, within only the GUID-owned Content scope and has no file"),
            !FPaths::IsRelative(Forbidden) && FPaths::CollapseRelativeDirectories(Collapsed) &&
                CodeUnits(Collapsed, Forbidden) &&
                FPaths::GetPath(Forbidden).Contains(TEXT("/Content/"), ESearchCase::IgnoreCase) &&
                !IFileManager::Get().FileExists(*Forbidden)))
        return false;
    // The actual custody rule rejects Content before any source file is opened.
    // No byte copy or directory is created at the forbidden path.
    TestTrue(TEXT("Public profile rejects Content as a source directory with no partial document/provenance"),
        F.Profile(Forbidden, {Config}, Sections, false, TEXT("texture-profile-capture-snapshot")).IsValid());
    const FString Recovered = F.Read(
        [&]() { return UP4AuthoringTools::NativeTextureProfileInventory(Capture, {Config}, Sections); });
    TestTrue(TEXT("Complete successful public baseline JSON recovers code-unit exactly after directory refusal"),
        CodeUnits(Recovered, Baseline) && F.Profile(Capture, {Config}, Sections, true).IsValid());
    State.Same(*this);
    F.Same();
    return !HasAnyErrors();
}

// Append inside P4InventoryAfter201CoverageAutomation.cpp guard, after P4TextureControls258.
#include "P4TextureInventory.h"
#include "Async/Async.h"
#include "Engine/Texture.h"
#include "UObject/StrongObjectPtr.h"

namespace P4TextureRemaining336Public
{
using namespace P4Inventory201;
using namespace P4TextureControls258;
std::vector<uint8> Vector(const TArray<uint8>& A)
{
    return A.Num() ? std::vector<uint8>(A.GetData(), A.GetData() + A.Num()) : std::vector<uint8>();
}
FString Report(const TSharedPtr<FJsonObject>& J)
{
    FString Out;
    if (J.IsValid())
        FJsonSerializer::Serialize(
            J.ToSharedRef(), TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out));
    return Out;
}
bool TextureFlags(const TSharedPtr<FJsonObject>& J, bool Metadata, bool Properties)
{
    if (!J.IsValid())
        return false;
    bool M = !Metadata, P = !Properties;
    bool Editable = true, Art = true, Pixels = false, Offsets = true;
    return J->TryGetBoolField(TEXT("metadata_valid"), M) && M == Metadata &&
        J->TryGetBoolField(TEXT("properties_valid"), P) && P == Properties &&
        J->TryGetBoolField(TEXT("editable_import_qualified"), Editable) && !Editable &&
        J->TryGetBoolField(TEXT("original_editable_source_recovered"), Art) && !Art &&
        J->TryGetBoolField(TEXT("pixels_decoded"), Pixels) && Pixels == Metadata &&
        J->TryGetBoolField(TEXT("native_skip_offsets_qualified"), Offsets) && !Offsets;
}
bool ActualPixels(const TSharedPtr<FJsonObject>& J)
{
    const auto Platforms = ArrayField(J, TEXT("platforms"));
    if (!Platforms || Platforms->Num() != 1 || !(*Platforms)[0].IsValid() || (*Platforms)[0]->Type != EJson::Object)
        return false;
    const auto Platform = (*Platforms)[0]->AsObject();
    const auto Mips = ArrayField(Platform, TEXT("mips"));
    FString Format;
    if (!Platform->TryGetStringField(TEXT("format"), Format) || !CodeUnits(Format, TEXT("PF_DXT5")) || !Mips ||
        Mips->Num() != 10 || !(*Mips)[0].IsValid() || (*Mips)[0]->Type != EJson::Object)
        return false;
    const auto First = (*Mips)[0]->AsObject();
    FString Raw, Pixels;
    return First->TryGetStringField(TEXT("sha256"), Raw) &&
        CodeUnits(Raw, TEXT("aba6347db22016158a1c66f7125ff8ace645688eb3647e30a6e9b69e966d98e0")) &&
        First->TryGetStringField(TEXT("pixel_sha256"), Pixels) &&
        CodeUnits(Pixels, TEXT("a206fc868057fa52b2735c47262450d139328566dd15cac00921187a8ec5fd46"));
}
} //namespace P4TextureRemaining336Public

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4TexturePublicObservations336, "P4MapKit.Quality.TexturePublicObservations336",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4TexturePublicObservations336::RunTest(const FString&)
{
    using namespace P4TextureRemaining336Public;
    using namespace P4MapKit;
    FFixture F(*this);
    FString Header, BodyPath;
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT")),
                  Repo = FPaths::ConvertRelativePathToFull(Root / TEXT("../.."));
    const FString Original = Repo /
        TEXT(
            "private/graphics-audit/sample-assets/Carrot4/Content/Carrot4/Characters/Buddy/Happy/T_chr_Happy_00_T00_D.uasset");
    if (!TestFalse(TEXT("Original capture root supplied"), Root.IsEmpty()) || !F.Pair(Original, Header, BodyPath))
        return false;
    const FString OriginalBulk = FPaths::ChangeExtension(Original, TEXT("ubulk")),
                  Bulk = FPaths::ChangeExtension(Header, TEXT("ubulk"));
    if (!F.Track(OriginalBulk) || !F.Write(FPaths::GetCleanFilename(Bulk), F.Files.FindChecked(OriginalBulk)))
        return false;
    const auto Head = Vector(F.Files.FindChecked(Header)), Body = Vector(F.Files.FindChecked(BodyPath));
    const auto Package = ReadPackage(Head, Body);
    if (!TestTrue(TEXT("Actual complete original texture package has one supported export"),
            Package.Error.Code.empty() && Package.Exports.size() == 1))
        return false;
    const auto Export = Package.Exports[0];
    std::string Class;
    if (!TestTrue(TEXT("Actual captured class reference, never replaced metadata"),
            Package.Reference(Export.Class, Class) && Class == "/Script/Engine.Texture2D"))
        return false;
    const auto Properties = ReadPropertyEnvelopes(Body, Package.Names, Export.Offset, Export.Offset + Export.Size);
    if (!TestTrue(TEXT("Actual complete original envelopes are independently parsed"),
            Properties.Error.Code.empty() && !Properties.Properties.empty()))
        return false;
    FWholeObject State(F.Sentinel.Get());
    if (!State.Same(*this) || !F.Same())
        return false;
    const auto Baseline = P4TexturePayloadInventory(Package, Export, Properties, Body, Header, true);
    const FString Before = Report(Baseline);
    if (!TestTrue(
            TEXT(
                "Genuine captured texture public inventory independently retains raw/pixel hashes and all qualification limits"),
            TextureFlags(Baseline, true, true) && ActualPixels(Baseline) && !Before.IsEmpty()))
        return false;
    // Prime/retain the engine's immutable format enum before a read-only worker.
    TStrongObjectPtr<UEnum> Formats(UTexture::GetPixelFormatEnum());
    if (!TestNotNull(TEXT("Actual format enum exists before the worker and survives its entire join"), Formats.Get()))
        return false;
    auto Future = Async(EAsyncExecution::Thread, [Package, Export, Properties, Body, Header]()
        { return P4TexturePayloadInventory(Package, Export, Properties, Body, Header, true); });
    const auto Worker = Future.Get();
    FString WorkerError;
    TestTrue(TEXT("Worker performs source observations while refusing UObject schema qualification"),
        TextureFlags(Worker, true, false) && ActualPixels(Worker) &&
            Worker->TryGetStringField(TEXT("property_error"), WorkerError) &&
            CodeUnits(WorkerError,
                TEXT("Texture: editor thread, supported complete schema and valid source inventories required")) &&
            !Worker->HasField(TEXT("property_inventory")));
    State.Same(*this);
    F.Same();
    // Invalid independent export observation exercises the public missing-reference contract.
    // Neither an actual native descriptor nor the authentic package/source is changed.
    auto MissingReference = Export;
    MissingReference.Class = MAX_int32;
    std::string Unresolved;
    TestTrue(TEXT("Independent invalid class index is demonstrably outside authentic captured package references"),
        !Package.Reference(MissingReference.Class, Unresolved));
    TestTrue(TEXT("Unresolvable caller export observation returns no texture report"),
        !P4TexturePayloadInventory(Package, MissingReference, Properties, Body, Header, true).IsValid() &&
            MissingReference.Class == MAX_int32 && Package.Reference(Export.Class, Class) &&
            Class == "/Script/Engine.Texture2D");
    // Independently produced incomplete envelope observation: original package/body/exports remain authentic and unchanged.
    auto Truncated = Body;
    Truncated.resize(Export.Offset + 1);
    const auto Incomplete = ReadPropertyEnvelopes(Truncated, Package.Names, Export.Offset, Truncated.size());
    if (!TestTrue(TEXT("Bounded original prefix itself produces a genuine incomplete-envelope diagnostic"),
            !Incomplete.Error.Code.empty()))
        return false;
    const auto Refused = P4TexturePayloadInventory(Package, Export, Incomplete, Body, Header, true);
    FString Error;
    TestTrue(TEXT("Incomplete independent observation retains exact error and emits no partial platform publication"),
        TextureFlags(Refused, false, false) && Refused->TryGetStringField(TEXT("error"), Error) &&
            CodeUnits(Error, FString(UTF8_TO_TCHAR(Incomplete.Error.Code.c_str()))) &&
            !Refused->HasField(TEXT("platforms")));
    State.Same(*this);
    F.Same();
    FString TableHeader, TableBody;
    if (!F.Pair(Root / TableInput, TableHeader, TableBody, TEXT("ActualTable336")))
        return false;
    const auto TableHead = Vector(F.Files.FindChecked(TableHeader)),
               TableBytes = Vector(F.Files.FindChecked(TableBody));
    const auto Table = ReadPackage(TableHead, TableBytes);
    if (!TestTrue(TEXT("Actual independently captured nontexture package is a genuine DataTable"),
            Table.Error.Code.empty() && Table.Exports.size() == 1 && Table.Reference(Table.Exports[0].Class, Class) &&
                Class == "/Script/Engine.DataTable"))
        return false;
    const auto TableProps = ReadPropertyEnvelopes(
        TableBytes, Table.Names, Table.Exports[0].Offset, Table.Exports[0].Offset + Table.Exports[0].Size);
    TestTrue(TEXT("Nontexture export is omitted by the public texture adapter without inventing a texture class"),
        TableProps.Error.Code.empty() &&
            !P4TexturePayloadInventory(Table, Table.Exports[0], TableProps, TableBytes, TableHeader, true).IsValid());
    const auto Recovery = P4TexturePayloadInventory(Package, Export, Properties, Body, Header, true);
    TestTrue(TEXT("All original source/native properties and decoded pixels recover the exact successful baseline"),
        TextureFlags(Recovery, true, true) && ActualPixels(Recovery) && CodeUnits(Report(Recovery), Before));
    State.Same(*this);
    F.Same();
    return !HasAnyErrors();
}

#endif

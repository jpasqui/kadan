#include "P4KadanBlueprintPlans.h"
#include "P4KadanImport.h"
#include "P4KadanTestSeams.h"
#include "P4BoundedSourceRead.h"
#include "P4NativeHash.h"
THIRD_PARTY_INCLUDES_START
#include "P4MapKitCore/package_reader.h"
THIRD_PARTY_INCLUDES_END
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace P4FreshWidgetPlanning
{
bool Json(const FString& File, int64 Limit, TSharedPtr<FJsonObject>& Out, FString& Digest)
{
    TArray<uint8> Bytes;
    FString Error;
    if (!P4ReadBoundedSourceFile(File, Limit, Bytes, Error) ||
        !P4HashSourceBuffers(Bytes.GetData(), Bytes.Num(), nullptr, 0, Digest))
        return false;
    const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
    const FString Text(Converted.Length(), Converted.Get());
    return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out) && Out.IsValid();
}
bool ExactFile(const FString& File, const TSharedPtr<FJsonObject>& Record)
{
    int64 Size = 0;
    FString Digest, Expected;
    double Bytes = -1;
    return Record.IsValid() && Record->TryGetStringField(TEXT("sha256"), Expected) &&
        Record->TryGetNumberField(TEXT("bytes"), Bytes) && Bytes >= 0 && Bytes <= 64 * 1024 * 1024 &&
        P4HashFile(File, 64 * 1024 * 1024, Size, Digest) && double(Size) == Bytes &&
        P4ExactNativeString(Digest, Expected);
}
int32 ArrayCount(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    return Object.IsValid() && Object->TryGetArrayField(Name, Values) ? Values->Num() : -1;
}
FString Kind(const FString& Name)
{
    if (Name == TEXT("BlueprintGeneratedClass"))
        return TEXT("Blueprint");
    if (Name == TEXT("AnimBlueprintGeneratedClass"))
        return TEXT("AnimBlueprint");
    if (Name == TEXT("WidgetBlueprintGeneratedClass"))
        return TEXT("WidgetBlueprint");
    return Name;
}
// This is ScanGame's root-export rule applied only to the manifest's exact
// paths. It performs no directory scan, source acquisition or asset load.
bool Descriptor(const FString& Package, const TArray<FString>& Paths, const TSharedPtr<FJsonObject>& Files,
    TSharedPtr<FP4KadanSourceAsset>& Out)
{
    FString HeaderPath, BodyPath;
    for (const FString& Path : Paths)
    {
        const TSharedPtr<FJsonObject>* Record = nullptr;
        if (!Files->TryGetObjectField(Path, Record) || !ExactFile(Path, *Record))
            return false;
        if (Path.EndsWith(TEXT(".uasset")) || Path.EndsWith(TEXT(".umap")))
        {
            if (!HeaderPath.IsEmpty())
                return false;
            HeaderPath = Path;
        }
        if (Path.EndsWith(TEXT(".uexp")))
        {
            if (!BodyPath.IsEmpty())
                return false;
            BodyPath = Path;
        }
    }
    if (HeaderPath.IsEmpty() || BodyPath.IsEmpty() ||
        !FPaths::IsSamePath(FPaths::ChangeExtension(HeaderPath, TEXT("uexp")), BodyPath))
        return false;
    TArray<uint8> Header;
    FString Error;
    if (!P4ReadBoundedSourceFile(HeaderPath, 64 * 1024 * 1024, Header, Error))
        return false;
    const int64 BodySize = IFileManager::Get().FileSize(*BodyPath);
    if (BodySize < 0 || BodySize > 64 * 1024 * 1024)
        return false;
    const auto Inventory = P4MapKit::ReadPackageHeader(
        std::vector<uint8>(Header.GetData(), Header.GetData() + Header.Num()), size_t(BodySize));
    if (!Inventory.Error.Code.empty() ||
        !P4ExactNativeString(FString(UTF8_TO_TCHAR(Inventory.Package.c_str())), Package))
        return false;
    auto Asset = MakeShared<FP4KadanSourceAsset>();
    Asset->Package = Package;
    Asset->Header = HeaderPath;
    Asset->Body = BodyPath;
    const FString Leaf = FPackageName::GetLongPackageAssetName(Package);
    FString First, Class;
    for (const auto& Export : Inventory.Exports)
    {
        if (Export.Outer != 0)
            continue;
        std::string Ref;
        if (!Inventory.Reference(Export.Class, Ref))
            continue;
        FString Native(UTF8_TO_TCHAR(Ref.c_str()));
        int32 Dot = INDEX_NONE;
        if (Native.FindLastChar(TEXT('.'), Dot))
            Native = Native.Mid(Dot + 1);
        const FString Name(UTF8_TO_TCHAR(Export.Name.c_str()));
        if (First.IsEmpty())
            First = Kind(Native);
        if (Name == Leaf)
        {
            Asset->Kind = Kind(Native);
            break;
        }
        if (Class.IsEmpty() && Name == Leaf + TEXT("_C"))
            Class = Kind(Native);
    }
    if (Asset->Kind.IsEmpty())
        Asset->Kind = Class.IsEmpty() ? First : Class;
    if (Asset->Kind.IsEmpty())
        return false;
    Asset->SourceBytes = Header.Num() + BodySize;
    Out = Asset;
    return true;
}
} //namespace P4FreshWidgetPlanning

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4FreshWidgetSourcePlanning, "P4MapKit.Kadan.FreshWidgetSourcePlanning",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4FreshWidgetSourcePlanning::RunTest(const FString&)
{
    using namespace P4FreshWidgetPlanning;
    P4_REQUIRE_KADAN_TEST_SEAMS();
    const FString ManifestPath = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_FRESH_SOURCE_SEAL"));
    const FString Expected = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_FRESH_SOURCE_SEAL_SHA256"));
    if (!TestTrue(TEXT("Planning-only selector requires an explicit sealed source manifest"),
            !ManifestPath.IsEmpty() && Expected.Len() == 64))
        return false;
    TSharedPtr<FJsonObject> Seal;
    FString Hash;
    if (!TestTrue(TEXT("Exact source seal is readable"),
            Json(ManifestPath, 24 * 1024 * 1024, Seal, Hash) && P4ExactNativeString(Hash, Expected)))
        return false;
    FString Format, Project;
    if (!TestTrue(TEXT("Source seal names this one isolated native child"),
            Seal->TryGetStringField(TEXT("format"), Format) && Format == TEXT("kadan-a-full-fresh-source-seal-v1") &&
                Seal->TryGetStringField(TEXT("proposed_native_project"), Project) &&
                FPaths::IsSamePath(FPaths::ConvertRelativePathToFull(Project),
                    FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()))))
        return false;
    const TSharedPtr<FJsonObject>* SourceFiles = nullptr;
    const TSharedPtr<FJsonObject>* Measured = nullptr;
    const TSharedPtr<FJsonObject>* Original = nullptr;
    const TSharedPtr<FJsonObject>* Retained = nullptr;
    if (!TestTrue(TEXT("Exact1749 source closure and192 retained plans are described"),
            Seal->TryGetObjectField(TEXT("source_files"), SourceFiles) &&
                Seal->TryGetObjectField(TEXT("measured_1749_packages"), Measured) &&
                Seal->TryGetObjectField(TEXT("original192_sources"), Original) &&
                Seal->TryGetObjectField(TEXT("retained_plans"), Retained) && (*Measured)->Values.Num() == 1749 &&
                (*Original)->Values.Num() == 192 && (*Retained)->Values.Num() == 193))
        return false;
    FString RetainedRoot;
    if (!Seal->TryGetStringField(TEXT("retained_plan_root"), RetainedRoot))
        return false;
    const auto CheckRetained = [&]()
    {
        for (const auto& Pair : (*Retained)->Values)
            if (!ExactFile(FPaths::Combine(RetainedRoot, Pair.Key), Pair.Value->AsObject()))
                return false;
        return true;
    };
    if (!TestTrue(TEXT("All original192 plans plus original index remain byte-exact before planning"), CheckRetained()))
        return false;
    ON_SCOPE_EXIT
    {
        TestTrue(TEXT("Original192 plans remain byte-exact even on planning refusal"), CheckRetained());
    };
    const FString Folder = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("Kadan/AdditionalWidgetPlans"));
    const FString Result = FPaths::ProjectSavedDir() / TEXT("Kadan/additional-widget-plans-result.json");
    if (!TestFalse(TEXT("Never clear or reuse an existing planning output"),
            IFileManager::Get().DirectoryExists(*Folder) || IFileManager::Get().FileExists(*Folder)))
        return false;
    if (!TestFalse(TEXT("Never overwrite an uncertain earlier planning receipt"),
            IFileManager::Get().FileExists(*Result) || IFileManager::Get().DirectoryExists(*Result)))
        return false;
    TMap<FString, TArray<FString>> Paths;
    TMap<FString, FString> Asked;
    TSet<FString> Allowed;
    for (const auto& Pair : (*Measured)->Values)
    {
        const auto Row = Pair.Value->AsObject();
        const TArray<TSharedPtr<FJsonValue>>* Files = nullptr;
        FString SourceKind;
        if (!Row.IsValid() || !Row->TryGetArrayField(TEXT("files"), Files) ||
            !Row->TryGetStringField(TEXT("kind"), SourceKind))
            return false;
        for (const auto& File : *Files)
            Paths.FindOrAdd(Pair.Key).Add(File->AsString());
        if (SourceKind == TEXT("Blueprint") || SourceKind == TEXT("WidgetBlueprint"))
        {
            Asked.Add(Pair.Key, SourceKind);
            Allowed.Add(Pair.Key);
        }
    }
    for (const auto& Pair : (*Original)->Values)
    {
        const auto& Files = Pair.Value->AsArray();
        auto& Bound = Paths.FindOrAdd(Pair.Key);
        for (const auto& File : Files)
            if (!Bound.Contains(File->AsString()))
                Bound.Add(File->AsString());
        Allowed.Add(Pair.Key);
    }
    if (!TestEqual(TEXT("Measured request is exactly375 real additional classes"), Asked.Num(), 375) ||
        !TestEqual(TEXT("Only original192 plus additional375 class closure is admissible"), Allowed.Num(), 567))
        return false;
    TArray<TSharedPtr<FP4KadanSourceAsset>> Sources;
    TArray<FString> Packages;
    int32 Widgets = 0, Blueprints = 0;
    for (const auto& Pair : Paths)
    {
        TSharedPtr<FP4KadanSourceAsset> Source;
        if (!TestTrue(TEXT("Actual immutable source package/root kind is readable: ") + Pair.Key,
                Descriptor(Pair.Key, Pair.Value, *SourceFiles, Source)))
            return false;
        if (const FString* RequestedKind = Asked.Find(Pair.Key))
        {
            if (!TestTrue(TEXT("Actual root export confirms requested kind: ") + Pair.Key,
                    P4ExactNativeString(Source->Kind, *RequestedKind)))
                return false;
            Packages.Add(Pair.Key);
            Widgets += Source->Kind == TEXT("WidgetBlueprint");
            Blueprints += Source->Kind == TEXT("Blueprint");
        }
        Sources.Add(Source);
    }
    if (!TestEqual(TEXT("Actual additional Widget roots"), Widgets, 370) ||
        !TestEqual(TEXT("Actual additional ordinary Blueprint roots"), Blueprints, 5))
        return false;
    Packages.Sort();
    FP4KadanImportSettings Settings;
    Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    Settings.Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    Settings.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    FP4CurrentGameRegistryPtr Reader;
    FP4KadanBlueprintPlans Plans;
    FString Error;
    const bool Planned = P4KadanPlanBlueprints(Sources, Packages, Folder, Plans, Error, Settings, Reader);
    // Deliberately no RunSelectedNative/RunImport/Blueprint declaration. Failure
    // outputs remain evidence and are never deleted or accepted as a plan.
    for (const auto& Refusal : Plans.Refused)
        AddInfo(TEXT("Source planning refused ") + Refusal.Key + TEXT(": ") + Refusal.Value);
    if (!TestTrue(TEXT("Ordinary native source planner completes: ") + Error, Planned) ||
        !TestEqual(TEXT("No source or semantic planning refusal is hidden"), Plans.Refused.Num(), 0) ||
        !TestEqual(TEXT("Every used native module was actually loaded"), Plans.ModulesNotLoaded.Num(), 0))
        return false;
    TSet<FString> Actual;
    for (const FString& Package : Plans.Planned)
    {
        if (!TestTrue(TEXT("No unmeasured Blueprint closure is added: ") + Package, Allowed.Contains(Package)))
            return false;
        Actual.Add(Package);
    }
    for (const auto& Pair : Asked)
        if (!TestTrue(
                TEXT("Every additional class has a complete source plan: ") + Pair.Key, Actual.Contains(Pair.Key)))
            return false;
    TSharedPtr<FJsonObject> Index;
    FString IndexSha, IndexFormat;
    const TArray<TSharedPtr<FJsonValue>>* IndexRows = nullptr;
    if (!TestTrue(TEXT("The actual new index contains exactly the planned class set"),
            Json(Folder / TEXT("index.json"), 4 * 1024 * 1024, Index, IndexSha) &&
                Index->TryGetStringField(TEXT("format"), IndexFormat) && IndexFormat == TEXT("p4-blueprint-plans") &&
                Index->TryGetArrayField(TEXT("order"), IndexRows) && IndexRows->Num() == Actual.Num()))
        return false;
    TSet<FString> Indexed;
    for (const auto& Value : *IndexRows)
    {
        const auto Row = Value->AsObject();
        FString Package, IndexedKind;
        if (!TestTrue(TEXT("Every actual index row has exact source identity and kind"),
                Row.IsValid() && Row->TryGetStringField(TEXT("package"), Package) && Actual.Contains(Package) &&
                    !Indexed.Contains(Package) && Row->TryGetStringField(TEXT("kind"), IndexedKind)))
            return false;
        const auto* Source = Sources.FindByPredicate(
            [&](const TSharedPtr<FP4KadanSourceAsset>& One) { return P4ExactNativeString(One->Package, Package); });
        if (!TestTrue(TEXT("Indexed class kind matches the actual source root: ") + Package,
                Source && P4ExactNativeString((*Source)->Kind, IndexedKind)))
            return false;
        Indexed.Add(Package);
    }
    TSet<FString> ExpectedFiles;
    ExpectedFiles.Add(Folder / TEXT("index.json"));
    for (const FString& Package : Plans.Planned)
    {
        ExpectedFiles.Add(Folder / TEXT("import") / (Package.Mid(1) + TEXT(".json")));
        ExpectedFiles.Add(Folder / TEXT("evidence") / (Package.Mid(1) + TEXT(".json")));
    }
    TArray<FString> Files;
    IFileManager::Get().FindFilesRecursive(Files, *Folder, TEXT("*"), true, false);
    if (!TestEqual(
            TEXT("Exact import/evidence pair and one index per actual class"), Files.Num(), Actual.Num() * 2 + 1))
        return false;
    int64 TotalBytes = 0;
    auto Receipt = MakeShared<FJsonObject>();
    auto Facts = MakeShared<FJsonObject>();
    for (const FString& File : Files)
    {
        if (!TestTrue(TEXT("No unrelated output file is accepted: ") + File, ExpectedFiles.Contains(File)))
            return false;
        int64 Size = 0;
        FString Digest;
        if (!TestTrue(TEXT("Bounded exact actual output is hashed: ") + File,
                P4HashFile(File, 64 * 1024 * 1024, Size, Digest)))
            return false;
        TotalBytes += Size;
        if (!TestTrue(TEXT("One planning output stays below256MiB"), TotalBytes <= 256 * 1024 * 1024))
            return false;
    }
    for (const FString& Package : Plans.Planned)
    {
        const FString Relative = Package.Mid(1) + TEXT(".json");
        TSharedPtr<FJsonObject> Import, Evidence;
        FString ImportSha, EvidenceSha;
        if (!TestTrue(TEXT("Actual import and evidence documents remain readable: ") + Package,
                Json(Folder / TEXT("import") / Relative, 64 * 1024 * 1024, Import, ImportSha) &&
                    Json(Folder / TEXT("evidence") / Relative, 64 * 1024 * 1024, Evidence, EvidenceSha)))
            return false;
        FString Named, EvidencePackage, ImportedKind;
        const auto* Source = Sources.FindByPredicate(
            [&](const TSharedPtr<FP4KadanSourceAsset>& One) { return P4ExactNativeString(One->Package, Package); });
        const TSharedPtr<FJsonObject>* Defaults = nullptr;
        const TSharedPtr<FJsonObject>* Top = nullptr;
        if (!TestTrue(TEXT("Actual source class/defaults/graph documents retain exact identity: ") + Package,
                Import->TryGetStringField(TEXT("package"), Named) && P4ExactNativeString(Named, Package) &&
                    Evidence->TryGetStringField(TEXT("package"), EvidencePackage) &&
                    P4ExactNativeString(EvidencePackage, Package) &&
                    Import->TryGetStringField(TEXT("kind"), ImportedKind) && Source &&
                    P4ExactNativeString((*Source)->Kind, ImportedKind) &&
                    Import->TryGetObjectField(TEXT("class_defaults"), Defaults) &&
                    Evidence->TryGetObjectField(TEXT("top"), Top) && ArrayCount(*Top, TEXT("functions")) >= 0 &&
                    ArrayCount(*Defaults, TEXT("values")) >= 0 && ArrayCount(Import, TEXT("variables")) >= 0 &&
                    (ArrayCount(Import, TEXT("graphs")) >= 0 ||
                        (!Import->HasField(TEXT("graphs")) && ArrayCount(*Top, TEXT("functions")) == 0))))
            return false;
        auto Fact = MakeShared<FJsonObject>();
        Fact->SetStringField(TEXT("kind"), ImportedKind);
        Fact->SetStringField(TEXT("import_sha256"), ImportSha);
        Fact->SetStringField(TEXT("evidence_sha256"), EvidenceSha);
        Fact->SetNumberField(TEXT("class_default_values"), ArrayCount(*Defaults, TEXT("values")));
        Fact->SetNumberField(TEXT("variables"), ArrayCount(Import, TEXT("variables")));
        Fact->SetNumberField(TEXT("graphs"), ArrayCount(Import, TEXT("graphs")));
        Fact->SetNumberField(TEXT("source_functions"), ArrayCount(*Top, TEXT("functions")));
        if (ImportedKind == TEXT("WidgetBlueprint"))
        {
            const TSharedPtr<FJsonObject>* Widget = nullptr;
            if (!TestTrue(TEXT("Full source Widget tree/animations/events fields exist: ") + Package,
                    Import->TryGetObjectField(TEXT("widget"), Widget) && ArrayCount(*Widget, TEXT("subobjects")) >= 0 &&
                        ArrayCount(*Widget, TEXT("animations")) >= 0 && (*Widget)->HasField(TEXT("events"))))
                return false;
            Fact->SetNumberField(TEXT("widget_subobjects"), ArrayCount(*Widget, TEXT("subobjects")));
            Fact->SetNumberField(TEXT("widget_animations"), ArrayCount(*Widget, TEXT("animations")));
        }
        Facts->SetObjectField(Package, Fact);
    }
    if (!TestTrue(TEXT("Original192 plans and source seal remain byte-exact after the complete planning batch"),
            CheckRetained() && P4HashFile(ManifestPath, 24 * 1024 * 1024, TotalBytes, Hash) &&
                P4ExactNativeString(Hash, Expected)))
        return false;
    Receipt->SetStringField(TEXT("format"), TEXT("kadan-a-additional-widget-source-plans-v1"));
    Receipt->SetStringField(TEXT("source_seal_sha256"), Expected);
    Receipt->SetBoolField(TEXT("native_asset_imported"), false);
    Receipt->SetStringField(TEXT("index_sha256"), IndexSha);
    Receipt->SetNumberField(TEXT("requested"), 375);
    Receipt->SetNumberField(TEXT("planned"), Actual.Num());
    Receipt->SetNumberField(TEXT("widgets"), Widgets);
    Receipt->SetNumberField(TEXT("ordinary_blueprints"), Blueprints);
    Receipt->SetNumberField(TEXT("original_identity_packages"), Plans.OriginalIdentityPackages);
    Receipt->SetObjectField(TEXT("facts"), Facts);
    FString Text;
    FJsonSerializer::Serialize(Receipt, TJsonWriterFactory<TCHAR>::Create(&Text));
    return TestTrue(TEXT("Planning-only receipt is retained separately from every import/evidence document"),
        FFileHelper::SaveStringToFile(Text, *Result, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
            &IFileManager::Get(), FILEWRITE_NoReplaceExisting));
}
#endif

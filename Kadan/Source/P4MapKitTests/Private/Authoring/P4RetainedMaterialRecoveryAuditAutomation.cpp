#if WITH_DEV_AUTOMATION_TESTS
#include "P4KadanImport.h"
#include "P4KadanImportSources.h"
#include "P4RetainedMaterialFixture.h"
#include "P4KadanSourceAcquire.h"
#include "P4MaterialGraph.h"
#include "P4MaterialCandidateSourceProof.h"
#include "P4MaterialRecovery.h"
#include "P4OriginalRecord.h"
#include "P4NativeHash.h"
#include "AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Materials/Material.h"
#include "RHI.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/ObjectWriter.h"
#include "P4MapKitCore/material_masters.h"
#include "P4MapKitCore/custom_material_hash.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

namespace P4RetainedMaterialCandidateProof
{
FString Hash(const TArray<uint8>& Bytes)
{
    const std::vector<uint8_t> Raw(Bytes.GetData(), Bytes.GetData() + Bytes.Num());
    return UTF8_TO_TCHAR(P4MapKit::CustomMaterial::Hex(P4MapKit::CustomMaterial::Sha256(Raw)).c_str());
}
bool Files(const FString& Package, TMap<FString, FString>& Out)
{
    const FString Stem = FPackageName::LongPackageNameToFilename(Package);
    for (const TCHAR* Extension : {TEXT(".uasset"), TEXT(".uexp"), TEXT(".ubulk"), TEXT(".uptnl")})
    {
        const FString Path = Stem + Extension;
        if (!IFileManager::Get().FileExists(*Path))
        {
            Out.Add(Path, TEXT("absent"));
            continue;
        }
        TArray<uint8> Bytes;
        if (IFileManager::Get().FileSize(*Path) > 64 * 1024 * 1024 || !FFileHelper::LoadFileToArray(Bytes, *Path))
            return false;
        Out.Add(Path,
            Hash(Bytes) + TEXT("|") + IFileManager::Get().GetTimeStamp(*Path).ToIso8601() +
                (IFileManager::Get().IsReadOnly(*Path) ? TEXT("|readonly") : TEXT("|writable")));
    }
    return true;
}
bool SourceFiles(const TArray<FP4KadanSourceBinding>& Bindings, TMap<FString, FString>& Out)
{
    for (const auto& Binding : Bindings)
        for (const FString& Path : {Binding.HeaderPath, Binding.BodyPath})
        {
            TArray<uint8> Bytes;
            if (IFileManager::Get().FileSize(*Path) <= 0 || IFileManager::Get().FileSize(*Path) > 64 * 1024 * 1024 ||
                !FFileHelper::LoadFileToArray(Bytes, *Path))
                return false;
            Out.Add(Path,
                Hash(Bytes) + TEXT("|") + IFileManager::Get().GetTimeStamp(*Path).ToIso8601() +
                    (IFileManager::Get().IsReadOnly(*Path) ? TEXT("|readonly") : TEXT("|writable")));
        }
    return true;
}
bool ArchiveFile(const FString& Path, FString& Out)
{
    int64 Size = 0;
    FString Digest;
    if (!P4HashFile(Path, int64(1) << 31, Size, Digest) || Size <= 0)
        return false;
    Out = Digest + TEXT("|") + IFileManager::Get().GetTimeStamp(*Path).ToIso8601() +
        (IFileManager::Get().IsReadOnly(*Path) ? TEXT("|readonly") : TEXT("|writable"));
    return true;
}
TMap<FString, TArray<uint8>> Native(UMaterial* Material)
{
    TArray<UObject*> Objects;
    GetObjectsWithOuter(Material, Objects, true);
    Objects.Add(Material);
    TMap<FString, TArray<uint8>> Out;
    for (UObject* Object : Objects)
    {
        TArray<uint8> Bytes;
        FObjectWriter Writer(Object, Bytes, false, false, false, PPF_DuplicateVerbatim);
        Out.Add(Object->GetPathName() + TEXT("|class=") + Object->GetClass()->GetPathName() +
                FString::Printf(TEXT("|flags=%08x"), uint32(Object->GetFlags())),
            MoveTemp(Bytes));
    }
    return Out;
}
TMap<FName, FString> Metadata(UObject* Object)
{
    if (const auto* Map = UMetaData::GetMapForObject(Object))
        return *Map;
    return {};
}
TMap<FString, FString> Properties(UMaterial* Material)
{
    TArray<UObject*> Objects;
    GetObjectsWithOuter(Material, Objects, true);
    Objects.Add(Material);
    TMap<FString, FString> Out;
    for (UObject* Object : Objects)
    {
        Out.Add(Object->GetPathName() + TEXT("|native-identity"),
            Object->GetClass()->GetPathName() + FString::Printf(TEXT("|flags=%08x"), uint32(Object->GetFlags())));
        for (TFieldIterator<FProperty> Field(Object->GetClass()); Field; ++Field)
            for (int32 Index = 0; Index < Field->ArrayDim; ++Index)
            {
                FString Text;
                Field->ExportTextItem(
                    Text, Field->ContainerPtrToValuePtr<void>(Object, Index), nullptr, Object, PPF_Delimited);
                Out.Add(Object->GetPathName() + TEXT("|") + Field->GetOwnerStruct()->GetPathName() + TEXT(".") +
                        Field->GetName() +
                        FString::Printf(TEXT("[%d]|flags=%016llx"), Index, Field->GetPropertyFlags()),
                    MoveTemp(Text));
            }
    }
    return Out;
}
TSharedRef<FJsonObject> PropertyDifferences(const TMap<FString, FString>& Before, const TMap<FString, FString>& After)
{
    auto Result = MakeShared<FJsonObject>();
    TSet<FString> Keys;
    for (const auto& Pair : Before)
        Keys.Add(Pair.Key);
    for (const auto& Pair : After)
        Keys.Add(Pair.Key);
    int32 Count = 0;
    for (const FString& Key : Keys)
    {
        const FString* A = Before.Find(Key);
        const FString* B = After.Find(Key);
        if (A && B && *A == *B)
            continue;
        ++Count;
        if (Count > 128)
            continue;
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("before"), A ? A->Left(2048) : TEXT("<absent>"));
        Row->SetStringField(TEXT("after"), B ? B->Left(2048) : TEXT("<absent>"));
        Result->SetObjectField(Key, Row);
    }
    Result->SetNumberField(TEXT("difference_count"), Count);
    return Result;
}
TArray<TArray<FString>> Families()
{
    return {{TEXT("/Game/Carrot4/MasterMaterial/chr/M_chr_Shadow_Master"),
                TEXT("/Game/Carrot4/MasterMaterial/chr/MI_chr_Shadow"),
                TEXT("/Game/Carrot4/MasterMaterial/chr/MI_chr_Shadow_Dissolve"),
                TEXT("/Game/Carrot4/Characters/Teki/DiscoDamagumo/MI_chr_DiscoDamagumo_00_Shadow"),
                TEXT("/Game/Carrot4/Characters/Teki/HageDamagumo/MI_chr_HageDamagumo_00_Shadow")},
        {TEXT("/Game/Carrot4/MasterMaterial/Unique/Silhouette/M_Silhouette_00_Master"),
            TEXT("/Game/Carrot4/Characters/Teki/Dodoro/MI_chr_Dodoro_00_Silhouette"),
            TEXT("/Game/Carrot4/MasterMaterial/Unique/Silhouette/MI_Silhouette_Object_00"),
            TEXT("/Game/Carrot4/MasterMaterial/Unique/Silhouette/MI_Silhouette_Player_00"),
            TEXT("/Game/Carrot4/MasterMaterial/Unique/Silhouette/MI_Silhouette_Teki_00")}};
}
bool FamilyView(const FString& Archive, TArray<TSharedPtr<FP4KadanSourceAsset>>& Sources,
    TArray<FP4KadanSourceBinding>& Bindings, FString& ManifestPath, FString& ManifestHash, FString& ArchiveState,
    FString& Error)
{
    FString View = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MATERIAL_FAMILY_SOURCES"));
    if (View.IsEmpty())
    {
        Error = TEXT("The explicit read-only material family source view is required.");
        return false;
    }
    View = FPaths::ConvertRelativePathToFull(View);
    FPaths::NormalizeDirectoryName(View);
    FString Owned = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../../material-family-source-view"));
    FPaths::CollapseRelativeDirectories(Owned);
    FPaths::NormalizeDirectoryName(Owned);
    if (View != Owned)
    {
        Error = TEXT("Only the exact owned material family source view is admitted.");
        return false;
    }
    ManifestPath = View / TEXT("manifest.json");
    TArray<uint8> ManifestBytes;
    FString ManifestText;
    TSharedPtr<FJsonObject> Manifest;
    if (IFileManager::Get().FileSize(*ManifestPath) <= 0 || IFileManager::Get().FileSize(*ManifestPath) > 65536 ||
        !FFileHelper::LoadFileToArray(ManifestBytes, *ManifestPath) ||
        !FFileHelper::LoadFileToString(ManifestText, *ManifestPath) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ManifestText), Manifest) || !Manifest.IsValid() ||
        Manifest->GetStringField(TEXT("format")) != TEXT("p4-material-family-source-view-v2"))
    {
        Error = TEXT("The bounded exact-family source manifest is invalid.");
        return false;
    }
    ManifestHash = Hash(ManifestBytes);
    if (!ArchiveFile(Archive, ArchiveState) ||
        !ArchiveState.StartsWith(Manifest->GetStringField(TEXT("archive_sha256")) + TEXT("|")))
    {
        Error = TEXT("The qualified shader archive differs from the explicit source view binding.");
        return false;
    }
    TArray<FString> Problems;
    if (!P4KadanScanGame(View / TEXT("Carrot4/Content"), Sources, Problems) || Problems.Num() || Sources.Num() != 12)
    {
        Error = TEXT("The exact ten-family/two-sampler source view must scan without ambiguity.");
        return false;
    }
    const TSharedPtr<FJsonObject>* Packages = nullptr;
    if (!Manifest->TryGetObjectField(TEXT("packages"), Packages) || (*Packages)->Values.Num() != 12)
    {
        Error = TEXT("The source view manifest must bind exactly ten family and two Texture2D packages.");
        return false;
    }
    FP4KadanImportSourceScope Scope(Sources);
    const auto Bind = [&](const FString& Package, const FString& Kind, const FString& Master)
    {
        const TSharedPtr<FJsonObject>* Row = nullptr;
        const auto* Source = Sources.FindByPredicate(
            [&](const TSharedPtr<FP4KadanSourceAsset>& Asset) { return Asset->Package == Package; });
        FP4KadanSourceBinding Binding;
        if (!Source || (*Source)->Kind != Kind || !(*Packages)->TryGetObjectField(Package, Row) ||
            !P4KadanReadBoundSource(Package, Binding, Error) ||
            !FPaths::IsSamePath(Binding.HeaderPath, View / (*Row)->GetStringField(TEXT("header"))) ||
            !FPaths::IsSamePath(Binding.BodyPath, View / (*Row)->GetStringField(TEXT("body"))) ||
            Hash(Binding.Header) != (*Row)->GetStringField(TEXT("header_sha256")) ||
            Hash(Binding.Body) != (*Row)->GetStringField(TEXT("body_sha256")) ||
            (*Row)->GetStringField(TEXT("class")) != (*Source)->Kind ||
            (*Row)->GetStringField(TEXT("master")) != Master)
        {
            if (Error.IsEmpty())
                Error = TEXT("A source-view package/class/path/hash binding differs: ") + Package;
            return false;
        }
        Bindings.Add(MoveTemp(Binding));
        return true;
    };
    for (const auto& Family : Families())
        for (int32 Index = 0; Index < Family.Num(); ++Index)
            if (!Bind(Family[Index], Index == 0 ? TEXT("Material") : TEXT("MaterialInstanceConstant"), Family[0]))
                return false;
    for (const TCHAR* Texture :
        {TEXT("/Game/Carrot4/MasterMaterial/Texture/T_Base_M"), TEXT("/Game/Carrot4/MasterMaterial/Texture/T_Base_D")})
        if (!Bind(Texture, TEXT("Texture2D"), FString()))
            return false;
    return true;
}
// Root pin fingerprints from the independently retained historical canonical
// 11/52-node graphs, not from this candidate or its just-recovered recipe.
bool HistoricalPins(const FString& Text, int32 Index, FString& Error)
{
    namespace M = P4MapKit::Material;
    try
    {
        FTCHARToUTF8 Utf8(*Text);
        const auto Graph = M::Json::Parse(std::string(Utf8.Get(), Utf8.Length()));
        M::MaterialMasterFingerprint Fingerprint{Graph, nullptr, {}};
        const std::map<std::string, std::string> Shadow = {
            {"AmbientOcclusion", "bb295a4b6ecbb934dca9ef1e15a7f60a69523e8fadec0ebe7b047b028c5a3f24"},
            {"BaseColor", "56e69f6f17a4820c63bdf5a44b372cb2bd0ce3258a445084d336704694451b6a"},
            {"EmissiveColor", "56e69f6f17a4820c63bdf5a44b372cb2bd0ce3258a445084d336704694451b6a"},
            {"Metallic", "9df2a32c8a0828cf97060cae2c099ff03e3cca915c8fca15471c0b7d2cfe7f49"},
            {"Normal", "8ff99b61fc8b7dd4d6ea132c20c1c94fd6225fb6a931c7d8e64b196444b46c57"},
            {"OpacityMask", "8ef9fd306e4b592141283933083ae96139af9829d58045fa3434338a2f9ab3c9"},
            {"Roughness", "bb295a4b6ecbb934dca9ef1e15a7f60a69523e8fadec0ebe7b047b028c5a3f24"},
            {"Specular", "299889da39f5bddbeb4bdbd7d3195351ecf53548aaa2481e5c12d3f1c6b80411"}};
        const std::map<std::string, std::string> Silhouette = {
            {"EmissiveColor", "15fb78c16a6db2cc08c356f1d6346db59b196976628fed883ff53c22534a37ad"},
            {"Opacity", "095ee0a12afd3c126694257ad641e9c48e391f5f4f445b3463d9da6a550e4f1f"}};
        const auto& Pins = Index == 0 ? Shadow : Silhouette;
        if (Graph.At("outputs").object.size() != Pins.size())
        {
            Error = TEXT("historical-output-inventory-differs");
            return false;
        }
        for (const auto& Pin : Pins)
            if (!Graph.At("outputs").Has(Pin.first) ||
                Fingerprint.Of(Graph.At("outputs").At(Pin.first)) != Pin.second + "[\"\"]")
            {
                Error = TEXT("historical-pin-differs:") + FString(UTF8_TO_TCHAR(Pin.first.c_str()));
                return false;
            }
        if (Graph.At("domain").Text() != "MD_Surface" || Graph.At("tangent_space_normal").kind != M::Json::Boolean ||
            !Graph.At("tangent_space_normal").boolean || Graph.At("two_sided").kind != M::Json::Boolean ||
            Graph.At("shading_model").Text() != (Index == 0 ? "MSM_DefaultLit" : "MSM_Unlit") ||
            Graph.At("blend_mode").Text() != (Index == 0 ? "BLEND_Opaque" : "BLEND_Translucent") ||
            Graph.At("two_sided").boolean != (Index != 0) || float(Graph.At("clip").Numeric()) != float(.3333))
        {
            Error = TEXT("historical-settings-differ");
            return false;
        }
        if (Index == 0 &&
            (!Graph.Has("render_settings") || !Graph.At("render_settings").Has("bFullyRough") ||
                Graph.At("render_settings").At("bFullyRough").kind != M::Json::Boolean ||
                !Graph.At("render_settings").At("bFullyRough").boolean))
        {
            Error = TEXT("historical-fully-rough-setting-differs");
            return false;
        }
        return true;
    }
    catch (const std::exception& Failure)
    {
        Error = UTF8_TO_TCHAR(Failure.what());
        return false;
    }
}
} //namespace P4RetainedMaterialCandidateProof

// Diagnostic foundation for a bounded repair, not permission to overwrite an
// existing recovered graph. Current-read source hashes are not historic proof.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RetainedMaterialRecoveryAudit, "P4MapKit.Kadan.RetainedMaterialRecoveryCandidates",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RetainedMaterialRecoveryAudit::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETAINED_MATERIAL_CANDIDATES")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. ")
                TEXT("Opt-in owned-fixture candidate proof; no original material is replaced or saved."));
        return true;
    }
    FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    FPaths::NormalizeDirectoryName(Project);
    if (!TestTrue(TEXT("Only the retained native-development fixture is selected"),
            Project.EndsWith(TEXT("/private/native-development/project"), ESearchCase::IgnoreCase)))
        return false;
    FP4KadanImportSettings Settings;
    Settings.GameFolder = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_CAVE_GAME"));
    Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    Settings.Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    Settings.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    Settings.ConvertedCache = FPaths::ProjectSavedDir() / TEXT("Kadan/ConvertedOriginals");
    Settings.ShaderArchive = Settings.ConvertedCache /
        TEXT("Carrot4/Content/ShaderArchive-Carrot4-GLSL_SWITCH.ushaderbytecode");
    // The archive must already have been acquired and proved by the fresh test.
    // This audit cannot hide another acquisition or substitute another library.
    if (!TestTrue(TEXT("The already-qualified owned shader library is present"),
            IFileManager::Get().FileExists(*Settings.ShaderArchive)))
        return false;
    const TArray<FString> Packages = {TEXT("/Game/Carrot4/MasterMaterial/chr/M_chr_Shadow_Master"),
        TEXT("/Game/Carrot4/MasterMaterial/Unique/Silhouette/M_Silhouette_00_Master")};
    TArray<TSharedPtr<FP4KadanSourceAsset>> Sources;
    TArray<FP4KadanSourceBinding> FamilyBindings;
    FString ManifestPath, ManifestHash, ArchiveState, SourceError;
    if (!TestTrue(TEXT("Every retained family source is bound without planning, acquisition or refresh"),
            P4RetainedMaterialCandidateProof::FamilyView(Settings.ShaderArchive, Sources, FamilyBindings, ManifestPath,
                ManifestHash, ArchiveState, SourceError)))
    {
        AddError(SourceError);
        return false;
    }
    TMap<FString, FString> SourceFilesBefore;
    if (!TestTrue(TEXT("All twenty-four family and sampler source files are snapshotted"),
            P4RetainedMaterialCandidateProof::SourceFiles(FamilyBindings, SourceFilesBefore)))
        return false;
    FP4KadanImportSourceScope SourceScope(Sources);
    FP4MaterialRecovery Recovery(Settings.GameFolder, Sources, Settings.ShaderArchive);
    const FString CandidatePackage = TEXT("/Game/P4MapKitMaterialRecoveryCandidate/OnlyCandidate");
    const FString CandidateObject = CandidatePackage + TEXT(".OnlyCandidate");
    if (!TestTrue(TEXT("The single unsaved diagnostic namespace is unoccupied"),
            !FPackageName::DoesPackageExist(CandidatePackage) && !FindObject<UObject>(nullptr, *CandidateObject)))
        return false;
    TArray<TSharedPtr<FJsonValue>> Rows;
    const bool CompileSM5 = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MATERIAL_CANDIDATE_COMPILE")) ==
        TEXT("1");
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit/MaterialRecoveryCandidates");
    IFileManager::Get().MakeDirectory(*Folder, true);
    ON_SCOPE_EXIT
    {
        bool SourcesPreserved = true;
        FString Why;
        for (const auto& Binding : FamilyBindings)
            if (!P4KadanSourceBindingCurrent(Binding, Why))
                SourcesPreserved = false;
        TArray<uint8> CurrentManifest;
        FString CurrentArchive;
        TMap<FString, FString> SourceFilesAfter;
        SourcesPreserved = FFileHelper::LoadFileToArray(CurrentManifest, *ManifestPath) &&
            P4RetainedMaterialCandidateProof::Hash(CurrentManifest) == ManifestHash &&
            P4RetainedMaterialCandidateProof::SourceFiles(FamilyBindings, SourceFilesAfter) &&
            SourceFilesAfter.OrderIndependentCompareEqual(SourceFilesBefore) &&
            P4RetainedMaterialCandidateProof::ArchiveFile(Settings.ShaderArchive, CurrentArchive) &&
            CurrentArchive == ArchiveState && SourcesPreserved;
        TestTrue(TEXT("All twelve read-only source bindings and their manifest remain unchanged"), SourcesPreserved);
        auto Report = MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("original_asset_write_requested"), false);
        Report->SetBoolField(TEXT("renderer_proven"), false);
        Report->SetBoolField(TEXT("sm5_compile_requested"), CompileSM5);
        Report->SetBoolField(TEXT("current_read_hashes_are_historic_proof"), false);
        Report->SetStringField(TEXT("source_manifest"), ManifestPath);
        Report->SetStringField(TEXT("source_manifest_sha256"), ManifestHash);
        Report->SetStringField(TEXT("shader_archive_before"), ArchiveState);
        Report->SetNumberField(TEXT("source_package_count"), FamilyBindings.Num());
        Report->SetBoolField(TEXT("all_family_sources_preserved"), SourcesPreserved);
        Report->SetNumberField(TEXT("family_package_count"), 10);
        Report->SetNumberField(TEXT("sampler_source_package_count"), 2);
        auto SourceFileReport = MakeShared<FJsonObject>();
        for (const auto& File : SourceFilesBefore)
            SourceFileReport->SetStringField(File.Key, File.Value);
        Report->SetObjectField(TEXT("source_files_before"), SourceFileReport);
        Report->SetBoolField(TEXT("test_passed"), !HasAnyErrors());
        Report->SetArrayField(TEXT("materials"), Rows);
        FString Text;
        FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Text));
        TestTrue(TEXT("Bounded candidate evidence is retained"),
            FFileHelper::SaveStringToFile(Text, *(Folder / TEXT("retained-material-candidates.json")),
                FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    };
    for (int32 Index = 0; Index < Packages.Num(); ++Index)
    {
        const FString& Package = Packages[Index];
        const FString Name = FPackageName::GetLongPackageAssetName(Package);
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("package"), Package);
        Rows.Add(MakeShared<FJsonValueObject>(Row));
        UMaterial* Original = LoadObject<UMaterial>(nullptr, *(Package + TEXT(".") + Name));
        FP4OriginalRecord Record;
        FString Error;
        if (!TestNotNull(TEXT("The exact retained material exists"), Original))
            continue;
        const bool HasRecord = P4ReadOriginalRecord(Original, Record, Error);
        if (!TestTrue(TEXT("A malformed existing record is refused"), Error.IsEmpty()))
        {
            AddError(Error);
            continue;
        }
        const bool Dirty = Original->GetOutermost()->IsDirty();
        const FString Marker = Original->GetOutermost()->GetMetaData()->GetValue(Original, TEXT("P4OriginalRecord"));
        Row->SetBoolField(TEXT("repair_authority"), false);
        Row->SetBoolField(TEXT("has_original_record"), HasRecord);
        Row->SetNumberField(TEXT("existing_expression_count"), Original->Expressions.Num());
        Row->SetStringField(TEXT("existing_kind"), Record.Kind);
        Row->SetStringField(TEXT("existing_source_digest_kind"), Record.SourceDigestKind);
        if (!TestTrue(TEXT("Only the evidenced clean graphless material is audited"),
                !Dirty && Original->Expressions.Num() == 0))
            continue;
        if (Index == 0 &&
            !TestTrue(TEXT("Shadow retains its source-verified protected original record"),
                HasRecord && Record.ReadOnly && Record.ReferenceOnly && FName(*Record.Package) == FName(*Package)))
            continue;
        if (Index == 1 &&
            !TestTrue(
                TEXT("The unmarked legacy Silhouette remains unowned by this repair"), !HasRecord && Marker.IsEmpty()))
            continue;
        TMap<FString, FString> FilesBefore;
        if (!TestTrue(TEXT("The saved bundle is readable without mutation"),
                P4RetainedMaterialCandidateProof::Files(Package, FilesBefore)))
            continue;
        if (Index == 1)
        {
            const FString* Saved = FilesBefore.Find(FPackageName::LongPackageNameToFilename(Package, TEXT(".uasset")));
            if (!TestTrue(TEXT("Only the exact observed legacy editor file is inspected"),
                    Saved &&
                        Saved->StartsWith(TEXT("3633e3014b3a8f01e1c8ba6d95001409d7220c79057765caf5397a906598df13|"))))
                continue;
            Row->SetStringField(
                TEXT("classification"), TEXT("legacy-unmarked-editor-material; not ReferenceOnly or proven stand-in"));
        }
        const auto NativeBefore = P4RetainedMaterialCandidateProof::Native(Original);
        const auto MetadataBefore = P4RetainedMaterialCandidateProof::Metadata(Original);
        const auto PropertiesBefore = P4RetainedMaterialCandidateProof::Properties(Original);
        FString NativeGraphError;
        const FString OriginalGraph = P4MaterialGraphJson(Original, NativeGraphError);
        if (!TestTrue(TEXT("The complete current native graph can be snapshotted"),
                !OriginalGraph.IsEmpty() && NativeGraphError.IsEmpty()))
            continue;
        FP4KadanSourceBinding Binding;
        if (!TestTrue(TEXT("The selected actual source pair is bound and still current"),
                P4KadanReadBoundSource(Package, Binding, Error) && P4KadanSourceBindingCurrent(Binding, Error)))
        {
            AddError(Error);
            continue;
        }
        Row->SetStringField(TEXT("current_source_sha256"), Binding.SourceDigest);
        Row->SetStringField(TEXT("header"), Binding.HeaderPath);
        Row->SetStringField(TEXT("body"), Binding.BodyPath);
        // Every later refusal, including coverage/topology/semantic failures,
        // still emits the preservation proof. A failed audit is never permission
        // to omit checks merely because no candidate was built.
        ON_SCOPE_EXIT
        {
            TMap<FString, FString> FilesAfter;
            FString AfterError;
            const bool SerializedEqual =
                P4RetainedMaterialCandidateProof::Native(Original).OrderIndependentCompareEqual(NativeBefore);
            const bool MetadataEqual =
                P4RetainedMaterialCandidateProof::Metadata(Original).OrderIndependentCompareEqual(MetadataBefore);
            const bool GraphEqual = P4MaterialGraphJson(Original, AfterError) == OriginalGraph && AfterError.IsEmpty();
            const bool FilesEqual = P4RetainedMaterialCandidateProof::Files(Package, FilesAfter) &&
                FilesAfter.OrderIndependentCompareEqual(FilesBefore);
            Row->SetBoolField(TEXT("native_serialization_class_and_flags_preserved"), SerializedEqual);
            Row->SetBoolField(TEXT("metadata_preserved"), MetadataEqual);
            Row->SetBoolField(TEXT("graph_preserved"), GraphEqual);
            Row->SetBoolField(TEXT("saved_bundle_preserved"), FilesEqual);
            Row->SetObjectField(TEXT("native_property_differences"),
                P4RetainedMaterialCandidateProof::PropertyDifferences(
                    PropertiesBefore, P4RetainedMaterialCandidateProof::Properties(Original)));
            const bool NativePreserved = SerializedEqual && MetadataEqual && GraphEqual && FilesEqual;
            Row->SetBoolField(TEXT("complete_native_metadata_and_saved_bundle_preserved"), NativePreserved);
            const bool Preserved = NativePreserved && Original->Expressions.Num() == 0 &&
                Original->GetOutermost()->IsDirty() == Dirty &&
                FString(Original->GetOutermost()->GetMetaData()->GetValue(Original, TEXT("P4OriginalRecord"))) ==
                    Marker &&
                P4KadanSourceBindingCurrent(Binding, AfterError) &&
                (Index != 0 || P4DiagnoseOriginalAsset(Original).Comparison == EP4OriginalComparison::Equal);
            Row->SetBoolField(TEXT("original_and_source_preservation_proved"), Preserved);
            TestTrue(TEXT("The original and authoritative source stayed unchanged on every candidate exit"), Preserved);
            if (!AfterError.IsEmpty())
                AddError(AfterError);
        };
        // Graphless generic originals carry H+B digests. A different record kind
        // needs a different historic-content proof; do not infer one here.
        if (Index == 0)
        {
            if (!TestTrue(TEXT("The old graphless source record pins this exact package payload"),
                    Record.Kind == TEXT("object") && Record.SourceDigestKind == TEXT("header-body-sha256-v1") &&
                        Record.SourceDigest == Binding.SourceDigest))
                continue;
            const auto Diagnostic = P4DiagnoseOriginalAsset(Original);
            Row->SetBoolField(TEXT("precheck_diagnostic_native_preserved"),
                P4RetainedMaterialCandidateProof::Native(Original).OrderIndependentCompareEqual(NativeBefore));
            Row->SetObjectField(TEXT("precheck_diagnostic_property_differences"),
                P4RetainedMaterialCandidateProof::PropertyDifferences(
                    PropertiesBefore, P4RetainedMaterialCandidateProof::Properties(Original)));
            if (!TestTrue(TEXT("The original's actual diagnostic cook still equals its immutable baseline"),
                    Diagnostic.Comparison == EP4OriginalComparison::Equal))
                continue;
        }
        Row->SetBoolField(TEXT("repair_authority"), Index == 0);
        const auto Recovered = Recovery.Recover(Package);
        Row->SetBoolField(TEXT("recipe_recovered"), Recovered.bOk);
        Row->SetStringField(TEXT("refusal"), Recovered.Reason);
        if (!TestTrue(TEXT("The actual current source and shader archive recover the master"),
                Recovered.bOk && Recovered.Kind == TEXT("Material")))
            continue;
        const auto Family = P4RetainedMaterialCandidateProof::Families()[Index];
        TArray<TSharedPtr<FJsonValue>> Members, Owners;
        for (const FString& Member : Recovered.FamilyMembers)
            Members.Add(MakeShared<FJsonValueString>(Member));
        for (const FString& Owner : Recovered.FamilyOwners)
            Owners.Add(MakeShared<FJsonValueString>(Owner));
        Row->SetArrayField(TEXT("source_family_members"), Members);
        Row->SetArrayField(TEXT("proved_shader_owners"), Owners);
        bool Covered = Recovered.FamilyMembers.Num() == 5 && Recovered.FamilyUsers.Num() == 5 &&
            Recovered.FamilyOwners.Num() == (Index == 0 ? 4 : 3);
        for (const FString& Member : Family)
            Covered = Recovered.FamilyMembers.Contains(Member) && Recovered.FamilyUsers.Contains(Member) && Covered;
        const TArray<FString> ExpectedOwners = Index == 0 ? TArray<FString>{Family[0], Family[2], Family[3], Family[4]}
                                                          : TArray<FString>{Family[0], Family[1], Family[2]};
        for (const FString& Owner : ExpectedOwners)
            Covered = Recovered.FamilyOwners.Contains(Owner) && Covered;
        if (!TestTrue(
                TEXT("All exact original family members and shader owners are covered before native construction"),
                Covered))
            continue;
        TSharedPtr<FJsonObject> Recipe;
        if (!TestTrue(TEXT("The recovered source recipe is an object"),
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Recovered.Source), Recipe) &&
                    Recipe.IsValid()))
            continue;
        Row->SetObjectField(TEXT("source_recipe"), Recipe);
        const TSharedPtr<FJsonObject>* RecipeNodes = nullptr;
        if (!TestTrue(TEXT("Source recipe has the independently retained historical topology size"),
                Recipe->TryGetObjectField(TEXT("nodes"), RecipeNodes) &&
                    (*RecipeNodes)->Values.Num() == (Index == 0 ? 11 : 52)))
            continue;
        const bool Historical = P4RetainedMaterialCandidateProof::HistoricalPins(Recovered.Source, Index, Error);
        Row->SetBoolField(TEXT("historical_source_pin_and_settings_equal"), Historical);
        if (!TestTrue(TEXT("Every source pin and setting matches the independent historical contract"), Historical))
        {
            AddError(Error);
            continue;
        }
        TStrongObjectPtr<UMaterial> Candidate(P4BuildMaterialFromGraph(Recovered.Source, CandidatePackage, Error));
        if (!TestTrue(TEXT("The source recipe constructs a real unsaved native graph"), Candidate.IsValid()))
        {
            AddError(Error);
            continue;
        }
        ON_SCOPE_EXIT
        {
            // Retire only the one candidate created above, never an original.
            const bool Owned = Candidate.IsValid() && Candidate->GetPathName() == CandidateObject &&
                Candidate->GetOutermost()->GetName() == CandidatePackage;
            bool Cleaned = false;
            if (Owned)
            {
                FAssetRegistryModule::AssetDeleted(Candidate.Get());
                Candidate->ClearFlags(RF_Public | RF_Standalone);
                if (Candidate->Rename(nullptr, GetTransientPackage(),
                        REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders))
                {
                    Candidate->MarkPendingKill();
                    Cleaned = !FindObject<UObject>(nullptr, *CandidateObject) &&
                        !FPackageName::DoesPackageExist(CandidatePackage);
                }
            }
            Row->SetBoolField(TEXT("owned_candidate_cleanup_proved"), Owned && Cleaned);
            TestTrue(TEXT("Only the owned unsaved candidate is retired and its namespace is free"), Owned && Cleaned);
        };
        const FString Native = P4MaterialGraphJson(Candidate.Get(), Error);
        TSharedPtr<FJsonObject> NativeGraph;
        TestTrue(TEXT("Native candidate readback is complete"),
            !Native.IsEmpty() && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Native), NativeGraph) &&
                NativeGraph.IsValid());
        if (NativeGraph.IsValid())
            Row->SetObjectField(TEXT("native_candidate_graph"), NativeGraph);
        Row->SetNumberField(TEXT("candidate_expression_count"), Candidate->Expressions.Num());
        // Native channel adapters can add explicit masks/appends; their complete
        // readback above is retained instead of treating a node count as fidelity.
        TestTrue(TEXT("Native graph contains the recovered expressions"),
            Candidate->Expressions.Num() >= (*RecipeNodes)->Values.Num());
        const bool Semantic = P4FreshMaterialCandidateMatchesSource(Candidate.Get(), Recovered.Source, Error);
        Row->SetBoolField(TEXT("native_pin_property_and_settings_equal"), Semantic);
        Row->SetStringField(TEXT("native_semantic_refusal"), Error);
        TestTrue(TEXT("Actual native properties, connections and settings equal the source contract"), Semantic);
        if (!Error.IsEmpty())
            AddError(Error);
        if (Semantic && CompileSM5)
        {
            TArray<FString> CompileErrors;
            FString CompileError;
            const bool RealSM5 = !GUsingNullRHI && GMaxRHIFeatureLevel == ERHIFeatureLevel::SM5;
            const bool Compiled = RealSM5 && P4WaitForMaterialShaders(Candidate.Get(), CompileErrors, CompileError);
            TArray<TSharedPtr<FJsonValue>> Errors;
            for (const FString& Text : CompileErrors)
                Errors.Add(MakeShared<FJsonValueString>(Text));
            Row->SetBoolField(TEXT("actual_sm5_renderer"), RealSM5);
            Row->SetBoolField(TEXT("actual_sm5_compiled"), Compiled);
            Row->SetArrayField(TEXT("compile_errors"), Errors);
            Row->SetStringField(TEXT("compile_refusal"), CompileError);
            TestTrue(TEXT("The same unsaved candidate has a real SM5 shader map with no compiler errors"), Compiled);
            if (!CompileError.IsEmpty())
                AddError(CompileError);
            TestTrue(TEXT("Shader compilation leaves source-native candidate semantics unchanged"),
                P4FreshMaterialCandidateMatchesSource(Candidate.Get(), Recovered.Source, Error));
            if (!Error.IsEmpty())
                AddError(Error);
        }
        TestFalse(TEXT("A diagnostic candidate must not publish a package file"),
            FPackageName::DoesPackageExist(CandidatePackage));
    }
    return !HasAnyErrors();
}
#endif

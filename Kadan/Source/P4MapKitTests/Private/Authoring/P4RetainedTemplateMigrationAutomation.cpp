// An explicit repair gate for the retained single-editor survey project. This is not an editor
// migration command for arbitrary user assets, and a current-file digest is only a TOCTOU snapshot.
#include "P4BlueprintSourceTemplates.h"
#include "P4BlueprintGraphs.h"
#include "P4BlueprintImport.h"
#include "P4OriginalImportSave.h"
#include "P4OriginalRecord.h"
#include "P4KadanAssetPolicy.h"
#include "P4NativeHash.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
THIRD_PARTY_INCLUDES_START
#include "P4MapKitCore/blueprint_builder.h"
THIRD_PARTY_INCLUDES_END

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
using P4MapKit::Blueprint::J;
struct FTemplateMigrationRow
{
    FString Package, File, FileDigest;
    TStrongObjectPtr<UBlueprint> Blueprint;
    TSharedPtr<FJsonObject> Source;
};
bool VerifiedBytes(
    const TSharedPtr<FJsonObject>& Row, const TCHAR* PathKey, const TCHAR* DigestKey, TArray<uint8>& Bytes)
{
    FString Path, Wanted, Actual;
    return Row->TryGetStringField(PathKey, Path) && Row->TryGetStringField(DigestKey, Wanted) && Wanted.Len() == 64 &&
        IFileManager::Get().FileSize(*Path) >= 0 && IFileManager::Get().FileSize(*Path) <= 64 * 1024 * 1024 &&
        FFileHelper::LoadFileToArray(Bytes, *Path, FILEREAD_Silent) &&
        P4HashSourceBuffers(Bytes.GetData(), Bytes.Num(), nullptr, 0, Actual) && P4ExactNativeString(Wanted, Actual);
}
void WithoutFlagsAndEvidence(J& Value)
{
    if (Value.Kind == J::K::Object)
    {
        for (const char* Key : {"flags", "unread", "template", "template_class", "node_extra", "problem"})
        {
            Value.Fields.erase(Key);
            Value.Order.erase(std::remove(Value.Order.begin(), Value.Order.end(), Key), Value.Order.end());
        }
        for (auto& Pair : Value.Fields)
            WithoutFlagsAndEvidence(Pair.second);
    }
    for (auto& Item : Value.Items)
        WithoutFlagsAndEvidence(Item);
}
} //namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RetainedTemplateMigrationTest,
    "P4MapKit.NativeImport.RepairRetainedTemplateFamilies",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RetainedTemplateMigrationTest::RunTest(const FString&)
{
    const FString ManifestFile = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_TEMPLATE_MIGRATION"));
    if (ManifestFile.IsEmpty())
    {
        AddError(TEXT("Set P4_NATIVE_TEMPLATE_MIGRATION to the retained survey's explicit repair manifest."));
        return false;
    }
    FString ManifestText;
    TSharedPtr<FJsonObject> Manifest;
    double Version = 0;
    FString Format;
    const TArray<TSharedPtr<FJsonValue>>* Inputs = nullptr;
    if (!TestTrue(TEXT("The private repair manifest has its exact format/version and bounded family rows"),
            FFileHelper::LoadFileToString(ManifestText, *ManifestFile) && ManifestText.Len() <= 256 * 1024 &&
                FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(ManifestText), Manifest) &&
                Manifest.IsValid() && Manifest->TryGetStringField(TEXT("format"), Format) &&
                Format == TEXT("p4-retained-template-migration") &&
                Manifest->TryGetNumberField(TEXT("version"), Version) && Version == 1 &&
                Manifest->TryGetArrayField(TEXT("rows"), Inputs) && Inputs->Num() == 2))
        return false;
    const TArray<FString> Family = {
        TEXT("/Game/Carrot4/Placeables/Player/GAvatarBase"), TEXT("/Game/Carrot4/Placeables/Player/GAvatar")};
    TArray<FTemplateMigrationRow> Prepared;
    // Validate every input before modifying either package. Existing graphs, CDO fields and existing
    // component templates are never replayed; only absent source-declared overrides are constructed.
    for (int32 Index = 0; Index < Inputs->Num(); ++Index)
    {
        const auto Row = (*Inputs)[Index]->AsObject();
        FString Package;
        TArray<uint8> Header, Body, CachedPlan;
        if (!TestTrue(TEXT("The manifest selects only this exact parent-first original family"),
                Row.IsValid() && Row->TryGetStringField(TEXT("package"), Package) && Package == Family[Index]))
            return false;
        if (!TestTrue(TEXT("Retained converted source and prior plan bytes match their supplied SHA256 snapshots: ") +
                    Package,
                VerifiedBytes(Row, TEXT("header"), TEXT("header_sha256"), Header) &&
                    VerifiedBytes(Row, TEXT("body"), TEXT("body_sha256"), Body) &&
                    VerifiedBytes(Row, TEXT("plan"), TEXT("plan_sha256"), CachedPlan)))
            return false;
        FTemplateMigrationRow One;
        One.Package = Package;
        if (!TestTrue(TEXT("The existing saved package has the manifest's pre-migration byte snapshot"),
                Row->TryGetStringField(TEXT("blueprint_sha256"), One.FileDigest) &&
                    FPackageName::DoesPackageExist(Package, nullptr, &One.File) &&
                    IFileManager::Get().IsReadOnly(*One.File)))
            return false;
        int64 FileSize = 0;
        FString FileDigest;
        if (!TestTrue(TEXT("The existing package bytes match before load: ") + Package,
                P4HashFile(One.File, 64 * 1024 * 1024, FileSize, FileDigest) &&
                    P4ExactNativeString(FileDigest, One.FileDigest)))
            return false;
        One.Blueprint.Reset(
            LoadObject<UBlueprint>(nullptr, *(Package + TEXT(".") + FPackageName::GetShortName(Package))));
        UBlueprint* Blueprint = One.Blueprint.Get();
        FP4OriginalRecord Record;
        FString Error;
        const auto Policy = P4Kadan::GetAssetPolicy(Blueprint);
        if (!TestTrue(TEXT("The loaded target is a clean original protected recovered Blueprint of importer version 2"),
                Blueprint && !Blueprint->GetOutermost()->IsDirty() && Policy.Original && !Policy.AllowEdit &&
                    !Policy.StandIn && P4ReadOriginalRecord(Blueprint, Record, Error) && Record.Version == 2 &&
                    Record.Kind == TEXT("blueprint") && Record.SourceDigestKind == TEXT("blueprint-import") &&
                    Record.Fidelity == TEXT("recovered") &&
                    FString(Blueprint->GetOutermost()->GetMetaData()->GetValue(
                        Blueprint, TEXT("P4BlueprintImportVersion"))) == TEXT("2")))
            return false;
        try
        {
            std::vector<std::uint8_t> H(Header.GetData(), Header.GetData() + Header.Num()),
                B(Body.GetData(), Body.GetData() + Body.Num());
            auto Inventory = P4MapKit::ReadPackage(H, B);
            if (!Inventory.Error.Code.empty() || Inventory.Package != std::string(TCHAR_TO_UTF8(*Package)))
                throw P4MapKit::Blueprint::Error("migration-source-package-differs");
            const auto Document = P4MapKit::Blueprint::ReadBlueprintDocument(Inventory, B);
            P4MapKit::Blueprint::PlanBuilder Builder(Inventory, B, Document);
            auto Source = Builder.Build();
            const auto Previous = J::Parse(
                std::string(reinterpret_cast<const char*>(CachedPlan.GetData()), CachedPlan.Num()));
            if (Previous["format"].S() != "p4-blueprint-plan" || Previous["version"].I() != 1 ||
                Previous["package"].S() != Inventory.Package)
                throw P4MapKit::Blueprint::Error("migration-plan-version-or-package-differs");
            auto OriginalOverrides = Source["overrides"], PreviousOverrides = Previous["overrides"];
            // Newly recovered UObject flags and old evidence-only fields do not alter the authored
            // source values. Those flags are taken from the verified actual source bytes above.
            WithoutFlagsAndEvidence(OriginalOverrides);
            WithoutFlagsAndEvidence(PreviousOverrides);
            if (OriginalOverrides.Dump() != PreviousOverrides.Dump())
                throw P4MapKit::Blueprint::Error("migration-plan-overrides-differ-from-source");
            const FString SourceText(UTF8_TO_TCHAR(Source.Dump().c_str()));
            if (!FJsonSerializer::Deserialize(TJsonReaderFactory<TCHAR>::Create(SourceText), One.Source))
                throw P4MapKit::Blueprint::Error("migration-source-plan-unreadable");
        }
        catch (const std::exception& Why)
        {
            AddError(FString(UTF8_TO_TCHAR(Why.what())));
            return false;
        }
        Prepared.Add(MoveTemp(One));
    }
    AddInfo(TEXT(
        "Current saved-file hashes are pre-migration snapshots for race detection, not historical proof that no external saved edit ever occurred."));
    for (auto& One : Prepared)
    {
        int64 Size = 0;
        FString Before;
        if (!TestTrue(TEXT("No writer changed the target between preflight and publication"),
                !One.Blueprint->GetOutermost()->IsDirty() && P4HashFile(One.File, 64 * 1024 * 1024, Size, Before) &&
                    P4ExactNativeString(Before, One.FileDigest)))
            return false;
        P4ImportPublication::FImportScope Import;
        P4RememberBlueprintSourceTemplates(One.Blueprint.Get(), One.Source);
        P4Graphs::FReport Report;
        P4RestoreBlueprintSourceTemplates(One.Blueprint.Get(), Report);
        for (const FString& Problem : Report.Problems)
            AddError(Problem);
        if (Report.Problems.Num())
            return false;
        P4ImportPublication::FSaveScope Save(One.Blueprint->GetOutermost());
        if (!TestTrue(TEXT("Publish only the source-template metadata and missing original templates: ") + One.Package,
                UPackage::SavePackage(One.Blueprint->GetOutermost(), One.Blueprint.Get(), RF_Public | RF_Standalone,
                    *One.File, GError, nullptr, false, true, SAVE_NoError)))
            return false;
        AddInfo(One.Package + TEXT(" ") + Report.Text());
    }
    return !HasAnyErrors();
}
#endif

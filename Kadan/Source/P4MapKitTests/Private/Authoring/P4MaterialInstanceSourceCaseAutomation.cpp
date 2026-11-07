#include "P4KadanImport.h"
#include "P4MaterialInstance.h"
#include "P4OriginalRecord.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "P4MapKitCore/material_source_import_compare.h"

namespace
{
bool CheckSourceObjectCase(FAutomationTestBase& Test, bool All)
{
    const auto TestTrue = [&](const FString& Name, bool Value) { return Test.TestTrue(Name, Value); };
    const auto TestFalse = [&](const FString& Name, bool Value) { return Test.TestFalse(Name, Value); };
    const auto TestNotNull = [&](const FString& Name, const void* Value) { return Test.TestNotNull(Name, Value); };
    const auto TestEqual = [&](const FString& Name, const FString& Actual, const FString& Expected)
    { return Test.TestEqual(Name, Actual, Expected); };
    const FString Project = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_MI_SOURCE_CASE_PROJECT"));
    if (Project.IsEmpty())
    {
        Test.AddInfo(TEXT(
            "P4MapKit test not run. Set P4_MI_SOURCE_CASE_PROJECT to an explicit disposable project for the original-name import fixture."));
        return true;
    }
    if (!TestTrue(TEXT("Explicit disposable project must match the running editor"),
            FPaths::IsSamePath(FPaths::ConvertRelativePathToFull(Project),
                FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()))))
        return false;
    const FString Package = TEXT(
        "/Game/Carrot4/Terrain/Area/Area003/Grounds/Bases/Materials/MI_stg_Area003_G_Ground33_SoftOfF");
    FP4KadanImportSettings Settings;
    Settings.GameFolder = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_MI_CENSUS_SOURCE"));
    Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    Settings.Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    Settings.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"));
    Settings.ShaderArchive = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_MI_CENSUS_SHADER_ARCHIVE"));
    Settings.AllowGameBackedLevels = true;
    TArray<TSharedPtr<FP4KadanSourceAsset>> Sources;
    TArray<FString> Problems;
    if (!TestTrue(TEXT("Normal source scan"), P4KadanScanGame(Settings.GameFolder, Sources, Problems)))
        return false;
    struct FOriginal
    {
        FString Package, Name;
        P4MapKit::Material::Json Expected;
    };
    TArray<FOriginal> Originals;
    TSet<FString> Roots;
    for (const auto& Source : Sources)
    {
        if (Source->Kind != TEXT("MaterialInstanceConstant") ||
            (!All && !Source->Package.Equals(Package, ESearchCase::CaseSensitive)))
            continue;
        TArray<uint8> Header, Body;
        if (!TestTrue(TEXT("Original bytes remain readable"),
                FFileHelper::LoadFileToArray(Header, *Source->Header) &&
                    FFileHelper::LoadFileToArray(Body, *Source->Body)))
            return false;
        const std::vector<uint8> H(Header.GetData(), Header.GetData() + Header.Num()),
            B(Body.GetData(), Body.GetData() + Body.Num());
        const auto Inventory = P4MapKit::ReadPackage(H, B);
        std::string Name;
        if (!TestTrue(
                TEXT("Original root export decodes"), Inventory.Error.Code.empty() && Inventory.Reference(1, Name)))
            return false;
        const FString ObjectName = UTF8_TO_TCHAR(Name.c_str()), Leaf = FPaths::GetBaseFilename(Source->Package);
        if (ObjectName.Equals(Leaf, ESearchCase::CaseSensitive))
            continue;
        if (!TestTrue(TEXT("Original export spelling differs only in case: ") + Source->Package,
                ObjectName.Equals(Leaf, ESearchCase::IgnoreCase)))
            return false;
        P4MapKit::FMaterialCookedRecord Record;
        P4MapKit::FReadError ReadError;
        const auto Cooked = P4MapKit::ReadPackageHeader(H, B.size());
        if (!TestTrue(TEXT("Original local overrides decode"),
                P4MapKit::ReadMaterialCookedRecord(Cooked, B, Record, ReadError)))
            return false;
        Record.Package = TCHAR_TO_UTF8(*Source->Package);
        Originals.Add(
            {Source->Package, ObjectName, P4MapKit::Material::ProjectOriginalMaterialInstanceForImport(Record)});
        Roots.Add(Source->Package);
    }
    if (!TestTrue(TEXT("All known case-differing original instances selected"), Originals.Num() == (All ? 15 : 1)))
        return false;
    const auto Plan = P4KadanPlanImport(Sources, Roots, All, true);
    const auto Imported = P4KadanRunImport(Settings, Plan, All);
    const FString Out = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_MI_OUTPUT"));
    if (!Out.IsEmpty())
        FFileHelper::SaveStringToFile(
            Imported.Report, *(Out / TEXT("normal-import.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    if (!TestFalse(TEXT("Normal reimport was not cancelled"), Imported.Cancelled))
        return false;
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (const auto& Item : Originals)
    {
        auto* Instance = LoadObject<UMaterialInstanceConstant>(
            nullptr, *(Item.Package + TEXT(".") + Item.Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
        if (!TestNotNull(TEXT("Normal route produced a native instance: ") + Item.Package, Instance))
            return false;
        const auto Original = P4QueryOriginalAsset(Instance);
        TestTrue(TEXT("Raw export spelling is preserved: ") + Item.Package,
            Instance->GetName().Equals(Item.Name, ESearchCase::CaseSensitive));
        TestTrue(TEXT("Package spelling is preserved: ") + Item.Package,
            Instance->GetOutermost()->GetName().Equals(Item.Package, ESearchCase::CaseSensitive));
        TestFalse(TEXT("Recovered instance is not reference-only: ") + Item.Package, Original.Record.ReferenceOnly);
        TestEqual(TEXT("Current native source state was captured: ") + Item.Package, Original.Record.ComparisonSchema,
            FString(TEXT("material-instance-native-state-v1")));
        FString Error;
        const FString Actual = P4MaterialInstanceJson(Instance, Error);
        const bool Equal = Error.IsEmpty() &&
            P4MapKit::Material::CompareOriginalMaterialInstanceImport(
                Item.Expected, P4MapKit::Material::Json::Parse(TCHAR_TO_UTF8(*Actual)));
        TestTrue(TEXT("All original overrides survive normal import: ") + Item.Package, Equal);
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("package"), Item.Package);
        Row->SetStringField(TEXT("object_name"), Instance->GetName());
        Row->SetBoolField(TEXT("all_overrides_intact"), Equal);
        Row->SetBoolField(TEXT("reference_only"), Original.Record.ReferenceOnly);
        Row->SetStringField(TEXT("comparison_schema"), Original.Record.ComparisonSchema);
        Rows.Add(MakeShared<FJsonValueObject>(Row));
    }
    if (!Out.IsEmpty())
    {
        auto Report = MakeShared<FJsonObject>();
        Report->SetArrayField(TEXT("rows"), Rows);
        FString Text;
        FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Text));
        FFileHelper::SaveStringToFile(
            Text, *(Out / TEXT("case-readback.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }
    return !Test.HasAnyErrors();
}
} //namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialInstanceSourceObjectCase,
    "P4MapKit.Quality.MaterialInstanceSourceObjectCase",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialInstanceSourceObjectCase::RunTest(const FString&)
{
    return CheckSourceObjectCase(*this, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialInstanceSourceObjectCaseCensus,
    "P4MapKit.Quality.MaterialInstanceSourceObjectCaseCensus",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialInstanceSourceObjectCaseCensus::RunTest(const FString&)
{
    return CheckSourceObjectCase(*this, true);
}

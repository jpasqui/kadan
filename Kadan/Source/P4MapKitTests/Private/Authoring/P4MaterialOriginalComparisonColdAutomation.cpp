#if WITH_DEV_AUTOMATION_TESTS
#include "P4MaterialGraph.h"
#include "P4OriginalRecord.h"
#include "P4PackageSave.h"
#include "P4NativeHash.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialOriginalComparisonColdTest, "P4MapKit.Kadan.MaterialOriginalComparisonCold",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialOriginalComparisonColdTest::RunTest(const FString&)
{
    const FString Phase = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_MATERIAL_COMPARISON_PHASE"));
    if (Phase.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. ")
                TEXT("Controlled saved/cold native comparison requires explicit phase; no qualification performed."));
        return true;
    }
    if (!TestTrue(TEXT("Only explicit save and separate cold phases are allowed"),
            Phase == TEXT("save") || Phase == TEXT("cold")))
        return false;
    const FString Inputs = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    const FString ExpectedProject = Inputs / TEXT("dev-host-tests/DevHost.uproject");
    if (!TestTrue(TEXT("Only the existing private build host is used"),
            !Inputs.IsEmpty() &&
                FPaths::IsSamePath(FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()),
                    FPaths::ConvertRelativePathToFull(ExpectedProject))))
        return false;
    const FString Package = TEXT("/Game/Carrot4/__P4MaterialComparisonCold/NativeComparison"),
                  ObjectPath = Package + TEXT(".NativeComparison");
    const FString File = FPackageName::LongPackageNameToFilename(Package, FPackageName::GetAssetPackageExtension());
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("P4MapKit/MaterialOriginalComparison");
    const FString Receipt = Directory / (Phase + TEXT(".json"));
    if (!TestFalse(TEXT("An existing phase receipt is never overwritten"), IFileManager::Get().FileExists(*Receipt)))
        return false;
    auto Proof = MakeShared<FJsonObject>();
    Proof->SetStringField(TEXT("owner"), TEXT("p4-material-native-comparison-controlled-v1"));
    Proof->SetStringField(TEXT("phase"), Phase);
    Proof->SetStringField(TEXT("package"), Package);
    Proof->SetNumberField(TEXT("pid"), FPlatformProcess::GetCurrentProcessId());
    FString Error, BeforeSha;
    int64 BeforeSize = 0;
    TStrongObjectPtr<UMaterial> Material;
    TSharedPtr<FJsonObject> Saved;
    if (Phase == TEXT("save"))
    {
        if (!TestTrue(TEXT("Controlled destination starts absent"),
                !FPackageName::DoesPackageExist(Package) && !FindPackage(nullptr, *Package)))
            return false;
        const FString Graph = TEXT(
            R"({"format":2,"domain":"MD_Surface","blend_mode":"BLEND_Opaque","shading_model":"MSM_Unlit","two_sided":false,"nodes":{"Value":{"type":"Constant","props":{"R":0.25}}},"outputs":{"EmissiveColor":["Value",""]}})");
        Material.Reset(P4BuildMaterialFromGraph(Graph, Package, Error));
        if (!TestNotNull(TEXT("A real controlled native graph is constructed: ") + Error, Material.Get()))
            return false;
    }
    else
    {
        FString Text;
        if (!TestTrue(TEXT("A successful prior save proof exists"),
                FFileHelper::LoadFileToString(Text, *(Directory / TEXT("save.json"))) &&
                    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Saved) && Saved.IsValid() &&
                    Saved->GetBoolField(TEXT("passed")) &&
                    Saved->GetStringField(TEXT("owner")) == TEXT("p4-material-native-comparison-controlled-v1")))
            return false;
        if (!TestTrue(TEXT("Cold load is initially unloaded and uses a distinct process"),
                !FindPackage(nullptr, *Package) &&
                    Saved->GetNumberField(TEXT("pid")) != FPlatformProcess::GetCurrentProcessId()))
            return false;
        if (!TestTrue(TEXT("Controlled saved bytes are unchanged before cold load"),
                P4HashFile(File, 1024 * 1024, BeforeSize, BeforeSha) &&
                    P4ExactNativeString(BeforeSha, Saved->GetStringField(TEXT("file_sha256"))) &&
                    BeforeSize == Saved->GetNumberField(TEXT("file_size"))))
            return false;
        Material.Reset(LoadObject<UMaterial>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet));
        if (!TestNotNull(TEXT("Actual saved native material cold-loads"), Material.Get()))
            return false;
    }
    FP4OriginalRecord Record;
    if (!TestTrue(TEXT("New native comparison schema and baseline were captured at construction"),
            P4ReadOriginalRecord(Material.Get(), Record, Error) &&
                Record.ComparisonSchema == TEXT("material-native-state-v1") && Record.ComparisonDigest.Len() == 64 &&
                Record.ReadOnly && Record.ReferenceOnly))
        return false;
    TArray<FString> CompileErrors;
    if (!TestTrue(TEXT("Actual current-platform shaders compile before comparing persisted state"),
            P4WaitForMaterialShaders(Material.Get(), CompileErrors, Error) && CompileErrors.Num() == 0))
        return false;
    const auto Compare = P4DiagnoseOriginalAsset(Material.Get());
    Proof->SetStringField(TEXT("comparison_state"), Compare.CurrentDigest);
    Proof->SetStringField(TEXT("comparison_error"), Compare.Error);
    TestTrue(TEXT("Native state including exact binary fields remains equal after actual shader work"),
        Compare.Error.IsEmpty() && Compare.Comparison == EP4OriginalComparison::Equal &&
            P4ExactNativeString(Compare.CurrentDigest, Record.ComparisonDigest));
    const FString Marker = Material->GetOutermost()->GetMetaData()->GetValue(Material.Get(), TEXT("P4OriginalRecord"));
    Proof->SetStringField(TEXT("original_record"), Marker);
    if (Phase == TEXT("save") && !HasAnyErrors())
    {
        FString Why;
        TestTrue(TEXT("Only the controlled package is saved with its original comparison baseline"),
            P4SaveAssetPackage(
                Material.Get(),
                [&]()
                {
                    return P4ExactNativeString(
                        P4MaterialComparisonState(Material.Get(), Error), Record.ComparisonDigest);
                },
                Why));
        if (!Why.IsEmpty())
            AddError(Why);
    }
    if (Phase == TEXT("cold"))
    {
        TestTrue(TEXT("Cold graph comparison exactly matches the saved native state and provenance"),
            P4ExactNativeString(Compare.CurrentDigest, Saved->GetStringField(TEXT("comparison_state"))) &&
                P4ExactNativeString(Marker, Saved->GetStringField(TEXT("original_record"))));
        TestFalse(TEXT("Cold validation does not dirty the package"), Material->GetOutermost()->IsDirty());
    }
    int64 Size = 0;
    FString Sha;
    TestTrue(TEXT("Controlled output has bounded actual saved bytes"), P4HashFile(File, 1024 * 1024, Size, Sha));
    if (Phase == TEXT("cold"))
        TestTrue(TEXT("Cold comparison changes no saved content bytes"),
            Size == BeforeSize && P4ExactNativeString(Sha, BeforeSha));
    Proof->SetNumberField(TEXT("file_size"), Size);
    Proof->SetStringField(TEXT("file_sha256"), Sha);
    Proof->SetBoolField(TEXT("passed"), !HasAnyErrors());
    FString Text;
    FJsonSerializer::Serialize(Proof, TJsonWriterFactory<>::Create(&Text));
    TestTrue(TEXT("Exact controlled comparison phase receipt is retained"),
        IFileManager::Get().MakeDirectory(*Directory, true) &&
            FFileHelper::SaveStringToFile(Text, *Receipt, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    return !HasAnyErrors();
}
#endif

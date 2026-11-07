#if WITH_DEV_AUTOMATION_TESTS
#include "P4MaterialGraph.h"
#include "P4OriginalRecord.h"
#include "P4KadanAssetPolicy.h"
#include "P4NativeHash.h"
#include "AssetRegistryModule.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialOriginalComparisonTest, "P4MapKit.Kadan.MaterialOriginalComparison",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialOriginalComparisonTest::RunTest(const FString&)
{
    // Controlled native graph, not a claim of game-source or pixel fidelity.
    // The genuine construction path must capture its own comparison baseline.
    const FString Package = TEXT("/Game/Carrot4/__P4MaterialComparison_") +
        FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Graph = TEXT(
        R"({"format":2,"domain":"MD_Surface","blend_mode":"BLEND_Opaque","shading_model":"MSM_Unlit","two_sided":false,"nodes":{"Value":{"type":"Constant","props":{"R":0.25}}},"outputs":{"EmissiveColor":["Value",""]}})");
    FString Error;
    TStrongObjectPtr<UMaterial> Material(P4BuildMaterialFromGraph(Graph, Package, Error));
    if (!TestNotNull(TEXT("Controlled graph uses the real native construction path: ") + Error, Material.Get()))
        return false;
    ON_SCOPE_EXIT
    {
        FAssetRegistryModule::AssetDeleted(Material.Get());
        Material->ClearFlags(RF_Public | RF_Standalone);
        Material->Rename(
            nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders);
        Material->MarkPendingKill();
    };
    Material->GetOutermost()->SetDirtyFlag(false);
    FP4OriginalRecord Record;
    if (!TestTrue(TEXT("Factory retained source identity and a separate native graph baseline"),
            P4ReadOriginalRecord(Material.Get(), Record, Error) &&
                Record.ComparisonSchema == TEXT("material-native-state-v1") && Record.ComparisonDigest.Len() == 64 &&
                Record.SourceDigestKind == TEXT("material-graph-v2-md5") && Record.ReadOnly && Record.ReferenceOnly))
        return false;
    const FString Marker = Material->GetOutermost()->GetMetaData()->GetValue(Material.Get(), TEXT("P4OriginalRecord"));
    const FString SourceRender = Material->GetOutermost()->GetMetaData()->GetValue(
        Material.Get(), TEXT("P4MaterialRenderSettingsV2"));
    TArray<uint8> Before;
    {
        FObjectWriter Writer(Material.Get(), Before, false, false, false);
    }
    const auto Equal = P4DiagnoseOriginalAsset(Material.Get());
    TArray<uint8> After;
    {
        FObjectWriter Writer(Material.Get(), After, false, false, false);
    }
    TestTrue(TEXT("Unchanged recovered graph has exact native comparison for dependency omission"),
        Equal.Error.IsEmpty() && Equal.Comparison == EP4OriginalComparison::Equal &&
            P4ExactNativeString(Equal.CurrentDigest, Record.ComparisonDigest));
    TestTrue(TEXT("Diagnostic changes no native material values or metadata"),
        Before == After &&
            P4ExactNativeString(
                Marker, Material->GetOutermost()->GetMetaData()->GetValue(Material.Get(), TEXT("P4OriginalRecord"))) &&
            !Material->GetOutermost()->IsDirty());
    TestFalse(TEXT("Native equality does not unlock original material editing"),
        P4Kadan::GetAssetPolicy(Material.Get()).AllowEdit);

    Material->TwoSided = true;
    const auto Changed = P4DiagnoseOriginalAsset(Material.Get());
    TestTrue(TEXT("A native render-property edit is different even with a clean package flag"),
        Changed.Error.IsEmpty() && Changed.Comparison == EP4OriginalComparison::Different &&
            !Material->GetOutermost()->IsDirty());
    TestTrue(TEXT("Changed content cannot rewrite its baseline"),
        P4ExactNativeString(
            Marker, Material->GetOutermost()->GetMetaData()->GetValue(Material.Get(), TEXT("P4OriginalRecord"))));
    Material->TwoSided = false;
    TestTrue(TEXT("Restoring actual native values restores equality"),
        P4DiagnoseOriginalAsset(Material.Get()).Comparison == EP4OriginalComparison::Equal);
    const bool FullyRough = Material->bFullyRough;
    Material->bFullyRough = !FullyRough;
    TestTrue(TEXT("Native render fields omitted by source-shaped JSON still compare different"),
        P4DiagnoseOriginalAsset(Material.Get()).Comparison == EP4OriginalComparison::Different);
    Material->bFullyRough = FullyRough;
    const float Clip = Material->OpacityMaskClipValue;
    uint32 ClipBits = 0;
    FMemory::Memcpy(&ClipBits, &Clip, sizeof(ClipBits));
    ++ClipBits;
    FMemory::Memcpy(&Material->OpacityMaskClipValue, &ClipBits, sizeof(ClipBits));
    TestTrue(TEXT("A one-ULP float edit cannot collapse through text formatting"),
        P4DiagnoseOriginalAsset(Material.Get()).Comparison == EP4OriginalComparison::Different);
    Material->OpacityMaskClipValue = Clip;
    const float Bias = Material->RefractionDepthBias;
    Material->RefractionDepthBias = 0.0f;
    const FString PositiveZero = P4MaterialComparisonState(Material.Get(), Error);
    const uint32 NegativeZeroBits = 0x80000000u;
    FMemory::Memcpy(&Material->RefractionDepthBias, &NegativeZeroBits, sizeof(NegativeZeroBits));
    const FString NegativeZero = P4MaterialComparisonState(Material.Get(), Error);
    TestTrue(TEXT("Persistent native float signed zero remains bit-exact"),
        !PositiveZero.IsEmpty() && !NegativeZero.IsEmpty() && !P4ExactNativeString(PositiveZero, NegativeZero));
    Material->RefractionDepthBias = Bias;

    const FString PartialPackage = Package + TEXT("_Partial");
    const FString PartialGraph = Graph.Replace(TEXT("\"two_sided\":false,"),
        TEXT("\"two_sided\":false,\"render_settings\":{\"bAllowNegativeEmissiveColor\":false},"));
    TStrongObjectPtr<UMaterial> Partial(P4BuildMaterialFromGraph(PartialGraph, PartialPackage, Error));
    if (!TestNotNull(TEXT("Partial source render policy uses real construction"), Partial.Get()))
        return false;
    ON_SCOPE_EXIT
    {
        FAssetRegistryModule::AssetDeleted(Partial.Get());
        Partial->ClearFlags(RF_Public | RF_Standalone);
        Partial->Rename(
            nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders);
        Partial->MarkPendingKill();
    };
    Partial->GetOutermost()->SetDirtyFlag(false);
    const bool PartialRough = Partial->bFullyRough;
    Partial->bFullyRough = !PartialRough;
    TestTrue(TEXT("A partial source render policy cannot hide other native settings"),
        P4DiagnoseOriginalAsset(Partial.Get()).Comparison == EP4OriginalComparison::Different);
    Partial->bFullyRough = PartialRough;

    FP4OriginalRecord Old = Record;
    Old.ComparisonSchema = TEXT("material-graph-v2");
    Old.ComparisonDigest.Empty();
    if (!TestTrue(TEXT("Controlled pre-fix record is installed"), P4MarkOriginalAsset(Material.Get(), Old, Error)))
        return false;
    const FString OldMarker = Material->GetOutermost()->GetMetaData()->GetValue(
        Material.Get(), TEXT("P4OriginalRecord"));
    const auto Missing = P4DiagnoseOriginalAsset(Material.Get());
    TestTrue(TEXT("A source recipe digest cannot substitute for a missing native baseline"),
        Missing.Comparison == EP4OriginalComparison::NotCompared &&
            Missing.Error == TEXT("original-material-comparison-baseline-missing") && Missing.CurrentDigest.IsEmpty() &&
            P4ExactNativeString(OldMarker,
                Material->GetOutermost()->GetMetaData()->GetValue(Material.Get(), TEXT("P4OriginalRecord"))));
    if (!TestTrue(TEXT("Original controlled baseline restored without recapture"),
            P4MarkOriginalAsset(Material.Get(), Record, Error)))
        return false;

    Material->GetOutermost()->GetMetaData()->SetValue(Material.Get(), TEXT("P4MaterialRenderSettingsV2"), TEXT("{"));
    const auto Unreadable = P4DiagnoseOriginalAsset(Material.Get());
    TestTrue(TEXT("An unreadable native graph has no equality proof"),
        Unreadable.Comparison == EP4OriginalComparison::Unreadable && !Unreadable.Error.IsEmpty() &&
            Unreadable.CurrentDigest.IsEmpty());
    Material->GetOutermost()->GetMetaData()->SetValue(
        Material.Get(), TEXT("P4MaterialRenderSettingsV2"), *SourceRender);
    TestTrue(TEXT("Refused readback preserves the original record"),
        P4ExactNativeString(
            Marker, Material->GetOutermost()->GetMetaData()->GetValue(Material.Get(), TEXT("P4OriginalRecord"))));
    TestTrue(TEXT("Final native graph still matches the initial baseline"),
        P4DiagnoseOriginalAsset(Material.Get()).Comparison == EP4OriginalComparison::Equal);
    TestFalse(TEXT("Comparison controls saved no content package"), FPackageName::DoesPackageExist(Package));
    return !HasAnyErrors();
}
#endif

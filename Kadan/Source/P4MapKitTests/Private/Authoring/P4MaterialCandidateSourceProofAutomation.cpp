#if WITH_DEV_AUTOMATION_TESTS
#include "P4MaterialCandidateSourceProof.h"
#include "P4MaterialGraph.h"
#include "AssetRegistryModule.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "MaterialExpressionIO.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4FreshMaterialCandidateSourceProof,
    "P4MapKit.Kadan.FreshMaterialCandidateSourceProof",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4FreshMaterialCandidateSourceProof::RunTest(const FString&)
{
    const FString Package = TEXT("/Game/P4MapKitMaterialSourceProof/OnlyCandidate");
    const FString ObjectPath = Package + TEXT(".OnlyCandidate");
    if (!TestTrue(TEXT("The unsaved proof namespace is empty"),
            !FPackageName::DoesPackageExist(Package) && !FindObject<UObject>(nullptr, *ObjectPath)))
        return false;
    // Default-valued R=0, a real RGB/alpha output and a world-space B channel
    // exercise typed default comparison and the builder's native channel adapters.
    const FString Source = TEXT(R"JSON({"format":2,"domain":"MD_Surface","blend_mode":"BLEND_Opaque",
        "two_sided":false,"shading_model":"MSM_DefaultLit","clip":0.3333,"tangent_space_normal":true,
        "render_settings":{"bFullyRough":true,"bUsedWithSkeletalMesh":true},
        "nodes":{"Constant_0":{"type":"Constant","inputs":{},"props":{"R":0}},
          "Custom_0":{"type":"Custom","inputs":{},"props":{"Code":"return 0;","Description":"Proof only","OutputType":"CMOT_Float1"}},
          "VectorParameter_0":{"type":"VectorParameter","inputs":{},"props":{"ParameterName":"ProofTint","DefaultValue":[0.25,0.5,0.75,1]}},
          "WorldPosition_0":{"type":"WorldPosition","inputs":{},"props":{}},
          "Multiply_0":{"type":"Multiply","inputs":{"A":["VectorParameter_0","RGB"],"B":["WorldPosition_0","B"]},"props":{}}},
        "outputs":{"EmissiveColor":["Multiply_0",""],"BaseColor":["VectorParameter_0",""],"Metallic":["Constant_0",""],"Opacity":["Custom_0",""]}})JSON");
    FString Error;
    TStrongObjectPtr<UMaterial> Candidate(P4BuildMaterialFromGraph(Source, Package, Error));
    if (!TestTrue(TEXT("A real native candidate is constructed"), Candidate.IsValid()))
    {
        AddError(Error);
        return false;
    }
    ON_SCOPE_EXIT
    {
        FAssetRegistryModule::AssetDeleted(Candidate.Get());
        Candidate->ClearFlags(RF_Public | RF_Standalone);
        const bool Renamed = Candidate->Rename(
            nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders);
        if (Renamed)
            Candidate->MarkPendingKill();
        TestTrue(TEXT("Only the owned unsaved proof candidate is retired"),
            Renamed && !FindObject<UObject>(nullptr, *ObjectPath) && !FPackageName::DoesPackageExist(Package));
    };
    TestTrue(TEXT("Native properties, output adapters and settings match source"),
        P4FreshMaterialCandidateMatchesSource(Candidate.Get(), Source, Error));
    if (!Error.IsEmpty())
        AddError(Error);
    UMaterialExpressionConstant* Constant = nullptr;
    for (UMaterialExpression* Expression : Candidate->Expressions)
        if (auto* Found = Cast<UMaterialExpressionConstant>(Expression))
            Constant = Found;
    if (!TestNotNull(TEXT("The source's scalar exists"), Constant))
        return false;
    Constant->R = .25f;
    TestFalse(TEXT("A changed default-valued source field is refused"),
        P4FreshMaterialCandidateMatchesSource(Candidate.Get(), Source, Error));
    Constant->R = 0.f;
    Candidate->TwoSided = true;
    TestFalse(TEXT("A changed material setting is refused"),
        P4FreshMaterialCandidateMatchesSource(Candidate.Get(), Source, Error));
    Candidate->TwoSided = false;
    Candidate->bFullyRough = false;
    TestFalse(TEXT("A changed source render flag is refused"),
        P4FreshMaterialCandidateMatchesSource(Candidate.Get(), Source, Error));
    Candidate->bFullyRough = true;
    UMaterialExpressionMultiply* Multiply = nullptr;
    UMaterialExpressionCustom* Custom = nullptr;
    for (UMaterialExpression* Expression : Candidate->Expressions)
    {
        if (auto* Found = Cast<UMaterialExpressionMultiply>(Expression))
            Multiply = Found;
        if (auto* Found = Cast<UMaterialExpressionCustom>(Expression))
            Custom = Found;
    }
    if (!TestNotNull(TEXT("The source multiplier exists"), Multiply) ||
        !TestNotNull(TEXT("The source Custom node exists"), Custom))
        return false;
    const FExpressionInput MaskedBefore = Multiply->A;
    if (!TestTrue(TEXT("The actual vector connection has a native output mask and multiple ports"),
            MaskedBefore.Mask && MaskedBefore.Expression && MaskedBefore.Expression->GetOutputs().Num() > 1))
        return false;
    Multiply->A.OutputIndex = (Multiply->A.OutputIndex + 1) % MaskedBefore.Expression->GetOutputs().Num();
    TestFalse(TEXT("A changed in-range output index with the original mask is refused"),
        P4FreshMaterialCandidateMatchesSource(Candidate.Get(), Source, Error));
    TestTrue(
        TEXT("The failure identifies the output-index/mask disagreement"), Error.Contains(TEXT("output-index-mask")));
    Multiply->A = MaskedBefore;
    Custom->Inputs.AddDefaulted();
    TestFalse(TEXT("An omitted native Custom input array cannot be added silently"),
        P4FreshMaterialCandidateMatchesSource(Candidate.Get(), Source, Error));
    Custom->Inputs.Reset();
    Custom->AdditionalOutputs.AddDefaulted();
    TestFalse(TEXT("An omitted independent Custom output array cannot be added silently"),
        P4FreshMaterialCandidateMatchesSource(Candidate.Get(), Source, Error));
    Custom->AdditionalOutputs.Reset();
    const auto Prior = Candidate->EmissiveColor;
    Candidate->EmissiveColor.Expression = Constant;
    TestFalse(TEXT("A substituted output connection is refused"),
        P4FreshMaterialCandidateMatchesSource(Candidate.Get(), Source, Error));
    Candidate->EmissiveColor = Prior;
    UMaterialExpressionConstant* Extra = NewObject<UMaterialExpressionConstant>(Candidate.Get());
    Candidate->Expressions.Add(Extra);
    TestFalse(TEXT("An unaccounted expression is refused"),
        P4FreshMaterialCandidateMatchesSource(Candidate.Get(), Source, Error));
    Candidate->Expressions.Remove(Extra);
    Extra->MarkPendingKill();
    TestTrue(
        TEXT("Exact restoration passes again"), P4FreshMaterialCandidateMatchesSource(Candidate.Get(), Source, Error));
    if (!Error.IsEmpty())
        AddError(Error);
    return !HasAnyErrors();
}
#endif

#include "P4CreatorMaterialFactories.h"
#include "P4ExportDriver.h"
#include "P4ModDescriptor.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "StaticParameterSet.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"
#include "P4NativeCustomCompilerBridge.h"
#include "P4MaterialInstance.h"
#include "P4MaterialInstancePermutation.h"
#include "P4OriginalRecord.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialExpressionStaticComponentMaskParameter.h"
#include "P4TranslucentMaterialStaging.h"
#include "Engine/StaticMesh.h"
#include "Components/InstancedStaticMeshComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialInstancePermutationReview,
    "P4MapKit.Quality.MaterialInstancePermutationReview",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialInstancePermutationReview::RunTest(const FString&)
{
    FP4ModDescriptor Mod;
    Mod.Id = TEXT("native.mi.static.test");
    Mod.Name = TEXT("Instance permutations");
    Mod.Version = TEXT("1.0.0");
    FP4ModNames Names;
    FString Error;
    if (!TestTrue(TEXT("Fixture mod names"), P4ModDerivedNames(Mod, Names, Error)))
        return false;
    TStrongObjectPtr<UP4CreatorMaterialFactory> Factory(NewObject<UP4CreatorMaterialFactory>());
    TStrongObjectPtr<UMaterial> Graph(Cast<UMaterial>(
        Factory->FactoryCreateNew(UMaterial::StaticClass(), CreatePackage(*(Names.PackageRoot / TEXT("M_Review"))),
            TEXT("M_Review"), RF_Public | RF_Standalone, nullptr, GWarn)));
    if (!TestNotNull(TEXT("Creator graph"), Graph.Get()))
        return false;
    TStrongObjectPtr<UP4CreatorMaterialInstanceFactory> InstanceFactory(NewObject<UP4CreatorMaterialInstanceFactory>());
    InstanceFactory->InitialParent = Graph.Get();
    TStrongObjectPtr<UMaterialInstanceConstant> Instance(
        Cast<UMaterialInstanceConstant>(InstanceFactory->FactoryCreateNew(UMaterialInstanceConstant::StaticClass(),
            CreatePackage(*(Names.PackageRoot / TEXT("MI_Review"))), TEXT("MI_Review"), RF_Public | RF_Standalone,
            nullptr, GWarn)));
    if (!TestNotNull(TEXT("Creator instance"), Instance.Get()))
        return false;
    const bool Old = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_MI_EXPECT_REFUSALS")) == TEXT("1");
    TSharedRef<FJsonObject> Rows = MakeShared<FJsonObject>();
    const auto Check = [&](const TCHAR* Kind)
    {
        const auto Result = P4ExportAssetAvailability(Instance.Get(), Mod);
        Rows->SetStringField(Kind, Result.Reason);
        if (Old)
            TestTrue(FString(Kind) + TEXT(" reaches the shader or game-parameter refusal"),
                Result.Reason.Contains(TEXT("Keep the switches and settings")) ||
                    Result.Reason.Contains(TEXT("The game sets C4SkyLightParam")));
        return TestEqual(
            FString(Kind) + TEXT(" export admission"), Result.Route == EP4ExportAssetRoute::Unavailable, Old);
    };
    bool Good = true;
    Instance->bHasStaticPermutationResource = true;
    Good &= Check(TEXT("static-resource"));
    Instance->bHasStaticPermutationResource = false;
    FStaticParameterSet Static;
    Static.StaticSwitchParameters.Add(
        FStaticSwitchParameter(FMaterialParameterInfo(TEXT("Switch")), true, true, FGuid()));
    Instance->UpdateStaticPermutation(Static);
    Good &= Check(TEXT("static-switch"));
    Static = FStaticParameterSet();
    Static.StaticComponentMaskParameters.Add(
        FStaticComponentMaskParameter(FMaterialParameterInfo(TEXT("Mask")), true, false, false, false, true, FGuid()));
    Instance->UpdateStaticPermutation(Static);
    Good &= Check(TEXT("static-component-mask"));
    Instance->UpdateStaticPermutation(FStaticParameterSet());
    const auto Base = Instance->BasePropertyOverrides;
    Instance->BasePropertyOverrides.bOverride_BlendMode = true;
    Good &= Check(TEXT("blend-mode"));
    Instance->BasePropertyOverrides = Base;
    Instance->BasePropertyOverrides.bOverride_TwoSided = true;
    Good &= Check(TEXT("two-sided"));
    Instance->BasePropertyOverrides = Base;
    Instance->BasePropertyOverrides.bOverride_OpacityMaskClipValue = true;
    Good &= Check(TEXT("opacity-clip"));
    Instance->BasePropertyOverrides = Base;
    Instance->BasePropertyOverrides.bOverride_ShadingModel = true;
    Good &= Check(TEXT("shading-model"));
    Instance->BasePropertyOverrides = Base;
    Instance->BasePropertyOverrides.bOverride_DitheredLODTransition = true;
    Good &= Check(TEXT("dithered-lod"));
    Instance->BasePropertyOverrides = Base;
    Instance->BasePropertyOverrides.bOverride_CastDynamicShadowAsMasked = true;
    Good &= Check(TEXT("shadow-as-masked"));
    Instance->BasePropertyOverrides = Base;
    Instance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("C4SkyLightParam")), FLinearColor::Red);
    Good &= Check(TEXT("C4SkyLightParam"));
    FString Json;
    FJsonSerializer::Serialize(Rows, TJsonWriterFactory<>::Create(&Json));
    const FString Output = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_MI_REVIEW_RECEIPT"));
    if (!Output.IsEmpty())
        Good &= TestTrue(TEXT("Review evidence written"),
            FFileHelper::SaveStringToFile(Json, *Output, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    return Good;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialInstancePermutationCompile,
    "P4MapKit.Quality.MaterialInstancePermutationCompile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialInstancePermutationCompile::RunTest(const FString&)
{
    FP4ModDescriptor Mod;
    Mod.Id = TEXT("native.mi.compile");
    Mod.Name = TEXT("Permutation compile");
    Mod.Version = TEXT("1.0.0");
    FP4ModNames Names;
    FString NameError;
    if (!P4ModDerivedNames(Mod, Names, NameError))
    {
        AddError(NameError);
        return false;
    }
    const FString Root = Names.PackageRoot / TEXT("M_Compile");
    TStrongObjectPtr<UP4CreatorMaterialFactory> Factory(NewObject<UP4CreatorMaterialFactory>());
    TStrongObjectPtr<UMaterial> Graph(Cast<UMaterial>(Factory->FactoryCreateNew(
        UMaterial::StaticClass(), CreatePackage(*Root), TEXT("M_Compile"), RF_Public | RF_Standalone, nullptr, GWarn)));
    if (!TestNotNull(TEXT("Creator graph constructed"), Graph.Get()))
        return false;
    Graph->SetShadingModel(MSM_Unlit);
    auto* Red = NewObject<UMaterialExpressionConstant3Vector>(Graph.Get());
    Red->Constant = FLinearColor(1, 0, 0);
    auto* Green = NewObject<UMaterialExpressionConstant3Vector>(Graph.Get());
    Green->Constant = FLinearColor(0, 1, 0);
    auto* Switch = NewObject<UMaterialExpressionStaticSwitchParameter>(Graph.Get());
    Switch->ParameterName = TEXT("Choice");
    Switch->A.Expression = Red;
    Switch->B.Expression = Green;
    Graph->Expressions.Append({Red, Green, Switch});
    Graph->EmissiveColor.Expression = Switch;
    Graph->PostEditChange();
    FString Error;
    TArray<FString> Missing;
    TStrongObjectPtr<UMaterialInstanceConstant> Instance(P4BuildMaterialInstance(
        TEXT("{\"parent\":\"") + Graph->GetPathName() + TEXT("\",\"switches\":{\"Choice\":false}}"),
        Names.PackageRoot / TEXT("MI_Compile"), Missing, Error));
    if (!TestNotNull(TEXT("Creator instance constructed: ") + Error, Instance.Get()))
        return false;
    FP4CreatorMaterialCompileRequest Request;
    Request.CreatorMaterial = Graph.Get();
    Request.CreatorInstance = Instance.Get();
    Request.RuntimeObjectPath = Names.PackageRoot / TEXT("_KadanRuntimeMaterials/MI_Compile.MI_Compile");
    const FString Before = P4MaterialInstanceJson(Instance.Get(), Error);
    const auto GreenMap = P4CompileCreatorMaterial(Request);
    if (!TestTrue(TEXT("False permutation compiles: ") + GreenMap.Error, GreenMap.Succeeded))
        return false;
    TestEqual(TEXT("Compilation preserves source overrides"), P4MaterialInstanceJson(Instance.Get(), Error), Before);
    FStaticParameterSet Static;
    Instance->GetStaticParameterValues(Static);
    for (auto& Parameter : Static.StaticSwitchParameters)
        if (Parameter.ParameterInfo.Name == TEXT("Choice"))
        {
            Parameter.Value = true;
            Parameter.bOverride = true;
        }
    Instance->UpdateStaticPermutation(Static);
    const auto RedMap = P4CompileCreatorMaterial(Request);
    if (!TestTrue(TEXT("True permutation compiles: ") + RedMap.Error, RedMap.Succeeded))
        return false;
    TestTrue(TEXT("Switch changes actual compiled programs"), RedMap.MapHash != GreenMap.MapHash);
    TestTrue(TEXT("Both permutations carry nonempty frozen maps"), !RedMap.Block.empty() && !GreenMap.Block.empty());
    TStrongObjectPtr<UMaterialInstanceConstant> Child(P4BuildMaterialInstance(
        TEXT("{\"parent\":\"") + Instance->GetPathName() + TEXT("\",\"overrides\":{\"two_sided\":true}}"),
        Names.PackageRoot / TEXT("MI_Child"), Missing, Error));
    if (!TestNotNull(TEXT("Nested instance with its own base override"), Child.Get()))
        return false;
    Request.CreatorInstance = Child.Get();
    const auto InheritedRed = P4CompileCreatorMaterial(Request);
    for (auto& Parameter : Static.StaticSwitchParameters)
        if (Parameter.ParameterInfo.Name == TEXT("Choice"))
            Parameter.Value = false;
    Instance->UpdateStaticPermutation(Static);
    const auto InheritedGreen = P4CompileCreatorMaterial(Request);
    if (!TestTrue(TEXT("Both inherited permutations compile"), InheritedRed.Succeeded && InheritedGreen.Succeeded))
        return false;
    TestTrue(TEXT("Child base override preserves inherited static switch semantics"),
        InheritedRed.MapHash != InheritedGreen.MapHash);

    auto* Mask = NewObject<UMaterialExpressionStaticComponentMaskParameter>(Graph.Get());
    Mask->ParameterName = TEXT("Channel");
    Mask->DefaultR = true;
    Mask->DefaultG = false;
    Mask->DefaultB = false;
    Mask->DefaultA = false;
    Red->Constant = FLinearColor(.2f, .8f, .5f);
    Mask->Input.Expression = Red;
    Graph->Expressions.Add(Mask);
    Graph->EmissiveColor.Expression = Mask;
    Graph->PostEditChange();
    TStrongObjectPtr<UMaterialInstanceConstant> MaskR(P4BuildMaterialInstance(
        TEXT("{\"parent\":\"") + Graph->GetPathName() + TEXT("\",\"masks\":{\"Channel\":[true,false,false,false]}}"),
        Names.PackageRoot / TEXT("MI_MaskR"), Missing, Error));
    TStrongObjectPtr<UMaterialInstanceConstant> MaskG(P4BuildMaterialInstance(
        TEXT("{\"parent\":\"") + Graph->GetPathName() + TEXT("\",\"masks\":{\"Channel\":[false,true,false,false]}}"),
        Names.PackageRoot / TEXT("MI_MaskG"), Missing, Error));
    if (!TestNotNull(TEXT("R mask instance"), MaskR.Get()) || !TestNotNull(TEXT("G mask instance"), MaskG.Get()))
        return false;
    Request.CreatorInstance = MaskR.Get();
    const auto RMap = P4CompileCreatorMaterial(Request);
    Request.CreatorInstance = MaskG.Get();
    const auto GMap = P4CompileCreatorMaterial(Request);
    if (!TestTrue(TEXT("Both mask permutations compile"), RMap.Succeeded && GMap.Succeeded))
        return false;
    TestTrue(TEXT("Component masks change actual compiled programs"), RMap.MapHash != GMap.MapHash);
    TestTrue(TEXT("Mask channels survive native JSON readback"),
        P4MaterialInstanceJson(MaskG.Get(), Error).Contains(TEXT("\"Channel\":[false,true,false,false]")));
    const FString Output = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_MI_OUTPUT"));
    if (!Output.IsEmpty())
    {
        TSharedRef<FJsonObject> Receipt = MakeShared<FJsonObject>();
        const auto Map = [&](const TCHAR* Name, const FP4CreatorMaterialCompileResult& Compiled)
        {
            TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("map_hash"), BytesToHex(Compiled.MapHash.data(), Compiled.MapHash.size()));
            Row->SetNumberField(TEXT("programs"), Compiled.Programs.size());
            Row->SetNumberField(TEXT("block_bytes"), Compiled.Block.size());
            Receipt->SetObjectField(Name, Row);
        };
        Map(TEXT("switch_false"), GreenMap);
        Map(TEXT("switch_true"), RedMap);
        Map(TEXT("inherited_false"), InheritedGreen);
        Map(TEXT("inherited_true"), InheritedRed);
        Map(TEXT("mask_r"), RMap);
        Map(TEXT("mask_g"), GMap);
        FString Text;
        FJsonSerializer::Serialize(Receipt, TJsonWriterFactory<>::Create(&Text));
        TestTrue(TEXT("Compiled permutation evidence written"),
            FFileHelper::SaveStringToFile(
                Text, *(Output / TEXT("compile.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    }
    // Static children stop nearest-map discovery at the child. Their renderers must
    // still reach the staged master's usage flags without editing the source master.
    Graph->BlendMode = BLEND_Masked;
    Graph->bUsedWithInstancedStaticMeshes = false;
    Graph->PostEditChange();
    TStrongObjectPtr<UMaterialInstanceConstant> InstancedChild(P4BuildMaterialInstance(
        TEXT("{\"parent\":\"") + Graph->GetPathName() + TEXT("\",\"overrides\":{\"blend_mode\":\"BLEND_Opaque\"}}"),
        Names.PackageRoot / TEXT("MI_Instanced"), Missing, Error));
    if (!TestNotNull(TEXT("Instanced permutation child"), InstancedChild.Get()))
        return false;
    TStrongObjectPtr<UStaticMesh> Mesh(NewObject<UStaticMesh>(GetTransientPackage()));
    Mesh->StaticMaterials.Add(FStaticMaterial(InstancedChild.Get()));
    TStrongObjectPtr<UInstancedStaticMeshComponent> Component(
        NewObject<UInstancedStaticMeshComponent>(GetTransientPackage()));
    Component->SetStaticMesh(Mesh.Get());
    Component->SetMaterial(0, InstancedChild.Get());
    FP4TranslucentMaterialStaging Staging;
    if (!TestTrue(TEXT("Static child renderer staging: ") + Error,
            Staging.Prepare(
                Names.PackageRoot, {Graph.Get(), InstancedChild.Get(), Mesh.Get(), Component.Get()}, Error)))
        return false;
    auto* Runtime = Cast<UMaterialInstanceConstant>(Staging.CookAsset(InstancedChild.Get()));
    TestTrue(TEXT("Runtime child keeps native MIC class and permutation flag"),
        Runtime && Runtime->bHasStaticPermutationResource);
    TestTrue(TEXT("Child's factory reaches the runtime master"),
        Runtime && Runtime->GetMaterial()->bUsedWithInstancedStaticMeshes);
    TestFalse(TEXT("Factory propagation preserves the authored master"), Graph->bUsedWithInstancedStaticMeshes != 0);
    TestTrue(TEXT("Instanced consumer accepts the compiled child: ") + Error, Staging.Remap(Component.Get(), Error));
    TestTrue(TEXT("Runtime materials release cleanly: ") + Error, Staging.Release(Error));
    TestTrue(TEXT("Consumer material restored exactly"), Component->GetMaterial(0) == InstancedChild.Get());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RecoveredViewInstancePermutation,
    "P4MapKit.Quality.MaterialInstanceRecoveredViewPermutation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RecoveredViewInstancePermutation::RunTest(const FString&)
{
    // Synthetic import identity tests routing only. Real Sky graph/game evidence
    // lives in the Cave000 fixture; this is not a recovery fidelity assertion.
    TStrongObjectPtr<UMaterial> Graph(
        NewObject<UMaterial>(CreatePackage(TEXT("/Game/__P4ViewPermutation/M_View")), TEXT("M_View")));
    auto* Vector = NewObject<UMaterialExpressionVectorParameter>(Graph.Get());
    Vector->ParameterName = TEXT("C4SkyLightParam");
    Vector->Group = TEXT("The game sets this");
    Graph->Expressions.Add(Vector);
    Graph->EmissiveColor.Expression = Vector;
    Graph->SetShadingModel(MSM_Unlit);
    Graph->PostEditChange();
    TStrongObjectPtr<UMaterialInstanceConstant> Source(NewObject<UMaterialInstanceConstant>(GetTransientPackage()));
    Source->SetParentEditorOnly(Graph.Get());
    Source->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(Vector->ParameterName), FLinearColor::Red);
    TestFalse(
        TEXT("An authored vector does not require a separate map"), P4MaterialInstanceNeedsOwnShaderMap(Source.Get()));
    FP4OriginalRecord Record;
    Record.Package = Graph->GetOutermost()->GetName();
    Record.Class = Graph->GetClass()->GetPathName();
    Record.Kind = TEXT("synthetic-view-alias-test");
    Record.SourceDigestKind = TEXT("fixture");
    Record.ComparisonSchema = TEXT("fixture");
    Record.SourceDigest = FString::ChrN(64, TEXT('a'));
    Record.ComparisonDigest = FString::ChrN(64, TEXT('b'));
    FString Error;
    if (!TestTrue(TEXT("Synthetic original identity"), P4MarkOriginalAsset(Graph.Get(), Record, Error)))
        return false;
    TestTrue(TEXT("A recovered View alias requires compiled material uniform reads"),
        P4MaterialInstanceNeedsOwnShaderMap(Source.Get()));
    const FString Before = P4MaterialInstanceJson(Source.Get(), Error);
    TStrongObjectPtr<UMaterialInstanceConstant> Copy(
        DuplicateObject<UMaterialInstanceConstant>(Source.Get(), GetTransientPackage()));
    P4MaterialInstancePrepareRecoveredViewPermutation(Source.Get(), Copy.Get());
    TestTrue(TEXT("No-switch graph receives native permutation identity"),
        Copy->GetStaticParameters().StaticSwitchParameters.Num() == 1);
    TestEqual(TEXT("Private preparation preserves source"), P4MaterialInstanceJson(Source.Get(), Error), Before);
    auto* Switch = NewObject<UMaterialExpressionStaticSwitchParameter>(Graph.Get());
    Switch->ParameterName = TEXT("Use_Cloud");
    Switch->DefaultValue = true;
    Graph->Expressions.Add(Switch);
    Graph->PostEditChange();
    Copy.Reset(DuplicateObject<UMaterialInstanceConstant>(Source.Get(), GetTransientPackage()));
    P4MaterialInstancePrepareRecoveredViewPermutation(Source.Get(), Copy.Get());
    const auto& Parameters = Copy->GetStaticParameters().StaticSwitchParameters;
    const auto* Cloud = Parameters.FindByPredicate(
        [](const auto& P) { return P.ParameterInfo.Name == TEXT("Use_Cloud"); });
    TestTrue(TEXT("Existing resolved switch keeps its value and becomes explicit"),
        Cloud && Cloud->Value && Cloud->bOverride);
    TestEqual(
        TEXT("Derived identity never edits the source instance"), P4MaterialInstanceJson(Source.Get(), Error), Before);
    return true;
}

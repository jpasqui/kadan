#if WITH_DEV_AUTOMATION_TESTS
#include "P4MaterialGraph.h"
#include "P4KadanMaterialExpressions.h"
#include "AssetRegistryModule.h"
#include "Materials/Material.h"
#include "MaterialShared.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "ShaderCompiler.h"
#include "UObject/StrongObjectPtr.h"
#include "P4MapKitCore/material_graph_canon.h"

// A recovered game master in UE's form (material_graph_canon.h, run by the import in FP4MaterialRecovery): each kind
// of rewrite the import makes, on a small graph of the form the decompiler writes, gives a graph Kadan's builder
// builds and UE compiles: the rim as UE's Lerp, a scroll as UE's Panner, the game's XluCut clip on the AO pin, the
// game's division where its uniform set has one, and customized UVs of per-instance data read from the game's
// instanced vertex program. Every recovered master is compared with the research route by
// tools/navigation/material_graph_canon_parity.py.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialGameFormTest, "P4MapKit.Kadan.MaterialGameForm",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialGameFormTest::RunTest(const FString&)
{
    namespace M = P4MapKit::Material;
    const auto Graph = [](const char* Settings, const char* Nodes, const char* Outputs)
    {
        return M::Json::Parse(
            std::string(
                R"({"format":2,"domain":"MD_Surface","two_sided":false,"clip":0.3333,"tangent_space_normal":true,)") +
            Settings + R"(,"nodes":)" + Nodes + R"(,"outputs":)" + Outputs + "}");
    };
    // The expression class each rewrite makes, by name (not every one is exported from the engine).
    struct FCase
    {
        const TCHAR* Name;
        M::Json Source;
        M::Json Facts;
        const TCHAR* Expected;
        int32 CustomizedUVs;
    };
    const char* Rim =
        R"({"X":{"type":"ScalarParameter","inputs":{},"props":{"ParameterName":"Rim","DefaultValue":0.2}},
        "P":{"type":"ScalarParameter","inputs":{},"props":{"ParameterName":"Power","DefaultValue":2.0}},
        "Neg":{"type":"Multiply","inputs":{"A":["X",""],"B":["MinusOne",""]},"props":{}},"MinusOne":{"type":"Constant","inputs":{},"props":{"R":-1.0}},
        "NegP":{"type":"Multiply","inputs":{"A":["Neg",""],"B":["P",""]},"props":{}},"Sum":{"type":"Add","inputs":{"A":["NegP",""],"B":["P",""]},"props":{}},
        "Rimmed":{"type":"Add","inputs":{"A":["X",""],"B":["Sum",""]},"props":{}}})";
    const char* Scroll = R"({"UV":{"type":"TextureCoordinate","inputs":{},"props":{"CoordinateIndex":0}},
        "U":{"type":"ScalarParameter","inputs":{},"props":{"ParameterName":"U","DefaultValue":0.1}},"V":{"type":"ScalarParameter","inputs":{},"props":{"ParameterName":"V","DefaultValue":0.2}},
        "Speed":{"type":"AppendVector","inputs":{"A":["U",""],"B":["V",""]},"props":{}},"Time":{"type":"Time","inputs":{},"props":{}},
        "Step":{"type":"Multiply","inputs":{"A":["Time",""],"B":["Speed",""]},"props":{}},"Moved":{"type":"Add","inputs":{"A":["UV",""],"B":["Step",""]},"props":{}},
        "Sum":{"type":"DotProduct","inputs":{"A":["Moved",""],"B":["Moved",""]},"props":{}}})";
    const char* Reciprocal =
        R"({"A":{"type":"ScalarParameter","inputs":{},"props":{"ParameterName":"A","DefaultValue":1.0}},
        "X":{"type":"ScalarParameter","inputs":{},"props":{"ParameterName":"X","DefaultValue":2.0}},"One":{"type":"Constant","inputs":{},"props":{"R":1.0}},
        "R":{"type":"Divide","inputs":{"A":["One",""],"B":["X",""]},"props":{}},"M":{"type":"Multiply","inputs":{"A":["A",""],"B":["R",""]},"props":{}}})";
    const char* Instanced = R"({"UV":{"type":"TextureCoordinate","inputs":{},"props":{"CoordinateIndex":2}},
        "Sum":{"type":"DotProduct","inputs":{"A":["UV",""],"B":["UV",""]},"props":{}}})";
    // An instanced vertex program's texture coordinates 2 and 3: per-instance float 3 (0.25 without it) and 0.5.
    const char* Program =
        "MIN.S R1.x, R0, {3, 0, 0, 0};\nTXF.F R2.x, R1.x, handle(D0.y), BUFFER;\nELSE;\nMOV.F R2.x, {0.25, 0, 0, 0}.x;\nENDIF;\n"
        "MOV.F result.attrib[3].x, R2.x;\nMOV.F result.attrib[3].y, {0.5, 0, 0, 0}.x;\n";
    M::Json InstancedFacts = M::Json::MakeObject();
    InstancedFacts["instanced_program"] = M::Json(Program);
    InstancedFacts["instanced_outputs"] = M::Json(
        std::vector<M::Json>{M::Json(std::vector<M::Json>{M::Json("out_texcoord1"), M::Json(3)})});
    const FCase Cases[] = {
        {TEXT("Rim"),
            Graph(R"("blend_mode":"BLEND_Translucent","shading_model":"MSM_DefaultLit")", Rim,
                R"({"EmissiveColor":["Rimmed",""],"Opacity":["P",""]})"),
            M::Json(), TEXT("MaterialExpressionLinearInterpolate"), 0},
        {TEXT("Scroll"),
            Graph(
                R"("blend_mode":"BLEND_Translucent","shading_model":"MSM_DefaultLit","render_settings":{"C4MaterialParam3":"C4XC_Enable"})",
                Scroll, R"({"BaseColor":["Sum",""],"Opacity":["U",""]})"),
            M::Json(), TEXT("MaterialExpressionPanner"), 0},
        {TEXT("Reciprocal"),
            Graph(R"("blend_mode":"BLEND_Opaque","shading_model":"MSM_Unlit")", Reciprocal,
                R"({"EmissiveColor":["M",""]})"),
            M::Json::Parse(
                R"J({"uniform_set":{"vectors":[],"scalars":["A","X","(A / X)"],"textures":{},"scalar_defaults":{"A":1.0,"X":2.0},"vector_defaults":{}}})J"),
            TEXT("MaterialExpressionDivide"), 0},
        {TEXT("Instanced"),
            Graph(
                R"("blend_mode":"BLEND_Opaque","shading_model":"MSM_Unlit","render_settings":{"bUsedWithInstancedStaticMeshes":true})",
                Instanced, R"({"EmissiveColor":["Sum",""]})"),
            InstancedFacts, TEXT("MaterialExpressionPerInstanceCustomData"), 3},
    };
    TArray<TStrongObjectPtr<UMaterial>> Built;
    for (const FCase& Case : Cases)
    {
        const FString Package = FString(TEXT("/Game/P4MapKitMaterialGameForm/")) + Case.Name;
        if (!TestTrue(FString(Case.Name) + TEXT(": the unsaved test namespace is empty"),
                !FPackageName::DoesPackageExist(Package)))
            return false;
        M::Json Form;
        try
        {
            Form = M::CanonMaterialGraph(Case.Source, M::MaterialCanonGameFromJson(Case.Facts));
        }
        catch (const std::exception& Error)
        {
            AddError(FString(Case.Name) + TEXT(": ") + UTF8_TO_TCHAR(Error.what()));
            continue;
        }
        FString Error;
        TStrongObjectPtr<UMaterial> Material(
            P4BuildMaterialFromGraph(UTF8_TO_TCHAR(Form.Dump().c_str()), Package, Error));
        if (!TestTrue(FString(Case.Name) + TEXT(": the graph in the game's form builds"), Material.IsValid()))
        {
            AddError(Error);
            continue;
        }
        bool Found = false, XluCut = false;
        for (UMaterialExpression* Expression : Material->Expressions)
        {
            Found |= Expression && Expression->GetClass()->GetName() == Case.Expected;
            if (const auto* Custom = Cast<UMaterialExpressionKadanGameCustom>(Expression))
                XluCut |= Custom->Code.Contains(TEXT("C4GlobalClippingPlane"));
        }
        TestTrue(FString(Case.Name) + TEXT(": the rewrite's node is in the material"), Found);
        if (FCString::Strcmp(Case.Name, TEXT("Scroll")) == 0)
            TestTrue(TEXT("Scroll: the game's XluCut clips on the AO pin"),
                XluCut && Material->AmbientOcclusion.Expression &&
                    Material->AmbientOcclusion.Expression->IsA(UMaterialExpressionKadanGameCustom::StaticClass()));
        TestEqual(FString(Case.Name) + TEXT(": customized UVs"), int32(Material->NumCustomizedUVs), Case.CustomizedUVs);
        Built.Add(Material);
    }
    GShaderCompilingManager->FinishAllCompilation();
    for (const TStrongObjectPtr<UMaterial>& Material : Built)
    {
        FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIFeatureLevel);
        const FString Name = Material->GetName();
        if (!TestNotNull(Name + TEXT(": a material resource"), Resource))
            continue;
        if (Resource->GetCompileErrors().Num())
            AddError(Name + TEXT(" does not compile: ") + Resource->GetCompileErrors()[0]);
        TestTrue(Name + TEXT(": a shader map"), Resource->HasValidGameThreadShaderMap());
    }
    for (const TStrongObjectPtr<UMaterial>& Material : Built)
    {
        FAssetRegistryModule::AssetDeleted(Material.Get());
        Material->ClearFlags(RF_Public | RF_Standalone);
        if (Material->Rename(nullptr, GetTransientPackage(),
                REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders))
            Material->MarkPendingKill();
    }
    return true;
}
#endif

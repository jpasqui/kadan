#if P4_MAPKIT_TESTS
// Kadan's game scene reads (Game Scene Depth, Game Scene Color) compile for the game only in translucent and post
// process materials, as stock's Scene Depth and Scene Color: the game's opaque passes bind no scene textures, and an
// opaque material reading the scene depth crashed the game. Inside a material function the node cannot see the
// material, so the game compile checks the whole graph.
#include "P4ExportDriverTestShared.h"
#include "P4CreatorMaterialFactories.h"
#include "P4KadanMaterialExpressions.h"
#include "AssetToolsModule.h"
#include "P4NativeCustomCompilerBridge.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialFunction.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
template <typename T> T* Node(UMaterial* Graph)
{
    T* Made = NewObject<T>(Graph);
    Made->Material = Graph;
    Graph->Expressions.Add(Made);
    return Made;
}

// An unlit surface whose emissive is the scene depth, read directly or through a material function.
// A creator's material in the mod's folder, made as Kadan's material action makes one.
UMaterial* SceneDepthMaterial(const FString& Root, const TCHAR* Leaf, EBlendMode Blend, bool bInFunction,
    TArray<TStrongObjectPtr<UObject>>& Keep)
{
    auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    TStrongObjectPtr<UP4CreatorMaterialFactory> Factory(NewObject<UP4CreatorMaterialFactory>());
    UMaterial* Graph = Cast<UMaterial>(Tools.CreateAsset(Leaf, Root, UMaterial::StaticClass(), Factory.Get()));
    if (!Graph)
        return nullptr;
    Keep.Emplace(Graph);
    Graph->MaterialDomain = MD_Surface;
    Graph->BlendMode = Blend;
    Graph->SetShadingModel(MSM_Unlit);
    if (IsTranslucentBlendMode(Blend))
    {
        auto* One = Node<UMaterialExpressionConstant>(Graph);
        One->R = 1.f;
        Graph->Opacity.Expression = One;
    }
    if (!bInFunction)
    {
        Graph->EmissiveColor.Expression = Node<UMaterialExpressionKadanSceneDepth>(Graph);
    }
    else
    {
        UMaterialFunction* Function = NewObject<UMaterialFunction>(GetTransientPackage(), MakeUniqueObjectName(
            GetTransientPackage(), UMaterialFunction::StaticClass(), TEXT("MF_KadanSceneRead")), RF_Transient);
        Keep.Emplace(Function);
        auto* Depth = NewObject<UMaterialExpressionKadanSceneDepth>(Function);
        auto* Output = NewObject<UMaterialExpressionFunctionOutput>(Function);
        Output->OutputName = TEXT("Depth");
        Output->A.Expression = Depth;
        Function->FunctionExpressions.Add(Depth);
        Function->FunctionExpressions.Add(Output);
        auto* Call = Node<UMaterialExpressionMaterialFunctionCall>(Graph);
        Call->SetMaterialFunction(Function);
        Graph->EmissiveColor.Expression = Call;
    }
    Graph->PostEditChange();
    return Graph;
}

FP4CreatorMaterialCompileResult Compile(UMaterial* Graph)
{
    if (!Graph)
    {
        FP4CreatorMaterialCompileResult None;
        None.Error = TEXT("the material was not made");
        return None;
    }
    FP4CreatorMaterialCompileRequest Request;
    Request.CreatorMaterial = Graph;
    Request.RuntimeObjectPath = Graph->GetPathName();
    return P4CompileCreatorMaterial(Request);
}
} //namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SceneReadRefusalTest, "P4MapKit.Materials.SceneReadRefusal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4SceneReadRefusalTest::RunTest(const FString&)
{
    if (!P4UseSwitchShaderRoute())
    {
        AddWarning(TEXT("P4MapKit test not run. Creator materials are exported through UE's shaders only."));
        return true;
    }
    using namespace P4ExportDriverTest;
    FP4ExportModRequest RequestData;
    FP4ModNames Names;
    if (!Request(*this, TEXT("kadan.scene.read"), TEXT("Scene read"), RequestData, Names))
        return false;
    TArray<TStrongObjectPtr<UObject>> Keep;
    const TCHAR* const Refusal = TEXT("Only translucent and post process materials can read the game's scene");
    {
        const FP4CreatorMaterialCompileResult Result =
            Compile(SceneDepthMaterial(Names.PackageRoot, TEXT("M_OpaqueDirect"), BLEND_Opaque, false, Keep));
        TestFalse(TEXT("opaque, read directly: refused"), Result.Succeeded);
        TestTrue(FString::Printf(TEXT("opaque, read directly: the reason (%s)"), *Result.Error),
            Result.Error.Contains(Refusal));
    }
    {
        const FP4CreatorMaterialCompileResult Result =
            Compile(SceneDepthMaterial(Names.PackageRoot, TEXT("M_OpaqueInFunction"), BLEND_Opaque, true, Keep));
        TestFalse(TEXT("opaque, read in a material function: refused"), Result.Succeeded);
        TestTrue(FString::Printf(TEXT("opaque, read in a material function: the reason (%s)"), *Result.Error),
            Result.Error.Contains(Refusal));
    }
    {
        const FP4CreatorMaterialCompileResult Result =
            Compile(SceneDepthMaterial(Names.PackageRoot, TEXT("M_TranslucentInFunction"), BLEND_Translucent, true, Keep));
        TestTrue(FString::Printf(TEXT("translucent, read in a material function: compiles (%s)"), *Result.Error),
            Result.Succeeded);
    }
    return true;
}
#endif

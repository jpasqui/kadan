#if P4_MAPKIT_TESTS
// Kadan's game scene reads (Game Scene Depth, Game Scene Color) compile for the game only in translucent and post
// process materials, as stock's Scene Depth and Scene Color: the game's opaque passes bind no scene textures, and an
// opaque material reading the scene depth crashed the game. The rule is the blend mode the material draws with, an
// instance's override included. Inside a material function the node cannot see the material, so the game compile
// checks the whole graph.
#include "P4ExportDriverTestShared.h"
#include "P4CreatorMaterialFactories.h"
#include "P4KadanMaterialExpressions.h"
#include "P4MaterialInstance.h"
#include "AssetToolsModule.h"
#include "P4NativeCustomCompilerBridge.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialInstanceConstant.h"
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

enum class ERead
{
    Depth,
    Color
};

// An unlit surface whose emissive is the scene depth or colour, read directly or through a material function.
// A creator's material in the mod's folder, made as Kadan's material action makes one.
UMaterial* SceneReadMaterial(const FString& Root, const TCHAR* Leaf, EBlendMode Blend, bool bInFunction, ERead Read,
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
    auto* One = Node<UMaterialExpressionConstant>(Graph);
    One->R = 1.f;
    Graph->Opacity.Expression = One;
    if (!bInFunction)
    {
        Graph->EmissiveColor.Expression = Read == ERead::Depth
            ? static_cast<UMaterialExpression*>(Node<UMaterialExpressionKadanSceneDepth>(Graph))
            : Node<UMaterialExpressionKadanSceneColor>(Graph);
    }
    else
    {
        UMaterialFunction* Function = NewObject<UMaterialFunction>(GetTransientPackage(), MakeUniqueObjectName(
            GetTransientPackage(), UMaterialFunction::StaticClass(), TEXT("MF_KadanSceneRead")), RF_Transient);
        Keep.Emplace(Function);
        UMaterialExpression* Scene = Read == ERead::Depth
            ? static_cast<UMaterialExpression*>(NewObject<UMaterialExpressionKadanSceneDepth>(Function))
            : NewObject<UMaterialExpressionKadanSceneColor>(Function);
        auto* Output = NewObject<UMaterialExpressionFunctionOutput>(Function);
        Output->OutputName = TEXT("Scene");
        Output->A.Expression = Scene;
        Function->FunctionExpressions.Add(Scene);
        Function->FunctionExpressions.Add(Output);
        auto* Call = Node<UMaterialExpressionMaterialFunctionCall>(Graph);
        Call->SetMaterialFunction(Function);
        Graph->EmissiveColor.Expression = Call;
    }
    Graph->PostEditChange();
    return Graph;
}

FP4CreatorMaterialCompileResult Compile(UMaterial* Graph, UMaterialInstanceConstant* Instance = nullptr)
{
    if (!Graph)
    {
        FP4CreatorMaterialCompileResult None;
        None.Error = TEXT("the material was not made");
        return None;
    }
    FP4CreatorMaterialCompileRequest Request;
    Request.CreatorMaterial = Graph;
    Request.CreatorInstance = Instance;
    Request.RuntimeObjectPath = (Instance ? static_cast<UObject*>(Instance) : Graph)->GetPathName();
    return P4CompileCreatorMaterial(Request);
}

// A creator instance of the material whose blend mode overrides the material's.
UMaterialInstanceConstant* BlendInstance(UMaterial* Graph, const FString& Package, EBlendMode Blend,
    TArray<TStrongObjectPtr<UObject>>& Keep, FString& Error)
{
    if (!Graph)
        return nullptr;
    TArray<FString> Missing;
    UMaterialInstanceConstant* Instance =
        P4BuildMaterialInstance(TEXT("{\"parent\":\"") + Graph->GetPathName() + TEXT("\"}"), Package, Missing, Error);
    if (!Instance)
        return nullptr;
    Keep.Emplace(Instance);
    Instance->BasePropertyOverrides.bOverride_BlendMode = true;
    Instance->BasePropertyOverrides.BlendMode = Blend;
    Instance->UpdateOverridableBaseProperties();
    return Instance;
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
    const auto Refused = [this, Refusal](const FString& What, const FP4CreatorMaterialCompileResult& Result)
    {
        TestFalse(What + TEXT(": refused"), Result.Succeeded);
        TestTrue(What + TEXT(": the reason (") + Result.Error + TEXT(")"), Result.Error.Contains(Refusal));
    };
    const auto Compiles = [this](const FString& What, const FP4CreatorMaterialCompileResult& Result)
    {
        TestTrue(What + TEXT(": compiles (") + Result.Error + TEXT(")"), Result.Succeeded);
    };
    const FString& Root = Names.PackageRoot;
    for (const ERead Read : {ERead::Depth, ERead::Color})
    {
        const FString Kind = Read == ERead::Depth ? TEXT("depth") : TEXT("color");
        const auto Leaf = [&Kind](const TCHAR* Name) { return FString(Name) + TEXT("_") + Kind; };
        const auto Say = [&Kind](const TCHAR* What) { return Kind + TEXT(", ") + What; };
        Refused(Say(TEXT("opaque, read directly")),
            Compile(SceneReadMaterial(Root, *Leaf(TEXT("M_OpaqueDirect")), BLEND_Opaque, false, Read, Keep)));
        Refused(Say(TEXT("opaque, read in a material function")),
            Compile(SceneReadMaterial(Root, *Leaf(TEXT("M_OpaqueInFunction")), BLEND_Opaque, true, Read, Keep)));
        Compiles(Say(TEXT("translucent, read directly")),
            Compile(SceneReadMaterial(Root, *Leaf(TEXT("M_TranslucentDirect")), BLEND_Translucent, false, Read, Keep)));
        Compiles(Say(TEXT("translucent, read in a material function")),
            Compile(SceneReadMaterial(Root, *Leaf(TEXT("M_TranslucentInFunction")), BLEND_Translucent, true, Read, Keep)));
        // The blend mode an instance draws with decides, not its parent's: an opaque parent's translucent instance
        // reads the scene, a translucent parent's opaque instance does not.
        FString Error;
        UMaterial* Opaque = SceneReadMaterial(Root, *Leaf(TEXT("M_OpaqueParent")), BLEND_Opaque, false, Read, Keep);
        UMaterialInstanceConstant* Translucent =
            BlendInstance(Opaque, Root / Leaf(TEXT("MI_TranslucentChild")), BLEND_Translucent, Keep, Error);
        if (TestNotNull(Say(TEXT("translucent instance made: ")) + Error, Translucent))
            Compiles(Say(TEXT("an opaque material's translucent instance")), Compile(Opaque, Translucent));
        UMaterial* Clear =
            SceneReadMaterial(Root, *Leaf(TEXT("M_TranslucentParent")), BLEND_Translucent, false, Read, Keep);
        UMaterialInstanceConstant* Solid =
            BlendInstance(Clear, Root / Leaf(TEXT("MI_OpaqueChild")), BLEND_Opaque, Keep, Error);
        if (TestNotNull(Say(TEXT("opaque instance made: ")) + Error, Solid))
            Refused(Say(TEXT("a translucent material's opaque instance")), Compile(Clear, Solid));
    }
    return true;
}
#endif

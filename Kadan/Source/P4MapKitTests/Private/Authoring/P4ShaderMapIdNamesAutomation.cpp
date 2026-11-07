#include "P4CreatorMaterialFactories.h"
#include "P4ModDescriptor.h"
#include "P4NativeCustomCompilerBridge.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
// A creator graph whose base colour is a tint times one scalar parameter of the given name.
UMaterial* NamedGraph(UP4CreatorMaterialFactory* Factory, const FString& Root, const TCHAR* Asset, const TCHAR* Scalar)
{
    UMaterial* Graph = Cast<UMaterial>(Factory->FactoryCreateNew(
        UMaterial::StaticClass(), CreatePackage(*(Root / Asset)), Asset, RF_Public | RF_Standalone, nullptr, GWarn));
    if (!Graph)
        return nullptr;
    UMaterialExpressionScalarParameter* Strength = NewObject<UMaterialExpressionScalarParameter>(Graph);
    Strength->ParameterName = Scalar;
    Strength->DefaultValue = 0.5f;
    UMaterialExpressionVectorParameter* Tint = NewObject<UMaterialExpressionVectorParameter>(Graph);
    Tint->ParameterName = TEXT("Tint");
    Tint->DefaultValue = FLinearColor(1.f, 0.5f, 0.25f, 1.f);
    UMaterialExpressionMultiply* Times = NewObject<UMaterialExpressionMultiply>(Graph);
    Times->A.Expression = Tint;
    Times->B.Expression = Strength;
    Graph->Expressions = {Strength, Tint, Times};
    Graph->BaseColor.Expression = Times;
    Graph->PostEditChange();
    return Graph;
}
} // namespace

// Two creator materials the same but for one scalar parameter's name compile to the same programs. Their maps
// still need different ids: the game reuses a loaded map by its id, and the second material would then look up its
// parameters in the first one's names and lose its instance values.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ShaderMapIdNames, "P4MapKit.Quality.ShaderMapIdNames",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ShaderMapIdNames::RunTest(const FString&)
{
    if (!P4UseSwitchShaderRoute())
    {
        AddWarning(TEXT("P4MapKit test not run. The map id is the one of the route through UE's shaders."));
        return true;
    }
    FP4ModDescriptor Mod;
    Mod.Id = TEXT("native.mapid.names.test");
    Mod.Name = TEXT("Map id names");
    Mod.Version = TEXT("1.0.0");
    FP4ModNames Names;
    FString Error;
    if (!TestTrue(TEXT("Fixture mod names"), P4ModDerivedNames(Mod, Names, Error)))
        return false;
    TStrongObjectPtr<UP4CreatorMaterialFactory> Factory(NewObject<UP4CreatorMaterialFactory>());
    TStrongObjectPtr<UMaterial> Alpha(NamedGraph(Factory.Get(), Names.PackageRoot, TEXT("M_Alpha"), TEXT("Alpha")));
    TStrongObjectPtr<UMaterial> Beta(NamedGraph(Factory.Get(), Names.PackageRoot, TEXT("M_Beta"), TEXT("Beta")));
    if (!TestTrue(TEXT("Both graphs are made"), Alpha.IsValid() && Beta.IsValid()))
        return false;
    FP4CreatorMaterialCompileRequest First;
    First.CreatorMaterial = Alpha.Get();
    FP4CreatorMaterialCompileRequest Second;
    Second.CreatorMaterial = Beta.Get();
    const FP4CreatorMaterialCompileResult A = P4CompileCreatorMaterial(First);
    const FP4CreatorMaterialCompileResult B = P4CompileCreatorMaterial(Second);
    if (!TestTrue(TEXT("Both graphs compile"), A.Succeeded && B.Succeeded))
    {
        AddError(A.Error + TEXT(" ") + B.Error);
        return false;
    }
    TestTrue(TEXT("The two graphs compile to the same programs"), A.MapHash == B.MapHash);
    TestTrue(TEXT("Both maps have an id"), A.MapId.size() == 20 && B.MapId.size() == 20);
    TestTrue(TEXT("Maps whose parameter names differ have different ids"), A.MapId != B.MapId);
    for (UMaterial* Graph : {Alpha.Get(), Beta.Get()})
    {
        Graph->ClearFlags(RF_Public | RF_Standalone);
        Graph->GetOutermost()->SetDirtyFlag(false);
        Graph->GetOutermost()->MarkPendingKill();
    }
    return !HasAnyErrors();
}

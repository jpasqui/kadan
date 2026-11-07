#include "P4TranslucentMaterialStaging.h"
#include "P4CreatorReferencePreflight.h"
#include "P4ModDescriptor.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Components/SpotLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/SpotLight.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4LightFunctionSlotsAutomation, "P4MapKit.Quality.LightFunctionMaterialSlots",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4LightFunctionSlotsAutomation::RunTest(const FString&)
{
    TStrongObjectPtr<UMaterial> Function(NewObject<UMaterial>());
    Function->MaterialDomain = MD_LightFunction;
    TStrongObjectPtr<UMaterial> Surface(NewObject<UMaterial>());
    TStrongObjectPtr<UMaterialInstanceConstant> Child(NewObject<UMaterialInstanceConstant>());
    Child->Parent = Function.Get();
    TStrongObjectPtr<USpotLightComponent> Spot(NewObject<USpotLightComponent>());
    TStrongObjectPtr<UDirectionalLightComponent> Sun(NewObject<UDirectionalLightComponent>());
    const TSet<UMaterialInterface*> Graphs{Function.Get(), Surface.Get()};
    const FString Root(TEXT("/Game/P4LightFunctionSlots"));
    FString Error;
    Spot->LightFunctionMaterial = Function.Get();
    Sun->LightFunctionMaterial = Child.Get();
    Spot->Mobility = EComponentMobility::Stationary;
    Spot->CastShadows = Spot->CastDynamicShadows = true;
    Sun->CastShadows = Sun->CastDynamicShadows = true;
    bool DirectionalFunctions = true;
    TestTrue(TEXT("An authored spot accepts a creator light function master"),
        FP4TranslucentMaterialStaging::PreflightReferences(Spot.Get(), Graphs, Root, Error, &DirectionalFunctions));
    TestFalse(TEXT("A spot does not request the directional channel correction"), DirectionalFunctions);
    TestTrue(TEXT("An authored directional light accepts a creator light function instance"),
        FP4TranslucentMaterialStaging::PreflightReferences(Sun.Get(), Graphs, Root, Error, &DirectionalFunctions));
    TestTrue(TEXT("A directional instance requests the channel correction even though only its parent is remapped"),
        DirectionalFunctions);
    TestTrue(TEXT("Review preserves both editable light references"),
        Spot->LightFunctionMaterial == Function.Get() && Sun->LightFunctionMaterial == Child.Get());
    Spot->CastDynamicShadows = false;
    TestFalse(TEXT("A stationary function cannot silently omit its dynamic shadow pass"),
        FP4TranslucentMaterialStaging::PreflightReferences(Spot.Get(), Graphs, Root, Error));
    TestTrue(TEXT("The stationary refusal names the setting to turn on"), Error.Contains(TEXT("Cast Dynamic Shadows")));
    Spot->CastDynamicShadows = true;
    Spot->CastShadows = false;
    TestFalse(TEXT("Cast Shadows is also required for a stationary function"),
        FP4TranslucentMaterialStaging::PreflightReferences(Spot.Get(), Graphs, Root, Error));
    Spot->Mobility = EComponentMobility::Movable;
    Spot->CastDynamicShadows = false;
    TestTrue(TEXT("The qualified shadowless movable spot remains accepted"),
        FP4TranslucentMaterialStaging::PreflightReferences(Spot.Get(), Graphs, Root, Error));
    Spot->Mobility = EComponentMobility::Stationary;
    Spot->CastShadows = Spot->CastDynamicShadows = true;
    Sun->CastDynamicShadows = false;
    TestFalse(TEXT("The directional channel correction requires a shadow-casting light"),
        FP4TranslucentMaterialStaging::PreflightReferences(Sun.Get(), Graphs, Root, Error));
    Sun->CastDynamicShadows = true;
    Spot->LightFunctionMaterial = Surface.Get();
    TestFalse(TEXT("A surface graph cannot be exported as a light function"),
        FP4TranslucentMaterialStaging::PreflightReferences(Spot.Get(), Graphs, Root, Error));
    TestTrue(TEXT("The refusal names the required domain"), Error.Contains(TEXT("Light Function domain")));
    // The live light reference must enter the dependency closure before cooking, even when
    // the actor is the only seed and the asset registry has never seen its assignment.
    const FString ForeignPackage = TEXT("/Game/Mods/foreign_light_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) +
        TEXT("/M_Function");
    TStrongObjectPtr<UMaterial> Foreign(NewObject<UMaterial>(CreatePackage(*ForeignPackage), TEXT("M_Function")));
    Foreign->MaterialDomain = MD_LightFunction;
    TStrongObjectPtr<ASpotLight> Actor(NewObject<ASpotLight>());
    Actor->GetLightComponent()->LightFunctionMaterial = Foreign.Get();
    FP4ModDescriptor Mod;
    Mod.Id = TEXT("native.light.slots.test");
    Mod.Name = TEXT("Light slots");
    Mod.Version = TEXT("1.0.0");
    FP4CreatorReferenceInputs Inputs;
    Inputs.AddedActors.Add(Actor.Get());
    TArray<FString> Closure;
    TestFalse(TEXT("A live light cannot silently lose its foreign material dependency"),
        P4PreflightCreatorReferences(Mod, Inputs, nullptr, {}, true, Closure, Error));
    TestTrue(TEXT("The early refusal identifies the foreign light material"),
        Error.Contains(ForeignPackage) && Error.Contains(TEXT("another mod")));
    Foreign->GetOutermost()->SetDirtyFlag(false);
    return !HasAnyErrors();
}
#endif

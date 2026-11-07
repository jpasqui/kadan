#if P4_MAPKIT_TESTS
#include "P4LightCookDefaults.h"
#include "Components/PointLightComponent.h"
#include "Engine/SpotLight.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4LightCookDefaultsTest, "P4MapKit.Quality.LightCookDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4LightCookDefaultsTest::RunTest(const FString&)
{
    TStrongObjectPtr<ASpotLight> Actor(NewObject<ASpotLight>());
    UPointLightComponent* Light = CastChecked<UPointLightComponent>(Actor->GetLightComponent());
    UPointLightComponent* Archetype = CastChecked<UPointLightComponent>(Light->GetArchetype());
    UPointLightComponent* ClassDefault = CastChecked<UPointLightComponent>(Light->GetClass()->GetDefaultObject());
    const bool Before = Archetype->bUseInverseSquaredFalloff;
    const bool ClassBefore = ClassDefault->bUseInverseSquaredFalloff;
    ON_SCOPE_EXIT
    {
        Archetype->bUseInverseSquaredFalloff = Before;
        ClassDefault->bUseInverseSquaredFalloff = ClassBefore;
    };
    Archetype->bUseInverseSquaredFalloff = true;
    ClassDefault->bUseInverseSquaredFalloff = true;
    Light->bUseInverseSquaredFalloff = true;
    const bool Dirty = Archetype->GetOutermost()->IsDirty();
    {
        FP4LightCookDefaults Defaults(Actor.Get());
        TestFalse(TEXT("The cook compares against the game's non-inverse-square default"),
            Archetype->bUseInverseSquaredFalloff);
        TestFalse(
            TEXT("Standalone component templates also use the game default"), ClassDefault->bUseInverseSquaredFalloff);
        TestTrue(
            TEXT("The authored true value remains true, so native save emits it"), Light->bUseInverseSquaredFalloff);
        {
            FP4LightCookDefaults Nested(Actor.Get());
            TestFalse(TEXT("Nested cooking also sees the game default"), Archetype->bUseInverseSquaredFalloff);
        }
        TestFalse(TEXT("Nested cooking restores the outer cook's default"), Archetype->bUseInverseSquaredFalloff);
    }
    TestTrue(TEXT("The editor's native archetype is restored"), Archetype->bUseInverseSquaredFalloff);
    TestTrue(TEXT("The editor's component class default is restored"), ClassDefault->bUseInverseSquaredFalloff);
    TestEqual(TEXT("Cooking does not dirty the native archetype package"), Archetype->GetOutermost()->IsDirty(), Dirty);
    TestTrue(TEXT("Cooking preserves the authored component"), Light->bUseInverseSquaredFalloff);
    return !HasAnyErrors();
}
#endif

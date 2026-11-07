#include "P4CreatorMaterialFactories.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Factories/MaterialFactoryNew.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CreatorMaterialFactoryLifecycleTest,
    "P4MapKit.Kadan.CreatorMaterialFactoryLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CreatorMaterialFactoryLifecycleTest::RunTest(const FString&)
{
    // Exercise normal unload/reload without broadcasting the process-wide engine exit.
    // The real command-line run's teardown separately exercises the registered early-exit hook.
    P4ShutdownCreatorMaterialFactories();
    ON_SCOPE_EXIT
    {
        P4InitializeCreatorMaterialFactories();
    };
    auto* Material = Cast<UMaterialFactoryNew>(UMaterialFactoryNew::StaticClass()->GetDefaultObject(false));
    auto* Instance = Cast<UMaterialInstanceConstantFactoryNew>(
        UMaterialInstanceConstantFactoryNew::StaticClass()->GetDefaultObject(false));
    auto* Flag = FindFProperty<FBoolProperty>(UFactory::StaticClass(), TEXT("bCreateNew"));
    if (!TestNotNull(TEXT("Existing native material default remains alive"), Material) ||
        !TestNotNull(TEXT("Existing native instance default remains alive"), Instance) ||
        !TestNotNull(TEXT("Native create-new menu flag is reflected"), Flag))
        return false;
    const bool MaterialOriginal = Flag->GetPropertyValue_InContainer(Material);
    const bool InstanceOriginal = Flag->GetPropertyValue_InContainer(Instance);
    P4InitializeCreatorMaterialFactories();
    TestFalse(TEXT("Native material menu is replaced while installed"), Flag->GetPropertyValue_InContainer(Material));
    TestFalse(TEXT("Native instance menu is replaced while installed"), Flag->GetPropertyValue_InContainer(Instance));
    P4InitializeCreatorMaterialFactories();
    P4ShutdownCreatorMaterialFactories();
    TestEqual(TEXT("Hot unload restores the material default's captured flag"),
        Flag->GetPropertyValue_InContainer(Material), MaterialOriginal);
    TestEqual(TEXT("Hot unload restores the instance default's captured flag"),
        Flag->GetPropertyValue_InContainer(Instance), InstanceOriginal);
    P4ShutdownCreatorMaterialFactories();
    TestTrue(TEXT("Repeated shutdown uses the same existing material default"),
        UMaterialFactoryNew::StaticClass()->GetDefaultObject(false) == Material);
    TestTrue(TEXT("Repeated shutdown uses the same existing instance default"),
        UMaterialInstanceConstantFactoryNew::StaticClass()->GetDefaultObject(false) == Instance);
    TestEqual(TEXT("Repeated shutdown preserves restored material flag"), Flag->GetPropertyValue_InContainer(Material),
        MaterialOriginal);
    TestEqual(TEXT("Repeated shutdown preserves restored instance flag"), Flag->GetPropertyValue_InContainer(Instance),
        InstanceOriginal);
    return !HasAnyErrors();
}
#endif

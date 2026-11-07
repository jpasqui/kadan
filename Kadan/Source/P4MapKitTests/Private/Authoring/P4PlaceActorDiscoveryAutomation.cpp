#include "P4PlaceActorDiscovery.h"
#include "Animation/AnimInstance.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ActorRegistryParentTest, "P4MapKit.Editor.PlaceActorsNativeParent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ActorRegistryParentTest::RunTest(const FString&)
{
    // The callback is the exact GetAsset call site used by registry discovery.
    int32 AssetLoads = 0;
    const auto Load = [&AssetLoads]() { ++AssetLoads; };
    for (UClass* NonActor : {UAnimInstance::StaticClass(), UObject::StaticClass()})
    {
        const FString Tag = TEXT("Class'") + NonActor->GetPathName() + TEXT("'");
        TestFalse(TEXT("animation/data parent never calls GetAsset"), P4VisitLoadedActorBlueprintParent(Tag, Load));
    }
    TestFalse(TEXT("an unregistered parent is not inferred or loaded"),
        P4VisitLoadedActorBlueprintParent(TEXT("Class'/Script/__KadanMissing.Actor'"), Load));
    TestEqual(TEXT("all nonactor and unknown parents were skipped without asset loading"), AssetLoads, 0);
    TestTrue(TEXT("already registered actor export-text path reaches explicit discovery"),
        P4VisitLoadedActorBlueprintParent(TEXT("Class'") + AActor::StaticClass()->GetPathName() + TEXT("'"), Load));
    TestEqual(TEXT("only the actor parent reaches the asset loader"), AssetLoads, 1);
    return true;
}
#endif

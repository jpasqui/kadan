#include "P4BlueprintSubobjectStorage.h"
#include "Components/PostProcessComponent.h"
#include "Components/SceneComponent.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4BlueprintPostProcessSourceStorageIdentity,"P4Blueprints.PostProcessSourceStorageIdentity",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FP4BlueprintPostProcessSourceStorageIdentity::RunTest(const FString&)
{
    UClass* Parent=UPostProcessComponent::StaticClass();
    UClass* Storage=LoadClass<UPostProcessComponent>(nullptr,TEXT("/Script/P4StubsCore.P4RecoveredPostProcessComponent"));
    if(!TestNotNull(TEXT("Requires the actual typed PP SDK child"),Storage))return false;
    TStrongObjectPtr<UPostProcessComponent> Instance(NewObject<UPostProcessComponent>(GetTransientPackage(),Storage));
    TestTrue(TEXT("Actual child constructs with the original native pointer type"),Instance.IsValid());
    TestTrue(TEXT("Actual existing child admits its exact original source class"),P4BlueprintSourceSubobjectClassMatches(Storage,Parent));
    TestTrue(TEXT("Stock exact source class remains admitted"),P4BlueprintSourceSubobjectClassMatches(Parent,Parent));
    TestFalse(TEXT("Known child cannot pose as a more distant source base"),P4BlueprintSourceSubobjectClassMatches(Storage,USceneComponent::StaticClass()));
    TestFalse(TEXT("Stock base cannot pose as the storage child"),P4BlueprintSourceSubobjectClassMatches(Parent,Storage));
    TestFalse(TEXT("Missing actual class remains refused"),P4BlueprintSourceSubobjectClassMatches(nullptr,Parent));
    TStrongObjectPtr<UClass> Other(NewObject<UClass>(GetTransientPackage(),NAME_None,RF_Transient));
    Other->SetSuperStruct(Parent);Other->ClassFlags=CLASS_Native;
    TestTrue(TEXT("Negative control really derives from the source base"),Other->IsChildOf(Parent));
    TestFalse(TEXT("Unrelated native subclass is not recovered storage"),P4BlueprintSourceSubobjectClassMatches(Other.Get(),Parent));
    return !HasAnyErrors();
}
#endif
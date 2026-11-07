#include "P4BlueprintSubobjectStorage.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4BlueprintSourceSubobjectStorageIdentity, "P4Blueprints.SourceSubobjectStorageIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4BlueprintSourceSubobjectStorageIdentity::RunTest(const FString&)
{
    UClass* Parent = UDirectionalLightComponent::StaticClass();
    TestTrue(TEXT("Unchanged exact native classes retain existing source admission"), P4BlueprintSourceSubobjectClassMatches(Parent, Parent));
    TestFalse(TEXT("Absent actual class is refused"), P4BlueprintSourceSubobjectClassMatches(nullptr, Parent));
    TestFalse(TEXT("Absent source class is refused"), P4BlueprintSourceSubobjectClassMatches(Parent, nullptr));
    TestFalse(TEXT("Other native light classes remain incompatible"), P4BlueprintSourceSubobjectClassMatches(UPointLightComponent::StaticClass(), Parent));
    TestFalse(TEXT("A native subclass does not grant broad source-base acceptance"), P4BlueprintSourceSubobjectClassMatches(Parent, ULightComponent::StaticClass()));
    TStrongObjectPtr<UClass> Other(NewObject<UClass>(GetTransientPackage(), NAME_None, RF_Transient));
    Other->SetSuperStruct(Parent);
    Other->ClassFlags = CLASS_Native;
    TestTrue(TEXT("Negative control really is a directional-light subclass"), Other->IsChildOf(Parent));
    TestFalse(TEXT("An unrelated subclass cannot pose as original engine storage"), P4BlueprintSourceSubobjectClassMatches(Other.Get(), Parent));
    UClass* Storage = LoadClass<UDirectionalLightComponent>(nullptr, TEXT("/Script/P4StubsCore.P4RecoveredDirectionalLightComponent"));
    if (!TestNotNull(TEXT("Positive native control requires the rebuilt original light SDK"), Storage)) return false;
    TStrongObjectPtr<UObject> NativeChild(NewObject<UObject>(GetTransientPackage(), Storage));
    if (!TestNotNull(TEXT("Real typed child constructs normally"), NativeChild.Get())) return false;
    TestTrue(TEXT("Actual existing native child matches its exact original source base"),
        P4BlueprintSourceSubobjectClassMatches(NativeChild->GetClass(), Parent));
    TestFalse(TEXT("Known storage identity cannot stand in for another light source class"),
        P4BlueprintSourceSubobjectClassMatches(Storage, UPointLightComponent::StaticClass()));
    TestFalse(TEXT("Known storage identity cannot stand in for a more distant native base"),
        P4BlueprintSourceSubobjectClassMatches(Storage, ULightComponent::StaticClass()));
    return !HasAnyErrors();
}
#endif

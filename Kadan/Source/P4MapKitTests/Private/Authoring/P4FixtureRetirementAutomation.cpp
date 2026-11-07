#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace P4FixtureRetirement { bool Package(UPackage* Made); }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4FixtureBlueprintRetirementTest,
    "P4MapKit.Kadan.RefusedBlueprintRetirementPreservesClassAncestry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4FixtureBlueprintRetirementTest::RunTest(const FString&)
{
    const FString Base = TEXT("/Game/P4RetirementTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UPackage* ParentPackage = CreatePackage(*(Base + TEXT("Parent")));
    UBlueprint* Parent = FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), ParentPackage, TEXT("Parent"),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    if (!TestNotNull(TEXT("Create the refused parent Blueprint"), Parent)) return false;
    UClass* ParentClass = Parent->GeneratedClass;
    UPackage* ChildPackage = CreatePackage(*(Base + TEXT("Child")));
    UBlueprint* Child = FKismetEditorUtilities::CreateBlueprint(ParentClass, ChildPackage, TEXT("Child"),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    if (!TestNotNull(TEXT("Create a dependent child Blueprint"), Child)) return false;
    UClass* ChildClass = Child->GeneratedClass;
    TStrongObjectPtr<UObject> Instance(NewObject<UObject>(GetTransientPackage(), ChildClass));
    TestTrue(TEXT("Retire the refused parent package"), P4FixtureRetirement::Package(ParentPackage));
    // Pending-kill classes are nulled by reference collection even when a live child needs
    // them. DestroyPersistentUberGraphFrame unconditionally walks that superclass chain.
    TestFalse(TEXT("Retirement must not force-kill class ancestry needed by dependent instances"), ParentClass->IsPendingKill());
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    TestEqual(TEXT("A retained child keeps its parent through collection"), ChildClass->GetSuperClass(), ParentClass);
    TestTrue(TEXT("Retire the dependent child package"), P4FixtureRetirement::Package(ChildPackage));
    Instance.Reset();
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    return !HasAnyErrors();
}
#endif

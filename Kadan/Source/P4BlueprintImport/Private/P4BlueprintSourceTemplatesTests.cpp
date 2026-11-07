#include "P4BlueprintSourceTemplates.h"
#include "P4BlueprintTestSeams.h"
#include "P4BlueprintGraphs.h"
#include "P4BlueprintImport.h"
#include "Components/SceneComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
namespace P4FixtureRetirement { bool Package(UPackage*); }
namespace
{
struct FTemplateFixture
{
    TStrongObjectPtr<UPackage> Parent{CreatePackage(*(TEXT("/Game/P4BlueprintProof/TemplateParent_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)))};
    TStrongObjectPtr<UPackage> Child{CreatePackage(*(TEXT("/Game/P4BlueprintProof/TemplateChild_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)))};
    ~FTemplateFixture()
    {
        UPackage* ChildPackage = Child.Get(); Child.Reset(); P4FixtureRetirement::Package(ChildPackage);
        UPackage* ParentPackage = Parent.Get(); Parent.Reset(); P4FixtureRetirement::Package(ParentPackage);
    }
};
TSharedPtr<FJsonValue> SourceValue(const TCHAR* Name, const FString& Text)
{
    auto Out = MakeShared<FJsonObject>(); Out->SetStringField(TEXT("name"), Name);
    Out->SetNumberField(TEXT("index"), 0); Out->SetStringField(TEXT("text"), Text);
    return MakeShared<FJsonValueObject>(Out);
}
TSharedPtr<FJsonObject> SourceUnit(UClass* Class, const FString& Reference)
{
    auto Out = MakeShared<FJsonObject>(); Out->SetStringField(TEXT("cls"), Class->GetPathName());
    Out->SetStringField(TEXT("flags"), TEXT("0x29")); Out->SetStringField(TEXT("ref"), Reference);
    Out->SetArrayField(TEXT("path"), {MakeShared<FJsonValueString>(TEXT("OriginalUnit_0"))});
    Out->SetArrayField(TEXT("values"), {SourceValue(TEXT("bSendRTPCMoveSpeedVerticalValue"), TEXT("True"))});
    return Out;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4BlueprintSourceSubobjectFlagsTest, "P4Blueprints.SourceSubobjectFlags",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4BlueprintSourceSubobjectFlagsTest::RunTest(const FString&)
{
	P4_REQUIRE_BLUEPRINT_TEST_SEAMS();
    UClass* Handler = LoadObject<UClass>(nullptr, TEXT("/Script/Carrot4.CakEventHandlerComponent"));
    UClass* UnitClass = LoadObject<UClass>(nullptr, TEXT("/Script/Carrot4.CakMoveSpeedControlUnit"));
    if (!TestTrue(TEXT("Real instanced unit declarations are available"), Handler && UnitClass)) return false;
    FTemplateFixture Fixture;
    TStrongObjectPtr<UObject> Owner(NewObject<UObject>(Fixture.Parent.Get(), Handler, TEXT("Handler"), RF_ArchetypeObject | RF_Public));
    auto Record = MakeShared<FJsonObject>();
    const FString Reference = Owner->GetPathName() + TEXT(".OriginalUnit_0");
    auto Unit = SourceUnit(UnitClass, Reference);
    Record->SetArrayField(TEXT("subobjects"), {MakeShared<FJsonValueObject>(Unit)});
    Record->SetArrayField(TEXT("values"), {SourceValue(TEXT("Unit_0"), Reference)});
    const FString Report = P4BlueprintApplySourceValuesForTest(Owner.Get(), Record);
    UObject* Child = StaticFindObjectFast(nullptr, Owner.Get(), TEXT("OriginalUnit_0"));
    if (!TestNotNull(TEXT("The source child is constructed"), Child)) return false;
    TestEqual(TEXT("The original source template flags survive construction"),
        uint32(Child->GetMaskedFlags(RF_Public | RF_Transactional | RF_ArchetypeObject | RF_DefaultSubObject)), uint32(0x29));
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("The native instanced Unit_0 property retains the child through collection"), IsValid(Child));
    Unit->SetStringField(TEXT("flags"), TEXT("0x8"));
    P4BlueprintApplySourceValuesForTest(Owner.Get(), Record);
    TestEqual(TEXT("Reimport updates the existing child using the new source flags"),
        uint32(Child->GetMaskedFlags(RF_Public | RF_Transactional | RF_ArchetypeObject | RF_DefaultSubObject)), uint32(0x8));
    TestFalse(TEXT("Source flags do not make up a class default or loading flag"), Child->HasAnyFlags(RF_ClassDefaultObject | RF_NeedLoad | RF_NeedPostLoad));
    TestFalse(TEXT("The successful original source record has no refusal"), Report.Contains(TEXT("incompatible")));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4BlueprintSourceInheritedTemplateTest, "P4Blueprints.SourceInheritedTemplateIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4BlueprintSourceInheritedTemplateTest::RunTest(const FString&)
{
    UClass* HandlerClass = LoadObject<UClass>(nullptr, TEXT("/Script/Carrot4.CakEventHandlerComponent"));
    UClass* UnitClass = LoadObject<UClass>(nullptr, TEXT("/Script/Carrot4.CakMoveSpeedControlUnit"));
    if (!TestTrue(TEXT("Use the game's native instanced handler and unit"), HandlerClass && UnitClass)) return false;
    FTemplateFixture Fixture;
    TStrongObjectPtr<UBlueprint> Parent(FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Fixture.Parent.Get(),
        TEXT("Parent"), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
    USCS_Node* Node = Parent->SimpleConstructionScript->CreateNode(HandlerClass, TEXT("Handler"));
    Parent->SimpleConstructionScript->AddNode(Node);
    UObject* ParentUnit = NewObject<UObject>(Node->ComponentTemplate, UnitClass, TEXT("ParentUnit_1"), RF_Public | RF_ArchetypeObject | RF_Transactional);
    FObjectPropertyBase* Slot = FindFProperty<FObjectPropertyBase>(HandlerClass, TEXT("Unit_0"));
    FBoolProperty* Speed = FindFProperty<FBoolProperty>(UnitClass, TEXT("bSendRTPCMoveSpeedVerticalValue"));
    if (!TestTrue(TEXT("The actual handler and unit fields are present"), Slot && Speed)) return false;
    Speed->SetPropertyValue_InContainer(ParentUnit, true);
    Slot->SetObjectPropertyValue_InContainer(Node->ComponentTemplate, ParentUnit);
    FKismetEditorUtilities::CompileBlueprint(Parent.Get());
    TStrongObjectPtr<UBlueprint> Child(FKismetEditorUtilities::CreateBlueprint(Parent->GeneratedClass, Fixture.Child.Get(),
        TEXT("Child"), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
    Node = Parent->SimpleConstructionScript->FindSCSNode(TEXT("Handler"));
    if (!TestNotNull(TEXT("The parent component survives compilation"), Node)) return false;
    auto Override = MakeShared<FJsonObject>();
    Override->SetStringField(TEXT("variable"), TEXT("Handler"));
    Override->SetStringField(TEXT("owner"), Parent->GeneratedClass->GetPathName());
    Override->SetStringField(TEXT("cls"), HandlerClass->GetPathName());
    const FString Reference = Child->GeneratedClass->GetPathName() + TEXT(":Handler_GEN_VARIABLE.OriginalUnit_0");
    Override->SetArrayField(TEXT("subobjects"), {MakeShared<FJsonValueObject>(SourceUnit(UnitClass, Reference))});
    Override->SetArrayField(TEXT("values"), {SourceValue(TEXT("Unit_0"), Reference)});
    auto Plan = MakeShared<FJsonObject>();
    Plan->SetArrayField(TEXT("overrides"), {MakeShared<FJsonValueObject>(Override)});
    P4RememberBlueprintSourceTemplates(Child.Get(), Plan);
    P4Graphs::FReport Made;
    P4RestoreBlueprintSourceTemplates(Child.Get(), Made);
    TestEqual(TEXT("Original source reconstruction reports no missing fields/classes"), Made.Problems.Num(), 0);
    // The two units have the same property values but different source-owned names. Stock deep
    // comparison prunes this override; it must not change the child's archetype on the next load.
    for (int32 Round = 0; Round < 2; ++Round)
    {
        FKismetEditorUtilities::CompileBlueprint(Child.Get(), EBlueprintCompileOptions::SkipGarbageCollection);
        CollectGarbage(RF_NoFlags);
        UInheritableComponentHandler* Handler = Child->GetInheritableComponentHandler(false);
        UActorComponent* Template = Handler ? Handler->GetOverridenComponentTemplate(FComponentKey(Node)) : nullptr;
        if (!TestNotNull(TEXT("The original override identity survives compiler cleanup and GC"), Template)) return false;
        UObject* Unit = Slot->GetObjectPropertyValue_InContainer(Template);
        if (!TestNotNull(TEXT("The original named instanced child stays referenced"), Unit)) return false;
        TestEqual(TEXT("The original child's name is retained rather than the parent's alternate name"), Unit->GetName(), FString(TEXT("OriginalUnit_0")));
        TestTrue(TEXT("It remains a source template owned by the generated component template"),
            Unit->GetOuter() == Template && Unit->HasAnyFlags(RF_ArchetypeObject) && !Unit->HasAnyFlags(RF_Transient));
        TStrongObjectPtr<AActor> Actor(NewObject<AActor>(Fixture.Child.Get(), Child->GeneratedClass,
            FName(*FString::Printf(TEXT("ActorInstance%d"), Round)), RF_Transactional));
        TStrongObjectPtr<UObject> Instance(NewObject<UObject>(Actor.Get(), HandlerClass, TEXT("Handler"), RF_Transactional, Template));
        UObject* InstanceUnit = Slot->GetObjectPropertyValue_InContainer(Instance.Get());
        TestTrue(TEXT("An actual handler instance inherits the original nested template"),
            InstanceUnit && InstanceUnit != Unit && InstanceUnit->GetOuter() == Instance.Get() && InstanceUnit->GetArchetype() == Unit);
    }
    return !HasAnyErrors();
}
#endif

#include "P4BlueprintImport.h"
#include "P4BlueprintTestSeams.h"
#include "Dom/JsonObject.h"
#include "Components/SceneComponent.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
namespace P4FixtureRetirement { bool Package(UPackage*); }
namespace
{
struct FTransientFixture
{
    UPackage* Package = CreatePackage(*(TEXT("/Game/P4BlueprintProof/Transient_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    ~FTransientFixture() { P4FixtureRetirement::Package(Package); }
};
TSharedPtr<FJsonValue> TransientSourceValue(const TCHAR* Name, const TCHAR* Text)
{
    auto Value = MakeShared<FJsonObject>();
    Value->SetStringField(TEXT("name"), Name); Value->SetNumberField(TEXT("index"), 0);
    Value->SetStringField(TEXT("text"), Text); return MakeShared<FJsonValueObject>(Value);
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4BlueprintTransientSourceTest, "P4Blueprints.TransientSourceBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4BlueprintTransientSourceTest::RunTest(const FString&)
{
	P4_REQUIRE_BLUEPRINT_TEST_SEAMS();
    UClass* Parent = LoadObject<UClass>(nullptr, TEXT("/Script/Carrot4.CakEventHandlerComponent"));
    UClass* Unit = LoadObject<UClass>(nullptr, TEXT("/Script/Carrot4.CakUnit"));
    UClass* ConcreteUnit = LoadObject<UClass>(nullptr, TEXT("/Script/Carrot4.CakMoveSpeedControlUnit"));
    if (!TestTrue(TEXT("Use genuine native handler and concrete unit declarations"), Parent && Unit && ConcreteUnit)) return false;
    FTransientFixture Fixture;
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(Parent, Fixture.Package, TEXT("TransientHandler"), BPTYPE_Normal,
        UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    if (!TestNotNull(TEXT("The native factory makes the private component Blueprint"), Blueprint)) return false;
    FEdGraphPinType SlotsType; SlotsType.PinCategory = UEdGraphSchema_K2::PC_Int; SlotsType.ContainerType = EPinContainerType::Array;
    FEdGraphPinType TextType; TextType.PinCategory = UEdGraphSchema_K2::PC_String;
    FEdGraphPinType ObjectType; ObjectType.PinCategory = UEdGraphSchema_K2::PC_Object; ObjectType.PinSubCategoryObject = Unit;
    FEdGraphPinType ActorType; ActorType.PinCategory = UEdGraphSchema_K2::PC_Object; ActorType.PinSubCategoryObject = AActor::StaticClass();
    if (!TestTrue(TEXT("Create real typed transient fixture fields"),
        FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(TEXT("TransientSlots")), SlotsType) &&
        FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(TEXT("TransientSentinel")), TextType) &&
        FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(TEXT("TransientOwned")), ObjectType) &&
        FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(TEXT("SavedActor")), ActorType) &&
        FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(TEXT("EmptyActor")), ActorType))) return false;
    for (FBPVariableDescription& Variable : Blueprint->NewVariables)
        if (Variable.VarName.ToString().StartsWith(TEXT("Transient"))) Variable.PropertyFlags |= CPF_Transient;
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    UObject* Defaults = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr;
    FArrayProperty* Slots = Defaults ? FindFProperty<FArrayProperty>(Defaults->GetClass(), TEXT("TransientSlots")) : nullptr;
    FStrProperty* Sentinel = Defaults ? FindFProperty<FStrProperty>(Defaults->GetClass(), TEXT("TransientSentinel")) : nullptr;
    FObjectPropertyBase* Owned = Defaults ? FindFProperty<FObjectPropertyBase>(Defaults->GetClass(), TEXT("TransientOwned")) : nullptr;
    FObjectPropertyBase* NativeUnit = Defaults ? FindFProperty<FObjectPropertyBase>(Defaults->GetClass(), TEXT("Unit_0")) : nullptr;
    if (!TestTrue(TEXT("Compiled fields retain transient flags and native Unit_0 remains instanced and nontransient"),
        Slots && Sentinel && Owned && NativeUnit && Slots->HasAnyPropertyFlags(CPF_Transient) &&
        Sentinel->HasAnyPropertyFlags(CPF_Transient) && Owned->HasAnyPropertyFlags(CPF_Transient) &&
        NativeUnit->HasAnyPropertyFlags(CPF_InstancedReference) && !NativeUnit->HasAnyPropertyFlags(CPF_Transient))) return false;
    FIntProperty* Element = CastField<FIntProperty>(Slots->Inner);
    if (!TestNotNull(TEXT("Array storage is actually integer typed"), Element)) return false;
    FScriptArrayHelper Elements(Slots, Slots->ContainerPtrToValuePtr<void>(Defaults)); Elements.AddValues(2);
    Element->SetPropertyValue(Elements.GetRawPtr(0), 41); Element->SetPropertyValue(Elements.GetRawPtr(1), 43);
    Sentinel->SetPropertyValue_InContainer(Defaults, TEXT("keep sentinel"));
    UObject* Initial = NewObject<UObject>(Defaults, ConcreteUnit, FName(TEXT("InitialOwned")));
    Owned->SetObjectPropertyValue_InContainer(Defaults, Initial);
    auto SourceChild = MakeShared<FJsonObject>();
    SourceChild->SetArrayField(TEXT("path"), {MakeShared<FJsonValueString>(TEXT("SourceOwned"))});
    SourceChild->SetStringField(TEXT("ref"), TEXT("SourceOwned")); SourceChild->SetStringField(TEXT("cls"), ConcreteUnit->GetPathName());
    SourceChild->SetArrayField(TEXT("values"), {});
    auto Record = MakeShared<FJsonObject>(); Record->SetArrayField(TEXT("subobjects"), {MakeShared<FJsonValueObject>(SourceChild)});
    Record->SetArrayField(TEXT("values"), {TransientSourceValue(TEXT("TransientSlots"), TEXT("")),
        TransientSourceValue(TEXT("TransientSlots"), TEXT("(91,92)")), TransientSourceValue(TEXT("TransientSentinel"), TEXT("replace")),
        TransientSourceValue(TEXT("TransientOwned"), TEXT("SourceOwned")), TransientSourceValue(TEXT("Unit_0"), TEXT("SourceOwned"))});
    const FString Report = P4BlueprintApplySourceValuesForTest(Defaults, Record);
    TestEqual(TEXT("Empty-array and text-array source routes leave the nonempty transient array intact"), Elements.Num(), 2);
    if (Elements.Num() == 2)
    {
        TestEqual(TEXT("First transient element stays unchanged"), Element->GetPropertyValue(Elements.GetRawPtr(0)), 41);
        TestEqual(TEXT("Second transient element stays unchanged"), Element->GetPropertyValue(Elements.GetRawPtr(1)), 43);
    }
    TestEqual(TEXT("Text source leaves the transient sentinel intact"), Sentinel->GetPropertyValue_InContainer(Defaults), FString(TEXT("keep sentinel")));
    TestTrue(TEXT("Direct known-subobject source leaves the transient reference intact"), Owned->GetObjectPropertyValue_InContainer(Defaults) == Initial);
    UObject* SourceOwned = StaticFindObjectFast(nullptr, Defaults, FName(TEXT("SourceOwned")));
    TestTrue(TEXT("Nontransient instanced Unit_0 receives the actual typed child owned by the template"), SourceOwned &&
        SourceOwned->GetOuter() == Defaults && SourceOwned->IsA(ConcreteUnit) && NativeUnit->GetObjectPropertyValue_InContainer(Defaults) == SourceOwned);
    for (const TCHAR* Name : {TEXT("TransientSlots"), TEXT("TransientSentinel"), TEXT("TransientOwned")})
        TestTrue(TEXT("The source report names each skipped transient property: ") + FString(Name),
            Report.Contains(FString(TEXT("transient property ")) + Name + TEXT(" was not imported")));

    FObjectPropertyBase* SavedActor = FindFProperty<FObjectPropertyBase>(Defaults->GetClass(), TEXT("SavedActor"));
    FObjectPropertyBase* EmptyActor = FindFProperty<FObjectPropertyBase>(Defaults->GetClass(), TEXT("EmptyActor"));
    if (!TestTrue(TEXT("Real actor-constrained fields are nontransient"), SavedActor && EmptyActor &&
        !SavedActor->HasAnyPropertyFlags(CPF_Transient) && !EmptyActor->HasAnyPropertyFlags(CPF_Transient))) return false;
    UObject* PriorActor = AActor::StaticClass()->GetDefaultObject();
    SavedActor->SetObjectPropertyValue_InContainer(Defaults, PriorActor);
    TestTrue(TEXT("The second actor field starts null"), EmptyActor->GetObjectPropertyValue_InContainer(Defaults) == nullptr);
    // The source child is real and correctly declared, but incompatible with an
    // actor property's constraint. Refusal must preserve both valid and null values.
    Record->SetArrayField(TEXT("values"), {TransientSourceValue(TEXT("SavedActor"), TEXT("SourceOwned")),
        TransientSourceValue(TEXT("EmptyActor"), TEXT("SourceOwned"))});
    const FString TypeReport = P4BlueprintApplySourceValuesForTest(Defaults, Record);
    TestTrue(TEXT("An incompatible known child preserves the valid existing actor reference"),
        SavedActor->GetObjectPropertyValue_InContainer(Defaults) == PriorActor);
    TestTrue(TEXT("An incompatible known child leaves an originally null actor reference null"),
        EmptyActor->GetObjectPropertyValue_InContainer(Defaults) == nullptr);
    TestTrue(TEXT("The report names the incompatible direct property"), TypeReport.Contains(TEXT("SavedActor")) &&
        TypeReport.Contains(TEXT("incompatible property class")));

    UObject* WrongChild = NewObject<USceneComponent>(Defaults, FName(TEXT("BadRecipe")));
    SourceChild->SetArrayField(TEXT("path"), {MakeShared<FJsonValueString>(TEXT("BadRecipe"))});
    SourceChild->SetStringField(TEXT("ref"), TEXT("BadRecipe"));
    Record->SetArrayField(TEXT("values"), {TransientSourceValue(TEXT("Unit_0"), TEXT("BadRecipe"))});
    const FString ReuseReport = P4BlueprintApplySourceValuesForTest(Defaults, Record);
    TestTrue(TEXT("A same-name child of the wrong declared class is neither replaced nor rebound"),
        StaticFindObjectFast(nullptr, Defaults, FName(TEXT("BadRecipe"))) == WrongChild && WrongChild->GetClass() == USceneComponent::StaticClass());
    TestTrue(TEXT("A refused source recipe preserves the existing valid instanced Unit_0"),
        NativeUnit->GetObjectPropertyValue_InContainer(Defaults) == SourceOwned);
    TestTrue(TEXT("Both recipe and dependent property refusals are named"),
        ReuseReport.Contains(TEXT("existing child is incompatible with declared class")) &&
        ReuseReport.Contains(TEXT("Unit_0 did not take refused source sub-object BadRecipe")));

    auto AliasRecipe = [&](const TCHAR* Name)
    {
        auto Recipe = MakeShared<FJsonObject>();
        Recipe->SetArrayField(TEXT("path"), {MakeShared<FJsonValueString>(Name)});
        Recipe->SetStringField(TEXT("ref"), TEXT("SharedAlias")); Recipe->SetStringField(TEXT("cls"), ConcreteUnit->GetPathName());
        Recipe->SetArrayField(TEXT("values"), {}); return MakeShared<FJsonValueObject>(Recipe);
    };
    Record->SetArrayField(TEXT("subobjects"), {AliasRecipe(TEXT("FirstAlias")), AliasRecipe(TEXT("SecondAlias"))});
    Record->SetArrayField(TEXT("values"), {TransientSourceValue(TEXT("Unit_0"), TEXT("SharedAlias"))});
    const FString AliasReport = P4BlueprintApplySourceValuesForTest(Defaults, Record);
    TestTrue(TEXT("Distinct real children cannot overwrite a shared source-reference identity"),
        NativeUnit->GetObjectPropertyValue_InContainer(Defaults) == SourceOwned && AliasReport.Contains(TEXT("source reference SharedAlias is ambiguous")));
    return !HasAnyErrors();
}
#endif

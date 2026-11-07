#if P4_MAPKIT_TESTS
#include "P4OriginalEngineFields.h"
#include "P4OriginalComponentReferences.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/MemberReference.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "FileHelpers.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalEngineFieldOwners, "P4MapKit.NativeImport.EngineFieldMemberOwners",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalEngineFieldOwners::RunTest(const FString&)
{
    UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.CarrotSkeletalMeshComponent"));
    if (!TestNotNull(TEXT("The real declaration SDK native skeletal child is loaded"), Class))
        return false;
    const FString Original = TEXT(
        "[{\"MemberParent\":\"/Script/Engine.SkinnedMeshComponent\",\"MemberName\":\"bC4UseDistanceLOD\",\"MemberGuid\":{\"A\":0,\"B\":0,\"C\":0,\"D\":0}}]");
    const auto Read = [](const FString& Json, P4MapKit::FDecodedValue& Value)
    { return P4MapKit::ReadDecodedJson(std::string(TCHAR_TO_UTF8(*Json)), Value); };
    P4MapKit::FDecodedValue Value;
    TestTrue(TEXT("Original native member identity decodes"), Read(Original, Value));
    TestTrue(TEXT("The imported member uses its existing real SDK storage owner"),
        P4PrepareOriginalEngineTracking(Class, Value));
    const FString Editor = UTF8_TO_TCHAR(P4MapKit::DecodedJson(Value).c_str());
    TestTrue(TEXT("No member is omitted; its actual native editor owner is recorded"),
        Editor.Contains(TEXT("/Script/Carrot4.CarrotSkeletalMeshComponent")) &&
            Editor.Contains(TEXT("bC4UseDistanceLOD")));
    TestTrue(
        TEXT("Native comparison restores the original logical owner"), P4CanonicalOriginalEngineTracking(Class, Value));
    TestEqual(TEXT("Every original member/name/GUID survives the round trip"),
        FString(UTF8_TO_TCHAR(P4MapKit::DecodedJson(Value).c_str())), Original);
    for (const FString& Other : {Original.Replace(TEXT("SkinnedMeshComponent"), TEXT("SkeletalMeshComponent")),
             Original.Replace(TEXT("bC4UseDistanceLOD"), TEXT("SkeletalMesh")),
             Original.Replace(TEXT("\"A\":0"), TEXT("\"A\":1")), Original.Replace(TEXT("\"D\":0"), TEXT("\"D\":\"0\"")),
             Original.Replace(TEXT("\"MemberName\":\"bC4UseDistanceLOD\""),
                 TEXT("\"MemberName\":\"bC4UseDistanceLOD\",\"Other\":0"))})
    {
        TestTrue(TEXT("Negative source control decodes"), Read(Other, Value));
        TestFalse(TEXT("Different owners, supported members, GUIDs and malformed references remain unchanged"),
            P4PrepareOriginalEngineTracking(Class, Value));
        TestEqual(TEXT("A refused source member retains all its original data"),
            FString(UTF8_TO_TCHAR(P4MapKit::DecodedJson(Value).c_str())), Other);
    }
    Read(Original, Value);
    TestFalse(TEXT("Stock engine storage is never resized or treated as an SDK child"),
        P4PrepareOriginalEngineTracking(USkeletalMeshComponent::StaticClass(), Value));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4NativeTransformTrackingTest,
    "P4MapKit.NativeImport.NativeTransformTrackingIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4NativeTransformTrackingTest::RunTest(const FString&)
{
    const FString Original = TEXT(
        "{\"MemberParent\":\"/Script/Engine.SceneComponent\",\"MemberName\":\"RelativeLocation\",\"MemberGuid\":{\"A\":0,\"B\":0,\"C\":0,\"D\":0}}");
    for (const FString& Json : {Original, Original.Replace(TEXT("RelativeLocation"), TEXT("RelativeRotation"))})
    {
        P4MapKit::FDecodedValue Value;
        P4MapKit::ReadDecodedJson(std::string(TCHAR_TO_UTF8(*Json)), Value);
        TestTrue(TEXT("Only allocated native scene transform tracking qualifies"),
            P4NativeTransformConstructionTracking(USceneComponent::StaticClass(), Value));
        TestFalse(TEXT("An unrelated component never supplies scene transform storage"),
            P4NativeTransformConstructionTracking(UActorComponent::StaticClass(), Value));
        TestEqual(TEXT("Classification never modifies the member"),
            FString(UTF8_TO_TCHAR(P4MapKit::DecodedJson(Value).c_str())), Json);
    }
    for (const FString& Json : {Original.Replace(TEXT("SceneComponent"), TEXT("ActorComponent")),
             Original.Replace(TEXT("RelativeLocation"), TEXT("RelativeScale3D")),
             Original.Replace(TEXT("\"A\":0"), TEXT("\"A\":1")), Original.Replace(TEXT("\"D\":0"), TEXT("\"D\":\"0\"")),
             Original.Replace(TEXT("\"MemberName\":"), TEXT("\"Extra\":0,\"MemberName\":"))})
    {
        P4MapKit::FDecodedValue Value;
        P4MapKit::ReadDecodedJson(std::string(TCHAR_TO_UTF8(*Json)), Value);
        TestFalse(TEXT("Other owners, fields, Blueprint GUIDs and malformed metadata remain compared"),
            P4NativeTransformConstructionTracking(USceneComponent::StaticClass(), Value));
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalEngineFieldReplay, "P4MapKit.NativeImport.EngineFieldConstructionReplay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalEngineFieldReplay::RunTest(const FString&)
{
    UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.CarrotSkeletalMeshComponent"));
    const FBoolProperty* Field = Class ? FindFProperty<FBoolProperty>(Class, TEXT("bC4UseDistanceLOD")) : nullptr;
    const FArrayProperty* Array = FindFProperty<FArrayProperty>(
        UActorComponent::StaticClass(), TEXT("UCSModifiedProperties"));
    if (!TestTrue(TEXT("Actual allocated SDK native bool and native construction tracking exist"), Field && Array))
        return false;
    UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    TStrongObjectPtr<UPackage> Package(
        CreatePackage(*(TEXT("/Temp/P4EngineField_") + FGuid::NewGuid().ToString(EGuidFormats::Digits))));
    TStrongObjectPtr<UBlueprint> Blueprint(FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Package.Get(),
        TEXT("BP_EngineField"), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
    if (!TestTrue(TEXT("A real editor Blueprint exists"), World && Blueprint.IsValid()))
        return false;
    USCS_Node* Node = Blueprint->SimpleConstructionScript->CreateNode(Class, TEXT("DistanceMeshOpaque"));
    Blueprint->SimpleConstructionScript->AddNode(Node);
    FKismetEditorUtilities::CompileBlueprint(Blueprint.Get());
    AActor* Actor = World->SpawnActor<AActor>(Blueprint->GeneratedClass);
    const auto Component = [&]() -> UActorComponent*
    {
        const FObjectProperty* Member = Actor
            ? FindFProperty<FObjectProperty>(Actor->GetClass(), Node->GetVariableName())
            : nullptr;
        return Member ? Cast<UActorComponent>(Member->GetObjectPropertyValue_InContainer(Actor)) : nullptr;
    };
    UActorComponent* First = Component();
    if (!TestNotNull(TEXT("The SDK component instantiates through its actual SCS"), First))
        return false;
    Field->SetPropertyValue_InContainer(First, true);
    FSimpleMemberReference Member;
    FMemberReference::FillSimpleMemberReference<FProperty>(const_cast<FBoolProperty*>(Field), Member);
    Array->ContainerPtrToValuePtr<TArray<FSimpleMemberReference>>(First)->Add(Member);
    for (int32 Round = 0; Round < 3; ++Round)
    {
        UActorComponent* Old = Component();
        {
            FP4OriginalComponentReferenceScope Scope(World);
            Actor->RerunConstructionScripts();
        }
        UActorComponent* Current = Component();
        TestTrue(TEXT("Actual reconstruction creates a new component with the exact source engine-field value"),
            Current && Current != Old && Field->GetPropertyValue_InContainer(Current));
        TSet<const FProperty*> Modified;
        if (Current)
            Current->GetUCSModifiedProperties(Modified);
        TestTrue(TEXT("Stock UE resolves the migrated native member through the SDK owner"), Modified.Contains(Field));
        CollectGarbage(RF_NoFlags);
        TestTrue(TEXT("Native source field survives old-component collection"),
            Current && Field->GetPropertyValue_InContainer(Current));
    }
    UEditorLoadingAndSavingUtils::NewBlankMap(false);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalLifeReplay, "P4MapKit.NativeImport.LifeConstructionReplay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalLifeReplay::RunTest(const FString&)
{
    UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.LifeComponent"));
    const FFloatProperty* Field = Class ? FindFProperty<FFloatProperty>(Class, TEXT("Life")) : nullptr;
    const FArrayProperty* Array = FindFProperty<FArrayProperty>(
        UActorComponent::StaticClass(), TEXT("UCSModifiedProperties"));
    if (!TestTrue(TEXT("Actual SDK Life float and construction tracking exist"), Field && Array))
        return false;
    UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    TStrongObjectPtr<UPackage> Package(
        CreatePackage(*(TEXT("/Temp/P4LifeField_") + FGuid::NewGuid().ToString(EGuidFormats::Digits))));
    TStrongObjectPtr<UBlueprint> Blueprint(FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), Package.Get(),
        TEXT("BP_LifeField"), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
    if (!TestTrue(TEXT("A real editor Blueprint exists"), World && Blueprint.IsValid()))
        return false;
    USCS_Node* Node = Blueprint->SimpleConstructionScript->CreateNode(Class, TEXT("SourceLifeOpaque"));
    Blueprint->SimpleConstructionScript->AddNode(Node);
    FKismetEditorUtilities::CompileBlueprint(Blueprint.Get());
    AActor* Actor = World->SpawnActor<AActor>(Blueprint->GeneratedClass);
    const auto Component = [&]() -> UActorComponent*
    {
        const FObjectProperty* Member = Actor
            ? FindFProperty<FObjectProperty>(Actor->GetClass(), Node->GetVariableName())
            : nullptr;
        return Member ? Cast<UActorComponent>(Member->GetObjectPropertyValue_InContainer(Actor)) : nullptr;
    };
    UActorComponent* First = Component();
    if (!TestNotNull(TEXT("Native Life component instantiates through its actual SCS"), First))
        return false;
    Field->SetPropertyValue_InContainer(First, 40.0f);
    FSimpleMemberReference Member;
    FMemberReference::FillSimpleMemberReference<FProperty>(const_cast<FFloatProperty*>(Field), Member);
    Array->ContainerPtrToValuePtr<TArray<FSimpleMemberReference>>(First)->Add(Member);
    for (int32 Round = 0; Round < 3; ++Round)
    {
        UActorComponent* Old = Component();
        {
            FP4OriginalComponentReferenceScope Scope(World);
            Actor->RerunConstructionScripts();
        }
        UActorComponent* Current = Component();
        if (!TestTrue(TEXT("Construction replaces the native source Life component"), Current && Current != Old))
            return false;
        TestEqual(TEXT("The exact source Life float survives native construction"),
            Field->GetPropertyValue_InContainer(Current), 40.0f);
        TSet<const FProperty*> Modified;
        Current->GetUCSModifiedProperties(Modified);
        TestTrue(TEXT("The native Life member stays resolvable in UE construction tracking"), Modified.Contains(Field));
        CollectGarbage(RF_NoFlags);
        TestEqual(TEXT("Life survives old-component collection"), Field->GetPropertyValue_InContainer(Current), 40.0f);
    }
    UActorComponent* Old = NewObject<UActorComponent>(Actor, Class);
    UActorComponent* New = NewObject<UActorComponent>(Actor, Class);
    Field->SetPropertyValue_InContainer(Old, 40.0f);
    Field->SetPropertyValue_InContainer(New, 11.0f);
    P4RestoreOriginalEngineFields(Old, New);
    TestEqual(TEXT("An untracked native value is never invented as a construction write"),
        Field->GetPropertyValue_InContainer(New), 11.0f);
    TArray<FSimpleMemberReference>* Before = Array->ContainerPtrToValuePtr<TArray<FSimpleMemberReference>>(Old);
    TArray<FSimpleMemberReference>* After = Array->ContainerPtrToValuePtr<TArray<FSimpleMemberReference>>(New);
    Before->Add(Member);
    After->Add(Member);
    P4RestoreOriginalEngineFields(Old, New);
    TestEqual(TEXT("Current tracked editor construction retains its own value"),
        Field->GetPropertyValue_InContainer(New), 11.0f);
    After->Reset();
    Before->Reset();
    Member.MemberGuid = FGuid::NewGuid();
    Before->Add(Member);
    P4RestoreOriginalEngineFields(Old, New);
    TestEqual(TEXT("A Blueprint GUID is never treated as the native source member"),
        Field->GetPropertyValue_InContainer(New), 11.0f);
    TestEqual(TEXT("A refused native identity does not add tracking"), After->Num(), 0);
    UEditorLoadingAndSavingUtils::NewBlankMap(false);
    return !HasAnyErrors();
}
#endif

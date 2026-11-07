#if P4_MAPKIT_TESTS
#include "P4OriginalComponentReferences.h"
#include "P4OriginalWeakComponentReferences.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Animation/SkeletalMeshActor.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

namespace P4OriginalWeakReferenceTest
{
USkeletalMeshComponent* Mesh(AActor* Actor)
{
    USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>(Actor);
    Actor->AddOwnedComponent(Component);
    Component->RegisterComponent();
    return Component;
}
bool HasSlave(const USkinnedMeshComponent* Master, const USkinnedMeshComponent* Slave)
{
    return Master->GetSlavePoseComponents().ContainsByPredicate(
        [&](const TWeakObjectPtr<USkinnedMeshComponent>& Value) { return Value.Get() == Slave; });
}
} //namespace P4OriginalWeakReferenceTest

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalWeakReplacementTest, "P4MapKit.NativeImport.WeakComponentReplacementAndGC",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalWeakReplacementTest::RunTest(const FString&)
{
    using namespace P4OriginalWeakReferenceTest;
    UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    if (!TestNotNull(TEXT("an editor world is available"), World))
        return false;
    AActor* Actor = World->SpawnActor<AActor>();
    AActor* ForeignActor = World->SpawnActor<AActor>();
    if (!TestTrue(TEXT("native component owners are available"), Actor && ForeignActor))
        return false;
    USkeletalMeshComponent *Old = Mesh(Actor), *New = Mesh(Actor), *Slave = Mesh(Actor);
    USkeletalMeshComponent* Foreign = Mesh(ForeignActor);
    Slave->SetMasterPoseComponent(Old, true);
    TMap<UObject*, UObject*> Replacements;
    Replacements.Add(Old, New);
    TestEqual(TEXT("a live registered component cannot be retired as a replacement"),
        P4RemapOriginalWeakComponentReferences(Replacements, Old, New, Actor), 0);
    TestTrue(TEXT("a refused live component keeps its exact link"), Slave->MasterPoseComponent.Get() == Old);
    Old->DestroyComponent();
    TestTrue(TEXT("the weak reference retains exact pending-kill identity"),
        !Slave->MasterPoseComponent.IsValid() && Slave->MasterPoseComponent.Get(true) == Old);
    TestTrue(TEXT("the destroyed master still has the live reverse slave relation"), HasSlave(Old, Slave));
    Replacements[Old] = Foreign;
    TestEqual(TEXT("a replacement in another actor is refused"),
        P4RemapOriginalWeakComponentReferences(Replacements, Old, Foreign, Actor), 0);
    USceneComponent* WrongClass = NewObject<USceneComponent>(Actor);
    Actor->AddOwnedComponent(WrongClass);
    Replacements[Old] = WrongClass;
    TestEqual(TEXT("a component class change is refused"),
        P4RemapOriginalWeakComponentReferences(Replacements, Old, WrongClass, Actor), 0);
    Replacements[Old] = New;
    TestEqual(TEXT("a replacement argument absent from the authoritative map is refused"),
        P4RemapOriginalWeakComponentReferences(Replacements, Old, Foreign, ForeignActor), 0);
    Replacements[Old] = nullptr;
    TestEqual(TEXT("a missing replacement cannot erase a pose link"),
        P4RemapOriginalWeakComponentReferences(Replacements, Old, New, Actor), 0);
    Replacements[Old] = Old;
    TestEqual(TEXT("a self replacement cannot advance retirement"),
        P4RemapOriginalWeakComponentReferences(Replacements, Old, Old, Actor), 0);
    Replacements[Old] = New;
    TestFalse(
        TEXT("negative replacements do not advance old component retirement"), Old->HasAnyFlags(RF_BeginDestroyed));
    {
        FP4OriginalComponentReferenceScope Scope(World);
        GEditor->NotifyToolsOfObjectReplacement(Replacements);
    }
    TestTrue(TEXT("normal retirement completes before repairing the live slave"), Old->HasAnyFlags(RF_BeginDestroyed));
    TestTrue(TEXT("the exact engine replacement becomes the master"), Slave->MasterPoseComponent.Get() == New);
    TestTrue(TEXT("the public setter restores the new reverse pose relation"), HasSlave(New, Slave));
    TWeakObjectPtr<USkinnedMeshComponent> WeakOld(Old);
    Replacements.Reset();
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("the old component is collected"), WeakOld.IsValid(true));
    TestTrue(TEXT("old-master garbage collection cannot erase the repaired live link"),
        Slave->MasterPoseComponent.Get() == New && HasSlave(New, Slave));

    // A normal authored change before replay must win over the old relationship.
    USkeletalMeshComponent *OtherOld = Mesh(Actor), *OtherNew = Mesh(Actor), *Authored = Mesh(Actor),
                           *OtherSlave = Mesh(Actor);
    OtherSlave->SetMasterPoseComponent(OtherOld, true);
    OtherSlave->SetMasterPoseComponent(Authored, true);
    OtherOld->DestroyComponent();
    Replacements.Add(OtherOld, OtherNew);
    {
        FP4OriginalComponentReferenceScope Scope(World);
        GEditor->NotifyToolsOfObjectReplacement(Replacements);
    }
    TestTrue(
        TEXT("a different authored pose target remains unchanged"), OtherSlave->MasterPoseComponent.Get() == Authored);
    TestFalse(TEXT("no captured old relationship means no early retirement"), OtherOld->HasAnyFlags(RF_BeginDestroyed));
    Replacements.Reset();
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("the authored target survives normal garbage collection"),
        OtherSlave->MasterPoseComponent.Get() == Authored && HasSlave(Authored, OtherSlave));

    // The global hook alone must not reinterpret a foreign unrecorded world.
    USkeletalMeshComponent *UnrecordedOld = Mesh(Actor), *UnrecordedNew = Mesh(Actor), *UnrecordedSlave = Mesh(Actor);
    UnrecordedSlave->SetMasterPoseComponent(UnrecordedOld, true);
    UnrecordedOld->DestroyComponent();
    Replacements.Add(UnrecordedOld, UnrecordedNew);
    GEditor->NotifyToolsOfObjectReplacement(Replacements);
    TestTrue(TEXT("an unrecorded world outside an active import scope is untouched"),
        UnrecordedSlave->MasterPoseComponent.Get(true) == UnrecordedOld);
    TestFalse(
        TEXT("foreign-world notification cannot retire a component"), UnrecordedOld->HasAnyFlags(RF_BeginDestroyed));
    Replacements.Reset();
    CollectGarbage(RF_NoFlags);
    UEditorLoadingAndSavingUtils::NewBlankMap(false);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalWeakConstructionTest,
    "P4MapKit.NativeImport.WeakComponentConstructionReplay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalWeakConstructionTest::RunTest(const FString&)
{
    using namespace P4OriginalWeakReferenceTest;
    UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    if (!TestNotNull(TEXT("an editor world is available"), World))
        return false;
    TStrongObjectPtr<UPackage> Package(
        CreatePackage(*(TEXT("/Temp/P4WeakPose_") + FGuid::NewGuid().ToString(EGuidFormats::Digits))));
    TStrongObjectPtr<UBlueprint> Blueprint(
        FKismetEditorUtilities::CreateBlueprint(ACharacter::StaticClass(), Package.Get(), TEXT("BP_WeakPose"),
            BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
    if (!TestTrue(TEXT("a real Blueprint is available"), Blueprint.IsValid()))
        return false;
    USCS_Node* Node = Blueprint->SimpleConstructionScript->CreateNode(
        USkeletalMeshComponent::StaticClass(), TEXT("PoseDriverOpaque"));
    Blueprint->SimpleConstructionScript->AddNode(Node);
    FKismetEditorUtilities::CompileBlueprint(Blueprint.Get());
    ACharacter* Actor = World->SpawnActor<ACharacter>(Blueprint->GeneratedClass);
    if (!TestNotNull(TEXT("the native shadow and construction-created driver instantiate"), Actor))
        return false;
    USkeletalMeshComponent* Slave = Actor->GetMesh();
    const auto Driver = [&]() -> USkinnedMeshComponent*
    {
        const FObjectProperty* Field = FindFProperty<FObjectProperty>(Actor->GetClass(), Node->GetVariableName());
        return Field ? Cast<USkinnedMeshComponent>(Field->GetObjectPropertyValue_InContainer(Actor)) : nullptr;
    };
    USkinnedMeshComponent* First = Driver();
    if (!TestNotNull(TEXT("the authored SCS member resolves its actual driver"), First))
        return false;
    Slave->SetMasterPoseComponent(First, true);
    TestTrue(TEXT("the native shadow component is not a construction-created component"),
        Slave->CreationMethod == EComponentCreationMethod::Native);
    const FWeakObjectProperty* Pose = FindFProperty<FWeakObjectProperty>(
        USkinnedMeshComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(USkinnedMeshComponent, MasterPoseComponent));
    TestTrue(TEXT("the actual serialized pose field is excluded by the stock instance-data edit filter"),
        Pose && !Pose->HasAnyPropertyFlags(CPF_Edit | CPF_Interp | CPF_Transient));
    for (int32 Round = 0; Round < 3; ++Round)
    {
        USkinnedMeshComponent* Previous = Driver();
        bool EngineIdentity = false;
        const FDelegateHandle Witness = GEditor->OnObjectsReplaced().AddLambda(
            [&](const UEditorEngine::ReplacementObjectMap& Map)
            {
                UObject* const* Actual = Map.Find(Previous);
                EngineIdentity |= Actual && *Actual == Driver();
            });
        {
            FP4OriginalComponentReferenceScope Scope(World);
            Actor->RerunConstructionScripts();
        }
        GEditor->OnObjectsReplaced().Remove(Witness);
        USkinnedMeshComponent* Current = Driver();
        TestTrue(TEXT("ordinary construction emits the real driver replacement map"),
            EngineIdentity && Current && Current != Previous);
        TestTrue(TEXT("ordinary replay preserves the exact pose link and new reverse cache"),
            Slave->MasterPoseComponent.Get() == Current && HasSlave(Current, Slave));
        CollectGarbage(RF_NoFlags);
        TestTrue(TEXT("ordinary replay remains correct after old component GC"),
            Slave->MasterPoseComponent.Get() == Current && HasSlave(Current, Slave));
    }
    UEditorLoadingAndSavingUtils::NewBlankMap(false);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalWeakBothEndpointsTest,
    "P4MapKit.NativeImport.WeakComponentBothEndpointsReplay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalWeakBothEndpointsTest::RunTest(const FString&)
{
    using namespace P4OriginalWeakReferenceTest;
    UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    if (!TestNotNull(TEXT("an editor world is available"), World))
        return false;
    // Exercise both map orders, an authored target, and completed native retirement.
    for (int32 Order = 0; Order < 4; ++Order)
    {
        AActor* Owner = World->SpawnActor<AActor>();
        USkeletalMeshComponent *OldMaster = Mesh(Owner), *NewMaster = Mesh(Owner);
        USkeletalMeshComponent *OldShadow = Mesh(Owner), *NewShadow = Mesh(Owner);
        USkeletalMeshComponent* Authored = Order == 2 ? Mesh(Owner) : nullptr;
        OldShadow->SetMasterPoseComponent(OldMaster, true);
        OldShadow->DestroyComponent();
        OldMaster->DestroyComponent();
        if (Order == 3)
            OldMaster->ConditionalBeginDestroy();
        TestTrue(TEXT("a replaced old shadow retains its exact serialized weak master before GC"),
            OldShadow->MasterPoseComponent.Get(true) == OldMaster && !OldShadow->MasterPoseComponent.IsValid());
        if (Authored)
            NewShadow->SetMasterPoseComponent(Authored, true);
        TMap<UObject*, UObject*> Map;
        if (Order == 1)
        {
            Map.Add(OldShadow, NewShadow);
            Map.Add(OldMaster, NewMaster);
        }
        else
        {
            Map.Add(OldMaster, NewMaster);
            Map.Add(OldShadow, NewShadow);
        }
        {
            FP4OriginalComponentReferenceScope Scope(World);
            GEditor->NotifyToolsOfObjectReplacement(Map);
        }
        USkinnedMeshComponent* Expected = Authored ? Authored : NewMaster;
        TestTrue(TEXT("actual double-endpoint identity restores nil and preserves a different live target"),
            NewShadow->MasterPoseComponent.Get() == Expected && HasSlave(Expected, NewShadow));
        TestTrue(TEXT("retiring the old master does not erase the pending-kill source shadow identity"),
            OldShadow->MasterPoseComponent.Get(true) == OldMaster);
        Map.Reset();
        CollectGarbage(RF_NoFlags);
        TestTrue(TEXT("double-endpoint repair survives old shadow/master collection in either map order"),
            NewShadow->MasterPoseComponent.Get() == Expected && HasSlave(Expected, NewShadow));
    }
    {
        AActor* Owner = World->SpawnActor<AActor>();
        AActor* ForeignOwner = World->SpawnActor<AActor>();
        USkeletalMeshComponent *OldMaster = Mesh(Owner), *NewMaster = Mesh(Owner), *OldShadow = Mesh(Owner),
                               *NewShadow = Mesh(Owner);
        USkeletalMeshComponent* ForeignShadow = Mesh(ForeignOwner);
        OldShadow->SetMasterPoseComponent(OldMaster, true);
        OldShadow->DestroyComponent();
        OldMaster->DestroyComponent();
        TMap<UObject*, UObject*> Map;
        Map.Add(OldMaster, NewMaster);
        Map.Add(OldShadow, ForeignShadow);
        TestEqual(TEXT("a foreign container map cannot recover an unrelated nil shadow"),
            P4RemapOriginalWeakComponentReferences(Map, OldMaster, NewMaster, Owner), 0);
        TestTrue(TEXT("a malformed container map leaves both new shadows nil"),
            !NewShadow->MasterPoseComponent.Get(true) && !ForeignShadow->MasterPoseComponent.Get(true));
        TestFalse(TEXT("a malformed container map cannot cause early old-master retirement"),
            OldMaster->HasAnyFlags(RF_BeginDestroyed));
        Map.Reset();
        CollectGarbage(RF_NoFlags);
    }

    TStrongObjectPtr<UPackage> Package(
        CreatePackage(*(TEXT("/Temp/P4WeakBoth_") + FGuid::NewGuid().ToString(EGuidFormats::Digits))));
    TStrongObjectPtr<UBlueprint> Blueprint(
        FKismetEditorUtilities::CreateBlueprint(ACharacter::StaticClass(), Package.Get(), TEXT("BP_WeakBoth"),
            BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
    if (!TestTrue(TEXT("a real two-endpoint Blueprint is available"), Blueprint.IsValid()))
        return false;
    USCS_Node* MasterNode = Blueprint->SimpleConstructionScript->CreateNode(
        USkeletalMeshComponent::StaticClass(), TEXT("OpaqueDriver"));
    USCS_Node* ShadowNode = Blueprint->SimpleConstructionScript->CreateNode(
        USkeletalMeshComponent::StaticClass(), TEXT("OpaqueFollower"));
    Blueprint->SimpleConstructionScript->AddNode(MasterNode);
    Blueprint->SimpleConstructionScript->AddNode(ShadowNode);
    FKismetEditorUtilities::CompileBlueprint(Blueprint.Get());
    ACharacter* Actor = World->SpawnActor<ACharacter>(Blueprint->GeneratedClass);
    if (!TestNotNull(TEXT("both authored SCS components instantiate"), Actor))
        return false;
    const auto Member = [&](const USCS_Node* Node) -> USkinnedMeshComponent*
    {
        const FObjectProperty* Field = FindFProperty<FObjectProperty>(Actor->GetClass(), Node->GetVariableName());
        return Field ? Cast<USkinnedMeshComponent>(Field->GetObjectPropertyValue_InContainer(Actor)) : nullptr;
    };
    USkinnedMeshComponent *FirstMaster = Member(MasterNode), *FirstShadow = Member(ShadowNode);
    if (!TestTrue(TEXT("both fixture endpoints are real construction-script components"),
            FirstMaster && FirstShadow &&
                FirstMaster->CreationMethod == EComponentCreationMethod::SimpleConstructionScript &&
                FirstShadow->CreationMethod == EComponentCreationMethod::SimpleConstructionScript))
        return false;
    FirstShadow->SetMasterPoseComponent(FirstMaster, true);
    for (int32 Round = 0; Round < 3; ++Round)
    {
        USkinnedMeshComponent *OldMaster = Member(MasterNode), *OldShadow = Member(ShadowNode);
        bool ActualEndpoints = false, PendingSourceIdentity = false;
        const FDelegateHandle Witness = GEditor->OnObjectsReplaced().AddLambda(
            [&](const UEditorEngine::ReplacementObjectMap& Map)
            {
                UObject* const* MasterReplacement = Map.Find(OldMaster);
                UObject* const* ShadowReplacement = Map.Find(OldShadow);
                ActualEndpoints |= MasterReplacement && ShadowReplacement && *MasterReplacement == Member(MasterNode) &&
                    *ShadowReplacement == Member(ShadowNode);
                PendingSourceIdentity |= OldShadow->IsPendingKill() &&
                    OldShadow->MasterPoseComponent.Get(true) == OldMaster;
            });
        {
            FP4OriginalComponentReferenceScope Scope(World);
            Actor->RerunConstructionScripts();
        }
        GEditor->OnObjectsReplaced().Remove(Witness);
        USkinnedMeshComponent *Master = Member(MasterNode), *Shadow = Member(ShadowNode);
        TestTrue(
            TEXT("ordinary construction supplies both authoritative replacements and the pending old weak identity"),
            ActualEndpoints && PendingSourceIdentity && Master != OldMaster && Shadow != OldShadow);
        TestTrue(TEXT("ordinary two-SCS replay preserves the exact native master and reverse cache"),
            Master && Shadow && Shadow->MasterPoseComponent.Get() == Master && HasSlave(Master, Shadow));
        CollectGarbage(RF_NoFlags);
        TestTrue(TEXT("two-SCS replay remains exact after old endpoints are collected"),
            Master && Shadow && Shadow->MasterPoseComponent.Get() == Master && HasSlave(Master, Shadow));
    }
    UEditorLoadingAndSavingUtils::NewBlankMap(false);
    return !HasAnyErrors();
}
#endif

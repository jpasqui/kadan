#include "P4OriginalEngineFields.h"
#include "P4GameNames.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/MemberReference.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"

namespace
{
using P4MapKit::FDecodedValue;
const TCHAR* const StoragePath = TEXT(P4_GAME_SCRIPT_PACKAGE ".CarrotSkeletalMeshComponent");
const TCHAR* const OriginalOwner = TEXT("/Script/Engine.SkinnedMeshComponent");
const FName DistanceLOD(TEXT("bC4UseDistanceLOD"));

const FBoolProperty* DistanceField(const UClass* Class)
{
    // Original reflection declares a native, editable scalar bool on SkinnedMeshComponent.
    // The established SDK's fork_fields generation declares it on this native child, with
    // real allocated C++ storage. Do not add bytes to a live stock class or guess offsets.
    UClass* Storage = FindObject<UClass>(nullptr, StoragePath);
    const FBoolProperty* Field = Storage ? FindFProperty<FBoolProperty>(Storage, DistanceLOD) : nullptr;
    if (!Class || !Storage || !Storage->HasAnyClassFlags(CLASS_Native) ||
        Storage->GetSuperClass() != USkeletalMeshComponent::StaticClass() || !Class->IsChildOf(Storage) || !Field ||
        Field->GetOwnerStruct() != Storage || !Field->IsNativeBool() || Field->ArrayDim != 1 ||
        !Field->HasAnyPropertyFlags(CPF_Edit) ||
        Field->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_EditorOnly | CPF_SkipSerialization) ||
        Class->FindPropertyByName(DistanceLOD) != Field ||
        Field->GetOffset_ForInternal() < USkeletalMeshComponent::StaticClass()->GetPropertiesSize() ||
        Field->GetOffset_ForInternal() + Field->GetSize() > Storage->GetPropertiesSize() ||
        USkinnedMeshComponent::StaticClass()->FindPropertyByName(DistanceLOD))
        return nullptr;
    return Field;
}

bool ZeroGuid(const FDecodedValue* Guid)
{
    if (!Guid || Guid->Kind != FDecodedValue::EKind::Object || Guid->Fields.size() != 4)
        return false;
    for (const char* Part : {"A", "B", "C", "D"})
    {
        const FDecodedValue* Number = Guid->Field(Part);
        if (!Number || Number->Kind != FDecodedValue::EKind::Number || Number->Text != "0")
            return false;
    }
    return true;
}

bool ChangeOwner(const UClass* Class, FDecodedValue& Value, bool Editor)
{
    const FBoolProperty* Field = DistanceField(Class);
    if (!Field || Value.Kind != FDecodedValue::EKind::Array)
        return false;
    const FString From = Editor ? OriginalOwner : Field->GetOwnerStruct()->GetPathName();
    const FString To = Editor ? Field->GetOwnerStruct()->GetPathName() : OriginalOwner;
    bool Changed = false;
    for (FDecodedValue& Member : Value.Items)
    {
        if (Member.Kind != FDecodedValue::EKind::Object || Member.Fields.size() != 3)
            continue;
        const FDecodedValue* Name = Member.Field("MemberName");
        FDecodedValue* Parent = nullptr;
        for (auto& Part : Member.Fields)
            if (Part.first == "MemberParent")
                Parent = &Part.second;
        if (!Name || (Name->Kind != FDecodedValue::EKind::Name && Name->Kind != FDecodedValue::EKind::String) ||
            Name->NameNumber || FName(UTF8_TO_TCHAR(Name->Text.c_str())) != DistanceLOD || !Parent ||
            (Parent->Kind != FDecodedValue::EKind::String && Parent->Kind != FDecodedValue::EKind::HardReference) ||
            FString(UTF8_TO_TCHAR(Parent->Text.c_str())) != From || !ZeroGuid(Member.Field("MemberGuid")))
            continue;
        Parent->Text = TCHAR_TO_UTF8(*To);
        Changed = true;
    }
    return Changed;
}

TArray<FSimpleMemberReference>* Tracking(UActorComponent* Component)
{
    const FArrayProperty* Array = FindFProperty<FArrayProperty>(
        UActorComponent::StaticClass(), TEXT("UCSModifiedProperties"));
    const FStructProperty* Item = Array ? CastField<FStructProperty>(Array->Inner) : nullptr;
    if (!Item || Item->Struct != FSimpleMemberReference::StaticStruct() ||
        Item->Struct->GetStructureSize() != sizeof(FSimpleMemberReference) || Array->ArrayDim != 1)
        return nullptr;
    return Array->ContainerPtrToValuePtr<TArray<FSimpleMemberReference>>(Component);
}

bool DistanceMember(const FSimpleMemberReference& Member, const FBoolProperty* Field)
{
    return Member.MemberName == DistanceLOD && !Member.MemberGuid.IsValid() && Member.MemberParent &&
        (Member.MemberParent == Field->GetOwnerStruct() || Member.MemberParent == USkinnedMeshComponent::StaticClass());
}

const FFloatProperty* LifeField(const UClass* Class)
{
    // The captured player component serializes Life=40 and a native Life member reference.
    // The declaration SDK has real float storage, but no game construction implementation.
    // UE deliberately excludes this UCS-owned value from its instance-data cache.
    UClass* Storage = FindObject<UClass>(nullptr, TEXT(P4_GAME_SCRIPT_PACKAGE ".LifeComponent"));
    const FFloatProperty* Field = Storage ? FindFProperty<FFloatProperty>(Storage, TEXT("Life")) : nullptr;
    if (!Class || !Storage || !Storage->HasAnyClassFlags(CLASS_Native) || !Class->IsChildOf(Storage) ||
        !Storage->GetSuperClass() ||
        Storage->GetSuperClass()->GetPathName() != TEXT(P4_GAME_SCRIPT_PACKAGE ".CarrotActorComponent") ||
        !Storage->IsChildOf(UActorComponent::StaticClass()) || !Field || Field->GetOwnerStruct() != Storage ||
        Field->ArrayDim != 1 || Field->ElementSize != sizeof(float) ||
        !Field->HasAllPropertyFlags(CPF_Edit | CPF_EditConst) ||
        Field->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_EditorOnly | CPF_SkipSerialization) ||
        Class->FindPropertyByName(TEXT("Life")) != Field ||
        Field->GetOffset_ForInternal() < Storage->GetSuperClass()->GetPropertiesSize() ||
        Field->GetOffset_ForInternal() + Field->GetSize() > Storage->GetPropertiesSize())
        return nullptr;
    return Field;
}

void RestoreTrackedField(UActorComponent* Old, UActorComponent* New, const FProperty* Field)
{
    TArray<FSimpleMemberReference>* Before = Field ? Tracking(Old) : nullptr;
    TArray<FSimpleMemberReference>* After = Field ? Tracking(New) : nullptr;
    if (!Before || !After)
        return;
    const auto Matches = [Field](const FSimpleMemberReference& Member)
    {
        const FBoolProperty* Distance = CastField<FBoolProperty>(Field);
        return Distance ? DistanceMember(Member, Distance)
                        : Member.MemberName == Field->GetFName() && !Member.MemberGuid.IsValid() &&
                Member.MemberParent == Field->GetOwnerStruct();
    };
    if (!Before->ContainsByPredicate(Matches) || After->ContainsByPredicate(Matches))
        return;
    // Current construction writes take precedence; otherwise retain exactly the source-owned
    // native state that UE's instance cache intentionally omitted. Never synthesize a value.
    Field->CopyCompleteValue_InContainer(New, Old);
    FSimpleMemberReference Member;
    FMemberReference::FillSimpleMemberReference<FProperty>(const_cast<FProperty*>(Field), Member);
    After->Add(Member);
}
} //namespace

bool P4PrepareOriginalEngineTracking(const UClass* Class, FDecodedValue& Value)
{
    return ChangeOwner(Class, Value, true);
}
bool P4CanonicalOriginalEngineTracking(const UClass* Class, FDecodedValue& Value)
{
    return ChangeOwner(Class, Value, false);
}
bool P4NativeTransformConstructionTracking(const UClass* Class, const FDecodedValue& Member)
{
    if (!Class || !Class->IsChildOf(USceneComponent::StaticClass()) || Member.Kind != FDecodedValue::EKind::Object ||
        Member.Fields.size() != 3)
        return false;
    const auto* Parent = Member.Field("MemberParent");
    const auto* Name = Member.Field("MemberName");
    if (!Parent ||
        (Parent->Kind != FDecodedValue::EKind::String && Parent->Kind != FDecodedValue::EKind::HardReference) ||
        Parent->Text != "/Script/Engine.SceneComponent" || !Name || Name->NameNumber ||
        (Name->Kind != FDecodedValue::EKind::String && Name->Kind != FDecodedValue::EKind::Name) ||
        !ZeroGuid(Member.Field("MemberGuid")))
        return false;
    const FName Key(UTF8_TO_TCHAR(Name->Text.c_str()));
    const bool Location = Key == TEXT("RelativeLocation"), Rotation = Key == TEXT("RelativeRotation");
    if (!Location && !Rotation)
        return false;
    const auto* Field = FindFProperty<FStructProperty>(USceneComponent::StaticClass(), Key);
    return Field && Field->GetOwnerStruct() == USceneComponent::StaticClass() &&
        Class->FindPropertyByName(Key) == Field && Field->ArrayDim == 1 &&
        Field->Struct == (Location ? TBaseStructure<FVector>::Get() : TBaseStructure<FRotator>::Get()) &&
        Field->ElementSize == (Location ? sizeof(FVector) : sizeof(FRotator)) && Field->HasAnyPropertyFlags(CPF_Edit) &&
        !Field->HasAnyPropertyFlags(CPF_Transient | CPF_EditorOnly | CPF_SkipSerialization);
}

void P4RestoreOriginalEngineFields(UActorComponent* Old, UActorComponent* New)
{
    if (!Old || !IsValid(New) || Old->GetClass() != New->GetClass() || Old->IsTemplate() || New->IsTemplate() ||
        !Old->GetTypedOuter<AActor>() || Old->GetTypedOuter<AActor>() != New->GetTypedOuter<AActor>())
        return;
    RestoreTrackedField(Old, New, DistanceField(New->GetClass()));
    RestoreTrackedField(Old, New, LifeField(New->GetClass()));
}

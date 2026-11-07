#include "P4ActorAssetValuePlanning.h"
#include "P4GamePaths.h"
#include "P4RecoveredPostProcessAuthoring.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

using FValues = FP4ActorValues;
using FAssetValueReference = FP4ActorAssetReference;
bool P4ValidateActorShaderContext(const FAssetValueReference& Reference, bool UsesNewShader, FString& Error)
{
    if (!UsesNewShader)
        return true;
    if (!Reference.Owner.IsValid() ||
        (Reference.StaticMesh ? !Reference.MeshSlot : !Reference.MaterialInterface || !Reference.MaterialSlot))
    {
        Error = TEXT("The change to ") + Reference.Path + TEXT(" of ") + Reference.Actor +
            TEXT(" uses a new shader outside the qualified static material context.");
        return false;
    }
    return true;
}
static FAssetValueReference TypedReference(const FProperty* Property, const UObject* Owner, const FString& Actor,
    const FString& Path, const FString& Target, UClass* Type)
{
    FAssetValueReference Result;
    Result.Actor = Actor;
    Result.Path = Path;
    Result.Target = Target;
    Result.Owner = Owner;
    Result.Property = Property->GetFName();
    Result.MaterialInterface = Type == UMaterialInterface::StaticClass();
    Result.StaticMesh = Type == UStaticMesh::StaticClass();
    Result.MaterialSlot = Owner && Owner->GetClass() == UMaterialInstanceConstant::StaticClass() &&
        Owner->GetOuter() == Owner->GetOutermost() && Property == Owner->GetClass()->FindPropertyByName(TEXT("Parent"));
    Result.MeshSlot = Owner && Owner->GetClass() == UStaticMeshComponent::StaticClass() &&
        Property == Owner->GetClass()->FindPropertyByName(TEXT("StaticMesh"));
    return Result;
}
bool P4RemapActorAssetValue(
    FValues& Values, const FAssetValueReference& Reference, const FString& Runtime, FString& Error)
{
    if (Runtime == Reference.Target)
        return true;
    if (!P4ValidateActorShaderContext(Reference, true, Error))
        return false;
    auto Found = Values.find(TCHAR_TO_UTF8(*Reference.Path));
    if (Found == Values.end() || Found->second.Type != P4MapKit::Export::SetValue::Kind::Text ||
        Found->second.Text != TCHAR_TO_UTF8(*Reference.Target))
    {
        Error = TEXT("The planned actor asset reference lost its native type provenance.");
        return false;
    }
    Found->second.Text = TCHAR_TO_UTF8(*Runtime);
    return true;
}
static FString ValueRefusal(const FString& Path, const FString& Actor)
{
    return FString::Printf(
        TEXT(
            "The change to %s of %s cannot be exported, as only numbers, true or false, names, strings, structs of those and asset references can be."),
        *Path, *Actor);
}
bool P4PlanActorValue(const FProperty* Property, const void* Data, const FString& Path, const FString& Actor,
    const UObject* Within, const UObject* Owner, FValues& Out, TArray<FString>& References,
    TArray<FAssetValueReference>& AssetValues, FString& Error)
{
    using P4MapKit::Export::SetValue;
    FString Unsupported;
    if (P4RefuseRecoveredPostProcessAuthoring(Owner, Property, Data, Unsupported))
    {
        Error = TEXT("The change to ") + Path + TEXT(" of ") + Actor + TEXT(" cannot be exported because ") +
            Unsupported;
        return false;
    }
    SetValue Value;
    if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
    {
        Value.Type = SetValue::Kind::Boolean;
        Value.Boolean = Bool->GetPropertyValue(Data);
    }
    else if (const UEnum* Type = CastField<FEnumProperty>(Property) ? CastField<FEnumProperty>(Property)->GetEnum()
            : CastField<FByteProperty>(Property)                    ? CastField<FByteProperty>(Property)->Enum
                                                                    : nullptr)
    {
        // An enum by the name of its member, which the framework looks up in the game's enum.
        const FNumericProperty* Number = CastField<FEnumProperty>(Property)
            ? CastField<FEnumProperty>(Property)->GetUnderlyingProperty()
            : CastField<FNumericProperty>(Property);
        const FString Member = Number ? Type->GetNameStringByValue(Number->GetSignedIntPropertyValue(Data)) : FString();
        if (Member.IsEmpty())
        {
            Error = FString::Printf(
                TEXT("The change to %s of %s cannot be exported, as its value is not a member of %s."), *Path, *Actor,
                *Type->GetName());
            return false;
        }
        Value.Type = SetValue::Kind::Text;
        Value.Text = TCHAR_TO_UTF8(*Member);
    }
    else if (const FNumericProperty* Number = CastField<FNumericProperty>(Property))
    {
        if (Number->IsFloatingPoint())
        {
            Value.Type = SetValue::Kind::Number;
            Value.Number = Number->GetFloatingPointPropertyValue(Data);
        }
        else
        {
            Value.Type = SetValue::Kind::Integer;
            Value.Integer = Number->GetSignedIntPropertyValue(Data);
        }
    }
    else if (const FNameProperty* Name = CastField<FNameProperty>(Property))
    {
        Value.Type = SetValue::Kind::Text;
        Value.Text = TCHAR_TO_UTF8(*Name->GetPropertyValue(Data).ToString());
    }
    else if (const FStrProperty* String = CastField<FStrProperty>(Property))
    {
        Value.Type = SetValue::Kind::Text;
        Value.Text = TCHAR_TO_UTF8(*String->GetPropertyValue(Data));
    }
    else if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
    {
        for (TFieldIterator<FProperty> Member(Struct->Struct); Member; ++Member)
            for (int32 Index = 0; Index < Member->ArrayDim; ++Index)
                if (!P4PlanActorValue(*Member, Member->ContainerPtrToValuePtr<void>(Data, Index),
                        Path + TEXT(".") + Member->GetName() +
                            (Member->ArrayDim > 1 ? FString::Printf(TEXT("[%d]"), Index) : FString()),
                        Actor, Within, Owner, Out, References, AssetValues, Error))
                    return false;
        return true;
    }
    else if (const FSoftObjectProperty* Soft = CastField<FSoftObjectProperty>(Property))
    {
        const FSoftObjectPath Target = Soft->GetPropertyValue(Data).ToSoftObjectPath();
        Value.Type = SetValue::Kind::Text;
        Value.Text = TCHAR_TO_UTF8(*Target.ToString());
        AssetValues.AddUnique(TypedReference(Property, Owner, Actor, Path, Target.ToString(), Soft->PropertyClass));
        const FString Package = Target.GetLongPackageName();
        if (P4IsContentPackageIgnoringCase(Package))
            References.AddUnique(Package);
    }
    else if (const FObjectPropertyBase* Reference =
                 CastField<FLazyObjectProperty>(Property) ? nullptr : CastField<FObjectPropertyBase>(Property))
    {
        // An asset, named by its object path, which the framework loads. A reference to something of the
        // level is not a value.
        const UObject* Target = Reference->GetObjectPropertyValue(Data);
        const FString Package = Target ? Target->GetOutermost()->GetName() : FString();
        if (Target && Target->IsA<UMaterial>() && P4IsModFolderPackage(Package) &&
            Reference->PropertyClass != UMaterialInterface::StaticClass())
        {
            Error = TEXT("The change to ") + Path + TEXT(" of ") + Actor +
                TEXT(" names a concrete material graph. Use a material interface reference for a new shader.");
            return false;
        }
        if (!Target)
        {
            Error = FString::Printf(
                TEXT("The change to %s of %s cannot be exported, as it clears a reference."), *Path, *Actor);
            return false;
        }
        if (Target->GetOutermost() == Within->GetOutermost() || Target->GetOutermost() == GetTransientPackage() ||
            !(P4IsContentPackageIgnoringCase(Package) || Package.StartsWith(TEXT("/Script/")) ||
                Package.StartsWith(TEXT("/Engine/"))))
        {
            Error = FString::Printf(
                TEXT("The change to %s of %s cannot be exported, as it refers to something that is not an asset."),
                *Path, *Actor);
            return false;
        }
        Value.Type = SetValue::Kind::Text;
        Value.Text = TCHAR_TO_UTF8(*Target->GetPathName());
        AssetValues.AddUnique(
            TypedReference(Property, Owner, Actor, Path, Target->GetPathName(), Reference->PropertyClass));
        if (P4IsContentPackageIgnoringCase(Package))
            References.AddUnique(Package);
    }
    else
    {
        Error = ValueRefusal(Path, Actor);
        return false;
    }
    Out[TCHAR_TO_UTF8(*Path)] = Value;
    return true;
}

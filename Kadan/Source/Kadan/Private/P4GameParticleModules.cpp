#include "P4GameParticleModules.h"
#include "P4ManualEngineClassRegistration.h"
#include "Particles/ParticleModule.h"
#include "Particles/Location/ParticleModuleLocationBase.h"
#include "Particles/TypeData/ParticleModuleTypeDataBase.h"
#include "Materials/MaterialInterface.h"
#include "Distributions/DistributionFloat.h"
#include "Distributions/DistributionVector.h"
#include "UObject/Class.h"
#include "UObject/EnumProperty.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

namespace P4GameParticleModules
{
enum class EField : uint8
{
    Vector,
    Float,
    Int,
    Number,
    Bool,
    Name,
    Material,
    DivisionType
};
struct FFieldSpec
{
    const TCHAR* Name;
    EField Kind;
    uint64 Flags;
};
struct FClassSpec
{
    const TCHAR* Name;
    UClass* (*Parent)();
    const FFieldSpec* Fields;
    int32 FieldCount;
    UClass::ClassConstructorType Constructor;
    void (*Defaults)(UObject*);
    UObject* (*Register)();
};

// Captured game schemas (reflection capture). Storage is allocated after the stock parent
// here; the game's native offsets are not reproduced. Property flags are the captured ones.
const FFieldSpec CircleFields[] = {{TEXT("StartLocation"), EField::Vector, 0x10008000000001ULL},
    {TEXT("StartRadius"), EField::Float, 0x10008000000001ULL}, {TEXT("Division"), EField::Int, 0x18001040000201ULL},
    {TEXT("fSweepStart"), EField::Number, 0x18001040000201ULL},
    {TEXT("fSweepEnd"), EField::Number, 0x18001040000201ULL}, {TEXT("Velocity"), EField::Bool, 0x18001000000001ULL},
    {TEXT("VelocityScale"), EField::Float, 0x10008000000001ULL},
    {TEXT("bRandomSpawn"), EField::Bool, 0x18001040000201ULL},
    {TEXT("fDivisionRadian"), EField::Number, 0x18001040000214ULL},
    {TEXT("fDivisionRandomRadian"), EField::Number, 0x18001040000214ULL},
    {TEXT("fSweepStartRadian"), EField::Number, 0x18001040000214ULL}};
const FFieldSpec SphereFields[] = {{TEXT("StartLocation"), EField::Vector, 0x10008000000001ULL},
    {TEXT("StartRadius"), EField::Float, 0x10008000000001ULL},
    {TEXT("DivisionType"), EField::DivisionType, 0x18001040000201ULL},
    {TEXT("fSweepStart"), EField::Number, 0x18001040000201ULL},
    {TEXT("fSweepEnd"), EField::Number, 0x18001040000201ULL}, {TEXT("Velocity"), EField::Bool, 0x18001000000001ULL},
    {TEXT("VelocityScale"), EField::Float, 0x10008000000001ULL},
    {TEXT("bRandomSpawn"), EField::Bool, 0x18001040000201ULL}, {TEXT("Division"), EField::Int, 0x18001040000214ULL},
    {TEXT("bInverseOrder"), EField::Bool, 0x18001040000214ULL},
    {TEXT("PolyhedronParamStartIndex"), EField::Int, 0x18001040000214ULL},
    {TEXT("PolyhedronParamNum"), EField::Int, 0x18001040000214ULL},
    {TEXT("fSweepStartDegree"), EField::Number, 0x18001040000214ULL},
    {TEXT("fSweepRadianDegree"), EField::Number, 0x18001040000214ULL}};
const FFieldSpec DecalFields[] = {{TEXT("DecalMaterial"), EField::Material, 0x18021040000214ULL},
    {TEXT("SortOrder"), EField::Int, 0x18001040000215ULL},
    {TEXT("FadeScreenSize"), EField::Number, 0x18001040000215ULL},
    {TEXT("FadeStartDelay"), EField::Number, 0x18001040000215ULL},
    {TEXT("FadeDuration"), EField::Number, 0x18001040000215ULL},
    {TEXT("FadeInDuration"), EField::Number, 0x18001040000215ULL},
    {TEXT("FadeInStartDelay"), EField::Number, 0x18001040000215ULL},
    {TEXT("bDestroyOwnerAfterFade"), EField::Bool, 0x18001000000015ULL},
    {TEXT("MaterialColorParamName"), EField::Name, 0x18001040000215ULL}};

UClass* CircleParent()
{
    return UParticleModuleLocationBase::StaticClass();
}
UClass* SphereParent()
{
    return UParticleModuleLocationBase::StaticClass();
}
UClass* DecalParent()
{
    return UParticleModuleTypeDataBase::StaticClass();
}

struct FEnumValue
{
    const TCHAR* Name;
    int64 Value;
};
const FEnumValue DivisionValues[] = {{TEXT("EDivisionType::None"), 0}, {TEXT("EDivisionType::DIVISION_8"), 8},
    {TEXT("EDivisionType::DIVISION_12"), 12}, {TEXT("EDivisionType::DIVISION_20"), 20},
    {TEXT("EDivisionType::EDivisionType_MAX"), 21}};

bool ValidDivisionType(const UEnum* Enum)
{
    if (!IsValid(Enum) || Enum->GetClass() != UEnum::StaticClass() ||
        !Enum->GetPathName().Equals(TEXT("/Script/Engine.EDivisionType"), ESearchCase::CaseSensitive) ||
        !Enum->CppType.Equals(TEXT("EDivisionType"), ESearchCase::CaseSensitive) ||
        Enum->GetCppForm() != UEnum::ECppForm::EnumClass || Enum->NumEnums() != int32(UE_ARRAY_COUNT(DivisionValues)))
        return false;
    for (int32 Index = 0; Index < int32(UE_ARRAY_COUNT(DivisionValues)); ++Index)
        if (!Enum->GetNameByIndex(Index).ToString().Equals(DivisionValues[Index].Name, ESearchCase::CaseSensitive) ||
            Enum->GetValueByIndex(Index) != DivisionValues[Index].Value)
            return false;
    return true;
}

UEnum* DivisionType()
{
    UPackage* Package = CircleParent()->GetOutermost();
    if (UObject* Existing = FindObjectFast<UObject>(Package, TEXT("EDivisionType")))
    {
        UEnum* Enum = Cast<UEnum>(Existing);
        return ValidDivisionType(Enum) ? Enum : nullptr;
    }
    UEnum* Enum = NewObject<UEnum>(Package, TEXT("EDivisionType"), RF_Public | RF_Standalone);
    TArray<TPair<FName, int64>> Values;
    for (const FEnumValue& Value : DivisionValues)
        Values.Emplace(FName(Value.Name), Value.Value);
    // Explicit scoped names and sparse byte values are the serialized identity;
    // no guessed entries or automatic MAX value may enter this storage enum.
    Enum->CppType = TEXT("EDivisionType");
    if (!Enum->SetEnums(Values, UEnum::ECppForm::EnumClass, EEnumFlags::None, false) || !ValidDivisionType(Enum))
    {
        Enum->ClearFlags(RF_Public | RF_Standalone);
        Enum->MarkPendingKill();
        return nullptr;
    }
    Enum->AddToRoot();
    return Enum;
}

// Captured game class defaults have inherited switches on that the stock parent constructor
// leaves off. Instances take native parent fields from the constructor, not from the class
// default object, so each constructor sets them as the game's does.
void ConstructCircle(const FObjectInitializer& Initializer)
{
    CircleParent()->ClassConstructor(Initializer);
    if (UParticleModule* Module = Cast<UParticleModule>(Initializer.GetObj()))
    {
        Module->bSpawnModule = true;
        Module->bSupported3DDrawMode = true;
    }
}
void ConstructDecal(const FObjectInitializer& Initializer)
{
    DecalParent()->ClassConstructor(Initializer);
    if (UParticleModule* Module = Cast<UParticleModule>(Initializer.GetObj()))
        Module->bSupported3DDrawMode = true;
}
void ConstructSphere(const FObjectInitializer& Initializer)
{
    SphereParent()->ClassConstructor(Initializer);
    if (UParticleModule* Module = Cast<UParticleModule>(Initializer.GetObj()))
    {
        Module->bSpawnModule = true;
        Module->bSupported3DDrawMode = true;
    }
}

template <typename T>
void SetField(UObject* Object, const TCHAR* Name, const typename T::TCppType& Value)
{
    CastFieldChecked<T>(Object->GetClass()->FindPropertyByName(Name))->SetPropertyValue_InContainer(Object, Value);
}

// Captured game class defaults. Every other own field is zero, false, null or an empty table.
void CircleDefaults(UObject* Defaults)
{
    auto* Module = CastChecked<UParticleModule>(Defaults);
    Module->bSpawnModule = true;
    Module->bSupported3DDrawMode = true;
    SetField<FFloatProperty>(Defaults, TEXT("fSweepEnd"), 360.f);
}
void SphereDefaults(UObject* Defaults)
{
    auto* Module = CastChecked<UParticleModule>(Defaults);
    Module->bSpawnModule = true;
    Module->bSupported3DDrawMode = true;
    SetField<FFloatProperty>(Defaults, TEXT("fSweepEnd"), 360.f);
}
void DecalDefaults(UObject* Defaults)
{
    CastChecked<UParticleModule>(Defaults)->bSupported3DDrawMode = true;
    SetField<FFloatProperty>(Defaults, TEXT("FadeScreenSize"), 0.01f);
    SetField<FBoolProperty>(Defaults, TEXT("bDestroyOwnerAfterFade"), true);
    SetField<FNameProperty>(Defaults, TEXT("MaterialColorParamName"), FName(TEXT("DecalColor")));
}

UObject* RegisterForLoader()
{
    return P4RegisterGameParticleModuleClasses();
}

const FClassSpec Classes[] = {
    {TEXT("ParticleModuleLocationPrimitiveC4Circle"), &CircleParent, CircleFields, int32(UE_ARRAY_COUNT(CircleFields)),
        &ConstructCircle, &CircleDefaults, &RegisterForLoader},
    {TEXT("ParticleModuleLocationPrimitiveC4Sphere"), &SphereParent, SphereFields, int32(UE_ARRAY_COUNT(SphereFields)),
        &ConstructSphere, &SphereDefaults, &RegisterForLoader},
    {TEXT("ParticleModuleTypeDataDecal"), &DecalParent, DecalFields, int32(UE_ARRAY_COUNT(DecalFields)),
        &ConstructDecal, &DecalDefaults, &RegisterForLoader}};

const UScriptStruct* RawStruct(EField Kind)
{
    return Kind == EField::Vector ? FRawDistributionVector::StaticStruct()
        : Kind == EField::Float   ? FRawDistributionFloat::StaticStruct()
                                  : nullptr;
}

bool Valid(UClass* Class, const FClassSpec& Spec)
{
    UClass* Parent = Spec.Parent();
    if (!Class || Class->GetSuperClass() != Parent || Class->ClassConstructor != Spec.Constructor ||
        Class->ClassAddReferencedObjects != Parent->ClassAddReferencedObjects)
        return false;
    int32 Count = 0;
    for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
        ++Count;
    if (Count != Spec.FieldCount)
        return false;
    for (int32 Index = 0; Index < Spec.FieldCount; ++Index)
    {
        const FFieldSpec& Field = Spec.Fields[Index];
        const FProperty* Property = Class->FindPropertyByName(Field.Name);
        if (!Property || Property->GetOwnerStruct() != Class || Property->ArrayDim != 1 ||
            Property->GetOffset_ForInternal() < Parent->GetPropertiesSize() ||
            Property->GetOffset_ForInternal() + Property->GetSize() > Class->GetPropertiesSize())
            return false;
        bool Typed = false;
        switch (Field.Kind)
        {
            case EField::Vector:
            case EField::Float:
                Typed = CastField<FStructProperty>(Property) &&
                    CastField<FStructProperty>(Property)->Struct == RawStruct(Field.Kind);
                break;
            case EField::Int:
                Typed = CastField<FIntProperty>(Property) != nullptr;
                break;
            case EField::Number:
                Typed = CastField<FFloatProperty>(Property) != nullptr;
                break;
            case EField::Bool:
                Typed = CastField<FBoolProperty>(Property) && CastField<FBoolProperty>(Property)->IsNativeBool();
                break;
            case EField::Name:
                Typed = CastField<FNameProperty>(Property) != nullptr;
                break;
            case EField::DivisionType:
            {
                const auto* Enum = CastField<FEnumProperty>(Property);
                const auto* Byte = Enum ? CastField<FByteProperty>(Enum->GetUnderlyingProperty()) : nullptr;
                Typed = Enum && ValidDivisionType(Enum->GetEnum()) && Byte && !Byte->Enum && Byte->ArrayDim == 1 &&
                    Byte->GetSize() == sizeof(uint8) && Byte->GetOwner<FEnumProperty>() == Enum;
                break;
            }
            case EField::Material:
                Typed = CastField<FObjectProperty>(Property) &&
                    CastField<FObjectProperty>(Property)->PropertyClass == UMaterialInterface::StaticClass();
                break;
        }
        if (!Typed)
            return false;
    }
    return true;
}

FProperty* MakeField(UClass* Class, const FFieldSpec& Spec)
{
    FProperty* Field = nullptr;
    switch (Spec.Kind)
    {
        case EField::Vector:
        case EField::Float:
        {
            auto* Struct = new FStructProperty(Class, Spec.Name, RF_Public);
            Struct->Struct = const_cast<UScriptStruct*>(RawStruct(Spec.Kind));
            Field = Struct;
            break;
        }
        case EField::Int:
            Field = new FIntProperty(Class, Spec.Name, RF_Public);
            break;
        case EField::Number:
            Field = new FFloatProperty(Class, Spec.Name, RF_Public);
            break;
        case EField::Bool:
        {
            auto* Bool = new FBoolProperty(Class, Spec.Name, RF_Public);
            Bool->SetBoolSize(sizeof(bool), true);
            Field = Bool;
            break;
        }
        case EField::Name:
            Field = new FNameProperty(Class, Spec.Name, RF_Public);
            break;
        case EField::DivisionType:
        {
            UEnum* Enum = DivisionType();
            if (!Enum)
                return nullptr;
            // The enum-taking constructor calls FProperty::Init and auto-inserts
            // itself. Use the unlinked constructor; RegisterOne inserts every field.
            auto* Property = new FEnumProperty(Class, Spec.Name, RF_Public);
            Property->SetEnum(Enum);
            Property->SetPropertyFlags(CPF_HasGetValueTypeHash);
            Property->AddCppProperty(new FByteProperty(Property, TEXT("UnderlyingType"), RF_Public));
            Field = Property;
            break;
        }
        case EField::Material:
        {
            auto* Object = new FObjectProperty(Class, Spec.Name, RF_Public);
            Object->PropertyClass = UMaterialInterface::StaticClass();
            Field = Object;
            break;
        }
    }
    Field->SetPropertyFlags(EPropertyFlags(Spec.Flags));
    Field->SetMetaData(TEXT("Category"), TEXT("Game"));
    return Field;
}

void Discard(UClass* Class)
{
    Class->ClearFlags(RF_Public | RF_Standalone);
    Class->MarkPendingKill();
}

UClass* RegisterOne(const FClassSpec& Spec)
{
    UClass* Parent = Spec.Parent();
    if (!Parent || !Parent->ClassConstructor)
        return nullptr;
    if (UClass* Existing = FindObjectFast<UClass>(Parent->GetOutermost(), Spec.Name))
        return Valid(Existing, Spec) ? Existing : nullptr;
    // The same native-construction mechanism as the other recovered engine classes: a new
    // child of the real parent in the parent's script package, keeping the game's identity
    // so original particle assets resolve their typed module references.
    UClass* Class = NewObject<UClass>(Parent->GetOutermost(), Spec.Name, RF_Public | RF_Standalone);
    Class->SetSuperStruct(Parent);
    Class->ClassWithin = Parent->ClassWithin;
    Class->ClassConfigName = Parent->ClassConfigName;
    Class->ClassFlags = EClassFlags((Parent->ClassFlags & CLASS_Inherit) & ~CLASS_Abstract);
    Class->ClassCastFlags = Parent->ClassCastFlags;
    Class->ClassConstructor = Spec.Constructor;
    Class->ClassVTableHelperCtorCaller = Parent->ClassVTableHelperCtorCaller;
    Class->ClassAddReferencedObjects = Parent->ClassAddReferencedObjects;
    // AddCppProperty prepends, so add in reverse to link the captured order.
    for (int32 Index = Spec.FieldCount - 1; Index >= 0; --Index)
    {
        FField* Previous = Class->ChildProperties;
        FProperty* Field = MakeField(Class, Spec.Fields[Index]);
        if (!Field)
        {
            Discard(Class);
            return nullptr;
        }
        // Refuse an unexpected auto-insertion before a second add can form a
        // cycle, leaving the unpublished class's already finite chain intact.
        if (Class->ChildProperties != Previous || Field->Next)
        {
            Discard(Class);
            return nullptr;
        }
        Class->AddCppProperty(Field);
    }
    TArray<FName> ExpectedFields;
    for (int32 Index = 0; Index < Spec.FieldCount; ++Index)
        ExpectedFields.Add(FName(Spec.Fields[Index].Name));
    if (!P4GameParticleModuleFields::ReadyToLink(Class, ExpectedFields))
    {
        Discard(Class);
        return nullptr;
    }
    FP4ManualEngineClassRegistration Registration(Class, Spec.Register);
    if (!Registration.IsValid())
    {
        Discard(Class);
        return nullptr;
    }
    Class->Bind();
    Class->StaticLink(true);
    if (!Valid(Class, Spec) || !Registration.FinishClass())
    {
        Discard(Class);
        return nullptr;
    }
    Class->AddToRoot();
    Class->GetOutermost()->GetMetaData()->SetValue(Class, TEXT("P4DeclarationOnly"), TEXT("1"));
    Spec.Defaults(Class->GetDefaultObject());
    return Class;
}
} //namespace P4GameParticleModules

UClass* P4RegisterGameParticleModuleClasses()
{
    using namespace P4GameParticleModules;
    if (!IsInGameThread())
        return nullptr;
    UClass* First = nullptr;
    for (const FClassSpec& Spec : Classes)
    {
        UClass* Class = RegisterOne(Spec);
        if (!First)
            First = Class;
    }
    return First;
}

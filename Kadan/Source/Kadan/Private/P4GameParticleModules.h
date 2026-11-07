#pragma once
#include "CoreMinimal.h"
#include "UObject/Class.h"
#include "UObject/EnumProperty.h"
#include "UObject/UnrealType.h"

class UClass;

// The game's own particle module classes that the stock editor lacks. Each one keeps
// the game's script identity, reflected fields and class defaults, so original particle
// assets import as real assets with typed modules. The game's runtime behavior of the
// module is not implemented; the stock parent's behavior applies in the editor.
// Registered with the recovered particle system class, before any original particle
// asset is read or loaded: the circle and sphere location modules and decal type data.
// Returns the circle location module class, or null.
KADAN_API UClass* P4RegisterGameParticleModuleClasses();

namespace P4GameParticleModuleFields
{
// Run before StaticLink: the bounded own chain must contain each declaration
// once, in order. A convenience property constructor can already insert itself.
inline bool ReadyToLink(const UClass* Class, const TArray<FName>& Expected)
{
    if (!Class || Expected.Num() < 1 || Expected.Num() > 64) return false;
    const FField* Field = Class->ChildProperties;
    TSet<const FField*> Seen;
    for (const FName& Name : Expected)
    {
        const FProperty* Property = CastField<FProperty>(Field);
        if (!Property || Seen.Contains(Field) || Property->GetOwner<UObject>() != Class || Property->GetFName() != Name) return false;
        Seen.Add(Field);
        if (const FEnumProperty* Enum = CastField<FEnumProperty>(Property))
        {
            const FByteProperty* Byte = CastField<FByteProperty>(Enum->GetUnderlyingProperty());
            if (!Enum->GetEnum() || !Byte || Byte->GetOwner<FEnumProperty>() != Enum || Byte->Next || Byte->Enum || Byte->ArrayDim != 1) return false;
        }
        Field = Field->Next;
    }
    return Field == nullptr;
}
}

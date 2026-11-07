#pragma once
#include "CoreMinimal.h"
#include "UObject/UnrealType.h"
#include "P4MapKitCore/property_decoder.h"
#include "P4PersistentTextFlags.h"
#include "P4UnkeyedTextLiteral.h"

// Cross-engine comparisons use Unreal's name semantics. They never change the
// imported state digest or the spelling of a serialized FString.
inline void P4ComparisonNames(P4MapKit::FDecodedValue& Value, const FProperty* Property)
{
    using P4MapKit::FDecodedValue;
    if (!Property)
        return;
    if (CastField<FTextProperty>(Property))
    {
        P4PersistentTextFlags(Value);
        P4CanonicalUnkeyedTextLiteral(Value);
    }
    if (CastField<FObjectPropertyBase>(Property) || CastField<FNameProperty>(Property))
    {
        if (Value.Kind == FDecodedValue::EKind::String)
        {
            const FString Lower = FString(UTF8_TO_TCHAR(Value.Text.c_str())).ToLower();
            FTCHARToUTF8 Utf8(*Lower);
            Value.Text.assign(Utf8.Get(), Utf8.Length());
        }
    }
    else if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
        for (FDecodedValue& Item : Value.Items)
            P4ComparisonNames(Item, Array->Inner);
    else if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
        for (auto& Field : Value.Fields)
        {
            const FProperty* Member = Struct->Struct->FindPropertyByName(FName(UTF8_TO_TCHAR(Field.first.c_str())));
            P4ComparisonNames(Field.second, Member);
            // Serialized struct member keys are FNames as well. Normalize only keys
            // independently resolved in the real schema; authored FString values stay exact.
            if (Member)
                for (char& Letter : Field.first)
                    if (Letter >= 'A' && Letter <= 'Z')
                        Letter += 'a' - 'A';
        }
}

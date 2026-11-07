// Property text: Unreal's text form of a property value, checked strictly before it is imported, and its
// application to an object through the editor's property chain.

#include "P4PropertyValuesInternal.h"
#include "P4AuthoringRows.h"
#include "P4GamePaths.h"
#include "P4GameNames.h"
#include "P4NativeHash.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "UObject/UnrealType.h"
#include "UObject/TextProperty.h"
#include "Components/SceneComponent.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Blueprint.h"
#include "Engine/DataAsset.h"
#include "AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "Serialization/ArchiveUObject.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "UObject/StructOnScope.h"
#include "Materials/MaterialInterface.h"
#include "P4TextValues.h"
#include "P4ImportReferences.h"
#include "ScopedTransaction.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GCObject.h"
#include "Serialization/StructuredArchive.h"
#include "Misc/OutputDevice.h"

namespace P4PropertyValuesDetail
{
bool HasEmbeddedNul(const FString& Text)
{
    for (TCHAR C : Text)
        if (!C)
            return true;
    return false;
}
bool EditorPropertyWritable(FProperty* Property, bool Template)
{
    return Property && Property->HasAnyPropertyFlags(CPF_Edit) && !Property->HasAnyPropertyFlags(CPF_EditConst) &&
        !Property->HasAnyPropertyFlags(Template ? CPF_DisableEditOnTemplate : CPF_DisableEditOnInstance);
}
bool ResolveProperty(UObject* Object, FName Name, bool Editable, FResolvedEditProperty& Out)
{
    if (!IsInGameThread() || !IsValid(Object))
        return false;
    const FString Text = Name.ToString();
    if (Text.Len() > 4096 || HasEmbeddedNul(Text))
        return false;
    TArray<FString> Path;
    Text.ParseIntoArray(Path, TEXT("."), false);
    if (!Path.Num() || Path.Num() > 16)
        return false;
    UStruct* Type = Object->GetClass();
    void* Container = Object;
    const bool Template = Object->IsTemplate();
    for (int32 At = 0; At < Path.Num(); ++At)
    {
        const FString& Segment = Path[At];
        int32 Bracket = INDEX_NONE;
        Segment.FindChar('[', Bracket);
        const FString Field = Bracket == INDEX_NONE ? Segment : Segment.Left(Bracket);
        if (Field.IsEmpty() || Field.Contains(TEXT("]")))
            return false;
        FProperty* Property = Type->FindPropertyByName(FName(*Field));
        if (!Property || (Editable && !EditorPropertyWritable(Property, Template)))
            return false;
        Out.Chain.AddTail(Property);
        void* Value = Property->ContainerPtrToValuePtr<void>(Container);
        bool Single = false;
        int32 Position = Bracket, Indices = 0;
        while (Position != INDEX_NONE && Position < Segment.Len())
        {
            if (++Indices > 16 || Segment[Position++] != '[')
                return false;
            int32 Index = 0, Digits = 0;
            while (Position < Segment.Len() && Segment[Position] >= '0' && Segment[Position] <= '9')
            {
                const int32 Digit = Segment[Position++] - '0';
                if (Index > (MAX_int32 - Digit) / 10)
                    return false;
                Index = Index * 10 + Digit;
                ++Digits;
            }
            if (!Digits || Position >= Segment.Len() || Segment[Position++] != ']')
                return false;
            Out.ArrayIndices.Add(Property->GetName(), Index);
            if (Property->ArrayDim > 1 && !Single)
            {
                if (Index >= Property->ArrayDim)
                    return false;
                Value = static_cast<uint8*>(Value) + SIZE_T(Index) * Property->ElementSize;
                Single = true;
            }
            else
            {
                auto Array = CastField<FArrayProperty>(Property);
                if (!Array)
                    return false;
                FScriptArrayHelper Helper(Array, Value);
                if (!Helper.IsValidIndex(Index))
                    return false;
                Value = Helper.GetRawPtr(Index);
                Property = Array->Inner;
                Single = false;
                Out.Chain.AddTail(Property);
            }
        }
        if (At + 1 < Path.Num())
        {
            auto Struct = CastField<FStructProperty>(Property);
            if (!Struct || (Property->ArrayDim > 1 && !Single))
                return false;
            Type = Struct->Struct;
            Container = Value;
        }
        Out.Property = Property;
        Out.Value = Value;
        Out.Single = Single;
    }
    Out.Chain.SetActivePropertyNode(Out.Property);
    Out.Chain.SetActiveMemberPropertyNode(Out.Chain.GetHead()->GetValue());
    return true;
}
// Split ordinary UE parenthesized text without splitting quoted commas or
// nested struct/array/map values. Unknown/missing/duplicate fields refuse.
bool TextParts(const FString& Text, TArray<FString>& Parts)
{
    const FString S = Text.TrimStartAndEnd();
    if (S.Len() < 2 || S[0] != '(' || S[S.Len() - 1] != ')')
        return false;
    int32 Depth = 0, Begin = 1;
    bool Quoted = false, Escape = false;
    for (int32 At = 1; At < S.Len() - 1; ++At)
    {
        const TCHAR C = S[At];
        if (Quoted)
        {
            if (Escape)
                Escape = false;
            else if (C == '\\')
                Escape = true;
            else if (C == '"')
                Quoted = false;
            continue;
        }
        if (C == '"')
            Quoted = true;
        else if (C == '(')
        {
            if (++Depth > 32)
                return false;
        }
        else if (C == ')')
        {
            if (--Depth < 0)
                return false;
        }
        else if (C == ',' && !Depth)
        {
            const FString Part = S.Mid(Begin, At - Begin).TrimStartAndEnd();
            if (Part.IsEmpty())
                return false;
            Parts.Add(Part);
            Begin = At + 1;
            if (Parts.Num() > 65536)
                return false;
        }
    }
    if (Quoted || Escape || Depth)
        return false;
    const FString Last = S.Mid(Begin, S.Len() - 1 - Begin).TrimStartAndEnd();
    if (!Last.IsEmpty())
        Parts.Add(Last);
    else if (Parts.Num())
        return false;
    return true;
}
bool StrictNumber(FNumericProperty* Property, const FString& Text, void* Destination = nullptr)
{
    FString S = Text.TrimStartAndEnd();
    if (S.IsEmpty())
        return false;
    const bool Unsigned = CastField<FByteProperty>(Property) || CastField<FUInt16Property>(Property) ||
        CastField<FUInt32Property>(Property) || CastField<FUInt64Property>(Property);
    if (Property->IsInteger())
    {
        int32 At = (S[0] == '-' || S[0] == '+') ? 1 : 0;
        const bool Negative = S[0] == '-';
        if (At == S.Len() || (Unsigned && Negative))
            return false;
        uint64 Value = 0;
        uint32 Base = 10;
        if (S.Len() - At > 2 && S[At] == '0' && (S[At + 1] == 'x' || S[At + 1] == 'X'))
        {
            Base = 16;
            At += 2;
        }
        for (; At < S.Len(); ++At)
        {
            const TCHAR C = S[At];
            const uint32 Digit = C >= '0' && C <= '9' ? C - '0'
                : C >= 'a' && C <= 'f'                ? C - 'a' + 10
                : C >= 'A' && C <= 'F'                ? C - 'A' + 10
                                                      : 99;
            if (Digit >= Base || Value > (MAX_uint64 - Digit) / Base)
                return false;
            Value = Value * Base + Digit;
        }
        const uint32 Bits = uint32(Property->ElementSize) * 8;
        if (!Bits || Bits > 64)
            return false;
        const uint64 Max = Unsigned ? Bits == 64 ? MAX_uint64 : (uint64(1) << Bits) - 1
            : Negative              ? uint64(1) << (Bits - 1)
                                    : (uint64(1) << (Bits - 1)) - 1;
        if (Value > Max)
            return false;
        if (Destination)
        {
            if (Unsigned)
                Property->SetIntPropertyValue(Destination, Value);
            else
                Property->SetIntPropertyValue(
                    Destination, Negative ? Value == uint64(MAX_int64) + 1 ? MIN_int64 : -int64(Value) : int64(Value));
        }
        return true;
    }
    if (S.EndsWith(TEXT("f"), ESearchCase::IgnoreCase))
        S.LeftChopInline(1);
    int32 At = 0;
    if (At < S.Len() && (S[At] == '+' || S[At] == '-'))
        ++At;
    int32 Digits = 0;
    while (At < S.Len() && S[At] >= '0' && S[At] <= '9')
    {
        ++At;
        ++Digits;
    }
    if (At < S.Len() && S[At] == '.')
    {
        ++At;
        while (At < S.Len() && S[At] >= '0' && S[At] <= '9')
        {
            ++At;
            ++Digits;
        }
    }
    if (!Digits)
        return false;
    if (At < S.Len() && (S[At] == 'e' || S[At] == 'E'))
    {
        ++At;
        if (At < S.Len() && (S[At] == '+' || S[At] == '-'))
            ++At;
        const int32 Begin = At;
        while (At < S.Len() && S[At] >= '0' && S[At] <= '9')
            ++At;
        if (At == Begin)
            return false;
    }
    if (At != S.Len())
        return false;
    const double Value = FCString::Atod(*S);
    if (!FMath::IsFinite(Value))
        return false;
    bool NonzeroMantissa = false;
    for (TCHAR C : S)
    {
        if (C == 'e' || C == 'E')
            break;
        if (C >= '1' && C <= '9')
            NonzeroMantissa = true;
    }
    if (Value == 0 && NonzeroMantissa)
        return false;
    if (CastField<FFloatProperty>(Property) && (!FMath::IsFinite(float(Value)) || (Value != 0 && float(Value) == 0)))
        return false;
    if (Destination)
        Property->SetFloatingPointPropertyValue(Destination, Value);
    return true;
}
bool ValidatePropertyText(
    FProperty* Property, const FString& Text, bool Single, bool Editable, bool Template, int32 Depth = 0)
{
    if (!Property || Depth > 32 || Text.Len() > 1024 * 1024 || HasEmbeddedNul(Text))
        return false;
    const FString S = Text.TrimStartAndEnd();
    TArray<FString> Parts;
    if (Property->ArrayDim > 1 && !Single)
    {
        if (!TextParts(S, Parts) || Parts.Num() != Property->ArrayDim)
            return false;
        for (const auto& Part : Parts)
            if (!ValidatePropertyText(Property, Part, true, Editable, Template, Depth + 1))
                return false;
        return true;
    }
    if (auto Number = CastField<FNumericProperty>(Property))
        if (!Number->GetIntPropertyEnum())
            return StrictNumber(Number, S);
    if (auto Array = CastField<FArrayProperty>(Property))
    {
        if (!TextParts(S, Parts))
            return false;
        for (const auto& Part : Parts)
            if (!ValidatePropertyText(Array->Inner, Part, false, Editable, Template, Depth + 1))
                return false;
        return true;
    }
    if (auto Map = CastField<FMapProperty>(Property))
    {
        if (!TextParts(S, Parts))
            return false;
        for (const auto& Part : Parts)
        {
            TArray<FString> Pair;
            if (!TextParts(Part, Pair) || Pair.Num() != 2 ||
                !ValidatePropertyText(Map->KeyProp, Pair[0], false, Editable, Template, Depth + 1) ||
                !ValidatePropertyText(Map->ValueProp, Pair[1], false, Editable, Template, Depth + 1))
                return false;
        }
        return true;
    }
    if (auto Struct = CastField<FStructProperty>(Property))
    {
        if (!TextParts(S, Parts))
            return false;
        TSet<FName> Fields;
        for (const auto& Part : Parts)
        {
            int32 Equal;
            if (!Part.FindChar('=', Equal))
                return false;
            const FString Key = Part.Left(Equal).TrimStartAndEnd();
            FProperty* Child = Struct->Struct->FindPropertyByName(FName(*Key));
            if (!Child || Fields.Contains(Child->GetFName()) || (Editable && !EditorPropertyWritable(Child, Template)))
                return false;
            Fields.Add(Child->GetFName());
            if (!ValidatePropertyText(Child, Part.Mid(Equal + 1), false, Editable, Template, Depth + 1))
                return false;
        }
        return true;
    }
    if (CastField<FNameProperty>(Property) && S.Len() >= NAME_SIZE)
        return false;
    return true; // Engine imports enums/text/references/strings with errors captured.
}
struct FPropertyImportErrors : FOutputDevice
{
    bool HadMessage = false;
    virtual void Serialize(const TCHAR*, ELogVerbosity::Type, const FName&) override { HadMessage = true; }
};
bool ImportStrictProperty(FProperty* Property, const FString& Text, void* Destination, UObject* Owner, bool Single,
    FPropertyImportErrors& Errors)
{
    const FString S = Text.TrimStartAndEnd();
    TArray<FString> Parts;
    if (Property->ArrayDim > 1 && !Single)
    {
        if (!TextParts(S, Parts) || Parts.Num() != Property->ArrayDim)
            return false;
        for (int32 Index = 0; Index < Parts.Num(); ++Index)
            if (!ImportStrictProperty(Property, Parts[Index],
                    static_cast<uint8*>(Destination) + SIZE_T(Index) * Property->ElementSize, Owner, true, Errors))
                return false;
        return true;
    }
    if (auto Number = CastField<FNumericProperty>(Property))
        if (!Number->GetIntPropertyEnum())
            return StrictNumber(Number, S, Destination);
    if (auto Array = CastField<FArrayProperty>(Property))
    {
        if (!TextParts(S, Parts))
            return false;
        FScriptArrayHelper Helper(Array, Destination);
        Helper.Resize(Parts.Num());
        for (int32 Index = 0; Index < Parts.Num(); ++Index)
            if (!ImportStrictProperty(Array->Inner, Parts[Index], Helper.GetRawPtr(Index), Owner, false, Errors))
                return false;
        return true;
    }
    if (auto Struct = CastField<FStructProperty>(Property))
    {
        if (!TextParts(S, Parts))
            return false;
        for (const auto& Part : Parts)
        {
            int32 Equal;
            if (!Part.FindChar('=', Equal))
                return false;
            FProperty* Child = Struct->Struct->FindPropertyByName(FName(*Part.Left(Equal).TrimStartAndEnd()));
            if (!Child ||
                !ImportStrictProperty(
                    Child, Part.Mid(Equal + 1), Child->ContainerPtrToValuePtr<void>(Destination), Owner, false, Errors))
                return false;
        }
        return true;
    }
    if (auto Map = CastField<FMapProperty>(Property))
    {
        if (!TextParts(S, Parts) || Parts.Num() > 4096)
            return false;
        FScriptMapHelper Helper(Map, Destination);
        Helper.EmptyValues();
        uint32 Comparisons = 0;
        for (const auto& Part : Parts)
        {
            TArray<FString> Pair;
            if (!TextParts(Part, Pair) || Pair.Num() != 2)
                return false;
            // Import directly into initialized, GC-traced map storage. UE's
            // text importer instead stages each pair in an untraced buffer.
            const int32 Index = Helper.AddDefaultValue_Invalid_NeedsRehash();
            if (!ImportStrictProperty(Map->KeyProp, Pair[0], Helper.GetKeyPtr(Index), Owner, false, Errors) ||
                !ImportStrictProperty(Map->ValueProp, Pair[1], Helper.GetValuePtr(Index), Owner, false, Errors))
                return false;
            for (int32 Previous = 0; Previous < Index; ++Previous)
            {
                if (!Helper.IsValidIndex(Previous))
                    continue;
                if (++Comparisons > 4u * 1024u * 1024u ||
                    Map->KeyProp->Identical(Helper.GetKeyPtr(Previous), Helper.GetKeyPtr(Index)))
                    return false;
            }
        }
        Helper.Rehash();
        return !Errors.HadMessage;
    }
    const bool Delimited = S.StartsWith(TEXT("\"")) &&
        (CastField<FNameProperty>(Property) || CastField<FStrProperty>(Property));
    const FString& Input = Delimited ? S : Text;
    const TCHAR* End = Property->ImportText(*Input, Destination, Delimited ? PPF_Delimited : PPF_None, Owner, &Errors);
    if (!End)
        return false;
    while (*End && FChar::IsWhitespace(*End))
        ++End;
    if (*End || Errors.HadMessage)
        return false;
    // UE's hard-object ImportText ignores ParseObjectPropertyValue's failure
    // result, can log externally, then returns consumed text with a null
    // value. A rejected or missing reference must never become a valid edit.
    if (auto ObjectProperty = CastField<FObjectProperty>(Property))
        if (!ObjectProperty->GetObjectPropertyValue(Destination) && !S.Equals(TEXT("None"), ESearchCase::IgnoreCase))
            return false;
    UEnum* Enum = nullptr;
    int64 Value = 0;
    if (auto EnumProperty = CastField<FEnumProperty>(Property))
    {
        Enum = EnumProperty->GetEnum();
        Value = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(Destination);
    }
    else if (auto Byte = CastField<FByteProperty>(Property))
    {
        Enum = Byte->Enum;
        Value = Byte->GetPropertyValue(Destination);
    }
    if (Enum && !Enum->IsValidEnumValue(Value))
        return false;
    return true;
}

// Staged values are not UObject properties yet. Trace their real native
// serialization so references imported earlier remain alive during later
// dependency loads and editor callbacks, including nested arrays/maps.
struct FStagedPropertyValue : FGCObject
{
    FProperty* Property;
    UStruct* Owner;
    void* Value;
    explicit FStagedPropertyValue(FProperty* InProperty)
        : Property(InProperty), Owner(InProperty->GetOwnerStruct()),
          Value(FMemory::Malloc(InProperty->GetSize(), InProperty->GetMinAlignment()))
    {
        Property->InitializeValue(Value);
    }
    virtual ~FStagedPropertyValue() override
    {
        Property->DestroyValue(Value);
        FMemory::Free(Value);
    }
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override
    {
        Collector.AddReferencedObject(Owner);
        for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
            Property->SerializeItem(
                FStructuredArchiveFromArchive(Collector.GetVerySlowReferenceCollectorArchive()).GetSlot(),
                static_cast<uint8*>(Value) + SIZE_T(Index) * Property->ElementSize, nullptr);
    }
    virtual FString GetReferencerName() const override { return TEXT("P4StagedPropertyValue"); }
};
enum class EP4ZeroIdentity
{
    Same,
    Different,
    Unavailable
};
struct FP4ZeroIdentityBudget
{
    uint32 Nodes = 0;
    uint32 MapComparisons = 0;
};

EP4ZeroIdentity P4ZeroIdentityWalk(
    FProperty* Property, const void* A, const void* B, bool Single, int32 Depth, FP4ZeroIdentityBudget& Budget)
{
    if (!Property || !A || !B || Depth > 32 || ++Budget.Nodes > 65536u || Property->ElementSize <= 0 ||
        Property->ArrayDim <= 0)
        return EP4ZeroIdentity::Unavailable;
    if (!Single && Property->ArrayDim > 1)
    {
        if (uint32(Property->ArrayDim) > 65536u - Budget.Nodes)
            return EP4ZeroIdentity::Unavailable;
        for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
        {
            const SIZE_T Offset = SIZE_T(Index) * SIZE_T(Property->ElementSize);
            const auto Result = P4ZeroIdentityWalk(Property, static_cast<const uint8*>(A) + Offset,
                static_cast<const uint8*>(B) + Offset, true, Depth + 1, Budget);
            if (Result != EP4ZeroIdentity::Same)
                return Result;
        }
        return EP4ZeroIdentity::Same;
    }
    if (auto String = CastField<FStrProperty>(Property))
    {
        return P4ExactNativeString(String->GetPropertyValue(A), String->GetPropertyValue(B))
            ? EP4ZeroIdentity::Same
            : EP4ZeroIdentity::Different;
    }
    if (CastField<FFloatProperty>(Property))
    {
        if (Property->ElementSize != int32(sizeof(float)))
            return EP4ZeroIdentity::Unavailable;
        uint32 BitsA = 0, BitsB = 0;
        FMemory::Memcpy(&BitsA, A, sizeof(BitsA));
        FMemory::Memcpy(&BitsB, B, sizeof(BitsB));
        return (BitsA & 0x7fffffffu) == 0 && (BitsB & 0x7fffffffu) == 0 && BitsA != BitsB ? EP4ZeroIdentity::Different
                                                                                          : EP4ZeroIdentity::Same;
    }
    if (CastField<FDoubleProperty>(Property))
    {
        if (Property->ElementSize != int32(sizeof(double)))
            return EP4ZeroIdentity::Unavailable;
        uint64 BitsA = 0, BitsB = 0;
        FMemory::Memcpy(&BitsA, A, sizeof(BitsA));
        FMemory::Memcpy(&BitsB, B, sizeof(BitsB));
        return (BitsA & 0x7fffffffffffffffULL) == 0 && (BitsB & 0x7fffffffffffffffULL) == 0 && BitsA != BitsB
            ? EP4ZeroIdentity::Different
            : EP4ZeroIdentity::Same;
    }
    if (auto* Struct = CastField<FStructProperty>(Property))
    {
        if (!Struct->Struct)
            return EP4ZeroIdentity::Unavailable;
        for (TFieldIterator<FProperty> Field(Struct->Struct); Field; ++Field)
        {
            // Match the already successful JsonValue source projection. Native
            // struct identity remains a separate gate, including native ops.
            if (Field->HasAnyPropertyFlags(CPF_Transient))
                continue;
            const auto Result = P4ZeroIdentityWalk(*Field, Field->ContainerPtrToValuePtr<void>(A),
                Field->ContainerPtrToValuePtr<void>(B), false, Depth + 1, Budget);
            if (Result != EP4ZeroIdentity::Same)
                return Result;
        }
        return EP4ZeroIdentity::Same;
    }
    if (auto* Array = CastField<FArrayProperty>(Property))
    {
        if (!Array->Inner)
            return EP4ZeroIdentity::Unavailable;
        FScriptArrayHelper Left(Array, A), Right(Array, B);
        if (Left.Num() != Right.Num())
            return EP4ZeroIdentity::Different;
        if (uint32(Left.Num()) > 65536u - Budget.Nodes)
            return EP4ZeroIdentity::Unavailable;
        for (int32 Index = 0; Index < Left.Num(); ++Index)
        {
            const auto Result = P4ZeroIdentityWalk(
                Array->Inner, Left.GetRawPtr(Index), Right.GetRawPtr(Index), false, Depth + 1, Budget);
            if (Result != EP4ZeroIdentity::Same)
                return Result;
        }
        return EP4ZeroIdentity::Same;
    }
    if (auto* Map = CastField<FMapProperty>(Property))
    {
        if (!Map->KeyProp || !Map->ValueProp)
            return EP4ZeroIdentity::Unavailable;
        FScriptMapHelper Left(Map, A), Right(Map, B);
        if (Left.Num() != Right.Num())
            return EP4ZeroIdentity::Different;
        if (Left.Num() > 4096 || Left.GetMaxIndex() > 65536 || Right.GetMaxIndex() > 65536 ||
            uint32(Left.Num()) > (65536u - Budget.Nodes) / 2u)
            return EP4ZeroIdentity::Unavailable;
        // Sparse holes are work too: charge both native slot scans to the same
        // whole-comparison node budget, not a fresh budget per nested map.
        const uint32 Slots = uint32(Left.GetMaxIndex()) + uint32(Right.GetMaxIndex());
        if (Slots > 65536u - Budget.Nodes)
            return EP4ZeroIdentity::Unavailable;
        Budget.Nodes += Slots;
        TArray<int32> LeftIndices, RightIndices;
        LeftIndices.Reserve(Left.Num());
        RightIndices.Reserve(Right.Num());
        for (int32 Index = 0; Index < Left.GetMaxIndex(); ++Index)
            if (Left.IsValidIndex(Index))
                LeftIndices.Add(Index);
        for (int32 Index = 0; Index < Right.GetMaxIndex(); ++Index)
            if (Right.IsValidIndex(Index))
                RightIndices.Add(Index);
        if (LeftIndices.Num() != Left.Num() || RightIndices.Num() != Right.Num())
            return EP4ZeroIdentity::Unavailable;
        // Strict text preparation already refuses native-equivalent key
        // duplicates in B. Still enforce one-to-one matching without hashes,
        // pair order, reference paths or a floating key identity replacement.
        TBitArray<> Used(false, RightIndices.Num());
        for (int32 LeftIndex : LeftIndices)
        {
            int32 Match = INDEX_NONE;
            for (int32 Candidate = 0; Candidate < RightIndices.Num(); ++Candidate)
            {
                if (Used[Candidate])
                    continue;
                if (++Budget.MapComparisons > 4u * 1024u * 1024u)
                    return EP4ZeroIdentity::Unavailable;
                if (!Map->KeyProp->Identical(Left.GetKeyPtr(LeftIndex), Right.GetKeyPtr(RightIndices[Candidate])))
                    continue;
                if (Match != INDEX_NONE)
                    return EP4ZeroIdentity::Unavailable;
                Match = Candidate;
            }
            if (Match == INDEX_NONE)
                return EP4ZeroIdentity::Unavailable;
            Used[Match] = true;
            const int32 RightIndex = RightIndices[Match];
            auto Result = P4ZeroIdentityWalk(
                Map->KeyProp, Left.GetKeyPtr(LeftIndex), Right.GetKeyPtr(RightIndex), false, Depth + 1, Budget);
            if (Result != EP4ZeroIdentity::Same)
                return Result;
            Result = P4ZeroIdentityWalk(
                Map->ValueProp, Left.GetValuePtr(LeftIndex), Right.GetValuePtr(RightIndex), false, Depth + 1, Budget);
            if (Result != EP4ZeroIdentity::Same)
                return Result;
        }
        return EP4ZeroIdentity::Same;
    }
    // Supplement only zero-sign identity. Original native semantics govern
    // object references, names, texts, enums, integers and all other leaves.
    return EP4ZeroIdentity::Same;
}

EP4ZeroIdentity P4ZeroIdentityCompare(FProperty* Property, const void* A, const void* B, bool Single)
{
    if (!Property || !A || !B || Property->ArrayDim <= 0 || Property->ArrayDim > 65536 || Property->ElementSize <= 0)
        return EP4ZeroIdentity::Unavailable;
    FP4ZeroIdentityBudget Budget;
    const int32 Count = Single ? 1 : Property->ArrayDim;
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const SIZE_T Offset = SIZE_T(Index) * SIZE_T(Property->ElementSize);
        const void* Left = static_cast<const uint8*>(A) + Offset;
        const void* Right = static_cast<const uint8*>(B) + Offset;
        // Preserve engine object/custom-struct identity and map permutation,
        // then check exact native strings and signed zero in the bounded walk.
        if (!Property->Identical(Left, Right))
            return EP4ZeroIdentity::Different;
        const auto Result = P4ZeroIdentityWalk(Property, Left, Right, true, 0, Budget);
        if (Result != EP4ZeroIdentity::Same)
            return Result;
    }
    return EP4ZeroIdentity::Same;
}
bool ApplyPropertyText(UObject* Object, FName Name, const FString& Text, bool Editable)
{
    FResolvedEditProperty Resolved;
    if (!ResolveProperty(Object, Name, Editable, Resolved) || Text.Len() > 1024 * 1024 || HasEmbeddedNul(Text) ||
        !ValidatePropertyText(Resolved.Property, Text, Resolved.Single, Editable, Object->IsTemplate()))
        return false;
    TStrongObjectPtr<UObject> Hold(Object);
    TStrongObjectPtr<UClass> HoldClass(Object->GetClass());
    FProperty* Property = Resolved.Property;
    FStagedPropertyValue Staged(Property);
    void* Parsed = Staged.Value;
    if (Resolved.Single)
        Property->CopySingleValue(Parsed, Resolved.Value);
    else
        Property->CopyCompleteValue(Parsed, Resolved.Value);
    FPropertyImportErrors Errors;
    FString Readback;
    const bool Valid = ImportStrictProperty(Property, Text, Parsed, Object, Resolved.Single, Errors) &&
        !Errors.HadMessage && JsonValue(Property, Parsed, Readback, Resolved.Single);
    if (!Valid)
        return false;
    FResolvedEditProperty Current;
    if (Object->GetClass() != HoldClass.Get() || !ResolveProperty(Object, Name, Editable, Current) ||
        Current.Property != Property || Current.Single != Resolved.Single)
        return false;
    const auto Identity = P4ZeroIdentityCompare(Property, Current.Value, Parsed, Current.Single);
    if (Identity == EP4ZeroIdentity::Unavailable)
        return false;
    if (Identity == EP4ZeroIdentity::Different)
    {
        TUniquePtr<FScopedTransaction> Transaction;
        TUniquePtr<FStagedPropertyValue> PreviousRoot;
        FProperty* Root = Current.Chain.GetHead()->GetValue();
        UPackage* Package = Object->GetOutermost();
        const bool WasDirty = Package->IsDirty();
        if (Editable)
        {
            PreviousRoot = MakeUnique<FStagedPropertyValue>(Root);
            Root->CopyCompleteValue(PreviousRoot->Value, Root->ContainerPtrToValuePtr<void>(Object));
            Transaction = MakeUnique<FScopedTransaction>(FText::FromString(TEXT("Edit property")));
        }
        const auto RestoreEditedRoot = [&]()
        {
            // Restoration covers this property's root, not arbitrary effects
            // callbacks have on unrelated objects/fields or destroyed types.
            if (PreviousRoot && IsValid(Object) && Object->GetClass() == HoldClass.Get() &&
                Object->GetClass()->FindPropertyByName(Root->GetFName()) == Root)
            {
                Root->CopyCompleteValue(Root->ContainerPtrToValuePtr<void>(Object), PreviousRoot->Value);
                Package->SetDirtyFlag(WasDirty);
            }
            if (Transaction)
                Transaction->Cancel();
        };
        Object->Modify();
        // Import transport preserves existing initialization behavior.
        // Editor callbacks may resize containers: never retain a leaf address
        // across PreEditChange, even when its native property is unchanged.
        if (Editable)
            Object->PreEditChange(Current.Chain);
        FResolvedEditProperty Destination;
        if (Object->GetClass() != HoldClass.Get() || !ResolveProperty(Object, Name, Editable, Destination) ||
            Destination.Property != Property || Destination.Single != Current.Single)
        {
            RestoreEditedRoot();
            return false;
        }
        if (Destination.Single)
            Property->CopySingleValue(Destination.Value, Parsed);
        else
            Property->CopyCompleteValue(Destination.Value, Parsed);
        FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
        Event.MemberProperty = Destination.Chain.GetHead()->GetValue();
        TArray<TMap<FString, int32>> ArrayIndices;
        ArrayIndices.Add(Destination.ArrayIndices);
        Event.SetArrayIndexPerObject(ArrayIndices);
        FPropertyChangedChainEvent ChainEvent(Destination.Chain, Event);
        Object->PostEditChangeChainProperty(ChainEvent);
        if (Editable)
        {
            // Native post-edit callbacks may normalize values or remove an
            // invalid array slot. Success must describe the final exact edit.
            FResolvedEditProperty Final;
            bool Exact = IsValid(Object) && Object->GetClass() == HoldClass.Get() &&
                ResolveProperty(Object, Name, true, Final) && Final.Property == Property &&
                Final.Single == Destination.Single;
            if (Exact)
                Exact = P4ZeroIdentityCompare(Property, Final.Value, Parsed, Final.Single) == EP4ZeroIdentity::Same;
            if (!Exact)
            {
                RestoreEditedRoot();
                return false;
            }
        }
        Object->MarkPackageDirty();
    }
    return true;
}
} // namespace P4PropertyValuesDetail

using namespace P4PropertyValuesDetail;

bool UP4AuthoringTools::SetPropertyText(UObject* Object, FName Name, const FString& Text)
{
    return ApplyPropertyText(Object, Name, Text, false);
}

bool UP4AuthoringTools::SetEditablePropertyText(UObject* Object, FName Name, const FString& Text)
{
    return ApplyPropertyText(Object, Name, Text, true);
}

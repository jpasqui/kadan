// Exact JSON: text admitted before Unreal's parser sees it, and integers read without rounding.

#include "P4AuthoringRowsInternal.h"
#include "P4AuthoringRows.h"
#include "P4Common.h"
#include "P4GamePaths.h"
#include "P4GameNames.h"
#include "P4OriginalImportSave.h"
#include "DataTableUtils.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "UObject/TextProperty.h"
#include "P4PlaceActors.h"
#include "P4MapKitCore/quality.h"
#include "ScopedTransaction.h"
#include "P4TextValues.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "UObject/SoftObjectPath.h"
#include "Misc/PackageName.h"
#include "P4NativeHash.h"
#include "Engine/DataAsset.h"
#include "UObject/StrongObjectPtr.h"
#include "P4ImportReferences.h"
#include "UObject/MetaData.h"
#include "Misc/Paths.h"
#include "P4OriginalDecode.h"
#include "P4OriginalMeshStaging.h"
#include "P4StaticMeshBuild.h"
#include "P4TextureBuild.h"
#include "P4TextureContent.h"
#include "P4MaterialGraph.h"
#include "P4MaterialInstance.h"
#include "P4StaticMeshContent.h"
#include "P4LevelContent.h"
#include "P4LightingBake.h"
#include "P4KadanImport.h"
#include "P4NativeValueStaging.h"

namespace P4AuthoringRowsDetail
{
bool P4HasNul(const FString& Text)
{
    for (const TCHAR Character : Text)
        if (!Character)
            return true;
    return false;
}

FString P4JsonString(const FString& Text)
{
    FString Out;
    auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
    Writer->WriteArrayStart();
    Writer->WriteValue(Text);
    Writer->WriteArrayEnd();
    Writer->Close();
    return Out.Mid(1, Out.Len() - 2);
}

bool P4ExactJson(const TSharedPtr<FJsonValue>& Value, FString& Out, int32 Depth)
{
    if (!Value.IsValid() || Depth > 64)
        return false;
    switch (Value->Type)
    {
        case EJson::Number:
            // Only DOM values from StoreNumbersAsStrings are accepted here.
            // Tokens have already passed the JSON lexer; never format doubles.
            return Value->TryGetString(Out) && !Out.IsEmpty();
        case EJson::String:
            Out = P4JsonString(Value->AsString());
            return true;
        case EJson::Boolean:
            Out = Value->AsBool() ? TEXT("true") : TEXT("false");
            return true;
        case EJson::Null:
            Out = TEXT("null");
            return true;
        case EJson::Array:
        {
            TArray<FString> Parts;
            for (const auto& Child : Value->AsArray())
            {
                FString Part;
                if (!P4ExactJson(Child, Part, Depth + 1))
                    return false;
                Parts.Add(MoveTemp(Part));
            }
            Out = TEXT("[") + FString::Join(Parts, TEXT(",")) + TEXT("]");
            return true;
        }
        case EJson::Object:
        {
            TArray<FString> Parts;
            for (const auto& Field : Value->AsObject()->Values)
            {
                FString Part;
                if (!P4ExactJson(Field.Value, Part, Depth + 1))
                    return false;
                Parts.Add(P4JsonString(Field.Key) + TEXT(":") + Part);
            }
            Parts.Sort([](const FString& A, const FString& B) { return A.Compare(B, ESearchCase::CaseSensitive) < 0; });
            Out = TEXT("{") + FString::Join(Parts, TEXT(",")) + TEXT("}");
            return true;
        }
        default:
            return false;
    }
}

bool P4AdmissionFail(FString& Error, const FString& Label, const TCHAR* Reason)
{
    Error = Label + TEXT(": ") + Reason;
    return false;
}

// Token admission precedes the recursive DOM parser. Duplicate JSON keys
// must not disappear into FJsonObject's map before schema validation.
bool P4TableJsonTokens(const FString& Json, FString& Error)
{
    if (P4HasNul(Json) || int64(Json.Len()) * sizeof(TCHAR) > 64 * 1024 * 1024)
        return P4AdmissionFail(Error, TEXT("Rows"), TEXT("staging text budget exceeded"));
    // UE4 FString += TCHAR(0) discards an escaped NUL during tokenization.
    // Refuse it in the original text before that information can disappear.
    bool Quoted = false, Escaped = false, PendingValue = false;
    for (int32 Index = 0; Index < Json.Len(); ++Index)
    {
        const TCHAR Character = Json[Index];
        // UE4's reader accepts a closing delimiter immediately after a
        // member's colon and silently omits that member from the DOM.
        if (!Quoted && PendingValue && !FChar::IsWhitespace(Character))
        {
            if (Character == TEXT('}') || Character == TEXT(']') || Character == TEXT(','))
                return P4AdmissionFail(Error, TEXT("Rows"), TEXT("missing JSON field value"));
            PendingValue = false;
        }
        if (!Quoted && Character == TEXT(':'))
            PendingValue = true;
        if (Escaped)
        {
            if (Character == TEXT('u') && Json.Mid(Index + 1, 4) == TEXT("0000"))
                return P4AdmissionFail(
                    Error, TEXT("Rows"), TEXT("escaped NUL cannot survive the engine string adapter"));
            Escaped = false;
            continue;
        }
        if (Quoted && Character == TEXT('\\'))
            Escaped = true;
        else if (Character == TEXT('"'))
            Quoted = !Quoted;
    }
    struct Frame
    {
        bool Object;
        TSet<FString> Keys;
    };
    TArray<Frame> Stack;
    auto Reader = TJsonReaderFactory<>::Create(Json);
    EJsonNotation Notation;
    while (Reader->ReadNext(Notation))
    {
        if (Notation == EJsonNotation::Error)
            return P4AdmissionFail(Error, TEXT("Rows"), TEXT("invalid JSON"));
        const bool End = Notation == EJsonNotation::ObjectEnd || Notation == EJsonNotation::ArrayEnd;
        if (!End && Stack.Num() && Stack.Last().Object)
        {
            const FString Key = Reader->GetIdentifier();
            if (P4HasNul(Key) || Stack.Last().Keys.Contains(Key))
                return P4AdmissionFail(Error, Key, TEXT("duplicate or invalid JSON field"));
            Stack.Last().Keys.Add(Key);
        }
        if (Notation == EJsonNotation::ObjectStart || Notation == EJsonNotation::ArrayStart)
        {
            if (Stack.Num() >= 64)
                return P4AdmissionFail(Error, TEXT("Rows"), TEXT("JSON nesting budget exceeded"));
            Frame Next;
            Next.Object = Notation == EJsonNotation::ObjectStart;
            Stack.Add(MoveTemp(Next));
        }
        else if (End)
        {
            if (!Stack.Num())
                return P4AdmissionFail(Error, TEXT("Rows"), TEXT("unmatched JSON container"));
            Stack.Pop();
        }
    }
    return Stack.Num() == 0 && Reader->GetErrorMessage().IsEmpty();
}

bool ExactIntegerJson(FProperty* Property, TSharedPtr<FJsonValue>& Value, int32 Depth, bool Single)
{
    if (!Property || !Value.IsValid() || Depth > 16)
        return false;
    if (!Single && Property->ArrayDim > 1)
    {
        if (Value->Type != EJson::Array || Value->AsArray().Num() != Property->ArrayDim)
            return false;
        TArray<TSharedPtr<FJsonValue>> Slots = Value->AsArray();
        for (auto& Slot : Slots)
            if (!ExactIntegerJson(Property, Slot, Depth + 1, true))
                return false;
        Value = MakeShared<FJsonValueArray>(Slots);
        return true;
    }
    if (auto Object = CastField<FObjectPropertyBase>(Property))
    {
        FString Path;
        if (Value->Type != EJson::String || !Value->TryGetString(Path))
            return false;
        if (!CastField<FSoftObjectProperty>(Property))
        {
            Value = MakeShared<FJsonValueString>(Path.IsEmpty()
                    ? TEXT("None")
                    : Object->PropertyClass->GetName() + TEXT("'") + P4ImportTextPath(Path) + TEXT("'"));
        }
        return true;
    }
    if (CastField<FInt64Property>(Property) || CastField<FUInt64Property>(Property))
    {
        if (Value->Type != EJson::Number)
            return false;
        FString Token;
        if (!Value->TryGetString(Token) || Token.IsEmpty())
            return false;
        const bool Unsigned = CastField<FUInt64Property>(Property) != nullptr;
        std::uint64_t IntegerBits = 0;
        if (!P4MapKit::ParseInteger(TCHAR_TO_UTF8(*Token), Unsigned, IntegerBits))
            return false;
        // UE4's table JSON importer reads integers through int64 then calls
        // SetIntPropertyValue(uint64). Supply an exact signed bit pattern
        // for uint64, including its upper half, instead of a JSON double.
        // Spell the signed representation without an out-of-range signed cast.
        const int64 Bits = IntegerBits <= uint64(MAX_int64) ? int64(IntegerBits) : -1 - int64(MAX_uint64 - IntegerBits);
        Value = MakeShared<FJsonValueString>(FString::Printf(TEXT("%lld"), Bits));
        return true;
    }
    if (auto Enum = CastField<FEnumProperty>(Property))
    {
        FString Name;
        return Value->TryGetString(Name) && Value->Type == EJson::String && !P4HasNul(Name) &&
            Enum->GetEnum()->GetValueByNameString(Name) != INDEX_NONE;
    }
    if (auto Number = CastField<FNumericProperty>(Property))
    {
        if (Number->GetIntPropertyEnum())
        {
            FString Name;
            return Value->TryGetString(Name) && Value->Type == EJson::String && !P4HasNul(Name) &&
                Number->GetIntPropertyEnum()->GetValueByNameString(Name) != INDEX_NONE;
        }
        if (Value->Type != EJson::Number)
            return false;
        if (Number->IsFloatingPoint())
        {
            double Parsed = 0;
            return Value->TryGetNumber(Parsed) && FMath::IsFinite(Parsed) &&
                (!CastField<FFloatProperty>(Property) || FMath::Abs(Parsed) <= double(FLT_MAX));
        }
        FString Token;
        std::uint64_t Bits = 0;
        const bool Unsigned = CastField<FByteProperty>(Property) || CastField<FUInt16Property>(Property) ||
            CastField<FUInt32Property>(Property);
        if (!Value->TryGetString(Token) || !P4MapKit::ParseInteger(TCHAR_TO_UTF8(*Token), Unsigned, Bits))
            return false;
        const int32 Width = Property->ElementSize * 8;
        if (Width < 1 || Width > 32)
            return false;
        if (Unsigned)
            return Bits <= ((std::uint64_t(1) << Width) - 1);
        const int64 Signed = Bits <= uint64(MAX_int64) ? int64(Bits) : -1 - int64(MAX_uint64 - Bits);
        return Signed >= -(int64(1) << (Width - 1)) && Signed < (int64(1) << (Width - 1));
    }
    if (CastField<FBoolProperty>(Property))
        return Value->Type == EJson::Boolean;
    if (CastField<FTextProperty>(Property))
    {
        FString Text;
        if (!P4TextJsonToString(Value, Text))
            return false;
        Value = MakeShared<FJsonValueString>(Text);
        return true;
    }
    if (CastField<FNameProperty>(Property) || CastField<FStrProperty>(Property))
        return Value->Type == EJson::String;
    if (auto Struct = CastField<FStructProperty>(Property))
    {
        if (Value->Type != EJson::Object)
            return false;
        for (auto& Field : Value->AsObject()->Values)
            if (!ExactIntegerJson(Struct->Struct->FindPropertyByName(FName(*Field.Key)), Field.Value, Depth + 1))
                return false;
    }
    else if (auto Array = CastField<FArrayProperty>(Property))
    {
        if (Value->Type != EJson::Array)
            return false;
        TArray<TSharedPtr<FJsonValue>> Children = Value->AsArray();
        for (auto& Child : Children)
            if (!ExactIntegerJson(Array->Inner, Child, Depth + 1))
                return false;
        Value = MakeShared<FJsonValueArray>(Children);
    }
    else if (auto Map = CastField<FMapProperty>(Property))
    {
        // Pair arrays retain non-string key types and exact numeric tokens
        // in our transport. UE4's table parser expects a JSON object here.
        if (Value->Type != EJson::Array)
            return false;
        auto Pairs = MakeShared<FJsonObject>();
        TSet<FString> Identities;
        for (const auto& PairValue : Value->AsArray())
        {
            if (!PairValue.IsValid() || PairValue->Type != EJson::Array || PairValue->AsArray().Num() != 2)
                return false;
            auto Key = PairValue->AsArray()[0];
            auto Child = PairValue->AsArray()[1];
            if (!Key.IsValid() || !Child.IsValid())
                return false;
            // Validate keys without replacing a uint64 token by a signed
            // representation: map keys are parsed directly as property text.
            auto CheckedKey = Key;
            if (!ExactIntegerJson(Map->KeyProp, CheckedKey, Depth + 1))
                return false;
            FString KeyText;
            if (Key->Type == EJson::Boolean)
                KeyText = Key->AsBool() ? TEXT("True") : TEXT("False");
            else if (!Key->TryGetString(KeyText))
                return false;
            if (CastField<FObjectPropertyBase>(Map->KeyProp) && !CastField<FSoftObjectProperty>(Map->KeyProp))
            {
                if (!CheckedKey->TryGetString(KeyText))
                    return false;
            }
            // FString keys are case-sensitive; FName/enum/object keys use
            // native identity. The typed readback gate also checks all keys.
            FString Identity = CastField<FStrProperty>(Map->KeyProp) ? KeyText : KeyText.ToLower();
            if (Identities.Contains(Identity) || !ExactIntegerJson(Map->ValueProp, Child, Depth + 1))
                return false;
            Identities.Add(Identity);
            Pairs->SetField(KeyText, Child);
        }
        Value = MakeShared<FJsonValueObject>(Pairs);
    }
    else
        return false;
    return true;
}

} // namespace P4AuthoringRowsDetail

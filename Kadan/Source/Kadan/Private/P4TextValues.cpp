#include "P4TextValues.h"
#include "P4MapKitCore/property_decoder.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/PropertyPortFlags.h"
#include "Internationalization/TextNamespaceUtil.h"
#include "Serialization/JsonSerializer.h"

namespace
{
bool P4TextInteger(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, int64& Out)
{
    const auto Field = Object->TryGetField(Name);
    FString Token;
    std::uint64_t Bits = 0;
    if (!Field.IsValid() || Field->Type != EJson::Number || !Field->TryGetString(Token) ||
        !P4MapKit::ParseInteger(TCHAR_TO_UTF8(*Token), false, Bits))
        return false;
    Out = Bits <= uint64(MAX_int64) ? int64(Bits) : -1 - int64(MAX_uint64 - Bits);
    return true;
}
bool P4TextString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, FString& Out)
{
    const auto Field = Object->TryGetField(Name);
    if (!Field.IsValid() || Field->Type != EJson::String || !Field->TryGetString(Out))
        return false;
    for (const TCHAR Character : Out)
        if (!Character)
            return false;
    return true;
}
FString P4TextLiteral(const FString& String)
{
    return TEXT("\"") + String.ReplaceCharWithEscapedChar() + TEXT("\"");
}
bool P4TextLiteralChecked(const TSharedPtr<FJsonObject>& Expected, const FString& Literal)
{
    FText Parsed;
    const TCHAR* End = FTextStringHelper::ReadFromBuffer(*Literal, Parsed);
    FString Json;
    TSharedPtr<FJsonObject> Readback;
    if (!End || *End || !P4TextRuntimeJson(Parsed, Json) ||
        !FJsonSerializer::Deserialize(
            TJsonReaderFactory<>::Create(Json), Readback, FJsonSerializer::EFlags::StoreNumbersAsStrings))
        return false;
    const auto Wrapped = Readback->TryGetField(TEXT("__native_text"));
    if (!Wrapped.IsValid() || Wrapped->Type != EJson::Object ||
        Wrapped->AsObject()->Values.Num() != Expected->Values.Num())
        return false;
    for (const auto& Pair : Expected->Values)
    {
        const auto Actual = Wrapped->AsObject()->TryGetField(Pair.Key);
        if (!Actual.IsValid() || Actual->Type != Pair.Value->Type)
            return false;
        if (Actual->Type == EJson::Boolean)
        {
            if (Actual->AsBool() != Pair.Value->AsBool())
                return false;
        }
        else
        {
            FString A, B;
            if (!Actual->TryGetString(A) || !Pair.Value->TryGetString(B) || A != B)
                return false;
        }
    }
    return true;
}
} //namespace
bool P4TextJsonToString(const TSharedPtr<FJsonValue>& Value, FString& Out)
{
    if (!Value.IsValid() || Value->Type != EJson::Object || Value->AsObject()->Values.Num() != 1)
        return false;
    const auto Wrapped = Value->AsObject()->TryGetField(TEXT("__native_text"));
    if (!Wrapped.IsValid() || Wrapped->Type != EJson::Object)
        return false;
    const auto Data = Wrapped->AsObject();
    int64 Flags = 0, History = 0;
    if (!P4TextInteger(Data, TEXT("flags"), Flags) || !P4TextInteger(Data, TEXT("history"), History))
        return false;
    if (History == 0)
    {
        FString Namespace, Key, Source;
        if (Flags != 0 || Data->Values.Num() != 5 || !P4TextString(Data, TEXT("namespace"), Namespace) ||
            !P4TextString(Data, TEXT("key"), Key) || !P4TextString(Data, TEXT("source"), Source) || Key.IsEmpty() ||
            Source.IsEmpty() || Namespace.TrimEnd() != Namespace ||
            (Namespace.EndsWith(TEXT("]")) && Namespace.Contains(TEXT("["))))
            return false;
        Out = TEXT("NSLOCTEXT(") + P4TextLiteral(Namespace) + TEXT(",") + P4TextLiteral(Key) + TEXT(",") +
            P4TextLiteral(Source) + TEXT(")");
        return P4TextLiteralChecked(Data, Out);
    }
    if (History != -1 || (Flags != 0 && Flags != ETextFlag::CultureInvariant))
        return false;
    const auto HasSource = Data->TryGetField(TEXT("has_source"));
    if (!HasSource.IsValid() || HasSource->Type != EJson::Boolean)
        return false;
    FString Source;
    if (HasSource->AsBool())
    {
        if (Flags != ETextFlag::CultureInvariant || Data->Values.Num() != 4 ||
            !P4TextString(Data, TEXT("source"), Source) || Source.IsEmpty())
            return false;
    }
    else if (Data->Values.Num() != 3)
        return false;
    Out = Flags == ETextFlag::CultureInvariant ? TEXT("INVTEXT(") + P4TextLiteral(Source) + TEXT(")") : FString();
    return P4TextLiteralChecked(Data, Out);
}
bool P4TextRuntimeJson(const FText& Value, FString& Out)
{
    TArray<uint8> Bytes;
    FMemoryWriter Writer(Bytes);
    Writer.SetPortFlags(PPF_DuplicateVerbatim);
    FText Copy = Value;
    Writer << Copy;
    if (Writer.IsError())
        return false;
    const std::vector<std::uint8_t> Body(Bytes.GetData(), Bytes.GetData() + Bytes.Num());
    P4MapKit::FPackageInventory Package;
    P4MapKit::FPropertyDecoder Decoder(Package, Body);
    P4MapKit::FPropertyEnvelope Tag;
    Tag.Type = "TextProperty";
    Tag.Size = Body.size();
    P4MapKit::FDecodedValue Decoded;
    if (!Decoder.Property(Tag, Decoded))
        return false;
    // UE's editor-only package suffix is stripped by the engine during cooking.
    // Keep namespace/key/source/flags/history exactly as they identify runtime text.
    for (auto& Field : Decoded.Fields[0].second.Fields)
        if (Field.first == "namespace")
        {
            const FString Namespace = TextNamespaceUtil::StripPackageNamespace(
                UTF8_TO_TCHAR(Field.second.Text.c_str()));
            Field.second.Text = TCHAR_TO_UTF8(*Namespace);
        }
    Out = UTF8_TO_TCHAR(P4MapKit::DecodedJson(Decoded).c_str());
    return true;
}

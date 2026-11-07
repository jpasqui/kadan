#include "P4OriginalClassValueNotes.h"
#include "P4MapKitCore/property_decoder.h"

namespace P4OriginalClassValueNotes
{
const TCHAR* Kind(P4MapKit::FDecodedValue::EKind Value)
{
    using K = P4MapKit::FDecodedValue::EKind;
    switch (Value)
    {
        case K::Object:
            return TEXT("object");
        case K::Array:
            return TEXT("array");
        case K::String:
            return TEXT("string");
        case K::Number:
            return TEXT("number");
        case K::Boolean:
            return TEXT("boolean");
        case K::Null:
            return TEXT("null");
        case K::Name:
            return TEXT("name");
        case K::Enum:
            return TEXT("enum");
        case K::HardReference:
            return TEXT("hard-reference");
        case K::SoftPath:
            return TEXT("soft-path");
        case K::FieldPath:
            return TEXT("field-path");
        case K::NativeText:
            return TEXT("native-text");
    }
    return TEXT("unknown");
}
bool Small(const P4MapKit::FDecodedValue& Value, int32 Depth, int32& Nodes, std::size_t& Bytes)
{
    if (Depth > 4 || ++Nodes > 32 || Value.Fields.size() > 16 || Value.Items.size() > 16)
        return false;
    Bytes += Value.Text.size();
    if (Bytes > 1024)
        return false;
    for (const auto& Field : Value.Fields)
    {
        Bytes += Field.first.size();
        if (Bytes > 1024 || !Small(Field.second, Depth + 1, Nodes, Bytes))
            return false;
    }
    for (const auto& Item : Value.Items)
        if (!Small(Item, Depth + 1, Nodes, Bytes))
            return false;
    return true;
}
} //namespace P4OriginalClassValueNotes

void P4AppendOriginalClassUnheldDetail(TArray<FP4OriginalClassValueNotHeld>* Details, const FString& Object,
    const FString& Property, const FString& Schema, const P4MapKit::FDecodedValue& Value, const FString& Reason)
{
    if (!Details || Details->Num() >= 256)
        return;
    FP4OriginalClassValueNotHeld Row;
    Row.Object = Object.Left(4096);
    Row.Property = Property.Left(4096);
    Row.Schema = Schema.Left(4096);
    Row.Reason = Reason.Left(256);
    Row.Kind = P4OriginalClassValueNotes::Kind(Value.Kind);
    int32 Nodes = 0;
    std::size_t Bytes = 0;
    if (P4OriginalClassValueNotes::Small(Value, 0, Nodes, Bytes))
        Row.SourceValue = FString(UTF8_TO_TCHAR(P4MapKit::DecodedJson(Value).c_str())).Left(512);
    else
        Row.SourceValue = FString::Printf(TEXT("bounded preview omitted: %s, %d fields, %d items"), *Row.Kind,
            int32(Value.Fields.size()), int32(Value.Items.size()));
    Details->Add(MoveTemp(Row));
}

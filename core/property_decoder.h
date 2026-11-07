// Exact original-property values. This does not qualify headers, bulk data,
// recovered UE schemas, editability or export; those have separate admission gates.
#pragma once
#include "package_reader.h"
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace P4MapKit
{
struct FDecodedValue
{
    enum class EKind
    {
        Object,
        Array,
        String,
        Number,
        Boolean,
        Null,
        Name,
        Enum,
        HardReference,
        SoftPath,
        FieldPath,
        NativeText
    };
    EKind Kind = EKind::Object;
    std::string Text;
    // Archive identity is distinct from its printable spelling. In particular,
    // a numbered FName and a literal name ending in _0 must not collapse.
    std::string Type, EnumType, NameBase, AssetPath, SubPath;
    std::int32_t ReferenceIndex = 0, NameIndex = -1, NameNumber = 0;
    bool EnumNumeric = false;
    std::vector<std::pair<std::string, FDecodedValue>> Fields;
    std::vector<FDecodedValue> Items;
    static FDecodedValue Atom(EKind Kind, std::string Text)
    {
        FDecodedValue Value;
        Value.Kind = Kind;
        Value.Text = std::move(Text);
        return Value;
    }
    const FDecodedValue* Field(const std::string& Name) const
    {
        for (const auto& Item : Fields)
            if (Item.first == Name)
                return &Item.second;
        return nullptr;
    }
};
inline std::string PropertyIdentity(std::string Name)
{
    for (auto& C : Name)
        if (C >= 'A' && C <= 'Z')
            C += 'a' - 'A';
    return Name;
}

// Strings are already validated UTF-8. Numeric tokens never pass through a
// JSON double, so UINT64_MAX and signed endpoints survive native interchange.
inline void WriteDecodedJson(const FDecodedValue& Value, std::ostream& Out)
{
    const auto Quote = [&](const std::string& Text)
    {
        static const char Hex[] = "0123456789abcdef";
        Out.put('"');
        for (const auto Character : Text)
        {
            const auto Byte = std::uint8_t(Character);
            if (Byte == '"' || Byte == '\\')
            {
                Out.put('\\');
                Out.put(Character);
            }
            else if (Byte < 32)
            {
                Out << "\\u00";
                Out.put(Hex[Byte >> 4]);
                Out.put(Hex[Byte & 15]);
            }
            else
                Out.put(Character);
        }
        Out.put('"');
    };
    // This is the established importer interchange projection. It deliberately
    // does not encode the typed identity; native typed callers retain the tree.
    if (Value.Kind == FDecodedValue::EKind::String || Value.Kind == FDecodedValue::EKind::Name ||
        (Value.Kind == FDecodedValue::EKind::Enum && !Value.EnumNumeric) ||
        Value.Kind == FDecodedValue::EKind::SoftPath ||
        (Value.Kind == FDecodedValue::EKind::HardReference && Value.ReferenceIndex))
        Quote(Value.Text);
    else if (Value.Kind == FDecodedValue::EKind::Enum)
        Out << Value.Text;
    else if (Value.Kind == FDecodedValue::EKind::HardReference)
        Out << "null";
    else if (Value.Kind == FDecodedValue::EKind::Null)
        Out << "null";
    else if (Value.Kind == FDecodedValue::EKind::Number || Value.Kind == FDecodedValue::EKind::Boolean)
        Out << Value.Text;
    else if (Value.Kind == FDecodedValue::EKind::Array)
    {
        Out.put('[');
        for (std::size_t Index = 0; Index < Value.Items.size(); ++Index)
        {
            if (Index)
                Out.put(',');
            WriteDecodedJson(Value.Items[Index], Out);
        }
        Out.put(']');
    }
    else
    {
        Out.put('{');
        for (std::size_t Index = 0; Index < Value.Fields.size(); ++Index)
        {
            if (Index)
                Out.put(',');
            Quote(Value.Fields[Index].first);
            Out.put(':');
            WriteDecodedJson(Value.Fields[Index].second, Out);
        }
        Out.put('}');
    }
}
inline std::string DecodedJson(const FDecodedValue& Value)
{
    std::ostringstream Out;
    Out.imbue(std::locale::classic());
    WriteDecodedJson(Value, Out);
    return Out.str();
}

// Explicitly erase typed identity when passing a decoded tree to an existing
// importer. Reading the legacy JSON cannot reconstruct these identities.
inline FDecodedValue LegacyDecodedValue(const FDecodedValue& Value)
{
    FDecodedValue Result;
    Result.Kind = Value.Kind;
    Result.Text = Value.Text;
    if (Value.Kind == FDecodedValue::EKind::Enum && Value.EnumNumeric)
        Result.Kind = FDecodedValue::EKind::Number;
    else if (Value.Kind == FDecodedValue::EKind::Name || Value.Kind == FDecodedValue::EKind::Enum ||
        Value.Kind == FDecodedValue::EKind::SoftPath)
        Result.Kind = FDecodedValue::EKind::String;
    else if (Value.Kind == FDecodedValue::EKind::HardReference)
        Result.Kind = Value.ReferenceIndex ? FDecodedValue::EKind::String : FDecodedValue::EKind::Null;
    else if (Value.Kind == FDecodedValue::EKind::NativeText || Value.Kind == FDecodedValue::EKind::FieldPath)
        Result.Kind = FDecodedValue::EKind::Object;
    for (const auto& Field : Value.Fields)
        Result.Fields.emplace_back(Field.first, LegacyDecodedValue(Field.second));
    for (const auto& Item : Value.Items)
        Result.Items.push_back(LegacyDecodedValue(Item));
    return Result;
}

// Reads back what WriteDecodedJson wrote, and nothing else: no spaces, the same escapes, a number as
// the text it was. A value kept as text can so be read again without passing through a double.
inline bool ReadDecodedJson(const std::string& Text, std::size_t& At, FDecodedValue& Out, int Depth = 0)
{
    const auto Quoted = [&](std::string& Into)
    {
        if (At >= Text.size() || Text[At] != '"')
            return false;
        for (++At; At < Text.size(); ++At)
        {
            const char Character = Text[At];
            if (Character == '"')
            {
                ++At;
                return true;
            }
            if (std::uint8_t(Character) < 32)
                return false;
            if (Character != '\\')
            {
                Into.push_back(Character);
                continue;
            }
            if (++At >= Text.size())
                return false;
            if (Text[At] == '"' || Text[At] == '\\')
            {
                Into.push_back(Text[At]);
                continue;
            }
            // A control character, as \u00XX.
            const auto Digit = [](char C)
            {
                return C >= '0' && C <= '9' ? C - '0' : C >= 'a' && C <= 'f' ? C - 'a' + 10 : -1;
            };
            if (Text[At] != 'u' || At + 4 >= Text.size() || Text[At + 1] != '0' || Text[At + 2] != '0')
                return false;
            const int High = Digit(Text[At + 3]), Low = Digit(Text[At + 4]);
            if (High < 0 || Low < 0 || High * 16 + Low >= 32)
                return false;
            Into.push_back(char(High * 16 + Low));
            At += 4;
        }
        return false;
    };
    if (Depth > 64 || At >= Text.size())
        return false;
    Out = FDecodedValue();
    const char First = Text[At];
    if (First == '"')
    {
        Out.Kind = FDecodedValue::EKind::String;
        return Quoted(Out.Text);
    }
    if (First == '[')
    {
        Out.Kind = FDecodedValue::EKind::Array;
        ++At;
        if (At < Text.size() && Text[At] == ']')
        {
            ++At;
            return true;
        }
        for (;;)
        {
            Out.Items.emplace_back();
            if (!ReadDecodedJson(Text, At, Out.Items.back(), Depth + 1) || At >= Text.size())
                return false;
            if (Text[At] == ']')
            {
                ++At;
                return true;
            }
            if (Text[At++] != ',')
                return false;
        }
    }
    if (First == '{')
    {
        Out.Kind = FDecodedValue::EKind::Object;
        ++At;
        if (At < Text.size() && Text[At] == '}')
        {
            ++At;
            return true;
        }
        for (;;)
        {
            Out.Fields.emplace_back();
            if (!Quoted(Out.Fields.back().first) || At >= Text.size() || Text[At++] != ':' ||
                !ReadDecodedJson(Text, At, Out.Fields.back().second, Depth + 1) || At >= Text.size())
                return false;
            if (Text[At] == '}')
            {
                ++At;
                return true;
            }
            if (Text[At++] != ',')
                return false;
        }
    }
    const auto Word = [&](const char* Is)
    {
        const std::size_t Size = std::char_traits<char>::length(Is);
        if (Text.compare(At, Size, Is) != 0)
            return false;
        At += Size;
        return true;
    };
    if (Word("null"))
    {
        Out.Kind = FDecodedValue::EKind::Null;
        return true;
    }
    if (Word("true"))
    {
        Out = FDecodedValue::Atom(FDecodedValue::EKind::Boolean, "true");
        return true;
    }
    if (Word("false"))
    {
        Out = FDecodedValue::Atom(FDecodedValue::EKind::Boolean, "false");
        return true;
    }
    // A number, or one of the words the writer has for a value that is none: kept as it stands.
    const std::size_t Start = At;
    while (At < Text.size() && Text[At] != ',' && Text[At] != ']' && Text[At] != '}')
        ++At;
    if (At == Start)
        return false;
    for (std::size_t Index = Start; Index < At; ++Index)
    {
        const char C = Text[Index];
        if (!((C >= '0' && C <= '9') || (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || C == '-' || C == '+' ||
                C == '.'))
            return false;
    }
    const std::string Token = Text.substr(Start, At - Start);
    const bool Numeral = (Token[0] >= '0' && Token[0] <= '9') || Token[0] == '-' || Token[0] == '+';
    if (!Numeral && Token != "nan" && Token != "inf")
        return false;
    Out = FDecodedValue::Atom(FDecodedValue::EKind::Number, Token);
    return true;
}
inline bool ReadDecodedJson(const std::string& Text, FDecodedValue& Out)
{
    std::size_t At = 0;
    return ReadDecodedJson(Text, At, Out) && At == Text.size();
}

// Map archives omit the struct name of keys/values. Supply it from a recovered
// owning UE schema, rather than guessing a native struct from its bytes.
struct FMapStructCodec
{
    std::string Owner, Property, Key, Value;
    bool KeyNative = false, ValueNative = false;
};
// A narrowly qualified original archive field may contain a nonfinite sentinel.
// Keep its IEEE word as text inside valid JSON; never round-trip a NaN through
// double or spell every distinct NaN as "nan". Ordinary fields remain finite.
struct FArchivedFloatCodec
{
    std::string Owner, Property;
    bool NaN = false, PositiveInfinity = false, NegativeInfinity = false;
};
class FPropertyDecoder
{
    const FPackageInventory& Package;
    const std::vector<std::uint8_t>& Body;
    std::size_t Nodes = 0, TextBytes = 0;
    const std::size_t NodeLimit, TextLimit;
    const std::vector<FMapStructCodec> MapCodecs;
    const std::vector<FArchivedFloatCodec> FloatCodecs;
    const FArchivedFloatCodec* ArchivedFloat = nullptr;
    bool TypedValues = false;
    struct FTypedScope
    {
        bool& Mode;
        const bool Previous;
        explicit FTypedScope(bool& Value) : Mode(Value), Previous(Value) { Mode = true; }
        ~FTypedScope() { Mode = Previous; }
    };
    bool Failure(std::size_t At, const char* Code)
    {
        if (Error.Code.empty())
            Error = {At, Code};
        return false;
    }
    bool Read(bool Good, const FByteReader& Reader)
    {
        return Good || Failure(Reader.Error.Offset, Reader.Error.Code.c_str());
    }
    bool Finish(const FByteReader& Reader, std::size_t End)
    {
        return Reader.Tell() == End || Failure(Reader.Tell(), "unread-property-payload");
    }
    bool Charge(std::size_t At, std::size_t Characters = 0)
    {
        if (Nodes >= NodeLimit || Characters > TextLimit - TextBytes)
            return Failure(At, "decoded-value-budget");
        ++Nodes;
        TextBytes += Characters;
        return true;
    }
    bool Metadata(std::size_t At, std::string& Into, const std::string& Text)
    {
        if (Into == Text)
            return true;
        if (Text.size() > TextLimit - TextBytes)
            return Failure(At, "decoded-value-budget");
        TextBytes += Text.size();
        Into = Text;
        return true;
    }
    bool String(FByteReader& Reader, bool IsName, FDecodedValue& Value)
    {
        std::string Text;
        const auto Start = Reader.Tell();
        if (!Read(IsName ? Reader.Name(Package.Names, Text) : Reader.String(Text), Reader) ||
            !Charge(Reader.Tell(), Text.size()))
            return false;
        Value = FDecodedValue::Atom(
            TypedValues && IsName ? FDecodedValue::EKind::Name : FDecodedValue::EKind::String, std::move(Text));
        if (TypedValues && IsName)
        {
            FByteReader Identity(Body, Start, Reader.Tell());
            if (!Read(Identity.I32(Value.NameIndex) && Identity.I32(Value.NameNumber), Identity))
                return false;
            Value.NameBase = Package.Names[std::size_t(Value.NameIndex)];
            if (Value.NameBase.size() > TextLimit - TextBytes)
                return Failure(Start, "decoded-value-budget");
            TextBytes += Value.NameBase.size();
        }
        return true;
    }
    bool Number(FByteReader& Reader, std::size_t Width, bool Signed, bool Floating, FDecodedValue& Value)
    {
        std::uint64_t Bits = 0;
        if (!Read(Reader.Unsigned(Width, Bits), Reader))
            return false;
        std::string Text;
        if (Floating)
        {
            static_assert(sizeof(float) == 4 && sizeof(double) == 8 && std::numeric_limits<float>::is_iec559 &&
                    std::numeric_limits<double>::is_iec559,
                "UE archive requires IEEE binary32/binary64");
            double Number = 0;
            if (Width == 4)
            {
                const auto Word = std::uint32_t(Bits);
                float Single = 0;
                std::memcpy(&Single, &Word, 4);
                Number = Single;
            }
            else
                std::memcpy(&Number, &Bits, 8);
            if (!std::isfinite(Number))
            {
                const auto Word = std::uint32_t(Bits);
                const bool IsNaN = (Word & 0x007fffffu) != 0;
                const bool Negative = (Word & 0x80000000u) != 0;
                if (Width != 4 || !ArchivedFloat ||
                    !(IsNaN ? ArchivedFloat->NaN
                            : Negative ? ArchivedFloat->NegativeInfinity
                                       : ArchivedFloat->PositiveInfinity))
                    return Failure(Reader.Tell(), "nonfinite-property");
                const auto Token = std::to_string(Word);
                if (!Charge(Reader.Tell(), Token.size()))
                    return false;
                Value = FDecodedValue();
                Value.Fields.emplace_back(
                    "__native_float32_bits", FDecodedValue::Atom(FDecodedValue::EKind::String, Token));
                return true;
            }
            std::ostringstream Out;
            Out.imbue(std::locale::classic());
            Out << std::setprecision(17) << Number;
            Text = Out.str();
            // Keep a float token even for integral and negative-zero values.
            if (Text.find_first_of(".eE") == std::string::npos)
                Text += ".0";
        }
        else if (Signed && (Bits & (std::uint64_t(1) << (Width * 8 - 1))))
        {
            const auto Mask = Width == 8 ? UINT64_MAX : (std::uint64_t(1) << (Width * 8)) - 1;
            Text = "-" + std::to_string((~Bits & Mask) + 1);
        }
        else
            Text = std::to_string(Bits);
        if (!Charge(Reader.Tell(), Text.size()))
            return false;
        Value = FDecodedValue::Atom(FDecodedValue::EKind::Number, std::move(Text));
        return true;
    }
    bool SoftPath(FByteReader& Reader, FDecodedValue& Value)
    {
        if (!String(Reader, true, Value))
            return false;
        std::string Sub;
        if (!Read(Reader.String(Sub), Reader))
            return false;
        if (!Sub.empty() && !TypedValues)
            return Failure(Reader.Tell(), "unsupported-soft-subobject");
        if (Value.Text == "None")
            Value.Text.clear();
        if (TypedValues)
        {
            if (Sub.size() > (TextLimit - TextBytes) / 2 ||
                Value.Text.size() + (Sub.empty() ? 0 : 1) > TextLimit - TextBytes - Sub.size() * 2)
                return Failure(Reader.Tell(), "decoded-value-budget");
            TextBytes += Sub.size() * 2 + Value.Text.size() + (Sub.empty() ? 0 : 1);
            Value.Kind = FDecodedValue::EKind::SoftPath;
            Value.AssetPath = Value.Text;
            Value.SubPath = std::move(Sub);
            if (!Value.SubPath.empty())
                Value.Text += ":" + Value.SubPath;
        }
        return true;
    }
    bool Text(FByteReader& Reader, FDecodedValue& Value)
    {
        // Current retoc UE4.26 text archive: flags, int8 history, then the
        // complete history payload. Never substitute the display string.
        FDecodedValue Flags, History, Data;
        if (!Charge(Reader.Tell()) || !Number(Reader, 4, false, false, Flags) ||
            !Number(Reader, 1, true, false, History))
            return false;
        const auto Kind = History.Text;
        Data.Fields.emplace_back("flags", std::move(Flags));
        Data.Fields.emplace_back("history", std::move(History));
        if (Kind == "0")
        {
            for (const auto* Field : {"namespace", "key", "source"})
            {
                FDecodedValue Child;
                if (!String(Reader, false, Child))
                    return false;
                Data.Fields.emplace_back(Field, std::move(Child));
            }
        }
        else if (Kind == "-1")
        {
            std::uint64_t HasSource = 0;
            const auto At = Reader.Tell();
            if (!Read(Reader.Unsigned(4, HasSource), Reader))
                return false;
            if (HasSource > 1)
                return Failure(At, "noncanonical-text-bool");
            if (!Charge(At))
                return false;
            Data.Fields.emplace_back(
                "has_source", FDecodedValue::Atom(FDecodedValue::EKind::Boolean, HasSource ? "true" : "false"));
            if (HasSource)
            {
                FDecodedValue Source;
                if (!String(Reader, false, Source))
                    return false;
                Data.Fields.emplace_back("source", std::move(Source));
            }
        }
        else
            return Failure(Reader.Tell() - 1, "unsupported-text-history");
        if (TypedValues)
            Value.Kind = FDecodedValue::EKind::NativeText;
        Value.Fields.emplace_back("__native_text", std::move(Data));
        return true;
    }
    bool FieldPath(FByteReader& Reader, FDecodedValue& Value)
    {
        // UE4.26 field paths: property-outward FNames followed by the owning
        // struct package index. This is also the grammar used by ue_deep and
        // the independently qualified Blueprint source-values reader.
        const auto At = Reader.Tell();
        std::size_t Count = 0;
        if (!Charge(At) || !Read(Reader.Count(Count, 64), Reader))
            return false;
        FDecodedValue Parts;
        Parts.Kind = FDecodedValue::EKind::Array;
        for (std::size_t Index = 0; Index < Count; ++Index)
        {
            FDecodedValue Part;
            if (!String(Reader, true, Part))
                return false;
            if (Part.Text == "None")
                return Failure(Reader.Tell(), "unsupported-legacy-field-path");
            Parts.Items.push_back(std::move(Part));
        }
        std::int32_t Owner = 0;
        std::string Path;
        if (!Read(Reader.I32(Owner), Reader))
            return false;
        if (!Package.Reference(Owner, Path) || (Count && !Owner))
            return Failure(At, "invalid-field-path-owner");
        if (!Charge(Reader.Tell(), Path.size()))
            return false;
        auto Reference = FDecodedValue::Atom(FDecodedValue::EKind::HardReference, std::move(Path));
        Reference.ReferenceIndex = Owner;
        if (!Metadata(At, Reference.Type, "ObjectProperty"))
            return false;
        Value.Kind = FDecodedValue::EKind::FieldPath;
        Value.ReferenceIndex = Owner;
        Value.Fields.emplace_back("field", std::move(Parts));
        Value.Fields.emplace_back("owner", std::move(Reference));
        return true;
    }
    bool Element(FByteReader& Reader, const std::string& Kind, FDecodedValue& Value, const std::string& StructType = "",
        std::size_t Depth = 0, bool NativeOnly = false, bool SchemaKnown = false)
    {
        const auto At = Reader.Tell();
        if (Kind == "Int8Property")
            return Number(Reader, 1, true, false, Value);
        if (Kind == "Int16Property")
            return Number(Reader, 2, true, false, Value);
        if (Kind == "IntProperty")
            return Number(Reader, 4, true, false, Value);
        if (Kind == "Int64Property")
            return Number(Reader, 8, true, false, Value);
        if (Kind == "UInt16Property")
            return Number(Reader, 2, false, false, Value);
        if (Kind == "UInt32Property")
            return Number(Reader, 4, false, false, Value);
        if (Kind == "UInt64Property")
            return Number(Reader, 8, false, false, Value);
        if (Kind == "ByteProperty")
            return Number(Reader, 1, false, false, Value);
        if (Kind == "FloatProperty")
            return Number(Reader, 4, true, true, Value);
        if (Kind == "DoubleProperty")
            return Number(Reader, 8, true, true, Value);
        if (Kind == "NameProperty" || Kind == "EnumProperty")
        {
            if (!String(Reader, true, Value))
                return false;
            if (TypedValues)
            {
                if (!Metadata(At, Value.Type, Kind))
                    return false;
                if (Kind == "EnumProperty")
                    Value.Kind = FDecodedValue::EKind::Enum;
            }
            return true;
        }
        if (Kind == "StrProperty")
            return String(Reader, false, Value);
        if (Kind == "TextProperty")
            return Text(Reader, Value);
        if (TypedValues && Kind == "FieldPathProperty")
            return FieldPath(Reader, Value);
        if (Kind == "SoftObjectProperty" || (TypedValues && Kind == "SoftClassProperty"))
        {
            if (!SoftPath(Reader, Value))
                return false;
            return !TypedValues || Metadata(At, Value.Type, Kind);
        }
        if (Kind == "StructProperty")
        {
            if (StructType.empty())
                return Failure(At, "missing-map-struct-codec");
            return StructElement(Reader, StructType, Value, Depth, NativeOnly, SchemaKnown);
        }
        if (Kind == "BoolProperty")
        {
            std::uint64_t Bit = 0;
            if (!Read(Reader.Unsigned(1, Bit), Reader))
                return false;
            if (Bit > 1)
                return Failure(At, "noncanonical-property-bool");
            if (!Charge(At))
                return false;
            Value = FDecodedValue::Atom(FDecodedValue::EKind::Boolean, Bit ? "true" : "false");
            return true;
        }
        if (Kind == "ObjectProperty" || Kind == "InterfaceProperty" ||
            (TypedValues && (Kind == "ClassProperty" || Kind == "WeakObjectProperty")))
        {
            std::int32_t Index = 0;
            std::string Path;
            if (!Read(Reader.I32(Index), Reader))
                return false;
            if (!Package.Reference(Index, Path))
                return Failure(At, "invalid-property-reference");
            if (!Charge(At, Path.size()))
                return false;
            Value = FDecodedValue::Atom(TypedValues ? FDecodedValue::EKind::HardReference
                    : Index                         ? FDecodedValue::EKind::String
                                                    : FDecodedValue::EKind::Null,
                std::move(Path));
            if (TypedValues)
            {
                Value.ReferenceIndex = Index;
                if (!Metadata(At, Value.Type, Kind))
                    return false;
            }
            return true;
        }
        // A lazy reference is the id the object it names carries in its own package (UObject's
        // PossiblySerializeObjectGuid), said as 32 hexadecimal digits, the four words in order.
        if (Kind == "LazyObjectProperty")
        {
            std::uint32_t Words[4] = {};
            for (auto& Word : Words)
                if (!Read(Reader.U32(Word), Reader))
                    return false;
            std::ostringstream Digits;
            Digits.imbue(std::locale::classic());
            for (const auto Word : Words)
                Digits << std::hex << std::setw(8) << std::setfill('0') << Word;
            if (!Charge(At, 32))
                return false;
            const bool Some = (Words[0] | Words[1] | Words[2] | Words[3]) != 0;
            Value = FDecodedValue::Atom(
                Some ? FDecodedValue::EKind::String : FDecodedValue::EKind::Null, Some ? Digits.str() : std::string());
            return true;
        }
        // A delegate is the object it calls and the name of the function. A multicast delegate is a
        // list of them.
        const bool Many = Kind == "MulticastDelegateProperty" || Kind == "MulticastInlineDelegateProperty" ||
            Kind == "MulticastSparseDelegateProperty";
        if (Many || Kind == "DelegateProperty")
        {
            std::size_t Count = 1;
            if (Many && !Read(Reader.Count(Count, 65536), Reader))
                return false;
            FDecodedValue List;
            List.Kind = FDecodedValue::EKind::Array;
            for (std::size_t Index = 0; Index < Count; ++Index)
            {
                std::int32_t Object = 0;
                std::string Path;
                FDecodedValue Function, Binding;
                const auto Start = Reader.Tell();
                if (!Read(Reader.I32(Object), Reader))
                    return false;
                if (!Package.Reference(Object, Path))
                    return Failure(Start, "invalid-property-reference");
                if (!Charge(Start, Path.size()) || !String(Reader, true, Function))
                    return false;
                auto Target = FDecodedValue::Atom(TypedValues ? FDecodedValue::EKind::HardReference
                        : Object                              ? FDecodedValue::EKind::String
                                                              : FDecodedValue::EKind::Null,
                    std::move(Path));
                if (TypedValues)
                {
                    Target.ReferenceIndex = Object;
                    if (!Metadata(Start, Target.Type, "ObjectProperty"))
                        return false;
                }
                Binding.Fields.emplace_back("object", std::move(Target));
                Binding.Fields.emplace_back("function", std::move(Function));
                List.Items.push_back(std::move(Binding));
            }
            if (Many)
                Value = std::move(List);
            else
            {
                FDecodedValue One = std::move(List.Items[0]);
                Value = std::move(One);
            }
            return true;
        }
        // Native object payloads need their own codecs. Digest markers and empty defaults cannot
        // stand in for values.
        return Failure(At, "unsupported-property-value");
    }
    struct FNativeLayout
    {
        std::vector<std::string> Members;
        std::size_t Width = 4;
        bool Signed = true, Floating = true;
    };
    static FNativeLayout Layout(const std::string& Type)
    {
        if (Type == "Vector")
            return {{"X", "Y", "Z"}, 4, true, true};
        if (Type == "Rotator")
            return {{"Pitch", "Yaw", "Roll"}, 4, true, true};
        if (Type == "Vector2D")
            return {{"X", "Y"}, 4, true, true};
        if (Type == "Vector4" || Type == "Quat")
            return {{"X", "Y", "Z", "W"}, 4, true, true};
        if (Type == "LinearColor")
            return {{"R", "G", "B", "A"}, 4, true, true};
        if (Type == "Color")
            return {{"B", "G", "R", "A"}, 1, false, false};
        if (Type == "IntPoint")
            return {{"X", "Y"}, 4, true, false};
        if (Type == "IntVector")
            return {{"X", "Y", "Z"}, 4, true, false};
        if (Type == "Plane")
            return {{"X", "Y", "Z", "W"}, 4, true, true};
        // An id is four words. The engine's own description of it has them signed.
        if (Type == "Guid")
            return {{"A", "B", "C", "D"}, 4, true, false};
        // Which navigation agents something is for: one bit each, in one word.
        if (Type == "NavAgentSelector")
            return {{"PackedBits"}, 4, false, false};
        return {};
    }
    // How the engine writes a struct: as one name or string, natively member by member, or as tagged properties.
    enum class EStructForm
    {
        Tagged,
        Text,
        Members
    };
    static EStructForm StructForm(const std::string& Type)
    {
        if (Type == "SmartName" || Type == "SoftObjectPath" || Type == "SoftClassPath" || Type == "GameplayTag")
            return EStructForm::Text;
        static const std::unordered_set<std::string> Members{"GameplayTagContainer", "RichCurveKey", "SimpleCurveKey",
            "PerPlatformFloat", "PerPlatformInt", "PerPlatformBool", "Box", "SkeletalMeshSamplingLODBuiltData", "FontData"};
        if (!Layout(Type).Members.empty() || Members.count(Type))
            return EStructForm::Members;
        return EStructForm::Tagged;
    }
    // Native structs whose form is not read yet. They cannot be guessed as tagged records.
    static bool NativePending(const std::string& Type)
    {
        static const std::unordered_set<std::string> Pending{"Box2D", "Matrix", "FrameNumber", "DateTime", "Timespan"};
        return Pending.count(Type) != 0;
    }
    // A struct the engine writes natively (its form is not Tagged), read from where the reader is.
    bool NativeStruct(FByteReader& Reader, const std::string& Type, FDecodedValue& Value)
    {
        const auto Start = Reader.Tell();
        const auto Native = Layout(Type);
        if (!Native.Members.empty())
        {
            for (const auto& Member : Native.Members)
            {
                FDecodedValue Child;
                if (!Number(Reader, Native.Width, Native.Signed, Native.Floating, Child))
                    return false;
                Value.Fields.emplace_back(Member, std::move(Child));
            }
            return true;
        }
        if (Type == "SmartName")
        {
            // Modern cooked FSmartName serializes DisplayName only. UID is an editor/runtime
            // lookup, not part of this payload; older UID/GUID-bearing extents still refuse.
            FDecodedValue Name;
            if (!String(Reader, true, Name))
                return false;
            Value.Fields.emplace_back("DisplayName", std::move(Name));
            return true;
        }
        if (Type == "SoftObjectPath" || Type == "SoftClassPath")
            return SoftPath(Reader, Value) && (!TypedValues || Metadata(Start, Value.Type, Type));
        if (Type == "GameplayTag")
        {
            FDecodedValue Tag;
            if (!String(Reader, true, Tag))
                return false;
            Value.Fields.emplace_back("tag", std::move(Tag));
            return true;
        }
        if (Type == "GameplayTagContainer")
        {
            std::size_t Count = 0;
            FDecodedValue Tags;
            Tags.Kind = FDecodedValue::EKind::Array;
            if (!Read(Reader.Count(Count), Reader))
                return false;
            for (std::size_t Index = 0; Index < Count; ++Index)
            {
                FDecodedValue Tag;
                if (!String(Reader, true, Tag))
                    return false;
                Tags.Items.push_back(std::move(Tag));
            }
            Value.Fields.emplace_back("tags", std::move(Tags));
            return true;
        }
        if (Type == "FontData")
        {
            // FFontData::Serialize when cooking: that it is cooked, its font face and its sub face. Only a font with
            // no font face writes more (its file name and how it loads), and the game has none of those.
            std::uint64_t Cooked = 0;
            std::int32_t Face = 0;
            std::string Path;
            if (!Read(Reader.Unsigned(4, Cooked), Reader) || !Read(Reader.I32(Face), Reader))
                return false;
            if (Cooked != 1 || !Face || !Package.Reference(Face, Path))
                return Failure(Reader.Tell(), "font-data-has-no-font-face");
            auto Asset = FDecodedValue::Atom(
                TypedValues ? FDecodedValue::EKind::HardReference : FDecodedValue::EKind::String, std::move(Path));
            if (TypedValues)
            {
                Asset.ReferenceIndex = Face;
                if (!Metadata(Reader.Tell(), Asset.Type, "ObjectProperty"))
                    return false;
            }
            Value.Fields.emplace_back("FontFaceAsset", std::move(Asset));
            FDecodedValue Sub;
            if (!Number(Reader, 4, true, false, Sub))
                return false;
            Value.Fields.emplace_back("SubFaceIndex", std::move(Sub));
            return true;
        }
        if (Type == "RichCurveKey" || Type == "SimpleCurveKey")
        {
            // A key of a curve: three bytes of modes and six numbers, or a time and a value.
            const bool Rich = Type == "RichCurveKey";
            if (Rich)
            {
                for (const char* Mode : {"InterpMode", "TangentMode", "TangentWeightMode"})
                {
                    FDecodedValue Child;
                    if (!Number(Reader, 1, false, false, Child))
                        return false;
                    Value.Fields.emplace_back(Mode, std::move(Child));
                }
            }
            for (const char* Part :
                {"Time", "Value", "ArriveTangent", "ArriveTangentWeight", "LeaveTangent", "LeaveTangentWeight"})
            {
                if (!Rich && std::string(Part) != "Time" && std::string(Part) != "Value")
                    break;
                FDecodedValue Child;
                if (!Number(Reader, 4, true, true, Child))
                    return false;
                Value.Fields.emplace_back(Part, std::move(Child));
            }
            return true;
        }
        if (Type == "PerPlatformFloat" || Type == "PerPlatformInt" || Type == "PerPlatformBool")
        {
            // A value that can differ by platform. A cook writes the one value of its platform: a word
            // that says so, then the value. Uncooked, a map of platform names follows, which is not read.
            FDecodedValue Cooked, Default;
            if (!Number(Reader, 4, false, false, Cooked))
                return false;
            if (Cooked.Text != "1")
                return Failure(Start, "per-platform-value-not-cooked");
            if (!Number(Reader, 4, Type != "PerPlatformBool", Type == "PerPlatformFloat", Default))
                return false;
            if (Type == "PerPlatformBool")
            {
                if (Default.Text != "0" && Default.Text != "1")
                    return Failure(Start, "invalid-bool-value");
                Default = FDecodedValue::Atom(FDecodedValue::EKind::Boolean, Default.Text == "1" ? "true" : "false");
            }
            Value.Fields.emplace_back("Default", std::move(Default));
            return true;
        }
        if (Type == "Box")
        {
            // Two corners of three floats and one byte that says the box holds something.
            for (const char* Corner : {"Min", "Max"})
            {
                FDecodedValue Point;
                Point.Kind = FDecodedValue::EKind::Object;
                for (const char* Axis : {"X", "Y", "Z"})
                {
                    FDecodedValue Child;
                    if (!Number(Reader, 4, true, true, Child))
                        return false;
                    Point.Fields.emplace_back(Axis, std::move(Child));
                }
                Value.Fields.emplace_back(Corner, std::move(Point));
            }
            FDecodedValue Valid;
            if (!Number(Reader, 1, false, false, Valid))
                return false;
            Value.Fields.emplace_back("IsValid", std::move(Valid));
            return true;
        }
        if (Type == "SkeletalMeshSamplingLODBuiltData")
        {
            // What a skinned mesh keeps for picking points on a level of detail by area, written natively:
            // the chances, the aliases and the total weight. One that holds nothing says nothing.
            FDecodedValue Sampler, Chances, Aliases, Total;
            std::size_t Count = 0;
            Chances.Kind = Aliases.Kind = FDecodedValue::EKind::Array;
            if (!Read(Reader.Count(Count, 16u * 1024u * 1024u), Reader))
                return false;
            for (std::size_t Index = 0; Index < Count; ++Index)
            {
                FDecodedValue Child;
                if (!Number(Reader, 4, true, true, Child))
                    return false;
                Chances.Items.push_back(std::move(Child));
            }
            if (!Read(Reader.Count(Count, 16u * 1024u * 1024u), Reader))
                return false;
            for (std::size_t Index = 0; Index < Count; ++Index)
            {
                FDecodedValue Child;
                if (!Number(Reader, 4, true, false, Child))
                    return false;
                Aliases.Items.push_back(std::move(Child));
            }
            if (!Number(Reader, 4, true, true, Total))
                return false;
            if (Chances.Items.empty() && Aliases.Items.empty() && Total.Text == "0.0")
                return true;
            Sampler.Fields.emplace_back("Prob", std::move(Chances));
            Sampler.Fields.emplace_back("Alias", std::move(Aliases));
            Sampler.Fields.emplace_back("TotalWeight", std::move(Total));
            Value.Fields.emplace_back("AreaWeightedTriangleSampler", std::move(Sampler));
            return true;
        }
        return Failure(Start, "unsupported-native-struct");
    }
    // A struct inside a collection, with no size of its own: read natively, or as tagged properties.
    bool StructElement(FByteReader& Reader, const std::string& Type, FDecodedValue& Value, std::size_t Depth,
        bool NativeOnly = false, bool SchemaKnown = false)
    {
        if (Depth >= 24)
            return Failure(Reader.Tell(), "property-depth-limit");
        const auto Form = StructForm(Type);
        if (Form != EStructForm::Tagged && !(SchemaKnown && !NativeOnly && Type == "GameplayTag"))
        {
            // A struct of members is one value; a struct of one name or string is that value.
            if (Form == EStructForm::Members && !Charge(Reader.Tell()))
                return false;
            return NativeStruct(Reader, Type, Value);
        }
        // A map whose reflected schema says GameplayTag is tagged (its net serializer does not imply a native
        // archive layout) reads it as tagged properties, without probing bytes or falling back.
        if (Form == EStructForm::Tagged && (NativeOnly || NativePending(Type)))
            return Failure(Reader.Tell(), "unsupported-native-struct");
        auto Position = Reader.Tell();
        if (!Properties(Position, Reader.Limit(), Value, Depth, Type))
            return false;
        return Read(Reader.Skip(Position - Reader.Tell()), Reader);
    }
    // A struct property's value, which has its size: read natively, or as tagged properties that fill it.
    bool Structure(const std::string& Type, std::size_t Start, std::size_t End, FDecodedValue& Value, std::size_t Depth)
    {
        if (Depth >= 24)
            return Failure(Start, "property-depth-limit");
        if (!Charge(Start))
            return false;
        FByteReader Reader(Body, Start, End);
        if (StructForm(Type) != EStructForm::Tagged)
            return NativeStruct(Reader, Type, Value) && Finish(Reader, End);
        // A material's inputs are written natively, in a form that is the material editor's and says
        // nothing without the material's graph. They are not read: the value says how many bytes
        // there were, so that a reader of the asset can tell what it does not hold.
        static const std::unordered_set<std::string> NativeNotRead{"ColorMaterialInput", "ScalarMaterialInput",
            "VectorMaterialInput", "Vector2MaterialInput", "MaterialAttributesInput", "ExpressionInput",
            "ShadingModelMaterialInput"};
        if (NativeNotRead.count(Type))
        {
            Value = FDecodedValue();
            Value.Fields.emplace_back("__native_bytes_not_read",
                FDecodedValue::Atom(FDecodedValue::EKind::Number, std::to_string(End - Start)));
            return true;
        }
        if (NativePending(Type))
            return Failure(Start, "unsupported-native-struct");
        std::size_t Position = Start;
        return Properties(Position, End, Value, Depth, Type) &&
            (Position == End || Failure(Position, "unread-struct-payload"));
    }
    bool Collection(const FPropertyEnvelope& Tag, FDecodedValue& Value, std::size_t Depth, const std::string& Owner)
    {
        FByteReader Reader(Body, Tag.PayloadOffset, Tag.PayloadOffset + Tag.Size);
        std::size_t Removed = 0, Count = 0;
        if (Tag.Type == "SetProperty" || Tag.Type == "MapProperty")
        {
            if (!Read(Reader.Count(Removed), Reader))
                return false;
            if (Removed)
                return Failure(Reader.Tell(), "unsupported-collection-removals");
        }
        if (!Read(Reader.Count(Count, 1000000), Reader) || !Charge(Reader.Tell()))
            return false;
        const auto End = Tag.PayloadOffset + Tag.Size;
        Value.Kind = FDecodedValue::EKind::Array;
        if (Tag.Type != "MapProperty" && Tag.Detail[0] == "StructProperty")
        {
            FPropertyEnvelope Inner;
            if (!Read(ReadPropertyEnvelope(Reader, Package.Names, Body.size(), Inner), Reader))
                return false;
            if (Inner.Type != "StructProperty" || Inner.ArrayIndex || Inner.HasGuid)
                return Failure(Inner.TagOffset, "invalid-struct-array-descriptor");
            if (!Finish(Reader, End))
                return false;
            FByteReader Elements(Body, Inner.PayloadOffset, Inner.PayloadOffset + Inner.Size);
            for (std::size_t Index = 0; Index < Count; ++Index)
            {
                FDecodedValue Child;
                if (!StructElement(Elements, Inner.Detail[0], Child, Depth))
                    return false;
                Value.Items.push_back(std::move(Child));
            }
            return Elements.Tell() == Elements.Limit() || Failure(Elements.Tell(), "struct-array-count-mismatch");
        }
        const FMapStructCodec* Codec = nullptr;
        for (const auto& Candidate : MapCodecs)
            if (PropertyIdentity(Candidate.Owner) == PropertyIdentity(Owner) &&
                PropertyIdentity(Candidate.Property) == PropertyIdentity(Tag.Name))
            {
                if (Codec)
                    return Failure(Tag.TagOffset, "ambiguous-map-struct-codec");
                Codec = &Candidate;
            }
        for (std::size_t Index = 0; Index < Count; ++Index)
        {
            FDecodedValue Item;
            if (Tag.Type == "MapProperty")
            {
                Item.Kind = FDecodedValue::EKind::Array;
                if (!Charge(Reader.Tell()))
                    return false;
                for (std::size_t Side = 0; Side < Tag.Detail.size(); ++Side)
                {
                    FDecodedValue Child;
                    const auto StructType = Codec ? (Side ? Codec->Value : Codec->Key) : std::string();
                    const bool NativeOnly = Codec && (Side ? Codec->ValueNative : Codec->KeyNative);
                    if (!Element(Reader, Tag.Detail[Side], Child, StructType, Depth, NativeOnly, Codec != nullptr))
                        return false;
                    Item.Items.push_back(std::move(Child));
                }
            }
            else if (!Element(Reader, Tag.Detail[0], Item))
                return false;
            Value.Items.push_back(std::move(Item));
        }
        if (!Finish(Reader, End))
            return false;
        if (Tag.Type == "MapProperty")
        {
            FDecodedValue Pairs;
            Pairs.Fields.emplace_back("__native_map_pairs", std::move(Value));
            Value = std::move(Pairs);
        }
        return true;
    }

public:
    FReadError Error;
    explicit FPropertyDecoder(const FPackageInventory& Inventory, const std::vector<std::uint8_t>& Bytes,
        std::size_t MaximumNodes = 1000000, std::size_t MaximumText = 64 * 1024 * 1024,
        const std::vector<FMapStructCodec>& Codecs = {}, const std::vector<FArchivedFloatCodec>& Floats = {})
        : Package(Inventory), Body(Bytes), NodeLimit(MaximumNodes), TextLimit(MaximumText), MapCodecs(Codecs),
          FloatCodecs(Floats)
    {
        Error = Inventory.Error;
    }
    // Typed entry points publish no partial output on refusal. The ordinary
    // Property/Properties/Document APIs retain their qualified legacy contract.
    bool TypedProperty(
        const FPropertyEnvelope& Tag, FDecodedValue& Value, std::size_t Depth = 0, const std::string& Owner = "")
    {
        FTypedScope Scope(TypedValues);
        FDecodedValue Result;
        if (!Property(Tag, Result, Depth, Owner))
            return false;
        Value = std::move(Result);
        return true;
    }
    bool TypedProperties(std::size_t& Position, std::size_t End, FDecodedValue& Value, std::size_t Depth = 0,
        const std::string& Owner = "")
    {
        FTypedScope Scope(TypedValues);
        FDecodedValue Result;
        auto At = Position;
        if (!Properties(At, End, Result, Depth, Owner))
            return false;
        Value = std::move(Result);
        Position = At;
        return true;
    }
    bool TypedDocument(FDecodedValue& Value)
    {
        FTypedScope Scope(TypedValues);
        FDecodedValue Result;
        if (!Document(Result))
            return false;
        Value = std::move(Result);
        return true;
    }
    bool TypedTableRows(std::size_t& Position, std::size_t End, const std::string& Owner,
        std::vector<std::pair<std::string, FDecodedValue>>& Rows)
    {
        FTypedScope Scope(TypedValues);
        std::vector<std::pair<std::string, FDecodedValue>> Result;
        auto At = Position;
        if (!TableRows(At, End, Owner, Result))
            return false;
        Rows = std::move(Result);
        Position = At;
        return true;
    }
    bool Property(
        const FPropertyEnvelope& Tag, FDecodedValue& Value, std::size_t Depth = 0, const std::string& Owner = "")
    {
        const auto PreviousFloat = ArchivedFloat;
        struct FRestoreFloat
        {
            const FArchivedFloatCodec*& At;
            const FArchivedFloatCodec* Before;
            ~FRestoreFloat() { At = Before; }
        } Restore{ArchivedFloat, PreviousFloat};
        ArchivedFloat = nullptr;
        for (const auto& Codec : FloatCodecs)
            if (PropertyIdentity(Codec.Owner) == PropertyIdentity(Owner) &&
                PropertyIdentity(Codec.Property) == PropertyIdentity(Tag.Name))
            {
                if (ArchivedFloat)
                    return Failure(Tag.TagOffset, "ambiguous-archived-float-codec");
                ArchivedFloat = &Codec;
            }
        if (!Error.Code.empty())
            return false;
        if (Depth >= 24)
            return Failure(Tag.TagOffset, "property-depth-limit");
        if (Tag.HasGuid)
            return Failure(Tag.TagOffset, "property-guid-requires-adapter");
        const std::size_t Details = Tag.Type == "MapProperty" ? 2
            : (Tag.Type == "StructProperty" || Tag.Type == "ArrayProperty" || Tag.Type == "SetProperty")
            ? 1
            : Tag.Detail.size();
        if (Tag.PayloadOffset > Body.size() || Tag.Size > Body.size() - Tag.PayloadOffset ||
            Tag.Detail.size() != Details || (Tag.Type == "BoolProperty" && Tag.Size != 0))
            return Failure(Tag.TagOffset, "invalid-value-envelope");
        FByteReader Reader(Body, Tag.PayloadOffset, Tag.PayloadOffset + Tag.Size);
        if (Tag.Type == "BoolProperty")
        {
            if (!Charge(Tag.TagOffset))
                return false;
            Value = FDecodedValue::Atom(FDecodedValue::EKind::Boolean, Tag.BoolValue ? "true" : "false");
        }
        else if (Tag.Type == "StructProperty")
        {
            return Structure(Tag.Detail[0], Tag.PayloadOffset, Tag.PayloadOffset + Tag.Size, Value, Depth);
        }
        else if (Tag.Type == "ArrayProperty" || Tag.Type == "SetProperty" || Tag.Type == "MapProperty")
        {
            return Collection(Tag, Value, Depth + 1, Owner);
        }
        else if (Tag.Type == "ByteProperty" && Tag.Size != 1)
        {
            if (!String(Reader, true, Value))
                return false;
            if (TypedValues)
                Value.Kind = FDecodedValue::EKind::Enum;
        }
        else if (!Element(Reader, Tag.Type, Value))
            return false;
        if (TypedValues)
        {
            if (!Metadata(Tag.TagOffset, Value.Type, Tag.Type))
                return false;
            if (Tag.Type == "ByteProperty" && Tag.Size == 1 && !Tag.Detail.empty() && Tag.Detail[0] != "None")
            {
                Value.Kind = FDecodedValue::EKind::Enum;
                Value.EnumNumeric = true;
            }
            if (Value.Kind == FDecodedValue::EKind::Enum && !Tag.Detail.empty() &&
                !Metadata(Tag.TagOffset, Value.EnumType, Tag.Detail[0]))
                return false;
        }
        return Finish(Reader, Tag.PayloadOffset + Tag.Size);
    }
    bool Properties(std::size_t& Position, std::size_t End, FDecodedValue& Value, std::size_t Depth = 0,
        const std::string& Owner = "")
    {
        if (!Error.Code.empty())
            return false;
        if (Depth >= 24)
            return Failure(Position, "property-depth-limit");
        if (!Charge(Position))
            return false;
        const auto Tags = ReadPropertyEnvelopes(Body, Package.Names, Position, End);
        if (!Tags.Error.Code.empty())
        {
            Error = Tags.Error;
            return false;
        }
        for (const auto& Tag : Tags.Properties)
        {
            std::string Key = Tag.Name + (Tag.ArrayIndex ? "[" + std::to_string(Tag.ArrayIndex) + "]" : "");
            if (!Charge(Tag.TagOffset, Key.size()))
                return false;
            FDecodedValue Child;
            if (!Property(Tag, Child, Depth + 1, Owner))
                return false;
            Value.Fields.emplace_back(std::move(Key), std::move(Child));
        }
        Position = Tags.End;
        return true;
    }
    bool Document(FDecodedValue& Value)
    {
        if (!Error.Code.empty())
            return false;
        if (Package.Exports.size() != 1)
            return Failure(0, "expected-one-document-export");
        const auto& Export = Package.Exports[0];
        std::string Class;
        if (!Package.Reference(Export.Class, Class))
            return Failure(0, "invalid-document-class");
        const bool Table = Class == "/Script/Engine.DataTable";
        if (!Table && Class != "/Script/Carrot4.GameSceneData" && Class != "/Script/Carrot4.ActorPlacedNumAsset" &&
            Class != "/Script/Carrot4.ActorPlacementInfoAsset" && Class != "/Script/Carrot4.OnyonCombineEftColorData")
            return Failure(Export.Offset, "unsupported-document-class");
        FDecodedValue Header;
        auto Position = Export.Offset;
        const auto End = Position + Export.Size;
        if (!Properties(Position, End, Header))
            return false;
        FByteReader Reader(Body, Position, End);
        std::int32_t Guid = 0;
        if (!Read(Reader.I32(Guid), Reader))
            return false;
        if (Guid != 0)
            return Failure(Position, "unsupported-object-guid");
        Value.Fields.emplace_back("format", FDecodedValue::Atom(FDecodedValue::EKind::Number, "1"));
        Value.Fields.emplace_back("package", FDecodedValue::Atom(FDecodedValue::EKind::String, Package.Package));
        if (!Table)
        {
            if (!Finish(Reader, End))
                return false;
            Value.Fields.emplace_back("cls", FDecodedValue::Atom(FDecodedValue::EKind::String, std::move(Class)));
            Value.Fields.emplace_back("values", std::move(Header));
            return true;
        }
        const auto* RowStruct = Header.Field("RowStruct");
        if (Header.Fields.size() != 1 || !RowStruct ||
            (RowStruct->Kind != FDecodedValue::EKind::String &&
                RowStruct->Kind != FDecodedValue::EKind::HardReference) ||
            RowStruct->Text.compare(0, 8, "/Script/") != 0)
            return Failure(Export.Offset, "unsupported-table-settings-or-row-type");
        Value.Fields.emplace_back("row_struct", *RowStruct);
        FDecodedValue Rows;
        Rows.Kind = FDecodedValue::EKind::Array;
        std::vector<std::pair<std::string, FDecodedValue>> Found;
        Position = Reader.Tell();
        const auto Dot = RowStruct->Text.rfind('.');
        if (!TableRows(Position, End, RowStruct->Text.substr(Dot == std::string::npos ? 0 : Dot + 1), Found))
            return false;
        if (Position != End)
            return Failure(Position, "unread-property-payload");
        for (auto& One : Found)
        {
            if (One.second.Field("Name"))
                return Failure(Position, "row-name-field-collision");
            One.second.Fields.emplace_back(
                "Name", FDecodedValue::Atom(FDecodedValue::EKind::String, std::move(One.first)));
            Rows.Items.push_back(std::move(One.second));
        }
        Value.Fields.emplace_back("rows", std::move(Rows));
        return true;
    }
    // The rows of a table, as they follow the table's own properties and the four bytes every object
    // ends its properties with: how many there are, then each row's name and every member of the row.
    // Owner is the short name of the row's type, for the map codecs. The rows are given in the file's
    // order, which is the table's. Position is left after the last row.
    bool TableRows(std::size_t& Position, std::size_t End, const std::string& Owner,
        std::vector<std::pair<std::string, FDecodedValue>>& Rows)
    {
        if (!Error.Code.empty())
            return false;
        if (Position > End || End > Body.size())
            return Failure(Position, "invalid-value-envelope");
        FByteReader Reader(Body, Position, End);
        std::size_t Count = 0;
        if (!Read(Reader.Count(Count, 65536), Reader))
            return false;
        std::unordered_set<std::string> Seen;
        for (std::size_t Index = 0; Index < Count; ++Index)
        {
            FDecodedValue Name, Row;
            if (!String(Reader, true, Name))
                return false;
            const auto Identity = PropertyIdentity(Name.Text);
            if (Identity.empty() || Identity == "none" || !Seen.insert(Identity).second)
                return Failure(Reader.Tell(), "invalid-or-duplicate-table-row");
            Position = Reader.Tell();
            if (!Properties(Position, End, Row, 0, Owner))
                return false;
            Rows.emplace_back(std::move(Name.Text), std::move(Row));
            if (!Read(Reader.Skip(Position - Reader.Tell()), Reader))
                return false;
        }
        Position = Reader.Tell();
        return true;
    }
};
}

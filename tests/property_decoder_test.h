#pragma once
#include "core/property_decoder.h"
struct FValueFixture
{
    P4MapKit::FPackageInventory Package;
    std::vector<std::uint8_t> Body;
    FValueFixture()
    {
        Package.Package = "/Game/Fixture";
        Package.Imports = {{"", "", "/Script/Engine", 0}, {"", "", "DataTable", -1},
            {"", "", "/Script/Carrot4", 0}, {"", "", "RowType", -3}};
    }
    void Name(std::vector<std::uint8_t>& Bytes, const std::string& Text)
    {
        auto At = std::find(Package.Names.begin(), Package.Names.end(), Text);
        if (At == Package.Names.end()) { Package.Names.push_back(Text); At = Package.Names.end() - 1; }
        P4Put(Bytes, std::size_t(At - Package.Names.begin())); P4Put(Bytes, 0);
    }
    void Tag(std::vector<std::uint8_t>& Bytes, const std::string& Type, const std::vector<std::uint8_t>& Payload,
        const std::vector<std::string>& Detail = {}, const std::string& Key = "Value", bool Bit = false)
    {
        Name(Bytes, Key); Name(Bytes, Type); P4Put(Bytes, Payload.size()); P4Put(Bytes, 0);
        for (const auto& Item : Detail) Name(Bytes, Item);
        if (Type == "StructProperty") for (int Index = 0; Index < 4; ++Index) P4Put(Bytes, 0);
        if (Type == "BoolProperty") Bytes.push_back(Bit ? 1 : 0);
        Bytes.push_back(0); Bytes.insert(Bytes.end(), Payload.begin(), Payload.end());
    }
    P4MapKit::FPropertyEnvelope One(const std::string& Type, const std::vector<std::uint8_t>& Payload,
        const std::vector<std::string>& Detail = {})
    {
        Body.clear(); Tag(Body, Type, Payload, Detail); Name(Body, "None");
        const auto List = P4MapKit::ReadPropertyEnvelopes(Body, Package.Names, 0, Body.size());
        Check(List.Error.Code.empty()); return List.Properties[0];
    }
    std::vector<std::uint8_t> Integer(std::uint64_t Bits, std::size_t Width)
    { std::vector<std::uint8_t> Bytes; P4Put(Bytes, Bits, Width); return Bytes; }
    void Table(const std::vector<std::string>& Rows)
    {
        Body.clear(); Tag(Body, "ObjectProperty", Integer(std::uint64_t(0) - 4, 4), {}, "RowStruct"); Name(Body, "None");
        P4Put(Body, 0); P4Put(Body, Rows.size());
        for (const auto& Row : Rows) { Name(Body, Row); Tag(Body, "UInt64Property", Integer(UINT64_MAX, 8)); Name(Body, "None"); }
        Package.Exports.resize(1); Package.Exports[0].Class = -2; Package.Exports[0].Size = Body.size();
    }
};
static void CheckValueDecoder()
{
    using namespace P4MapKit;
    FValueFixture Fixture;
    const auto Decode = [&](const FPropertyEnvelope& Tag, const std::string& Json) {
        FPropertyDecoder Decoder(Fixture.Package, Fixture.Body); FDecodedValue Value;
        Check(Decoder.Property(Tag, Value)); Check(Decoder.Error.Code.empty()); Check(DecodedJson(Value) == Json);
        FPropertyDecoder TypedDecoder(Fixture.Package, Fixture.Body); FDecodedValue Typed;
        Check(TypedDecoder.TypedProperty(Tag, Typed)); Check(TypedDecoder.Error.Code.empty());
        Check(DecodedJson(Typed) == Json); Check(DecodedJson(LegacyDecodedValue(Typed)) == Json);
    };
    const auto Refuse = [&](const FPropertyEnvelope& Tag, const std::string& Code) {
        FPropertyDecoder Decoder(Fixture.Package, Fixture.Body); FDecodedValue Value;
        Check(!Decoder.Property(Tag, Value)); Check(Decoder.Error.Code == Code);
    };
    for (const auto& Case : std::vector<std::pair<std::string, std::size_t>>{{"Int8Property",1},{"Int16Property",2},{"IntProperty",4},{"Int64Property",8},
        {"UInt16Property",2},{"UInt32Property",4},{"UInt64Property",8},{"ByteProperty",1}})
    {
        const auto Signed = Case.first.compare(0, 3, "Int") == 0;
        const auto Max = Case.second == 8 ? UINT64_MAX : (std::uint64_t(1) << (Case.second * 8)) - 1;
        Decode(Fixture.One(Case.first, Fixture.Integer(Max, Case.second), Case.first == "ByteProperty" ? std::vector<std::string>{"None"} : std::vector<std::string>{}), Signed ? "-1" : std::to_string(Max));
        Decode(Fixture.One(Case.first, Fixture.Integer(0, Case.second), Case.first == "ByteProperty" ? std::vector<std::string>{"None"} : std::vector<std::string>{}), "0");
        Refuse(Fixture.One(Case.first, Fixture.Integer(0, Case.second + 1), Case.first == "ByteProperty" ? std::vector<std::string>{"None"} : std::vector<std::string>{}), Case.first == "ByteProperty" ? "truncated" : "unread-property-payload");
    }
    Decode(Fixture.One("Int64Property", Fixture.Integer(std::uint64_t(1) << 63, 8)), "-9223372036854775808");
    Decode(Fixture.One("UInt64Property", Fixture.Integer(9007199254740993ULL, 8)), "9007199254740993");
    Decode(Fixture.One("DoubleProperty", Fixture.Integer(0x8000000000000000ULL, 8)), "-0.0");
    Decode(Fixture.One("FloatProperty", Fixture.Integer(0x3f800000, 4)), "1.0");
    for (const auto& Kind : {std::string("FloatProperty"), std::string("DoubleProperty")})
    {
        const auto Width = Kind == "FloatProperty" ? 4u : 8u;
        Refuse(Fixture.One(Kind, Fixture.Integer(Width == 4 ? 0x7f800000ULL : 0x7ff0000000000000ULL, Width)), "nonfinite-property");
        Refuse(Fixture.One(Kind, {}), "truncated");
    }
    {
        const FArchivedFloatCodec Codec{"AnimNotifyEvent", "Value", true, false, false};
        for (const std::uint32_t Word : {0xffc4b7c0u, 0x7fc00001u, 0x7f800001u})
        {
            const auto Tag = Fixture.One("FloatProperty", Fixture.Integer(Word, 4));
            Refuse(Tag, "nonfinite-property"); // Ordinary decoder/mesh boundary stays strict.
            FPropertyDecoder Decoder(Fixture.Package, Fixture.Body, 1000000, 64 * 1024 * 1024, {}, {Codec});
            FDecodedValue Value, Again;
            Check(Decoder.TypedProperty(Tag, Value, 0, "AnimNotifyEvent"));
            const auto Json = DecodedJson(Value);
            Check(Json == "{\"__native_float32_bits\":\"" + std::to_string(Word) + "\"}");
            Check(ReadDecodedJson(Json, Again)); Check(Again.Field("__native_float32_bits")->Text == std::to_string(Word));
            Check(DecodedJson(LegacyDecodedValue(Value)) == Json);
            FPropertyDecoder OtherOwner(Fixture.Package, Fixture.Body, 1000000, 64 * 1024 * 1024, {}, {Codec});
            Check(!OtherOwner.Property(Tag, Again, 0, "StaticMaterial")); Check(OtherOwner.Error.Code == "nonfinite-property");
        }
        const auto Infinity = Fixture.One("FloatProperty", Fixture.Integer(0x7f800000u, 4));
        FPropertyDecoder OnlyNaN(Fixture.Package, Fixture.Body, 1000000, 64 * 1024 * 1024, {}, {Codec}); FDecodedValue Value;
        Check(!OnlyNaN.Property(Infinity, Value, 0, "AnimNotifyEvent")); Check(OnlyNaN.Error.Code == "nonfinite-property");
        const FArchivedFloatCodec Radius{"FoliageType", "Value", false, true, false};
        std::vector<std::uint8_t> Vector;
        P4Put(Vector, 0x7effffffu); P4Put(Vector, 0x80000000u); P4Put(Vector, 0x7f800000u);
        const auto Tag = Fixture.One("StructProperty", Vector, {"Vector"});
        FPropertyDecoder Qualified(Fixture.Package, Fixture.Body, 1000000, 64 * 1024 * 1024, {}, {Radius});
        Check(Qualified.Property(Tag, Value, 0, "FoliageType"));
        Check(Value.Field("Y")->Text == "-0.0"); Check(Value.Field("Z")->Field("__native_float32_bits")->Text == "2139095040");
        P4Patch(Vector, 8, 0xff800000u);
        const auto Negative = Fixture.One("StructProperty", Vector, {"Vector"});
        FPropertyDecoder PositiveOnly(Fixture.Package, Fixture.Body, 1000000, 64 * 1024 * 1024, {}, {Radius});
        Check(!PositiveOnly.Property(Negative, Value, 0, "FoliageType")); Check(PositiveOnly.Error.Code == "nonfinite-property");
        FPropertyDecoder Duplicate(Fixture.Package, Fixture.Body, 1000000, 64 * 1024 * 1024, {}, {Radius, Radius});
        Check(!Duplicate.Property(Tag, Value, 0, "FoliageType")); Check(Duplicate.Error.Code == "ambiguous-archived-float-codec");
    }
    std::mt19937_64 Random(614);
    for (int Index = 0; Index < 2000; ++Index)
    {
        const auto Bits = Random(); double Expected = 0; std::memcpy(&Expected, &Bits, 8);
        if (!std::isfinite(Expected)) continue;
        const auto Tag = Fixture.One("DoubleProperty", Fixture.Integer(Bits, 8));
        FPropertyDecoder Decoder(Fixture.Package, Fixture.Body); FDecodedValue Value;
        Check(Decoder.Property(Tag, Value)); const auto Actual = std::stod(Value.Text);
        Check(Actual == Expected); Check(std::signbit(Actual) == std::signbit(Expected));
    }
    for (const auto& Kind : {"NameProperty", "EnumProperty", "ByteProperty"})
    {
        std::vector<std::uint8_t> Name; Fixture.Name(Name, "FixtureName");
        Decode(Fixture.One(Kind, Name, std::string(Kind) == "NameProperty" ? std::vector<std::string>{} : std::vector<std::string>{"None"}), "\"FixtureName\"");
    }
    {
        // Literal "Stem_0" and numbered "Stem" have the same display spelling
        // but distinct archive identities. No reference search may mistake them.
        std::vector<std::uint8_t> Payload; Fixture.Name(Payload, "Stem"); P4Patch(Payload, 4, 1);
        auto NameTag = Fixture.One("NameProperty", Payload);
        FPropertyDecoder Decoder(Fixture.Package, Fixture.Body); FDecodedValue Numbered, Legacy;
        Check(Decoder.TypedProperty(NameTag, Numbered)); Check(Numbered.Kind == FDecodedValue::EKind::Name);
        Check(Numbered.NameBase == "Stem" && Numbered.NameNumber == 1 && Numbered.Text == "Stem_0");
        Check(Decoder.Property(NameTag, Legacy)); Check(Legacy.Kind == FDecodedValue::EKind::String);
        Payload.clear(); Fixture.Name(Payload, "Stem_0"); NameTag = Fixture.One("NameProperty", Payload);
        FPropertyDecoder LiteralDecoder(Fixture.Package, Fixture.Body); FDecodedValue Literal;
        Check(LiteralDecoder.TypedProperty(NameTag, Literal)); Check(Literal.Text == Numbered.Text);
        Check(Literal.NameBase == "Stem_0" && Literal.NameNumber == 0 && Literal.NameIndex != Numbered.NameIndex);
        auto EnumTag = Fixture.One("EnumProperty", Payload, {"FixtureEnum"});
        FPropertyDecoder EnumDecoder(Fixture.Package, Fixture.Body); FDecodedValue Enum;
        Check(EnumDecoder.TypedProperty(EnumTag, Enum)); Check(Enum.Kind == FDecodedValue::EKind::Enum);
        Check(Enum.EnumType == "FixtureEnum" && Enum.Type == "EnumProperty" && Enum.NameNumber == 0);
        EnumTag = Fixture.One("ByteProperty", Payload, {"FixtureEnum"});
        FPropertyDecoder ByteDecoder(Fixture.Package, Fixture.Body);
        Check(ByteDecoder.TypedProperty(EnumTag, Enum)); Check(Enum.Kind == FDecodedValue::EKind::Enum);
        Check(Enum.EnumType == "FixtureEnum" && Enum.Type == "ByteProperty");
        EnumTag = Fixture.One("ByteProperty", Fixture.Integer(2, 1), {"FixtureEnum"});
        FPropertyDecoder OrdinalDecoder(Fixture.Package, Fixture.Body);
        Check(OrdinalDecoder.TypedProperty(EnumTag, Enum)); Check(Enum.Kind == FDecodedValue::EKind::Enum && Enum.EnumNumeric);
        Check(Enum.EnumType == "FixtureEnum" && DecodedJson(Enum) == "2");
        Check(LegacyDecodedValue(Enum).Kind == FDecodedValue::EKind::Number);
        auto StringTag = Fixture.One("StrProperty", std::vector<std::uint8_t>{});
        FPropertyDecoder Truncated(Fixture.Package, Fixture.Body); FDecodedValue Kept = Literal;
        Check(!Truncated.TypedProperty(StringTag, Kept)); Check(Kept.NameBase == Literal.NameBase);
    }
    std::vector<std::uint8_t> Text; P4String(Text, "quote\"\\\n\t\x01");
    Decode(Fixture.One("StrProperty", Text), "\"quote\\\"\\\\\\u000a\\u0009\\u0001\"");
    Decode(Fixture.One("ObjectProperty", Fixture.Integer(0,4)), "null");
    Decode(Fixture.One("InterfaceProperty", Fixture.Integer(std::uint64_t(0)-4,4)), "\"/Script/Carrot4.RowType\"");
    Refuse(Fixture.One("ObjectProperty", Fixture.Integer(5,4)), "invalid-property-reference");
    for (const auto* Kind : {"ObjectProperty", "InterfaceProperty", "ClassProperty", "WeakObjectProperty"})
    {
        auto ReferenceTag = Fixture.One(Kind, Fixture.Integer(std::uint64_t(0)-4, 4));
        FPropertyDecoder Decoder(Fixture.Package, Fixture.Body); FDecodedValue Reference;
        Check(Decoder.TypedProperty(ReferenceTag, Reference)); Check(Reference.Kind == FDecodedValue::EKind::HardReference);
        Check(Reference.ReferenceIndex == -4 && Reference.Type == Kind && Reference.Text == "/Script/Carrot4.RowType");
        ReferenceTag = Fixture.One(Kind, Fixture.Integer(0, 4));
        FPropertyDecoder NullDecoder(Fixture.Package, Fixture.Body);
        Check(NullDecoder.TypedProperty(ReferenceTag, Reference)); Check(Reference.Kind == FDecodedValue::EKind::HardReference);
        Check(Reference.ReferenceIndex == 0 && DecodedJson(Reference) == "null");
    }
    std::vector<std::uint8_t> Soft; Fixture.Name(Soft, "None"); P4String(Soft, "");
    Decode(Fixture.One("SoftObjectProperty", Soft), "\"\"");
    Soft.clear(); Fixture.Name(Soft,"AssetPath"); P4String(Soft,"SubObject");
    Refuse(Fixture.One("SoftObjectProperty",Soft), "unsupported-soft-subobject");
    for (const auto* Kind : {"SoftObjectProperty", "SoftClassProperty"})
    {
        auto SoftTag = Fixture.One(Kind, Soft); FDecodedValue Path;
        FPropertyDecoder Decoder(Fixture.Package, Fixture.Body);
        Check(Decoder.TypedProperty(SoftTag, Path)); Check(Path.Kind == FDecodedValue::EKind::SoftPath);
        Check(Path.AssetPath == "AssetPath" && Path.SubPath == "SubObject" && Path.Text == "AssetPath:SubObject");
        Check(Path.Type == Kind && DecodedJson(Path) == "\"AssetPath:SubObject\"");
        for (std::size_t Size = 0; Size < Soft.size(); ++Size)
        {
            auto Cut = Fixture.One(Kind, std::vector<std::uint8_t>(Soft.begin(), Soft.begin() + Size));
            FPropertyDecoder Truncated(Fixture.Package, Fixture.Body); Check(!Truncated.TypedProperty(Cut, Path));
        }
    }
    {
        std::vector<std::uint8_t> Field; P4Put(Field, 2); Fixture.Name(Field, "Member"); Fixture.Name(Field, "Nested");
        P4Put(Field, std::uint64_t(0)-4);
        auto FieldTag = Fixture.One("FieldPathProperty", Field); FDecodedValue Value;
        FPropertyDecoder Decoder(Fixture.Package, Fixture.Body);
        Check(Decoder.TypedProperty(FieldTag, Value)); Check(Value.Kind == FDecodedValue::EKind::FieldPath);
        Check(Value.ReferenceIndex == -4 && Value.Field("field")->Items.size() == 2);
        Check(Value.Field("field")->Items[0].Kind == FDecodedValue::EKind::Name);
        Check(Value.Field("owner")->Kind == FDecodedValue::EKind::HardReference);
        Check(DecodedJson(Value) == "{\"field\":[\"Member\",\"Nested\"],\"owner\":\"/Script/Carrot4.RowType\"}");
        for (std::size_t Size = 0; Size < Field.size(); ++Size)
        {
            auto Cut = Fixture.One("FieldPathProperty", std::vector<std::uint8_t>(Field.begin(), Field.begin()+Size));
            FPropertyDecoder Truncated(Fixture.Package, Fixture.Body); Check(!Truncated.TypedProperty(Cut, Value));
        }
        P4Patch(Field, Field.size()-4, 0); FieldTag = Fixture.One("FieldPathProperty", Field);
        FPropertyDecoder MissingOwner(Fixture.Package, Fixture.Body); Check(!MissingOwner.TypedProperty(FieldTag, Value));
        Check(MissingOwner.Error.Code == "invalid-field-path-owner");
        Field.clear(); P4Put(Field, 0); P4Put(Field, 0); FieldTag = Fixture.One("FieldPathProperty", Field);
        FPropertyDecoder Empty(Fixture.Package, Fixture.Body); Check(Empty.TypedProperty(FieldTag, Value));
        Check(Value.Field("field")->Items.empty() && Value.Field("owner")->ReferenceIndex == 0);
        Field.clear(); P4Put(Field, 1); Fixture.Name(Field, "None"); P4Put(Field, std::uint64_t(0)-4);
        FieldTag = Fixture.One("FieldPathProperty", Field); FPropertyDecoder Old(Fixture.Package, Fixture.Body);
        Check(!Old.TypedProperty(FieldTag, Value)); Check(Old.Error.Code == "unsupported-legacy-field-path");
        Field.clear(); P4Put(Field, 65); FieldTag = Fixture.One("FieldPathProperty", Field);
        FPropertyDecoder Bounded(Fixture.Package, Fixture.Body); Check(!Bounded.TypedProperty(FieldTag, Value));
        Check(Bounded.Error.Code == "invalid-count");
    }
    Decode(Fixture.One("BoolProperty",{}), "false");
    auto Tag = Fixture.One("BoolProperty",{}); Tag.BoolValue = true; Decode(Tag,"true");
    Tag.HasGuid = true; Refuse(Tag,"property-guid-requires-adapter");
    Tag.HasGuid = false; Tag.Size = 1; Refuse(Tag,"invalid-value-envelope");
    std::vector<std::uint8_t> Localized; P4Put(Localized,0); Localized.push_back(0);
    P4String(Localized,"FixtureNamespace"); P4String(Localized,"FixtureKey"); P4String(Localized,"quote\"\\\n\xc3\xb1");
    Decode(Fixture.One("TextProperty",Localized),"{\"__native_text\":{\"flags\":0,\"history\":0,\"namespace\":\"FixtureNamespace\",\"key\":\"FixtureKey\",\"source\":\"quote\\\"\\\\\\u000a\xc3\xb1\"}}");
    {
        FPropertyDecoder Decoder(Fixture.Package, Fixture.Body); FDecodedValue TextValue;
        Check(Decoder.TypedProperty(ReadPropertyEnvelopes(Fixture.Body, Fixture.Package.Names, 0, Fixture.Body.size()).Properties[0], TextValue));
        Check(TextValue.Kind == FDecodedValue::EKind::NativeText && TextValue.Type == "TextProperty");
        Check(TextValue.Field("__native_text")->Field("namespace")->Text == "FixtureNamespace");
        Check(TextValue.Field("__native_text")->Field("source")->Kind == FDecodedValue::EKind::String);
    }
    for (std::size_t Size=0;Size<Localized.size();++Size)
        Refuse(Fixture.One("TextProperty",std::vector<std::uint8_t>(Localized.begin(),Localized.begin()+Size)),"truncated");
    Localized.push_back(0); Refuse(Fixture.One("TextProperty",Localized),"unread-property-payload"); Localized.pop_back();
    for (int History=1;History<=12;++History)
    { Localized[4]=std::uint8_t(History); Refuse(Fixture.One("TextProperty",Localized),"unsupported-text-history"); }
    std::vector<std::uint8_t> EmptyText; P4Put(EmptyText,0); EmptyText.push_back(255); P4Put(EmptyText,0);
    Decode(Fixture.One("TextProperty",EmptyText),"{\"__native_text\":{\"flags\":0,\"history\":-1,\"has_source\":false}}");
    P4Patch(EmptyText,0,2); P4Patch(EmptyText,5,1); P4String(EmptyText,"Custom label");
    Decode(Fixture.One("TextProperty",EmptyText),"{\"__native_text\":{\"flags\":2,\"history\":-1,\"has_source\":true,\"source\":\"Custom label\"}}");
    P4Patch(EmptyText,5,2); Refuse(Fixture.One("TextProperty",EmptyText),"noncanonical-text-bool");
    EmptyText.resize(5); Refuse(Fixture.One("TextProperty",EmptyText),"truncated");
    Tag = Fixture.One("TextProperty",{}); Refuse(Tag,"truncated");
    Tag.PayloadOffset = SIZE_MAX; Refuse(Tag,"invalid-value-envelope");
    Tag = Fixture.One("StructProperty",{}, {"Vector"}); Tag.Detail.clear(); Refuse(Tag,"invalid-value-envelope");
    for (const auto& Type : {"Vector", "Rotator", "Vector2D", "Vector4", "Quat", "LinearColor", "Color", "IntPoint", "IntVector", "Plane", "Guid"})
    {
        const std::size_t Count = std::string(Type) == "Vector" || std::string(Type) == "Rotator" || std::string(Type) == "IntVector" ? 3 :
            std::string(Type) == "Vector2D" || std::string(Type) == "IntPoint" ? 2 : 4;
        const auto Width = std::string(Type) == "Color" ? 1u : 4u;
        std::vector<std::uint8_t> Payload(Count*Width,0); Tag = Fixture.One("StructProperty",Payload,{Type});
        FPropertyDecoder Decoder(Fixture.Package,Fixture.Body); FDecodedValue Value;
        Check(Decoder.Property(Tag,Value)); Check(Value.Fields.size()==Count);
        Payload.push_back(0); Refuse(Fixture.One("StructProperty",Payload,{Type}),"unread-property-payload");
    }
    for (const auto& Type : {"SoftObjectPath", "SoftClassPath"})
    { Soft.clear(); Fixture.Name(Soft,"AssetPath"); P4String(Soft,""); Decode(Fixture.One("StructProperty",Soft,{Type}),"\"AssetPath\""); }
    {
        std::vector<std::uint8_t> Smart; Fixture.Name(Smart,"CurveName");
        Decode(Fixture.One("StructProperty",Smart,{"SmartName"}),"{\"DisplayName\":\"CurveName\"}");
        Smart.pop_back(); Refuse(Fixture.One("StructProperty",Smart,{"SmartName"}),"truncated");
        Smart.clear(); Fixture.Name(Smart,"CurveName"); Smart.push_back(0); Smart.push_back(0);
        Refuse(Fixture.One("StructProperty",Smart,{"SmartName"}),"unread-property-payload");
    }
    Soft.clear(); Fixture.Name(Soft,"FixtureTag"); Decode(Fixture.One("StructProperty",Soft,{"GameplayTag"}),"{\"tag\":\"FixtureTag\"}");
    Soft.clear(); P4Put(Soft,1); Fixture.Name(Soft,"FixtureTag"); Decode(Fixture.One("StructProperty",Soft,{"GameplayTagContainer"}),"{\"tags\":[\"FixtureTag\"]}");
    Refuse(Fixture.One("StructProperty",{}, {"Matrix"}),"unsupported-native-struct");
    // A material's input is written natively and is not read: the value says how many bytes there were.
    Decode(Fixture.One("StructProperty",std::vector<std::uint8_t>(48,7),{"ColorMaterialInput"}),"{\"__native_bytes_not_read\":48}");
    Decode(Fixture.One("StructProperty",{}, {"ExpressionInput"}),"{\"__native_bytes_not_read\":0}");
    Decode(Fixture.One("StructProperty",Fixture.Integer(65535,4),{"NavAgentSelector"}),"{\"PackedBits\":65535}");
    {
        std::vector<std::uint8_t> Key{1, 2, 0};
        for (const std::uint32_t Bits : {0x3f800000u, 0x40000000u, 0u, 0u, 0u, 0u}) P4Put(Key, Bits);
        Decode(Fixture.One("StructProperty",Key,{"RichCurveKey"}),
            "{\"InterpMode\":1,\"TangentMode\":2,\"TangentWeightMode\":0,\"Time\":1.0,\"Value\":2.0,\"ArriveTangent\":0.0,\"ArriveTangentWeight\":0.0,\"LeaveTangent\":0.0,\"LeaveTangentWeight\":0.0}");
        Key.pop_back(); Refuse(Fixture.One("StructProperty",Key,{"RichCurveKey"}),"truncated");
        std::vector<std::uint8_t> Simple; P4Put(Simple, 0x3f800000u); P4Put(Simple, 0x40000000u);
        Decode(Fixture.One("StructProperty",Simple,{"SimpleCurveKey"}),"{\"Time\":1.0,\"Value\":2.0}");
    }
    std::vector<std::uint8_t> Nested; Fixture.Tag(Nested,"IntProperty",Fixture.Integer(42,4)); Fixture.Name(Nested,"None");
    Decode(Fixture.One("StructProperty",Nested,{"UserStruct"}),"{\"Value\":42}");
    Nested.push_back(0); Refuse(Fixture.One("StructProperty",Nested,{"UserStruct"}),"unread-struct-payload"); Nested.pop_back();
    for (const auto& Kind : {"ArrayProperty", "SetProperty", "MapProperty"})
    {
        std::vector<std::uint8_t> Payload;
        if (std::string(Kind)!="ArrayProperty") P4Put(Payload,0);
        P4Put(Payload,2); P4Put(Payload,42); if (std::string(Kind)=="MapProperty") P4Put(Payload,43);
        P4Put(Payload,44); if (std::string(Kind)=="MapProperty") P4Put(Payload,45);
        const std::vector<std::string> Detail = std::string(Kind)=="MapProperty" ? std::vector<std::string>{"IntProperty","IntProperty"} : std::vector<std::string>{"IntProperty"};
        Decode(Fixture.One(Kind,Payload,Detail),std::string(Kind)=="MapProperty" ? "{\"__native_map_pairs\":[[42,43],[44,45]]}" : "[42,44]");
        Payload.push_back(0); Refuse(Fixture.One(Kind,Payload,Detail),"unread-property-payload"); Payload.pop_back();
        if (std::string(Kind)!="ArrayProperty") { P4Patch(Payload,0,1); Refuse(Fixture.One(Kind,Payload,Detail),"unsupported-collection-removals"); }
    }
    std::vector<std::uint8_t> Array; P4Put(Array,1); Array.push_back(2);
    Refuse(Fixture.One("ArrayProperty",Array,{"BoolProperty"}),"noncanonical-property-bool"); Array.back()=1;
    Decode(Fixture.One("ArrayProperty",Array,{"BoolProperty"}),"[true]");
    Array.clear(); P4Put(Array,1); Fixture.Tag(Array,"StructProperty",std::vector<std::uint8_t>(12,0),{"Vector"});
    Decode(Fixture.One("ArrayProperty",Array,{"StructProperty"}),"[{\"X\":0.0,\"Y\":0.0,\"Z\":0.0}]");
    P4Patch(Array,0,2); Refuse(Fixture.One("ArrayProperty",Array,{"StructProperty"}),"truncated");
    P4Patch(Array,0,0); Refuse(Fixture.One("ArrayProperty",Array,{"StructProperty"}),"struct-array-count-mismatch");
    Array.clear(); P4Put(Array,1); Fixture.Tag(Array,"StructProperty",Nested,{"UserStruct"});
    Decode(Fixture.One("ArrayProperty",Array,{"StructProperty"}),"[{\"Value\":42}]");
    Array.clear(); P4Put(Array,0); Fixture.Tag(Array,"StructProperty",{}, {"SoftObjectPath"});
    Decode(Fixture.One("ArrayProperty",Array,{"StructProperty"}),"[]"); P4Patch(Array,0,1);
    Refuse(Fixture.One("ArrayProperty",Array,{"StructProperty"}),"truncated");
    Soft.clear(); Fixture.Name(Soft,"None"); P4String(Soft,""); Fixture.Name(Soft,"AssetPath"); P4String(Soft,"");
    Array.clear(); P4Put(Array,2); Fixture.Tag(Array,"StructProperty",Soft,{"SoftObjectPath"});
    Decode(Fixture.One("ArrayProperty",Array,{"StructProperty"}),"[\"\",\"AssetPath\"]");
    for (const auto& Type : {"SoftClassPath","GameplayTag","GameplayTagContainer","SmartName"})
    {
        std::vector<std::uint8_t> Payload;
        if (std::string(Type)=="GameplayTagContainer") P4Put(Payload,1);
        Fixture.Name(Payload,"FixtureTag"); if (std::string(Type)=="SoftClassPath") P4String(Payload,"");
        Array.clear(); P4Put(Array,1); Fixture.Tag(Array,"StructProperty",Payload,{Type});
        Decode(Fixture.One("ArrayProperty",Array,{"StructProperty"}),std::string(Type)=="SoftClassPath" ? "[\"FixtureTag\"]" :
            std::string(Type)=="GameplayTag" ? "[{\"tag\":\"FixtureTag\"}]" :
            std::string(Type)=="SmartName" ? "[{\"DisplayName\":\"FixtureTag\"}]" : "[{\"tags\":[\"FixtureTag\"]}]");
    }
    std::vector<std::uint8_t> Map; P4Put(Map,0); P4Put(Map,1); P4Put(Map,7); Map.insert(Map.end(),Nested.begin(),Nested.end());
    Tag=Fixture.One("MapProperty",Map,{"IntProperty","StructProperty"});
    Refuse(Tag,"missing-map-struct-codec");
    const FMapStructCodec MapCodec{"Root","Value","","UserStruct"};
    FPropertyDecoder TypedMap(Fixture.Package,Fixture.Body,1000000,64*1024*1024,{MapCodec}); FDecodedValue MapValue;
    Check(TypedMap.Property(Tag,MapValue,0,"root")); Check(DecodedJson(MapValue)=="{\"__native_map_pairs\":[[7,{\"Value\":42}]]}");
    FPropertyDecoder Ambiguous(Fixture.Package,Fixture.Body,1000000,64*1024*1024,{MapCodec,MapCodec});
    Check(!Ambiguous.Property(Tag,MapValue,0,"Root")); Check(Ambiguous.Error.Code=="ambiguous-map-struct-codec");
    auto NativeOnly=MapCodec; NativeOnly.ValueNative=true;
    FPropertyDecoder UnknownNative(Fixture.Package,Fixture.Body,1000000,64*1024*1024,{NativeOnly});
    Check(!UnknownNative.Property(Tag,MapValue,0,"Root")); Check(UnknownNative.Error.Code=="unsupported-native-struct");
    Map.clear(); P4Put(Map,0); P4Put(Map,1); Map.insert(Map.end(),Nested.begin(),Nested.end()); P4Put(Map,7);
    Tag=Fixture.One("MapProperty",Map,{"StructProperty","IntProperty"});
    FPropertyDecoder StructKey(Fixture.Package,Fixture.Body,1000000,64*1024*1024,{{"Root","Value","UserStruct",""}}); FDecodedValue KeyValue;
    Check(StructKey.Property(Tag,KeyValue,0,"Root")); Check(DecodedJson(KeyValue)=="{\"__native_map_pairs\":[[{\"Value\":42},7]]}");
    Map.clear(); P4Put(Map,0); P4Put(Map,1); P4Put(Map,7); Map.resize(Map.size()+12,0);
    Tag=Fixture.One("MapProperty",Map,{"IntProperty","StructProperty"});
    FPropertyDecoder NativeMap(Fixture.Package,Fixture.Body,1000000,64*1024*1024,{{"Root","Value","","Vector",false,true}}); FDecodedValue NativeValue;
    Check(NativeMap.Property(Tag,NativeValue,0,"Root")); Check(NativeValue.Field("__native_map_pairs")->Items[0].Items[1].Fields.size()==3);
    {
        // UE GameplayTag has a network serializer, not a native archive serializer.
        // A reflected non-native map key therefore holds its tagged TagName field.
        std::vector<uint8_t> Name,Key,Pairs;Fixture.Name(Name,"Terrain.Effect.Sand");
        Fixture.Tag(Key,"NameProperty",Name,{},"TagName");Fixture.Name(Key,"None");
        P4Put(Pairs,0);P4Put(Pairs,1);Pairs.insert(Pairs.end(),Key.begin(),Key.end());P4Put(Pairs,7);
        const auto KeyTag=Fixture.One("MapProperty",Pairs,{"StructProperty","IntProperty"});
        const FMapStructCodec Codec{"Root","Value","GameplayTag","",false,false};
        for(bool Typed:{false,true})
        {
            FPropertyDecoder Decoder(Fixture.Package,Fixture.Body,1000000,64*1024*1024,{Codec});FDecodedValue Value;
            Check(Typed?Decoder.TypedProperty(KeyTag,Value,0,"Root"):Decoder.Property(KeyTag,Value,0,"Root"));
            Check(DecodedJson(Value)=="{\"__native_map_pairs\":[[{\"TagName\":\"Terrain.Effect.Sand\"},7]]}");
        }
        auto NativeCodec=Codec;NativeCodec.KeyNative=true;
        FPropertyDecoder WrongSchema(Fixture.Package,Fixture.Body,1000000,64*1024*1024,{NativeCodec});FDecodedValue Value;
        Check(!WrongSchema.Property(KeyTag,Value,0,"Root"));
        Pairs.clear();P4Put(Pairs,0);P4Put(Pairs,1);Pairs.insert(Pairs.end(),Name.begin(),Name.end());P4Put(Pairs,7);
        const auto RawTag=Fixture.One("MapProperty",Pairs,{"StructProperty","IntProperty"});
        FPropertyDecoder Raw(Fixture.Package,Fixture.Body,1000000,64*1024*1024,{NativeCodec});
        Value=FDecodedValue();
        Check(Raw.Property(RawTag,Value,0,"Root"));
        Check(DecodedJson(Value)=="{\"__native_map_pairs\":[[{\"tag\":\"Terrain.Effect.Sand\"},7]]}");
        FPropertyDecoder NoGuess(Fixture.Package,Fixture.Body,1000000,64*1024*1024,{Codec});
        Check(!NoGuess.Property(RawTag,Value,0,"Root"));
    }
    Array.clear(); P4Put(Array,0); Fixture.Name(Array,"None"); Refuse(Fixture.One("ArrayProperty",Array,{"StructProperty"}),"invalid-struct-array-descriptor");
    Tag=Fixture.One("IntProperty",Fixture.Integer(42,4));
    FDecodedValue Value; FPropertyDecoder Depth(Fixture.Package,Fixture.Body); Check(!Depth.Property(Tag,Value,24));
    Check(Depth.Error.Code=="property-depth-limit"); Check(!Depth.Property(Tag,Value));
    FPropertyDecoder Budget(Fixture.Package,Fixture.Body,0); Check(!Budget.Property(Tag,Value)); Check(Budget.Error.Code=="decoded-value-budget");
    FPropertyDecoder TextBudget(Fixture.Package,Fixture.Body,100,0); Check(!TextBudget.Property(Tag,Value));
    for (int Level=0;Level<26;++Level)
    { std::vector<std::uint8_t> Outer; Fixture.Tag(Outer,"StructProperty",Nested,{"UserStruct"}); Fixture.Name(Outer,"None"); Nested=std::move(Outer); }
    Refuse(Fixture.One("StructProperty",Nested,{"UserStruct"}),"property-depth-limit");

    {
        // Identical text is a name, a string or a reference according to the
        // archive type, including inside collections. Discovery never scans text.
        const std::string Path = "/Script/Carrot4.RowType";
        std::vector<std::uint8_t> String, Name, Names;
        P4String(String, Path); Fixture.Name(Name, Path); P4Put(Names, 1); Fixture.Name(Names, Path);
        Fixture.Body.clear();
        Fixture.Tag(Fixture.Body, "StrProperty", String, {}, "String");
        Fixture.Tag(Fixture.Body, "NameProperty", Name, {}, "Name");
        Fixture.Tag(Fixture.Body, "ObjectProperty", Fixture.Integer(std::uint64_t(0)-4, 4), {}, "Reference");
        Fixture.Tag(Fixture.Body, "ArrayProperty", Names, {"NameProperty"}, "Names");
        std::vector<std::uint8_t> Pairs; P4Put(Pairs, 0); P4Put(Pairs, 1);
        Pairs.insert(Pairs.end(), String.begin(), String.end()); P4Put(Pairs, std::uint64_t(0)-4);
        Fixture.Tag(Fixture.Body, "MapProperty", Pairs, {"StrProperty", "ObjectProperty"}, "Map");
        Fixture.Name(Fixture.Body, "None");
        FPropertyDecoder Decoder(Fixture.Package, Fixture.Body); FDecodedValue Typed; std::size_t At = 0;
        Check(Decoder.TypedProperties(At, Fixture.Body.size(), Typed)); Check(At == Fixture.Body.size());
        Check(Typed.Field("String")->Text == Path && Typed.Field("String")->Kind == FDecodedValue::EKind::String);
        Check(Typed.Field("Name")->Text == Path && Typed.Field("Name")->Kind == FDecodedValue::EKind::Name);
        Check(Typed.Field("Reference")->Text == Path && Typed.Field("Reference")->Kind == FDecodedValue::EKind::HardReference);
        Check(Typed.Field("Names")->Items[0].Kind == FDecodedValue::EKind::Name);
        const auto& Pair = Typed.Field("Map")->Field("__native_map_pairs")->Items[0].Items;
        Check(Pair[0].Kind == FDecodedValue::EKind::String && Pair[1].Kind == FDecodedValue::EKind::HardReference);
        FPropertyDecoder Legacy(Fixture.Package, Fixture.Body); FDecodedValue Old; At = 0;
        Check(Legacy.Properties(At, Fixture.Body.size(), Old)); Check(DecodedJson(Typed) == DecodedJson(Old));
        Check(LegacyDecodedValue(Typed).Field("Name")->Kind == FDecodedValue::EKind::String);
        FPropertyDecoder Cut(Fixture.Package, Fixture.Body); At = 0;
        Check(!Cut.TypedProperties(At, Fixture.Body.size()-1, Typed)); Check(At == 0);
        Check(Typed.Field("Reference")->Kind == FDecodedValue::EKind::HardReference);
        const auto NameTag = ReadPropertyEnvelopes(Fixture.Body, Fixture.Package.Names, 0, Fixture.Body.size()).Properties[1];
        FPropertyDecoder Tight(Fixture.Package, Fixture.Body, 100, Path.size());
        Check(!Tight.TypedProperty(NameTag, Typed)); Check(Tight.Error.Code == "decoded-value-budget");
    }
    Fixture.Table({"First","Second"}); FDecodedValue Document;
    FPropertyDecoder Tables(Fixture.Package,Fixture.Body); Check(Tables.Document(Document));
    Check(Document.Field("rows")->Items.size()==2); Check(Document.Field("rows")->Items[1].Field("Value")->Text=="18446744073709551615");
    {
        FPropertyDecoder Typed(Fixture.Package, Fixture.Body); FDecodedValue TypedDoc;
        Check(Typed.TypedDocument(TypedDoc)); Check(DecodedJson(TypedDoc) == DecodedJson(Document));
        Check(TypedDoc.Field("row_struct")->Kind == FDecodedValue::EKind::HardReference);
        Check(TypedDoc.Field("row_struct")->ReferenceIndex == -4);
        auto At = ReadPropertyEnvelopes(Fixture.Body, Fixture.Package.Names, 0, Fixture.Body.size()).End + 4;
        FPropertyDecoder Rows(Fixture.Package, Fixture.Body); std::vector<std::pair<std::string, FDecodedValue>> TypedRows;
        Check(Rows.TypedTableRows(At, Fixture.Body.size(), "RowType", TypedRows));
        Check(At == Fixture.Body.size() && TypedRows.size() == 2);
        Check(TypedRows[1].second.Field("Value")->Text == "18446744073709551615");
    }
    for (const auto& Rows : {std::vector<std::string>{"First","first"},std::vector<std::string>{"None"},std::vector<std::string>{""}})
    { Fixture.Table(Rows); FPropertyDecoder Bad(Fixture.Package,Fixture.Body); FDecodedValue Out; Check(!Bad.Document(Out)); Check(Bad.Error.Code=="invalid-or-duplicate-table-row"); }
    {
        // The rows by themselves, from where they start: in the file's order, each with its name, and
        // the place after the last one given back.
        Fixture.Table({"Second","First"});
        const auto Tags=ReadPropertyEnvelopes(Fixture.Body,Fixture.Package.Names,0,Fixture.Body.size());
        std::size_t At=Tags.End+4; std::vector<std::pair<std::string,FDecodedValue>> Found;
        FPropertyDecoder Rows(Fixture.Package,Fixture.Body); Check(Rows.TableRows(At,Fixture.Body.size(),"Row",Found));
        Check(At==Fixture.Body.size()); Check(Found.size()==2); Check(Found[0].first=="Second"); Check(Found[1].first=="First");
        Check(Found[1].second.Field("Value")->Text=="18446744073709551615");
        At=Tags.End+4; Found.clear(); FPropertyDecoder Short(Fixture.Package,Fixture.Body); Check(!Short.TableRows(At,Fixture.Body.size()-1,"Row",Found));
        At=Fixture.Body.size()+1; FPropertyDecoder Outside(Fixture.Package,Fixture.Body); Check(!Outside.TableRows(At,Fixture.Body.size(),"Row",Found));
        Check(Outside.Error.Code=="invalid-value-envelope");
        Fixture.Table({"First","FIRST"}); At=Tags.End+4; FPropertyDecoder Twice(Fixture.Package,Fixture.Body);
        Check(!Twice.TableRows(At,Fixture.Body.size(),"Row",Found)); Check(Twice.Error.Code=="invalid-or-duplicate-table-row");
    }
    Fixture.Table({}); FPropertyDecoder Empty(Fixture.Package,Fixture.Body); FDecodedValue EmptyDoc; Check(Empty.Document(EmptyDoc));
    Fixture.Body.push_back(0); Fixture.Package.Exports[0].Size++; FPropertyDecoder Tail(Fixture.Package,Fixture.Body); Check(!Tail.Document(Document));
    Fixture.Table({"First"});
    for (std::size_t Size=0;Size<Fixture.Body.size();++Size)
    { auto Package=Fixture.Package; Package.Exports[0].Size=Size; FPropertyDecoder Truncated(Package,Fixture.Body); FDecodedValue Out; Check(!Truncated.Document(Out)); }
    auto Package=Fixture.Package; Package.Exports.clear(); FPropertyDecoder Missing(Package,Fixture.Body); Check(!Missing.Document(Document));
    Package=Fixture.Package; Package.Exports[0].Class=100; FPropertyDecoder Class(Package,Fixture.Body); Check(!Class.Document(Document));
    Package=Fixture.Package; Package.Error={17,"preflight-refused"}; FPropertyDecoder Preflight(Package,Fixture.Body); Check(!Preflight.Document(Document));
    Fixture.Package.Imports[1].Name="GameSceneData"; Fixture.Package.Imports[0].Name="/Script/Carrot4";
    Fixture.Body.clear(); Fixture.Tag(Fixture.Body,"IntProperty",Fixture.Integer(42,4)); Fixture.Name(Fixture.Body,"None"); P4Put(Fixture.Body,0);
    Fixture.Package.Exports[0].Size=Fixture.Body.size(); FPropertyDecoder Scene(Fixture.Package,Fixture.Body); FDecodedValue SceneDoc;
    Check(Scene.Document(SceneDoc)); Check(SceneDoc.Field("values")->Field("Value")->Text=="42");
    Fixture.Package.Imports[1].Name="OnyonCombineEftColorData";
    FPropertyDecoder Onyon(Fixture.Package,Fixture.Body); FDecodedValue OnyonDoc;
    Check(Onyon.Document(OnyonDoc)); Check(OnyonDoc.Field("cls")->Text=="/Script/Carrot4.OnyonCombineEftColorData");
    Fixture.Body.push_back(0); Fixture.Package.Exports[0].Size++;
    FPropertyDecoder OnyonTail(Fixture.Package,Fixture.Body); Check(!OnyonTail.Document(Document));
    Check(OnyonTail.Error.Code=="unread-property-payload");
    Fixture.Body.pop_back(); Fixture.Package.Exports[0].Size--;
    P4Patch(Fixture.Body,Fixture.Body.size()-4,1); FPropertyDecoder Guid(Fixture.Package,Fixture.Body); Check(!Guid.Document(Document));
    Fixture.Package.Imports[1].Name="Unqualified"; FPropertyDecoder Unqualified(Fixture.Package,Fixture.Body); Check(!Unqualified.Document(Document));
    Fixture.Package.Imports[1].Name="DataTable"; Fixture.Package.Imports[0].Name="/Script/Engine";
    FPropertyDecoder Settings(Fixture.Package,Fixture.Body); P4Patch(Fixture.Body,Fixture.Body.size()-4,0); Check(!Settings.Document(Document));
    FDecodedValue Unicode = FDecodedValue::Atom(FDecodedValue::EKind::String,"\xc3\xb1"); Check(DecodedJson(Unicode)=="\"\xc3\xb1\"");
    {
        // What the writer wrote is read back to the same value, and written again to the same text.
        for (const char* Json : {"{\"A\":[1,-2.5,18446744073709551615,true,false,null,\"x\\\"y\\\\z\\u0009\"],\"B\":{},\"C\":[]}", "[]", "\"\"", "-0.0",
                                 "{\"__native_map_pairs\":[[7,{\"Value\":42}]]}", "[[[]]]", "1e+20", "nan"})
        {
            FDecodedValue Read;
            Check(ReadDecodedJson(Json, Read)); Check(DecodedJson(Read) == Json);
        }
        FDecodedValue Read;
        Check(ReadDecodedJson("{\"A\":18446744073709551615}", Read)); Check(Read.Fields.size() == 1); Check(Read.Fields[0].first == "A");
        Check(Read.Fields[0].second.Kind == FDecodedValue::EKind::Number); Check(Read.Fields[0].second.Text == "18446744073709551615");
        Check(ReadDecodedJson("\"a\\u0009b\"", Read)); Check(Read.Text == "a\tb");
        // Anything the writer does not write is refused: spaces, a cut text, other escapes, text after the end.
        for (const char* Bad : {"", " 1", "[1, 2]", "{\"A\":1", "[1,", "\"a", "\"\\n\"", "\"\\u0041\"", "{A:1}", "[1]x", "{\"A\" :1}", "tru", "[,]", "{\"A\":}", "1 "})
            Check(!ReadDecodedJson(Bad, Read));
    }
}

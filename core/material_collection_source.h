// Exact source values for a stock native material parameter collection. No
// acquisition, defaults, native object construction or renderer authority.
#pragma once
#include "material_source_data.h"
#include "custom_material_hash.h"
namespace P4MapKit
{
namespace Material
{
struct FMaterialCollectionParameterSource
{
    std::string Name, NameBase;
    std::int32_t NameNumber = 0;
    std::array<std::uint32_t, 4> Id{{}};
    std::array<std::uint32_t, 4> Value{{}};
};
struct FMaterialCollectionSource
{
    std::string Package, Object;
    std::array<std::uint32_t, 4> State{{}};
    std::vector<FMaterialCollectionParameterSource> Scalars, Vectors;
};
namespace CollectionSourceDetail
{
inline void Need(bool Good, const char* Why)
{
    if (!Good)
        throw Refused(Why, "material collection source");
}
inline void Fields(const FDecodedValue& V, std::initializer_list<const char*> Names)
{
    Need(V.Kind == FDecodedValue::EKind::Object && V.Fields.size() == Names.size(),
         "collection source has unsupported or missing fields");
    for (const auto* Name : Names)
        Need(V.Field(Name) != nullptr, "collection source field absent");
}
inline void Tag(const FPropertyEnvelope& P, const char* Name, const char* Type, const char* Detail = "")
{
    Need(P.Name == Name && P.Type == Type && P.ArrayIndex == 0, "collection source field native type/index differs");
    Need(*Detail ? (P.Detail.size() == 1 && P.Detail[0] == Detail) : P.Detail.empty(),
         "collection source native struct/element type differs");
}
inline const FPropertyEnvelope& Field(const FPropertyInventory& V, const char* Name)
{
    for (const auto& P : V.Properties)
        if (P.Name == Name)
            return P;
    throw Refused("collection source schema field absent", "material collection source");
}
inline void ParameterShape(const FPropertyInventory& V, bool Vector)
{
    Need(V.Error.Code.empty() && V.Properties.size() == 3, "collection source parameter schema unreadable");
    Tag(Field(V, "ParameterName"), "ParameterName", "NameProperty");
    Tag(Field(V, "Id"), "Id", "StructProperty", "Guid");
    Tag(Field(V, "DefaultValue"), "DefaultValue", Vector ? "StructProperty" : "FloatProperty",
        Vector ? "LinearColor" : "");
}
inline std::array<std::uint32_t, 4> Guid(const FDecodedValue& V)
{
    Fields(V, {"A", "B", "C", "D"});
    std::array<std::uint32_t, 4> Out{{}};
    for (std::size_t I = 0; I < 4; ++I)
    {
        const auto* W = V.Field(std::string(1, char('A' + I)));
        Need(W->Kind == FDecodedValue::EKind::Number, "collection GUID word is not numeric");
        std::int64_t N = 0;
        std::istringstream In(W->Text);
        In >> N;
        Need(!In.fail() && In.eof() && N >= INT32_MIN && N <= std::int64_t(UINT32_MAX),
             "collection GUID word is invalid");
        Out[I] = std::uint32_t(N);
    }
    Need(Out[0] || Out[1] || Out[2] || Out[3], "collection source GUID is invalid");
    return Out;
}
inline std::uint32_t Float(const FDecodedValue& V, bool NativeColor = false)
{
    float N = 0;
    Need(V.Kind == FDecodedValue::EKind::Number && (NativeColor ? V.Type.empty() : V.Type == "FloatProperty") &&
             MaterialNumber(V, N),
         "collection source default is not a finite float32");
    return MaterialFloatBits(N);
}
inline FMaterialCollectionSource Project(const FDecodedValue& Props, const std::string& Package)
{
    Fields(Props, {"StateId", "ScalarParameters", "VectorParameters"});
    FMaterialCollectionSource Out;
    Out.Package = Package;
    Out.Object = Package + '.' + Package.substr(Package.rfind('/') + 1);
    Out.State = Guid(*Props.Field("StateId"));
    std::set<std::string> Names, Ids;
    const auto Read = [&](const char* Field, bool Vector, std::vector<FMaterialCollectionParameterSource>& Rows)
    {
        const auto& List = *Props.Field(Field);
        Need(List.Kind == FDecodedValue::EKind::Array && List.Items.size() <= 4096,
             "collection source parameter list is invalid");
        for (const auto& Row : List.Items)
        {
            Fields(Row, {"ParameterName", "Id", "DefaultValue"});
            FMaterialCollectionParameterSource P;
            const auto& Name = *Row.Field("ParameterName");
            Need(Name.Kind == FDecodedValue::EKind::Name && !Name.Text.empty() && Name.Text != "None" &&
                     JsonDetail::ValidUtf8(Name.Text),
                 "collection source parameter name is invalid");
            Need(!Name.NameBase.empty() && Name.NameNumber >= 0,
                 "collection source parameter FName identity is absent");
            P.Name = Name.Text;
            P.NameBase = Name.NameBase;
            P.NameNumber = Name.NameNumber;
            Need(Names.insert(PropertyIdentity(P.Name)).second, "collection source parameter name is ambiguous");
            P.Id = Guid(*Row.Field("Id"));
            std::string Key;
            for (const auto W : P.Id)
                Key += std::to_string(W) + ":";
            Need(Ids.insert(Key).second, "collection source parameter GUID is ambiguous");
            // The decoder's native LinearColor layout emits untagged float32
            // R/G/B/A children. Tagged user structs cannot impersonate it.
            const auto& Value = *Row.Field("DefaultValue");
            if (Vector)
            {
                Fields(Value, {"R", "G", "B", "A"});
                const char* C[] = {"R", "G", "B", "A"};
                for (std::size_t I = 0; I < 4; ++I)
                    P.Value[I] = Float(*Value.Field(C[I]), true);
            }
            else
                P.Value[0] = Float(Value);
            Rows.push_back(P);
        }
    };
    Read("ScalarParameters", false, Out.Scalars);
    Read("VectorParameters", true, Out.Vectors);
    return Out;
}
}
inline FMaterialCollectionSource ReadMaterialCollectionSource(const std::vector<std::uint8_t>& Header,
                                                              const std::vector<std::uint8_t>& Body,
                                                              const std::string& Expected)
{
    using namespace CollectionSourceDetail;
    Need(!Header.empty() && Header.size() <= 64u * 1024u * 1024u && !Body.empty() && Body.size() <= 64u * 1024u * 1024u,
         "collection source bounds");
    const auto Package = ReadPackage(Header, Body);
    Need(Package.Error.Code.empty() && Package.Package == Expected, "collection source canonical package mismatch");
    Need(Package.Exports.size() == 1, "collection source has additional objects");
    const auto& E = Package.Exports[0];
    std::string Class;
    Need(E.Outer == 0 && E.Name == Expected.substr(Expected.rfind('/') + 1) && Package.Reference(E.Class, Class) &&
             Class == "/Script/Engine.MaterialParameterCollection",
         "collection source canonical native root mismatch");
    const auto Tags = ReadPropertyEnvelopes(Body, Package.Names, E.Offset, E.Offset + E.Size);
    Need(Tags.Error.Code.empty() && Tags.Properties.size() == 3, "collection source root schema unsupported");
    Tag(Field(Tags, "StateId"), "StateId", "StructProperty", "Guid");
    for (const bool Vector : {false, true})
    {
        const char* Name = Vector ? "VectorParameters" : "ScalarParameters";
        const auto& Array = Field(Tags, Name);
        Tag(Array, Name, "ArrayProperty", "StructProperty");
        FByteReader R(Body, Array.PayloadOffset, Array.PayloadOffset + Array.Size);
        std::size_t Count = 0;
        FPropertyEnvelope Inner;
        Need(R.Count(Count, 4096) && ReadPropertyEnvelope(R, Package.Names, Body.size(), Inner) &&
                 R.Tell() == R.Limit(),
             "collection source native array descriptor unreadable");
        Tag(Inner, Name, "StructProperty", Vector ? "CollectionVectorParameter" : "CollectionScalarParameter");
        auto Position = Inner.PayloadOffset;
        for (std::size_t I = 0; I < Count; ++I)
        {
            const auto Row = ReadPropertyEnvelopes(Body, Package.Names, Position, Inner.PayloadOffset + Inner.Size);
            ParameterShape(Row, Vector);
            Position = Row.End;
        }
        Need(Position == Inner.PayloadOffset + Inner.Size, "collection source native array payload has unread bytes");
    }
    FPropertyDecoder Decoder(Package, Body);
    FDecodedValue Props;
    std::size_t At = E.Offset;
    Need(Decoder.TypedProperties(At, E.Offset + E.Size, Props), "collection source properties unreadable");
    // UObject's absent object GUID is the only supported serialized suffix.
    Need(At <= Body.size() && E.Offset + E.Size >= At && E.Offset + E.Size - At == 4 && Body[At] == 0 &&
             Body[At + 1] == 0 && Body[At + 2] == 0 && Body[At + 3] == 0,
         "collection source native suffix unsupported");
    Need(E.Offset + E.Size <= Body.size() && Body.size() - (E.Offset + E.Size) == 4 && Body[Body.size() - 4] == 0xc1 &&
             Body[Body.size() - 3] == 0x83 && Body[Body.size() - 2] == 0x2a && Body.back() == 0x9e,
         "collection source extra body data or footer unsupported");
    return Project(Props, Expected);
}
inline std::string MaterialCollectionStateHex(const std::array<std::uint32_t, 4>& State)
{
    CustomMaterial::Bytes B;
    for (auto W : State)
        for (unsigned I = 0; I < 4; ++I)
            B.push_back(std::uint8_t(W >> (8 * I)));
    return CustomMaterial::Hex(B);
}
}
}

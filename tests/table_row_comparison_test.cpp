#include "table_row_comparison.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using V = P4MapKit::FDecodedValue;
namespace C = P4MapKit::TableRowComparison;
static unsigned Checks = 0;
static void Check(bool Passed, const char* Why)
{
    ++Checks;
    if (!Passed) throw std::runtime_error(Why);
}

// Construct an archive independently of the comparison helper. Both decoders
// must read the same serialized row, just as the native importer requires.
struct FArchive
{
    P4MapKit::FPackageInventory Package;
    std::vector<std::uint8_t> Bytes;
    static void Put(std::vector<std::uint8_t>& Into, std::uint32_t Word)
    { for (unsigned Byte = 0; Byte < 4; ++Byte) Into.push_back(std::uint8_t(Word >> (Byte * 8))); }
    void Name(std::vector<std::uint8_t>& Into, const std::string& Said)
    {
        auto Found = std::find(Package.Names.begin(), Package.Names.end(), Said);
        if (Found == Package.Names.end()) { Package.Names.push_back(Said); Found = Package.Names.end() - 1; }
        Put(Into, std::uint32_t(Found - Package.Names.begin())); Put(Into, 0);
    }
    static void String(std::vector<std::uint8_t>& Into, const std::string& Said)
    { Put(Into, std::uint32_t(Said.size() + 1)); Into.insert(Into.end(), Said.begin(), Said.end()); Into.push_back(0); }
    void Tag(const std::string& Field, const std::string& Type, const std::vector<std::uint8_t>& Payload,
        const std::string& Inner = std::string())
    {
        Name(Bytes, Field); Name(Bytes, Type); Put(Bytes, std::uint32_t(Payload.size())); Put(Bytes, 0);
        if (!Inner.empty()) Name(Bytes, Inner);
        Bytes.push_back(0); Bytes.insert(Bytes.end(), Payload.begin(), Payload.end());
    }
    std::vector<std::uint8_t> Soft(const std::string& Path)
    { std::vector<std::uint8_t> Out; Name(Out, Path); String(Out, ""); return Out; }
    explicit FArchive(const std::string& Path)
    {
        Package.Package = "/Game/TableComparison";
        Put(Bytes, 1); Name(Bytes, "Row");
        Tag("Soft", "SoftObjectProperty", Soft(Path));
        std::vector<std::uint8_t> Plain; String(Plain, Path); Tag("Literal", "StrProperty", Plain);
        std::vector<std::uint8_t> Text; Put(Text, 0); Text.push_back(0);
        String(Text, Path + "/Namespace"); String(Text, Path + "/Key"); String(Text, Path + "/Source");
        Tag("Localized", "TextProperty", Text);
        std::vector<std::uint8_t> Softs; Put(Softs, 2);
        for (unsigned At = 0; At < 2; ++At) { auto Item = Soft(Path); Softs.insert(Softs.end(), Item.begin(), Item.end()); }
        Tag("Softs", "ArrayProperty", Softs, "SoftObjectProperty");
        std::vector<std::uint8_t> Strings; Put(Strings, 2); String(Strings, Path); String(Strings, Path);
        Tag("Literals", "ArrayProperty", Strings, "StrProperty");
        Name(Bytes, "None");
    }
    std::pair<V, V> Read() const
    {
        std::size_t LegacyAt = 0, TypedAt = 0;
        std::vector<std::pair<std::string, V>> LegacyRows, TypedRows;
        P4MapKit::FPropertyDecoder Legacy(Package, Bytes), Typed(Package, Bytes);
        Check(Legacy.TableRows(LegacyAt, Bytes.size(), "TestRow", LegacyRows) && LegacyAt == Bytes.size(), "legacy row decode failed");
        Check(Typed.TypedTableRows(TypedAt, Bytes.size(), "TestRow", TypedRows) && TypedAt == Bytes.size(), "typed row decode failed");
        Check(LegacyRows.size() == 1 && TypedRows.size() == 1 && LegacyRows[0].first == TypedRows[0].first, "row names differ");
        Check(P4MapKit::DecodedJson(LegacyRows[0].second) == P4MapKit::DecodedJson(TypedRows[0].second), "typed row does not project to exact legacy input");
        return {LegacyRows[0].second, TypedRows[0].second};
    }
};

static V Soft(const std::string& Asset, const std::string& Sub = std::string())
{
    V Out = V::Atom(V::EKind::SoftPath, Asset + (Sub.empty() ? "" : ":" + Sub));
    Out.AssetPath = Asset; Out.SubPath = Sub; return Out;
}
static std::string Compare(const V& Typed, bool Fold = true)
{
    const V Legacy = P4MapKit::LegacyDecodedValue(Typed);
    return P4MapKit::DecodedJson(C::Value(Legacy, &Typed, Fold));
}
static V Hard(const std::string& Path, std::int32_t Index = -1)
{
    V Out = V::Atom(V::EKind::HardReference, Path);
    Out.ReferenceIndex = Index; Out.Type = "ClassProperty"; return Out;
}

int main()
try
{
    const V StoryWidget = Hard("/Game/UI/WBP_ui_Caveinfo_Entrance.WBP_ui_CaveInfo_Entrance_C");
    const std::string StoryBefore = P4MapKit::DecodedJson(StoryWidget);
    Check(Compare(StoryWidget) == Compare(Hard("/Game/UI/WBP_ui_CaveInfo_Entrance.WBP_ui_Caveinfo_Entrance_C", -9)),
        "hard class reference FName spelling poisoned the deferred widget table");
    Check(Compare(StoryWidget, false) != Compare(Hard("/game/ui/wbp_ui_caveinfo_entrance.wbp_ui_caveinfo_entrance_c"), false),
        "unfolded source spelling was lost");
    Check(Compare(StoryWidget) != Compare(Hard("/Game/UI/WBP_Other.WBP_Other_C")), "different hard class reference was hidden");
    Check(Compare(Hard("", 0)) == "null", "null hard reference changed");
    Check(Compare(V::Atom(V::EKind::String, StoryWidget.Text)) != Compare(V::Atom(V::EKind::String, "/game/ui/wbp_ui_caveinfo_entrance.wbp_ui_caveinfo_entrance_c")),
        "a literal class-looking string was case folded");
    Check(P4MapKit::DecodedJson(StoryWidget) == StoryBefore, "hard reference comparison changed source bytes");
    const auto Upper = FArchive("/Game/Widget.WBP_CaveInfo_C").Read();
    const auto Lower = FArchive("/game/widget.wbp_caveinfo_c").Read();
    const std::string LegacyBefore = P4MapKit::DecodedJson(Upper.first), TypedBefore = P4MapKit::DecodedJson(Upper.second);
    for (const char* Field : {"Soft", "Softs"})
    {
        const V* A = Upper.second.Field(Field), *B = Lower.second.Field(Field);
        Check(A && B && Compare(*A) == Compare(*B), "soft reference case did not compare equal");
        Check(Compare(*A, false) != Compare(*B, false), "unfolded diagnostic lost source spelling");
    }
    for (const char* Field : {"Literal", "Localized", "Literals"})
    {
        const V* A = Upper.second.Field(Field), *B = Lower.second.Field(Field);
        Check(A && B && Compare(*A) != Compare(*B), "literal string or text case edit was hidden");
    }
    Check(Upper.second.Field("Soft")->Kind == V::EKind::SoftPath && Upper.first.Field("Soft")->Kind == V::EKind::String,
        "raw decoder did not distinguish soft reference from string");
    Check(Upper.second.Field("Localized")->Kind == V::EKind::NativeText, "raw decoder did not retain native text identity");
    Check(P4MapKit::DecodedJson(C::Value(Upper.first, nullptr, true)) == LegacyBefore, "missing typed witness changed legacy value");
    V Different = Upper.second; Different.Fields.front().second = Soft("/Game/Other.Other_C");
    Check(P4MapKit::DecodedJson(C::Value(Upper.first, &Different, true)) == LegacyBefore, "mismatched typed witness changed legacy value");
    V Broken = Upper.second; Broken.Fields.front().second.AssetPath += "_Wrong";
    Check(P4MapKit::DecodedJson(C::Value(Upper.first, &Broken, true)) == LegacyBefore, "malformed soft identity changed legacy value");

    // Recursive containers must normalize only their typed reference leaves.
    V Nested; Nested.Kind = V::EKind::Array;
    V Pair; Pair.Kind = V::EKind::Array;
    Pair.Items = {Soft("/Game/Map.Map_C"), V::Atom(V::EKind::String, "/Game/Literal.CaseSensitive")};
    V Struct; Struct.Fields.emplace_back("Pair", Pair); Nested.Items.push_back(Struct);
    V Same = Nested; Same.Items[0].Fields[0].second.Items[0] = Soft("/game/map.map_c");
    Check(Compare(Nested) == Compare(Same), "nested array reference case did not compare equal");
    Same.Items[0].Fields[0].second.Items[1].Text = "/game/literal.casesensitive";
    Check(Compare(Nested) != Compare(Same), "nested array literal case edit was hidden");
    Check(Compare(Soft("/Game/Map.Map_C", "Component/ Game")) != Compare(Soft("/game/map.map_c", "component/ Game")),
        "soft subobject path case edit was hidden");
    Check(Compare(Soft("/Game/Map.Map_C", "/Game/Exact.Sub")) == Compare(Soft("/game/map.map_c", "/Game/Exact.Sub")),
        "equal exact subobject paths did not compare equal");
    Check(Compare(Soft("/Game/Map.Map_C", "/Game/Exact.Sub")).find("/Game/Exact.Sub") != std::string::npos,
        "asset projection folded a /Game/ substring in a subobject string");
    Check(Compare(Soft("/Game/Map.Map_C")) != Compare(Soft("/game/map.other_c")), "different soft reference was hidden");
    Check(P4MapKit::DecodedJson(Upper.first) == LegacyBefore && P4MapKit::DecodedJson(Upper.second) == TypedBefore,
        "comparison mutated an importer input");
    std::cout << Checks << " table row comparison controls passed\n";
    return 0;
}
catch (const std::exception& Error) { std::cerr << Error.what() << '\n'; return 1; }

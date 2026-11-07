// Exact-token JSON for the portable Blueprint boundary. No Unreal or Python.
// Material JSON supplies the shared UTF-8/quoting primitives, not its double
// number storage: Int64/UInt64 bytecode operands must retain every bit.
#pragma once
#include "material_json.h"
#include "quality.h"
#include <functional>
#include <fstream>
#include <algorithm>
#include <set>

namespace P4MapKit { namespace Blueprint {
struct Error : std::runtime_error { explicit Error(const std::string& Message) : std::runtime_error(Message) {} };
struct J
{
    enum class K { Null, Boolean, Number, String, Array, Object };
    K Kind = K::Null;
    bool Bit = false;
    std::string Text;
    std::vector<J> Items;
    std::map<std::string, J> Fields;
    // Canonical output is sorted; extraction of comparison-only records keeps
    // the original construction order of dictionaries, as the B3 boundary does.
    std::vector<std::string> Order;
    J() = default;
    J(bool Value) : Kind(K::Boolean), Bit(Value) {}
    J(int Value) : Kind(K::Number), Text(std::to_string(Value)) {}
    J(std::int64_t Value) : Kind(K::Number), Text(std::to_string(Value)) {}
    J(std::uint64_t Value) : Kind(K::Number), Text(std::to_string(Value)) {}
    J(const char* Value) : Kind(K::String), Text(Value) {}
    J(const std::string& Value) : Kind(K::String), Text(Value) {}
    static J Number(const std::string& Token) { J Value; Value.Kind = K::Number; Value.Text = Token; return Value; }
    static J Array() { J Value; Value.Kind = K::Array; return Value; }
    static J Object() { J Value; Value.Kind = K::Object; return Value; }
    static J Object(std::initializer_list<std::pair<const std::string, J>> Values)
    { J Value = Object(); for (const auto& Field : Values) { Value.Order.push_back(Field.first); Value.Fields.emplace(Field); } return Value; }
    static J Array(std::initializer_list<J> Values) { J Value = Array(); Value.Items = Values; return Value; }
    bool Has(const std::string& Key) const { return Kind == K::Object && Fields.count(Key); }
    const J& Get(const std::string& Key) const
    {
        if (Kind != K::Object) throw Error("json-object-required");
        const auto Found = Fields.find(Key);
        if (Found == Fields.end()) throw Error("json-missing-key: " + Key);
        return Found->second;
    }
    J& operator[](const std::string& Key)
    { if (Kind == K::Null) Kind = K::Object; if (Kind != K::Object) throw Error("json-object-required"); if (!Has(Key)) Order.push_back(Key); return Fields[Key]; }
    std::vector<std::string> Keys() const
    { if (Order.size() == Fields.size()) return Order; auto Out = Order; const std::set<std::string> Seen(Order.begin(), Order.end()); for (const auto& Field : Fields) if (!Seen.count(Field.first)) Out.push_back(Field.first); return Out; }
    const J& operator[](const std::string& Key) const { return Get(Key); }
    const J& operator[](std::size_t Index) const
    { if (Kind != K::Array || Index >= Items.size()) throw Error("json-array-index"); return Items[Index]; }
    std::string S() const { if (Kind != K::String) throw Error("json-string-required"); return Text; }
    std::uint64_t U() const
    { std::uint64_t Value = 0; if (Kind != K::Number || !ParseInteger(Text, true, Value)) throw Error("json-uint64-required"); return Value; }
    std::int64_t I() const
    {
        std::uint64_t Value = 0;
        if (Kind != K::Number || !ParseInteger(Text, false, Value)) throw Error("json-int64-required");
        return Value <= std::uint64_t(INT64_MAX) ? std::int64_t(Value) : -1 - std::int64_t(UINT64_MAX - Value);
    }
    double D() const
    {
        if (Kind != K::Number) throw Error("json-number-required");
        std::istringstream In(Text); In.imbue(std::locale::classic()); double Value = 0; In >> Value;
        if (!In || !std::isfinite(Value)) throw Error("json-finite-number-required"); return Value;
    }
    bool B() const { if (Kind != K::Boolean) throw Error("json-bool-required"); return Bit; }
    bool Truth() const
    { return Kind == K::Null ? false : Kind == K::Boolean ? Bit : Kind == K::Array ? !Items.empty() :
        Kind == K::Object ? !Fields.empty() : Kind == K::String ? !Text.empty() : D() != 0; }
    std::string Dump() const
    {
        if (Kind == K::Null) return "null";
        if (Kind == K::Boolean) return Bit ? "true" : "false";
        if (Kind == K::String) return Material::JsonDetail::Quote(Text);
        if (Kind == K::Number) return Text;
        std::string Out = Kind == K::Array ? "[" : "{";
        if (Kind == K::Array) for (const auto& Item : Items)
        { if (Out.size() > 1) Out += ','; Out += Item.Dump(); }
        else for (const auto& Field : Fields)
        { if (Out.size() > 1) Out += ','; Out += Material::JsonDetail::Quote(Field.first) + ':' + Field.second.Dump(); }
        return Out + (Kind == K::Array ? "]" : "}");
    }
    static J Parse(const std::string& Text, std::size_t MaxBytes = 64u * 1024u * 1024u,
        std::size_t MaxNodes = 1000000, std::size_t MaxDepth = 128);
};
class JsonReader
{
    const std::string& Source;
    std::size_t At = 0, Nodes = 0;
    const std::size_t NodeLimit, DepthLimit;
    void Fail(const std::string& Why) const { throw Error(Why + " at byte " + std::to_string(At)); }
    void Space() { while (At < Source.size() && (Source[At] == ' ' || Source[At] == '\t' || Source[At] == '\n' || Source[At] == '\r')) ++At; }
    bool Take(char Char) { Space(); if (At < Source.size() && Source[At] == Char) { ++At; return true; } return false; }
    std::uint32_t Hex()
    {
        std::uint32_t Out = 0;
        for (int Count = 0; Count < 4; ++Count)
        {
            if (At == Source.size()) Fail("json-truncated-unicode"); const char Char = Source[At++];
            const int Digit = Char >= '0' && Char <= '9' ? Char - '0' : Char >= 'a' && Char <= 'f' ? Char - 'a' + 10 : Char >= 'A' && Char <= 'F' ? Char - 'A' + 10 : -1;
            if (Digit < 0) Fail("json-invalid-unicode"); Out = Out * 16 + std::uint32_t(Digit);
        }
        return Out;
    }
    std::string String()
    {
        if (!Take('"')) Fail("json-string-required"); std::string Out;
        while (At < Source.size())
        {
            const auto Char = std::uint8_t(Source[At++]);
            if (Char == '"') { if (!Material::JsonDetail::ValidUtf8(Out)) Fail("json-invalid-utf8"); return Out; }
            if (Char < 32) Fail("json-control-character");
            if (Char != '\\') { Out.push_back(char(Char)); continue; }
            if (At == Source.size()) Fail("json-truncated-escape"); const char Escape = Source[At++];
            if (Escape == '"' || Escape == '\\' || Escape == '/') Out.push_back(Escape);
            else if (Escape == 'b') Out.push_back('\b'); else if (Escape == 'f') Out.push_back('\f');
            else if (Escape == 'n') Out.push_back('\n'); else if (Escape == 'r') Out.push_back('\r'); else if (Escape == 't') Out.push_back('\t');
            else if (Escape == 'u')
            {
                auto Code = Hex();
                if (Code >= 0xd800 && Code <= 0xdbff)
                {
                    if (Source.size() - At < 2 || Source[At] != '\\' || Source[At + 1] != 'u') Fail("json-unpaired-surrogate"); At += 2;
                    const auto Low = Hex(); if (Low < 0xdc00 || Low > 0xdfff) Fail("json-unpaired-surrogate");
                    Code = 0x10000 + ((Code - 0xd800) << 10) + Low - 0xdc00;
                }
                else if (Code >= 0xdc00 && Code <= 0xdfff) Fail("json-unpaired-surrogate");
                Material::JsonDetail::Utf8(Out, Code);
            }
            else Fail("json-invalid-escape");
        }
        Fail("json-unterminated-string"); return Out;
    }
    J Value(std::size_t Depth)
    {
        if (Depth > DepthLimit || ++Nodes > NodeLimit) Fail("json-budget"); Space();
        if (At == Source.size()) Fail("json-truncated");
        if (Source[At] == '"') return J(String());
        if (Take('{'))
        {
            J Out = J::Object(); if (Take('}')) return Out;
            do { const auto Key = String(); if (!Take(':')) Fail("json-colon-required"); auto Item = Value(Depth + 1);
                if (!Out.Fields.emplace(Key, std::move(Item)).second) Fail("json-duplicate-key"); Out.Order.push_back(Key); if (Take('}')) return Out; } while (Take(','));
            Fail("json-object-end-required");
        }
        if (Take('['))
        {
            J Out = J::Array(); if (Take(']')) return Out;
            do { Out.Items.push_back(Value(Depth + 1)); if (Take(']')) return Out; } while (Take(',')); Fail("json-array-end-required");
        }
        for (const auto& Word : std::vector<std::pair<std::string, J>>{{"null", J()}, {"true", J(true)}, {"false", J(false)}})
            if (Source.compare(At, Word.first.size(), Word.first) == 0) { At += Word.first.size(); return Word.second; }
        const auto Start = At; if (Source[At] == '-') ++At; if (At == Source.size()) Fail("json-number-required");
        if (Source[At] == '0') ++At;
        else if (Source[At] >= '1' && Source[At] <= '9') while (At < Source.size() && Source[At] >= '0' && Source[At] <= '9') ++At;
        else Fail("json-number-required");
        if (At < Source.size() && Source[At] == '.')
        { ++At; const auto First = At; while (At < Source.size() && Source[At] >= '0' && Source[At] <= '9') ++At; if (First == At) Fail("json-fraction-required"); }
        if (At < Source.size() && (Source[At] == 'e' || Source[At] == 'E'))
        { ++At; if (At < Source.size() && (Source[At] == '-' || Source[At] == '+')) ++At; const auto First = At;
            while (At < Source.size() && Source[At] >= '0' && Source[At] <= '9') ++At; if (First == At) Fail("json-exponent-required"); }
        return J::Number(Source.substr(Start, At - Start));
    }
public:
    JsonReader(const std::string& Text, std::size_t MaxNodes, std::size_t MaxDepth) : Source(Text), NodeLimit(MaxNodes), DepthLimit(MaxDepth) {}
    J Read() { J Out = Value(0); Space(); if (At != Source.size()) Fail("json-trailing-input"); return Out; }
};
inline J J::Parse(const std::string& Text, std::size_t MaxBytes, std::size_t MaxNodes, std::size_t MaxDepth)
{ if (Text.size() > MaxBytes) throw Error("json-byte-limit"); return JsonReader(Text, MaxNodes, MaxDepth).Read(); }
inline std::vector<std::uint8_t> ReadBytes(const std::string& Path, std::size_t Limit = 64u * 1024u * 1024u)
{
#ifdef _WIN32
    if (!Material::JsonDetail::ValidUtf8(Path)) throw Error("invalid-source-path-utf8");
    std::wstring Wide;
    for (std::size_t At = 0; At < Path.size();)
    {
        const auto First = std::uint8_t(Path[At++]); std::uint32_t Code = First;
        const int More = First < 0x80 ? 0 : First < 0xe0 ? 1 : First < 0xf0 ? 2 : 3;
        if (More) { Code &= (1u << (6 - More)) - 1; for (int N = 0; N < More; ++N) Code = (Code << 6) | (std::uint8_t(Path[At++]) & 0x3f); }
        if (Code <= 0xffff) Wide += wchar_t(Code);
        else { Code -= 0x10000; Wide += wchar_t(0xd800 + (Code >> 10)); Wide += wchar_t(0xdc00 + (Code & 0x3ff)); }
    }
    std::replace(Wide.begin(), Wide.end(), L'/', L'\\');
    if (Wide.size() >= 3 && Wide[1] == L':' && Wide[2] == L'\\') Wide = L"\\\\?\\" + Wide;
    else if (Wide.size() >= 2 && Wide[0] == L'\\' && Wide[1] == L'\\' && (Wide.size() < 4 || Wide[2] != L'?')) Wide = L"\\\\?\\UNC\\" + Wide.substr(2);
    std::ifstream Input(Wide.c_str(), std::ios::binary | std::ios::ate); if (!Input) throw Error("unreadable-source-file");
#else
    std::ifstream Input(Path, std::ios::binary | std::ios::ate); if (!Input) throw Error("unreadable-source-file");
#endif
    const auto Size = Input.tellg(); if (Size < 0 || std::uint64_t(Size) > Limit) throw Error("source-file-bound");
    std::vector<std::uint8_t> Out((std::size_t(Size))); Input.seekg(0);
    if (!Out.empty()) Input.read(reinterpret_cast<char*>(Out.data()), std::streamsize(Out.size()));
    if (!Input) throw Error("source-file-short-read"); return Out;
}
inline J ReadJson(const std::string& Path)
{ const auto Bytes = ReadBytes(Path); return J::Parse(std::string(Bytes.begin(), Bytes.end())); }
}} // namespace P4MapKit::Blueprint

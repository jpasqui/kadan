// Portable function/field lookup for recovered Blueprint graphs.
#pragma once
#include "blueprint_json.h"
#include "blueprint_signature_unicode.h"

namespace P4MapKit
{
namespace Blueprint
{
namespace SignatureUnicode
{
inline std::string Lower(const std::string& Text)
{
    const auto& Maps = LowerMaps();
    const auto& Cased = CasedRanges();
    const auto& Ignored = IgnoredRanges();
    auto In = [](std::uint32_t Value, const std::uint32_t (*Ranges)[2], std::size_t Count)
    {
        std::size_t Lo = 0, Hi = Count;
        while (Lo < Hi)
        {
            const auto Mid = Lo + (Hi - Lo) / 2;
            if (Ranges[Mid][1] < Value)
                Lo = Mid + 1;
            else
                Hi = Mid;
        }
        return Lo < Count && Ranges[Lo][0] <= Value;
    };
    if (!Material::JsonDetail::ValidUtf8(Text))
        throw Error("signature-invalid-utf8");
    std::vector<std::uint32_t> Characters;
    for (std::size_t At = 0; At < Text.size();)
    {
        auto Value = std::uint32_t(static_cast<unsigned char>(Text[At++]));
        unsigned More = 0;
        if (Value >= 0xf0)
        {
            Value &= 7;
            More = 3;
        }
        else if (Value >= 0xe0)
        {
            Value &= 15;
            More = 2;
        }
        else if (Value >= 0xc0)
        {
            Value &= 31;
            More = 1;
        }
        while (More--)
            Value = (Value << 6) | (static_cast<unsigned char>(Text[At++]) & 63);
        Characters.push_back(Value);
    }
    std::string Out;
    for (std::size_t At = 0; At < Characters.size(); ++At)
    {
        const auto Value = Characters[At];
        if (Value == 0x3a3)
        {
            bool Before = false, After = false;
            for (auto Pos = At; Pos > 0;)
            {
                const auto C = Characters[--Pos];
                if (In(C, Ignored, sizeof(Ignored) / sizeof(*Ignored)))
                    continue;
                Before = In(C, Cased, sizeof(Cased) / sizeof(*Cased));
                break;
            }
            for (auto Pos = At + 1; Pos < Characters.size(); ++Pos)
            {
                const auto C = Characters[Pos];
                if (In(C, Ignored, sizeof(Ignored) / sizeof(*Ignored)))
                    continue;
                After = In(C, Cased, sizeof(Cased) / sizeof(*Cased));
                break;
            }
            Material::JsonDetail::Utf8(Out, Before && !After ? 0x3c2 : 0x3c3);
            continue;
        }
        const auto Found = std::lower_bound(std::begin(Maps), std::end(Maps), Value,
                                            [](const Mapping& Map, std::uint32_t Code) { return Map.From < Code; });
        if (Found == std::end(Maps) || Found->From != Value)
            Material::JsonDetail::Utf8(Out, Value);
        else
        {
            Material::JsonDetail::Utf8(Out, Found->First);
            if (Found->Second)
                Material::JsonDetail::Utf8(Out, Found->Second);
        }
    }
    return Out;
}
}
class GraphSignatures
{
public:
    using Documents = std::map<std::string, J>;
    using Sources = std::map<std::string, std::string>;
    using Loader = std::function<J(const std::string&)>;
    using Folder = std::function<std::string(const std::string&)>;
    std::map<std::string, J> Native, Parents, Fields, Types, Enums;
    std::set<std::string> Touched;

private:
    Sources Source;
    Documents Docs;
    std::map<std::string, std::string> Folded;
    std::map<std::string, std::map<std::string, std::string>> ByOwner;
    Loader Load;
    Folder Fold;
    static bool Starts(const std::string& Text, const std::string& Prefix)
    {
        return Text.compare(0, Prefix.size(), Prefix) == 0;
    }
    static std::string Package(const std::string& Path)
    {
        return Path.substr(0, Path.find('.'));
    }
    static std::pair<std::string, std::string> OwnerName(const std::string& Path)
    {
        const auto At = Path.rfind('.');
        if (At == std::string::npos)
            throw Error("function-path-without-owner");
        return {Path.substr(0, At), Path.substr(At + 1)};
    }
    static const J& Maybe(const J& Value, const std::string& Name)
    {
        static const J Null;
        return Value.Has(Name) ? Value[Name] : Null;
    }
    void Capture(const J& Reflection)
    {
        std::vector<std::string> NativeOrder;
        for (const auto& Function : Reflection["functions"].Items)
        {
            const auto Path = Function["path"].S();
            if (!Native.count(Path))
                NativeOrder.push_back(Path);
            Native[Path] = Function;
        }
        for (const auto& Structure : Reflection["structures"].Items)
        {
            const auto Path = Structure["path"].S();
            Parents[Path] = Maybe(Structure, "super_path");
            J Names = J::Array(), Detail = J::Object();
            for (const auto& Field : Structure["fields"].Items)
            {
                Names.Items.emplace_back(Field["name"]);
                Detail[Fold(Field["name"].S())] = Field["type_detail"];
            }
            Fields[Path] = Names;
            Types[Path] = Detail;
        }
        for (const auto& Enum : Reflection["enums"].Items)
        {
            J Values = J::Array();
            for (const auto& Value : Enum["values"].Items)
                Values.Items.emplace_back(Value["name"]);
            Enums[Enum["path"].S()] = Values;
        }
        for (const auto& Path : NativeOrder)
        {
            const auto Pair = OwnerName(Path);
            ByOwner[Pair.first][Fold(Pair.second)] = Path;
        }
    }

public:
    // Available for callers whose contract explicitly restricts names to ASCII.
    static std::string AsciiLower(const std::string& Text)
    {
        std::string Out = Text;
        for (auto& Character : Out)
        {
            const auto Byte = static_cast<unsigned char>(Character);
            if (Byte >= 128)
                throw Error("signature-non-ascii-case-fold");
            if (Character >= 'A' && Character <= 'Z')
                Character = static_cast<char>(Character + ('a' - 'A'));
        }
        return Out;
    }
    static std::uint64_t HexFlags(const J& Value)
    {
        auto Text = Value.S();
        std::uint64_t Out = 0;
        if (Text.compare(0, 2, "0x") == 0 || Text.compare(0, 2, "0X") == 0)
            Text = Text.substr(2);
        if (Text.empty())
            throw Error("signature-empty-flags");
        for (const auto Character : Text)
        {
            const int Digit = Character >= '0' && Character <= '9'   ? Character - '0'
                              : Character >= 'a' && Character <= 'f' ? Character - 'a' + 10
                              : Character >= 'A' && Character <= 'F' ? Character - 'A' + 10
                                                                     : -1;
            if (Digit < 0 || Out > (UINT64_MAX - static_cast<unsigned>(Digit)) / 16)
                throw Error("signature-invalid-flags");
            Out = Out * 16 + static_cast<unsigned>(Digit);
        }
        return Out;
    }
    explicit GraphSignatures(const J& Reflection, const Documents& DocumentsIn = {},
                             Folder FolderIn = SignatureUnicode::Lower)
        : Docs(DocumentsIn), Fold(std::move(FolderIn))
    {
        Capture(Reflection);
        for (const auto& Document : DocumentsIn)
            Source[Document.first] = Document.first;
        Load = [DocumentsIn](const std::string& Key)
        {
            const auto Found = DocumentsIn.find(Key);
            return Found == DocumentsIn.end() ? J() : Found->second;
        };
        for (const auto& Name : Source)
            Folded[Fold(Name.first)] = Name.second;
    }
    GraphSignatures(const J& Reflection, const Sources& SourcesIn, Loader LoaderIn,
                    Folder FolderIn = SignatureUnicode::Lower)
        : Source(SourcesIn), Load(std::move(LoaderIn)), Fold(std::move(FolderIn))
    {
        Capture(Reflection);
        for (const auto& Name : Source)
            Folded[Fold(Name.first)] = Name.second;
    }
    const J* Doc(const std::string& Name)
    {
        auto Found = Docs.find(Name);
        if (Found == Docs.end())
        {
            std::string Token;
            const auto Exact = Source.find(Name);
            if (Exact != Source.end())
                Token = Exact->second;
            else
            {
                const auto Other = Folded.find(Fold(Name));
                if (Other != Folded.end())
                    Token = Other->second;
            }
            const auto Installed = Docs.find(Token);
            Found = Docs.emplace(Name, Token.empty()             ? J()
                                       : Installed != Docs.end() ? Installed->second
                                                                 : Load(Token))
                        .first;
        }
        if (!Found->second.Truth())
            return nullptr;
        Touched.insert(Found->second["package"].S());
        return &Found->second;
    }
    void PutDocument(const std::string& Name, const J& Document)
    {
        // Cached misses and aliases may predate this caller-supplied document.
        J Stable = Document;
        const auto Lower = Fold(Name);
        for (auto At = Docs.begin(); At != Docs.end();)
            if (Fold(At->first) == Lower)
                At = Docs.erase(At);
            else
                ++At;
        Docs[Name] = std::move(Stable);
        Source[Name] = Name;
        Folded[Lower] = Name;
    }
    J OfRecord(const J& Record) const
    {
        J Out = J::Array();
        for (const auto& Property : Record["properties"].Items)
        {
            const auto Flags = HexFlags(Property["flags"]);
            if (Flags & 0x80)
                Out.Items.push_back(J::Object({{"name", Property["name"]},
                                               {"out", bool(Flags & 0x100)},
                                               {"ref", bool(Flags & 0x8000000)},
                                               {"const", bool(Flags & 2)},
                                               {"ret", bool(Flags & 0x400)},
                                               {"prop", Property}}));
        }
        return Out;
    }
    J Get(const std::string& Path)
    {
        const auto Record = Native.find(Path);
        if (Record != Native.end())
        {
            J Parameters = J::Array();
            const J& Signature = Maybe(Record->second, "script_signature");
            for (const auto& Parameter : Maybe(Signature, "parameters").Items)
                if (!Maybe(Parameter, "is_return").Truth())
                    Parameters.Items.push_back(J::Object({{"name", Parameter["name"]},
                                                          {"out", Maybe(Parameter, "is_out").Truth()},
                                                          {"ref", Maybe(Parameter, "is_reference").Truth()},
                                                          {"ret", false}}));
            const auto& Result = Maybe(Signature, "return_property");
            if (Result.Truth())
                Parameters.Items.push_back(
                    J::Object({{"name", Result["name"]}, {"out", true}, {"ref", false}, {"ret", true}}));
            return J::Array({Parameters, J(HexFlags(Record->second["flags"]))});
        }
        if (Starts(Path, "/Script/"))
            return J();
        const auto Pair = OwnerName(Path);
        const auto* Document = Doc(Package(Pair.first));
        if (Document && (*Document)["functions"].Has(Pair.second))
        {
            const auto& Function = (*Document)["functions"][Pair.second];
            return J::Array({OfRecord(Function), Function["flags"]});
        }
        return J();
    }
    std::vector<std::string> Chain(std::string Class)
    {
        std::vector<std::string> Out;
        std::set<std::string> Seen;
        while (!Class.empty() && Seen.insert(Class).second)
        {
            Out.push_back(Class);
            if (Starts(Class, "/Script/"))
            {
                const auto Parent = Parents.find(Class);
                Class = Parent == Parents.end() || !Parent->second.Truth() ? "" : Parent->second.S();
            }
            else
            {
                const auto* Document = Doc(Package(Class));
                const auto Parent = Document && (*Document)["class"].Truth() ? (*Document)["class"]["super"] : J();
                Class = !Parent.Truth() ? "" : Starts(Parent.S(), "/") ? Parent.S() : Package(Class) + "." + Parent.S();
            }
        }
        return Out;
    }
    std::string Find(const std::string& Class, std::string Name)
    {
        for (const auto& Owner : Chain(Class))
        {
            if (Starts(Owner, "/Script/"))
            {
                const auto Found = ByOwner.find(Owner);
                if (Found == ByOwner.end())
                    continue;
                const auto Path = Found->second.find(Fold(Name));
                if (Path != Found->second.end())
                    return Path->second;
            }
            else
            {
                const auto* Document = Doc(Package(Owner));
                if (!Document)
                    continue;
                const auto& Functions = (*Document)["functions"];
                if (!Functions.Has(Name))
                    for (const auto& Function : Functions.Keys())
                        if (Fold(Function) == Fold(Name))
                        {
                            Name = Function;
                            break;
                        }
                if (Functions.Has(Name))
                    return Owner + "." + Name;
            }
        }
        return "";
    }
    std::string Anywhere(const std::string& Name) const
    {
        const auto Lower = Fold(Name);
        std::string Out;
        for (const auto& Owner : ByOwner)
        {
            const auto Found = Owner.second.find(Lower);
            if (Found != Owner.second.end())
            {
                if (!Out.empty())
                    return "";
                Out = Found->second;
            }
        }
        return Out;
    }
};
}
}

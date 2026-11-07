// Native, fail-closed first stage of original declaration generation. No Python,
// filesystem, private captures, or C++ supplied by evidence. Unsupported cases
// return explicit refusals and no files; this is not whole-game coverage.
#pragma once
#include "original_declarations_engine.h"

namespace P4MapKit
{
namespace OriginalDeclarationsGenerate
{
using Blueprint::J;
using Blueprint::Error;
struct Generated
{
    std::map<std::string, std::string> Files;
    std::vector<std::string> Modules, Refusals;
    std::string SourceDigest;
};
inline std::string Module(const std::string& Path)
{
    if (Path.find("/Script/") != 0)
        throw Error("original script identity required: " + Path);
    const auto Dot = Path.find('.', 8);
    if (Dot == std::string::npos || !OriginalDeclarationsEngine::Ident(Path.substr(8, Dot - 8)))
        throw Error("original script module invalid");
    return Path.substr(8, Dot - 8);
}
inline std::string Name(const std::string& Path)
{
    (void)Module(Path);
    const auto Value = Path.substr(Path.find('.', 8) + 1);
    if (!OriginalDeclarationsEngine::Ident(Value))
        throw Error("original script type name invalid: " + Path);
    return Value;
}
inline bool Game(const std::string& Path)
{
    return OriginalDeclarations::ModuleIds().count(Module(Path)) && Module(Path) != "P4StubsCore";
}
inline std::uint64_t Flags(const J& Value)
{
    const auto Text = Value.S();
    if (Text.size() < 3 || Text.find("0x") != 0 || Text.size() > 18)
        throw Error("original hexadecimal flags required");
    std::uint64_t Out = 0;
    for (std::size_t At = 2; At < Text.size(); ++At)
    {
        const char C = Text[At];
        const auto Digit = C >= '0' && C <= '9' ? C - '0' : C >= 'a' && C <= 'f' ? C - 'a' + 10 : -1;
        if (Digit < 0)
            throw Error("original hexadecimal flags invalid");
        Out = (Out << 4) | unsigned(Digit);
    }
    return Out;
}
inline bool CppName(const std::string& Text)
{
    std::size_t At = 0;
    while (At < Text.size())
    {
        const auto End = Text.find("::", At);
        if (!OriginalDeclarationsEngine::Ident(
                Text.substr(At, End == std::string::npos ? std::string::npos : End - At)))
            return false;
        if (End == std::string::npos)
            return true;
        At = End + 2;
    }
    return false;
}
inline bool SourceIdentifier(const std::string& Text)
{
    static const std::set<std::string> Reserved{"alignas",  "alignof",   "and",       "asm",       "auto",
                                                "bool",     "break",     "case",      "catch",     "char",
                                                "class",    "const",     "constexpr", "continue",  "decltype",
                                                "default",  "delete",    "do",        "double",    "else",
                                                "enum",     "explicit",  "export",    "extern",    "false",
                                                "float",    "for",       "friend",    "goto",      "if",
                                                "inline",   "int",       "long",      "namespace", "new",
                                                "noexcept", "not",       "nullptr",   "operator",  "or",
                                                "private",  "protected", "public",    "register",  "reinterpret_cast",
                                                "return",   "short",     "signed",    "sizeof",    "static",
                                                "struct",   "switch",    "template",  "this",      "throw",
                                                "true",     "try",       "typedef",   "typeid",    "typename",
                                                "union",    "unsigned",  "using",     "virtual",   "void",
                                                "volatile", "while",     "xor",       "TRUE",      "FALSE",
                                                "IN",       "OUT",       "ERROR",     "NULL",      "TEXT",
                                                "check",    "verify"};
    return OriginalDeclarationsEngine::Ident(Text) && !Reserved.count(Text);
}
class Model
{
    const J& Defaults;
    const J& Zero;
    const J& Index;
    const J& Declared;
    std::map<std::string, J> Types, Enums, Functions, Delegates;
    std::map<std::string, std::string> Cpp;
    std::map<std::string, std::set<std::string>> Includes, Dependencies;
    std::map<std::string, std::set<std::string>> GroupIncludes, Forward;
    std::string Phase = "Classes";
    std::size_t AssetEvidenceRows = 0;
    Generated Out;
    void Refuse(const std::string& Why)
    {
        if (std::find(Out.Refusals.begin(), Out.Refusals.end(), Why) == Out.Refusals.end())
            Out.Refusals.push_back(Why);
    }
    std::string TypeName(const std::string& Path, const std::string& Kind, const std::string& Owner, unsigned Depth = 0)
    {
        if (Depth > 128)
        {
            Refuse("declaration inheritance cycle: " + Path);
            return "";
        }
        const auto Mod = Module(Path), Key = Mod + '.' + Name(Path);
        if (Game(Path))
        {
            if (Mod != Owner)
            {
                Dependencies[Owner].insert(Mod);
                if (Kind != "classes")
                    GroupIncludes[Owner + '/' + Phase].insert(Mod + "Stub" + (Kind == "structs" ? "Structs" : "Enums") +
                                                              ".h");
            }
            if (Cpp.count(Path))
                return Cpp.at(Path);
            if (Kind == "enums")
            {
                if (!Enums.count(Path))
                {
                    Refuse("missing original enum: " + Path);
                    return "";
                }
                if (!SourceIdentifier(Name(Path)))
                {
                    Refuse("original enum C++ identity invalid: " + Path);
                    return "";
                }
                return Cpp[Path] = Name(Path);
            }
            if (!Types.count(Path))
            {
                Refuse("missing original declaration: " + Path);
                return "";
            }
            const auto& Record = Types.at(Path);
            if (Kind == "structs" && Record["kind"].S() == "ScriptStruct")
                return Cpp[Path] = 'F' + Name(Path);
            if (Kind != "classes" || Record["kind"].S() != "Class")
            {
                Refuse("original declaration kind mismatch: " + Path);
                return "";
            }
            const auto Parent = Record["super_path"].Kind == J::K::Null ? "" : Record["super_path"].S();
            if (Parent.empty())
            {
                Refuse("original class parent absent: " + Path);
                return "";
            }
            const auto Base = TypeName(Parent, "classes", Owner, Depth + 1);
            if (Base.empty())
                return "";
            if (Parent == "/Script/CoreUObject.Interface")
            {
                Refuse("original interface generation not qualified: " + Path);
                return "";
            }
            return Cpp[Path] = std::string(Base.front() == 'A' ? "A" : "U") + Name(Path);
        }
        if (!Index.Has(Kind) || !Index[Kind].Has(Key))
        {
            Refuse("installed engine declaration absent: " + Path);
            return "";
        }
        const auto& Record = Index[Kind][Key];
        const auto Value = Record["cpp"].S();
        if (!CppName(Value) || !OriginalDeclarations::RelativePath(Record["include"].S()))
        {
            Refuse("unsafe installed engine declaration: " + Path);
            return "";
        }
        Includes[Owner].insert(Record["include"].S());
        Dependencies[Owner].insert(Mod);
        return Cpp[Path] = Value;
    }
    std::string PropertyType(const J& Detail, const std::string& Owner, unsigned Depth = 0)
    {
        if (Depth > 24)
        {
            Refuse("original property nesting budget");
            return "";
        }
        if (!Detail.Has("status") || Detail["status"].S() != "observed")
        {
            Refuse("original property type not observed");
            return "";
        }
        const auto Kind = Detail["kind"].S();
        static const std::map<std::string, std::string> Simple{
            {"BoolProperty", "bool"},     {"Int8Property", "int8"},     {"Int16Property", "int16"},
            {"IntProperty", "int32"},     {"Int64Property", "int64"},   {"UInt16Property", "uint16"},
            {"UInt32Property", "uint32"}, {"UInt64Property", "uint64"}, {"FloatProperty", "float"},
            {"DoubleProperty", "double"}, {"NameProperty", "FName"},    {"StrProperty", "FString"},
            {"TextProperty", "FText"}};
        const auto Found = Simple.find(Kind);
        if (Found != Simple.end())
            return Found->second;
        if (Kind == "ByteProperty" && (!Detail.Has("enum_path") || Detail["enum_path"].Kind == J::K::Null))
            return "uint8";
        if (Kind == "StructProperty")
            return TypeName(Detail["struct_path"].S(), "structs", Owner);
        if (Kind == "EnumProperty")
        {
            if (!Detail.Has("underlying") || Detail["underlying"]["kind"].S() != "ByteProperty")
            {
                Refuse("non-byte original enum width not qualified");
                return "";
            }
            return TypeName(Detail["enum_path"].S(), "enums", Owner);
        }
        if (Kind == "ObjectProperty" || Kind == "WeakObjectProperty" || Kind == "SoftObjectProperty")
        {
            const auto Path = Detail["class_path"].S();
            const auto CppValue = TypeName(Path, "classes", Owner);
            if (Game(Path) && !CppValue.empty())
                Forward[Owner + '/' + Phase].insert(CppValue);
            return Kind == "ObjectProperty"
                       ? CppValue + '*'
                       : (Kind == "WeakObjectProperty" ? "TWeakObjectPtr<" : "TSoftObjectPtr<") + CppValue + '>';
        }
        if (Kind == "ArrayProperty")
            return "TArray<" + PropertyType(Detail["inner"], Owner, Depth + 1) + '>';
        if (Kind == "ClassProperty" || Kind == "SoftClassProperty")
        {
            const auto Path = Detail["meta_class_path"].S();
            const auto CppValue = TypeName(Path, "classes", Owner);
            if (Game(Path) && !CppValue.empty())
                Forward[Owner + '/' + Phase].insert(CppValue);
            return Kind == "SoftClassProperty"            ? "TSoftClassPtr<" + CppValue + '>'
                   : Path == "/Script/CoreUObject.Object" ? "UClass*"
                                                          : "TSubclassOf<" + CppValue + '>';
        }
        if (Kind == "SetProperty" || Kind == "MapProperty")
        {
            const auto& Key = Detail[Kind == "SetProperty" ? "inner" : "key"];
            if (Key["kind"].S() == "StructProperty")
            {
                Refuse("original struct key hashing not qualified");
                return "";
            }
            const auto KeyType = PropertyType(Key, Owner, Depth + 1);
            return Kind == "SetProperty"
                       ? "TSet<" + KeyType + '>'
                       : "TMap<" + KeyType + ", " + PropertyType(Detail["value"], Owner, Depth + 1) + '>';
        }
        Refuse("original property kind not qualified: " + Kind);
        return "";
    }
    std::string Fields(const J& Record, const std::string& Owner)
    {
        std::string Text;
        std::set<std::string> Names;
        for (const auto& Field : Record["fields"].Items)
        {
            const auto FieldName = Field["name"].S();
            if (!SourceIdentifier(FieldName) || !Names.insert(OriginalDeclarations::Fold(FieldName)).second)
            {
                Refuse("original field identity invalid or duplicate: " + FieldName);
                continue;
            }
            const auto FlagsValue = Flags(Field["flags"]), Dim = Field["array_dim"].U();
            if (Dim != 1)
            {
                Refuse("original fixed property array not qualified: " + FieldName);
                continue;
            }
            // Net/config/instancing/default-subobject behavior requires separate qualification.
            if (FlagsValue & (0x20ull | 0x4000ull | 0x80000ull | 0x20000000ull))
            {
                Refuse("original property lifecycle flags not qualified: " + FieldName);
                continue;
            }
            const auto Value = PropertyType(Field["type_detail"], Owner);
            if (Value.empty())
                continue;
            const auto Kind = Field["type_detail"]["kind"].S();
            const bool Friendly = Kind == "BoolProperty" || Kind == "IntProperty" || Kind == "Int64Property" ||
                                  Kind == "FloatProperty" || Kind == "NameProperty" || Kind == "StrProperty" ||
                                  Kind == "TextProperty" || Kind == "StructProperty" || Kind == "EnumProperty" ||
                                  Kind == "ObjectProperty" || Kind == "ArrayProperty" || Kind == "ByteProperty";
            std::string Spec = "EditAnywhere";
            if (FlagsValue & 0x2000)
                Spec += ", Transient";
            if (FlagsValue & 0x1000000)
                Spec += ", SaveGame";
            if (Friendly)
                Spec += (FlagsValue & 0x10) ? ", BlueprintReadOnly" : ", BlueprintReadWrite";
            Text += "\tUPROPERTY(" + Spec + ", Category=\"Pikmin 4\")\n\t";
            if (Kind == "BoolProperty" && Field["type_detail"].Has("native_bool") &&
                !Field["type_detail"]["native_bool"].B())
                Text += "uint8 " + FieldName + " : 1;\n";
            else
                Text += Value + ' ' + FieldName + "{};\n";
        }
        return Text;
    }
    std::string Function(const std::string& Path, const J& Record, const std::string& Owner)
    {
        const auto Dot = Path.rfind('.');
        const auto Fn = Path.substr(Dot + 1);
        if (!SourceIdentifier(Fn) || !Record.Has("script_signature"))
        {
            Refuse("original function signature absent: " + Path);
            return "";
        }
        const auto FlagsValue = Flags(Record["flags"]);
        if (!(FlagsValue & 0x400))
        {
            Refuse("original non-native function not qualified: " + Path);
            return "";
        }
        if (FlagsValue & (0x100000ull | 0x8000000ull | 0x40ull | 0x80000000ull))
        {
            Refuse("original event/delegate/network function not qualified: " + Path);
            return "";
        }
        const auto& Signature = Record["script_signature"];
        if (Signature["status"].S() != "observed" || !Signature.Has("return_property") ||
            Signature["parameters"].Kind != J::K::Array)
        {
            Refuse("original function signature not observed: " + Path);
            return "";
        }
        std::string Return = "void";
        if (Signature["return_property"].Kind != J::K::Null)
        {
            const auto& Property = Signature["return_property"];
            if (Property["array_dim"].U() != 1 || (Flags(Property["flags"]) & 0x480) != 0x480)
                Refuse("original function return property invalid: " + Path);
            Return = PropertyType(Property["type_detail"], Owner);
        }
        std::string Args;
        std::set<std::string> Names;
        for (const auto& Param : Signature["parameters"].Items)
        {
            const auto ParamName = Param["name"].S();
            if (!SourceIdentifier(ParamName) || !Names.insert(OriginalDeclarations::Fold(ParamName)).second)
            {
                Refuse("original function parameter identity invalid: " + Path);
                continue;
            }
            if (Param["array_dim"].U() != 1)
            {
                Refuse("original function fixed array not qualified: " + Path);
                continue;
            }
            const auto Type = PropertyType(Param["type_detail"], Owner);
            const auto Bits = Flags(Param["flags"]);
            if (!(Bits & 0x80) || (Bits & 0x400))
            {
                Refuse("original function parameter flags invalid: " + Path);
                continue;
            }
            if (!Args.empty())
                Args += ", ";
            if (Bits & 0x8000000)
                Args += "UPARAM(ref) ";
            if (Bits & 2)
                Args += "const ";
            Args += Type + ((Bits & (0x100 | 0x8000000)) ? "& " : " ") + ParamName;
        }
        std::string Spec;
        if ((FlagsValue & 0x10000000) && Return == "void" && Signature["parameters"].Items.empty())
            Refuse("original pure function has no output: " + Path);
        if (FlagsValue & 0x10000000)
            Spec = "BlueprintPure";
        else if (FlagsValue & 0x4000000)
            Spec = "BlueprintCallable";
        if (!Spec.empty())
            Spec += ", ";
        Spec += "Category=\"Pikmin 4\"";
        std::string Text = "\tUFUNCTION(" + Spec + ")\n\t";
        if (FlagsValue & 0x2000)
            Text += "static ";
        Text += Return + ' ' + Fn + '(' + Args + ')' + ((FlagsValue & 0x40000000) ? " const" : "") + " { ";
        if (Return != "void")
            Text += "return {}; ";
        return Text + "}\n";
    }
    std::string DefaultText(const J& Value, const J& Detail, unsigned Depth = 0)
    {
        if (Depth > 24)
        {
            Refuse("original default value nesting budget");
            return "";
        }
        const auto Kind = Detail["kind"].S();
        if (Value.Kind == J::K::Boolean && Kind == "BoolProperty")
            return Value.B() ? "True" : "False";
        if (Value.Kind == J::K::Number)
        {
            static const std::map<std::string, unsigned> Signed{
                {"Int8Property", 8}, {"Int16Property", 16}, {"IntProperty", 32}, {"Int64Property", 64}},
                Unsigned{{"ByteProperty", 8}, {"UInt16Property", 16}, {"UInt32Property", 32}, {"UInt64Property", 64}};
            if (Signed.count(Kind))
            {
                const auto N = Value.I();
                const auto Bits = Signed.at(Kind);
                if (Bits < 64 && (N < -(std::int64_t(1) << (Bits - 1)) || N >= (std::int64_t(1) << (Bits - 1))))
                {
                    Refuse("original signed property default out of range");
                    return "";
                }
                return Value.Text;
            }
            if (Unsigned.count(Kind))
            {
                const auto N = Value.U();
                const auto Bits = Unsigned.at(Kind);
                if (Bits < 64 && N >= (std::uint64_t(1) << Bits))
                {
                    Refuse("original unsigned property default out of range");
                    return "";
                }
                return Value.Text;
            }
            if (Kind == "DoubleProperty" || Kind == "FloatProperty")
            {
                const auto N = Value.D();
                if (Kind == "FloatProperty" && std::abs(N) > std::numeric_limits<float>::max())
                {
                    Refuse("original floating property default out of range");
                    return "";
                }
                return Value.Text;
            }
        }
        if (Value.Kind == J::K::Null &&
            (Kind == "ObjectProperty" || Kind == "WeakObjectProperty" || Kind == "ClassProperty"))
            return "None";
        if (Value.Kind == J::K::Object && Value.Fields.size() == 1 && Value.Has("name") && Kind == "NameProperty")
            return Value["name"].S();
        if (Value.Kind == J::K::Object && Value.Fields.size() == 1 && Value.Has("str") && Kind == "StrProperty")
            return J(Value["str"].S()).Dump();
        if (Value.Kind == J::K::Object && Value.Fields.size() == 1 && Value.Has("soft") &&
            (Kind == "SoftObjectProperty" || Kind == "SoftClassProperty"))
            return J(Value["soft"].S()).Dump();
        if (Value.Kind == J::K::Object && Value.Has("array") && Kind == "ArrayProperty" &&
            Value["array"].Kind == J::K::Array)
        {
            std::string Text;
            for (const auto& Element : Value["array"].Items)
            {
                if (!Text.empty())
                    Text += ',';
                Text += DefaultText(Element, Detail["inner"], Depth + 1);
            }
            return Value["array"].Items.empty() ? "" : '(' + Text + ')';
        }
        if (Value.Kind == J::K::Object && Value.Has("count") && Value["count"].U() == 0 &&
            (Kind == "MapProperty" || Kind == "SetProperty") && !Value.Has("unread"))
            return "";
        if (Value.Kind == J::K::Object && Value.Has("fields") && Kind == "StructProperty" &&
            Value["fields"].Kind == J::K::Object)
        {
            const auto Path = Detail["struct_path"].S();
            if (!Types.count(Path))
            {
                Refuse("original struct default declaration absent: " + Path);
                return "";
            }
            std::string Text;
            for (const auto& Member : Value["fields"].Fields)
            {
                if (!SourceIdentifier(Member.first))
                {
                    Refuse("original struct default member identity invalid: " + Member.first);
                    continue;
                }
                const J* Found = nullptr;
                auto Current = Path;
                std::set<std::string> Seen;
                while (Types.count(Current) && Seen.insert(Current).second && !Found)
                {
                    for (const auto& Field : Types.at(Current)["fields"].Items)
                        if (Field["name"].S() == Member.first)
                        {
                            Found = &Field;
                            break;
                        }
                    const auto& Parent = Types.at(Current)["super_path"];
                    if (Parent.Kind == J::K::Null)
                        break;
                    Current = Parent.S();
                }
                if (!Found)
                {
                    Refuse("original struct default member absent: " + Path + '.' + Member.first);
                    continue;
                }
                if (!Game(Path) && (!Declared.Has(Path) || !Declared[Path].Has("p") ||
                                    !std::any_of(Declared[Path]["p"].Items.begin(), Declared[Path]["p"].Items.end(),
                                                 [&](const J& Field) { return Field.S() == Member.first; })))
                    Refuse("installed engine struct default member not proven: " + Path + '.' + Member.first);
                if (!Text.empty())
                    Text += ',';
                Text += Member.first + '=' + DefaultText(Member.second, (*Found)["type_detail"], Depth + 1);
            }
            return '(' + Text + ')';
        }
        Refuse("original default value kind not qualified");
        return "";
    }
    std::string ClassSpecifiers(const J& Default, const std::string& Path)
    {
        if (!Default.Has("class_flags"))
        {
            Refuse("original class flags absent: " + Path);
            return "";
        }
        const auto Bits = Flags(Default["class_flags"]);
        // These are renderable semantic flags or observed runtime flags. Runtime
        // bits are not assigned here: the loaded reflection gate compares their
        // original values after Unreal creates and assembles the native class.
        const std::uint64_t Allowed = 0x304000a0ull | 1 | 2 | 4 | 8 | 0x200 | 0x1000 | 0x2000 | 0x10000 | 0x80000 |
                                      0x100000 | 0x200000 | 0x800000;
        if (!(Bits & 0x80) || (Bits & ~Allowed))
        {
            Refuse("original class lifecycle flags not qualified: " + Path);
            return "";
        }
        std::string Spec = "BlueprintType, Blueprintable";
        for (const auto& Pair : std::vector<std::pair<std::uint64_t, std::string>>{{1, "Abstract"},
                                                                                   {2, "DefaultConfig"},
                                                                                   {8, "Transient"},
                                                                                   {0x1000, "EditInlineNew"},
                                                                                   {0x2000, "CollapseCategories"},
                                                                                   {0x10000, "Const"},
                                                                                   {0x80000, "MinimalAPI"},
                                                                                   {0x200000, "DefaultToInstanced"}})
            if (Bits & Pair.first)
                Spec += ", " + Pair.second;
        if (Bits & 0x200)
            Spec += ", NotPlaceable";
        if (Bits & 4)
        {
            if (!Default.Has("config") || !SourceIdentifier(Default["config"].S()))
                Refuse("original config identity absent or unsafe: " + Path);
            else
                Spec += ", Config=" + Default["config"].S();
        }
        if (Default.Has("within") && Default["within"].Kind != J::K::Null)
            Refuse("original class within behavior not qualified: " + Path);
        return Spec;
    }
    std::vector<std::string> Ordered(const std::string& Kind)
    {
        std::vector<std::string> Result;
        std::map<std::string, unsigned> State;
        std::function<void(const std::string&, unsigned)> Visit;
        Visit = [&](const std::string& Path, unsigned Depth)
        {
            if (!Types.count(Path) || !Game(Path) || Types.at(Path)["kind"].S() != Kind || State[Path] == 2)
                return;
            if (Depth > 128 || State[Path] == 1)
            {
                Refuse("original declaration dependency cycle: " + Path);
                return;
            }
            State[Path] = 1;
            const auto& Record = Types.at(Path);
            if (Record["super_path"].Kind != J::K::Null)
                Visit(Record["super_path"].S(), Depth + 1);
            if (Kind == "ScriptStruct")
            {
                std::function<void(const J&, unsigned)> Reference;
                Reference = [&](const J& Detail, unsigned Nesting)
                {
                    if (Nesting > 24)
                    {
                        Refuse("original property nesting budget");
                        return;
                    }
                    if (Detail.Has("struct_path") && Detail["struct_path"].Kind != J::K::Null)
                        Visit(Detail["struct_path"].S(), Depth + 1);
                    for (const auto* Key : {"inner", "key", "value", "underlying"})
                        if (Detail.Has(Key) && Detail[Key].Kind == J::K::Object)
                            Reference(Detail[Key], Nesting + 1);
                };
                for (const auto& Field : Record["fields"].Items)
                    Reference(Field["type_detail"], 0);
            }
            State[Path] = 2;
            Result.push_back(Path);
        };
        for (const auto& Pair : Types)
            Visit(Pair.first, 0);
        return Result;
    }

public:
    Model(const J& Reflection, const J& DefaultsValue, const J& ZeroValue, const J& IndexValue, const J& DeclaredValue,
          const std::string& Digest)
        : Defaults(DefaultsValue), Zero(ZeroValue), Index(IndexValue), Declared(DeclaredValue)
    {
        Out.SourceDigest = Digest;
        Out.Modules = {"P4StubsCore", "AkAudio", "Carrot4", "EasyParticle", "SwitchRuntimeSettings", "UIParticle"};
        if (!OriginalDeclarations::Digest(Digest) || Reflection["schema_version"].I() != 2 ||
            !Reflection.Has("source_sha256") || Reflection["source_sha256"].S() != Digest)
            Refuse("original reflection executable provenance mismatch");
        if (Defaults.Kind != J::K::Object || !Defaults.Has("classes") || Defaults["classes"].Kind != J::K::Object ||
            Zero.Kind != J::K::Array || Index.Kind != J::K::Object || Declared.Kind != J::K::Object)
            throw Error("native declaration generator input schemas required");
        for (const auto* Key : {"structures", "enums", "functions"})
            if (!Reflection.Has(Key) || Reflection[Key].Kind != J::K::Array)
                throw Error("original reflection declaration arrays required");
        for (const auto& Record : Reflection["structures"].Items)
        {
            const auto Path = Record["path"].S();
            if (Path.find("/Script/") != 0)
            {
                ++AssetEvidenceRows;
                continue;
            }
            (void)Name(Path);
            if (Record["fields"].Kind != J::K::Array)
                throw Error("original structure fields array required");
            if (Module(Path) == "P4StubsCore")
                Refuse("reserved original module identity: " + Path);
            if (!Types.emplace(Path, Record).second)
                Refuse("duplicate original structure: " + Path);
        }
        for (const auto& Record : Reflection["enums"].Items)
        {
            const auto Path = Record["path"].S();
            if (Path.find("/Script/") != 0)
            {
                ++AssetEvidenceRows;
                continue;
            }
            (void)Name(Path);
            if (!Enums.emplace(Path, Record).second)
                Refuse("duplicate original enum: " + Path);
        }
        for (const auto& Record : Reflection["functions"].Items)
        {
            const auto Path = Record["path"].S();
            if (Path.find("/Script/") != 0)
            {
                ++AssetEvidenceRows;
                continue;
            }
            if (Flags(Record["flags"]) & 0x100000)
            {
                (void)Module(Path);
                if (!Delegates.emplace(Path, Record).second)
                    Refuse("duplicate original delegate signature: " + Path);
                continue;
            }
            const auto Dot = Path.rfind('.');
            if (Dot == std::string::npos)
                throw Error("original function owner required");
            (void)Name(Path.substr(0, Dot));
            if (!Functions.emplace(Path, Record).second)
                Refuse("duplicate original function: " + Path);
        }
        for (const auto& Fn : Functions)
        {
            const auto Owner = Fn.first.substr(0, Fn.first.rfind('.'));
            if (Game(Owner) && (!Types.count(Owner) || Types[Owner]["kind"].S() != "Class"))
                Refuse("original function owner class missing: " + Fn.first);
        }
        for (const auto& Signature : Delegates)
            if (Game(Signature.first))
                Refuse("original delegate declaration not qualified: " + Signature.first);
    }
    Generated Generate()
    {
        std::map<std::string, std::string> EnumBodies, StructBodies, ClassBodies;
        std::map<std::string, std::string> SeenCpp;
        std::set<std::string> EngineCpp;
        for (const auto* Kind : {"classes", "structs", "enums"})
            if (Index.Has(Kind))
                for (const auto& Pair : Index[Kind].Fields)
                    EngineCpp.insert(OriginalDeclarations::Fold(
                        Pair.second["cpp"].S().substr(0, Pair.second["cpp"].S().find("::"))));
        for (const auto& Pair : Types)
            if (Game(Pair.first))
            {
                const auto Kind = Pair.second["kind"].S();
                if (Kind != "Class" && Kind != "ScriptStruct")
                {
                    Refuse("original structure kind not qualified: " + Pair.first);
                    continue;
                }
                const auto CppValue = TypeName(Pair.first, Kind == "Class" ? "classes" : "structs", Module(Pair.first));
                if (CppValue.empty())
                    continue;
                if (!SeenCpp.emplace(OriginalDeclarations::Fold(CppValue), Pair.first).second)
                    Refuse("global original C++ type name collision: " + CppValue);
                if (EngineCpp.count(OriginalDeclarations::Fold(CppValue)))
                    Refuse("installed engine C++ type name collision: " + Pair.first);
            }
        for (const auto& Pair : Enums)
            if (Game(Pair.first))
            {
                const auto Mod = Module(Pair.first), CppValue = TypeName(Pair.first, "enums", Mod);
                if (!SeenCpp.emplace(OriginalDeclarations::Fold(CppValue), Pair.first).second)
                    Refuse("global original enum name collision: " + CppValue);
                if (EngineCpp.count(OriginalDeclarations::Fold(CppValue)))
                    Refuse("installed engine C++ enum name collision: " + Pair.first);
                std::string Text = "UENUM(BlueprintType)\nenum class " + CppValue + " : uint8\n{\n";
                std::set<std::string> Names;
                if (!Pair.second.Has("status") || Pair.second["status"].S() != "observed")
                    Refuse("original enum not observed: " + Pair.first);
                if (Pair.second["values"].Kind != J::K::Array || Pair.second["values"].Items.empty())
                    Refuse("original enum values array required: " + Pair.first);
                for (const auto& Item : Pair.second["values"].Items)
                {
                    auto Entry = Item["name"].S();
                    const auto Sep = Entry.rfind("::");
                    if (Sep != std::string::npos)
                        Entry = Entry.substr(Sep + 2);
                    const auto Value = Item["value"].I();
                    if (!SourceIdentifier(Entry) || !Names.insert(OriginalDeclarations::Fold(Entry)).second ||
                        Value < 0 || Value > 255)
                    {
                        Refuse("original enum entry not qualified: " + Pair.first);
                        continue;
                    }
                    Text += "\t" + Entry + " = " + std::to_string(Value) + ",\n";
                }
                EnumBodies[Mod] += Text + "};\n";
            }
        auto OrderedTypes = Ordered("ScriptStruct");
        const auto Classes = Ordered("Class");
        OrderedTypes.insert(OrderedTypes.end(), Classes.begin(), Classes.end());
        for (const auto& PathValue : OrderedTypes)
        {
            const auto Pair = std::make_pair(PathValue, Types.at(PathValue));
            const auto Path = Pair.first, Mod = Module(Path), Kind = Pair.second["kind"].S();
            Phase = Kind == "ScriptStruct" ? "Structs" : "Classes";
            if (!Cpp.count(Path))
                continue;
            const auto CppValue = Cpp.at(Path);
            const auto FieldText = Fields(Pair.second, Mod);
            if (Kind == "ScriptStruct")
            {
                auto Api = Mod;
                for (auto& C : Api)
                    if (C >= 'a' && C <= 'z')
                        C -= 'a' - 'A';
                const auto Parent = Pair.second["super_path"].Kind == J::K::Null
                                        ? ""
                                        : TypeName(Pair.second["super_path"].S(), "structs", Mod);
                StructBodies[Mod] += "USTRUCT(BlueprintType)\nstruct " + Api + "_API " + CppValue +
                                     (Parent.empty() ? "" : " : public " + Parent) + "\n{\n\tGENERATED_BODY()\n" +
                                     FieldText + "};\n";
                if (std::any_of(Zero.Items.begin(), Zero.Items.end(), [&](const J& Row) { return Row.S() == Path; }))
                    StructBodies[Mod] += "template<> struct TStructOpsTypeTraits<" + CppValue +
                                         "> : TStructOpsTypeTraitsBase2<" + CppValue +
                                         "> { enum { WithZeroConstructor = true }; };\n";
                continue;
            }
            if (Kind != "Class")
                continue;
            const auto Parent = Pair.second["super_path"].S(), ParentCpp = TypeName(Parent, "classes", Mod);
            if (Game(Parent) && Module(Parent) != Mod)
                GroupIncludes[Mod + "/Classes"].insert(Module(Parent) + "StubClasses.h");
            if (Types.count(Parent) && !Types[Parent]["fields"].Items.empty())
            {
                if (!Declared.Has(Parent) || !Declared[Parent].Has("p"))
                    Refuse("installed engine parent declaration proof absent: " + Parent);
                else
                    for (const auto& Field : Types[Parent]["fields"].Items)
                        if (!std::any_of(Declared[Parent]["p"].Items.begin(), Declared[Parent]["p"].Items.end(),
                                         [&](const J& Item)
                                         {
                                             return OriginalDeclarations::Fold(Item.S()) ==
                                                    OriginalDeclarations::Fold(Field["name"].S());
                                         }))
                            Refuse("native engine property addition not qualified: " + Parent + '.' +
                                   Field["name"].S());
            }
            if (!Defaults["classes"].Has(Path))
            {
                Refuse("original class defaults absent: " + Path);
                continue;
            }
            const auto& Default = Defaults["classes"][Path];
            if (Default.Has("error") || !Default.Has("own") || !Default.Has("inherited") ||
                !Default.Has("subobjects") || Default["own"].Kind != J::K::Object ||
                Default["inherited"].Kind != J::K::Object || Default["subobjects"].Kind != J::K::Array)
            {
                Refuse("original class defaults incomplete: " + Path);
                continue;
            }
            if (!Default["subobjects"].Items.empty() || !Default["inherited"].Fields.empty())
                Refuse("original inherited/subobject defaults not qualified: " + Path);
            const auto Specifiers = ClassSpecifiers(Default, Path);
            if (!Default.Has("super") || Default["super"].S() != Parent)
                Refuse("original class defaults parent mismatch: " + Path);
            std::string Constructor;
            for (const auto& Field : Pair.second["fields"].Items)
            {
                const auto FieldName = Field["name"].S();
                if (!Default["own"].Has(FieldName))
                {
                    Refuse("original own property default absent: " + Path + '.' + FieldName);
                    continue;
                }
                const auto Text = DefaultText(Default["own"][FieldName], Field["type_detail"]);
                Constructor += "\t\tP4OriginalDefaults::Set(this, TEXT(" + J(FieldName).Dump() + "), TEXT(" +
                               J(Text).Dump() + "));\n";
            }
            if (Default["own"].Fields.size() != Pair.second["fields"].Items.size())
                Refuse("original default property coverage mismatch: " + Path);
            std::string FunctionText;
            std::set<std::string> FunctionNames;
            for (const auto& Fn : Functions)
                if (Fn.first.substr(0, Fn.first.rfind('.')) == Path)
                {
                    const auto FnName = Fn.first.substr(Fn.first.rfind('.') + 1);
                    if (!FunctionNames.insert(OriginalDeclarations::Fold(FnName)).second)
                        Refuse("original function identity collision: " + Fn.first);
                    for (const auto& Field : Pair.second["fields"].Items)
                        if (OriginalDeclarations::Fold(Field["name"].S()) == OriginalDeclarations::Fold(FnName))
                            Refuse("original member identity collision: " + Fn.first);
                    FunctionText += Function(Fn.first, Fn.second, Mod);
                }
            auto Api = Mod;
            for (auto& C : Api)
                if (C >= 'a' && C <= 'z')
                    C -= 'a' - 'A';
            const auto ApiSpecifier = Flags(Default["class_flags"]) & 0x100000 ? Api + "_API " : "";
            ClassBodies[Mod] += "UCLASS(" + Specifiers + ")\nclass " + ApiSpecifier + CppValue + " : public " +
                                ParentCpp + "\n{\n\tGENERATED_BODY()\npublic:\n" + FieldText + "\t" + CppValue +
                                "()\n\t{\n" + Constructor + "\t}\n" + FunctionText + "};\n";
        }
        for (const auto& Item : Zero.Items)
            if (!Types.count(Item.S()) || !Game(Item.S()) || Types.at(Item.S())["kind"].S() != "ScriptStruct")
                Refuse("original zero-constructor evidence not qualified: " + Item.S());
        std::map<std::string, unsigned> ModuleState;
        std::function<void(const std::string&)> CheckModule = [&](const std::string& Mod)
        {
            if (ModuleState[Mod] == 2)
                return;
            if (ModuleState[Mod] == 1)
            {
                Refuse("original runtime module dependency cycle: " + Mod);
                return;
            }
            ModuleState[Mod] = 1;
            for (const auto& Dependency : Dependencies[Mod])
                if (OriginalDeclarations::ModuleIds().count(Dependency))
                    CheckModule(Dependency);
            ModuleState[Mod] = 2;
        };
        for (const auto& Mod : Out.Modules)
            CheckModule(Mod);
        if (!Out.Refusals.empty())
            return Out;
        J Descriptor = J::Object({{"FileVersion", 3},
                                  {"Version", 1},
                                  {"VersionName", "0.1"},
                                  {"FriendlyName", "Pikmin 4 original declarations"},
                                  {"CanContainContent", false},
                                  {"Modules", J::Array()}});
        for (const auto& Mod : Out.Modules)
        {
            Descriptor["Modules"].Items.push_back(
                J::Object({{"Name", Mod}, {"Type", "Runtime"}, {"LoadingPhase", "PreDefault"}}));
            auto Deps = Dependencies[Mod];
            Deps.insert("Core");
            Deps.insert("CoreUObject");
            Deps.insert("Engine");
            if (Mod != "P4StubsCore")
                Deps.insert("P4StubsCore");
            Deps.erase(Mod);
            std::string DependencyText;
            for (const auto& Dep : Deps)
            {
                if (!DependencyText.empty())
                    DependencyText += ", ";
                DependencyText += '"' + Dep + '"';
            }
            const auto Base = "Source/" + Mod + '/';
            Out.Files[Base + Mod + ".Build.cs"] =
                "using UnrealBuildTool;\npublic class " + Mod + " : ModuleRules { public " + Mod +
                "(ReadOnlyTargetRules Target) : base(Target) { PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs; PublicDependencyModuleNames.AddRange(new string[] { " +
                DependencyText + " }); } }\n";
            Out.Files[Base + "Private/" + Mod + "Module.cpp"] =
                "#include \"Modules/ModuleManager.h\"\nIMPLEMENT_MODULE(FDefaultModuleImpl, " + Mod + ")\n";
            if (Mod == "P4StubsCore")
                continue;
            for (const auto& Group : {std::string("Enums"), std::string("Structs"), std::string("Classes")})
            {
                const auto Header = Mod + "Stub" + Group;
                std::string Text = "#pragma once\n#include \"CoreMinimal.h\"\n";
                if (Group != "Enums")
                {
                    Text += "#include \"" + Mod + "StubEnums.h\"\n";
                    if (Group == "Classes")
                        Text += "#include \"" + Mod + "StubStructs.h\"\n#include \"P4OriginalDefaults.h\"\n";
                    for (const auto& Include : Includes[Mod])
                        Text += "#include \"" + Include + "\"\n";
                }
                for (const auto& Include : GroupIncludes[Mod + '/' + Group])
                    Text += "#include \"" + Include + "\"\n";
                const auto Body = Group == "Enums"     ? EnumBodies[Mod]
                                  : Group == "Structs" ? StructBodies[Mod]
                                                       : ClassBodies[Mod];
                if (!Body.empty())
                    Text += "#include \"" + Header + ".generated.h\"\n";
                for (const auto& NameValue : Forward[Mod + '/' + Group])
                    Text += "class " + NameValue + ";\n";
                Text += Body;
                Out.Files[Base + "Public/" + Header + ".h"] = Text;
            }
        }
        Out.Files["P4Stubs.uplugin"] = Descriptor.Dump() + '\n';
        Out.Files["original-declarations-generation.json"] =
            J::Object({{"format", "p4-original-declaration-generation"},
                       {"version", 1},
                       {"original_executable_sha256", Out.SourceDigest},
                       {"scope", "native-script-modules"},
                       {"asset_evidence_rows_outside_scope", J(std::uint64_t(AssetEvidenceRows))}})
                .Dump() +
            '\n';
        Out.Files["Source/P4StubsCore/Public/P4OriginalDefaults.h"] =
            "#pragma once\n#include \"CoreMinimal.h\"\n#include \"UObject/UnrealType.h\"\nnamespace P4OriginalDefaults { inline void Set(UObject* Object, const TCHAR* Name, const TCHAR* Text) { FProperty* Property = FindFProperty<FProperty>(Object->GetClass(), Name); check(Property); const TCHAR* End = Property->ImportText(Text, Property->ContainerPtrToValuePtr<void>(Object), 0, Object); check(End && !*End); } }\n";
        return Out;
    }
};
inline Generated Generate(const J& Reflection, const J& Defaults, const J& ZeroConstructor, const J& EngineIndex,
                          const J& EngineDeclared, const std::string& SourceDigest)
{
    return Model(Reflection, Defaults, ZeroConstructor, EngineIndex, EngineDeclared, SourceDigest).Generate();
}
}
} // namespace P4MapKit::OriginalDeclarationsGenerate

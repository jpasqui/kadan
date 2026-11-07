// UE4.26 serialized Kismet grammar. Storage offsets and expanded script offsets
// remain distinct; a successful decode proves neither execution nor graph recovery.
#pragma once
#include "blueprint_json.h"
#include "package_reader.h"
#include "custom_material_hash.h"
#include <deque>
#include <set>

namespace P4MapKit { namespace Blueprint {
inline std::string HexNumber(std::uint64_t Value, bool Upper = false)
{ std::ostringstream Out; Out.imbue(std::locale::classic()); if (Upper) Out << std::uppercase; Out << std::hex << Value; return Out.str(); }
inline J Floating(double Value)
{
    if (!std::isfinite(Value)) throw Error("Non-finite script literal");
    std::ostringstream Out; Out.imbue(std::locale::classic()); Out << std::setprecision(17) << Value;
    auto Text = Out.str(); if (Text.find_first_of(".eE") == std::string::npos) Text += ".0"; return J::Number(Text);
}
inline const std::map<int, std::string>& Opcodes()
{
    static const std::map<int, std::string> Values = {
        {0x00,"LocalVariable"},{0x01,"InstanceVariable"},{0x02,"DefaultVariable"},{0x04,"Return"},
        {0x06,"Jump"},{0x07,"JumpIfNot"},{0x09,"Assert"},{0x0B,"Nothing"},{0x0F,"Let"},
        {0x12,"ClassContext"},{0x13,"MetaCast"},{0x14,"LetBool"},{0x15,"EndParmValue"},{0x16,"EndFunctionParms"},
        {0x17,"Self"},{0x18,"Skip"},{0x19,"Context"},{0x1A,"Context_FailSilent"},{0x1B,"VirtualFunction"},
        {0x1C,"FinalFunction"},{0x1D,"IntConst"},{0x1E,"FloatConst"},{0x1F,"StringConst"},{0x20,"ObjectConst"},
        {0x21,"NameConst"},{0x22,"RotationConst"},{0x23,"VectorConst"},{0x24,"ByteConst"},{0x25,"IntZero"},
        {0x26,"IntOne"},{0x27,"True"},{0x28,"False"},{0x29,"TextConst"},{0x2A,"NoObject"},
        {0x2B,"TransformConst"},{0x2C,"IntConstByte"},{0x2D,"NoInterface"},{0x2E,"DynamicCast"},
        {0x2F,"StructConst"},{0x30,"EndStructConst"},{0x31,"SetArray"},{0x32,"EndArray"},{0x33,"PropertyConst"},
        {0x34,"UnicodeStringConst"},{0x35,"Int64Const"},{0x36,"UInt64Const"},{0x38,"PrimitiveCast"},
        {0x39,"SetSet"},{0x3A,"EndSet"},{0x3B,"SetMap"},{0x3C,"EndMap"},{0x3D,"SetConst"},
        {0x3E,"EndSetConst"},{0x3F,"MapConst"},{0x40,"EndMapConst"},{0x42,"StructMemberContext"},
        {0x43,"LetMulticastDelegate"},{0x44,"LetDelegate"},{0x45,"LocalVirtualFunction"},{0x46,"LocalFinalFunction"},
        {0x48,"LocalOutVariable"},{0x4A,"DeprecatedOp4A"},{0x4B,"InstanceDelegate"},{0x4C,"PushExecutionFlow"},
        {0x4D,"PopExecutionFlow"},{0x4E,"ComputedJump"},{0x4F,"PopExecutionFlowIfNot"},{0x50,"Breakpoint"},
        {0x51,"InterfaceContext"},{0x52,"ObjToInterfaceCast"},{0x53,"EndOfScript"},{0x54,"CrossInterfaceCast"},
        {0x55,"InterfaceToObjCast"},{0x5A,"WireTracepoint"},{0x5B,"SkipOffsetConst"},{0x5C,"AddMulticastDelegate"},
        {0x5D,"ClearMulticastDelegate"},{0x5E,"Tracepoint"},{0x5F,"LetObj"},{0x60,"LetWeakObjPtr"},
        {0x61,"BindDelegate"},{0x62,"RemoveMulticastDelegate"},{0x63,"CallMulticastDelegate"},
        {0x64,"LetValueOnPersistentFrame"},{0x65,"ArrayConst"},{0x66,"EndArrayConst"},{0x67,"SoftObjectConst"},
        {0x68,"CallMath"},{0x69,"SwitchValue"},{0x6B,"ArrayGetByRef"},{0x6C,"ClassSparseDataVariable"},{0x6D,"FieldPathConst"}};
    return Values;
}
class BytecodeDecoder
{
    const std::vector<std::uint8_t>& Data;
    const std::vector<std::string>& Names;
    const std::function<J(std::int32_t)> Reference;
    std::size_t Position = 0, Runtime = 0;
    std::deque<J> Nodes;
    J Targets = J::Array(), References = J::Array();
    std::uint64_t Unsigned(std::size_t Width, std::size_t RuntimeWidth = 0)
    {
        if (Width > Data.size() - Position) throw Error("Truncated operand at storage 0x" + HexNumber(Position));
        std::uint64_t Out = 0; for (std::size_t Byte = 0; Byte < Width; ++Byte) Out |= std::uint64_t(Data[Position + Byte]) << (Byte * 8);
        Position += Width; Runtime += RuntimeWidth ? RuntimeWidth : Width; return Out;
    }
    std::int64_t Signed(std::size_t Width, std::size_t RuntimeWidth = 0)
    {
        const auto Bits = Unsigned(Width, RuntimeWidth), Sign = std::uint64_t(1) << (Width * 8 - 1);
        if (!(Bits & Sign)) return std::int64_t(Bits);
        const auto Mask = Width == 8 ? UINT64_MAX : (std::uint64_t(1) << (Width * 8)) - 1;
        return -1 - std::int64_t((~Bits) & Mask);
    }
    J Float()
    { const auto Bits = std::uint32_t(Unsigned(4)); float Value = 0; std::memcpy(&Value, &Bits, 4); return Floating(Value); }
    std::string Name(bool Resident = true)
    {
        const auto Index = Signed(4), Number = Signed(4); if (Resident) Runtime += 4;
        if (Index < 0 || std::uint64_t(Index) >= Names.size() || Number < 0) throw Error("Invalid script FName");
        return Names[std::size_t(Index)] + (Number ? "_" + std::to_string(Number - 1) : "");
    }
    J Object()
    {
        const auto Index = std::int32_t(Signed(4, 8)); J Path = Reference(Index);
        if (Index && Path.Kind != J::K::String) throw Error("Object reference did not resolve");
        References.Items.push_back(J::Object({{"kind", "object"}, {"index", J(int(Index))}, {"path", Path}}));
        return J::Object({{"index", J(int(Index))}, {"path", Path}});
    }
    J Field()
    {
        const auto Before = Runtime; const auto Count = Signed(4);
        if (Count < 0 || Count > 64) throw Error("Field-path depth bound");
        J Parts = J::Array(); for (std::int64_t Index = 0; Index < Count; ++Index) Parts.Items.emplace_back(Name(false));
        const auto Index = std::int32_t(Signed(4)); J Owner = Reference(Index);
        if ((Index && Owner.Kind != J::K::String) || (Count && !Index)) throw Error("Invalid field-path owner");
        for (const auto& Part : Parts.Items) if (Part.S() == "None") throw Error("Legacy None field path is unsupported");
        Runtime = Before + 8;
        References.Items.push_back(J::Object({{"kind", "field"}, {"owner_index", J(int(Index))}, {"owner", Owner}, {"path", Parts}}));
        return J::Object({{"owner_index", J(int(Index))}, {"owner", Owner}, {"path", Parts}});
    }
    std::string String(bool Wide)
    {
        std::string Out; std::size_t Bytes = 0;
        while (true)
        {
            auto Code = std::uint32_t(Unsigned(Wide ? 2 : 1)); if (!Code) break;
            Bytes += Wide ? 2 : 1; if (Bytes > 65536) throw Error("String literal exceeds bound");
            if (Wide && Code >= 0xd800 && Code <= 0xdbff)
            {
                const auto Low = std::uint32_t(Unsigned(2)); Bytes += 2;
                if (Bytes > 65536 || Low < 0xdc00 || Low > 0xdfff) throw Error("Invalid Unicode string literal");
                Code = 0x10000 + ((Code - 0xd800) << 10) + Low - 0xdc00;
            }
            else if (Wide && Code >= 0xdc00 && Code <= 0xdfff) throw Error("Invalid Unicode string literal");
            Material::JsonDetail::Utf8(Out, Code);
        }
        return Out;
    }
    void Target(J& Node, const std::string& Label)
    {
        const auto Value = Unsigned(4); Node["operands"][Label] = J::Object({{"value", J(Value)}, {"mode", "absolute"}});
        Targets.Items.push_back(J::Object({{"source", Node["runtime_offset"]}, {"operand", Label}, {"target", J(Value)}}));
    }
    static bool Is(int Token, std::initializer_list<int> Values)
    { return std::find(Values.begin(), Values.end(), Token) != Values.end(); }
    int Expression(J Parent, std::size_t Depth, int Terminator = -1)
    {
        if (Depth >= 64 || Nodes.size() >= 100000) throw Error("Script nesting/instruction bound");
        const auto Start = Position, Begin = Runtime; const int Token = int(Unsigned(1)); const auto Found = Opcodes().find(Token);
        if (Found == Opcodes().end()) throw Error("Unsupported opcode 0x" + HexNumber(Token) + " at storage 0x" + HexNumber(Start));
        const bool End = Is(Token, {0x16,0x30,0x32,0x3A,0x3C,0x3E,0x40,0x66,0x53});
        if (End && Token != Terminator) throw Error("Unexpected " + Found->second + " at storage 0x" + HexNumber(Start));
        const auto Index = Nodes.size();
        Nodes.push_back(J::Object({{"storage_offset", J(std::uint64_t(Start))}, {"runtime_offset", J(std::uint64_t(Begin))},
            {"opcode", "EX_" + Found->second}, {"token", J(Token)}, {"parent", Parent}, {"children", J::Array()}, {"operands", J::Object()}}));
        auto& Node = Nodes[Index]; auto& Ops = Node["operands"];
        const auto Child = [&](int Stop = -1) { const auto Slot = Nodes.size(); const int Value = Expression(J(std::uint64_t(Index)), Depth + 1, Stop);
            Node["children"].Items.emplace_back(std::uint64_t(Slot)); return Value; };
        const auto Sequence = [&](int Stop) { std::size_t Count = 0; while (Child(Stop) != Stop) ++Count; return Count; };
        if (End || Is(Token, {0x0B,0x15,0x17,0x25,0x26,0x27,0x28,0x2A,0x2D,0x4A,0x4D,0x50,0x5A,0x5E})) {}
        else if (Is(Token, {0x00,0x01,0x02,0x33,0x48,0x6C})) Ops["property"] = Field();
        else if (Is(Token, {0x04,0x4E,0x4F,0x51,0x5D,0x67,0x6D})) Child();
        else if (Is(Token, {0x14,0x43,0x44,0x5C,0x5F,0x60,0x62,0x6B})) { Child(); Child(); }
        else if (Is(Token, {0x13,0x2E,0x52,0x54,0x55})) { Ops["class"] = Object(); Child(); }
        else if (Is(Token, {0x1C,0x46,0x63,0x68})) { Ops["function"] = Object(); Sequence(0x16); }
        else if (Is(Token, {0x1B,0x45})) { Ops["function_name"] = Name(); Sequence(0x16); }
        else if (Is(Token, {0x06,0x07,0x4C})) { Target(Node, "address"); if (Token == 0x07) Child(); }
        else if (Token == 0x0F) { Ops["property"] = Field(); Child(); Child(); }
        else if (Is(Token, {0x42,0x64})) { Ops["property"] = Field(); Child(); }
        else if (Is(Token, {0x12,0x19,0x1A}))
        {
            Child(); const auto Skip = Unsigned(4); Ops["skip"] = J::Object({{"value", J(Skip)}, {"mode", "relative"}});
            Ops["rvalue"] = Field(); const auto Base = Runtime;
            Targets.Items.push_back(J::Object({{"source", J(std::uint64_t(Begin))}, {"operand", "skip"}, {"target", J(std::uint64_t(Base + Skip))}}));
            Child(); if (Runtime != Base + Skip) throw Error("Context skip does not cover its expression");
        }
        else if (Token == 0x09) { Ops["line"] = J(Unsigned(2)); Ops["debug"] = J(Unsigned(1)); Child(); }
        else if (Token == 0x18)
        {
            const auto Skip = Unsigned(4), Base = Runtime; Child(); Ops["skip"] = J::Object({{"value", J(Skip)}, {"mode", "relative"}});
            if (Runtime != Base + Skip) throw Error("Skip size mismatch");
            Targets.Items.push_back(J::Object({{"source", J(std::uint64_t(Begin))}, {"operand", "skip"}, {"target", J(std::uint64_t(Base + Skip))}}));
        }
        else if (Token == 0x38) { const auto Cast = Unsigned(1); if (!Is(int(Cast), {0x46,0x47,0x49})) throw Error("Unsupported primitive cast"); Ops["cast"] = J(Cast); Child(); }
        else if (Is(Token, {0x1D,0x1E,0x24,0x2C,0x35,0x36,0x5B}))
            Ops["value"] = Token == 0x1E ? Float() : Token == 0x1D ? J(Signed(4)) : Token == 0x35 ? J(Signed(8)) : J(Unsigned(Token == 0x36 ? 8 : Token == 0x5B ? 4 : 1));
        else if (Is(Token, {0x1F,0x34})) Ops["value"] = String(Token == 0x34);
        else if (Token == 0x20) Ops["object"] = Object();
        else if (Is(Token, {0x21,0x4B})) Ops["name"] = Name();
        else if (Is(Token, {0x22,0x23,0x2B})) { Ops["value"] = J::Array(); for (int Count = 0; Count < (Token == 0x2B ? 10 : 3); ++Count) Ops["value"].Items.push_back(Float()); }
        else if (Token == 0x29)
        {
            const auto Kind = Unsigned(1); Ops["literal_type"] = J(Kind); if (Kind > 4) throw Error("Unsupported text literal type");
            if (Kind == 4) Ops["table"] = Object(); const int Counts[] = {0,3,1,1,2};
            for (int Count = 0; Count < Counts[Kind]; ++Count) if (!Is(Child(), {0x1F,0x34})) throw Error("Text literal requires string expressions");
        }
        else if (Token == 0x2F)
        { Ops["struct"] = Object(); const auto Size = Signed(4); if (Size < 0 || Size > 1024 * 1024) throw Error("Invalid struct size"); Ops["struct_size"] = J(Size); Sequence(0x30); }
        else if (Token == 0x31) { Child(); Sequence(0x32); }
        else if (Is(Token, {0x39,0x3B,0x3D,0x3F,0x65}))
        {
            if (Is(Token, {0x39,0x3B})) Child(); else { Ops["inner"] = Field(); if (Token == 0x3F) Ops["value_property"] = Field(); }
            const auto Count = Signed(4); if (Count < 0 || Count > 100000) throw Error("Invalid container count"); Ops["count"] = J(Count);
            const int Stop = Token == 0x39 ? 0x3A : Token == 0x3B ? 0x3C : Token == 0x3D ? 0x3E : Token == 0x3F ? 0x40 : 0x66;
            if (Sequence(Stop) != std::uint64_t(Count) * (Is(Token, {0x3B,0x3F}) ? 2 : 1)) throw Error("Container count does not match expression count");
        }
        else if (Token == 0x61) { Ops["name"] = Name(); Child(); Child(); }
        else if (Token == 0x69)
        {
            const auto Count = Unsigned(2); Ops["cases"] = J(Count); Target(Node, "end"); Child();
            for (std::uint64_t Number = 0; Number < Count; ++Number) { Child(); Target(Node, "next_case_" + std::to_string(Number)); Child(); } Child();
        }
        else throw Error("Opcode grammar not implemented: " + Node["opcode"].S());
        Node["storage_end"] = J(std::uint64_t(Position)); Node["runtime_end"] = J(std::uint64_t(Runtime)); return Token;
    }
public:
    BytecodeDecoder(const std::vector<std::uint8_t>& Bytes, const std::vector<std::string>& InNames,
        std::function<J(std::int32_t)> Resolve) : Data(Bytes), Names(InNames), Reference(std::move(Resolve))
    { if (Data.size() > 1024 * 1024) throw Error("Script exceeds byte bound or is not bytes"); }
    J Decode(std::int64_t ExpectedRuntime = -1)
    {
        J Roots = J::Array();
        while (Position < Data.size())
        { Roots.Items.emplace_back(std::uint64_t(Nodes.size())); const auto Token = Expression(J(), 0, 0x53);
            if (Token == 0x53 && Position != Data.size()) throw Error("Bytes after EndOfScript"); }
        if (!Data.empty() && (Roots.Items.empty() || Nodes[std::size_t(Roots.Items.back().U())]["token"].I() != 0x53)) throw Error("Missing final EndOfScript");
        if (ExpectedRuntime >= 0 && std::uint64_t(ExpectedRuntime) != Runtime) throw Error("Expanded runtime size mismatch: " + std::to_string(Runtime) + " != " + std::to_string(ExpectedRuntime));
        std::map<std::uint64_t, std::size_t> ByRuntime;
        for (std::size_t Index = 0; Index < Nodes.size(); ++Index) ByRuntime.emplace(Nodes[Index]["runtime_offset"].U(), Index);
        for (auto& Edge : Targets.Items)
        {
            const auto TargetOffset = Edge["target"].U(); const auto Found = ByRuntime.find(TargetOffset);
            if (Found != ByRuntime.end()) { Edge["storage_target"] = Nodes[Found->second]["storage_offset"]; continue; }
            if (Edge["operand"].S() == "skip" && TargetOffset == Runtime) { Edge["storage_target"] = J(std::uint64_t(Position)); continue; }
            const auto& Source = Nodes[ByRuntime.at(Edge["source"].U())]; const int Token = int(Source["token"].I()); const auto ChildIndex = Is(Token, {0x12,0x19,0x1A}) ? 1u : 0u;
            if (Edge["operand"].S() == "skip" && Is(Token, {0x12,0x18,0x19,0x1A}) &&
                Source["operands"]["skip"]["mode"].S() == "relative" && TargetOffset == Source["runtime_end"].U() &&
                Nodes[std::size_t(Source["children"][ChildIndex].U())]["runtime_offset"].U() + Source["operands"]["skip"]["value"].U() == TargetOffset)
            { Edge["storage_target"] = Source["storage_end"]; Edge["target_scope"] = "expression-continuation"; continue; }
            throw Error("Target 0x" + HexNumber(TargetOffset) + " is not an instruction boundary");
        }
        J Instructions = J::Array(), Dynamic = J::Array();
        for (const auto& Node : Nodes)
        { Instructions.Items.push_back(Node); if (Is(int(Node["token"].I()), {0x4D,0x4E,0x4F})) Dynamic.Items.push_back(J::Object({{"source", Node["runtime_offset"]}, {"kind", Node["opcode"]}})); }
        return J::Object({{"format", "p4-kismet-inspection"}, {"version", 1}, {"storage_bytes", J(std::uint64_t(Data.size()))},
            {"runtime_bytes", J(std::uint64_t(Runtime))}, {"sha256", CustomMaterial::Hex(CustomMaterial::Sha256(Data))},
            {"instructions", Instructions}, {"roots", Roots}, {"targets", Targets}, {"references", References}, {"dynamic_control_flow", Dynamic},
            {"evidence", "serialized-instruction grammar; no execution or editor-graph recovery"}});
    }
};
inline J DecodeBytecode(const std::vector<std::uint8_t>& Data, const FPackageInventory& Package, std::int64_t ExpectedRuntime)
{
    return BytecodeDecoder(Data, Package.Names, [&](std::int32_t Index) {
        std::string Path; if (!Package.Reference(Index, Path)) throw Error("invalid-script-package-index"); return Index ? J(Path) : J();
    }).Decode(ExpectedRuntime);
}
}} // namespace P4MapKit::Blueprint

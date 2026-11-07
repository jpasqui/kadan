// Narrow original LevelScript proof: BeginPlay calls an ubergraph entry that
// immediately returns. This does not admit arbitrary empty-looking functions.
#pragma once
#include "blueprint_class.h"
#include "level_native.h"
#include <map>

namespace P4MapKit { namespace Blueprint {
// A compile in the running editor adds trace instructions that do nothing, and a cook from the command line
// does not. A script is the same with or without them. Each takes one byte.
inline bool IsLevelTrace(const J& Node)
{ return Node["token"].I() == 0x5A || Node["token"].I() == 0x5E; }
inline std::int64_t LevelTraces(const J& Script)
{
    std::int64_t Count = 0;
    for (const auto& Node : Script["instructions"].Items) Count += IsLevelTrace(Node) ? 1 : 0;
    return Count;
}
inline std::map<std::uint64_t, std::uint64_t> LevelIndicesWithoutTraces(const J& Script)
{
    std::map<std::uint64_t, std::uint64_t> Out;
    const auto& Nodes = Script["instructions"].Items;
    for (std::size_t Index = 0; Index < Nodes.size(); ++Index)
        if (!IsLevelTrace(Nodes[Index])) { const auto Next = std::uint64_t(Out.size()); Out[std::uint64_t(Index)] = Next; }
    return Out;
}
inline J EmptyLevelShape(const J& Script)
{
    const auto Indices = LevelIndicesWithoutTraces(Script);
    J Out = J::Array();
    for (const auto& Node : Script["instructions"].Items)
    {
        if (IsLevelTrace(Node)) continue;
        J Children = J::Array();
        for (const auto& Child : Node["children"].Items) Children.Items.emplace_back(Indices.at(Child.U()));
        Out.Items.push_back(J::Object({{"token", Node["token"]}, {"opcode", Node["opcode"]},
            {"parent", Node["parent"].Kind == J::K::Null ? J() : J(Indices.at(Node["parent"].U()))}, {"children", Children}, {"operands", Node["operands"]},
            {"storage", J(Node["storage_end"].U() - Node["storage_offset"].U())}, {"runtime", J(Node["runtime_end"].U() - Node["runtime_offset"].U())}}));
    }
    return Out;
}
inline J EmptyLevelRoots(const J& Script)
{
    const auto Indices = LevelIndicesWithoutTraces(Script);
    J Out = J::Array();
    for (const auto& Root : Script["roots"].Items)
        if (Indices.count(Root.U())) Out.Items.emplace_back(Indices.at(Root.U()));
    return Out;
}
inline bool IsProvenEmptyLevelClass(const J& Document, std::string& Why)
{
    try
    {
        if (Document.Has("problems") && !Document["problems"].Items.empty()) throw Error("original level class has reader problems");
        const auto& Class = Document["class"]; const auto Name = Class["name"].S();
        if (Class["super"].S() != "/Script/Engine.LevelScriptActor" || !Class["interfaces"].Items.empty() || Class["bytecode_size"].I() != 0 || !Class["cooked"].B()) throw Error("different level class boundary");
        const auto& Properties = Class["properties"];
        if (Properties.Items.size() != 1 || Properties.Items[0].Dump() != J::Object({{"name","UberGraphFrame"},{"type","StructProperty"},{"array_dim",1},{"element_size",16},{"flags","0x202000"},{"struct","/Script/Engine.PointerToUberGraphFrame"}}).Dump()) throw Error("level class has authored properties");
        const auto& Functions = Document["functions"]; if (Functions.Fields.size() != 2 || !Functions.Has("ReceiveBeginPlay")) throw Error("level class has additional functions");
        std::string GraphName;
        for (const auto& Key : Functions.Keys()) if (Key != "ReceiveBeginPlay") GraphName = Key;
        if (GraphName.find("ExecuteUbergraph_") != 0) throw Error("unknown level ubergraph function");
        const auto GraphPath = Name + '.' + GraphName; const auto& Graph = Functions[GraphName]; const auto& Begin = Functions["ReceiveBeginPlay"];
        if (Graph["outer"].S() != Name || Begin["outer"].S() != Name || Graph["super"].Kind != J::K::Null || Begin["super"].S() != "/Script/Engine.Actor.ReceiveBeginPlay") throw Error("different level function owner/super");
        if (Graph["flags"].U() != 32769 || Begin["flags"].U() != 134744064 || !Graph["children"].Items.empty() || !Begin["children"].Items.empty() ||
            Graph["event_graph_function"].Kind != J::K::Null || Begin["event_graph_function"].Kind != J::K::Null || Graph["event_graph_call_offset"].I() || Begin["event_graph_call_offset"].I()) throw Error("different level function tail");
        if (!Begin["properties"].Items.empty() || Graph["properties"].Items.size() != 1 || Graph["properties"].Items[0].Dump() != J::Object({{"name","EntryPoint"},{"type","IntProperty"},{"array_dim",1},{"element_size",4},{"flags","0x94"}}).Dump()) throw Error("different level function parameters");
        const auto FunctionMap = J::Object({{GraphName,GraphPath},{"ReceiveBeginPlay",Name + ".ReceiveBeginPlay"}});
        if (Class["function_map"].Dump() != FunctionMap.Dump()) throw Error("different level function map");
        std::multiset<std::string> Children;
        for (const auto& Child : Class["children"].Items) Children.insert(Child.S());
        if (Children != std::multiset<std::string>{GraphPath,Name + ".ReceiveBeginPlay"}) throw Error("different level class children");
        if (Class.Has("values"))
        { const auto& Values = Class["values"]; if (Values.Kind != J::K::Object || Values.Fields.size() != 1 || !Values.Has("UberGraphFunction") || Values["UberGraphFunction"].S() != GraphPath) throw Error("different stored level class values"); }
        // Construct the complete allowed scripts with the actual package index,
        // field index and names. Decode them through the same bounded grammar.
        const auto GraphIndex = Graph["index"].I(); if (GraphIndex < 1 || GraphIndex > INT32_MAX) throw Error("invalid level function index");
        const auto Put32 = [](std::vector<std::uint8_t>& Out, std::int64_t Value) { for (int N = 0; N < 4; ++N) Out.push_back(std::uint8_t(std::uint64_t(Value) >> (N * 8))); };
        std::vector<std::uint8_t> ExpectedGraph{0x4e,0x00}; Put32(ExpectedGraph,1); Put32(ExpectedGraph,0); Put32(ExpectedGraph,0); Put32(ExpectedGraph,GraphIndex); ExpectedGraph.insert(ExpectedGraph.end(), {0x04,0x0b,0x53});
        std::vector<std::uint8_t> ExpectedBegin{0x46}; Put32(ExpectedBegin,GraphIndex); ExpectedBegin.push_back(0x1d); Put32(ExpectedBegin,10); ExpectedBegin.insert(ExpectedBegin.end(), {0x16,0x04,0x0b,0x53});
        const std::vector<std::string> Names{"EntryPoint"};
        const auto Resolve = [&](std::int32_t Index) -> J { if (Index != GraphIndex) throw Error("unknown expected level reference"); return GraphPath; };
        const auto GraphScript = BytecodeDecoder(ExpectedGraph,Names,Resolve).Decode(13), BeginScript = BytecodeDecoder(ExpectedBegin,Names,Resolve).Decode(18);
        const auto Different = [](const char* What, const std::string& Found, const std::string& Wanted)
        { if (Found != Wanted) throw Error(std::string("level scripts do more than the original empty BeginPlay wrapper: ") + What + " is " + Found + ", the wrapper's is " + Wanted); };
        Different("the event graph's size", std::to_string(Graph["bytecode_size"].I() - LevelTraces(Graph["script"])) + "/" + std::to_string(Graph["script_size"].I() - LevelTraces(Graph["script"])), "13/21");
        Different("BeginPlay's size", std::to_string(Begin["bytecode_size"].I() - LevelTraces(Begin["script"])) + "/" + std::to_string(Begin["script_size"].I() - LevelTraces(Begin["script"])), "18/14");
        Different("the event graph", EmptyLevelShape(Graph["script"]).Dump(), EmptyLevelShape(GraphScript).Dump());
        Different("BeginPlay", EmptyLevelShape(Begin["script"]).Dump(), EmptyLevelShape(BeginScript).Dump());
        Different("the event graph's roots", EmptyLevelRoots(Graph["script"]).Dump(), "[0,2,4]");
        Different("BeginPlay's roots", EmptyLevelRoots(Begin["script"]).Dump(), "[0,3,5]");
        Different("the event graph's jumps", Graph["script"]["targets"].Dump(), "[]");
        Different("BeginPlay's jumps", Begin["script"]["targets"].Dump(), "[]");
        Why.clear(); return true;
    }
    catch (const std::exception& Failure) { Why = Failure.what(); return false; }
}
inline bool IsProvenEmptyLevelClass(const FPackageInventory& Package, const std::vector<std::uint8_t>& Body, std::string& Why, std::int32_t RequestedClass = 0)
{
    try
    {
        auto Document = ReadClassStructure(Package,Body);
        if (Document["class"].Kind != J::K::Object) throw Error("no generated level class");
        const auto ClassIndex = Document["class"]["index"].U(); if (!ClassIndex || ClassIndex > Package.Exports.size() || (RequestedClass && ClassIndex != std::uint64_t(RequestedClass))) throw Error("different requested generated level class index");
        const auto& Export = Package.Exports[std::size_t(ClassIndex-1)];
        const auto Tags = ReadPropertyEnvelopes(Body,Package.Names,Export.Offset,Export.Offset+Export.Size);
        if (!Tags.Error.Code.empty() || Tags.Properties.size() != 1) throw Error("different level class tagged envelope");
        const auto& Tag = Tags.Properties[0];
        if (Tag.Name != "UberGraphFunction" || Tag.Type != "ObjectProperty" || Tag.Size != 4 || Tag.ArrayIndex || Tag.HasGuid) throw Error("different level class tagged value");
        FByteReader Reader(Body,Tag.PayloadOffset,Tag.PayloadOffset+Tag.Size); std::int32_t Index = 0; std::string Ref;
        if (!Reader.I32(Index) || !Package.Reference(Index,Ref)) throw Error("invalid level ubergraph value"); Document["class"]["values"] = J::Object({{"UberGraphFunction",Ref}});
        // The original CDO contains only the compiler frame's eight stored
        // bytes. They are capture bookkeeping, not a live pointer or a default
        // to replay. Any other stored CDO member refuses this narrow proof.
        std::size_t Defaults = 0;
        for (const auto& Default : Package.Exports)
        {
            std::string Path; if (!Package.Reference(std::int32_t(&Default-Package.Exports.data()+1),Path)) throw Error("invalid level CDO reference");
            if (Path != Document["class"]["class_default_object"].S()) continue;
            ++Defaults; if (Default.Class != std::int32_t(ClassIndex)) throw Error("different level CDO class");
            const auto Stored = ReadPropertyEnvelopes(Body,Package.Names,Default.Offset,Default.Offset+Default.Size);
            if (!Stored.Error.Code.empty() || Stored.End != Default.Offset+Default.Size || Stored.Properties.size() != 1) throw Error("different stored level CDO values");
            const auto& Frame = Stored.Properties[0];
            if (Frame.Name != "UberGraphFrame" || Frame.Type != "StructProperty" || Frame.Detail != std::vector<std::string>{"PointerToUberGraphFrame"} || Frame.Size != 8 || Frame.ArrayIndex || Frame.HasGuid) throw Error("level CDO has authored defaults");
        }
        if (Defaults != 1) throw Error("missing or ambiguous level CDO");
        return IsProvenEmptyLevelClass(Document,Why);
    }
    catch (const std::exception& Failure) { Why = Failure.what(); return false; }
}
inline bool IsProvenEmptyLevelClass(const FPackageInventory& Package, const std::vector<std::uint8_t>& Body, std::int32_t RequestedClass, std::string& Why)
{ if (RequestedClass <= 0) { Why="invalid requested level class index"; return false; } return IsProvenEmptyLevelClass(Package,Body,Why,RequestedClass); }
// Adapter for the older scalar-only level inventory. It never changes that
// reader's refusal policy: only this already-proven compiler frame is skipped.
inline FGeneratedClassNative ReadProvenEmptyGeneratedClassNative(const FPackageInventory& Package, const std::vector<std::uint8_t>& Body, std::int32_t ClassIndex)
{
    FGeneratedClassNative Out; std::string Why;
    if (!IsProvenEmptyLevelClass(Package,Body,ClassIndex,Why)) { Out.Error={0,"not-proven-empty-level-class: "+Why}; return Out; }
    const auto& Export=Package.Exports[std::size_t(ClassIndex-1)];
    const auto Tags=ReadPropertyEnvelopes(Body,Package.Names,Export.Offset,Export.Offset+Export.Size);
    FByteReader Reader(Body,Tags.End,Export.Offset+Export.Size);
    const auto Check=[&](bool Good) { if (!Good) throw Error(Reader.Error.Code); };
    try
    {
        using namespace LevelNative;
        Check(ObjectId(Reader)); Check(Reader.I32(Out.Super)); Check(References(Reader,Out.Children));
        std::size_t Count=0; Check(Reader.Count(Count)); if (Count!=1) throw Error("different proven compiler frame count");
        std::string Kind,Name,Notify; std::uint64_t Flags=0,Small=0; std::uint32_t ObjectFlags=0; std::int32_t Dim=0,Size=0,Struct=0;
        Check(Reader.Name(Package.Names,Kind)); Check(Reader.Name(Package.Names,Name)); Check(Reader.U32(ObjectFlags)); Check(Reader.I32(Dim)); Check(Reader.I32(Size));
        Check(Reader.Unsigned(8,Flags)); Check(Reader.Unsigned(2,Small)); Check(Reader.Name(Package.Names,Notify)); Check(Reader.Unsigned(1,Small)); Check(Reader.I32(Struct));
        std::string StructPath; if (!Package.Reference(Struct,StructPath) || Kind!="StructProperty" || Name!="UberGraphFrame" || Dim!=1 || Size!=16 || Flags!=0x202000 || StructPath!="/Script/Engine.PointerToUberGraphFrame") throw Error("different proven compiler frame record");
        std::int32_t Runtime=0,Storage=0; Check(Reader.I32(Runtime)); Check(Reader.I32(Storage)); if (Runtime || Storage) throw Error("proven class unexpectedly holds scripts"); Out.ScriptBytes=std::size_t(Storage);
        Check(Reader.Count(Count));
        for (std::size_t N=0;N<Count;++N) { std::string Function; std::int32_t Index=0; Check(Reader.Name(Package.Names,Function)); Check(Reader.I32(Index)); Out.Functions.emplace_back(Function,Index); }
        Check(Reader.U32(Out.ClassFlags)); Check(Reader.I32(Out.Within)); Check(Reader.Name(Package.Names,Out.ConfigName)); Check(Reader.I32(Out.GeneratedBy)); Check(Reader.Count(Out.Interfaces));
        if (Out.Interfaces) throw Error("proven class unexpectedly has interfaces"); bool Dummy=false; Check(Flag(Reader,Dummy)); Check(Reader.Name(Package.Names,Name)); Check(Flag(Reader,Out.Cooked)); Check(Reader.I32(Out.DefaultObject)); Check(Done(Reader,Out.End));
        return Out;
    }
    catch(const std::exception& Failure) { Out.Error={Reader.Tell(),"proven-empty-class-adapter: "+std::string(Failure.what())}; return Out; }
}
}} // namespace P4MapKit::Blueprint

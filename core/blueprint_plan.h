// Exact B3 plan-file boundary. This is a native representation and validator,
// not a claim that a recovered graph has been imported or recompiled.
#pragma once
#include "blueprint_bytecode.h"

namespace P4MapKit { namespace Blueprint {
inline const std::set<std::string>& PlanEvidence()
{ static const std::set<std::string> Values{"class_name","skipped_variables","components_not_placed","class_values_not_handled","shell","functions","other_objects","problems","code","code_value_only","function_flags","parameters","graphs_skipped","node_variables"}; return Values; }
inline const std::set<std::string>& InsideEvidence()
{ static const std::set<std::string> Values{"unread","game_flags","template","template_class","node_extra","problem","parent_owner","entry","notes","blueprint"}; return Values; }
inline const std::set<std::string>& GameNames()
{ static const std::set<std::string> Values{"defaults","objects","payload","properties"}; return Values; }
inline void CheckPlanNumbers(const J& Value, const std::string& Path = "", std::size_t Depth = 0)
{
    if (Depth >= 128) throw Error("plan nesting budget");
    if (Value.Kind == J::K::Number && Value.Text.find_first_of(".eE") != std::string::npos) throw Error("a decimal number in the plan at " + Path);
    for (const auto& Key : Value.Keys()) CheckPlanNumbers(Value[Key], Path + '.' + Key, Depth + 1);
    for (std::size_t N = 0; N < Value.Items.size(); ++N) CheckPlanNumbers(Value.Items[N], Path + '[' + std::to_string(N) + ']', Depth + 1);
}
inline std::string PlanText(const J& Value) { CheckPlanNumbers(Value); return Value.Dump(); }
inline J SplitWalk(const J& Value, J& Path, J& Inside, std::size_t Depth)
{
    if (Depth >= 128) throw Error("plan nesting budget");
    if (Value.Kind == J::K::Object)
    {
        J Out = J::Object();
        for (const auto& Key : Value.Keys())
        {
            const auto& Item = Value[Key];
            if (!Path.Items.empty() && InsideEvidence().count(Key))
            { if (Item.Kind != J::K::Null && !(Item.Kind == J::K::Array && Item.Items.empty())) Inside.Items.push_back(J::Array({Path, Key, Item})); }
            else if (GameNames().count(Key)) Out[Key] = Item;
            else { Path.Items.push_back(Key); Out[Key] = SplitWalk(Item, Path, Inside, Depth + 1); Path.Items.pop_back(); }
        }
        return Out;
    }
    if (Value.Kind == J::K::Array)
    { J Out = J::Array(); Out.Items.reserve(Value.Items.size()); for (std::size_t N = 0; N < Value.Items.size(); ++N) { Path.Items.push_back(J(std::uint64_t(N))); Out.Items.push_back(SplitWalk(Value.Items[N], Path, Inside, Depth + 1)); Path.Items.pop_back(); } return Out; }
    return Value;
}
inline J SplitPlan(const J& Plan)
{
    if (Plan.Kind != J::K::Object || !Plan.Has("package")) throw Error("plan package required");
    J Part = J::Object({{"format", "p4-blueprint-plan"}, {"version", 1}}), Inside = J::Array(), Top = J::Object();
    for (const auto& Key : Plan.Keys())
    { if (PlanEvidence().count(Key)) Top[Key] = Plan[Key]; else { auto Path=J::Array({Key}); Part[Key] = SplitWalk(Plan[Key],Path,Inside,0); } }
    J Result=J::Object(),Evidence=J::Object({{"format","p4-blueprint-evidence"},{"version",1},{"package",Plan["package"]}});
    Evidence["top"]=std::move(Top); Evidence["inside"]=std::move(Inside); Result["import"]=std::move(Part); Result["evidence"]=std::move(Evidence); return Result;
}
inline J JoinPlan(J Part, const J& Evidence)
{
    if (Part.Kind != J::K::Object || Evidence.Kind != J::K::Object || !Part.Has("format") || !Part.Has("version") || !Evidence.Has("format") || !Evidence.Has("version") ||
        Part["format"].S() != "p4-blueprint-plan" || Part["version"].I() != 1 || Evidence["format"].S() != "p4-blueprint-evidence" || Evidence["version"].I() != 1) throw Error("not a Blueprint plan/evidence pair of version 1");
    if (Part["package"].S() != Evidence["package"].S()) throw Error("plan/evidence package mismatch");
    J Out=std::move(Part); Out.Fields.erase("format"); Out.Fields.erase("version"); Out.Order.erase(std::remove(Out.Order.begin(),Out.Order.end(),"format"),Out.Order.end()); Out.Order.erase(std::remove(Out.Order.begin(),Out.Order.end(),"version"),Out.Order.end());
    if (Evidence["top"].Kind != J::K::Object || Evidence["inside"].Kind != J::K::Array) throw Error("invalid plan evidence schema");
    for (const auto& Key : Evidence["top"].Keys())
    { if (!PlanEvidence().count(Key) || Out.Has(Key)) throw Error("invalid top evidence member"); Out[Key] = Evidence["top"][Key]; }
    std::set<std::string> Seen;
    for (const auto& Record : Evidence["inside"].Items)
    {
        if (Record.Kind != J::K::Array || Record.Items.size() != 3 || Record[std::size_t(0)].Kind != J::K::Array) throw Error("invalid inside evidence record");
        const auto& Path = Record[std::size_t(0)]; const auto Key = Record[std::size_t(1)].S();
        if (Path.Items.empty() || !InsideEvidence().count(Key) || !Seen.insert(Path.Dump() + ':' + Key).second) throw Error("invalid inside evidence member");
        J* Current = &Out;
        for (const auto& Step : Path.Items)
        {
            if (Step.Kind == J::K::String) { if (Current->Kind != J::K::Object || !Current->Has(Step.S())) throw Error("invalid evidence object path"); Current = &Current->Fields.at(Step.S()); }
            else { const auto Index = Step.U(); if (Current->Kind != J::K::Array || Index >= Current->Items.size()) throw Error("invalid evidence array path"); Current = &Current->Items[std::size_t(Index)]; }
        }
        if (Current->Kind != J::K::Object) throw Error("evidence record is not an object"); (*Current)[Key] = Record[std::size_t(2)];
    }
    CheckPlanNumbers(Out); return Out;
}
inline std::string PlanFlags(std::uint64_t Flags) { return "0x" + HexNumber(Flags, true); }
inline J NodeVariables(const J& Functions)
{
    J Out = J::Object();
    for (const auto& Name : Functions.Keys())
    {
        J Counts = J::Object();
        for (const auto& Property : Functions[Name]["properties"].Items)
        {
            auto Key = Property["name"].S(); if (Key.find("K2Node_") != 0) continue;
            const auto Last = Key.rfind('_');
            if (Last != std::string::npos && Last + 1 < Key.size() && std::all_of(Key.begin() + Last + 1, Key.end(), [](char C) { return C >= '0' && C <= '9'; })) Key.resize(Last);
            Counts[Key] = Counts.Has(Key) ? J(Counts[Key].I() + 1) : J(1);
        }
        if (!Counts.Fields.empty()) Out[Name] = Counts;
    }
    return Out;
}
inline J PinType(const J& Property, std::size_t Depth = 0)
{
    if (Depth >= 24) throw Error("pin type nesting budget"); const auto Kind = Property["type"].S();
    static const std::map<std::string,std::string> Simple{{"BoolProperty","bool"},{"IntProperty","int"},{"Int64Property","int64"},{"FloatProperty","float"},{"NameProperty","name"},{"StrProperty","string"},{"TextProperty","text"}};
    const auto Found = Simple.find(Kind); if (Found != Simple.end()) return J::Object({{"c", Found->second}});
    if (Kind == "ByteProperty" || Kind == "EnumProperty") { J Out = J::Object({{"c", "byte"}}); if (Property.Has("enum") && Property["enum"].Truth()) Out["o"] = Property["enum"]; return Out; }
    if (Kind == "StructProperty") return J::Object({{"c", "struct"}, {"o", Property["struct"]}});
    if (Kind == "ObjectProperty" || Kind == "WeakObjectProperty") { J Out = J::Object({{"c", "object"}, {"o", Property["class"]}}); if (Kind == "WeakObjectProperty") Out["weak"] = true; return Out; }
    if (Kind == "SoftObjectProperty") return J::Object({{"c", "softobject"}, {"o", Property["class"]}});
    if (Kind == "ClassProperty" || Kind == "SoftClassProperty") return J::Object({{"c", Kind == "ClassProperty" ? "class" : "softclass"}, {"o", Property["meta_class"]}});
    if (Kind == "InterfaceProperty") return J::Object({{"c", "interface"}, {"o", Property["interface"]}});
    if (Kind == "ArrayProperty" || Kind == "SetProperty") { auto Out = PinType(Property["inner"], Depth + 1); Out["k"] = Kind == "ArrayProperty" ? "array" : "set"; return Out; }
    if (Kind == "MapProperty") { auto Out = PinType(Property["key"], Depth + 1); Out["k"] = "map"; Out["v"] = PinType(Property["value"], Depth + 1); return Out; }
    if (Kind == "DelegateProperty" || Kind == "MulticastInlineDelegateProperty" || Kind == "MulticastDelegateProperty") return J::Object({{"c", Kind == "DelegateProperty" ? "delegate" : "mcdelegate"}, {"f", Property["signature"]}});
    throw Error("a " + Kind + " cannot be a Blueprint variable");
}
inline J PlanOrder(const J& Plans)
{
    if (Plans.Kind != J::K::Object) throw Error("plans object required"); J Out = J::Array(); std::set<std::string> Visited, Active;
    std::function<void(const std::string&,std::size_t)> Visit = [&](const std::string& Package, std::size_t Depth)
    {
        if (Depth > 512) throw Error("plan dependency nesting budget"); if (Visited.count(Package) || Active.count(Package) || !Plans.Has(Package)) return;
        Active.insert(Package); const auto& Plan = Plans[Package];
        if (Plan.Has("parent") && Plan["parent"].Kind == J::K::String) { auto Parent = Plan["parent"].S(); const auto Dot = Parent.find('.'); if (Dot != std::string::npos) Parent.resize(Dot); Visit(Parent, Depth + 1); }
        if (Plan.Has("needs")) for (const auto& Needed : Plan["needs"].Items) Visit(Needed.S(), Depth + 1);
        Active.erase(Package); Visited.insert(Package); Out.Items.push_back(Package);
    };
    for (const auto& Field : Plans.Fields) Visit(Field.first, 0); return Out;
}
}} // namespace P4MapKit::Blueprint

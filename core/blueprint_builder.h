// Native cooked-package to authoring plan. Inputs are installed-user files and
// captured reflection supplied by the caller; this layer contains no local paths.
#pragma once
#include "blueprint_deep.h"
#include "blueprint_plan.h"
#include "blueprint_graph_signatures.h"
#include "blueprint_graph_pseudo.h"
#include "blueprint_graph_sequencer.h"
#include "blueprint_widget_members.h"

namespace P4MapKit
{
namespace Blueprint
{
class PlanBuilder
{
    const FPackageInventory& Package;
    const std::vector<std::uint8_t>& Body;
    const J& Doc;
    GraphSignatures* Signatures;
    const FWidgetGeneratedMemberFacts* WidgetMembers;
    ValueText Text;
    std::map<std::string, const J*> ByRef;
    std::vector<const J*> Records;
    const J* SequenceRecord = nullptr;
    static const J& Optional(const J& Value, const std::string& Key)
    {
        static const J Empty;
        return Value.Kind == J::K::Object && Value.Has(Key) ? Value[Key] : Empty;
    }
    static std::string Str(const J& Value)
    {
        return Value.Kind == J::K::String ? Value.S() : "";
    }
    static bool Starts(const std::string& Value, const std::string& Prefix)
    {
        return Value.compare(0, Prefix.size(), Prefix) == 0;
    }
    static bool Ends(const std::string& Value, const std::string& Suffix)
    {
        return Value.size() >= Suffix.size() && Value.compare(Value.size() - Suffix.size(), Suffix.size(), Suffix) == 0;
    }
    static std::string Lower(std::string Value)
    {
        for (auto& C : Value)
            if (C >= 'A' && C <= 'Z')
                C = char(C - 'A' + 'a');
        return Value;
    }
    static std::string Ref(const J& Record)
    {
        const auto Outer = Str(Record["outer"]);
        return (Outer.empty() ? "" : Outer + ".") + Record["name"].S();
    }
    const J* Find(const J& Reference) const
    {
        const auto At = ByRef.find(Str(Reference));
        return At == ByRef.end() ? nullptr : At->second;
    }
    J Full(const J& Reference) const
    {
        const auto Value = Str(Reference);
        return Value.empty() ? J() : J(Starts(Value, "/") ? Value : Package.Package + "." + Value);
    }
    J Stored(const J& Record, ValueText* Reader = nullptr)
    {
        const auto Index = Record["index"].U();
        if (!Index || Index > Package.Exports.size())
            throw Error("plan-export-index");
        return (Reader ? Reader : &Text)->Values(Package.Exports[std::size_t(Index - 1)]);
    }
    J Path(const J& Record, const J& Owner) const
    {
        J Out = J::Array();
        const J* Here = &Record;
        std::set<std::string> Seen;
        while (Here && Here != &Owner)
        {
            const auto Key = Ref(*Here);
            if (!Seen.insert(Key).second || Seen.size() > 512)
                throw Error("plan-subobject-cycle");
            Out.Items.insert(Out.Items.begin(), (*Here)["name"]);
            Here = Find((*Here)["outer"]);
        }
        return Here ? Out : J();
    }
    static void SortPaths(J& Items)
    {
        std::stable_sort(Items.Items.begin(), Items.Items.end(),
                         [](const J& A, const J& B)
                         {
                             const auto& AP = A["path"].Items;
                             const auto& BP = B["path"].Items;
                             if (AP.size() != BP.size())
                                 return AP.size() < BP.size();
                             for (std::size_t I = 0; I < AP.size(); ++I)
                                 if (AP[I].S() != BP[I].S())
                                     return AP[I].S() < BP[I].S();
                             return false;
                         });
    }
    J Subobjects(const J& OwnerRef)
    {
        const auto Owner = Find(OwnerRef);
        J Out = J::Array();
        if (!Owner)
            return Out;
        for (const auto Record : Records)
            if (Record->Has("values") && !Record->Has("class_flags"))
            {
                const auto Names = Path(*Record, *Owner);
                if (!Names.Truth())
                    continue;
                const auto Values = Stored(*Record);
                Out.Items.push_back(J::Object({{"path", Names},
                                               {"ref", Text.ObjectPath(std::int32_t((*Record)["index"].I()))},
                                               {"cls", (*Record)["cls"]},
                                               {"flags", PlanFlags((*Record)["flags"].I())},
                                               {"values", Values["held"]},
                                               {"unread", Values["unread"]}}));
            }
        SortPaths(Out);
        return Out;
    }
    J Assets() const
    {
        static const std::set<std::string> Skip{"Package",
                                                "Class",
                                                "ScriptStruct",
                                                "Enum",
                                                "Function",
                                                "BlueprintGeneratedClass",
                                                "AnimBlueprintGeneratedClass",
                                                "WidgetBlueprintGeneratedClass",
                                                "UserDefinedEnum"};
        std::map<std::string, std::string> Found;
        for (const auto& Item : Package.Imports)
            if (!Skip.count(Item.Class) && Item.Outer < 0 && !Starts(Item.Name, "Default__"))
            {
                const auto Index = std::size_t(-std::int64_t(Item.Outer) - 1);
                if (Index >= Package.Imports.size())
                    throw Error("plan-import-outer");
                const auto& Outer = Package.Imports[Index];
                if (Outer.Class == "Package" && !Starts(Outer.Name, "/Script/") &&
                    !Starts(Outer.Name, "/Engine/UnknownPackage"))
                    Found[Outer.Name + "." + Item.Name] = Item.ClassPackage + "." + Item.Class;
            }
        J Out = J::Array();
        for (const auto& Item : Found)
            Out.Items.push_back(J::Object({{"path", Item.first}, {"cls", Item.second}}));
        return Out;
    }
    J Needs() const
    {
        static const std::set<std::string> Types{"BlueprintGeneratedClass", "AnimBlueprintGeneratedClass",
                                                 "WidgetBlueprintGeneratedClass", "LevelSequence"};
        std::set<std::string> Found;
        for (const auto& Item : Package.Imports)
            if (Types.count(Item.Class) && Item.Outer < 0 && !Starts(Item.Name, "Default__"))
            {
                const auto Index = std::size_t(-std::int64_t(Item.Outer) - 1);
                if (Index >= Package.Imports.size())
                    throw Error("plan-import-outer");
                Found.insert(Package.Imports[Index].Name);
            }
        J Out = J::Array();
        for (const auto& Name : Found)
            Out.Items.push_back(Name);
        return Out;
    }
    J Components(J& Unplaced)
    {
        J Out = J::Array();
        const auto Script = Find(Optional(Doc["class"]["values"], "SimpleConstructionScript"));
        if (!Script)
        {
            Unplaced = J();
            return Out;
        }
        const auto& Values = (*Script)["values"];
        const auto Default = Optional(Values, "DefaultSceneRootNode");
        std::set<std::string> Active, Made;
        std::function<void(const J&, const J&, std::size_t)> Walk =
            [&](const J& Reference, const J& Parent, std::size_t Depth)
        {
            if (Depth > 128 || !Active.insert(Reference.S()).second)
                throw Error("plan-component-cycle");
            const auto Record = Find(Reference);
            if (!Record)
                throw Error("plan-component-reference");
            const auto& Node = (*Record)["values"];
            const auto Template = Find(Optional(Node, "ComponentTemplate"));
            J Item = J::Object({{"variable", Node["InternalVariableName"]},
                                {"cls", Full(Node["ComponentClass"])},
                                {"guid", J()},
                                {"parent", Parent},
                                {"default_scene_root", Reference.Dump() == Default.Dump()}});
            if (Optional(Node, "VariableGuid").Truth())
            {
                std::string Guid;
                for (const auto& Word : Node["VariableGuid"].Items)
                {
                    std::ostringstream Stream;
                    Stream << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << Word.U();
                    Guid += Stream.str();
                }
                Item["guid"] = Guid;
            }
            const auto ParentName = Optional(Node, "ParentComponentOrVariableName");
            if (Parent.Kind == J::K::Null && ParentName.Kind != J::K::Null && ParentName.S() != "None")
            {
                Item["parent"] = ParentName;
                Item["parent_native"] = Optional(Node, "bIsParentComponentNative").Truth();
                Item["parent_owner"] = Optional(Node, "ParentComponentOwnerClassName");
            }
            const auto Socket = Optional(Node, "AttachToName");
            if (Socket.Kind != J::K::Null && Socket.S() != "None")
                Item["socket"] = Socket;
            const auto Metadata = Optional(Node, "MetaDataArray");
            if (Metadata.Truth())
                Item["node_metadata"] = Metadata;
            static const std::set<std::string> Known{"ComponentClass",
                                                     "ComponentTemplate",
                                                     "VariableGuid",
                                                     "InternalVariableName",
                                                     "ParentComponentOrVariableName",
                                                     "bIsParentComponentNative",
                                                     "ParentComponentOwnerClassName",
                                                     "AttachToName",
                                                     "ChildNodes",
                                                     "CookedComponentInstancingData",
                                                     "MetaDataArray"};
            J Extra = J::Array();
            for (const auto& Field : Node.Fields)
                if (!Known.count(Field.first))
                    Extra.Items.push_back(Field.first);
            if (Extra.Truth())
                Item["node_extra"] = Extra;
            if (!Template)
                Item["problem"] = "no template in the package";
            else
            {
                const auto Held = Stored(*Template);
                Item["template"] = (*Template)["name"];
                Item["values"] = Held["held"];
                Item["unread"] = Held["unread"];
                Item["subobjects"] = Subobjects(Node["ComponentTemplate"]);
                if ((*Template)["cls"].Dump() != Full(Node["ComponentClass"]).Dump())
                    Item["template_class"] = (*Template)["cls"];
            }
            Made.insert(Item["variable"].S());
            Out.Items.push_back(Item);
            for (const auto& Child : Optional(Node, "ChildNodes").Items)
                Walk(Child, Node["InternalVariableName"], Depth + 1);
            Active.erase(Reference.S());
        };
        for (const auto& Reference : Optional(Values, "RootNodes").Items)
            Walk(Reference, J(), 0);
        std::set<std::string> Missing;
        for (const auto& Reference : Optional(Values, "AllNodes").Items)
        {
            const auto Record = Find(Reference);
            if (!Record)
                throw Error("plan-component-reference");
            const auto Name = (*Record)["values"]["InternalVariableName"].S();
            if (!Made.count(Name))
                Missing.insert(Name);
        }
        Unplaced = J::Array();
        for (const auto& Name : Missing)
            Unplaced.Items.push_back(Name);
        return Out;
    }
    // The template of the scene root a construction script keeps unused. The editor renames the template of
    // a root it removed, and the game's file keeps that name.
    J DefaultSceneRootTemplate()
    {
        const auto Script = Find(Optional(Doc["class"]["values"], "SimpleConstructionScript"));
        if (!Script)
            return J();
        const auto Node = Find(Optional((*Script)["values"], "DefaultSceneRootNode"));
        if (!Node)
            return J();
        const auto Template = Find(Optional((*Node)["values"], "ComponentTemplate"));
        return Template ? (*Template)["name"] : J();
    }
    J UnusedDefaultSceneRoot(const J& Components)
    {
        for (const auto& Component : Components.Items)
            if (Optional(Component, "default_scene_root").Truth())
                return J();
        const auto Script = Find(Optional(Doc["class"]["values"], "SimpleConstructionScript"));
        if (!Script)
            return J();
        const auto Node = Find(Optional((*Script)["values"], "DefaultSceneRootNode"));
        if (!Node)
            return J();
        const auto Template = Find(Optional((*Node)["values"], "ComponentTemplate"));
        if (!Template)
            return J();
        const auto Values = Stored(*Template), Sub = Subobjects((*Node)["values"]["ComponentTemplate"]);
        const auto Metadata = Optional((*Node)["values"], "MetaDataArray");
        // The existing name field covers an otherwise empty spare. Preserve its
        // actual source values too when it says more than the engine default.
        if (!Values["held"].Truth() && !Values["unread"].Truth() && !Sub.Truth() && !Metadata.Truth())
            return J();
        J Out = J::Object({{"variable", (*Node)["values"]["InternalVariableName"]},
                           {"cls", Full((*Template)["cls"])},
                           {"values", Values["held"]},
                           {"unread", Values["unread"]},
                           {"subobjects", Sub}});
        if (Metadata.Truth())
            Out["node_metadata"] = Metadata;
        return Out;
    }
    J Overrides()
    {
        J Out = J::Array();
        const J* Handler = nullptr;
        for (const auto& Record : Doc["objects"].Items)
            if (Ends(Record["cls"].S(), ".InheritableComponentHandler"))
            {
                Handler = &Record;
                break;
            }
        if (!Handler)
            return Out;
        for (const auto& Row : Optional((*Handler)["values"], "Records").Items)
        {
            const auto& Key = Optional(Row, "ComponentKey");
            const auto Template = Find(Optional(Row, "ComponentTemplate"));
            J Item = J::Object({{"variable", Optional(Key, "SCSVariableName")},
                                {"owner", Full(Optional(Key, "OwnerClass"))},
                                {"cls", Full(Optional(Row, "ComponentClass"))}});
            if (!Template)
                Item["problem"] = "no template in the package";
            else
            {
                const auto Values = Stored(*Template);
                Item["values"] = Values["held"];
                Item["unread"] = Values["unread"];
                Item["subobjects"] = Subobjects(Row["ComponentTemplate"]);
            }
            Out.Items.push_back(Item);
        }
        return Out;
    }
    // Add-component node templates are a separate class array, not SCS nodes.
    // Keep their exact names/order, sparse values and nested objects. Invalid
    // references remain explicit refusals rather than becoming empty templates.
    J ComponentTemplates()
    {
        J Out = J::Array();
        const auto& Held = Optional(Doc["class"]["values"], "ComponentTemplates");
        if (Held.Kind == J::K::Null)
            return Out;
        if (Held.Kind != J::K::Array)
            throw Error("plan-component-templates-array");
        std::set<std::string> Seen;
        for (const auto& Reference : Held.Items)
        {
            const auto Record = Find(Reference);
            J Item = J::Object({{"source", Full(Reference)}});
            if (!Record || Str((*Record)["outer"]) != Doc["class"]["name"].S() || !Seen.insert(Reference.S()).second)
                Item["problem"] = "component template is absent, repeated or outside this class";
            else
            {
                const auto Values = Stored(*Record);
                Item["name"] = (*Record)["name"];
                Item["cls"] = (*Record)["cls"];
                Item["flags"] = PlanFlags((*Record)["flags"].U());
                Item["archetype"] = Full((*Record)["template"]);
                Item["values"] = Values["held"];
                Item["unread"] = Values["unread"];
                Item["subobjects"] = Subobjects(Reference);
            }
            Out.Items.push_back(Item);
        }
        return Out;
    }

public:
    using EventBuilder = std::function<J(const J&, GraphSignatures*, const J&, const std::function<J(const J&)>&)>;
    EventBuilder Events;
    PlanBuilder(const FPackageInventory& InPackage, const std::vector<std::uint8_t>& InBody, const J& Document,
                GraphSignatures* Sig = nullptr, const FWidgetGeneratedMemberFacts* Members = nullptr)
        : Package(InPackage), Body(InBody), Doc(Document), Signatures(Sig), WidgetMembers(Members),
          Text(Package, Body, {}, true)
    {
        if (WidgetMembers && &WidgetMembers->Document() != &Document)
            throw Error("widget-member-document-not-source-owned");
        Events =
            [](const J& Document, GraphSignatures* SigValue, const J& Root, const std::function<J(const J&)>& Place)
        {
            return BlueprintSequencerEvents(Document, SigValue, Root,
                                            [&](const std::vector<std::string>& Names)
                                            {
                                                J PathValue = J::Array();
                                                for (const auto& Name : Names)
                                                    PathValue.Items.push_back(Name);
                                                return Place(PathValue);
                                            });
        };
        if (Doc["class"].Truth())
            Records.push_back(&Doc["class"]);
        for (const auto& Name : Doc["functions"].Keys())
            Records.push_back(&Doc["functions"][Name]);
        for (const auto& Record : Doc["objects"].Items)
            Records.push_back(&Record);
        for (const auto Record : Records)
            if (!ByRef.emplace(Ref(*Record), Record).second)
                throw Error("plan-duplicate-reference");
        for (const auto& Record : Doc["objects"].Items)
            if (Record["outer"].Kind == J::K::Null && Record["cls"].S() == "/Script/LevelSequence.LevelSequence")
            {
                SequenceRecord = &Record;
                break;
            }
    }
    J Sequence()
    {
        if (!SequenceRecord)
            return J();
        J Objects = J::Array(), Sections = J::Array();
        for (const auto Record : Records)
            if (Record->Has("values") && !Record->Has("class_flags") &&
                !Ends((*Record)["cls"].S(), ".MovieSceneCompiledData"))
            {
                const auto Names = Path(*Record, *SequenceRecord);
                if (!Names.Truth())
                    continue;
                const auto Values = Stored(*Record);
                Objects.Items.push_back(J::Object({{"path", Names},
                                                   {"ref", Text.ObjectPath(std::int32_t((*Record)["index"].I()))},
                                                   {"cls", Full((*Record)["cls"])},
                                                   {"flags", PlanFlags((*Record)["flags"].U())},
                                                   {"values", Values["held"]},
                                                   {"unread", Values["unread"]}}));
                if (Ends((*Record)["cls"].S(), ".MovieSceneEventTriggerSection") ||
                    Ends((*Record)["cls"].S(), ".MovieSceneEventRepeaterSection"))
                    Sections.Items.push_back(Names);
            }
        SortPaths(Objects);
        std::sort(Sections.Items.begin(), Sections.Items.end(),
                  [](const J& A, const J& B) { return A.Dump() < B.Dump(); });
        const auto Values = Stored(*SequenceRecord);
        J Held = J::Array();
        for (const auto& Value : Values["held"].Items)
            if (Value["name"].S() != "CompiledData" && Value["name"].S() != "DirectorClass")
                Held.Items.push_back(Value);
        const bool Director = Doc["class"].Truth();
        if (Director && !Events)
            throw Error("plan-sequencer-events-not-connected");
        return J::Object(
            {{"name", (*SequenceRecord)["name"]},
             {"values", Held},
             {"unread", Values["unread"]},
             {"subobjects", Objects},
             {"events", Director ? Events(Doc, Signatures, *SequenceRecord, [](const J& V) { return V; }) : J::Array()},
             {"event_sections", Director ? Sections : J::Array()}});
    }
    J Widget()
    {
        const auto& Class = Doc["class"];
        const auto Top = Class["name"].S(), Blueprint = Package.Package.substr(Package.Package.find_last_of('/') + 1);
        std::set<std::string> Copies;
        for (const auto& Record : Doc["objects"].Items)
            if (Str(Record["outer"]) == Top && Ends(Record["cls"].S(), ".WidgetAnimation"))
                Copies.insert(Record["name"].S());
        const auto Rebase = [&](const std::vector<std::string>& Parts)
        {
            if (Parts.size() < 2 || Parts[0] != Top || (Parts[1] != "WidgetTree" && !Copies.count(Parts[1])))
                return Parts;
            auto Out = Parts;
            Out[0] = Blueprint;
            if (Copies.count(Out[1]) && Ends(Out[1], "_INST"))
                Out[1].resize(Out[1].size() - 5);
            return Out;
        };
        ValueText Reader(Package, Body, Rebase, true);
        J Objects = J::Array(), Animations = J::Array();
        for (const auto Record : Records)
            if (Record->Has("values") && !Record->Has("class_flags") &&
                !Ends((*Record)["cls"].S(), ".MovieSceneCompiledData"))
            {
                const auto Names = Path(*Record, Class);
                if (!Names.Truth())
                    continue;
                std::vector<std::string> Parts{Top};
                for (const auto& Name : Names.Items)
                    Parts.push_back(Name.S());
                Parts = Rebase(Parts);
                J Placed = J::Array();
                for (std::size_t I = 1; I < Parts.size(); ++I)
                    Placed.Items.push_back(Parts[I]);
                const auto Values = Stored(*Record, &Reader);
                J Held = J::Array();
                for (const auto& Value : Values["held"].Items)
                    if (!(Value["name"].S() == "CompiledData" && Ends((*Record)["cls"].S(), ".WidgetAnimation")))
                        Held.Items.push_back(Value);
                Objects.Items.push_back(J::Object({{"path", Placed},
                                                   {"ref", Reader.ObjectPath(std::int32_t((*Record)["index"].I()))},
                                                   {"cls", Full((*Record)["cls"])},
                                                   {"flags", PlanFlags((*Record)["flags"].U())},
                                                   {"values", Held},
                                                   {"unread", Values["unread"]}}));
            }
        for (const auto& RefValue : Optional(Class["values"], "Animations").Items)
            if (RefValue.Truth())
            {
                auto Name = RefValue.S();
                const auto Dot = Name.find('.');
                if (Dot == std::string::npos)
                    throw Error("plan-widget-animation-reference");
                Name = Name.substr(Dot + 1);
                if (Ends(Name, "_INST"))
                    Name.resize(Name.size() - 5);
                Animations.Items.push_back(Name);
            }
        SortPaths(Objects);
        if (!Events)
            throw Error("plan-sequencer-events-not-connected");
        auto Placed = [&](const J& Names)
        {
            std::vector<std::string> Parts{Top};
            for (const auto& Name : Names.Items)
                Parts.push_back(Name.S());
            Parts = Rebase(Parts);
            J Out = J::Array();
            for (std::size_t I = 1; I < Parts.size(); ++I)
                Out.Items.push_back(Parts[I]);
            return Out;
        };
        J Out = J::Object(
            {{"subobjects", Objects}, {"animations", Animations}, {"events", Events(Doc, Signatures, Class, Placed)}});
        if (WidgetMembers && WidgetMembers->Proof().Truth())
            Out["generated_members"] = WidgetMembers->Proof();
        return Out;
    }
    J Build()
    {
        const auto& Class = Doc["class"];
        const auto SequencePlan = Sequence();
        if (!Class.Truth() && !SequencePlan.Truth())
            return J();
        J Out = J::Object(
            {{"package", Package.Package},
             {"name", SequencePlan.Truth() ? SequencePlan["name"]
                                           : J(Package.Package.substr(Package.Package.find_last_of('/') + 1))},
             {"class_name", J()},
             {"kind", "LevelSequence"},
             {"parent", J()},
             {"class_flags", PlanFlags(0)},
             {"interfaces", J::Array()},
             {"needs", Needs()},
             {"assets", Assets()},
             {"variables", J::Array()},
             {"skipped_variables", J::Array()},
             {"components", J::Array()},
             {"components_not_placed", J()},
             {"overrides", J::Array()},
             {"class_defaults", J::Object({{"values", J::Array()}, {"unread", J::Array()}})},
             {"class_values_not_handled", J::Array()},
             {"skeleton", J()},
             {"widget", J()},
             {"sequence", SequencePlan},
             {"director", false},
             {"shell", false},
             {"functions", J::Array()},
             {"other_objects", J::Object()},
             {"problems", Doc["problems"]},
             {"code", J::Object()},
             {"code_value_only", J::Object()},
             {"function_flags", J::Object()},
             {"parameters", J::Object()},
             {"node_variables", J::Object()}});
        if (!Class.Truth())
            return Out;
        J Unplaced;
        const auto ComponentList = Components(Unplaced);
        std::set<std::string> ComponentNames, InnerNames;
        for (const auto& Component : ComponentList.Items)
            ComponentNames.insert(Component["variable"].S());
        for (const auto& Object : Doc["objects"].Items)
        {
            const auto Name = Object["name"].S();
            if (Str(Object["outer"]).find(".WidgetTree") != std::string::npos)
                InnerNames.insert(Lower(Name));
            if (Ends(Object["cls"].S(), ".WidgetAnimation"))
                InnerNames.insert(Lower(Ends(Name, "_INST") ? Name.substr(0, Name.size() - 5) : Name));
        }
        const auto Kind = Doc["kind"].S();
        const bool IsAnim = Kind == "AnimBlueprint", IsWidget = Kind == "WidgetBlueprint";
        J Variables = J::Array(), Skipped = J::Array();
        static const std::uint64_t Authored =
            0x1ULL | 0x4ULL | 0x10ULL | 0x20ULL | 0x40ULL | 0x800ULL | 0x2000ULL | 0x4000ULL | 0x10000ULL | 0x20000ULL |
            0x200000ULL | 0x1000000ULL | 0x2000000ULL | 0x10000000ULL | 0x100000000ULL | 0x200000000ULL |
            0x400000000ULL | 0x10000000000ULL | 0x20000000000ULL | 0x40000000000ULL | 0x100000000000ULL |
            0x200000000000ULL | 0x400000000000ULL | 0x800000000000ULL | 0x1000000000000ULL | 0x4000000000000ULL;
        const auto FlagValue = [](const J& Value)
        {
            const auto Text = Value.S();
            std::size_t Used = 0;
            const auto Flags = std::stoull(Text, &Used, 16);
            if (Used != Text.size())
                throw Error("plan-invalid-flags");
            return std::uint64_t(Flags);
        };
        for (const auto& Property : Class["properties"].Items)
        {
            const auto Name = Property["name"].S();
            const auto Flags = FlagValue(Property["flags"]);
            if (Name == "UberGraphFrame" || ComponentNames.count(Name) || (IsAnim && Starts(Name, "AnimGraphNode_")))
                continue;
            if (IsWidget && WidgetMembers && WidgetMembers->Covers(Property))
                continue;
            if (IsWidget && (Property["type"].S() == "StructProperty" || InnerNames.count(Lower(Name))))
            {
                Skipped.Items.push_back(J::Object({{"name", Name}, {"why", "part of the widget tree"}}));
                continue;
            }
            J Type;
            try
            {
                Type = PinType(Property);
            }
            catch (const std::exception& Failure)
            {
                Skipped.Items.push_back(J::Object({{"name", Name}, {"why", Failure.what()}}));
                continue;
            }
            for (const auto* Key : {"o", "f"})
                if (Optional(Type, Key).Truth())
                    Type[Key] = Full(Type[Key]);
            if (Optional(Type, "v").Kind == J::K::Object && Optional(Type["v"], "o").Truth())
                Type["v"]["o"] = Full(Type["v"]["o"]);
            J Item = J::Object({{"name", Name},
                                {"type", Type},
                                {"flags", PlanFlags(Flags & Authored)},
                                {"game_flags", PlanFlags(Flags)}});
            if (Optional(Property, "rep_notify").Truth())
                Item["rep_notify"] = Property["rep_notify"];
            if (Property.Has("array_dim") && Property["array_dim"].I() != 1)
            {
                Skipped.Items.push_back(J::Object({{"name", Name}, {"why", "a fixed array"}}));
                continue;
            }
            Variables.Items.push_back(Item);
        }
        J Defaults = Out["class_defaults"];
        const J* Default = nullptr;
        for (const auto& Record : Doc["objects"].Items)
            if (Record["name"].Dump() == Class["class_default_object"].Dump())
            {
                Default = &Record;
                break;
            }
        if (Default)
        {
            const auto Values = Stored(*Default);
            Defaults["unread"] = Values["unread"];
            J Held = J::Array();
            for (const auto& Value : Values["held"].Items)
                if (Value["name"].S() != "UberGraphFrame" && !(IsAnim && Starts(Value["name"].S(), "AnimGraphNode_")))
                    Held.Items.push_back(Value);
            Defaults["values"] = Held;
            Defaults["subobjects"] = Subobjects(Class["class_default_object"]);
        }
        static const std::set<std::string> Handled{"ComponentTemplates",
                                                   "SimpleConstructionScript",
                                                   "InheritableComponentHandler",
                                                   "UberGraphFramePointerProperty",
                                                   "UberGraphFunction",
                                                   "WidgetTree",
                                                   "Animations",
                                                   "bClassRequiresNativeTick"};
        J Unhandled = J::Array();
        for (const auto& Value : Class["values"].Fields)
            if (!Handled.count(Value.first))
                Unhandled.Items.push_back(Value.first);
        std::set<std::string> Covered{Str(Class["class_default_object"])};
        for (const auto& Item : ComponentList.Items)
            if (Item.Has("template"))
                Covered.insert(Item["template"].S());
        J Other = J::Object();
        for (const auto& Object : Doc["objects"].Items)
        {
            const auto ObjectClass = Object["cls"].S();
            if (Covered.count(Object["name"].S()) || Ends(ObjectClass, ".SCS_Node") ||
                Ends(ObjectClass, ".SimpleConstructionScript") || Ends(ObjectClass, ".InheritableComponentHandler"))
                continue;
            Other[ObjectClass] = Other.Has(ObjectClass) ? J(Other[ObjectClass].U() + 1) : J(1);
        }
        const auto Pure = [&](const std::string& Path)
        {
            const auto Result =
                Signatures ? Signatures->Get(Starts(Path, "/") ? Path : Package.Package + "." + Path) : J();
            return Result.Truth() && (Result[1].U() & 0x10000000) != 0;
        };
        const auto Code = BlueprintCodeAndValues(Doc["functions"], Pure);
        J Interfaces = J::Array(), Functions = J::Array(), FunctionFlags = J::Object(), Parameters = J::Object();
        for (const auto& Interface : Class["interfaces"].Items)
            Interfaces.Items.push_back(
                J::Object({{"cls", Full(Interface["cls"])}, {"blueprint", Interface["blueprint_implemented"]}}));
        for (const auto& Function : Doc["functions"].Fields)
        {
            Functions.Items.push_back(Function.first);
            FunctionFlags[Function.first] = PlanFlags(Function.second["flags"].U());
            J Props = J::Array();
            for (const auto& Property : Function.second["properties"].Items)
            {
                const auto Flags = FlagValue(Property["flags"]);
                if (Flags & 0x80)
                    Props.Items.push_back(
                        J::Array({Property["name"], PlanFlags(Flags & (0x80 | 0x2 | 0x100 | 0x400 | 0x8000000))}));
            }
            Parameters[Function.first] = Props;
        }
        Out["name"] = SequencePlan.Truth() ? J(SequencePlan["name"].S() + ":SequenceDirector") : Out["name"];
        Out["director"] = SequencePlan.Truth();
        Out["class_name"] = Class["name"];
        Out["kind"] = SequencePlan.Truth() ? J("LevelSequence") : Doc["kind"];
        Out["parent"] = Full(Class["super"]);
        Out["class_flags"] = PlanFlags(Class["class_flags"].U());
        Out["interfaces"] = Interfaces;
        Out["variables"] = Variables;
        Out["skipped_variables"] = Skipped;
        Out["components"] = ComponentList;
        Out["components_not_placed"] = Unplaced;
        Out["default_scene_root_template"] = DefaultSceneRootTemplate();
        Out["overrides"] = Overrides();
        Out["class_defaults"] = Defaults;
        Out["class_values_not_handled"] = Unhandled;
        Out["skeleton"] = Optional(Class["values"], "TargetSkeleton");
        Out["widget"] = IsWidget ? Widget() : J();
        Out["shell"] = IsWidget;
        Out["functions"] = Functions;
        Out["other_objects"] = Other;
        Out["code"] = Code[0];
        Out["code_value_only"] = Code[1];
        Out["function_flags"] = FunctionFlags;
        Out["parameters"] = Parameters;
        Out["node_variables"] = NodeVariables(Doc["functions"]);
        if (Class["values"].Has("ComponentTemplates"))
            Out["component_templates"] = ComponentTemplates();
        const auto Spare = UnusedDefaultSceneRoot(ComponentList);
        if (Spare.Truth())
            Out["unused_default_scene_root"] = Spare;
        return Out;
    }
};
}
} // namespace P4MapKit::Blueprint

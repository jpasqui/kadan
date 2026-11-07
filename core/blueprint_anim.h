// Animation graph recovery from explicitly supplied package records and value text.
#pragma once
#include "blueprint_graph_anim_inputs.h"
#include "blueprint_graph_pseudo.h"
namespace P4MapKit
{
namespace Blueprint
{
namespace AnimDetail
{
using namespace GraphDetail;
inline bool Ends(const std::string& A, const std::string& B)
{
    return A.size() >= B.size() && A.substr(A.size() - B.size()) == B;
}
inline std::int64_t Int(const J& A, const std::string& K, std::int64_t Default = -1)
{
    return A.Has(K) ? A[K].I() : Default;
}
inline std::string PyStr(const J& V)
{
    if (V.Kind == J::K::Null)
        return "None";
    if (V.Kind == J::K::String)
        return V.S();
    if (V.Kind == J::K::Boolean)
        return V.B() ? "True" : "False";
    return V.Dump();
}
inline J ArrayOr(const J& V)
{
    return V.Truth() ? V : J::Array();
}
inline J ObjectOr(const J& V)
{
    return V.Truth() ? V : J::Object();
}
inline std::string Number(double V)
{
    if (!std::isfinite(V))
        throw Error("animation-nonfinite-value");
    auto S = Float(V, 9);
    if (S.find('e') != std::string::npos)
    {
        S = Float(V, 60, true);
        while (!S.empty() && S.back() == '0')
            S.pop_back();
    }
    if (S.find('.') == std::string::npos && S.find('n') == std::string::npos)
        S += ".0";
    return S;
}
inline J UniquePins(const J& V)
{
    std::set<std::string> Names;
    for (const auto& P : V.Items)
        Names.insert(P.S());
    J Out = J::Array();
    for (const auto& N : Names)
        Out.Items.push_back(N);
    return Out;
}
inline bool Less(const J& A, const J& B)
{
    if (A.Kind == J::K::Array && B.Kind == J::K::Array)
    {
        for (std::size_t I = 0; I < std::min(A.Items.size(), B.Items.size()); ++I)
        {
            if (A[I].Dump() == B[I].Dump())
                continue;
            return Less(A[I], B[I]);
        }
        return A.Items.size() < B.Items.size();
    }
    if (A.Kind == J::K::String && B.Kind == J::K::String)
        return A.Text < B.Text;
    if (A.Kind == J::K::Number && B.Kind == J::K::Number)
        return A.D() < B.D();
    return A.Dump() < B.Dump();
}
inline void PoseLinks(const J& V, const std::string& Path, J& Out, std::size_t Depth = 0)
{
    if (Depth > 128)
        throw Error("animation-pose-depth");
    if (V.Kind == J::K::Object)
    {
        if (V.Has("LinkID") && V.Fields.size() <= 2 && (V.Fields.size() == 1 || V.Has("SourceLinkID")))
        {
            Out.Items.push_back(J::Array({Path, V["LinkID"]}));
            return;
        }
        for (const auto& K : V.Keys())
            PoseLinks(V[K], Path.empty() ? K : Path + "." + K, Out, Depth + 1);
    }
    else if (V.Kind == J::K::Array)
        for (std::size_t I = 0; I < V.Items.size(); ++I)
            PoseLinks(V[I], Path + "[" + std::to_string(I) + "]", Out, Depth + 1);
}
inline J BlendOrder(const J& Wired, const J& Loose, const J& Listed)
{
    const std::size_t Base = Wired.Items.size() + 1;
    std::size_t Count = 1;
    for (std::size_t I = 0; I < Loose.Items.size(); ++I)
    {
        if (Count > 2000000 / Base)
            throw Error("animation-blend-order-budget");
        Count *= Base;
    }
    std::set<std::string> LooseSet, ListedSet;
    for (const auto& V : Loose.Items)
        LooseSet.insert(V.S());
    for (const auto& V : Listed.Items)
        ListedSet.insert(V.S());
    for (std::size_t Combination = 0; Combination < Count; ++Combination)
    {
        std::vector<std::pair<J, std::size_t>> Places;
        auto X = Combination;
        std::vector<std::size_t> Digits(Loose.Items.size());
        for (std::size_t I = Digits.size(); I > 0; --I)
        {
            Digits[I - 1] = X % Base;
            X /= Base;
        }
        for (std::size_t I = 0; I < Digits.size(); ++I)
            Places.push_back({Loose[I], Digits[I]});
        std::stable_sort(Places.begin(), Places.end(),
                         [](const std::pair<J, std::size_t>& A, const std::pair<J, std::size_t>& B)
                         { return A.second > B.second; });
        J Order = Wired;
        for (const auto& P : Places)
            Order.Items.insert(Order.Items.begin() + std::ptrdiff_t(P.second), P.first);
        std::vector<std::string> A, B;
        std::set<std::string> InOrder;
        for (const auto& V : Order.Items)
        {
            InOrder.insert(V.S());
            if (ListedSet.count(V.S()))
                A.push_back(V.S());
        }
        for (const auto& V : Listed.Items)
            if (InOrder.count(V.S()))
                B.push_back(V.S());
        if (A != B)
            continue;
        J Left = Order;
        std::size_t At = 0;
        while (At < Left.Items.size())
        {
            if (LooseSet.count(Left[At].S()))
            {
                Left.Items[At] = Left.Items.back();
                Left.Items.pop_back();
            }
            else
                ++At;
        }
        if (Left.Dump() == Wired.Dump())
            return Order;
    }
    return {};
}
class Model
{
    const J& Doc;
    const J* Values;
    J ClassValues, Machines, Handlers;
    std::vector<J> Nodes;
    std::string Package;
    const J& Value(const std::string& Name) const
    {
        return Optional(*Values, Name);
    }
    J NameAt(std::int64_t Index) const
    {
        return Index >= 0 && std::uint64_t(Index) < Nodes.size() ? Nodes[Nodes.size() - 1 - std::size_t(Index)]["name"]
                                                                 : J();
    }
    std::string Notify(std::int64_t Index) const
    {
        const auto Listed = ArrayOr(Optional(ClassValues, "AnimNotifies"));
        if (Index < 0 || std::uint64_t(Index) >= Listed.Items.size())
            return "";
        const auto V = Optional(Listed[std::size_t(Index)], "NotifyName");
        return V.Truth() ? PyStr(V) : "";
    }
    void Getter(J& Node) const
    {
        const auto Given = ObjectOr(Optional(Node, "defaults"));
        if (Str(Optional(Node, "t")) != "call" || !Optional(Node, "pure").Truth() || !Given.Has("MachineIndex"))
            return;
        try
        {
            const auto OwnerName = NameAt(std::stoll(Given["MachineIndex"].S()));
            if (OwnerName.Kind == J::K::Null)
                return;
            const auto& Baked = Machines[std::size_t(Value(OwnerName.S())["StateMachineIndexInClass"].U())];
            J Found = J::Object({{"machine", OwnerName}});
            for (const auto* Key : {"StateIndex", "TransitionIndex"})
                if (Given.Has(Key))
                {
                    const auto Index = std::int64_t(std::stoll(Given[Key].S()));
                    if (Index < 0)
                        throw Error("negative-animation-getter-index");
                    const bool State = std::string(Key) == "StateIndex";
                    const auto& Item = Baked[State ? "States" : "Transitions"][std::size_t(Index)];
                    Found[State ? "state" : "transition"] = J(Index);
                    Found["of"] =
                        State ? Item["StateName"]
                              : J(Baked["States"][std::size_t(Item["PreviousState"].U())]["StateName"].S() + " to " +
                                  Baked["States"][std::size_t(Item["NextState"].U())]["StateName"].S());
                }
            if (!Found.Has("of"))
                Found["of"] = Baked["MachineName"];
            Node["getter"] = Found;
            J Defaults = J::Object();
            for (const auto& K : Given.Keys())
                if (!Is(K, {"MachineIndex", "StateIndex", "TransitionIndex"}))
                    Defaults[K] = Given[K];
            Node["defaults"] = Defaults;
        }
        catch (const std::exception&)
        {
            return;
        }
    }
    J EnumOf(GraphSignatures& Sig, const J& Entry) const;

public:
    std::set<std::string> ReconstructedClassFields(const J& Plan) const;
    explicit Model(const J& D)
        : Doc(D), Values(nullptr), ClassValues(ObjectOr(Optional(D["class"], "values"))),
          Machines(ArrayOr(Optional(ClassValues, "BakedStateMachines"))),
          Handlers(ArrayOr(Optional(ClassValues, "EvaluateGraphExposedInputs"))), Package(D["package"].S())
    {
        for (const auto& O : D["objects"].Items)
            if (O["name"].S() == D["class"]["class_default_object"].S())
            {
                Values = &O["values"];
                break;
            }
        if (!Values)
            throw Error("animation-class-default-object-missing");
        for (const auto& P : D["class"]["properties"].Items)
            if (P["type"].S() == "StructProperty" && Starts(P["name"].S(), "AnimGraphNode_"))
                Nodes.push_back(P);
    }
    J Links() const
    {
        J Out = J::Array();
        for (const auto& P : Nodes)
        {
            const auto Name = P["name"].S();
            J Found = J::Array();
            PoseLinks(Value(Name), "", Found);
            for (const auto& L : Found.Items)
            {
                const auto Index = L[1].I();
                Out.Items.push_back(J::Array(
                    {Name, L[0],
                     Index >= 0 && std::uint64_t(Index) < Nodes.size() ? Nodes[std::size_t(Index)]["name"] : J()}));
            }
        }
        return Out;
    }
    J Computed(GraphSignatures& Sig, J& Notes) const
    {
        J Out = J::Object();
        Recovery Owner(Doc, Sig);
        auto& Context = Owner.Context();
        const auto& Functions = Doc["functions"];
        const auto* Uber = Context.Uber.empty() ? nullptr : &Functions[Context.Uber];
        const auto Library = ObjectOr(Optional(ClassValues, "PropertyAccessLibrary"));
        const auto Segments = ArrayOr(Optional(Library, "PathSegments"));
        std::vector<J> Batches;
        for (const auto& V : Library.Fields)
            if (Starts(V.first, "CopyBatches") && V.second.Kind == J::K::Object)
                Batches.push_back(V.second);
        for (const auto& Handler : Handlers.Items)
        {
            const auto Field = ArrayOr(Optional(ObjectOr(Optional(Handler, "ValueHandlerNodeProperty")), "field"));
            if (Field.Items.empty() || Field[0].Kind == J::K::Null)
                continue;
            const auto Name = Field[0].S();
            if (!Out.Has(Name))
                Out[Name] = J::Object({{"inputs", J::Object({{"nodes", J::Array()}, {"links", J::Array()}})},
                                       {"pins", J::Array()},
                                       {"constants", J::Object()}});
            auto& Entry = Out[Name];
            Graph G("", "anim");
            for (const auto& N : Entry["inputs"]["nodes"].Items)
                G.Nodes.push_back(N);
            G.Links = Entry["inputs"]["links"];
            try
            {
                const auto Bound = Str(Optional(Handler, "BoundFunction"));
                if (Optional(Handler, "Function").Truth() && Functions.Has(Bound))
                {
                    const auto Handed = Owner.EventStub(Functions[Bound]);
                    if (Handed.Kind == J::K::Null || !Uber)
                        throw Unsupported("an input function that is not a stub of the event graph");
                    AnimationInputs Work(Context, *Uber, G, Name);
                    Work.RunFrom(Handed[0].I());
                    Entry["pins"].Items.insert(Entry["pins"].Items.end(), Work.Pins.Items.begin(),
                                               Work.Pins.Items.end());
                    for (const auto& K : Work.Constants.Keys())
                        Entry["constants"][K] = Work.Constants[K];
                }
                for (const auto& Record : ArrayOr(Optional(Handler, "CopyRecords")).Items)
                {
                    const J* Copy = nullptr;
                    const auto Index = Record["CopyIndex"].I();
                    if (Index < 0)
                        throw Error("animation-copy-index");
                    for (const auto& B : Batches)
                    {
                        const auto& C = Optional(B, "Copies");
                        if (std::uint64_t(Index) < C.Items.size())
                        {
                            Copy = &C[std::size_t(Index)];
                            break;
                        }
                    }
                    if (!Copy)
                        throw Unsupported("a property copy that is not in the class");
                    const auto& Source = Library["SrcPaths"][std::size_t((*Copy)["AccessIndex"].U())];
                    const auto Start = Source["PathSegmentStartIndex"].U(), Count = Source["PathSegmentCount"].U();
                    if (Count != 1 || Start >= Segments.Items.size() ||
                        Optional(Segments[std::size_t(Start)], "Function").Truth())
                        throw Unsupported("a property copy from something other than one variable");
                    const auto& Path = Segments[std::size_t(Start)];
                    const auto Got = G.Node("get", J::Object({{"var", Path["Name"]}, {"local", false}}));
                    Wire Last = G.Pin(Got, Path["Name"].S());
                    if (Int(Path, "ArrayIndex") >= 0)
                    {
                        const auto M = G.Node(
                            "call",
                            J::Object({{"fn", "Array_Get"},
                                       {"cls", "/Script/Engine.KismetArrayLibrary"},
                                       {"pure", true},
                                       {"defaults", J::Object({{"Index", std::to_string(Int(Path, "ArrayIndex"))}})}}));
                        G.Link(Last, G.Pin(M, "TargetArray"));
                        Last = G.Pin(M, "Item");
                    }
                    if (PyStr(Optional(Record, "PostCopyOperation")).find("Negate") != std::string::npos)
                    {
                        const auto M = G.Node("call", J::Object({{"fn", "Not_PreBool"},
                                                                 {"cls", "/Script/Engine.KismetMathLibrary"},
                                                                 {"pure", true}}));
                        G.Link(Last, G.Pin(M, "A"));
                        Last = G.Pin(M, "ReturnValue");
                    }
                    for (auto I = (*Copy)["DestAccessStartIndex"].U(); I < (*Copy)["DestAccessEndIndex"].U(); ++I)
                    {
                        const auto& Target = Library["DestPaths"][std::size_t(I)];
                        const auto At = Target["PathSegmentStartIndex"].U();
                        if (Target["PathSegmentCount"].U() != 2 || At + 1 >= Segments.Items.size() ||
                            Segments[std::size_t(At)]["Name"].S() != Name)
                            throw Unsupported("a property copy into something other than one setting of the node");
                        const auto& Where = Segments[std::size_t(At + 1)];
                        const auto Member =
                            Where["Name"].S() +
                            (Int(Where, "ArrayIndex") >= 0 ? "_" + std::to_string(Int(Where, "ArrayIndex")) : "");
                        G.Link(Last, {"@", Member});
                        Entry["pins"].Items.push_back(Where["Name"]);
                    }
                }
            }
            catch (const Unsupported& Why)
            {
                Notes.Items.push_back(Name + ": input not made: " + Why.what());
            }
            const auto Data = G.Data();
            Entry["inputs"]["nodes"] = Data["nodes"];
            Entry["inputs"]["links"] = G.Links;
        }
        for (const auto& K : Out.Keys())
            for (auto& Node : Out[K]["inputs"]["nodes"].Items)
                Getter(Node);
        return Out;
    }
    J Plan(const J& ValueText, GraphSignatures* Sig = nullptr) const;
    J Summary(GraphSignatures* Sig = nullptr) const;
    J InputsSummary(GraphSignatures* Sig = nullptr) const;
};
inline J Model::EnumOf(GraphSignatures& Sig, const J& Entry) const
{
    for (const auto& L : Entry["inputs"]["links"].Items)
    {
        if (L[2].S() != "@" || L[3].S() != "ActiveEnumValue")
            continue;
        const J* Node = nullptr;
        for (const auto& N : Entry["inputs"]["nodes"].Items)
            if (N["id"].S() == L[0].S())
            {
                Node = &N;
                break;
            }
        if (!Node)
            throw Error("animation-enum-wire-source");
        if ((*Node)["t"].S() == "get")
        {
            const auto Name = (*Node)["var"].S();
            const auto Folded = SignatureUnicode::Lower(Name);
            for (const auto& Owner : Sig.Chain(Package + "." + Doc["class"]["name"].S()))
            {
                const auto Types = Sig.Types.find(Owner);
                if (Types != Sig.Types.end() && Types->second.Has(Folded) && Types->second[Folded].Truth())
                    return Optional(Types->second[Folded], "enum_path");
            }
            for (const auto& P : Doc["class"]["properties"].Items)
                if (P["name"].S() == Name && Optional(P, "enum").Truth())
                    return P["enum"];
            std::vector<std::string> Named;
            for (const auto& E : Sig.Enums)
                if (SignatureUnicode::Lower(Leaf(E.first)) == "e" + Folded)
                    Named.push_back(E.first);
            return Named.size() == 1 ? J(Named[0]) : J();
        }
        if ((*Node)["t"].S() == "call")
        {
            const auto Native = Sig.Native.find((*Node)["cls"].S() + "." + (*Node)["fn"].S());
            if (Native == Sig.Native.end())
                return {};
            return Optional(ObjectOr(Optional(ObjectOr(Optional(ObjectOr(Optional(Native->second, "script_signature")),
                                                                "return_property")),
                                              "type_detail")),
                            "enum_path");
        }
    }
    return {};
}
inline J Model::Plan(const J& ValueText, GraphSignatures* Sig) const
{
    std::map<std::string, std::string> Text, Struct;
    for (const auto& V : ValueText.Items)
        Text[V["name"].S()] = Str(Optional(V, "text"));
    std::map<std::string, std::int64_t> MadeAt;
    for (std::size_t I = 0; I < Nodes.size(); ++I)
    {
        const auto Name = Nodes[I]["name"].S();
        Struct[Name] = Nodes[I]["struct"].S();
        MadeAt[Name] = std::int64_t(Nodes.size() - 1 - I);
    }
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> Feeds;
    for (const auto& L : Links().Items)
        if (L[2].Truth())
        {
            auto PinName = L[1].S();
            PinName.erase(std::remove(PinName.begin(), PinName.end(), ']'), PinName.end());
            std::replace(PinName.begin(), PinName.end(), '[', '_');
            Feeds[L[0].S()].push_back({PinName, L[2].S()});
        }
    J Notes = J::Array();
    const J Computations = Sig ? Computed(*Sig, Notes) : J::Object();
    std::set<std::string> Placed;
    auto Reach = [&](const std::string& Start)
    {
        std::vector<std::string> Out, Queue = {Start};
        std::set<std::string> Seen;
        for (std::size_t I = 0; I < Queue.size(); ++I)
        {
            if (!Seen.insert(Queue[I]).second)
                continue;
            Out.push_back(Queue[I]);
            for (const auto& F : Feeds[Queue[I]])
                Queue.push_back(F.second);
            if (Out.size() > 100000)
                throw Error("animation-reach-budget");
        }
        return Out;
    };
    auto RuleNode = [&](const std::string& Name)
    {
        const auto& V = Computations[Name];
        return J::Object({{"inputs", V["inputs"]}, {"pins", UniquePins(V["pins"])}});
    };
    std::function<J(std::vector<std::string>)> GraphMake;
    std::function<J(const std::string&)> MachineMake;
    GraphMake = [&](std::vector<std::string> Names)
    {
        J Made = J::Array(), Wires = J::Array();
        std::stable_sort(Names.begin(), Names.end(),
                         [&](const std::string& A, const std::string& B) { return MadeAt.at(A) < MadeAt.at(B); });
        for (const auto& Name : Names)
        {
            if (Placed.count(Name))
            {
                Notes.Items.push_back(Name + " is reached from two graphs");
                continue;
            }
            Placed.insert(Name);
            J Node = J::Object(
                {{"name", Name}, {"struct", Struct.at(Name)}, {"text", Text.count(Name) ? Text.at(Name) : ""}});
            const auto Held = ObjectOr(Value(Name));
            J Extra = J::Object();
            const auto Group = Optional(Held, "GroupName");
            if (Group.Kind != J::K::Null && Group.S() != "None")
            {
                auto Role = Held.Has("GroupRole") ? PyStr(Held["GroupRole"]) : "CanBeLeader",
                     Scope = Held.Has("GroupScope") ? PyStr(Held["GroupScope"]) : "Local";
                const auto A = Role.rfind("::"), B = Scope.rfind("::");
                if (A != std::string::npos)
                    Role = Role.substr(A + 2);
                if (B != std::string::npos)
                    Scope = Scope.substr(B + 2);
                Extra["SyncGroup"] =
                    "(GroupName=\"" + Group.S() + "\",GroupRole=" + Role + ",GroupScope=" + Scope + ")";
            }
            if (Ends(Struct.at(Name), ".AnimNode_SaveCachedPose") && Optional(Held, "CachePoseName").Truth())
                Extra["CacheName"] = Held["CachePoseName"];
            const J* Wired = Computations.Has(Name) ? &Computations[Name] : nullptr;
            if (Wired && Wired->Truth())
            {
                Node["inputs"] = (*Wired)["inputs"];
                Node["pins"] = UniquePins((*Wired)["pins"]);
            }
            if (Ends(Struct.at(Name), ".AnimNode_BlendListByEnum"))
            {
                const auto Enum = Wired && Sig ? EnumOf(*Sig, *Wired) : J();
                const auto NamesI = Enum.Truth() && Sig ? Sig->Enums.find(Enum.S())
                                    : Sig               ? Sig->Enums.end()
                                                        : std::map<std::string, J>::iterator();
                if (!Sig || NamesI == Sig->Enums.end() || !NamesI->second.Truth())
                    Notes.Items.push_back(Name + ": the enum it switches on was not found");
                else
                {
                    std::vector<std::pair<std::int64_t, std::string>> Shown;
                    const auto Poses = ArrayOr(Optional(Held, "EnumToPoseIndex"));
                    for (std::size_t I = 0; I < Poses.Items.size() && I < NamesI->second.Items.size(); ++I)
                        if (Poses[I].I() > 0)
                            Shown.push_back({Poses[I].I(), NamesI->second[I].S()});
                    std::sort(Shown.begin(), Shown.end());
                    Extra["BoundEnum"] = Enum;
                    std::vector<std::string> V;
                    for (const auto& E : Shown)
                        V.push_back("\"" + E.second + "\"");
                    Extra["VisibleEnumEntries"] = "(" + Join(V, ",") + ")";
                }
            }
            if (Extra.Truth())
                Node["properties"] = Extra;
            if (Ends(Struct.at(Name), ".AnimNode_StateMachine"))
                Node["machine"] = MachineMake(Name);
            Made.Items.push_back(Node);
            for (const auto& F : Feeds[Name])
                Wires.Items.push_back(J::Array({F.second, Name, F.first}));
        }
        return J::Object({{"nodes", Made}, {"links", Wires}});
    };
    MachineMake = [&](const std::string& OwnerName)
    {
        const auto Index = Int(ObjectOr(Value(OwnerName)), "StateMachineIndexInClass");
        if (Index < 0 || std::uint64_t(Index) >= Machines.Items.size())
        {
            Notes.Items.push_back(OwnerName + " names no state machine");
            return J();
        }
        const auto& Baked = Machines[std::size_t(Index)];
        J States = J::Array(), Transitions = J::Array();
        for (std::size_t I = 0; I < Baked["States"].Items.size(); ++I)
        {
            const auto& S = Baked["States"][I];
            const bool Conduit = Optional(S, "bIsAConduit").Truth();
            J Notifies = J::Array();
            for (const auto* K : {"StartNotify", "EndNotify", "FullyBlendedNotify"})
                Notifies.Items.push_back(Notify(Int(S, K)));
            J Item = J::Object({{"name", S["StateName"]},
                                {"index", J(std::uint64_t(I))},
                                {"conduit", Conduit},
                                {"always_reset", Optional(S, "bAlwaysResetOnEntry").Truth()},
                                {"notifies", Notifies}});
            if (Conduit)
            {
                const auto Rule = NameAt(Int(S, "EntryRuleNodeIndex"));
                const auto Name = Str(Rule);
                Item["rule"] = Rule;
                Item["rule_text"] = Text.count(Name) ? Text.at(Name) : "";
                if (Computations.Has(Name))
                    Item["rule_node"] = RuleNode(Name);
                Placed.insert(Name);
            }
            else
            {
                const auto Result = NameAt(S["StateRootNodeIndex"].I());
                Item["result"] = Result;
                Item["graph"] = Result.Truth() ? GraphMake(Reach(Result.S()))
                                               : J::Object({{"nodes", J::Array()}, {"links", J::Array()}});
            }
            States.Items.push_back(Item);
        }
        for (std::size_t I = 0; I < Baked["States"].Items.size(); ++I)
        {
            const auto& S = Baked["States"][I];
            for (std::size_t Priority = 0; Priority < S["Transitions"].Items.size(); ++Priority)
            {
                const auto& Taken = S["Transitions"][Priority];
                const auto& Edge = Baked["Transitions"][std::size_t(Taken["TransitionIndex"].U())];
                const auto Rule = NameAt(Taken["CanTakeDelegateIndex"].I());
                const auto RuleName = Str(Rule);
                if (Rule.Truth())
                    Placed.insert(RuleName);
                if (Edge["PreviousState"].I() != std::int64_t(I))
                    Notes.Items.push_back(OwnerName + ": a transition listed under state " + std::to_string(I) +
                                          " leaves state " + Edge["PreviousState"].Text);
                if (Taken.Has("bDesiredTransitionReturnValue") && !Taken["bDesiredTransitionReturnValue"].Truth())
                    Notes.Items.push_back(OwnerName + ": a transition taken when its rule is false");
                J Notifies = J::Array();
                for (const auto* K : {"StartNotify", "EndNotify", "InterruptNotify"})
                    Notifies.Items.push_back(Notify(Int(Edge, K)));
                J Item = J::Object(
                    {{"source", J(std::uint64_t(I))},
                     {"target", Edge["NextState"]},
                     {"priority", J(std::uint64_t(Priority))},
                     {"rule", Rule},
                     {"index", Taken["TransitionIndex"]},
                     {"rule_struct", Struct.count(RuleName) ? J(Struct.at(RuleName)) : J()},
                     {"rule_text", Text.count(RuleName) ? Text.at(RuleName) : ""},
                     {"crossfade", Number(Edge.Has("CrossfadeDuration") ? Edge["CrossfadeDuration"].D() : 0.0)},
                     {"blend_mode", Optional(Edge, "BlendMode")},
                     {"logic", Optional(Edge, "LogicType")},
                     {"automatic", Optional(Taken, "bAutomaticRemainingTimeRule").Truth()},
                     {"notifies", Notifies}});
                for (const auto* K : {"CustomCurve", "BlendProfile"})
                    if (Optional(Edge, K).Truth())
                        Item[std::string(K) == "CustomCurve" ? "curve" : "profile"] = PyStr(Edge[K]);
                if (Computations.Has(RuleName))
                    Item["rule_node"] = RuleNode(RuleName);
                const auto Custom = NameAt(Int(Taken, "CustomResultNodeIndex"));
                if (Optional(Taken, "PoseEvaluatorLinks").Truth())
                {
                    J V = J::Array();
                    for (const auto& X : Taken["PoseEvaluatorLinks"].Items)
                        V.Items.push_back(NameAt(X.I()));
                    Item["evaluators"] = V;
                }
                if (Custom.Truth())
                {
                    Item["custom"] = Custom;
                    const auto Inside = Reach(Custom.S());
                    std::vector<std::string> Loose;
                    for (const auto& V : Optional(Item, "evaluators").Items)
                        if (V.Truth() && std::find(Inside.begin(), Inside.end(), V.S()) == Inside.end())
                            Loose.push_back(V.S());
                    auto Names = Inside;
                    Names.insert(Names.end(), Loose.begin(), Loose.end());
                    Item["custom_graph"] = GraphMake(Names);
                    if (!Loose.empty())
                    {
                        J Wired = J::Array(), LooseJ = J::Array();
                        for (const auto& N : Item["custom_graph"]["nodes"].Items)
                            if (std::find(Inside.begin(), Inside.end(), N["name"].S()) != Inside.end())
                                Wired.Items.push_back(N["name"]);
                        for (const auto& N : Loose)
                            LooseJ.Items.push_back(N);
                        const auto Arranged = BlendOrder(Wired, LooseJ, Item["evaluators"]);
                        if (Arranged.Kind == J::K::Null)
                            Notes.Items.push_back(OwnerName +
                                                  ": the order of the nodes of a blend graph was not worked out");
                        else
                        {
                            std::map<std::string, std::size_t> Positions;
                            for (std::size_t P = 0; P < Arranged.Items.size(); ++P)
                                Positions[Arranged[P].S()] = P;
                            auto& V = Item["custom_graph"]["nodes"].Items;
                            std::stable_sort(V.begin(), V.end(), [&](const J& A, const J& B)
                                             { return Positions.at(A["name"].S()) < Positions.at(B["name"].S()); });
                        }
                    }
                }
                Transitions.Items.push_back(Item);
            }
        }
        std::stable_sort(States.Items.begin(), States.Items.end(),
                         [&](const J& A, const J& B)
                         {
                             const auto NA =
                                            Optional(A, "result").Truth() ? Str(A["result"]) : Str(Optional(A, "rule")),
                                        NB =
                                            Optional(B, "result").Truth() ? Str(B["result"]) : Str(Optional(B, "rule"));
                             return (MadeAt.count(NA) ? MadeAt.at(NA) : 0) < (MadeAt.count(NB) ? MadeAt.at(NB) : 0);
                         });
        std::stable_sort(Transitions.Items.begin(), Transitions.Items.end(),
                         [](const J& A, const J& B) { return A["index"].I() < B["index"].I(); });
        return J::Object({{"name", Baked["MachineName"]},
                          {"initial", Baked.Has("InitialState") ? Baked["InitialState"] : J(0)},
                          {"states", States},
                          {"transitions", Transitions}});
    };
    J Root;
    for (const auto& P : Nodes)
        if (Ends(P["struct"].S(), ".AnimNode_Root"))
        {
            Root = P["name"];
            break;
        }
    J Top = Root.Truth() ? GraphMake(Reach(Root.S())) : J::Object({{"nodes", J::Array()}, {"links", J::Array()}});
    for (const auto& P : Nodes)
    {
        const auto Name = P["name"].S();
        if (Placed.count(Name))
            continue;
        if (Ends(P["struct"].S(), ".AnimNode_TransitionResult") || Ends(P["struct"].S(), ".AnimNode_StateResult"))
        {
            Notes.Items.push_back(Name + " belongs to no state machine");
            continue;
        }
        const auto Extra = GraphMake(Reach(Name));
        Top["nodes"].Items.insert(Top["nodes"].Items.end(), Extra["nodes"].Items.begin(), Extra["nodes"].Items.end());
        Top["links"].Items.insert(Top["links"].Items.end(), Extra["links"].Items.begin(), Extra["links"].Items.end());
    }
    std::stable_sort(Top["nodes"].Items.begin(), Top["nodes"].Items.end(),
                     [&](const J& A, const J& B) { return MadeAt.at(A["name"].S()) > MadeAt.at(B["name"].S()); });
    J Groups = J::Array();
    for (const auto& V : ArrayOr(Optional(ClassValues, "SyncGroupNames")).Items)
        Groups.Items.push_back(PyStr(V));
    return J::Object({{"root", Root}, {"graph", Top}, {"notes", Notes}, {"sync_groups", Groups}});
}
inline J Model::InputsSummary(GraphSignatures* Sig) const
{
    const auto& Functions = Doc["functions"];
    std::string Uber;
    for (const auto& K : Functions.Keys())
        if (Starts(K, "ExecuteUbergraph_"))
        {
            Uber = K;
            break;
        }
    auto Pure = [&](const std::string& Path)
    {
        const auto Full = Starts(Path, "/") ? Path : Package + "." + Path;
        const auto Found = Sig ? Sig->Get(Full) : J();
        return Found.Kind != J::K::Null && (Found[1].U() & 0x10000000);
    };
    const auto Pair = BlueprintCodeAndValues(Functions, Pure);
    const auto& Code = Pair[0];
    const auto& FlagsValue = Pair[1];
    const auto Library = ObjectOr(Optional(ClassValues, "PropertyAccessLibrary"));
    const auto Segments = ArrayOr(Optional(Library, "PathSegments"));
    std::vector<J> Batches;
    for (const auto& V : Library.Fields)
        if (Starts(V.first, "CopyBatches") && V.second.Kind == J::K::Object)
            Batches.push_back(V.second);
    auto Names = [&](const J& Path)
    {
        std::vector<std::string> Out;
        const auto Start = Path["PathSegmentStartIndex"].U(), Count = Path["PathSegmentCount"].U();
        if (Start > Segments.Items.size() || Count > Segments.Items.size() - Start)
            throw Error("animation-summary-path");
        for (auto I = Start; I < Start + Count; ++I)
        {
            const auto& S = Segments[std::size_t(I)];
            Out.push_back(S["Name"].S() +
                          (Int(S, "ArrayIndex") >= 0 ? "[" + std::to_string(Int(S, "ArrayIndex")) + "]" : ""));
        }
        return Join(Out, ".");
    };
    J Out = J::Object();
    for (const auto& Handler : Handlers.Items)
    {
        const auto Field = ArrayOr(Optional(ObjectOr(Optional(Handler, "ValueHandlerNodeProperty")), "field"));
        const auto Name = Field.Items.empty() || Field[0].Kind == J::K::Null ? "null" : Field[0].S();
        if (!Out.Has(Name))
            Out[Name] = J::Object({{"copies", J::Array()}, {"code", J()}});
        auto& Entry = Out[Name];
        for (const auto& Record : ArrayOr(Optional(Handler, "CopyRecords")).Items)
        {
            const J* Copy = nullptr;
            const auto Index = Record["CopyIndex"].I();
            if (Index < 0)
                throw Error("animation-summary-copy-index");
            for (const auto& B : Batches)
                if (std::uint64_t(Index) < Optional(B, "Copies").Items.size())
                {
                    Copy = &B["Copies"][std::size_t(Index)];
                    break;
                }
            if (Copy && Copy->Truth())
            {
                std::vector<std::string> Dest;
                for (auto I = (*Copy)["DestAccessStartIndex"].U(); I < (*Copy)["DestAccessEndIndex"].U(); ++I)
                    Dest.push_back(Names(Library["DestPaths"][std::size_t(I)]));
                std::sort(Dest.begin(), Dest.end());
                J D = J::Array();
                for (const auto& S : Dest)
                    D.Items.push_back(S);
                Entry["copies"].Items.push_back(
                    J::Array({Names(Library["SrcPaths"][std::size_t((*Copy)["AccessIndex"].U())]), D,
                              PyStr(Optional(Record, "PostCopyOperation")), PyStr(Optional(*Copy, "Type"))}));
            }
        }
        const auto Bound = Str(Optional(Handler, "BoundFunction"));
        if (Optional(Handler, "Function").Truth() && Code.Has(Bound) && !Uber.empty())
        {
            std::int64_t Start = -1;
            const auto Prefix = Uber + "(#";
            for (const auto& L : Code[Bound].Items)
            {
                const auto S = L.S();
                const auto At = S.find(Prefix);
                if (At == std::string::npos)
                    continue;
                const auto End = S.find(')', At + Prefix.size());
                if (End != std::string::npos)
                {
                    const auto T = S.substr(At + Prefix.size(), End - At - Prefix.size());
                    if (!T.empty() && T.find_first_not_of("0123456789") == std::string::npos)
                        Start = std::stoll(T);
                }
                break;
            }
            if (Start >= 0)
            {
                std::vector<std::string> Lines;
                std::vector<bool> Bits;
                for (const auto& L : Code[Uber].Items)
                    Lines.push_back(L.S());
                for (const auto& B : FlagsValue[Uber].Items)
                    Bits.push_back(B.B());
                J F = J::Array();
                for (const auto& L : BlueprintFlowForm(Lines, Bits, std::size_t(Start)))
                    F.Items.push_back(L);
                Entry["code"] = F;
            }
            else
                Entry["code"] = J::Array({"?"});
        }
        std::stable_sort(Entry["copies"].Items.begin(), Entry["copies"].Items.end(), Less);
    }
    return Out;
}
inline J Model::Summary(GraphSignatures* Sig) const
{
    std::function<J(const J&, std::size_t)> Named = [&](const J& V, std::size_t Depth)
    {
        if (Depth > 128)
            throw Error("animation-summary-depth");
        if (V.Kind == J::K::Object)
        {
            if (V.Has("LinkID") && V.Fields.size() <= 2 && (V.Fields.size() == 1 || V.Has("SourceLinkID")))
            {
                const auto Link = V["LinkID"].I();
                return J::Object(
                    {{"pose from",
                      Link >= 0 && std::uint64_t(Link) < Nodes.size() ? Nodes[std::size_t(Link)]["name"] : J()}});
            }
            J Out = J::Object();
            for (const auto& K : V.Keys())
                if (K != "StateMachineIndexInClass")
                    Out[K] = Named(V[K], Depth + 1);
            return Out;
        }
        if (V.Kind == J::K::Array)
        {
            J Out = J::Array();
            for (const auto& X : V.Items)
                Out.Items.push_back(Named(X, Depth + 1));
            return Out;
        }
        return V;
    };
    J NodeValues = J::Object(), MachineValues = J::Object(), Order = J::Array(), Groups = J::Array(),
      Notifies = J::Array();
    for (const auto& P : Nodes)
    {
        const auto Name = P["name"].S();
        NodeValues[Name] = J::Object({{"struct", P["struct"]}, {"values", Named(Value(Name), 0)}});
        Order.Items.push_back(P["name"]);
    }
    for (const auto& P : Nodes)
    {
        if (!Ends(P["struct"].S(), ".AnimNode_StateMachine"))
            continue;
        const auto Index = Int(ObjectOr(Value(P["name"].S())), "StateMachineIndexInClass");
        if (Index < 0 || std::uint64_t(Index) >= Machines.Items.size())
            continue;
        const auto& Baked = Machines[std::size_t(Index)];
        J States = J::Array(), NumberedStates = J::Array(), NumberedEdges = J::Array();
        for (const auto& S : Baked["States"].Items)
        {
            J Taken = J::Array();
            for (const auto& E : S["Transitions"].Items)
            {
                const auto& Edge = Baked["Transitions"][std::size_t(E["TransitionIndex"].U())];
                J Evaluators = J::Array();
                for (const auto& I : ArrayOr(Optional(E, "PoseEvaluatorLinks")).Items)
                    Evaluators.Items.push_back(NameAt(I.I()));
                J T = J::Object({{"to", Baked["States"][std::size_t(Edge["NextState"].U())]["StateName"]},
                                 {"rule", NameAt(E["CanTakeDelegateIndex"].I())},
                                 {"when", Optional(E, "bDesiredTransitionReturnValue")},
                                 {"custom", NameAt(Int(E, "CustomResultNodeIndex"))},
                                 {"automatic", Optional(E, "bAutomaticRemainingTimeRule").Truth()},
                                 {"evaluators", Evaluators}});
                for (const auto* K : {"CrossfadeDuration", "BlendMode", "LogicType", "CustomCurve", "BlendProfile",
                                      "StartNotify", "EndNotify", "InterruptNotify"})
                    T[K] = Optional(Edge, K);
                Taken.Items.push_back(T);
            }
            std::vector<std::string> Players;
            for (const auto& I : ArrayOr(Optional(S, "PlayerNodeIndices")).Items)
            {
                const auto Name = NameAt(I.I());
                if (Name.Truth())
                    Players.push_back(Name.S());
            }
            std::sort(Players.begin(), Players.end());
            J PlayerNames = J::Array();
            for (const auto& N : Players)
                PlayerNames.Items.push_back(N);
            J StateNotifies = J::Array();
            for (const auto* K : {"StartNotify", "EndNotify", "FullyBlendedNotify"})
                StateNotifies.Items.push_back(Notify(Int(S, K)));
            States.Items.push_back(J::Object({{"name", S["StateName"]},
                                              {"root", NameAt(S["StateRootNodeIndex"].I())},
                                              {"conduit", Optional(S, "bIsAConduit")},
                                              {"rule", NameAt(Int(S, "EntryRuleNodeIndex"))},
                                              {"players", PlayerNames},
                                              {"reset", Optional(S, "bAlwaysResetOnEntry")},
                                              {"transitions", Taken},
                                              {"notifies", StateNotifies}}));
            NumberedStates.Items.push_back(S["StateName"]);
        }
        for (const auto& E : Baked["Transitions"].Items)
            NumberedEdges.Items.push_back(J::Array({E["PreviousState"], E["NextState"]}));
        MachineValues[P["name"].S()] =
            J::Object({{"name", Baked["MachineName"]},
                       {"initial", Baked["States"].Items.empty()
                                       ? J()
                                       : Baked["States"][std::size_t(Baked["InitialState"].U())]["StateName"]},
                       {"states", States},
                       {"numbered", J::Array({NumberedStates, NumberedEdges})}});
    }
    for (const auto& N : ArrayOr(Optional(ClassValues, "SyncGroupNames")).Items)
        Groups.Items.push_back(PyStr(N));
    for (const auto& N : ArrayOr(Optional(ClassValues, "AnimNotifies")).Items)
        Notifies.Items.push_back(PyStr(Optional(N, "NotifyName")));
    return J::Object({{"nodes", NodeValues},
                      {"machines", MachineValues},
                      {"inputs", InputsSummary(Sig)},
                      {"order", Order},
                      {"groups", Groups},
                      {"notifies", Notifies}});
}
} // namespace AnimDetail
inline std::set<std::string> AnimDetail::Model::ReconstructedClassFields(const J& Plan) const
{
    std::set<std::string> Out;
    const J& Animation = Optional(Plan, "anim");
    if (Animation.Kind != J::K::Object || Optional(Animation, "notes").Truth() || !Optional(Animation, "root").Truth())
        return Out;
    if (std::count_if(Nodes.begin(), Nodes.end(),
                      [](const J& Node) { return Ends(Node["struct"].S(), ".AnimNode_Root"); }) != 1)
        return Out;
    const auto Keys = [](const J& Value, std::initializer_list<const char*> Allowed)
    {
        if (Value.Kind != J::K::Object)
            return false;
        for (const auto& Key : Value.Keys())
            if (!Is(Key, Allowed))
                return false;
        return true;
    };
    const auto Every = [](const J& Values, const std::function<bool(const J&)>& Check)
    {
        return Values.Kind == J::K::Array && std::all_of(Values.Items.begin(), Values.Items.end(), Check);
    };
    std::set<std::string> Reachable, MachineNames, NotifyNames;
    std::map<std::string, const J*> MadeMachines;
    std::function<bool(const J&, const std::string&)> ReachGraph = [&](const J& Graph, const std::string& Root)
    {
        std::map<std::string, const J*> Named;
        for (const auto& Node : Graph["nodes"].Items)
            Named.emplace(Node["name"].S(), &Node);
        if (!Named.count(Root))
            return false;
        std::vector<std::string> Pending = {Root};
        std::set<std::string> Visited;
        for (std::size_t At = 0; At < Pending.size(); ++At)
        {
            const auto Name = Pending[At];
            if (!Visited.insert(Name).second)
                continue;
            if (!Named.count(Name))
                continue;
            Reachable.insert(Name);
            const J& Node = *Named.at(Name);
            for (const auto& Link : Graph["links"].Items)
                if (Link[1].S() == Name)
                    Pending.push_back(Link[std::size_t(0)].S());
            if (!Node.Has("machine"))
                continue;
            const auto& Machine = Node["machine"];
            if (!MachineNames.insert(Machine["name"].S()).second)
                return false;
            MadeMachines.emplace(Machine["name"].S(), &Machine);
            for (const auto& State : Machine["states"].Items)
            {
                for (const auto& Notify : State["notifies"].Items)
                    if (Notify.Truth())
                        NotifyNames.insert(Notify.S());
                if (State.Has("graph") && !ReachGraph(State["graph"], State["result"].S()))
                    return false;
            }
            for (const auto& Transition : Machine["transitions"].Items)
            {
                for (const auto& Notify : Transition["notifies"].Items)
                    if (Notify.Truth())
                        NotifyNames.insert(Notify.S());
                if (Transition.Has("custom_graph") && !ReachGraph(Transition["custom_graph"], Transition["custom"].S()))
                    return false;
            }
        }
        return true;
    };
    if (!ReachGraph(Animation["graph"], Animation["root"].S()))
        return Out;
    // The editor compiler regenerates these records. Limit admission to source
    // layouts represented by this recovery model, so a new member stays visible.
    std::set<std::string> SourceMachines;
    const bool MachinesKnown =
        MachineNames.size() == Machines.Items.size() &&
        Every(Optional(ClassValues, "BakedStateMachines"),
              [&](const J& Machine)
              {
                  if (!Keys(Machine, {"MachineName", "InitialState", "States", "Transitions"}) ||
                      !SourceMachines.insert(Machine["MachineName"].S()).second ||
                      !MadeMachines.count(Machine["MachineName"].S()))
                      return false;
                  const auto& Made = *MadeMachines.at(Machine["MachineName"].S());
                  std::set<std::int64_t> EdgeIndices;
                  for (const auto& Edge : Made["transitions"].Items)
                      EdgeIndices.insert(Edge["index"].I());
                  if (Made["states"].Items.size() != Machine["States"].Items.size() ||
                      Made["transitions"].Items.size() != Machine["Transitions"].Items.size() ||
                      EdgeIndices.size() != Machine["Transitions"].Items.size())
                      return false;
                  for (std::size_t Index = 0; Index < Machine["Transitions"].Items.size(); ++Index)
                      if (!EdgeIndices.count(std::int64_t(Index)))
                          return false;
                  return Every(Optional(Machine, "States"),
                               [&](const J& State)
                               {
                                   return Keys(State, {"StateName", "StateRootNodeIndex", "EntryRuleNodeIndex",
                                                       "PlayerNodeIndices", "LayerNodeIndices", "StartNotify",
                                                       "EndNotify", "FullyBlendedNotify", "bAlwaysResetOnEntry",
                                                       "bIsAConduit", "Transitions"}) &&
                                          !Optional(State, "LayerNodeIndices").Truth() &&
                                          Every(Optional(State, "Transitions"),
                                                [&](const J& Edge)
                                                {
                                                    return Keys(Edge, {"CanTakeDelegateIndex", "CustomResultNodeIndex",
                                                                       "PoseEvaluatorLinks", "TransitionIndex",
                                                                       "bAutomaticRemainingTimeRule",
                                                                       "bDesiredTransitionReturnValue"});
                                                });
                               }) &&
                         Every(Optional(Machine, "Transitions"),
                               [&](const J& Edge)
                               {
                                   return Keys(Edge, {"PreviousState", "NextState", "StateName", "CrossfadeDuration",
                                                      "BlendMode", "LogicType", "CustomCurve", "BlendProfile",
                                                      "StartNotify", "EndNotify", "InterruptNotify"});
                               });
              });
    if (MachinesKnown)
        Out.insert("BakedStateMachines");
    std::set<std::string> InputNodes;
    std::function<void(const J&)> CollectInputs = [&](const J& Value)
    {
        if (Value.Kind == J::K::Object)
        {
            if (Value.Has("name") && Value.Has("inputs"))
                InputNodes.insert(Value["name"].S());
            if (Value.Has("rule") && Value.Has("rule_node"))
                InputNodes.insert(Value["rule"].S());
            for (const auto& Field : Value.Fields)
                CollectInputs(Field.second);
        }
        else if (Value.Kind == J::K::Array)
            for (const auto& Item : Value.Items)
                CollectInputs(Item);
    };
    CollectInputs(Animation["graph"]);
    const bool InputsKnown = Every(
        Optional(ClassValues, "EvaluateGraphExposedInputs"),
        [&](const J& Handler)
        {
            const J &Property = Optional(Handler, "ValueHandlerNodeProperty"), Field = Optional(Property, "field");
            if (!Keys(Handler, {"BoundFunction", "Function", "ValueHandlerNodeProperty", "CopyRecords"}) ||
                !Keys(Property, {"field", "owner"}) || Field.Kind != J::K::Array || Field.Items.size() != 1 ||
                Field[std::size_t(0)].Kind != J::K::String || !InputNodes.count(Field[std::size_t(0)].S()))
                return false;
            const auto Bound = Optional(Handler, "BoundFunction");
            if (Optional(Handler, "Function").Truth() && (!Bound.Truth() || !Doc["functions"].Has(Bound.S())))
                return false;
            return Every(Optional(Handler, "CopyRecords"),
                         [&](const J& Copy)
                         {
                             return Keys(Copy, {"CopyIndex", "PostCopyOperation"}) &&
                                    Optional(Copy, "CopyIndex").Kind == J::K::Number && Copy["CopyIndex"].I() >= 0 &&
                                    Is(PyStr(Optional(Copy, "PostCopyOperation")),
                                       {"None", "LogicalNegateBool", "EPostCopyOperation::None",
                                        "EPostCopyOperation::LogicalNegateBool"});
                         });
        });
    if (InputsKnown)
        Out.insert("EvaluateGraphExposedInputs");
    const J& Library = Optional(ClassValues, "PropertyAccessLibrary");
    bool LibraryKnown = InputsKnown && Library.Kind == J::K::Object;
    for (const auto& Key : Library.Keys())
    {
        if (Starts(Key, "CopyBatches"))
        {
            LibraryKnown =
                LibraryKnown && Keys(Library[Key], {"Copies"}) &&
                Every(Optional(Library[Key], "Copies"),
                      [&](const J& Copy)
                      {
                          return Keys(Copy, {"AccessIndex", "DestAccessStartIndex", "DestAccessEndIndex", "Type"}) &&
                                 Is(Optional(Copy, "Type").S(),
                                    {"Plain", "Bool", "Struct", "EPropertyAccessCopyType::Plain",
                                     "EPropertyAccessCopyType::Bool", "EPropertyAccessCopyType::Struct"});
                      });
        }
        else if (Key == "SrcPaths" || Key == "DestPaths")
        {
            LibraryKnown = LibraryKnown &&
                           Every(Library[Key],
                                 [&](const J& Path)
                                 {
                                     // bHasEvents is generated path metadata, not an additional graph
                                     // operation (UE4's property-copy runtime does not consult it).
                                     return Keys(Path, {"PathSegmentStartIndex", "PathSegmentCount", "bHasEvents"});
                                 });
        }
        else if (Key == "PathSegments")
        {
            LibraryKnown = LibraryKnown && Every(Library[Key],
                                                 [&](const J& Segment)
                                                 {
                                                     const auto Flags = Int(Segment, "Flags", 0);
                                                     return Keys(Segment, {"Name", "Struct", "Property", "Function",
                                                                           "ArrayIndex", "Flags"}) &&
                                                            (Flags == 1 || Flags == 2 || Flags == 6 || Flags == 7) &&
                                                            !Optional(Segment, "Function").Truth() &&
                                                            Keys(Optional(Segment, "Property"), {"field", "owner"});
                                                 });
        }
        else
            LibraryKnown = false;
    }
    if (LibraryKnown)
        Out.insert("PropertyAccessLibrary");
    // State-machine notifies are names on fresh FAnimNotifyEvent records. Object
    // notifies and nondefault timing/filter settings are not rebuilt by this path.
    const auto LinkDefault = [&](const J& Link)
    {
        if (!Keys(Link, {"CachedLinkMethod", "LinkMethod", "LinkValue", "LinkedMontage", "LinkedSequence",
                         "SegmentBeginTime", "SegmentIndex", "SegmentLength", "SlotIndex"}))
            return false;
        for (const auto& Key : Link.Keys())
        {
            const auto& Value = Link[Key];
            if (Key == "LinkMethod" || Key == "CachedLinkMethod")
            {
                if (Value.S() != "EAnimLinkMethod::Absolute")
                    return false;
            }
            else if (Key == "SegmentIndex")
            {
                if (Value.I() != -1)
                    return false;
            }
            else if (Value.Truth())
                return false;
        }
        return true;
    };
    std::set<std::string> SourceNotifies;
    const bool NotifiesKnown = Every(
        Optional(ClassValues, "AnimNotifies"),
        [&](const J& Notify)
        {
            if (Notify.Kind != J::K::Object || Optional(Notify, "NotifyName").Kind != J::K::String ||
                !NotifyNames.count(Notify["NotifyName"].S()) || !SourceNotifies.insert(Notify["NotifyName"].S()).second)
                return false;
            for (const auto& Key : Notify.Keys())
            {
                const auto& Value = Notify[Key];
                if (Key == "NotifyName")
                    continue;
                if (Key == "EndLink" || Key == "MiddleLink")
                {
                    if (!LinkDefault(Value))
                        return false;
                }
                else if (Key == "LinkMethod" || Key == "CachedLinkMethod")
                {
                    if (Value.S() != "EAnimLinkMethod::Absolute")
                        return false;
                }
                else if (Key == "MontageTickType")
                {
                    if (Value.S() != "EMontageNotifyTickType::Queued")
                        return false;
                }
                else if (Key == "NotifyFilterType")
                {
                    if (Value.S() != "ENotifyFilterType::NoFiltering")
                        return false;
                }
                else if (Key == "SegmentIndex")
                {
                    if (Value.I() != -1)
                        return false;
                }
                else if (Key == "NotifyTriggerChance")
                {
                    if (Value.D() != 1.0)
                        return false;
                }
                else if (Key == "TriggerWeightThreshold")
                {
                    if (std::abs(Value.D() - 1e-5) > 1e-12)
                        return false;
                }
                else if (Key == "bTriggerOnDedicatedServer")
                {
                    if (!Value.B())
                        return false;
                }
                else if (Is(Key, {"Duration", "EndTriggerTimeOffset", "LinkValue", "LinkedMontage", "LinkedSequence",
                                  "MiddleDuration", "MiddleTriggerTimeOffset", "Notify", "NotifyFilterLOD",
                                  "NotifyStateClass", "SegmentBeginTime", "SegmentLength", "SlotIndex", "TrackIndex",
                                  "TriggerTimeOffset", "bConvertedFromBranchingPoint", "bTriggerOnFollower"}))
                {
                    if (Value.Truth())
                        return false;
                }
                else
                    return false;
            }
            return true;
        });
    if (NotifiesKnown)
        Out.insert("AnimNotifies");
    if (Optional(ClassValues, "SyncGroupNames").Kind == J::K::Array &&
        ClassValues["SyncGroupNames"].Dump() == Optional(Animation, "sync_groups").Dump())
        Out.insert("SyncGroupNames");
    if (Optional(ClassValues, "TargetSkeleton").Kind == J::K::String &&
        ClassValues["TargetSkeleton"].Dump() == Optional(Plan, "skeleton").Dump())
        Out.insert("TargetSkeleton");

    // UE4 rebuilds saved-pose update indices from UseCachedPose -> SaveCachedPose
    // associations. Prove the empty/single-cache case without trusting an index
    // as an editor node identity. More complex ordering stays unhandled.
    try
    {
        const J& Stored = Optional(ClassValues, "OrderedSavedPoseIndicesMap");
        if (!Keys(Stored, {"map"}) || Optional(Stored, "map").Kind != J::K::Array || Stored["map"].Items.size() != 1)
            return Out;
        const J& Pair = Stored["map"][std::size_t(0)];
        if (Pair.Kind != J::K::Array || Pair.Items.size() != 2 || Pair[std::size_t(0)].Kind != J::K::String)
            return Out;
        const J& Indices = Optional(Pair[1], "OrderedSavedPoseNodeIndices");
        if (!Keys(Pair[1], {"OrderedSavedPoseNodeIndices"}) || Indices.Kind != J::K::Array || Indices.Items.size() > 1)
            return Out;
        const auto RootName = Animation["root"].S();
        if (Optional(Value(RootName), "Name").Dump() != Pair[std::size_t(0)].Dump())
            return Out;
        std::vector<std::string> Saves;
        for (const auto& Node : Nodes)
            if (Ends(Node["struct"].S(), ".AnimNode_SaveCachedPose"))
                Saves.push_back(Node["name"].S());
        if (Saves.size() != Indices.Items.size())
            return Out;
        if (!Saves.empty())
        {
            const auto Index = Indices[std::size_t(0)].I();
            if (Index < 0 || std::uint64_t(Index) >= Nodes.size() || NameAt(Index).S() != Saves[0] ||
                !Reachable.count(Saves[0]))
                return Out;
            bool Bound = false;
            std::function<void(const J&)> Inspect = [&](const J& Graph)
            {
                std::set<std::string> Uses;
                for (const auto& Node : Graph["nodes"].Items)
                {
                    if (Ends(Node["struct"].S(), ".AnimNode_UseCachedPose"))
                        Uses.insert(Node["name"].S());
                    if (Node.Has("machine"))
                    {
                        for (const auto& State : Node["machine"]["states"].Items)
                            if (State.Has("graph"))
                                Inspect(State["graph"]);
                        for (const auto& Transition : Node["machine"]["transitions"].Items)
                            if (Transition.Has("custom_graph"))
                                Inspect(Transition["custom_graph"]);
                    }
                }
                for (const auto& Link : Graph["links"].Items)
                    if (Link.Items.size() == 3 && Link[std::size_t(0)].S() == Saves[0] && Uses.count(Link[1].S()) &&
                        Reachable.count(Link[1].S()))
                        Bound = true;
            };
            Inspect(Animation["graph"]);
            if (!Bound)
                return Out;
        }
        Out.insert("OrderedSavedPoseIndicesMap");
    }
    catch (const Error&)
    { /* An unsupported source shape remains explicit. */
    }
    return Out;
}
inline J RecoverBlueprintAnimation(const J& Document, const J& ValueText, GraphSignatures* Signatures = nullptr)
{
    return AnimDetail::Model(Document).Plan(ValueText, Signatures);
}
inline J BlueprintAnimationSummary(const J& Document, GraphSignatures* Signatures = nullptr)
{
    return AnimDetail::Model(Document).Summary(Signatures);
}
}
}

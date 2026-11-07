// Portable recovery of the authored K2 graph boundary. No editor layout or host paths.
#pragma once
#include "blueprint_graph_function.h"

namespace P4MapKit
{
namespace Blueprint
{
namespace GraphDetail
{
class Recovery
{
    GraphOwner OwnerData;
    J Pins(const J& R, bool Outputs)
    {
        J Out = J::Array();
        for (const auto& P : OwnerData.Sig.OfRecord(R).Items)
        {
            const bool IsOut = P["out"].B() && !P["ref"].B();
            if (P["name"].S() == "__WorldContext" || IsOut != Outputs)
                continue;
            Out.Items.push_back(J::Object({{"name", P["name"]},
                                           {"type", OwnerData.PinFull(P["prop"])},
                                           {"ref", P["ref"]},
                                           {"const", Optional(P, "const").Truth()}}));
        }
        return Out;
    }
    void Locals(const J& R, Graph& G)
    {
        for (const auto& P : R["properties"].Items)
        {
            const auto Name = P["name"].S();
            const auto FlagsValue = Bits(P["flags"]);
            if ((FlagsValue & 0x80) || (Temp(Name) && !Starts(Name, "Temp_")))
                continue;
            bool Used = false;
            for (const auto& N : R["script"]["instructions"].Items)
                if (N["opcode"].S() == "EX_LocalVariable")
                {
                    const auto& Path = N["operands"]["property"]["path"].Items;
                    if (!Path.empty() && Path.back().S() == Name)
                    {
                        Used = true;
                        break;
                    }
                }
            if (!Used && Temp(Name))
                continue;
            G.Locals.Items.push_back(J::Object({{"name", Name}, {"type", OwnerData.PinFull(P)}}));
        }
    }
    J Stub(const J& R)
    {
        const auto& S = R["script"];
        if (!S.Truth() || OwnerData.Uber.empty())
            return {};
        const auto& Nodes = S["instructions"];
        J Frame = J::Object(), Entry;
        for (const auto& N : Nodes.Items)
            if (N["parent"].Kind == J::K::Null)
            {
                const auto O = N["opcode"].S();
                const auto& K = N["children"].Items;
                if (O == "EX_LetValueOnPersistentFrame")
                {
                    const auto& V = Nodes[std::size_t(K.at(0).U())];
                    if (!Is(V["opcode"].S(), {"EX_LocalVariable", "EX_LocalOutVariable"}))
                        return {};
                    Frame[N["operands"]["property"]["path"].Items.back().S()] =
                        V["operands"]["property"]["path"].Items.back();
                }
                else if (O == "EX_LocalFinalFunction")
                {
                    const auto Path = Str(N["operands"]["function"]["path"]), Suffix = "." + OwnerData.Uber;
                    if (Path.size() < Suffix.size() || Path.substr(Path.size() - Suffix.size()) != Suffix)
                        return {};
                    Entry = Optional(Nodes[std::size_t(K.at(0).U())]["operands"], "value");
                }
                else if (!Is(O, {"EX_Return", "EX_EndOfScript", "EX_Nothing"}))
                    return {};
            }
        return Entry.Kind == J::K::Null ? J() : J::Array({Entry, Frame});
    }

public:
    Recovery(const J& Doc, GraphSignatures& Sig) : OwnerData(Doc, Sig)
    {
    }
    GraphOwner& Context()
    {
        return OwnerData;
    }
    J EventStub(const J& Record)
    {
        return Stub(Record);
    }
    J Build(const std::string& Only = "")
    {
        const auto& Functions = OwnerData.Doc["functions"];
        J Graphs = J::Array(), Skipped = J::Object();
        struct Event
        {
            std::string Name;
            const J* Record;
            J Handed;
        };
        std::vector<Event> Events;
        const bool Animation = OwnerData.Doc["kind"].S() == "AnimBlueprint";
        for (const auto& Item : Functions.Fields)
        {
            const auto& Name = Item.first;
            const auto& R = Item.second;
            if (Name == OwnerData.Uber || (!Only.empty() && Name != Only && Only != OwnerData.Uber))
                continue;
            if (Animation && (Name == "AnimGraph" || Starts(Name, "EvaluateGraphExposedInputs_")))
                continue;
            if (Starts(Name, "SequenceEvent__ENTRYPOINT"))
                continue;
            if (R["flags"].U() & 0x100000)
            {
                Skipped[Name] = "the signature of an event dispatcher (made with the dispatcher)";
                continue;
            }
            const auto Handed = Stub(R);
            if (Handed.Kind != J::K::Null)
            {
                Events.push_back({Name, &R, Handed});
                continue;
            }
            Graph G(Name, "function");
            const auto Override = OwnerData.FullJ(Optional(R, "super"));
            const auto FlagText = PlanFlags(R["flags"].U());
            J Inputs, Outputs;
            try
            {
                if (!R["script"].Truth())
                    throw Unsupported("no bytecode");
                Inputs = Pins(R, false);
                Outputs = Pins(R, true);
                const auto Entry = G.Node("entry");
                Locals(R, G);
                std::map<std::string, Wire> P;
                for (const auto& V : Inputs.Items)
                    P[V["name"].S()] = G.Pin(Entry, V["name"].S());
                Function Work(OwnerData, R, G, P);
                if (Work.Places().empty())
                    throw Error("empty-bytecode-instructions");
                Work.Run({{Work.Places()[0], G.Pin(Entry, "then")}});
            }
            catch (const Unsupported& Why)
            {
                Skipped[Name] = Why.what();
                if (R["script"].Truth() && Inputs.Kind != J::K::Null)
                {
                    Graph Empty(Name, "function");
                    Empty.Node("entry");
                    J Data = Empty.Data();
                    Data["override"] = Override;
                    Data["flags"] = FlagText;
                    Data["inputs"] = Inputs;
                    Data["outputs"] = Outputs;
                    Data["body"] = false;
                    Graphs.Items.push_back(Data);
                }
                continue;
            }
            J Data = G.Data();
            Data["override"] = Override;
            Data["flags"] = FlagText;
            Data["inputs"] = Inputs;
            Data["outputs"] = Outputs;
            Graphs.Items.push_back(Data);
        }
        if (!OwnerData.Uber.empty() && (!Events.empty() || Only.empty()))
        {
            Graph G("EventGraph", "event");
            const auto& R = Functions[OwnerData.Uber];
            try
            {
                std::map<std::string, Wire> P;
                std::vector<std::pair<std::int64_t, Wire>> Entries;
                std::stable_sort(Events.begin(), Events.end(),
                                 [](const Event& A, const Event& B) { return A.Handed[0].I() < B.Handed[0].I(); });
                for (const auto& E : Events)
                {
                    const auto& S = *E.Record;
                    std::string Interface;
                    if (!Optional(S, "super").Truth())
                        for (const auto& I : OwnerData.Doc["class"]["interfaces"].Items)
                        {
                            const auto Cls = OwnerData.Full(Str(I["cls"]));
                            if (!OwnerData.Sig.Find(Cls, E.Name).empty())
                            {
                                Interface = Cls;
                                break;
                            }
                        }
                    const std::string Kind = Starts(E.Name, "BndEvt__")                           ? "bound_event"
                                             : Optional(S, "super").Truth() || !Interface.empty() ? "event"
                                                                                                  : "custom_event";
                    const auto M = G.Node(Kind, J::Object({{"fn", E.Name}}));
                    if (Kind == "event")
                        G.Nodes[M]["cls"] =
                            Interface.empty() ? GraphDetail::Owner(OwnerData.Full(Str(S["super"]))) : Interface;
                    else if (Kind == "custom_event")
                    {
                        G.Nodes[M]["inputs"] = Pins(S, false);
                        G.Nodes[M]["flags"] = PlanFlags(S["flags"].U());
                    }
                    else
                    {
                        const auto N = E.Name.substr(8);
                        const auto At = N.find("_K2Node_ComponentBoundEvent_");
                        if (At == std::string::npos)
                            throw Error("bound-event-name");
                        const auto Rest = N.substr(At + 28);
                        const auto Sep = Rest.find('_');
                        if (Sep == std::string::npos)
                            throw Error("bound-event-delegate");
                        const auto Delegate = Rest.substr(Sep + 1);
                        const auto Suffix = Delegate.rfind("__DelegateSignature");
                        G.Nodes[M]["component"] = N.substr(0, At);
                        G.Nodes[M]["delegate"] = Suffix == std::string::npos ? Delegate : Delegate.substr(0, Suffix);
                    }
                    for (const auto& K : E.Handed[1].Keys())
                        P[K] = G.Pin(M, E.Handed[1][K].S());
                    Entries.push_back({E.Handed[0].I(), G.Pin(M, "then")});
                }
                Locals(R, G);
                for (const auto& V : G.Locals.Items)
                    if (!P.count(V["name"].S()) && V["name"].S() != "EntryPoint")
                        G.Held[V["name"].S()] = V["type"];
                G.Locals = J::Array();
                for (const auto& V : G.Held)
                    if (!Starts(V.first, "Temp_"))
                        throw Unsupported("a local variable in the event graph that is not a macro's");
                Function Work(OwnerData, R, G, P);
                Work.Run(Entries, Animation);
                if (!Entries.empty() || !Animation)
                    Graphs.Items.push_back(G.Data());
            }
            catch (const Unsupported& Why)
            {
                Skipped[OwnerData.Uber] = Why.what();
            }
        }
        return J::Object({{"graphs", Graphs}, {"skipped", Skipped}});
    }
};
} // namespace GraphDetail
inline J RecoverBlueprintGraphs(const J& Document, GraphSignatures& Signatures, const std::string& Only = "")
{
    return GraphDetail::Recovery(Document, Signatures).Build(Only);
}
inline std::string BlueprintExpressionText(const J& Script, std::size_t Index)
{
    return GraphDetail::Printer(Script).Text(Index);
}
}
} // namespace P4MapKit::Blueprint

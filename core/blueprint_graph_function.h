// Portable recovery of the authored K2 graph boundary: one function's bytecode as graph nodes.
#pragma once
#include "blueprint_graph_nodes.h"

namespace P4MapKit
{
namespace Blueprint
{
namespace GraphDetail
{
class Function
{
protected:
    GraphOwner& OwnerData;
    Graph& G;
    const J& Nodes;
    Printer Print;
    std::vector<std::size_t> Roots;
    std::vector<std::int64_t> Offsets;
    std::map<std::int64_t, std::size_t> At, Positions;
    std::map<std::string, J> Props;
    std::map<std::string, std::uint64_t> Params;
    std::map<std::string, Wire> TempPins, EntryPins;
    std::map<std::string, std::size_t> Alias, Made, Getters;
    std::map<std::string, std::string> Members, MakeVar;
    struct LazyCall
    {
        std::size_t Index;
        std::int64_t Target;
        std::string Slot;
    };
    std::map<std::string, LazyCall> Lazy;
    std::map<std::int64_t, Wire> ExecAt;
    std::map<std::string, std::int64_t> Resume;
    std::map<std::string, std::vector<std::pair<std::string, std::int64_t>>> After;
    std::set<std::string> SwitchIndex, Absorbed, Awaited;
    std::set<std::int64_t> Skip;
    struct LoopRecord
    {
        std::size_t Node;
        std::string Array, Index;
    };
    std::vector<LoopRecord> Loops;
    std::map<std::int64_t, std::pair<std::size_t, Wire>> Deferred;
    bool Leading = true, KeepsReturn = false;
    std::int64_t LastResult = -2;
    std::size_t Result = 0, Steps = 0, Depth = 0;
    std::string User;
    struct DepthGuard
    {
        std::size_t& Value;
        explicit DepthGuard(std::size_t& V) : Value(V)
        {
            if (Value >= 128)
                throw Error("graph-recovery-depth");
            ++Value;
        }
        ~DepthGuard()
        {
            --Value;
        }
    };
    void Step()
    {
        if (++Steps > 10000000)
            throw Error("graph-recovery-budget");
    }
    const J& N(std::size_t I) const
    {
        return Nodes[I];
    }
    std::string Op(std::size_t I) const
    {
        return N(I)["opcode"].S();
    }
    std::vector<std::size_t> Kids(std::size_t I) const
    {
        std::vector<std::size_t> V;
        for (const auto& X : N(I)["children"].Items)
            V.push_back(std::size_t(X.U()));
        return V;
    }
    std::vector<std::size_t> Arguments(std::size_t I) const
    {
        auto V = Kids(I);
        V.erase(std::remove_if(V.begin(), V.end(), [&](std::size_t X) { return Op(X) == "EX_EndFunctionParms"; }),
                V.end());
        return V;
    }
    std::string Var(std::size_t I) const
    {
        if (Is(Op(I), {"EX_LocalVariable", "EX_InstanceVariable", "EX_LocalOutVariable", "EX_DefaultVariable"}))
        {
            const auto& V = N(I)["operands"]["property"]["path"].Items;
            if (!V.empty())
                return V.back().S();
        }
        return "";
    }
    std::int64_t Offset(std::size_t I) const
    {
        return N(I)["runtime_offset"].I();
    }
    std::int64_t Address(std::size_t I) const
    {
        return N(I)["operands"]["address"]["value"].I();
    }
    std::int64_t Next(std::size_t P, std::size_t Count = 1) const
    {
        return P + Count < Offsets.size() ? Offsets[P + Count] : -1;
    }
    std::string Key(const std::string& T, std::initializer_list<std::string> V) const
    {
        J A = J::Array({T});
        for (const auto& X : V)
            A.Items.push_back(X);
        return A.Dump();
    }
    J Literal(std::size_t I)
    {
        DepthGuard Guard(Depth);
        Step();
        const auto O = Op(I);
        const auto& P = N(I)["operands"];
        const auto K = Kids(I);
        if (Is(O, {"EX_IntConst", "EX_ByteConst", "EX_IntConstByte", "EX_Int64Const", "EX_UInt64Const"}))
            return J(P["value"].Text);
        if (O == "EX_FloatConst")
            return Float(P["value"].D(), 9);
        if (O == "EX_StringConst" || O == "EX_UnicodeStringConst")
            return P["value"];
        if (O == "EX_NameConst")
            return P["name"];
        if (O == "EX_True" || O == "EX_IntOne")
            return O == "EX_True" ? "true" : "1";
        if (O == "EX_False" || O == "EX_IntZero")
            return O == "EX_False" ? "false" : "0";
        if ((O == "EX_ArrayConst" && Optional(P, "count").Kind == J::K::Number && P["count"].U() == 0) ||
            O == "EX_NoObject" || O == "EX_NoInterface")
            return J("");
        if (O == "EX_VectorConst" || O == "EX_RotationConst")
        {
            std::vector<std::string> V;
            for (const auto& X : P["value"].Items)
                V.push_back(Float(X.D(), 6, true));
            return Join(V, ",");
        }
        if (O == "EX_StructConst")
        {
            auto F = OwnerData.Sig.Fields.find(Str(P["struct"]["path"]));
            if (F != OwnerData.Sig.Fields.end() && F->second.Items.size() + 1 == K.size())
            {
                std::vector<std::string> V;
                for (std::size_t X = 0; X + 1 < K.size(); ++X)
                {
                    const auto E = Element(K[X]);
                    if (E.Kind == J::K::Null)
                        return {};
                    V.push_back(F->second[X].S() + "=" + E.S());
                }
                return "(" + Join(V, ",") + ")";
            }
        }
        return {};
    }
    J Element(std::size_t I)
    {
        J V = Literal(I);
        if (V.Kind != J::K::Null && Is(Op(I), {"EX_StringConst", "EX_UnicodeStringConst", "EX_NameConst"}))
            V = QuoteElement(V.S());
        return V;
    }
    std::size_t Unalias(std::size_t I)
    {
        for (int Seen = 0; Seen < 20 && Op(I) == "EX_LocalVariable" && Alias.count(Var(I)); ++Seen)
            I = Alias.at(Var(I));
        return I;
    }
    virtual void Feed(std::size_t I, const Wire& To)
    {
        Step();
        I = Unalias(I);
        User = To.first;
        const auto V = Literal(I);
        if (V.Kind != J::K::Null)
        {
            G.Charge(To.second.size() + V.S().size());
            G.ById(To.first)["defaults"][To.second] = V;
            return;
        }
        if (Op(I) == "EX_ObjectConst")
        {
            G.Charge(To.second.size() + N(I)["operands"]["object"]["path"].Dump().size());
            G.ById(To.first)["objects"][To.second] = N(I)["operands"]["object"]["path"];
            return;
        }
        if (Op(I) == "EX_Self")
        {
            G.Link(G.Pin(G.Node("self"), "self"), To);
            return;
        }
        G.Link(Source(I), To);
    }
    Wire Holder(const std::string& Name)
    {
        const auto K = Key("held", {Name});
        if (!Made.count(K))
        {
            auto Middle = std::regex_replace(Name, std::regex("_Variable(_[0-9]+)?$"), "").substr(5);
            const auto P = Middle.find('_');
            Made[K] = G.Node("temp_var", J::Object({{"var", Name},
                                                    {"type", G.Held.at(Name)},
                                                    {"comment", P == std::string::npos ? "" : Middle.substr(P + 1)},
                                                    {"pure", true}}));
        }
        return G.Pin(Made.at(K), "Variable");
    }
    Wire Getter(const std::string& Name, std::int64_t Target, const std::string& Owner, bool Local = false)
    {
        if (Local && Target < 0 && G.Held.count(Name))
            return Holder(Name);
        const auto K = Key("get", {Name, Target < 0 ? "" : Print.Text(std::size_t(Target)), Local ? "1" : "0", User});
        if (!Getters.count(K))
        {
            J V = J::Object({{"var", Name}, {"local", Local}});
            if (!Owner.empty())
                V["cls"] = OwnerData.Full(Owner);
            const auto M = G.Node("get", V);
            Getters[K] = M;
            if (Target >= 0)
                Feed(std::size_t(Target), G.Pin(M, "self"));
        }
        return G.Pin(Getters.at(K), Name);
    }
    Wire Source(std::size_t I)
    {
        DepthGuard Guard(Depth);
        Step();
        const auto O = Op(I), Name = Var(I);
        const auto K = Kids(I);
        const auto& P = N(I)["operands"];
        if (O == "EX_LocalVariable" || O == "EX_LocalOutVariable")
        {
            if (Alias.count(Name))
                return Source(Alias.at(Name));
            if (Lazy.count(Name) && !TempPins.count(Name))
            {
                auto L = Lazy.at(Name);
                const auto M = CallNode(L.Index, L.Target, L.Slot);
                if (M == SIZE_MAX)
                    throw Unsupported("unresolved math node");
                TempPins.emplace(Name, G.Pin(M, "ReturnValue"));
            }
            if (TempPins.count(Name))
                return TempPins.at(Name);
            if (EntryPins.count(Name))
                return EntryPins.at(Name);
            if (Temp(Name) && !Starts(Name, "Temp_"))
            {
                if (G.Kind == "event")
                {
                    Awaited.insert(Name);
                    return {"?" + Name, ""};
                }
                throw Unsupported("temporary " + Name + " is read before a node wrote it");
            }
            return Getter(Name, -1, "", true);
        }
        if (O == "EX_InstanceVariable")
            return Getter(Name, -1, "");
        if (O == "EX_Context" || O == "EX_Context_FailSilent")
        {
            const auto Inner = Op(K.at(1));
            if (Inner == "EX_InstanceVariable")
                return Getter(Var(K[1]), std::int64_t(K[0]), Str(N(K[1])["operands"]["property"]["owner"]));
            if (Call(Inner))
                return G.Pin(CallNode(K[1], std::int64_t(K[0])), "ReturnValue");
            throw Unsupported("context around " + Inner);
        }
        if (O == "EX_StructMemberContext")
        {
            const auto S = Str(P["property"]["owner"]), Cache = Key("break", {S, Print.Text(K.at(0))});
            if (!Made.count(Cache))
            {
                const auto M = G.Node("break_struct", J::Object({{"struct", OwnerData.Full(S)}}));
                Made[Cache] = M;
                Feed(K[0], G.Pin(M, Leaf(S)));
            }
            return G.Pin(Made.at(Cache), P["property"]["path"].Items.back().S());
        }
        if (Call(O))
            return G.Pin(CallNode(I), "ReturnValue");
        if (O == "EX_SwitchValue")
            return Select(I);
        if (O == "EX_ArrayGetByRef")
        {
            const auto M = G.Node("array_get");
            Feed(K.at(0), G.Pin(M, "Array"));
            Feed(K.at(1), G.Pin(M, "Dimension 1"));
            return G.Pin(M, "Output");
        }
        if (Is(O, {"EX_ObjToInterfaceCast", "EX_InterfaceToObjCast", "EX_CrossInterfaceCast", "EX_InterfaceContext"}))
            return Source(K.at(0));
        if (O == "EX_Self")
            return G.Pin(G.Node("self"), "self");
        throw Unsupported("value of " + O);
    }
    Wire Select(std::size_t I)
    {
        const auto K = Kids(I);
        const auto Count = N(I)["operands"]["cases"].U();
        const auto Default = K.empty() ? "" : Var(K.back()), Cache = Key("select", {Default, Print.Text(I)});
        if (!Default.empty() && Made.count(Cache))
            return G.Pin(Made.at(Cache), "ReturnValue");
        const auto M = G.Node("select", J::Object({{"options", J(Count)}}));
        Made[Cache] = M;
        const auto* P = OwnerData.Prop(Default, Props);
        if (P)
        {
            try
            {
                G.Nodes[M]["type"] = OwnerData.PinFull(*P);
            }
            catch (const Error&)
            {
            }
        }
        Feed(K.at(0), G.Pin(M, "Index"));
        J Cases = J::Array();
        for (std::size_t X = 0; X < Count; ++X)
        {
            Cases.Items.push_back(Literal(K.at(1 + 2 * X)));
            Feed(K.at(2 + 2 * X), G.Pin(M, "Option " + std::to_string(X)));
        }
        G.Nodes[M]["cases"] = Cases;
        return G.Pin(M, "ReturnValue");
    }
    std::string TypeOf(std::size_t I)
    {
        const auto O = Op(I), Name = Var(I);
        const J* P = nullptr;
        if (O == "EX_LocalVariable" || O == "EX_LocalOutVariable")
            P = OwnerData.Prop(Name, Props);
        else if (O == "EX_InstanceVariable")
        {
            const auto F = OwnerData.Variables.find(Name);
            if (F != OwnerData.Variables.end())
                P = &F->second;
        }
        return P && Optional(*P, "class").Truth() ? OwnerData.Full((*P)["class"].S()) : "";
    }
    std::pair<std::string, std::string> Resolve(std::size_t I, std::int64_t Target)
    {
        const auto& P = N(I)["operands"];
        if (P.Has("function"))
        {
            const auto Path = OwnerData.Full(Str(P["function"]["path"]));
            return {Path, Leaf(Path)};
        }
        const auto Name = P["function_name"].S();
        const auto Cls = Target < 0 ? OwnerData.ClassPath : TypeOf(std::size_t(Target));
        auto Path = Cls.empty() ? "" : OwnerData.Sig.Find(Cls, Name);
        if (Path.empty())
            Path = OwnerData.Sig.Anywhere(Name);
        return {Path, Name};
    }
    std::size_t CallNode(std::size_t I, std::int64_t Target = -1, const std::string& Slot = "")
    {
        DepthGuard Guard(Depth);
        Step();
        const auto R = Resolve(I, Target);
        const auto Found = R.first.empty() ? J() : OwnerData.Sig.Get(R.first);
        if (Found.Kind == J::K::Null)
            throw Unsupported("function " + R.second + " has no known parameter list");
        const auto& Parameters = Found[0].Items;
        const auto FlagsValue = Found[1].U();
        const bool Pure = (FlagsValue & 0x10000000) != 0;
        const auto K = Kids(I);
        if (R.first == "/Script/Engine.KismetNodeHelperLibrary.GetValidValue" && !K.empty() &&
            Op(K[0]) == "EX_ObjectConst")
        {
            const auto Cache = Key("byte_to_enum", {Print.Text(I), Slot});
            if (!Made.count(Cache))
            {
                const auto M = G.Node(
                    "byte_to_enum",
                    J::Object({{"enum", OwnerData.Full(Str(N(K[0])["operands"]["object"]["path"]))}, {"pure", true}}));
                Made[Cache] = M;
                Feed(K.at(1), G.Pin(M, "Byte"));
            }
            return Made.at(Cache);
        }
        for (const auto C : K)
            if (Call(Op(C)))
                return Math(I, Slot);
        const auto Cache = Key("call", {Print.Text(I), Target < 0 ? "" : Print.Text(std::size_t(Target)), Slot});
        const bool Cached = Pure && Made.count(Cache);
        const auto Owner = GraphDetail::Owner(R.first);
        const bool Parent = Is(Op(I), {"EX_FinalFunction", "EX_LocalFinalFunction"}) && Target < 0 &&
                            (FlagsValue & 0x8000000) && Owner != OwnerData.ClassPath;
        const auto M = Cached ? Made.at(Cache)
                              : G.Node(Parent ? "call_parent" : "call",
                                       J::Object({{"fn", R.second}, {"cls", Owner}, {"pure", Pure}}));
        if (Pure)
            Made[Cache] = M;
        if (!Cached && Target >= 0 && Op(std::size_t(Target)) != "EX_ObjectConst")
            Feed(std::size_t(Target), G.Pin(M, "self"));
        const auto Args = Arguments(I);
        std::vector<J> Listed;
        for (const auto& P : Parameters)
            if (!P["ret"].B())
                Listed.push_back(P);
        if (Args.size() != Listed.size())
            throw Unsupported(R.second + ": " + std::to_string(Args.size()) + " arguments for " +
                              std::to_string(Listed.size()) + " parameters");
        for (std::size_t X = 0; X < Args.size(); ++X)
        {
            const auto A = Args[X];
            const auto& P = Listed[X];
            const auto Written = Var(A);
            if (P["out"].B() && !P["ref"].B())
            {
                if (Written.empty() || Op(A) != "EX_LocalVariable")
                    throw Unsupported(R.second + ": output " + P["name"].S() + " is not written to a local");
                if (!Temp(Written))
                    throw Unsupported(R.second + ": output " + P["name"].S() + " is written to the variable " +
                                      Written);
                TempPins[Written] = G.Pin(M, P["name"].S());
                continue;
            }
            if (Written == "__WorldContext" || Cached)
                continue;
            if (Op(A) == "EX_StructConst" &&
                Str(N(A)["operands"]["struct"]["path"]) == "/Script/Engine.LatentActionInfo")
            {
                const auto First = Kids(A).at(0);
                Resume[G.Id(M)] = Op(First) == "EX_SkipOffsetConst" ? N(First)["operands"]["value"].I() : -1;
                continue;
            }
            Feed(A, G.Pin(M, P["name"].S()));
        }
        return M;
    }
    std::string Expression(std::size_t I, std::vector<std::string>& Names)
    {
        Step();
        const auto O = Op(I);
        if (O == "EX_InstanceVariable" && N(I)["operands"]["property"]["path"].Items.size() == 1)
        {
            const auto Name = Var(I);
            if (!OwnerData.Variables.count(Name) && std::find(Names.begin(), Names.end(), Name) == Names.end())
                Names.push_back(Name);
            return Name;
        }
        if (O == "EX_FloatConst")
        {
            const double V = N(I)["operands"]["value"].D();
            double W = V;
            for (int X = 1; X < 10; ++X)
            {
                const auto S = Float(V, X);
                const auto D = std::stod(S);
                if (double(float(D)) == V)
                {
                    W = D;
                    break;
                }
            }
            return Repr(W);
        }
        if (Is(O, {"EX_IntConst", "EX_IntConstByte", "EX_IntZero", "EX_IntOne"}))
            return Literal(I).S();
        if (O == "EX_CallMath")
        {
            const auto R = Resolve(I, -1);
            const auto A = Arguments(I);
            const auto P = R.second.find('_');
            const auto Name = R.second.substr(0, P);
            static const std::map<std::string, std::string> Ops = {
                {"Add", "+"}, {"Subtract", "-"}, {"Multiply", "*"}, {"Divide", "/"}};
            const auto S = Ops.find(Name);
            if (S != Ops.end() && A.size() == 2 && Starts(R.first, "/Script/Engine.KismetMathLibrary."))
                return "(" + Expression(A[0], Names) + " " + S->second + " " + Expression(A[1], Names) + ")";
        }
        throw Unsupported("a call written inside another call that is not plain arithmetic on variables and constants");
    }
    std::size_t Math(std::size_t I, const std::string& Slot)
    {
        const auto Cache = Key("math", {Print.Text(I), Slot});
        if (!Made.count(Cache))
        {
            std::vector<std::string> Names;
            std::string Text;
            try
            {
                Text = Expression(I, Names);
            }
            catch (const Unsupported&)
            {
                if (Starts(Slot, "K2Node_MathExpression"))
                    return SIZE_MAX;
                throw;
            }
            const auto M =
                G.Node("math", J::Object({{"expression", Starts(Text, "(") ? Text.substr(1, Text.size() - 2) : Text},
                                          {"pure", true}}));
            Made[Cache] = M;
            if (Starts(Slot, "K2Node_MathExpression"))
            {
                G.Nodes[M]["of"] = Slot;
                if (OwnerData.MathNodes.count(Slot))
                {
                    G.Nodes[M]["again"] = true;
                    return M;
                }
                OwnerData.MathNodes.insert(Slot);
            }
            for (const auto& Name : Names)
            {
                const auto Got = G.Node("get", J::Object({{"var", Name}, {"local", false}}));
                G.Link(G.Pin(Got, Name), G.Pin(M, Name));
            }
        }
        return Made.at(Cache);
    }
    bool Later(std::size_t I, std::int64_t Target, const std::string& ResultName = "")
    {
        const auto R = Resolve(I, Target);
        const auto F = R.first.empty() ? J() : OwnerData.Sig.Get(R.first);
        if (F.Kind == J::K::Null)
            throw Unsupported("function " + R.second + " has no known parameter list");
        if (!(F[1].U() & 0x10000000))
            return false;
        const auto A = Arguments(I);
        if (R.second == "Array_Get" && A.size() == 3)
            for (const auto& L : Loops)
                if (Print.Text(A[0]) == L.Array && Var(A[1]) == L.Index)
                {
                    const auto E = Var(A[2]);
                    Lazy.erase(E);
                    TempPins[E] = G.Pin(L.Node, "Array Element");
                    return true;
                }
        std::vector<J> Listed;
        for (const auto& P : F[0].Items)
            if (!P["ret"].B())
                Listed.push_back(P);
        std::vector<std::string> W;
        for (std::size_t X = 0; X < std::min(A.size(), Listed.size()); ++X)
            if (Listed[X]["out"].B() && !Listed[X]["ref"].B())
                W.push_back(Var(A[X]));
        W.push_back(ResultName);
        for (const auto& Name : W)
            if (!Name.empty() && Temp(Name))
            {
                TempPins.erase(Name);
                Lazy[Name] = {I, Target, ResultName};
            }
        return true;
    }
    bool Default(std::size_t I)
    {
        const auto O = Op(I);
        const auto K = Kids(I);
        if (Is(O, {"EX_SetArray", "EX_SetSet", "EX_SetMap"}))
        {
            const auto Name = Var(K.at(0));
            std::vector<std::string> V;
            bool Valid = true;
            for (std::size_t X = 1; X + 1 < K.size(); ++X)
            {
                const auto E = Element(K[X]);
                if (E.Kind == J::K::Null)
                {
                    Valid = false;
                    break;
                }
                V.push_back(E.S());
            }
            if (Starts(Name, "K2Node_MakeVariable") && Valid)
            {
                if (O == "EX_SetMap")
                {
                    std::vector<std::string> P;
                    for (std::size_t X = 0; X + 1 < V.size(); X += 2)
                        P.push_back("(" + V[X] + ", " + V[X + 1] + ")");
                    V = P;
                }
                MakeVar[Name] = "(" + Join(V, ",") + ")";
                return true;
            }
        }
        if (Let(O) && Op(K.at(0)) == "EX_LocalVariable" && G.Kind == "function")
        {
            const auto Name = Var(K[0]);
            const auto Held = Op(K.at(1)) == "EX_LocalVariable" ? Var(K[1]) : "";
            J Text = Held.empty() ? Literal(K[1]) : MakeVar.count(Held) ? J(MakeVar.at(Held)) : J();
            for (auto& V : G.Locals.Items)
                if (V["name"].S() == Name && !Temp(Name) && !Params.count(Name) && Text.Kind != J::K::Null &&
                    !V.Has("default"))
                {
                    V["default"] = Text;
                    std::size_t Order = 0;
                    for (const auto& P : G.Locals.Items)
                        if (P.Has("order"))
                            ++Order;
                    V["order"] = J(std::uint64_t(Order));
                    return true;
                }
        }
        return false;
    }
    Wire Flow(std::int64_t Offset)
    {
        std::set<std::int64_t> Seen;
        while (Offset >= 0 && !Seen.count(Offset))
        {
            Step();
            Seen.insert(Offset);
            if (ExecAt.count(Offset))
                return ExecAt.at(Offset);
            if (!At.count(Offset))
                throw Error("graph-flow-address");
            const auto I = At.at(Offset);
            const auto O = Op(I);
            if (O == "EX_Jump")
            {
                Offset = Address(I);
                continue;
            }
            if (Is(O, {"EX_Return", "EX_EndOfScript", "EX_PopExecutionFlow"}))
                return {};
            Offset = Next(Positions.at(Offset));
        }
        return {};
    }
    void Executes(std::size_t M, std::int64_t Offset, std::size_t Position, const std::string& PinName = "execute",
                  const std::string& Then = "then", std::size_t Count = 1)
    {
        const auto Id = G.Id(M);
        ExecAt[Offset] = G.Pin(M, PinName);
        if (Resume.count(Id))
        {
            const auto Target = Resume.at(Id);
            Resume.erase(Id);
            if (Target >= 0)
                After[Id].push_back({Then.empty() ? "then" : Then, Target});
            return;
        }
        if (!Then.empty())
            After[Id].push_back({Then, Next(Position, Count)});
    }
    std::size_t Cast(std::size_t I, std::size_t Position)
    {
        const auto K = Kids(I), V = Kids(K.at(1));
        const bool Meta = Op(K[1]) == "EX_MetaCast";
        const auto M =
            G.Node("cast", J::Object({{"cls", N(K[1])["operands"]["class"]["path"]}, {"meta", Meta}, {"pure", true}}));
        Feed(V.at(0), G.Pin(M, Meta ? "Class" : "Object"));
        TempPins[Var(K[0])] = G.Pin(M, "As");
        std::size_t Taken = 1;
        if (Position + 1 < Roots.size())
        {
            const auto F = Roots[Position + 1];
            const auto FK = Kids(F);
            if (Let(Op(F)) && FK.size() > 1 && Op(FK[1]) == "EX_PrimitiveCast")
            {
                TempPins[Var(FK[0])] = G.Pin(M, "bSuccess");
                Taken = 2;
                if (Position + 2 < Roots.size())
                {
                    const auto Jump = Roots[Position + 2];
                    if (Op(Jump) == "EX_JumpIfNot" && Var(Kids(Jump).at(0)) == Var(FK[0]))
                    {
                        G.Nodes[M]["pure"] = false;
                        ExecAt[Offset(I)] = G.Pin(M, "execute");
                        After[G.Id(M)] = {{"then", Next(Position, 3)}, {"CastFailed", Address(Jump)}};
                        Taken = 3;
                    }
                }
            }
        }
        return Taken;
    }
    std::size_t Switch(std::size_t Position)
    {
        const auto First = Roots.at(Position);
        const auto Temporary = Var(Kids(First).at(0));
        const auto Base = std::regex_replace(Temporary, std::regex("_[0-9]+$"), "");
        static const std::map<std::string, std::pair<std::string, std::vector<std::string>>> Kinds = {
            {"K2Node_SwitchInteger_CmpSuccess", {"switch_int", {"NotEqual_IntInt"}}},
            {"K2Node_SwitchName_CmpSuccess", {"switch_name", {"NotEqual_NameName"}}},
            {"K2Node_SwitchString_CmpSuccess", {"switch_string", {"NotEqual_StriStri", "NotEqual_StrStr"}}},
            {"K2Node_SwitchEnum_CmpSuccess", {"switch_enum", {"NotEqual_ByteByte"}}}};
        const auto KindI = Kinds.find(Base);
        if (KindI == Kinds.end())
            return 0;
        const auto Kind = KindI->second.first;
        std::vector<std::pair<J, std::int64_t>> Cases;
        std::size_t Taken = 0, Chosen = 0;
        std::string Selection, FunctionName;
        while (Position + Taken + 1 < Roots.size())
        {
            const auto L = Roots[Position + Taken], Jump = Roots[Position + Taken + 1];
            const auto LK = Kids(L), JK = Kids(Jump);
            const auto Name = Let(Op(L)) && !LK.empty() ? Var(LK[0]) : "";
            if (Name != Temporary || Op(Jump) != "EX_JumpIfNot" || JK.empty() || Var(JK[0]) != Temporary)
                break;
            const auto C = LK.at(1);
            const auto A = Kids(C);
            if (A.size() < 2)
                break;
            const auto Text = Print.Text(A[0]),
                       Compared = Leaf(Str(Optional(Optional(N(C)["operands"], "function"), "path")));
            if ((!Selection.empty() && Selection != Text) ||
                std::find(KindI->second.second.begin(), KindI->second.second.end(), Compared) ==
                    KindI->second.second.end() ||
                (!FunctionName.empty() && FunctionName != Compared))
                break;
            J Value = Literal(A[1]);
            if (Value.Kind == J::K::Null)
                break;
            if (Kind == "switch_int")
            {
                Value = J(std::int64_t(std::stoll(Value.S())));
                if (!Cases.empty() && Value.I() <= Cases.back().first.I())
                    break;
            }
            Selection = Text;
            Chosen = A[0];
            FunctionName = Compared;
            Cases.push_back({Value, Address(Jump)});
            Taken += 2;
        }
        if (Cases.empty())
        {
            if (Kind == "switch_int")
                throw Unsupported("a switch on integer without cases");
            return 0;
        }
        J Fields = J::Object();
        if (Kind == "switch_int")
        {
            Fields["start"] = Cases.front().first;
            const auto A = Cases.front().first.I(), B = Cases.back().first.I();
            if (B - A > 100000)
                throw Error("graph-switch-budget");
            Fields["cases"] = J(B - A + 1);
        }
        else if (Kind == "switch_enum")
        {
            const auto* P = OwnerData.Prop(Var(Chosen), Props, true);
            if (!P || !Optional(*P, "enum").Truth())
                return 0;
            Fields["enum"] = OwnerData.Full((*P)["enum"].S());
            J V = J::Array();
            for (const auto& C : Cases)
                V.Items.push_back(J(std::int64_t(std::stoll(C.first.S()))));
            Fields["cases"] = V;
        }
        else
        {
            J V = J::Array();
            for (const auto& C : Cases)
                V.Items.push_back(C.first);
            Fields["cases"] = V;
            if (Kind == "switch_string")
                Fields["case_sensitive"] = FunctionName == "NotEqual_StrStr";
        }
        const auto M = G.Node(Kind, Fields);
        Feed(Chosen, G.Pin(M, "Selection"));
        ExecAt[Offset(First)] = G.Pin(M, "execute");
        auto& W = After[G.Id(M)];
        for (const auto& C : Cases)
            W.push_back({C.first.Kind == J::K::Number ? C.first.Text : C.first.S(), C.second});
        W.push_back({"default", Next(Position, Taken)});
        return Taken;
    }
    std::size_t Loop(std::size_t Position)
    {
        auto LetAt = [&](std::size_t P) -> std::pair<std::string, std::size_t>
        {
            if (P >= Roots.size())
                return {"", SIZE_MAX};
            const auto I = Roots[P];
            const auto K = Kids(I);
            if (!Let(Op(I)) || K.size() < 2 || Op(K[0]) != "EX_LocalVariable")
                return {"", SIZE_MAX};
            return {Var(K[0]), K[1]};
        };
        auto FunctionName = [&](std::size_t I)
        {
            return Leaf(Str(Optional(Optional(N(I)["operands"], "function"), "path")));
        };
        const auto Start = Position;
        std::string Breaker;
        auto L = LetAt(Position);
        if (Starts(L.first, "Temp_bool_True_if_break_was_hit_Variable"))
        {
            if (Op(L.second) != "EX_False")
                return 0;
            Breaker = L.first;
            ++Position;
        }
        L = LetAt(Position);
        const auto Counter = L.first;
        if (!Starts(Counter, "Temp_int_Loop_Counter_Variable"))
            return 0;
        const auto V = Literal(L.second);
        const bool Reverse = V.Kind == J::K::Null || V.S() != "0";
        std::string IndexName;
        std::size_t FirstIndex = SIZE_MAX, AtPos;
        if (Reverse)
        {
            const auto Begun = Lazy.find(Var(L.second));
            if (!Breaker.empty() || Begun == Lazy.end() || FunctionName(Begun->second.Index) != "Subtract_IntInt")
                return 0;
            AtPos = Position + 1;
        }
        else
        {
            L = LetAt(Position + 1);
            const auto Value = L.second == SIZE_MAX ? J() : Literal(L.second);
            if (!Starts(L.first, "Temp_int_Array_Index_Variable") || Value.Kind == J::K::Null || Value.S() != "0")
                return 0;
            IndexName = L.first;
            AtPos = Position + 2;
        }
        std::map<std::string, std::size_t> Calls;
        while (AtPos < Roots.size() && Op(Roots[AtPos]) != "EX_JumpIfNot")
        {
            L = LetAt(AtPos);
            if (Reverse && Starts(L.first, "Temp_int_Array_Index_Variable") && IndexName.empty())
            {
                IndexName = L.first;
                FirstIndex = AtPos;
            }
            else if (!L.first.empty() && Temp(L.first))
            {
                auto I = L.second;
                if (Op(I) == "EX_Context" || Op(I) == "EX_Context_FailSilent")
                    I = Kids(I).at(1);
                if (Call(Op(I)))
                    Calls[L.first] = I;
            }
            ++AtPos;
            if (AtPos - Position > 14)
                return 0;
        }
        if (AtPos + 2 >= Roots.size())
            return 0;
        const auto Jump = Roots[AtPos];
        const auto Condition = Var(Kids(Jump).at(0));
        auto Less = Calls.count(Condition) ? Calls.at(Condition) : SIZE_MAX;
        if (!Breaker.empty() && Less != SIZE_MAX && FunctionName(Less) == "BooleanAND")
        {
            std::size_t Found = SIZE_MAX;
            for (const auto A : Arguments(Less))
            {
                const auto Name = Var(A);
                if (Calls.count(Name) && FunctionName(Calls.at(Name)) == "Less_IntInt")
                {
                    Found = Calls.at(Name);
                    break;
                }
            }
            Less = Found;
        }
        std::size_t Length = SIZE_MAX;
        if (Reverse)
        {
            for (const auto& C : Calls)
                if (FunctionName(C.second) == "Array_Length")
                {
                    Length = C.second;
                    break;
                }
            if (Less == SIZE_MAX || FunctionName(Less) != "GreaterEqual_IntInt" || Arguments(Less).empty() ||
                Var(Arguments(Less)[0]) != Counter || IndexName.empty() || Length == SIZE_MAX)
                return 0;
        }
        else
        {
            if (Less == SIZE_MAX || FunctionName(Less) != "Less_IntInt" || Arguments(Less).size() < 2 ||
                Var(Arguments(Less)[0]) != Counter)
                return 0;
            const auto Name = Var(Arguments(Less)[1]);
            Length = Calls.count(Name) ? Calls.at(Name) : SIZE_MAX;
            if (Length == SIZE_MAX || FunctionName(Length) != "Array_Length")
                return 0;
        }
        const auto A = Arguments(Length);
        if (A.empty())
            return 0;
        const auto Array = A[0];
        L = LetAt(AtPos + 1);
        const auto Push = Roots[AtPos + 2];
        if (L.first != IndexName || L.second == SIZE_MAX || Var(L.second) != Counter ||
            Op(Push) != "EX_PushExecutionFlow")
            return 0;
        const auto StepOffset = Address(Push);
        if (!Positions.count(StepOffset) || LetAt(Positions.at(StepOffset) + 1).first != Counter)
            return 0;
        const auto M = G.Node("macro", J::Object({{"macro", Reverse           ? "ReverseForEachLoop"
                                                            : Breaker.empty() ? "ForEachLoop"
                                                                              : "ForEachLoopWithBreak"}}));
        if (Reverse)
            Skip.insert(Offsets.at(FirstIndex));
        TempPins[IndexName] = G.Pin(M, "Array Index");
        Loops.push_back({M, Print.Text(Array), IndexName});
        Absorbed.insert(Counter);
        Absorbed.insert(IndexName);
        Absorbed.insert(Breaker);
        ExecAt[Offsets.at(Start)] = G.Pin(M, "Exec");
        After[G.Id(M)] = {{"LoopBody", Next(AtPos + 2)}, {"Completed", Address(Jump)}};
        Skip.insert(Offsets.at(AtPos));
        Skip.insert(Offsets.at(AtPos + 1));
        Skip.insert(Offsets.at(AtPos + 2));
        Skip.insert(Offsets.at(Positions.at(StepOffset) + 1));
        Deferred[Offsets.at(AtPos)] = {Array, G.Pin(M, "Array")};
        if (!Breaker.empty())
            for (const auto I : Roots)
            {
                const auto K = Kids(I);
                if (Let(Op(I)) && K.size() > 1 && Var(K[0]) == Breaker && Op(K[1]) == "EX_True")
                {
                    ExecAt[Offset(I)] = G.Pin(M, "Break");
                    Skip.insert(Offset(I));
                }
            }
        return Reverse ? 1 : Position + 2 - Start;
    }
    std::size_t Increment(std::size_t I, std::size_t Position)
    {
        const auto K = Kids(I);
        const auto Name = Var(K.at(0)), Held = Var(K.at(1));
        if (!Lazy.count(Held) || Op(K[1]) != "EX_LocalVariable" || Position + 1 >= Roots.size() ||
            !Let(Op(Roots[Position + 1])))
            return 0;
        const auto CallI = Lazy.at(Held).Index, Follow = Roots[Position + 1];
        const auto Path = Str(Optional(Optional(N(CallI)["operands"], "function"), "path"));
        const auto Fn = Leaf(Path);
        const std::string Macro = Fn == "Add_IntInt" ? "IncrementInt" : Fn == "Subtract_IntInt" ? "DecrementInt" : "";
        const auto A = Arguments(CallI), FK = Kids(Follow);
        if (std::string(Macro).empty() || A.size() != 2 || Literal(A[1]).Kind == J::K::Null ||
            Literal(A[1]).S() != "1" || FK.size() < 2 || Var(FK[1]) != Name || Print.Text(FK[0]) != Print.Text(A[0]))
            return 0;
        const auto M = G.Node("macro", J::Object({{"macro", Macro}}));
        Feed(A[0], G.Pin(M, "in"));
        TempPins[Name] = G.Pin(M, "out");
        Absorbed.insert(Name);
        Lazy.erase(Held);
        Executes(M, Offset(I), Position, "execute", "then", 2);
        return 2;
    }
    bool OnlyReference(const std::string& Name)
    {
        for (std::size_t I = 0; I < Nodes.Items.size(); ++I)
            if (Op(I) == "EX_LocalVariable" && Var(I) == Name)
            {
                const auto Parent = N(I)["parent"];
                if (Parent.Kind == J::K::Null)
                    return false;
                const auto P = std::size_t(Parent.U());
                const auto PK = Kids(P);
                if (Let(Op(P)) && !PK.empty() && PK[0] == I)
                    continue;
                if (!Call(Op(P)))
                    return false;
                std::pair<std::string, std::string> R;
                try
                {
                    R = Resolve(P, -1);
                }
                catch (const Unsupported&)
                {
                    return false;
                }
                const auto F = R.first.empty() ? J() : OwnerData.Sig.Get(R.first);
                std::vector<J> Listed;
                if (F.Kind != J::K::Null)
                    for (const auto& V : F[0].Items)
                        if (!V["ret"].B())
                            Listed.push_back(V);
                const auto A = Arguments(P);
                const auto AtI = std::find(A.begin(), A.end(), I);
                if (AtI == A.end())
                    return false;
                const auto X = std::size_t(AtI - A.begin());
                if (X >= Listed.size() || !Listed[X]["ref"].B())
                    return false;
            }
        return true;
    }
    virtual std::size_t Assignment(std::size_t I, std::size_t Position)
    {
        const auto K = Kids(I);
        const auto Target = K.at(0), Value = K.at(1);
        const auto TO = Op(Target), VO = Op(Value), Name = Var(Target);
        const auto OffsetValue = Offset(I);
        if (TO == "EX_StructMemberContext")
        {
            const auto Whole = Kids(Target).at(0);
            const auto HolderName = Var(Whole);
            if (Op(Whole) == "EX_LocalVariable" && Starts(HolderName, "K2Node_MakeStruct_"))
            {
                const auto* P = OwnerData.Prop(HolderName, Props);
                const auto Kind = P ? Str(Optional(*P, "struct")) : "";
                if (Kind.empty())
                    throw Unsupported("a Make Struct node of an unknown struct");
                const auto Member = N(Target)["operands"]["property"]["path"].Items.back().S(),
                           Cache = Key("make_struct", {HolderName});
                if (!Made.count(Cache))
                {
                    const auto M = G.Node("make_struct", J::Object({{"struct", OwnerData.Full(Kind)}, {"pure", true}}));
                    Made[Cache] = M;
                    TempPins[HolderName] = G.Pin(M, Leaf(Kind));
                }
                const auto Text = Print.Text(Value), MK = Key("member", {HolderName, Member});
                if (!Members.count(MK))
                {
                    Members[MK] = Text;
                    const auto M = Made.at(Cache);
                    User = G.Id(M);
                    Feed(Value, G.Pin(M, Member));
                }
                else if (Members.at(MK) != Text)
                    throw Unsupported("a Make Struct node whose members are written twice with other values");
                return 1;
            }
        }
        if (TO == "EX_LocalVariable" && Temp(Name))
        {
            if (Call(VO) || ((VO == "EX_Context" || VO == "EX_Context_FailSilent") && Call(Op(Kids(Value).at(1)))))
            {
                const auto CallI = Call(VO) ? Value : Kids(Value)[1];
                const std::int64_t On = Call(VO) ? -1 : std::int64_t(Kids(Value)[0]);
                if (Starts(Name, "K2Node_Switch"))
                {
                    const auto Taken = Switch(Position);
                    if (Taken)
                        return Taken;
                }
                if (Starts(Name, "K2Node_MathExpression_ReturnValue") && On < 0)
                {
                    const auto M = Math(CallI, Name);
                    if (M != SIZE_MAX)
                    {
                        TempPins[Name] = G.Pin(M, "ReturnValue");
                        return 1;
                    }
                }
                if (Later(CallI, On, Name))
                    return 1;
                const auto M = CallNode(CallI, On);
                Lazy.erase(Name);
                TempPins[Name] = G.Pin(M, "ReturnValue");
                Executes(M, OffsetValue, Position);
                return 1;
            }
            if (VO == "EX_ArrayConst" && Optional(N(Value)["operands"], "count").Kind == J::K::Number &&
                N(Value)["operands"]["count"].U() == 0 && Starts(Name, "CallFunc_"))
                return 1;
            if (VO == "EX_DynamicCast" || VO == "EX_MetaCast")
                return Cast(I, Position);
            if (VO == "EX_SwitchValue")
            {
                TempPins[Name] = Select(Value);
                return 1;
            }
            if (SwitchIndex.count(Name))
            {
                Alias[Name] = Value;
                return 1;
            }
            auto Taken = Loop(Position);
            if (!Taken)
                Taken = Increment(I, Position);
            if (Taken)
                return Taken;
            std::size_t Written = 0;
            for (const auto R : Roots)
                if (Let(Op(R)) && Var(Kids(R).at(0)) == Name)
                    ++Written;
            if (std::regex_match(Name, std::regex("Temp_[a-z]+_Variable(_[0-9]+)?")) &&
                Literal(Value).Kind != J::K::Null && Written == 1 && OnlyReference(Name))
            {
                Alias[Name] = Value;
                return 1;
            }
            if (!Starts(Name, "Temp_"))
                throw Unsupported("statement " + Print.Text(I).substr(0, 80));
        }
        if (TO == "EX_LocalOutVariable" || (TO == "EX_LocalVariable" && (Params[Name] & 0x100)))
        {
            if (G.Kind != "function")
                throw Unsupported("an output parameter written in the event graph");
            if (LastResult != std::int64_t(Position) - 1)
            {
                Result = G.Node("result");
                ExecAt[OffsetValue] = G.Pin(Result, "execute");
            }
            LastResult = std::int64_t(Position);
            Feed(Value, G.Pin(Result, Name));
            return 1;
        }
        if (TO == "EX_LocalVariable" && G.Held.count(Name))
        {
            const auto M = G.Node("assign");
            G.Link(Holder(Name), G.Pin(M, "Variable"));
            Feed(Value, G.Pin(M, "Value"));
            Executes(M, OffsetValue, Position);
            return 1;
        }
        if (TO == "EX_LocalVariable" || TO == "EX_InstanceVariable")
        {
            const auto M = G.Node("set", J::Object({{"var", Name}, {"local", TO == "EX_LocalVariable"}}));
            Feed(Value, G.Pin(M, Name));
            Executes(M, OffsetValue, Position);
            return 1;
        }
        if ((TO == "EX_Context" || TO == "EX_Context_FailSilent") && Op(Kids(Target).at(1)) == "EX_InstanceVariable")
        {
            const auto TK = Kids(Target);
            const auto Inner = TK[1];
            const auto Member = Var(Inner);
            const auto M =
                G.Node("set", J::Object({{"var", Member},
                                         {"local", false},
                                         {"cls", OwnerData.Full(Str(N(Inner)["operands"]["property"]["owner"]))}}));
            Feed(TK[0], G.Pin(M, "self"));
            Feed(Value, G.Pin(M, Member));
            Executes(M, OffsetValue, Position);
            return 1;
        }
        throw Unsupported("statement " + Print.Text(I).substr(0, 80));
    }
    virtual std::size_t Statement(std::size_t I, std::size_t Position, bool InitialPush)
    {
        Step();
        const auto O = Op(I);
        const auto K = Kids(I);
        const auto OffsetValue = Offset(I);
        if (Skip.count(OffsetValue))
        {
            if (Deferred.count(OffsetValue))
            {
                const auto D = Deferred.at(OffsetValue);
                Deferred.erase(OffsetValue);
                Feed(D.first, D.second);
            }
            return 1;
        }
        if (Leading && !InitialPush)
        {
            if (Default(I))
                return 1;
            Leading = false;
        }
        if (O == "EX_Jump" && G.Kind == "function" && KeepsReturn && Op(At.at(Address(I))) == "EX_Return" &&
            LastResult != std::int64_t(Position) - 1)
        {
            const auto M = G.Node("result");
            ExecAt[OffsetValue] = G.Pin(M, "execute");
            return 1;
        }
        if (Is(O, {"EX_Jump", "EX_Return", "EX_EndOfScript", "EX_PopExecutionFlow", "EX_Nothing"}))
            return 1;
        if (O == "EX_ComputedJump")
        {
            if (G.Kind != "event")
                throw Unsupported("a computed jump outside the event graph");
            return 1;
        }
        if (O == "EX_PushExecutionFlow")
        {
            if (InitialPush)
                return 1;
            const auto M = G.Node("sequence", J::Object({{"outputs", 2}}));
            ExecAt[OffsetValue] = G.Pin(M, "execute");
            After[G.Id(M)] = {{"then_0", Next(Position)}, {"then_1", Address(I)}};
            return 1;
        }
        if (O == "EX_JumpIfNot" || O == "EX_PopExecutionFlowIfNot")
        {
            const auto M = G.Node("branch");
            Feed(K.at(0), G.Pin(M, "Condition"));
            ExecAt[OffsetValue] = G.Pin(M, "execute");
            After[G.Id(M)] = {{"then", Next(Position)}};
            if (O == "EX_JumpIfNot")
                After[G.Id(M)].push_back({"else", Address(I)});
            return 1;
        }
        if (Call(O) || O == "EX_Context" || O == "EX_Context_FailSilent")
        {
            const auto CI = Call(O) ? I : K.at(1);
            const std::int64_t On = Call(O) ? -1 : std::int64_t(K.at(0));
            if (!Call(Op(CI)))
                throw Unsupported("statement " + Print.Text(I).substr(0, 80));
            if (Later(CI, On))
                return 1;
            const auto M = CallNode(CI, On);
            Executes(M, OffsetValue, Position);
            return 1;
        }
        if (Let(O))
            return Assignment(I, Position);
        if (O == "EX_SetArray")
        {
            const auto Name = Var(K.at(0));
            if (Starts(Name, "K2Node_MakeArray"))
            {
                const auto Cache = Key("array", {Print.Text(I)});
                if (!Made.count(Cache))
                {
                    const auto M = G.Node("make_array", J::Object({{"inputs", J(std::uint64_t(K.size() - 2))}}));
                    Made[Cache] = M;
                    const auto* P = OwnerData.Prop(Name, Props);
                    if (P && Optional(*P, "inner").Truth())
                        G.Nodes[M]["type"] = OwnerData.PinFull((*P)["inner"]);
                    for (std::size_t X = 1; X + 1 < K.size(); ++X)
                        Feed(K[X], G.Pin(M, "[" + std::to_string(X - 1) + "]"));
                }
                TempPins[Name] = G.Pin(Made.at(Cache), "Array");
                return 1;
            }
            if (Starts(Name, "CallFunc_") && K.size() == 2)
                return 1;
        }
        throw Unsupported("statement " + Print.Text(I).substr(0, 80));
    }

public:
    virtual ~Function() = default;
    Function(GraphOwner& O, const J& R, Graph& GraphValue, const std::map<std::string, Wire>& Entries = {})
        : OwnerData(O), G(GraphValue), Nodes(R["script"]["instructions"]), Print(R["script"]), EntryPins(Entries)
    {
        if (Nodes.Kind != J::K::Array || Nodes.Items.size() > 100000)
            throw Error("graph-instruction-budget");
        for (std::size_t I = 0; I < Nodes.Items.size(); ++I)
        {
            for (const auto& K : N(I)["children"].Items)
                if (K.U() >= Nodes.Items.size())
                    throw Error("graph-child-index");
            if (N(I)["parent"].Kind == J::K::Null)
            {
                Roots.push_back(I);
                Offsets.push_back(Offset(I));
                if (!At.emplace(Offset(I), I).second)
                    throw Error("graph-duplicate-offset");
                Positions[Offset(I)] = Roots.size() - 1;
            }
            if (Op(I) == "EX_SwitchValue")
                for (const auto C : Kids(I))
                    if (Op(C) == "EX_LocalVariable" && Starts(Var(C), "Temp_"))
                        SwitchIndex.insert(Var(C));
        }
        for (const auto& P : R["properties"].Items)
        {
            const auto Name = P["name"].S();
            Props[Name] = P;
            const auto FlagsValue = Bits(P["flags"]);
            if (FlagsValue & 0x80)
                Params[Name] = FlagsValue;
        }
    }
    const std::vector<std::int64_t>& Places() const
    {
        return Offsets;
    }
    void Run(const std::vector<std::pair<std::int64_t, Wire>>& Entries, bool OnlyReached = false)
    {
        if (OnlyReached)
        {
            std::set<std::int64_t> Seen;
            std::vector<std::int64_t> Queue;
            for (const auto& E : Entries)
                Queue.push_back(E.first);
            while (!Queue.empty())
            {
                const auto V = Queue.back();
                Queue.pop_back();
                if (Seen.count(V) || !At.count(V))
                    continue;
                Seen.insert(V);
                const auto I = At.at(V);
                const auto O = Op(I);
                if (Is(O, {"EX_Jump", "EX_JumpIfNot", "EX_PushExecutionFlow"}))
                    Queue.push_back(Address(I));
                if (!Is(O, {"EX_Jump", "EX_Return", "EX_EndOfScript", "EX_PopExecutionFlow"}))
                    Queue.push_back(Next(Positions.at(V)));
            }
            for (const auto V : Offsets)
                if (!Seen.count(V))
                    Skip.insert(V);
        }
        bool Initial = false;
        if (!Roots.empty() && Op(Roots[0]) == "EX_PushExecutionFlow")
            Initial = Op(At.at(Address(Roots[0]))) == "EX_Return";
        KeepsReturn = Initial;
        for (std::size_t P = 0; P < Roots.size();)
            P += Statement(Roots[P], P, Initial && P == 0);
        for (const auto& E : Entries)
            G.Link(E.second, Flow(E.first));
        std::vector<std::string> AfterKeys;
        for (const auto& Item : After)
            AfterKeys.push_back(Item.first);
        std::sort(AfterKeys.begin(), AfterKeys.end(), [](const std::string& A, const std::string& B)
                  { return std::stoull(A.substr(1)) < std::stoull(B.substr(1)); });
        for (const auto& Id : AfterKeys)
            for (const auto& W : After.at(Id))
            {
                const auto To = Flow(W.second);
                G.Link({Id, W.first}, To);
                if (W.first == "default" && !To.first.empty() && G.ById(Id)["t"].S() != "switch_int")
                    G.ById(Id)["has_default"] = true;
            }
        for (std::size_t X = 0; X < G.Links.Items.size(); ++X)
        {
            auto& L = G.Links.Items[X];
            const auto Id = L.Items[0].S();
            if (Starts(Id, "?"))
            {
                const auto Name = Id.substr(1);
                if (Lazy.count(Name) && !TempPins.count(Name))
                {
                    const auto V = Lazy.at(Name);
                    const auto M = CallNode(V.Index, V.Target, V.Slot);
                    TempPins.emplace(Name, G.Pin(M, "ReturnValue"));
                }
                if (!TempPins.count(Name))
                    throw Unsupported("temporary " + Name + " is read and no node writes it");
                const auto PinValue = TempPins.at(Name);
                G.Links.Items[X].Items[0] = PinValue.first;
                G.Links.Items[X].Items[1] = PinValue.second;
            }
        }
        std::vector<J> Ordered, Plain;
        for (const auto& V : G.Locals.Items)
            if (!Absorbed.count(V["name"].S()) && !Alias.count(V["name"].S()))
                (V.Has("order") ? Ordered : Plain).push_back(V);
        std::stable_sort(Ordered.begin(), Ordered.end(),
                         [](const J& A, const J& B) { return A["order"].U() < B["order"].U(); });
        G.Locals.Items = Ordered;
        G.Locals.Items.insert(G.Locals.Items.end(), Plain.begin(), Plain.end());
    }
};
} // namespace GraphDetail
}
} // namespace P4MapKit::Blueprint

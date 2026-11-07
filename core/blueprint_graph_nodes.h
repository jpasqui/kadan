// Portable recovery of the authored K2 graph boundary: shared helpers, the graph being built, the bytecode
// printer and the owner a graph is recovered for.
#pragma once
#include "blueprint_graph_signatures.h"
#include "blueprint_plan.h"
#include <deque>
#include <regex>
#include <iomanip>
#include <cstring>

namespace P4MapKit
{
namespace Blueprint
{
namespace GraphDetail
{
inline bool Starts(const std::string& A, const std::string& B)
{
    return A.compare(0, B.size(), B) == 0;
}
inline std::string Leaf(const std::string& A)
{
    const auto P = A.find_last_of(".:");
    return P == std::string::npos ? A : A.substr(P + 1);
}
inline std::string Owner(const std::string& A)
{
    const auto P = A.rfind('.');
    return P == std::string::npos ? "" : A.substr(0, P);
}
inline std::string Str(const J& A)
{
    return A.Kind == J::K::Null ? "" : A.S();
}
inline const J& Optional(const J& A, const std::string& K)
{
    static const J Empty;
    return A.Has(K) ? A[K] : Empty;
}
inline bool Is(const std::string& A, std::initializer_list<const char*> B)
{
    for (const auto* V : B)
        if (A == V)
            return true;
    return false;
}
inline bool Temp(const std::string& A)
{
    return Starts(A, "CallFunc_") || Starts(A, "K2Node_") || Starts(A, "Temp_");
}
inline bool Call(const std::string& A)
{
    return Is(A, {"EX_FinalFunction", "EX_LocalFinalFunction", "EX_CallMath", "EX_VirtualFunction",
                  "EX_LocalVirtualFunction"});
}
inline bool Let(const std::string& A)
{
    return Is(A,
              {"EX_Let", "EX_LetBool", "EX_LetObj", "EX_LetWeakObjPtr", "EX_LetDelegate", "EX_LetMulticastDelegate"});
}
inline std::string Join(const std::vector<std::string>& V, const std::string& Sep)
{
    std::string S;
    for (const auto& X : V)
    {
        if (!S.empty())
            S += Sep;
        S += X;
    }
    return S;
}
inline std::string Float(double V, int Digits, bool Fixed = false)
{
    std::ostringstream S;
    S.imbue(std::locale::classic());
    S << std::setprecision(Digits);
    if (Fixed)
        S << std::fixed;
    S << V;
    return S.str();
}
inline std::string Repr(double V)
{
    std::string S;
    for (int P = 1; P <= 17; ++P)
    {
        S = Float(V, P);
        std::istringstream In(S);
        In.imbue(std::locale::classic());
        double W = 0;
        In >> W;
        if (W == V)
            break;
    }
    const auto E = S.find('e');
    if (E != std::string::npos)
    {
        const int N = std::stoi(S.substr(E + 1));
        if (N >= -4 && N < 16)
        {
            S = Float(V, std::max(0, 16 - N), true);
            while (S.size() > 1 && S.back() == '0')
                S.pop_back();
            if (S.back() == '.')
                S += '0';
        }
    }
    if (S.find_first_of(".e") == std::string::npos)
        S += ".0";
    return S;
}
inline std::string QuoteElement(const std::string& V)
{
    std::string S = "\"";
    for (char C : V)
    {
        if (C == '\\' || C == '\"')
            S += '\\';
        S += C;
    }
    return S + '\"';
}
inline std::uint64_t Bits(const J& V)
{
    if (V.Kind == J::K::Number)
        return V.U();
    const auto T = V.S();
    std::size_t N = 0;
    const auto B = std::stoull(T, &N, 16);
    if (N != T.size())
        throw Error("invalid-property-flags");
    return B;
}
inline std::string Flags(std::uint64_t V)
{
    std::ostringstream S;
    S << "0x" << std::hex << V;
    return S.str();
}
inline J Pin(const J& P)
{
    return PinType(P);
}
struct Unsupported : Error
{
    explicit Unsupported(const std::string& Why) : Error(Why)
    {
    }
};
using Wire = std::pair<std::string, std::string>;
inline Wire EmptyWire()
{
    return {};
}
struct Graph
{
    std::string Name, Kind;
    std::deque<J> Nodes;
    J Links = J::Array(), Locals = J::Array();
    std::map<std::string, J> Held;
    std::size_t TextBytes = 0;
    Graph(const std::string& N, const std::string& K) : Name(N), Kind(K)
    {
    }
    void Charge(std::size_t Bytes)
    {
        if (Bytes > 64u * 1024u * 1024u - TextBytes)
            throw Error("graph-output-budget");
        TextBytes += Bytes;
    }
    std::size_t Node(const std::string& T, J V = J::Object())
    {
        const auto I = Nodes.size();
        if (I >= 100000)
            throw Error("graph-node-budget");
        Charge(T.size() + V.Dump().size());
        J N = J::Object({{"id", "n" + std::to_string(I)}, {"t", T}});
        for (const auto& K : V.Keys())
            N[K] = V[K];
        Nodes.push_back(std::move(N));
        return I;
    }
    std::string Id(std::size_t I) const
    {
        return Nodes.at(I)["id"].S();
    }
    Wire Pin(std::size_t I, const std::string& P) const
    {
        return {Id(I), P};
    }
    J& ById(const std::string& S)
    {
        if (S.size() < 2 || S[0] != 'n')
            throw Error("graph-node-id");
        const auto I = std::stoull(S.substr(1));
        if ("n" + std::to_string(I) != S)
            throw Error("graph-node-id");
        return Nodes.at(std::size_t(I));
    }
    void Link(const Wire& A, const Wire& B)
    {
        if (!A.first.empty() && !B.first.empty())
        {
            if (Links.Items.size() >= 500000)
                throw Error("graph-link-budget");
            Charge(A.first.size() + A.second.size() + B.first.size() + B.second.size());
            Links.Items.push_back(J::Array({A.first, A.second, B.first, B.second}));
        }
    }
    J Data() const
    {
        J N = J::Array();
        for (const auto& V : Nodes)
            N.Items.push_back(V);
        return J::Object({{"name", Name}, {"kind", Kind}, {"locals", Locals}, {"nodes", N}, {"links", Links}});
    }
};
class Printer
{
    const J& Nodes;
    mutable std::size_t Steps = 0;
    std::string TextAt(std::size_t I, std::size_t Depth) const
    {
        if (Depth > 128 || ++Steps > 10000000)
            throw Error("graph-expression-budget");
        const auto& N = Nodes[I];
        const auto O = N["opcode"].S();
        const auto& P = N["operands"];
        const auto& K = N["children"].Items;
        auto T = [&](std::size_t C)
        {
            return TextAt(C, Depth + 1);
        };
        auto Kid = [&](std::size_t C)
        {
            return T(std::size_t(K.at(C).U()));
        };
        auto Kids = [&](std::size_t A, std::size_t B)
        {
            std::vector<std::string> S;
            for (std::size_t X = A; X < B; ++X)
                S.push_back(Kid(X));
            return Join(S, ", ");
        };
        static const std::map<std::string, std::string> Constants = {
            {"EX_IntZero", "0"},     {"EX_IntOne", "1"},         {"EX_True", "true"}, {"EX_False", "false"},
            {"EX_NoObject", "None"}, {"EX_NoInterface", "None"}, {"EX_Self", "self"}, {"EX_Nothing", "nothing"}};
        const auto C = Constants.find(O);
        if (C != Constants.end())
            return C->second;
        if (Is(O, {"EX_LocalVariable", "EX_InstanceVariable", "EX_LocalOutVariable", "EX_DefaultVariable",
                   "EX_ClassSparseDataVariable"}))
        {
            std::vector<std::string> V;
            for (const auto& X : P["property"]["path"].Items)
                V.push_back(X.S());
            const auto S = Join(V, ".");
            return (O == "EX_InstanceVariable"          ? "self."
                    : O == "EX_LocalOutVariable"        ? "out "
                    : O == "EX_DefaultVariable"         ? "default "
                    : O == "EX_ClassSparseDataVariable" ? "sparse "
                                                        : "") +
                   (S.empty() ? "<no property>" : S);
        }
        if (Let(O))
            return Kid(0) + " = " + Kid(1);
        if (O == "EX_LetValueOnPersistentFrame")
        {
            std::vector<std::string> V;
            for (const auto& X : P["property"]["path"].Items)
                V.push_back(X.S());
            return "frame." + Join(V, ".") + " = " + Kid(0);
        }
        if (Is(O, {"EX_Context", "EX_Context_FailSilent", "EX_ClassContext"}))
            return Kid(0) + "->" + Kid(1);
        if (O == "EX_StructMemberContext")
        {
            std::vector<std::string> V;
            for (const auto& X : P["property"]["path"].Items)
                V.push_back(X.S());
            return Kid(0) + "." + Join(V, ".");
        }
        if (Call(O))
        {
            std::vector<std::string> A;
            for (const auto& X : K)
                if (Nodes[std::size_t(X.U())]["opcode"].S() != "EX_EndFunctionParms")
                    A.push_back(T(std::size_t(X.U())));
            if (P.Has("function_name"))
                return P["function_name"].S() + "(" + Join(A, ", ") + ")";
            auto Path = Str(P["function"]["path"]);
            if (Path.empty())
                Path = "None";
            return (O == "EX_CallMath" ? Leaf(Owner(Path)) + "::" : "") + Leaf(Path) + "(" + Join(A, ", ") + ")";
        }
        if (O == "EX_CallMulticastDelegate")
        {
            std::vector<std::string> A;
            for (const auto& X : K)
                if (Nodes[std::size_t(X.U())]["opcode"].S() != "EX_EndFunctionParms")
                    A.push_back(T(std::size_t(X.U())));
            if (A.empty())
                throw Error("delegate-arguments");
            auto First = A.front();
            A.erase(A.begin());
            return First + ".Broadcast(" + Join(A, ", ") + ")";
        }
        if (Is(O, {"EX_IntConst", "EX_FloatConst", "EX_ByteConst", "EX_IntConstByte", "EX_Int64Const", "EX_UInt64Const",
                   "EX_SkipOffsetConst"}))
            return O == "EX_SkipOffsetConst" ? "@" + P["value"].Text
                   : O == "EX_FloatConst"    ? Repr(P["value"].D())
                                             : P["value"].Text;
        if (O == "EX_StringConst" || O == "EX_UnicodeStringConst")
        {
            std::string S = "\"";
            for (char X : P["value"].S())
            {
                if (X == '\"')
                    S += '\\';
                S += X;
            }
            return S + '\"';
        }
        if (O == "EX_NameConst")
            return "'" + P["name"].S() + "'";
        if (O == "EX_ObjectConst")
            return "<" + (P["object"]["path"].Kind == J::K::Null ? "None" : P["object"]["path"].S()) + ">";
        if (Is(O, {"EX_VectorConst", "EX_RotationConst", "EX_TransformConst"}))
        {
            std::vector<std::string> A;
            for (const auto& X : P["value"].Items)
                A.push_back(Float(X.D(), 6));
            return O.substr(3, O.size() - 8) + "(" + Join(A, ", ") + ")";
        }
        if (O == "EX_TextConst")
            return "Text(" + Kids(0, K.size()) + ")";
        if (O == "EX_StructConst")
            return Leaf(Str(P["struct"]["path"])) + "{" + Kids(0, K.size() - 1) + "}";
        if (Is(O, {"EX_SetArray", "EX_SetSet", "EX_SetMap"}))
            return Kid(0) + " = [" + Kids(1, K.size() - 1) + "]";
        if (Is(O, {"EX_ArrayConst", "EX_SetConst", "EX_MapConst"}))
            return "[" + Kids(0, K.size() - 1) + "]";
        if (Is(O, {"EX_DynamicCast", "EX_MetaCast", "EX_ObjToInterfaceCast", "EX_CrossInterfaceCast",
                   "EX_InterfaceToObjCast"}))
            return O.substr(3) + "<" + Leaf(Str(P["class"]["path"])) + ">(" + Kid(0) + ")";
        if (O == "EX_PrimitiveCast")
            return "cast" + Flags(P["cast"].U()) + "(" + Kid(0) + ")";
        if (O == "EX_Jump")
            return "goto " + P["address"]["value"].Text;
        if (O == "EX_JumpIfNot")
            return "if not (" + Kid(0) + ") goto " + P["address"]["value"].Text;
        if (O == "EX_PushExecutionFlow")
            return "push " + P["address"]["value"].Text;
        if (O == "EX_PopExecutionFlow")
            return "pop";
        if (O == "EX_PopExecutionFlowIfNot")
            return "if not (" + Kid(0) + ") pop";
        if (O == "EX_ComputedJump")
            return "goto computed(" + Kid(0) + ")";
        if (O == "EX_Return")
            return "return " + Kid(0);
        if (O == "EX_EndOfScript")
            return "end";
        if (O == "EX_SwitchValue")
        {
            std::vector<std::string> A;
            for (std::size_t X = 0; X < P["cases"].U(); ++X)
                A.push_back(Kid(1 + 2 * X) + ": " + Kid(2 + 2 * X));
            return "switch(" + Kid(0) + ") {" + Join(A, ", ") + ", default: " + Kid(K.size() - 1) + "}";
        }
        if (O == "EX_ArrayGetByRef")
            return Kid(0) + "[" + Kid(1) + "]";
        if (O == "EX_BindDelegate")
            return Kid(0) + ".Bind(" + Kid(1) + ", '" + P["name"].S() + "')";
        if (O == "EX_AddMulticastDelegate" || O == "EX_RemoveMulticastDelegate")
            return Kid(0) + (O == "EX_AddMulticastDelegate" ? ".Add(" : ".Remove(") + Kid(1) + ")";
        if (O == "EX_ClearMulticastDelegate")
            return Kid(0) + ".Clear()";
        if (O == "EX_InstanceDelegate")
            return "delegate '" + P["name"].S() + "'";
        if (Is(O, {"EX_Skip", "EX_InterfaceContext", "EX_SoftObjectConst", "EX_FieldPathConst"}))
            return O.substr(3) + "(" + Kids(0, K.size()) + ")";
        if (Is(O, {"EX_Tracepoint", "EX_WireTracepoint", "EX_Breakpoint"}))
        {
            auto S = O.substr(3);
            for (char& X : S)
                X = char(std::tolower(static_cast<unsigned char>(X)));
            return S;
        }
        return O + "(" + Kids(0, K.size()) + ")";
    }

public:
    explicit Printer(const J& Script) : Nodes(Script["instructions"])
    {
    }
    std::string Text(std::size_t I) const
    {
        return TextAt(I, 0);
    }
};
struct GraphOwner
{
    const J& Doc;
    GraphSignatures& Sig;
    std::string Package, ClassPath, Uber;
    std::map<std::string, J> Variables, Frame;
    std::set<std::string> MathNodes;
    GraphOwner(const J& D, GraphSignatures& S)
        : Doc(D), Sig(S), Package(D["package"].S()), ClassPath(Package + "." + D["class"]["name"].S())
    {
        for (const auto& P : D["class"]["properties"].Items)
            Variables[P["name"].S()] = P;
        for (const auto& N : D["functions"].Keys())
            if (Starts(N, "ExecuteUbergraph_"))
            {
                Uber = N;
                break;
            }
        if (!Uber.empty())
            for (const auto& P : D["functions"][Uber]["properties"].Items)
                Frame[P["name"].S()] = P;
    }
    std::string Full(const std::string& R) const
    {
        return R.empty() || R[0] == '/' ? R : Package + "." + R;
    }
    J FullJ(const J& R) const
    {
        return R.Truth() ? J(Full(R.S())) : J();
    }
    J PinFull(const J& P) const
    {
        J V = Pin(P);
        for (const auto* K : {"o", "f"})
            if (Optional(V, K).Truth())
                V[K] = FullJ(V[K]);
        return V;
    }
    const J* Prop(const std::string& Name, const std::map<std::string, J>& P, bool Vars = false) const
    {
        auto I = P.find(Name);
        if (I != P.end())
            return &I->second;
        I = Frame.find(Name);
        if (I != Frame.end())
            return &I->second;
        if (Vars)
        {
            I = Variables.find(Name);
            if (I != Variables.end())
                return &I->second;
        }
        return nullptr;
    }
};
} // namespace GraphDetail
}
} // namespace P4MapKit::Blueprint

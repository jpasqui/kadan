// Structural material boundaries from compiled NV assembly. These recognize
// compiler interfaces; an anchor is not proof of an original artist graph.
#pragma once
#include "material_numeric.h"
#include <cstdlib>
#include <set>

namespace P4MapKit
{
namespace Material
{
struct AnchorOperand
{
    std::string base, swizzle;
    bool negative = false, absolute = false, literal = false;
    std::array<double, 4> values{{}};
    explicit AnchorOperand(std::string Text = "")
    {
        Text = trim(Text);
        negative = starts(Text, "-");
        if (negative)
            Text = trim(Text.substr(1));
        absolute = Text.size() >= 2 && Text.front() == '|' && Text.back() == '|';
        if (absolute)
            Text = trim(Text.substr(1, Text.size() - 2));
        static const std::regex Pattern("^(.*?)\\.([xyzw]+)$");
        std::smatch M;
        if (std::regex_match(Text, M, Pattern) &&
            !(M[1].str().size() >= 8 && M[1].str().substr(M[1].str().size() - 8) == "fragment"))
        {
            base = M[1];
            swizzle = M[2];
        }
        else
            base = Text;
        literal = starts(base, "{");
        if (literal)
        {
            auto Raw = base;
            while (!Raw.empty() && (Raw.front() == '{' || Raw.front() == '}'))
                Raw.erase(Raw.begin());
            while (!Raw.empty() && (Raw.back() == '{' || Raw.back() == '}'))
                Raw.pop_back();
            auto Components = split_operands(Raw);
            if (Components.empty() || Components.size() > 4)
                throw EvalError("anchor-literal-size");
            for (std::size_t I = 0; I < Components.size(); ++I)
            {
                char* End = nullptr;
                values[I] = std::strtod(Components[I].c_str(), &End);
                if (End == Components[I].c_str() || *End)
                    throw EvalError("anchor-literal-number");
            }
        }
        if (swizzle.size() > 4)
            throw EvalError("anchor-swizzle-size");
    }
    char component(std::size_t K) const
    {
        if (K >= 4)
            throw EvalError("anchor-component");
        if (swizzle.empty())
            return "xyzw"[K];
        return swizzle[swizzle.size() == 1 ? 0 : K < swizzle.size() ? K : throw EvalError("anchor-swizzle-component")];
    }
    double scalar() const
    {
        return values[std::string("xyzw").find(component(0))];
    }
    std::array<double, 4> vector() const
    {
        std::array<double, 4> V;
        for (std::size_t I = 0; I < 4; ++I)
            V[I] = values[std::string("xyzw").find(component(I))];
        return V;
    }
};
struct AnchorInstruction
{
    std::string text, op, dst, mask;
    std::vector<std::string> mods;
    std::vector<AnchorOperand> src;
    explicit AnchorInstruction(const std::string& Text) : text(Text)
    {
        const auto Space = Text.find(' ');
        const auto Head = Text.substr(0, Space);
        auto Dot = Head.find('.');
        op = Head.substr(0, Dot);
        while (Dot != std::string::npos)
        {
            auto Next = Head.find('.', Dot + 1);
            mods.push_back(Head.substr(Dot + 1, Next == std::string::npos ? Next : Next - Dot - 1));
            Dot = Next;
        }
        auto Operands = split_operands(Space == std::string::npos ? "" : Text.substr(Space + 1));
        const std::set<std::string> Control = {"IF", "ELSE", "ENDIF", "KIL", "REP", "ENDREP", "BRK", "RET"};
        if (Control.count(op) || Operands.empty())
            return;
        static const std::regex Destination("^(\\w+(?:\\[\\d+\\])?)(?:\\.([xyzw]+))?");
        std::smatch M;
        if (std::regex_search(Operands[0], M, Destination))
        {
            dst = M[1];
            mask = M[2].matched ? M[2].str() : "xyzw";
        }
        for (std::size_t I = 1; I < Operands.size(); ++I)
            src.emplace_back(Operands[I]);
    }
    std::pair<std::string, std::string> reads(std::size_t I, const std::string& Comps) const
    {
        const auto& O = src.at(I);
        std::string C;
        for (auto K : Comps)
            C.push_back(O.component(std::string("xyzw").find(K)));
        return {O.base, C};
    }
    bool has(const std::string& Mod) const
    {
        return std::find(mods.begin(), mods.end(), Mod) != mods.end();
    }
};
struct MaterialAnchor
{
    std::size_t line = 0;
    std::string register_name, comps;
    bool has_constant = false, saturate = false;
    double constant = 0;
    static MaterialAnchor Register(std::size_t Line, const std::string& Register, const std::string& Comps)
    {
        MaterialAnchor V;
        V.line = Line;
        V.register_name = Register;
        V.comps = Comps;
        return V;
    }
    static MaterialAnchor Constant(std::size_t Line, double Number)
    {
        MaterialAnchor V;
        V.line = Line;
        V.has_constant = true;
        V.constant = Number;
        return V;
    }
};
using MaterialAnchors = std::map<std::string, MaterialAnchor>;
inline std::vector<AnchorInstruction> ParseMaterialAnchorInstructions(const std::vector<std::string>& Lines)
{
    if (Lines.size() > 100000)
        throw EvalError("anchor-instruction-budget");
    std::vector<AnchorInstruction> Code;
    Code.reserve(Lines.size());
    for (const auto& L : Lines)
    {
        if (L.size() > 65536)
            throw EvalError("anchor-line-budget");
        Code.emplace_back(L);
    }
    return Code;
}
inline bool AnchorNear(double A, double B, double Tolerance = 1e-4)
{
    return std::fabs(A - B) <= Tolerance * std::max(1.0, std::fabs(B));
}
inline bool AnchorIs(const AnchorInstruction& L, const std::string& Op, const std::string& Mask = "")
{
    return L.op == Op && (Mask.empty() || L.mask == Mask);
}
inline bool AnchorWritten(const std::vector<AnchorInstruction>& Code, const std::string& Register,
                          const std::string& Comps, std::size_t Start, std::size_t End)
{
    if (Start > End || End > Code.size())
        throw EvalError("anchor-write-range");
    for (std::size_t I = Start; I < End; ++I)
        if (Code[I].dst == Register)
            for (auto C : Comps)
                if (Code[I].mask.find(C) != std::string::npos)
                    return true;
    return false;
}
inline bool AnchorNormalizedVector(const std::vector<AnchorInstruction>& Code,
                                   const std::pair<std::string, std::string>& Vector, std::size_t Start,
                                   MaterialAnchor& Out)
{
    for (std::size_t J = Start; J < std::min(Start + 15, Code.size()); ++J)
    {
        const auto& D = Code[J];
        if (D.op != "DP3" || D.src.size() != 2 || D.mask.size() != 1 || D.reads(0, "xyz") != Vector ||
            D.reads(1, "xyz") != Vector || D.src[0].negative || D.src[1].negative || D.src[0].absolute ||
            D.src[1].absolute || AnchorWritten(Code, Vector.first, Vector.second, Start, J))
            continue;
        const auto Length = std::make_pair(D.dst, D.mask);
        for (std::size_t R = J + 1; R < std::min(J + 14, Code.size()); ++R)
        {
            const auto& S = Code[R];
            if (S.op != "RSQ" || S.src.size() != 1 || S.mask.size() != 1 || S.reads(0, S.mask) != Length ||
                S.src[0].negative || S.src[0].absolute || AnchorWritten(Code, Length.first, Length.second, J + 1, R))
                continue;
            const auto Inverse = std::make_pair(S.dst, S.mask + S.mask + S.mask);
            for (std::size_t K = R + 1; K < std::min(R + 14, Code.size()); ++K)
            {
                const auto& M = Code[K];
                if (!AnchorIs(M, "MUL", "xyz") || M.src.size() != 2)
                    continue;
                auto A = M.reads(0, "xyz"), B = M.reads(1, "xyz");
                if ((A != Vector && B != Vector) || (A != Inverse && B != Inverse) || M.src[0].negative ||
                    M.src[1].negative || M.src[0].absolute || M.src[1].absolute ||
                    AnchorWritten(Code, Vector.first, Vector.second, Start, K) ||
                    AnchorWritten(Code, S.dst, S.mask, R + 1, K))
                    continue;
                Out = MaterialAnchor::Register(K, M.dst, "xyz");
                return true;
            }
            break;
        }
        break;
    }
    return false;
}
inline bool AnchorGameOcclusion(const std::vector<AnchorInstruction>& Code, std::size_t Stop, MaterialAnchor& Out)
{
    for (std::size_t I = Stop; I + 2 < Code.size(); ++I)
    {
        const auto& L = Code[I];
        const auto& Clamp = Code[I + 1];
        const auto& Floor = Code[I + 2];
        if (!AnchorIs(L, "MIN", "w") || L.src.size() != 2 || !AnchorIs(Clamp, "MOV") || !Clamp.has("SAT") ||
            Clamp.src.empty() || Clamp.src[0].base != L.dst || !AnchorIs(Floor, "MAX") || Floor.src.size() != 2 ||
            !Floor.src[1].literal || !AnchorNear(Floor.src[1].scalar(), 0.01))
            continue;
        std::vector<std::size_t> Held, Fetched;
        for (std::size_t K = 0; K < 2; ++K)
            if (!L.src[K].literal)
            {
                auto V = L.reads(K, "w");
                if (!AnchorWritten(Code, V.first, V.second, Stop, I))
                    Held.push_back(K);
                else
                {
                    for (std::size_t J = Stop; J < I; ++J)
                        if (Code[J].op == "TXF" && Code[J].dst == V.first)
                        {
                            Fetched.push_back(K);
                            break;
                        }
                }
            }
        if (Held.size() == 1 && Fetched.size() == 1)
        {
            auto V = L.reads(Held[0], "w");
            Out = MaterialAnchor::Register(I, V.first, V.second);
            return true;
        }
        return false;
    }
    return false;
}
// Instructions are declaration-free, semicolon-free NV assembly lines (the VM
// instructions() boundary). Registers at anchor.line are read BEFORE that line;
// a "stop" anchor evaluates the material prefix [0, stop.line).
inline MaterialAnchors FindMaterialAnchors(const std::vector<AnchorInstruction>& Code, const std::string& TangentZ = "")
{
    MaterialAnchors Found;
    const auto Put = [&](const std::string& Name, std::size_t Line, const std::pair<std::string, std::string>& Value)
    {
        Found.emplace(Name, MaterialAnchor::Register(Line, Value.first, Value.second));
    };
    for (std::size_t I = 0; I < Code.size(); ++I)
    {
        const auto& L = Code[I];
        const auto& S = L.src;
        if (AnchorIs(L, "MAD", "xyz") && S.size() == 3 && S[0].negative && S[1].literal &&
            AnchorNear(S[1].scalar(), 0.08) && !S[2].literal && !S[2].negative)
        {
            for (std::size_t J = I + 1; J < std::min(I + 8, Code.size()); ++J)
            {
                const auto& N = Code[J];
                if (AnchorIs(N, "MAD", "xyz") && N.src.size() == 3 && N.src[1].base == L.dst && !N.src[0].literal)
                {
                    Put("Specular", I, L.reads(0, "x"));
                    Put("BaseColor", I, L.reads(2, "xyz"));
                    Put("Metallic", J, N.reads(0, "x"));
                    break;
                }
            }
        }
        if (AnchorIs(L, "ADD", "xyz") && S.size() == 2 && S[1].literal && S[1].scalar() < 0 &&
            S[1].scalar() >= -0.0801 && !S[0].literal)
        {
            auto Constant = -S[1].scalar();
            for (std::size_t J = I + 1; J < std::min(I + 8, Code.size()); ++J)
            {
                const auto& N = Code[J];
                if (AnchorIs(N, "MAD", "xyz") && N.src.size() == 3 && N.src[1].base == L.dst && N.src[2].literal &&
                    AnchorNear(N.src[2].scalar(), Constant))
                {
                    Put("BaseColor", I, L.reads(0, "xyz"));
                    Put("Metallic", J, N.reads(0, "x"));
                    Found.emplace("Specular", MaterialAnchor::Constant(I, Constant / 0.08));
                    break;
                }
            }
        }
        if (AnchorIs(L, "MAD", "xyz") && S.size() == 3 && S[1].negative && S[1].base == S[2].base &&
            S[1].swizzle == S[2].swizzle && !S[2].literal && !S[2].negative && !S[0].literal)
        {
            Put("DiffuseBase", I, L.reads(2, "xyz"));
            Put("DiffuseMetallic", I, L.reads(0, "x"));
        }
        if (AnchorIs(L, "MAD") && S.size() == 3 && S[1].literal && !S[0].literal)
        {
            const auto V = S[1].vector();
            const double B[] = {-1, -0.0275, -0.572, 0.022};
            bool Near = true;
            for (int K = 0; K < 4; ++K)
                Near = Near && AnchorNear(V[K], B[K]);
            if (Near)
                Put("Roughness", I, L.reads(0, "x"));
        }
        if (AnchorIs(L, "MAD", "xyz") && S.size() == 3 && S[1].literal && AnchorNear(S[1].scalar(), 2.0) &&
            S[2].negative && I >= 2 && AnchorIs(Code[I - 1], "MUL", "xyz") && Code[I - 1].dst == S[0].base &&
            Code[I - 2].op == "DP3")
        {
            const auto& Dot = Code[I - 2];
            const auto& Mul = Code[I - 1];
            const auto View = S[2].base;
            if (Dot.src.size() >= 2)
            {
                const std::set<std::string> Pair = {Dot.src[0].base, Dot.src[1].base};
                for (const auto& N : Mul.src)
                    if (Pair.count(N.base) && N.base != View && !N.literal && Pair.count(View))
                    {
                        std::string Comps;
                        for (int K = 0; K < 3; ++K)
                            Comps.push_back(N.component(K));
                        Put("Normal", I - 2, {N.base, Comps});
                        Comps.clear();
                        for (int K = 0; K < 3; ++K)
                            Comps.push_back(S[2].component(K));
                        Put("CameraVector", I - 2, {View, Comps});
                        break;
                    }
            }
        }
        if (AnchorIs(L, "ADD", "xyz") && S.size() == 2 && S[1].literal &&
            AnchorNear(S[1].scalar(), -0.003921628, 1e-5) && !S[0].literal)
            Put("NormalDecal", I, {L.dst, "xyz"});
        if (AnchorIs(L, "MOV", "xyz") && L.has("SAT") && !S.empty() && !S[0].literal)
        {
            bool Root = false, Reciprocal = false;
            for (std::size_t J = I + 1; J < std::min(I + 10, Code.size()); ++J)
            {
                auto& N = Code[J];
                if (N.op == "RSQ" && !N.src.empty() && N.src[0].base == L.dst)
                    Root = true;
                if (N.op == "RCP")
                    Reciprocal = true;
            }
            if (Root && Reciprocal)
                Put("SubsurfaceColor", I, L.reads(0, "xyz"));
        }
        if (AnchorIs(L, "MAD") && S.size() == 3 && S[1].literal && AnchorNear(S[1].scalar(), 2.0404) &&
            I + 1 < Code.size() && AnchorIs(Code[I + 1], "MUL") && Code[I + 1].src.size() == 2)
        {
            auto& N = Code[I + 1];
            for (std::size_t K = 0; K < 2; ++K)
                if (N.src[K].base != L.dst)
                {
                    if (!N.src[K].literal)
                    {
                        Put("AmbientOcclusion", I, N.reads(K, "x"));
                        Put("AOBase", I, {S[0].base, "xyz"});
                    }
                    break;
                }
        }
        if (!TangentZ.empty() && AnchorIs(L, "MAD", "xyz") && S.size() == 3 &&
            (TangentZ == S[0].base || TangentZ == S[1].base))
        {
            MaterialAnchor V;
            if (AnchorNormalizedVector(Code, {L.dst, "xyz"}, I + 1, V))
                Found["NormalWorld"] = V;
        }
        else if (!TangentZ.empty() && L.op == "DP3" && S.size() == 2 &&
                 L.reads(0, "xyz") == std::make_pair(TangentZ, std::string("xyz")) &&
                 L.reads(1, "xyz") == std::make_pair(TangentZ, std::string("xyz")))
        {
            bool Transform = false;
            for (const auto& C : Code)
                if (AnchorIs(C, "MAD", "xyz") && C.src.size() == 3 &&
                    (TangentZ == C.src[0].base || TangentZ == C.src[1].base))
                {
                    Transform = true;
                    break;
                }
            MaterialAnchor V;
            if (!Transform && AnchorNormalizedVector(Code, {TangentZ, "xyz"}, I, V))
                Found["NormalWorld"] = V;
        }
    }
    if (!Found.count("BaseColor") && Found.count("DiffuseBase"))
    {
        Found["BaseColor"] = Found.at("DiffuseBase");
        Found["Metallic"] = Found.at("DiffuseMetallic");
    }
    if (!Found.count("BaseColor"))
    {
        const auto Base = Found.count("AOBase") ? Found.at("AOBase").register_name : "";
        for (std::size_t I = 0; I < Code.size(); ++I)
        {
            const auto& L = Code[I];
            const auto& S = L.src;
            if (AnchorIs(L, "ADD", "xyz") && S.size() == 2 && S[1].literal && !S[0].literal &&
                ((S[0].base == Base && S[1].scalar() > 0 && S[1].scalar() <= 0.03601) ||
                 (Base.empty() && AnchorNear(S[1].scalar(), 0.018, 1e-5))))
            {
                Found["BaseColor"] = MaterialAnchor::Register(I, S[0].base, L.reads(0, "xyz").second);
                Found["Metallic"] = MaterialAnchor::Constant(I, 0);
                Found["Specular"] = MaterialAnchor::Constant(I, S[1].scalar() / 0.036);
                Found["Roughness"] = MaterialAnchor::Constant(I, 1);
                break;
            }
        }
    }
    Found.erase("AOBase");
    std::size_t Stop = Code.size();
    for (const auto* K : {"Specular", "BaseColor", "Metallic", "SubsurfaceColor"})
        if (Found.count(K))
            Stop = std::min(Stop, Found.at(K).line);
    if (Stop != Code.size())
    {
        Found["stop"].line = Stop;
        if (!Found.count("AmbientOcclusion"))
        {
            MaterialAnchor V;
            if (AnchorGameOcclusion(Code, Stop, V))
                Found["AmbientOcclusion"] = V;
        }
    }
    return Found;
}
struct MaterialMaskAnchor
{
    std::size_t line = 0;
    std::pair<std::string, std::string> mask, clip;
    bool constant_clip = false;
    double clip_value = 0;
};
inline bool FindMaterialMaskAnchor(const std::vector<AnchorInstruction>& Code, MaterialMaskAnchor& Out)
{
    bool Hit = false;
    for (std::size_t I = 0; I < Code.size(); ++I)
        if (Code[I].op == "KIL")
        {
            for (std::size_t Offset = 1; Offset <= 5 && Offset <= I; ++Offset)
            {
                const auto J = I - Offset;
                const auto& C = Code[J];
                if (C.op == "SLT" && C.has("F32") && C.src.size() == 2 && !C.mask.empty())
                {
                    Out.line = J;
                    Out.mask = C.reads(0, C.mask.substr(0, 1));
                    Out.constant_clip = C.src[1].literal;
                    if (Out.constant_clip)
                        Out.clip_value = C.src[1].scalar();
                    else
                        Out.clip = C.reads(1, C.mask.substr(0, 1));
                    Hit = true;
                    break;
                }
            }
        }
    return Hit;
}
inline bool MaterialTwoSided(const std::vector<AnchorInstruction>& Code)
{
    for (const auto& L : Code)
        for (const auto& S : L.src)
            if (S.base == "fragment.facing")
                return true;
    return false;
}
}
}

// Exact unchanged-copy boundary for translucent output alpha. The caller must
// establish BLEND_Translucent and a material stop from the selected base policy.
#pragma once
#include "material_anchor.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialOpacityAnchor
{
    MaterialAnchor value;
    bool saturate = false;
};
inline MaterialOpacityAnchor MaterialCopySourceAnchor(const std::vector<AnchorInstruction>& Code, std::size_t At,
                                                      AnchorOperand Operand, char Component, std::size_t Stop)
{
    if (At >= Code.size() || Stop > Code.size() || std::string("xyzw").find(Component) == std::string::npos)
        throw EvalError("translucent-opacity-range");
    MaterialOpacityAnchor Out;
    while (true)
    {
        if (Operand.literal)
        {
            auto Value = Operand.vector()[std::string("xyzw").find(Component)];
            Out.value = MaterialAnchor::Constant(At, Out.saturate ? std::min(std::max(Value, 0.0), 1.0) : Value);
            Out.saturate = false;
            return Out;
        }
        const auto Register = Operand.base;
        Component = Operand.component(std::string("xyzw").find(Component));
        std::size_t Last = Code.size();
        for (std::size_t I = Stop; I < At; ++I)
            if (Code[I].dst == Register && Code[I].mask.find(Component) != std::string::npos)
                Last = I;
        if (Last == Code.size())
        {
            Out.value = MaterialAnchor::Register(At, Register, std::string(1, Component));
            return Out;
        }
        At = Last;
        const auto& L = Code[At];
        int Depth = 0;
        for (std::size_t I = 0; I < At; ++I)
        {
            if (Code[I].op == "IF")
                ++Depth;
            else if (Code[I].op == "ENDIF")
                --Depth;
        }
        if (Depth || L.op != "MOV" || L.src.size() != 1 ||
            L.text.substr(0, L.text.find(',')).find('(') != std::string::npos)
            throw EvalError("translucent opacity: its component changes after the material stop");
        Operand = L.src[0];
        if (Operand.negative || Operand.absolute)
            throw EvalError("translucent opacity: a scheduled copy transforms its source");
        Out.saturate = Out.saturate || L.has("SAT");
    }
}
inline MaterialOpacityAnchor FindMaterialOpacityAnchor(const std::vector<AnchorInstruction>& Code, std::size_t Stop)
{
    std::vector<std::size_t> Writes;
    for (std::size_t I = 0; I < Code.size(); ++I)
        if (Code[I].dst == "result_color0" && Code[I].mask.find('w') != std::string::npos)
            Writes.push_back(I);
    if (Writes.size() != 1)
        throw EvalError("translucent opacity: output alpha has " + std::to_string(Writes.size()) + " writes");
    const auto At = Writes[0];
    const auto& L = Code[At];
    if (L.op != "MOV" || L.src.size() != 1 || L.text.substr(0, L.text.find(',')).find('(') != std::string::npos ||
        L.has("SAT"))
        throw EvalError("translucent opacity: output alpha is not an unconditional unchanged MOV");
    if (L.src[0].negative || L.src[0].absolute)
        throw EvalError("translucent opacity: output alpha transforms its source");
    return MaterialCopySourceAnchor(Code, At, L.src[0], 'w', Stop);
}
// Corrected must be the generic anchors after CorrectMaterialAmbientOcclusion
// and ForwardMaterialWorldNormal. This isolates the lighting control boundary
// from symbolic normal lifting and preserves exact scheduled clamp metadata.
inline MaterialAnchors FindMaterialLightingAnchors(const std::vector<AnchorInstruction>& Code,
                                                   MaterialAnchors Corrected, const std::string& BlendMode)
{
    if (BlendMode != "BLEND_Translucent" || !Corrected.count("stop"))
        return Corrected;
    const auto Stop = Corrected.at("stop").line;
    const auto Put = [&](const std::string& Name, const MaterialOpacityAnchor& A)
    {
        auto Value = A.value;
        Value.saturate = A.saturate;
        Corrected[Name] = Value;
    };
    Put("Opacity", FindMaterialOpacityAnchor(Code, Stop));
    auto AO = Corrected.find("AmbientOcclusion");
    if (AO != Corrected.end() && AO->second.line + 1 < Code.size())
    {
        const auto& Begin = Code[AO->second.line];
        const auto& Multiply = Code[AO->second.line + 1];
        if (Begin.op == "MAD" && Multiply.op == "MUL" && Multiply.src.size() == 2 && Multiply.mask.size() == 1)
            for (std::size_t K = 0; K < 2; ++K)
                if (Multiply.src[K].base != Begin.dst && !Multiply.src[K].literal)
                {
                    auto Value = Multiply.reads(K, Multiply.mask);
                    AO->second = MaterialAnchor::Register(AO->second.line, Value.first, Value.second);
                    break;
                }
    }
    for (const auto* Name : {"Metallic", "Specular", "Roughness", "AmbientOcclusion"})
    {
        auto A = Corrected.find(Name);
        if (A != Corrected.end() && !A->second.has_constant && A->second.line >= Stop && A->second.comps.size() == 1)
        {
            Put(Name, MaterialCopySourceAnchor(Code, A->second.line,
                                               AnchorOperand(A->second.register_name + "." + A->second.comps),
                                               A->second.comps[0], Stop));
        }
    }
    return Corrected;
}
}
}

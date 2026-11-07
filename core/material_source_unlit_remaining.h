// An unlit opaque surface's material input before the engine's fog wrapper. Unreal clamps the unlit colour
// (max(EmissiveColor, 0)) and then applies vertex fog, volumetric fog and pre-exposure to it; a program whose tail
// after that clamp does more than scale the colour is cut at the clamp. The full original program remains available
// for finite numerical validation.
#pragma once
#include "material_source_unlit_scene.h"
#include "material_source_scene_cache.h"
#include "material_source_common.h"
#include <regex>
namespace P4MapKit
{
namespace Material
{
// The program's one unlit colour clamp and the register it writes.
struct MaterialUnlitFogWrapper
{
    std::size_t Clamp = std::string::npos;
    std::string Register;
};
// The register an operand reads ("-R1.xyz" reads R1), or empty for a literal or an input.
inline std::string MaterialOperandRegister(std::string operand)
{
    operand = trim(operand);
    while (!operand.empty() && (operand[0] == '-' || operand[0] == '|'))
        operand.erase(0, 1);
    const auto stop = operand.find_first_of(".|");
    operand = operand.substr(0, stop);
    return operand.size() > 1 && operand[0] == 'R' && std::isdigit(static_cast<unsigned char>(operand[1])) ? operand
                                                                                                           : "";
}
// The fog wrapper of a program: its one clamp "MAX.F Rk.xyz, Rx, {0, 0, 0, 0}.x" when every later instruction reads
// no material data (no Material buffer and no texture) and one of them adds to the clamped colour (the engine's fog
// term); none when the program has no clamp, several, material reads after it, or only a scaling of it.
inline MaterialUnlitFogWrapper OriginalUnlitFogWrapper(const MaterialPreparedProgram& program)
{
    static const std::regex clamp("^MAX\\.F (R[0-9]+)\\.xyz, R[0-9]+, \\{0, 0, 0, 0\\}\\.x$");
    MaterialUnlitFogWrapper found;
    std::size_t count = 0;
    for (std::size_t at = 0; at < program.Lines.size(); ++at)
    {
        std::smatch match;
        if (!std::regex_match(program.Lines[at], match, clamp))
            continue;
        found.Clamp = at;
        found.Register = match[1];
        ++count;
    }
    if (count != 1)
        return {};
    std::string material;
    for (const auto& buffer : program.BaseBinding.Buffers)
        if (buffer.second == "Material")
            material = "buf" + std::to_string(buffer.first) + "[";
    bool adds = false, writes = false;
    for (std::size_t at = found.Clamp + 1; at < program.Lines.size(); ++at)
    {
        const auto& line = program.Lines[at];
        const auto space = line.find(' ');
        const auto opcode = line.substr(0, line.find_first_of(" .", 0));
        if (IsMember(opcode, {"TEX", "TXL", "TXF", "TXB", "TXD", "TXG"}) ||
            (!material.empty() && line.find(material) != std::string::npos))
            return {};
        if (line.find("result_color0") != std::string::npos)
            writes = true;
        if ((opcode == "ADD" || opcode == "MAD") && space != std::string::npos)
        {
            const auto operands = split_operands(line.substr(space + 1));
            for (std::size_t i = 1; i < operands.size(); ++i)
                if (MaterialOperandRegister(operands[i]) == found.Register)
                    adds = true;
        }
    }
    return adds && writes ? found : MaterialUnlitFogWrapper{};
}
// An unlit opaque surface pixel program with a fog wrapper to cut.
inline bool OriginalUnlitFogWrapperProgram(const MaterialPreparedProgram& program, const Json& settings)
{
    return program.Reference.Frequency == 3 && settings.At("domain").Text() == "MD_Surface" &&
           settings.At("blend_mode").Text() == "BLEND_Opaque" && settings.At("shading_model").Text() == "MSM_Unlit" &&
           OriginalUnlitFogWrapper(program).Clamp != std::string::npos;
}
struct MaterialOriginalSoftBlackContract : MaterialEvidence
{
    MaterialPreparedProgram Original;
    MaterialNativeSceneResources Resources;
    std::size_t EmissiveClampLine = 0;
    std::string ClampRegister;
};
inline MaterialOriginalSoftBlackContract PrepareOriginalSoftBlackContract(const MaterialPreparedProgram& program,
                                                                          const Json& settings,
                                                                          bool originalAssociationProved)
{
    if (!originalAssociationProved)
        throw Refused("the fog wrapper cut requires a canonical original shader association",
                      "material SoftBlack source association");
    if (!OriginalUnlitFogWrapperProgram(program, settings))
        throw Refused("an unlit opaque program without a fog wrapper to cut", "material SoftBlack contract");
    const auto wrapper = OriginalUnlitFogWrapper(program);
    MaterialOriginalSoftBlackContract result;
    result.Original = program;
    result.EmissiveClampLine = wrapper.Clamp;
    result.ClampRegister = wrapper.Register;
    result.Resources = BindOriginalMaterialSceneResources(program, settings, true);
    if (!result.Resources.Factory.RolesProved)
        throw Refused("the fog wrapper's scene lookups have no original constructor identity",
                      "material SoftBlack scene contract");
    return result;
}
inline MaterialRecoveryPlan OriginalSoftBlackExtraction(const MaterialRecoveryPlan& plan,
                                                        const MaterialOriginalSoftBlackContract& contract)
{
    if (plan.Lines != contract.Original.Lines)
        throw Refused("SoftBlack extraction plan differs from checked original program", "material SoftBlack contract");
    auto out = plan;
    out.Lines.resize(contract.EmissiveClampLine + 1);
    out.Lines.push_back("MOV.F result_color0.xyz, " + contract.ClampRegister);
    out.Lines.push_back("MOV.F result_color0.w, {0, 0, 0, 0}.x");
    return out;
}
inline MaterialRecoveryResult
RecoverOriginalSoftBlackExpressionGraph(const MaterialRecoveryPlan& plan, const MaterialPreparedProgram& program,
                                        bool originalAssociationProved,
                                        MaterialOriginalSoftBlackContract* facts = nullptr)
{
    auto contract = PrepareOriginalSoftBlackContract(program, plan.Settings, originalAssociationProved);
    auto extraction = OriginalSoftBlackExtraction(plan, contract);
    const auto previous = plan.Rewrite;
    const auto factory = contract.Resources.Factory;
    extraction.Rewrite = [previous, factory](SymbolicArena& arena, std::vector<std::vector<Value>>& values)
    {
        if (previous)
            previous(arena, values);
        std::vector<Value> flat;
        for (const auto& row : values)
            flat.insert(flat.end(), row.begin(), row.end());
        SceneRoles roles;
        const auto rewritten = SceneReads(arena, flat, factory, roles);
        std::size_t i = 0;
        for (auto& row : values)
            for (auto& value : row)
                value = rewritten.at(i++);
        if (i != rewritten.size())
            throw Refused("SoftBlack scene rewrite changed output width", "material rewrite outputs");
    };
    auto result = RecoverOriginalUnlitSceneExpressionGraph(extraction, program.Reference, program.Header, true);
    result.TraceStop = program.Lines.size();
    const auto fog = program.BaseBinding.Varyings.find("in_texcoord7");
    result.AssumedEngineFields[{"VertexFog", fog == program.BaseBinding.Varyings.end() ? -1 : fog->second}] =
        "original TEXCOORD7: prefog material-input extraction (finite full-program proof required)";
    result.AssumedEngineFields[{"View.PreExposure", 2180}] =
        "exposure-one material-input projection (finite full-program proof required)";
    result.FamilyComplete = false;
    result.CompilerProven = false;
    result.RendererValidated = false;
    if (facts)
        *facts = std::move(contract);
    return result;
}
}
} // P4MapKit::Material

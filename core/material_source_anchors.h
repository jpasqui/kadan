// Source-qualified normal anchors and late unit-normal dependency slices.
// Preparation proves identities; graph/family/runtime admission stays separate.
#pragma once
#include "material_prepare.h"
#include "material_program.h"
#include "material_anchor_lighting.h"
#include "material_normal.h"
#include "material_skin_source.h"
#include "material_schedule.h"

namespace P4MapKit
{
namespace Material
{
struct MaterialOriginalSourceAnchors
{
    MaterialAnchors Generic, Surface;
    std::map<std::string, MaterialScheduledAnchor> Scheduled;
    std::string TangentZ;
};
inline MaterialAnchors NormalizeOriginalMaterialAnchorStop(const std::vector<std::string>& lines, MaterialAnchors found)
{
    const auto marker = found.find("stop");
    if (marker == found.end())
        return found;
    if (marker->second.line > lines.size())
        throw Refused("original anchor stop exceeds program", "material source anchor range");
    while (marker->second.line && starts(lines[marker->second.line - 1], "IF "))
        --marker->second.line;
    return found;
}
inline bool FindOriginalDelayedMaterialNormal(const std::vector<AnchorInstruction>& code, const MaterialAnchors& found,
                                              MaterialScheduledAnchor& output)
{
    const auto stop = found.find("stop"), candidate = found.find("NormalWorld");
    if (stop == found.end() || candidate == found.end() || candidate->second.has_constant ||
        candidate->second.comps != "xyz" || candidate->second.line < stop->second.line ||
        candidate->second.line >= code.size())
        return false;
    const auto current = found.find("Normal");
    if (current != found.end() && !current->second.has_constant && current->second.line >= stop->second.line &&
        !AnchorWritten(code, current->second.register_name, current->second.comps, stop->second.line,
                       current->second.line))
        return false;
    const auto at = candidate->second.line;
    const auto& multiply = code[at];
    if (multiply.op != "MUL" || multiply.mask != "xyz" || multiply.dst != candidate->second.register_name)
        return false;
    bool matched = false;
    for (std::size_t before = at > 30 ? at - 30 : 0; before < at; ++before)
    {
        const auto& dot = code[before];
        if (dot.op != "DP3" || dot.src.size() != 2 || dot.reads(0, "xyz") != dot.reads(1, "xyz"))
            continue;
        MaterialAnchor normalized;
        if (AnchorNormalizedVector(code, dot.reads(0, "xyz"), before, normalized) && normalized.line == at &&
            normalized.register_name == candidate->second.register_name && normalized.comps == candidate->second.comps)
        {
            matched = true;
            break;
        }
    }
    if (!matched)
        return false;
    // Include the MUL's newly written value; no other lighting instruction is
    // evaluated merely because it lies between the stop and the normalizer.
    MaterialComponents live;
    for (const auto c : std::string("xyz"))
        live.insert({candidate->second.register_name, c});
    std::vector<int> depths;
    int depth = 0;
    for (const auto& line : code)
    {
        if (line.op == "ENDIF" || line.op == "ENDREP")
            --depth;
        depths.push_back(depth);
        if (line.op == "IF" || line.op == "REP")
            ++depth;
    }
    MaterialScheduledAnchor result;
    result.ValueAnchor = MaterialAnchor::Register(stop->second.line, candidate->second.register_name, "xyz");
    result.ScheduledLine = at + 1;
    for (std::size_t end = at + 1; end > stop->second.line;)
    {
        const auto index = --end;
        const auto& instruction = code[index];
        std::string affected;
        for (const auto c : instruction.mask)
            if (live.count({instruction.dst, c}))
                affected += c;
        if (affected.empty())
            continue;
        if (depths[index] || instruction.text.substr(0, instruction.text.find(',')).find('(') != std::string::npos ||
            !IsMember(instruction.op, {"MOV", "MUL", "DP3", "RSQ"}))
            return false;
        result.Indices.push_back(index);
        for (const auto c : affected)
            live.erase({instruction.dst, c});
        const auto reads = MaterialInstructionReads(instruction, affected);
        live.insert(reads.begin(), reads.end());
    }
    if (result.Indices.empty())
        return false;
    std::reverse(result.Indices.begin(), result.Indices.end());
    output = std::move(result);
    return true;
}
inline MaterialOriginalSourceAnchors PrepareOriginalMaterialSourceAnchors(const MaterialPreparedProgram& program,
                                                                          const Json& settings,
                                                                          const std::string& factory)
{
    const auto normalized = NormalizeMaterialTraceDeclarations(program.Lines);
    const auto code = ParseMaterialAnchorInstructions(normalized.Lines);
    MaterialOriginalSourceAnchors result;
    const auto tangent = program.BaseBinding.Varyings.find("in_texcoord11");
    if (tangent != program.BaseBinding.Varyings.end())
    {
        if (tangent->second < 0 || tangent->second > 255)
            throw Refused("original tangent-Z varying exceeds native input range", "material source tangent varying");
        result.TangentZ = "fragment.attrib[" + std::to_string(tangent->second) + "]";
    }
    result.Generic = FindMaterialAnchors(code, result.TangentZ);
    result.Surface = FindMaterialLightingAnchors(
        code, ForwardMaterialWorldNormal(code, CorrectMaterialAmbientOcclusion(code, result.Generic)),
        settings.At("blend_mode").Text());
    result.Surface = RecoverOriginalMaterialSkinAnchors(code, result.Surface, settings);
    result.Surface = NormalizeOriginalMaterialAnchorStop(normalized.Lines, std::move(result.Surface));
    if (settings.At("domain").Text() == "MD_Surface" && settings.At("blend_mode").Text() == "BLEND_Translucent" &&
        settings.At("shading_model").Text() == "MSM_DefaultLit" && settings.At("two_sided").kind == Json::Boolean &&
        !settings.At("two_sided").boolean &&
        IsMember(factory, {"FLocalVertexFactory", "FInstancedStaticMeshVertexFactory", "FSplineMeshVertexFactory"}))
    {
        MaterialScheduledAnchor delayed;
        if (FindOriginalDelayedMaterialNormal(code, result.Surface, delayed))
        {
            result.Surface["Normal"] = delayed.ValueAnchor;
            result.Surface["NormalWorld"] = delayed.ValueAnchor;
            result.Scheduled["Normal"] = delayed;
            result.Scheduled["NormalWorld"] = std::move(delayed);
        }
    }
    return result;
}
}
} // P4MapKit::Material

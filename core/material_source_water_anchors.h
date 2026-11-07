// Exact original late-opacity scheduling; material stop and blend stay intact.
#pragma once
#include "material_source_anchors.h"
namespace P4MapKit
{
namespace Material
{
inline MaterialOriginalSourceAnchors PrepareOriginalMaterialLateOpacityAnchors(const MaterialPreparedProgram& program,
                                                                               const Json& settings,
                                                                               const std::string& factory)
{
    std::string failure;
    try
    {
        return PrepareOriginalMaterialSourceAnchors(program, settings, factory);
    }
    catch (const EvalError& error)
    {
        failure = error.what();
    }
    if (settings.At("blend_mode").Text() != "BLEND_Translucent" ||
        failure.find("opacity: its component changes") == std::string::npos)
        throw EvalError(failure);
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
        code, ForwardMaterialWorldNormal(code, CorrectMaterialAmbientOcclusion(code, result.Generic)), "BLEND_Opaque");
    const auto marker = result.Surface.find("stop");
    if (marker == result.Surface.end())
        throw EvalError("late opacity has no original material boundary");
    std::vector<std::size_t> writes;
    for (std::size_t index = 0; index < code.size(); ++index)
        if (code[index].dst == "result_color0" && code[index].mask.find('w') != std::string::npos)
            writes.push_back(index);
    if (writes.size() != 1)
        throw EvalError("late opacity has no unique original alpha output");
    const auto at = writes.front();
    const auto& output = code[at];
    if (output.op != "MOV" || output.src.size() != 1 || output.has("SAT") ||
        output.text.substr(0, output.text.find(',')).find('(') != std::string::npos)
        throw EvalError("late opacity output is not an unchanged MOV");
    const auto opacity = BuildDelayedMaterialAnchor(code, marker->second.line, at, output.src[0], 'w');
    const auto ao = result.Surface.find("AmbientOcclusion");
    if (ao != result.Surface.end() && ao->second.line + 1 < code.size())
    {
        const auto& begin = code[ao->second.line];
        const auto& multiply = code[ao->second.line + 1];
        if (begin.op == "MAD" && multiply.op == "MUL" && multiply.src.size() == 2 && multiply.mask.size() == 1)
            for (std::size_t k = 0; k < 2; ++k)
                if (multiply.src[k].base != begin.dst && !multiply.src[k].literal)
                {
                    const auto value = multiply.reads(k, multiply.mask);
                    ao->second = MaterialAnchor::Register(ao->second.line, value.first, value.second);
                    break;
                }
    }
    for (const auto* name : {"Metallic", "Specular", "Roughness", "AmbientOcclusion"})
    {
        const auto found = result.Surface.find(name);
        if (found == result.Surface.end() || found->second.has_constant || found->second.line < marker->second.line ||
            found->second.comps.size() != 1)
            continue;
        const auto original = found->second;
        const AnchorOperand operand(original.register_name + "." + original.comps);
        MaterialOpacityAnchor copied;
        bool held = true;
        try
        {
            copied = MaterialCopySourceAnchor(code, original.line, operand, original.comps[0], marker->second.line);
        }
        catch (const EvalError&)
        {
            held = false;
        }
        if (held)
        {
            found->second = copied.value;
            found->second.saturate = copied.saturate;
        }
        else
        {
            const auto value =
                BuildDelayedMaterialAnchor(code, marker->second.line, original.line, operand, original.comps[0]);
            found->second = value.ValueAnchor;
            if (!value.Indices.empty())
                result.Scheduled[name] = value;
        }
    }
    result.Surface["Opacity"] = opacity.ValueAnchor;
    if (!opacity.Indices.empty())
        result.Scheduled["Opacity"] = opacity;
    result.Surface = RecoverOriginalMaterialSkinAnchors(code, result.Surface, settings);
    result.Surface = NormalizeOriginalMaterialAnchorStop(normalized.Lines, std::move(result.Surface));
    if (settings.At("domain").Text() == "MD_Surface" && settings.At("shading_model").Text() == "MSM_DefaultLit" &&
        settings.At("two_sided").kind == Json::Boolean && !settings.At("two_sided").boolean &&
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

// Particle tangent names, consumed normal carriers and exact prefix snapshots.
#pragma once
#include "material_source_particle_plan.h"
#include "material_source_water_anchors.h"
#include "material_source_particle_lit_capture.h"
#include "material_source_particle_lit_additive.h"
#include "material_source_foliage_forward.h"
#include "material_source_vector_anchors.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialOriginalParticleLitAnchors
{
    MaterialOriginalSourceAnchors Source;
    std::map<std::string, MaterialParticleEarlyNormal> Early;
};
inline bool OriginalParticleFirstAccessRead(const std::vector<AnchorInstruction>& code, const std::string& reg,
                                            char component, std::size_t begin)
{
    for (std::size_t i = begin; i < code.size(); ++i)
    {
        for (const auto& operand : code[i].src)
            if (operand.base == reg &&
                (operand.swizzle.empty() || operand.swizzle.find(component) != std::string::npos))
                return true;
        if (code[i].dst == reg && code[i].mask.find(component) != std::string::npos)
            return false;
    }
    return false;
}
inline MaterialAnchors OriginalParticleConsumedNormal(const std::vector<AnchorInstruction>& code, MaterialAnchors found)
{
    const auto normal = found.find("NormalWorld"), stop = found.find("stop");
    if (normal == found.end() || stop == found.end() || normal->second.has_constant ||
        normal->second.line >= stop->second.line)
        return found;
    const auto original = normal->second;
    const auto begin = stop->second.line;
    const auto consumed = [&](const std::pair<std::string, std::string>& key)
    {
        return std::all_of(key.second.begin(), key.second.end(),
                           [&](char c) { return OriginalParticleFirstAccessRead(code, key.first, c, begin); });
    };
    if (!AnchorWritten(code, original.register_name, original.comps, original.line + 1, begin) &&
        consumed({original.register_name, original.comps}))
        return found;
    std::map<std::pair<std::string, std::string>, std::size_t> carriers = {
        {{original.register_name, original.comps}, original.line}};
    int depth = 0;
    for (std::size_t i = 0; i < begin; ++i)
    {
        const auto& line = code[i];
        if (line.op == "ENDIF")
            --depth;
        if (i <= original.line)
        {
            if (line.op == "IF")
                ++depth;
            continue;
        }
        bool candidate = false;
        if (!depth && line.text.substr(0, line.text.find(',')).find('(') == std::string::npos && line.mask == "xyz" &&
            !line.has("SAT"))
        {
            if (line.op == "MOV" && line.src.size() == 1 && !line.src[0].negative && !line.src[0].absolute)
                candidate = carriers.count(line.reads(0, "xyz")) != 0;
            else if (line.op == "MUL" && line.src.size() == 2 && !line.src[0].negative && !line.src[0].absolute &&
                     !line.src[1].negative && !line.src[1].absolute)
                for (std::size_t k = 0; k < 2; ++k)
                {
                    const auto other = line.reads(1 - k, "xyz").second;
                    candidate =
                        candidate || (carriers.count(line.reads(k, "xyz")) && !other.empty() &&
                                      std::all_of(other.begin(), other.end(), [&](char c) { return c == other[0]; }));
                }
        }
        for (auto carrier = carriers.begin(); carrier != carriers.end();)
            if (carrier->first.first == line.dst &&
                std::any_of(carrier->first.second.begin(), carrier->first.second.end(),
                            [&](char c) { return line.mask.find(c) != std::string::npos; }))
                carrier = carriers.erase(carrier);
            else
                ++carrier;
        if (candidate)
            carriers[{line.dst, "xyz"}] = i;
        if (line.op == "IF")
            ++depth;
    }
    std::vector<MaterialAnchor> held;
    for (const auto& carrier : carriers)
        if (consumed(carrier.first))
            held.push_back(MaterialAnchor::Register(carrier.second, carrier.first.first, carrier.first.second));
    if (held.size() == 1)
        found["NormalWorld"] = held.front();
    return found;
}
inline MaterialOriginalParticleLitAnchors
PrepareOriginalParticleLitAnchors(const MaterialPreparedProgram& prepared,
                                  const MaterialOriginalParticleSourcePlan& plan)
{
    auto program = prepared;
    program.Lines = plan.Source.Recovery.Lines;
    const auto factory = plan.Source.Factory;
    if (factory == "FGPUSpriteVertexFactory")
    {
        const auto tangent = program.BaseBinding.Varyings.find("in_tangentz");
        if (tangent != program.BaseBinding.Varyings.end())
            program.BaseBinding.Varyings["in_texcoord11"] = tangent->second;
        else
            program.BaseBinding.Varyings.erase("in_texcoord11");
    }
    MaterialOriginalParticleLitAnchors result;
    result.Source = PrepareOriginalMaterialLateOpacityAnchors(program, plan.Source.Recovery.Settings, factory);
    const auto code = ParseMaterialAnchorInstructions(program.Lines);
    result.Source.Surface = NormalizeOriginalMaterialAnchorStop(
        program.Lines,
        OriginalParticleReflectionCaptureAnchors(code, std::move(result.Source.Surface), program.BaseBinding));
    result.Source.Surface = OriginalParticleConsumedNormal(code, std::move(result.Source.Surface));
    const auto marker = result.Source.Surface.find("stop");
    if (marker != result.Source.Surface.end())
    {
        const auto blend = plan.Source.Recovery.Settings.At("blend_mode").Text();
        if (IsMember(blend, {"BLEND_Opaque", "BLEND_Masked"}))
            for (const auto* name : {"Metallic", "Specular", "Roughness", "AmbientOcclusion"})
            {
                const auto found = result.Source.Surface.find(name);
                if (found == result.Source.Surface.end() || found->second.has_constant ||
                    found->second.line < marker->second.line || found->second.comps.size() != 1)
                    continue;
                const auto raw = found->second;
                const AnchorOperand operand(raw.register_name + "." + raw.comps);
                try
                {
                    const auto copied =
                        MaterialCopySourceAnchor(code, raw.line, operand, raw.comps[0], marker->second.line);
                    found->second = copied.value;
                    found->second.saturate = copied.saturate;
                }
                catch (const EvalError&)
                {
                    const auto delayed =
                        BuildDelayedMaterialAnchor(code, marker->second.line, raw.line, operand, raw.comps[0]);
                    found->second = delayed.ValueAnchor;
                    if (!delayed.Indices.empty())
                        result.Source.Scheduled[name] = delayed;
                }
            }
        MaterialScheduledAnchor delayed;
        if (FindOriginalDelayedMaterialNormal(code, result.Source.Surface, delayed))
        {
            result.Source.Surface["Normal"] = delayed.ValueAnchor;
            result.Source.Surface["NormalWorld"] = delayed.ValueAnchor;
            result.Source.Scheduled["Normal"] = delayed;
            result.Source.Scheduled["NormalWorld"] = std::move(delayed);
        }
        MaterialParticleEarlyNormal early;
        if (FindOriginalParticleEarlyNormal(code, result.Source.Surface, early))
            for (const auto* name : {"Normal", "NormalWorld"})
            {
                result.Source.Surface[name] = early.Anchor;
                result.Source.Scheduled.erase(name);
                result.Early[name] = early;
            }
    }
    if (plan.Source.Recovery.Settings.At("shading_model").Text() == "MSM_TwoSidedFoliage")
    {
        result.Source.Surface =
            ApplyOriginalFoliageForwardAnchors(code, std::move(result.Source.Surface), program.BaseBinding.Buffers);
        result.Source = ScheduleOriginalMaterialVectorAnchors(program.Lines, std::move(result.Source));
    }
    if (plan.Source.Recovery.Settings.At("blend_mode").Text() == "BLEND_Additive")
        result.Source = OriginalParticleLitAdditiveAnchors(code, std::move(result.Source), program.BaseBinding);
    return result;
}
}
} // P4MapKit::Material

// Original late material color registers retain only their dependency slices.
#pragma once
#include "material_source_water_anchors.h"
#include "material_emission.h"
#include "material_source_foliage_forward.h"
#include "material_source_particle_lit_additive.h"
namespace P4MapKit
{
namespace Material
{
inline MaterialOriginalSourceAnchors ScheduleOriginalMaterialVectorAnchors(const std::vector<std::string>& lines,
                                                                           MaterialOriginalSourceAnchors source)
{
    const auto stop = source.Surface.find("stop");
    if (stop == source.Surface.end())
        return source;
    const auto code = ParseMaterialAnchorInstructions(lines);
    for (const auto* name : {"BaseColor", "SubsurfaceColor"})
    {
        const auto found = source.Surface.find(name);
        if (found == source.Surface.end() || found->second.register_name.empty() || source.Scheduled.count(name) ||
            found->second.line < stop->second.line)
            continue;
        const auto original = found->second;
        if (!AnchorWritten(code, original.register_name, original.comps, stop->second.line, original.line))
            continue;
        auto value = BuildDelayedMaterialVectorAnchor(code, stop->second.line, original.line,
                                                      AnchorOperand(original.register_name), original.comps);
        value.ValueAnchor.saturate = original.saturate;
        found->second = value.ValueAnchor;
        if (!value.Indices.empty())
            source.Scheduled[name] = std::move(value);
    }
    return source;
}
inline MaterialOriginalSourceAnchors PrepareOriginalMaterialLateVectorAnchors(const MaterialPreparedProgram& program,
                                                                              const Json& settings,
                                                                              const std::string& factory)
{
    const auto lines = NormalizeMaterialTraceDeclarations(program.Lines).Lines;
    auto source = PrepareOriginalMaterialLateOpacityAnchors(program, settings, factory);
    if (settings.At("shading_model").Text() == "MSM_TwoSidedFoliage")
    {
        const auto code = ParseMaterialAnchorInstructions(lines);
        source.Surface =
            ApplyOriginalFoliageForwardAnchors(code, std::move(source.Surface), program.BaseBinding.Buffers);
        if (settings.At("blend_mode").Text() == "BLEND_Additive")
            source = OriginalParticleLitAdditiveAnchors(code, std::move(source), program.BaseBinding);
    }
    return ScheduleOriginalMaterialVectorAnchors(lines, std::move(source));
}
}
} // P4MapKit::Material

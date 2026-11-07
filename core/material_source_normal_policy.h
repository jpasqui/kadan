// The particle families' policy rule (material_lit_particles.policy_shader, material_foliage_particles
// .policy_shader): non-directional lighting policies optimize the whole world normal out, so a lit
// owner drawn by particle factories is recovered from an actual normal-bearing policy of the same
// factory. Among the policies that hold the material boundary, the first (in sorted factory/resource
// order) with the most of {a subsurface color, a world normal} is taken. Every other policy keeps its
// own named evidence; an owner whose selected program already holds a normal is unchanged.
#pragma once
#include "material_source_subsurface.h"
#include "material_source_particle_owner.h"
namespace P4MapKit
{
namespace Material
{
inline FMaterialShaderReference
SelectOriginalMaterialNormalProgram(const MaterialOriginalOwnerSource& owner,
                                    const MaterialOriginalArchiveAccess& archive,
                                    const FMaterialShaderReference& selected,
                                    MaterialDomainSourceScope scope = MaterialDomainSourceScope::ReadOnlyExpressions)
{
    if (owner.User.Cooked.Domain != "MD_Surface" || owner.User.Cooked.ShadingModel == "MSM_Unlit" ||
        OriginalParticleOwnerFactories(owner).empty())
        return selected;
    const auto holdsNormal = [](const MaterialAnchors& anchors)
    {
        return anchors.count("Normal") || anchors.count("NormalWorld") || anchors.count("NormalDecal");
    };
    const auto anchorsOf = [&](const FMaterialShaderReference& ref)
    {
        const auto source = PrepareOriginalMaterialArchiveProgram(owner, ref, archive, scope);
        return PrepareOriginalMaterialLateVectorAnchors(source.Program, owner.User.Settings,
                                                        MaterialFactoryName(ref.VertexFactoryHash))
            .Surface;
    };
    const auto current = anchorsOf(selected);
    const auto source = PrepareOriginalMaterialArchiveProgram(owner, selected, archive, scope);
    const bool sided = MaterialTwoSided(
        ParseMaterialAnchorInstructions(NormalizeMaterialTraceDeclarations(source.Program.Lines).Lines));
    if (!current.count("stop") || (holdsNormal(current) && sided == owner.User.Settings.At("two_sided").boolean))
        return selected;
    const auto grouped = ShaderPolicyIndex(owner.References).FirstByResource(selected.VertexFactoryHash);
    const FMaterialShaderReference* best = nullptr;
    std::pair<int, int> bestKey{-1, -1};
    for (const auto& entry : grouped)
    {
        const auto anchors = anchorsOf(entry.second);
        if (!anchors.count("stop"))
            continue;
        const auto candidate = PrepareOriginalMaterialArchiveProgram(owner, entry.second, archive, scope);
        if (MaterialTwoSided(
                ParseMaterialAnchorInstructions(NormalizeMaterialTraceDeclarations(candidate.Program.Lines).Lines)) !=
            owner.User.Settings.At("two_sided").boolean)
            continue;
        const std::pair<int, int> key{int(anchors.count("SubsurfaceColor") != 0), int(holdsNormal(anchors))};
        if (key > bestKey)
        {
            bestKey = key;
            best = &entry.second;
        }
    }
    if (!best || !bestKey.second)
        return selected;
    return CanonicalOriginalMaterialReference(owner, *best);
}
}
} // P4MapKit::Material

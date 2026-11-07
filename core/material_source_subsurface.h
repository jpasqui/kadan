// Select an original foliage permutation that structurally retains its input.
// No cooked setting, missing pin or original renderer policy is substituted.
#pragma once
#include "material_source_owner.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialOriginalSubsurfaceSelectionFacts
{
    std::size_t DistinctPrograms = 0, RetainedCandidates = 0;
    bool OrdinaryRetainsColor = false, SelectedRetainsColor = false;
};
inline MaterialAnchors ReadOriginalMaterialGenericProgramAnchors(const MaterialDomainPreparedProgram& source)
{
    const auto normalized = NormalizeMaterialTraceDeclarations(source.Program.Lines);
    std::string tangent;
    const auto varying = source.Program.BaseBinding.Varyings.find("in_texcoord11");
    if (varying != source.Program.BaseBinding.Varyings.end())
    {
        if (varying->second < 0 || varying->second > 255)
            throw Refused("original tangent-Z varying exceeds native input range", "material source tangent varying");
        tangent = "fragment.attrib[" + std::to_string(varying->second) + "]";
    }
    return FindMaterialAnchors(ParseMaterialAnchorInstructions(normalized.Lines), tangent);
}
inline FMaterialShaderReference SelectOriginalMaterialSubsurfaceProgram(
    const MaterialOriginalOwnerSource& owner, const MaterialOriginalArchiveAccess& archive,
    const FMaterialShaderReference& ordinary,
    MaterialDomainSourceScope scope = MaterialDomainSourceScope::ReadOnlyExpressions,
    MaterialOriginalSubsurfaceSelectionFacts* facts = nullptr)
{
    const auto canonical = CanonicalOriginalMaterialReference(owner, ordinary);
    MaterialOriginalSubsurfaceSelectionFacts result;
    if (owner.User.Cooked.ShadingModel != "MSM_TwoSidedFoliage")
    {
        if (facts)
            *facts = result;
        return canonical;
    }
    const auto current = PrepareOriginalMaterialArchiveProgram(owner, canonical, archive, scope);
    result.OrdinaryRetainsColor = ReadOriginalMaterialGenericProgramAnchors(current).count("SubsurfaceColor") != 0;
    if (result.OrdinaryRetainsColor)
    {
        result.SelectedRetainsColor = true;
        if (facts)
            *facts = result;
        return canonical;
    }
    // material_lighting.policy_refs groups aliases by actual factory/resource
    // and visits increasing resource indices; each group's first typed ref is
    // authoritative for its original class association.
    const auto grouped = ShaderPolicyIndex(owner.References).FirstByResource(canonical.VertexFactoryHash);
    std::vector<FMaterialShaderReference> candidates;
    for (const auto& entry : grouped)
    {
        ++result.DistinctPrograms;
        const auto prepared = PrepareOriginalMaterialArchiveProgram(owner, entry.second, archive, scope);
        if (ReadOriginalMaterialGenericProgramAnchors(prepared).count("SubsurfaceColor"))
        {
            candidates.push_back(entry.second);
            ++result.RetainedCandidates;
        }
    }
    if (candidates.empty())
    {
        if (facts)
            *facts = result;
        return canonical;
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const FMaterialShaderReference& left, const FMaterialShaderReference& right)
                     {
                         const auto rank = [](const FMaterialShaderReference& ref)
                         {
                             const auto name = MaterialOriginalSourceShaderName(ref.TypeHash);
                             return std::make_tuple(name.find("LightMapPolicyHQ") == std::string::npos,
                                                    name.find("NoLightMap") == std::string::npos, name);
                         };
                         return rank(left) < rank(right);
                     });
    result.SelectedRetainsColor = true;
    if (facts)
        *facts = result;
    return candidates.front();
}
}
} // P4MapKit::Material

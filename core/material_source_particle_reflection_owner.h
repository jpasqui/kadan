// Recover both original ENABLE_SKY_LIGHT policies. Only emission may differ;
// all other source pins and cooked settings must have equal graph formulas.
#pragma once
#include "material_source_particle_lit_owner.h"
namespace P4MapKit
{
namespace Material
{
namespace ParticleReflectionSourceDetail
{
inline Json Formula(const Json& graph, const Json& link, std::map<std::string, std::size_t>& intern)
{
    const auto& nodes = graph.At("nodes");
    std::map<std::string, std::size_t> done;
    std::set<std::string> active;
    std::vector<std::pair<std::string, bool>> stack = {{link.array.at(0).Text(), false}};
    while (!stack.empty())
    {
        const auto frame = stack.back();
        stack.pop_back();
        if (done.count(frame.first))
            continue;
        const auto& node = nodes.At(frame.first);
        if (!frame.second)
        {
            if (!active.insert(frame.first).second)
                throw Refused("cycle in reflection graph", "material authored reflection graph");
            stack.push_back({frame.first, true});
            for (const auto& input : node.At("inputs").object)
                if (!done.count(input.second.array.at(0).Text()))
                    stack.push_back({input.second.array.at(0).Text(), false});
            continue;
        }
        auto inputs = Json::MakeObject();
        for (const auto& input : node.At("inputs").object)
        {
            auto value = input.second;
            value.array.at(0) = double(done.at(value.array.at(0).Text()));
            inputs[input.first] = value;
        }
        const auto key = Array({node.At("type"), inputs, node.At("props")}).Dump();
        const auto found = intern.find(key);
        const auto id = found == intern.end() ? intern.size() : found->second;
        if (found == intern.end())
            intern.emplace(key, id);
        done[frame.first] = id;
        active.erase(frame.first);
        if (done.size() > 100000)
            throw Refused("reflection graph exceeds node budget", "material authored reflection graph");
    }
    auto out = link;
    out.array.at(0) = double(done.at(link.array.at(0).Text()));
    return out;
}
}
// The node MergeOriginalAuthoredReflectionGraphs adds: an ENABLE_SKY_LIGHT choice between the two policies' emission.
inline const char* MaterialSkyLightPolicyNode()
{
    return "AuthoredReflectionPolicy";
}
// True when a graph already chooses its emission by ENABLE_SKY_LIGHT (both policies were recovered and merged).
inline bool MaterialGraphHasSkyLightPolicy(const Json& graph)
{
    return graph.Has("nodes") && graph.At("nodes").Has(MaterialSkyLightPolicyNode());
}
inline Json MergeOriginalAuthoredReflectionGraphs(const Json& sky, const Json& noSky)
{
    ValidateGraph(sky);
    ValidateGraph(noSky);
    const auto& a = sky.At("outputs");
    const auto& b = noSky.At("outputs");
    if (a.object.size() != b.object.size() || !a.Has("EmissiveColor") || !b.Has("EmissiveColor"))
        throw Refused("reflection policies have different explicit outputs", "material authored reflection policy");
    std::map<std::string, std::size_t> intern;
    for (const auto& pin : b.object)
    {
        if (!a.Has(pin.first))
            throw Refused("reflection policies have different explicit outputs", "material authored reflection policy");
        if (pin.first != "EmissiveColor" &&
            ParticleReflectionSourceDetail::Formula(sky, a.At(pin.first), intern).Dump() !=
                ParticleReflectionSourceDetail::Formula(noSky, pin.second, intern).Dump())
            throw Refused("reflection policies differ outside emission: " + pin.first,
                          "material authored reflection policy");
    }
    for (const auto* setting :
         {"domain", "blend_mode", "shading_model", "two_sided", "clip", "tangent_space_normal", "render_settings"})
        if (sky.Has(setting) != noSky.Has(setting) ||
            (sky.Has(setting) && sky.At(setting).Dump() != noSky.At(setting).Dump()))
            throw Refused("reflection policies disagree on cooked settings: " + std::string(setting),
                          "material authored reflection policy");
    auto result = noSky;
    for (const auto& row : sky.At("nodes").object)
    {
        const auto name = "AuthoredReflectionSky_" + row.first;
        if (result.At("nodes").Has(name))
            throw Refused("reflection graph node name collision", "material authored reflection graph");
        auto node = row.second;
        for (auto& input : node["inputs"].object)
            input.second.array.at(0) = "AuthoredReflectionSky_" + input.second.array.at(0).Text();
        result["nodes"][name] = std::move(node);
    }
    if (result.At("nodes").Has(MaterialSkyLightPolicyNode()))
        throw Refused("reflection policy node name collision", "material authored reflection graph");
    auto skyLink = a.At("EmissiveColor");
    skyLink.array.at(0) = "AuthoredReflectionSky_" + skyLink.array.at(0).Text();
    result["nodes"][MaterialSkyLightPolicyNode()] = Object(
        {{"type", "Custom"},
         {"inputs", Object({{"Sky", skyLink}, {"NoSky", b.At("EmissiveColor")}})},
         {"props", Object({{"Code", "#if ENABLE_SKY_LIGHT\nreturn Sky;\n#else\nreturn NoSky;\n#endif"},
                           {"OutputType", "CMOT_Float3"},
                           {"Description", "P4 native ENABLE_SKY_LIGHT material pin"},
                           {"Inputs", Array({Object({{"InputName", "Sky"}}), Object({{"InputName", "NoSky"}})})}})}});
    result["outputs"]["EmissiveColor"] = Array({MaterialSkyLightPolicyNode(), ""});
    ValidateGraph(result);
    return result;
}
// One ENABLE_SKY_LIGHT policy of a lit particle owner, recovered alone: the owner with only that policy's base-pass
// programs.
inline MaterialOriginalParticleOwnerRecovery
RecoverOriginalParticleLitPolicy(const MaterialOriginalOwnerSource& owner, const MaterialOriginalArchiveAccess& archive,
                                 MaterialDomainSourceScope scope,
                                 const std::function<void(SymbolicArena&, BindingContext&)>& binding,
                                 const MaterialSurfaceRewrites& rewrites, bool sky)
{
    auto selected = owner;
    selected.References.erase(std::remove_if(selected.References.begin(), selected.References.end(),
                                             [sky](const FMaterialShaderReference& ref)
                                             {
                                                 const auto name = MaterialOriginalSourceShaderName(ref.TypeHash);
                                                 return ref.Frequency == 3 && IsBasePassShaderName(name) &&
                                                        IsSkylightShaderName(name) != sky;
                                             }),
                              selected.References.end());
    return RecoverOriginalParticleLitOwnerExpressionGraph(selected, archive, scope, binding, rewrites);
}
// An owner whose emission depends on ENABLE_SKY_LIGHT: its base pass was compiled with and without the sky light into
// different programs, and the two policies, each recovered alone, give different EmissiveColor formulas. Both are
// then recovered and merged (MergeOriginalAuthoredReflectionGraphs). A policy that cannot be recovered alone leaves the
// owner to the single-policy recovery.
inline bool OriginalParticleAuthoredReflectionEligible(
    const MaterialOriginalOwnerSource& owner, const MaterialOriginalArchiveAccess& archive,
    MaterialDomainSourceScope scope, const std::function<void(SymbolicArena&, BindingContext&)>& binding,
    const MaterialSurfaceRewrites& rewrites)
{
    const ShaderPolicyIndex policies(owner.References);
    const auto indices = archive.MapIndices(owner.Map.ResourceHash);
    std::set<std::uint32_t> skyPrograms, noSkyPrograms;
    for (const auto& policy : policies.BasePass())
        (policy.Skylight ? skyPrograms : noSkyPrograms)
            .insert(ArchiveIndexOf(indices, policy.Reference,
                                   "original shader resource index is outside its archive map",
                                   "material authored reflection policy"));
    if (skyPrograms.empty() || noSkyPrograms.empty() || skyPrograms == noSkyPrograms)
        return false;
    Json sky, noSky;
    try
    {
        sky = RecoverOriginalParticleLitPolicy(owner, archive, scope, binding, rewrites, true)
                  .Recovery.Primary.Material.Graph;
        noSky = RecoverOriginalParticleLitPolicy(owner, archive, scope, binding, rewrites, false)
                    .Recovery.Primary.Material.Graph;
    }
    catch (const Refused&)
    {
        return false;
    }
    const auto& a = sky.At("outputs");
    const auto& b = noSky.At("outputs");
    if (!a.Has("EmissiveColor") || !b.Has("EmissiveColor"))
        return a.Has("EmissiveColor") != b.Has("EmissiveColor");
    std::map<std::string, std::size_t> intern;
    return ParticleReflectionSourceDetail::Formula(sky, a.At("EmissiveColor"), intern).Dump() !=
           ParticleReflectionSourceDetail::Formula(noSky, b.At("EmissiveColor"), intern).Dump();
}
inline MaterialOriginalParticleOwnerRecovery RecoverOriginalParticleAuthoredReflectionOwnerExpressionGraph(
    const MaterialOriginalOwnerSource& owner, const MaterialOriginalArchiveAccess& archive,
    MaterialDomainSourceScope scope = MaterialDomainSourceScope::QualifiedGlobalTail,
    const std::function<void(SymbolicArena&, BindingContext&)>& binding = {},
    const MaterialSurfaceRewrites& rewrites = {})
{
    auto policy = [&](bool sky)
    {
        return RecoverOriginalParticleLitPolicy(owner, archive, scope, binding, rewrites, sky);
    };
    const auto sky = policy(true);
    auto result = policy(false);
    result.Recovery.Primary.Material.Graph = MergeOriginalAuthoredReflectionGraphs(
        sky.Recovery.Primary.Material.Graph, result.Recovery.Primary.Material.Graph);
    result.ParticleFacts["authored_reflection_contract"] =
        Object({{"policy", "ENABLE_SKY_LIGHT"},
                {"other_material_pins", "structurally equal in both original permutations"},
                {"view_layout", "0x166705f7"}});
    result.ParticleFacts["unaccounted_values"].array.push_back(
        "authored engine reflection Custom requires ReflectionStruct declared before Generated/Material.ush and ENABLE_SKY_LIGHT on import");
    return result;
}
}
} // P4MapKit::Material

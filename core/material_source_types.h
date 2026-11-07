// Exact primary shader class name identities for automatic original program selection.
// Names identify a source contract; they do not prove its runtime implementation.
#pragma once
#include "material_source_recovery.h"
#include "material_engine_hashes.h"
#include <tuple>
namespace P4MapKit
{
namespace Material
{
inline const std::map<std::uint64_t, std::string>& MaterialOriginalSourceShaderNames()
{
    static const std::map<std::uint64_t, std::string> names = {
        {0x11bbd5f6eead4256ull, "TBasePassPSFMobileMovableDirectionalLightCSMWithLightmapPolicy"},
        {0x18a82ebde5d3d122ull, "TBasePassPSFPrecomputedVolumetricLightmapLightingPolicySkylight"},
        {0x1c9b57cd23cb98ecull, "TBasePassPSTLightMapPolicyLQSkylight"},
        {0x1dde9d3265602333ull, "TBasePassPSFMobileDistanceFieldShadowsAndLQLightMapPolicy"},
        {0x28b73d7ae30b556cull, "TBasePassPSTDistanceFieldShadowsAndLightMapPolicyHQ"},
        {0x30291128ef05ea5bull, "TBasePassPSFSimpleLightmapOnlyLightingPolicySkylight"},
        {0x30e14509095f3879ull, "TBasePassPSFMobileMovableDirectionalLightWithLightmapPolicySkylight"},
        {0x36ee023c48635cb2ull, "TBasePassPSFMobileDirectionalLightCSMAndLightMapPolicySkylight"},
        {0x3c79a2188506dc5bull, "TBasePassPSFSelfShadowedVolumetricLightmapPolicySkylight"},
        {0x3e8c09433d5fdc27ull, "TBasePassPSFSimpleDirectionalLightLightingPolicy"},
        {0x4000b9a57dda3ef7ull, "TBasePassPSFMobileMovableDirectionalLightWithLightmapPolicy"},
        {0x4203988c96241777ull, "TBasePassPSFMobileDistanceFieldShadowsLightMapAndCSMLightingPolicySkylight"},
        {0x4507f03a199c6526ull, "TBasePassPSFNoLightMapPolicy"},
        {0x4972b5658873ee44ull, "TBasePassPSFMobileDirectionalLightAndCSMPolicySkylight"},
        {0x4b3039e981a99284ull, "TBasePassPSFMobileDistanceFieldShadowsLightMapAndCSMLightingPolicy"},
        {0x4b8164749839ec41ull, "FLightFunctionPS"},
        {0x4c961825074d71c9ull, "TBasePassPSFMobileDirectionalLightCSMAndSHIndirectPolicySkylight"},
        {0x516b0b21292f6cceull, "TBasePassPSFSimpleStationaryLightSingleSampleShadowsLightingPolicy"},
        {0x58ed11e84076a289ull, "FVolumetricFogLightFunctionPS"},
        {0x645d8b45d285d605ull, "TBasePassPSFMobileDirectionalLightCSMAndSHIndirectPolicy"},
        {0x661e2d85a2c9dbe6ull, "TBasePassPSTDistanceFieldShadowsAndLightMapPolicyHQSkylight"},
        {0x666403914542b619ull, "TBasePassPSFPrecomputedVolumetricLightmapLightingPolicy"},
        {0x6d90922c199fe04full, "TBasePassPSTLightMapPolicyHQ"},
        {0x706f654826d359fcull, "TBasePassPSFSimpleStationaryLightPrecomputedShadowsLightingPolicySkylight"},
        {0x787d13a1ace9957dull, "TBasePassPSFSelfShadowedVolumetricLightmapPolicy"},
        {0x7b0a3b6664b0695cull, "TBasePassPSFSimpleStationaryLightVolumetricLightmapShadowsLightingPolicySkylight"},
        {0x802927f5cc385835ull, "TBasePassPSFCachedPointIndirectLightingPolicySkylight"},
        {0x814cdf86ec89fbfbull, "TBasePassPSFSimpleDirectionalLightLightingPolicySkylight"},
        {0x89588aefe9328bc4ull, "FPostProcessMaterialPS"},
        {0x8ad55c2e1c3ee03full, "TSlateMaterialShaderPSColorFont"},
        {0x8bb122acb1590c8full, "TSlateMaterialShaderPSGrayscaleFont"},
        {0x8d1627e7456c737cull, "TBasePassPSFCachedPointIndirectLightingPolicy"},
        {0x911ee94a91189b71ull, "TSlateMaterialShaderPSCustom"},
        {0x9134506f7bff0ffaull, "TBasePassPSFSelfShadowedTranslucencyPolicy"},
        {0x925355faf735b81cull, "FDepthOnlyPS<false>"},
        {0x9371456966c31d46ull, "TBasePassPSFCachedVolumeIndirectLightingPolicy"},
        {0x979eb83f4a91dcb3ull, "TBasePassPSFSimpleStationaryLightPrecomputedShadowsLightingPolicy"},
        {0x97f99c74f119c486ull, "TBasePassPSFCachedVolumeIndirectLightingPolicySkylight"},
        {0x9d0d67e991a616dcull, "TBasePassPSFSimpleLightmapOnlyLightingPolicy"},
        {0x9ea2378a2db522f1ull, "TBasePassPSFMobileDirectionalLightAndSHIndirectPolicySkylight"},
        {0x9f63684dbca4b663ull, "TBasePassPSFSelfShadowedCachedPointIndirectLightingPolicySkylight"},
        {0xa8d53bcb8fca168dull, "TBasePassPSFNoLightMapPolicySkylight"},
        {0xac0849e91682bf04ull, "TSlateMaterialShaderPSLineSegment"},
        {0xba9b0d30dcc48e73ull, "TBasePassPSFSelfShadowedTranslucencyPolicySkylight"},
        {0xc7e1528d5c386917ull, "TBasePassPSTLightMapPolicyLQ"},
        {0xc8da498d2207b9beull, "TBasePassPSFMobileDirectionalLightAndCSMPolicy"},
        {0xca92a3ce0b01e043ull, "TBasePassPSFSimpleNoLightmapLightingPolicySkylight"},
        {0xcbbaa41ba470d935ull, "TBasePassPSFSimpleNoLightmapLightingPolicy"},
        {0xcbf72596a6e713c2ull, "FDeferredDecalPS"},
        {0xcd14071d4e6fe28cull, "TBasePassPSFMobileDistanceFieldShadowsAndLQLightMapPolicySkylight"},
        {0xd066509a8b8e4e0bull, "TSlateMaterialShaderPSDefault"},
        {0xdbbfd1d594df3611ull, "TBasePassPSTLightMapPolicyHQSkylight"},
        {0xe3031705b0aac089ull, "TBasePassPSFSimpleStationaryLightSingleSampleShadowsLightingPolicySkylight"},
        {0xe33f938b80d1a25eull, "FDepthOnlyPS<true>"},
        {0xe38d98e8b45169dbull, "TBasePassPSFSelfShadowedCachedPointIndirectLightingPolicy"},
        {0xe8fff5e3b279af3aull, "FMeshDecalsPS"},
        {0xf0118975ce2c0de5ull, "TBasePassPSFSimpleStationaryLightVolumetricLightmapShadowsLightingPolicy"},
        {0xf04ccd5b2c60c076ull, "TBasePassPSFMobileDirectionalLightCSMAndLightMapPolicy"},
        {0xf107acbb91d41a50ull, "TBasePassPSFMobileMovableDirectionalLightCSMWithLightmapPolicySkylight"},
        {0xf6fb6def354e601full, "TSlateMaterialShaderPSBorder"},
        {0xfde4488d7ba2f490ull, "TBasePassPSFMobileDirectionalLightAndSHIndirectPolicy"}};
    return names;
}
inline std::string MaterialOriginalSourceShaderName(std::uint64_t hash)
{
    const auto name = MaterialOriginalSourceShaderNames().find(hash);
    return name == MaterialOriginalSourceShaderNames().end() ? std::string() : name->second;
}
inline bool IsSkylightShaderName(const std::string& name)
{
    return name.size() >= 8 && name.substr(name.size() - 8) == "Skylight";
}
inline bool IsBasePassShaderName(const std::string& name)
{
    return starts(name, "TBasePassPS");
}
// A base pass pixel program: frequency 3 with a TBasePassPS shader type, one per lighting policy, each in a
// plain and a Skylight variant.
inline bool IsBasePassPixelShader(const FMaterialShaderReference& ref)
{
    return ref.Frequency == 3 && IsBasePassShaderName(MaterialOriginalSourceShaderName(ref.TypeHash));
}
struct ShaderPolicy
{
    FMaterialShaderReference Reference;
    std::string Name;
    bool Skylight = false;
};
// The base pass pixel programs among shader references, the enumeration every source producer starts from.
class ShaderPolicyIndex
{
public:
    explicit ShaderPolicyIndex(const std::vector<FMaterialShaderReference>& references)
    {
        for (const auto& ref : references)
        {
            if (ref.Frequency != 3)
                continue;
            auto name = MaterialOriginalSourceShaderName(ref.TypeHash);
            if (!IsBasePassShaderName(name))
                continue;
            const bool sky = IsSkylightShaderName(name);
            Policies.push_back({ref, std::move(name), sky});
        }
    }
    // In reference order.
    const std::vector<ShaderPolicy>& BasePass() const&
    {
        return Policies;
    }
    // A temporary index would leave the list it returns dangling.
    const std::vector<ShaderPolicy>& BasePass() && = delete;
    // The first program of each resource index of one vertex factory, by increasing resource index
    // (material_lighting.policy_refs).
    std::map<std::int32_t, FMaterialShaderReference> FirstByResource(std::uint64_t factory) const
    {
        std::map<std::int32_t, FMaterialShaderReference> out;
        for (const auto& policy : Policies)
            if (policy.Reference.VertexFactoryHash == factory)
                out.emplace(policy.Reference.ResourceIndex, policy.Reference);
        return out;
    }

private:
    std::vector<ShaderPolicy> Policies;
};
inline FMaterialShaderReference SelectOriginalMaterialPrimaryProgram(const MaterialOriginalOwnerSource& owner)
{
    const auto domain = owner.User.Cooked.Domain;
    std::vector<FMaterialShaderReference> candidates;
    if (domain == "MD_LightFunction" || domain == "MD_PostProcess")
    {
        const auto type = domain == "MD_LightFunction" ? 0x58ed11e84076a289ull : 0x89588aefe9328bc4ull;
        for (const auto& ref : owner.References)
            if (ref.Frequency == 3 && !ref.VertexFactoryHash && ref.TypeHash == type)
                candidates.push_back(ref);
        if (candidates.size() != 1)
            throw Refused("original domain has no unique source-qualified primary pixel program",
                          "material source primary program");
        return candidates[0];
    }
    const bool translucent =
        owner.User.Cooked.BlendMode == "BLEND_Translucent" && owner.User.Cooked.ShadingModel != "MSM_Unlit";
    const ShaderPolicyIndex policies(owner.References);
    for (const auto& policy : policies.BasePass())
        if (policy.Reference.VertexFactoryHash && (translucent || !policy.Skylight))
            candidates.push_back(policy.Reference);
    if (candidates.empty())
        throw Refused("original material has no source-qualified base pass pixel program",
                      "material source primary program");
    if (!translucent)
    {
        // material_probe.pick first chooses the exact NoLightMap class across
        // factories, then prefers local within that class. Its fallback retains
        // original reference order and excludes every Skylight class.
        const bool exact = std::any_of(candidates.begin(), candidates.end(), [](const FMaterialShaderReference& ref)
                                       { return ref.TypeHash == 0x4507f03a199c6526ull; });
        if (exact)
            candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                            [](const FMaterialShaderReference& ref)
                                            { return ref.TypeHash != 0x4507f03a199c6526ull; }),
                             candidates.end());
        std::stable_sort(candidates.begin(), candidates.end(),
                         [](const FMaterialShaderReference& a, const FMaterialShaderReference& b)
                         {
                             return (a.VertexFactoryHash != EngineHash::LocalVertexFactory) <
                                    (b.VertexFactoryHash != EngineHash::LocalVertexFactory);
                         });
    }
    else
    {
        // material_decompile._factory_shader selects the local factory's HQ
        // lightmap program first so PrecomputedAOMask remains observable.
        std::stable_sort(candidates.begin(), candidates.end(),
                         [](const FMaterialShaderReference& a, const FMaterialShaderReference& b)
                         {
                             const auto rank = [](const FMaterialShaderReference& ref)
                             {
                                 const auto name = MaterialOriginalSourceShaderName(ref.TypeHash);
                                 const bool local = ref.VertexFactoryHash == EngineHash::LocalVertexFactory;
                                 const bool sky = IsSkylightShaderName(name);
                                 const bool hq = name.find("LightMapPolicyHQ") != std::string::npos;
                                 const bool no = name.find("NoLightMap") != std::string::npos;
                                 return std::make_tuple(!local, !hq, sky, !no, name);
                             };
                             return rank(a) < rank(b);
                         });
    }
    MaterialFactoryName(candidates[0].VertexFactoryHash);
    return candidates[0];
}
inline MaterialOriginalProgramRecovery
RecoverOriginalMaterialPrimaryProgram(const MaterialOriginalOwnerSource& owner,
                                      const MaterialOriginalArchiveAccess& archive,
                                      MaterialDomainSourceScope scope = MaterialDomainSourceScope::QualifiedGlobalTail,
                                      const std::function<void(SymbolicArena&, BindingContext&)>& binding = {},
                                      const MaterialSurfaceRewrites& rewrites = {})
{
    return RecoverOriginalMaterialSelectedProgram(owner, SelectOriginalMaterialPrimaryProgram(owner), archive, scope,
                                                  binding, rewrites);
}
}
} // P4MapKit::Material

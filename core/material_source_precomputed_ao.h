// Original baked AO resource identity. Scheduling and renderer admission remain
// separate from the constructor/SRT association and material-input formula.
#pragma once
#include "material_prepare.h"
#include "material_scene_original_layout.h"
#include "material_source_common.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialOriginalPrecomputedAOFacts : MaterialEvidence
{
    int Buffer = -1, TextureSlot = -1;
    std::size_t Samples = 0;
    std::map<int, std::string> ConsumedSamplers;
    bool ConstructorAssociationProved = false;
};
inline const MaterialSceneNativeLayout& MaterialOriginalLightmapClusterLayout()
{
    static const MaterialSceneNativeLayout layout = []()
    {
        MaterialSceneNativeLayout out;
        out.Name = "FLightmapResourceClusterShaderParameters";
        out.Size = 128;
        out.StaticSlot = 255;
        out.Hash = 0x0b8d0b8c;
        out.ConstructorProvenanceProved = true;
        out.Resources = {{"LightMapTexture", 0, 5},
                         {"SkyOcclusionTexture", 8, 5},
                         {"AOMaterialMaskTexture", 16, 5},
                         {"StaticShadowTexture", 24, 5},
                         {"VTLightMapTexture", 32, 6},
                         {"VTLightMapTexture_1", 40, 6},
                         {"VTSkyOcclusionTexture", 48, 6},
                         {"VTAOMaterialMaskTexture", 56, 6},
                         {"VTStaticShadowTexture", 64, 6},
                         {"LightMapSampler", 72, 8},
                         {"SkyOcclusionSampler", 80, 8},
                         {"AOMaterialMaskSampler", 88, 8},
                         {"StaticShadowTextureSampler", 96, 8},
                         {"LightmapVirtualTexturePageTable0", 104, 5},
                         {"LightmapVirtualTexturePageTable1", 112, 5}};
        if (MaterialSceneNativeLayoutHash(out) != out.Hash)
            throw Refused("original lightmap descriptor checksum disagrees", "original precomputed AO layout");
        return out;
    }();
    return layout;
}
inline void InstallOriginalMaterialPrecomputedAO(BindingContext& binding, const FMaterialShaderReference& reference,
                                                 const CustomMaterial::ShaderBindings& header,
                                                 bool originalAssociationProved,
                                                 const std::shared_ptr<MaterialOriginalPrecomputedAOFacts>& facts = {})
{
    int buffer = -1;
    for (const auto& field : reference.UniformBufferHashes)
        if (field.second == 0x198d8af2b3acf133ull)
        {
            if (buffer != -1 || field.first > 14)
                throw BindingRefused("original lightmap cluster identity is ambiguous",
                                     "original precomputed AO association");
            buffer = field.first;
        }
    if (buffer < 0)
        return;
    const auto& layout = MaterialOriginalLightmapClusterLayout();
    if (!originalAssociationProved || reference.Frequency != 3 || std::size_t(buffer) >= header.layout_hashes.size() ||
        header.layout_hashes[buffer] != layout.Hash || !(header.resource_bits & (1u << buffer)))
        throw BindingRefused("original lightmap cluster lacks checked reference/layout association",
                             "original precomputed AO association");
    int texture = -1;
    std::map<int, std::string> samplerRoles;
    const auto textures = CustomMaterial::ReadResourceTable(header.textures),
               samplers = CustomMaterial::ReadResourceTable(header.samplers);
    const auto join = [&](const CustomMaterial::ResourceMap& table, bool samplerTable)
    {
        const auto rows = table.find(unsigned(buffer));
        if (rows == table.end())
            return;
        std::set<unsigned> seen;
        for (const auto& field : rows->second)
        {
            if (field.first >= layout.Resources.size() || !seen.insert(field.first).second ||
                field.second > (samplerTable ? 31u : 63u) ||
                layout.Resources[field.first].Type != (samplerTable ? 8 : 5))
                throw BindingRefused("lightmap SRT resource disagrees with original descriptor",
                                     "original precomputed AO resource");
            if (samplerTable)
            {
                if (!samplerRoles.emplace(int(field.second), layout.Resources[field.first].Name).second)
                    throw BindingRefused("lightmap sampler slot has several identities",
                                         "original precomputed AO sampler");
            }
            else if (field.first == 2)
                texture = int(field.second);
        }
    };
    join(textures, false);
    join(samplers, true);
    if (texture < 0)
        return;
    if (samplerRoles.empty() || binding.TextureParameters.count(texture) ||
        (binding.PrecomputedAOSlot >= 0 && binding.PrecomputedAOSlot != texture))
        throw BindingRefused("original AO texture/sampler association conflicts or is absent",
                             "original precomputed AO resource");
    binding.PrecomputedAOSlot = texture;
    if (facts)
    {
        facts->Buffer = buffer;
        facts->TextureSlot = texture;
        facts->ConstructorAssociationProved = true;
    }
    const auto prior = binding.TextureOverride;
    const int defaultSampler = header.default_sampler;
    binding.TextureOverride =
        [prior, texture, samplerRoles, defaultSampler, facts](const Value& value, const Vec& coords, Value& out)
    {
        const auto& a = Args(value);
        if (a.size() >= 4 && a[2].kind != Value::Null && IsNumber(a[2]) && a[2].number() == 328 + 8 * texture)
        {
            double slot = defaultSampler;
            if (a[3].kind != Value::Null)
            {
                if (!IsNumber(a[3]))
                    throw BindingRefused("precomputed AO sampler is not an immutable handle",
                                         "original precomputed AO sampler");
                slot = (a[3].number() - 1352) / 8;
            }
            if (!std::isfinite(slot) || slot < 0 || slot > 31 || std::floor(slot) != slot ||
                !samplerRoles.count(int(slot)))
                throw BindingRefused("precomputed AO sample does not use an observed original lightmap sampler",
                                     "original precomputed AO sampler");
            if (facts)
            {
                ++facts->Samples;
                facts->ConsumedSamplers[int(slot)] = samplerRoles.at(int(slot));
            }
        }
        return prior && prior(value, coords, out);
    };
}
}
}

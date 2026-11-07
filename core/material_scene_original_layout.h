// Explicit original constructor metadata supplies resource identities, never
// pass execution availability. A layout checksum alone is insufficient.
#pragma once
#include "material_scene_policy.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialSceneNativeResource
{
    std::string Name;
    std::uint32_t Offset = 0;
    std::uint8_t Type = 0;
};
struct MaterialSceneNativeLayout
{
    std::string Name;
    std::uint32_t Size = 0, Hash = 0;
    std::uint8_t StaticSlot = 255;
    std::vector<MaterialSceneNativeResource> Resources;
    bool ConstructorProvenanceProved = false;
};
using MaterialSceneNativeRegistry = std::map<std::uint32_t, MaterialSceneNativeLayout>;
struct MaterialOriginalSceneResourcePolicy
{
    MaterialSceneResourcePolicy ResourcePolicy;
    std::set<int> OriginalLayoutBuffers;
    std::size_t JoinedResources = 0;
};
inline std::uint32_t MaterialSceneNativeLayoutHash(const MaterialSceneNativeLayout& layout)
{
    std::uint32_t hash = (layout.Size << 16) | layout.StaticSlot;
    for (const auto& field : layout.Resources)
        hash ^= field.Offset;
    std::size_t count = layout.Resources.size();
    while (count >= 4)
        for (const unsigned shift : {0u, 8u, 16u, 24u})
            hash ^= std::uint32_t(layout.Resources[--count].Type) << shift;
    while (count >= 2)
        for (const unsigned shift : {0u, 16u})
            hash ^= std::uint32_t(layout.Resources[--count].Type) << shift;
    while (count)
        hash ^= layout.Resources[--count].Type;
    return hash;
}
inline void ValidateMaterialSceneNativeLayout(const MaterialSceneNativeLayout& layout)
{
    const std::map<std::uint32_t, std::pair<std::string, std::size_t>> known = {
        {0x0d480502, {"FOpaqueBasePassUniformParameters", 68}},
        {0x05ec0dee, {"FTranslucentBasePassUniformParameters", 80}},
        {0x097f0609, {"FSceneTextureUniformParameters", 14}}};
    const auto identity = known.find(layout.Hash);
    if (!layout.ConstructorProvenanceProved || identity == known.end() || identity->second.first != layout.Name ||
        identity->second.second != layout.Resources.size())
        throw Refused("original constructor descriptor provenance is unqualified", "material scene original layout");
    const auto size = layout.Hash == 0x0d480502 ? 2368u : layout.Hash == 0x05ec0dee ? 2528u : 112u;
    if (layout.Size != size || layout.StaticSlot != 0 || MaterialSceneNativeLayoutHash(layout) != layout.Hash)
        throw Refused("original constructor size/static-slot/checksum disagrees", "material scene original layout");
    std::uint32_t previous = 0;
    std::set<std::string> names;
    for (std::size_t i = 0; i < layout.Resources.size(); ++i)
    {
        const auto& field = layout.Resources[i];
        if (field.Name.empty() || !names.insert(field.Name).second || field.Offset % 8 ||
            field.Offset + 8 > layout.Size || (i && field.Offset <= previous))
            throw Refused("original descriptor name/offset ordering disagrees", "material scene original descriptor");
        previous = field.Offset;
    }
    const auto require = [&](unsigned ordinal, const std::string& name, unsigned offset, unsigned type)
    {
        const auto& field = layout.Resources.at(ordinal);
        if (field.Name != name || field.Offset != offset || field.Type != type)
            throw Refused("scene role disagrees with the original ordered descriptor",
                          "material scene original descriptor");
    };
    if (layout.Hash == 0x097f0609)
    {
        require(1, "SceneDepthTexture", 8, 9);
        require(10, "CustomDepthTexture", 80, 9);
        require(11, "CustomStencilTexture", 88, 6);
        require(12, "SceneStencilTexture", 96, 6);
        require(13, "PointClampSampler", 104, 8);
    }
    else
    {
        require(36, "SceneTextures.SceneDepthTexture", 2056, 9);
        require(47, "SceneTextures.SceneStencilTexture", 2144, 6);
        require(48, "SceneTextures.PointClampSampler", 2152, 8);
        const auto eye = layout.Hash == 0x0d480502 ? 65u : 77u;
        const auto offset = layout.Hash == 0x0d480502 ? 2336u : 2504u;
        require(eye, "EyeAdaptationTexture", offset, 9);
        require(eye + 1, "SceneColorCopyTexture", offset + 8, 9);
        require(eye + 2, "SceneColorCopySampler", offset + 16, 8);
    }
}
inline MaterialOriginalSceneResourcePolicy
ReadMaterialOriginalSceneResourcePolicy(const FMaterialShaderReference& reference,
                                        const CustomMaterial::ShaderBindings& header,
                                        const MaterialSceneNativeRegistry& registry, const std::string& domain,
                                        const std::string& blend, bool originalAssociationProved)
{
    if (!originalAssociationProved || reference.Frequency != 3)
        throw Refused("original pixel reference association is unqualified", "material scene association");
    auto qualified = reference;
    MaterialOriginalSceneResourcePolicy out;
    const auto textures = CustomMaterial::ReadResourceTable(header.textures),
               srvs = CustomMaterial::ReadResourceTable(header.srv),
               samplers = CustomMaterial::ReadResourceTable(header.samplers),
               uavs = CustomMaterial::ReadResourceTable(header.uav);
    for (std::size_t buffer = 0; buffer < header.layout_hashes.size(); ++buffer)
    {
        const auto captured = registry.find(header.layout_hashes[buffer]);
        if (captured == registry.end())
            continue;
        const auto& layout = captured->second;
        ValidateMaterialSceneNativeLayout(layout);
        if (layout.Hash != captured->first)
            throw Refused("original registry key disagrees", "material scene original layout");
        const std::uint64_t semantic = layout.Hash == 0x0d480502   ? 0xdb2cde9471cc30d3ull
                                       : layout.Hash == 0x05ec0dee ? 0x9f079ec4ea0e07fcull
                                                                   : 0xa1220657911b1226ull;
        const auto named = qualified.UniformBufferHashes.find(std::uint16_t(buffer));
        if (named != qualified.UniformBufferHashes.end() && named->second != semantic)
            throw Refused("original hashed buffer conflicts with constructor resource layout",
                          "material scene layout association");
        qualified.UniformBufferHashes[std::uint16_t(buffer)] = semantic;
        out.OriginalLayoutBuffers.insert(int(buffer));
        const auto join = [&](const CustomMaterial::ResourceMap& table, int category)
        {
            const auto row = table.find(unsigned(buffer));
            if (row == table.end())
                return;
            if (buffer >= 32 || !(header.resource_bits & (1u << buffer)))
                throw Refused("original resource table has no active buffer bit", "material scene original resource");
            std::set<unsigned> ordinals;
            for (const auto& field : row->second)
            {
                if (field.first >= layout.Resources.size() || !ordinals.insert(field.first).second)
                    throw Refused("original resource ordinal is outside its unique descriptor",
                                  "material scene original resource");
                const auto type = layout.Resources[field.first].Type;
                const bool allowed = category == 0   ? (type == 5 || type == 9)
                                     : category == 1 ? (type == 6 || type == 11 || type == 15)
                                     : category == 2 ? type == 8
                                                     : (type == 7 || type == 12 || type == 16);
                if (!allowed || field.second > (category == 2 ? 31u : 63u))
                    throw Refused("original SRT category/slot disagrees with descriptor",
                                  "material scene original resource");
                ++out.JoinedResources;
            }
        };
        join(textures, 0);
        join(srvs, 1);
        join(samplers, 2);
        join(uavs, 3);
    }
    out.ResourcePolicy = ReadMaterialSceneResourcePolicy(qualified, header, domain, blend, true);
    return out;
}
}
}

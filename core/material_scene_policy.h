// Original scene RESOURCE ABI evidence is separate from pass scheduling and
// stock authoring admission. Names require their original hashed UB identity.
#pragma once
#include "material_prepare.h"
#include "material_engine_hashes.h"
#include "material_post_bindings.h"
#include "material_scene.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialSceneResourcePolicy
{
    std::string OriginalDomain, OriginalBlend;
    std::vector<std::string> ResourceAbiNames;
    std::map<std::string, int> Textures, Samplers;
    bool ViewLayoutProved = false, ResourceRolesProved = false;
    bool SchedulingProved = false, StockPreviewProved = false, ProductionAdmitted = false;
};
inline MaterialSceneResourcePolicy ReadMaterialSceneResourcePolicy(const FMaterialShaderReference& ref,
                                                                   const CustomMaterial::ShaderBindings& header,
                                                                   const std::string& domain, const std::string& blend,
                                                                   bool originalAssociationProved)
{
    if (!originalAssociationProved || ref.Frequency != 3)
        throw Refused("scene resource metadata lacks an original pixel association", "material scene association");
    MaterialSceneResourcePolicy out;
    out.OriginalDomain = domain;
    out.OriginalBlend = blend;
    MaterialPostSceneHeaderMetadata metadata;
    metadata.LayoutHashes = header.layout_hashes;
    for (const auto& buffer : ref.UniformBufferHashes)
    {
        const auto name = MaterialKnownBufferNames().find(buffer.second);
        if (name != MaterialKnownBufferNames().end())
            metadata.Buffers[int(buffer.first)] = name->second;
    }
    const std::map<std::uint32_t, std::string> qualified = {{EngineHash::ViewLayout, "View"},
                                                            {0x0d480502, "OpaqueBasePass"},
                                                            {0x05ec0dee, "TranslucentBasePass"},
                                                            {0x097f0609, "SceneTexturesStruct"}};
    for (std::size_t i = 0; i < header.layout_hashes.size(); ++i)
    {
        const auto known = qualified.find(header.layout_hashes[i]);
        if (known == qualified.end())
            continue;
        const auto named = metadata.Buffers.find(int(i));
        if (named == metadata.Buffers.end() || named->second != known->second)
            throw Refused("source-qualified layout disagrees with the original uniform-buffer hash",
                          "material scene layout association");
        if (std::find(out.ResourceAbiNames.begin(), out.ResourceAbiNames.end(), known->second) ==
            out.ResourceAbiNames.end())
            out.ResourceAbiNames.push_back(known->second);
    }
    const auto convert = [](const CustomMaterial::ResourceMap& source)
    {
        std::map<int, std::vector<std::pair<int, int>>> table;
        for (const auto& row : source)
            for (const auto& value : row.second)
                table[int(row.first)].push_back({int(value.first), int(value.second)});
        return table;
    };
    metadata.TextureEntries = convert(CustomMaterial::ReadResourceTable(header.textures));
    metadata.SRVEntries = convert(CustomMaterial::ReadResourceTable(header.srv));
    metadata.SamplerEntries = convert(CustomMaterial::ReadResourceTable(header.samplers));
    for (const auto& table : {metadata.TextureEntries, metadata.SRVEntries, metadata.SamplerEntries})
        for (const auto& row : table)
        {
            std::map<int, int> fields;
            for (const auto& value : row.second)
            {
                const auto prior = fields.find(value.first);
                if (prior != fields.end() && prior->second != value.second)
                    throw Refused("scene member has conflicting original resource bindings",
                                  "material scene resource association");
                fields[value.first] = value.second;
            }
        }
    for (const auto& row : metadata.TextureEntries)
    {
        const auto srv = metadata.SRVEntries.find(row.first);
        if (srv == metadata.SRVEntries.end())
            continue;
        for (const auto& texture : row.second)
            for (const auto& view : srv->second)
                if (texture.first == view.first && texture.second != view.second)
                    throw Refused("scene member has conflicting texture and SRV bindings",
                                  "material scene resource association");
    }
    // Only the declared sampler fields can supply these semantic roles. Other
    // observed SRT members remain in the original header, with no guessed role.
    for (auto& row : metadata.SamplerEntries)
    {
        if (row.first < 0 || std::size_t(row.first) >= header.layout_hashes.size())
            continue;
        std::set<int> fields;
        switch (header.layout_hashes[std::size_t(row.first)])
        {
            case EngineHash::ViewLayout:
                fields = {43};
                break;
            case 0x0d480502:
                fields = {67, 48};
                break;
            case 0x05ec0dee:
                fields = {79, 48};
                break;
            case 0x097f0609:
                fields = {13};
                break;
            default:
                continue;
        }
        row.second.erase(std::remove_if(row.second.begin(), row.second.end(),
                                        [&](const std::pair<int, int>& value) { return !fields.count(value.first); }),
                         row.second.end());
    }
    MaterialPostSupplementalBindings supplemental;
    FReadError error;
    if (!ReadMaterialPostSupplemental(nullptr, metadata, supplemental, error))
        throw Refused(error.Code, "material scene resource association");
    out.Textures = supplemental.EngineTextureRoles;
    out.Samplers = supplemental.SceneSamplers;
    out.ViewLayoutProved = supplemental.ViewLayoutProved;
    for (const auto& texture : out.Textures)
        if (texture.second < 0 || texture.second > 63)
            throw Refused("scene texture is outside the original native cache", "material scene resource association");
    for (const auto& sampler : out.Samplers)
        if (sampler.second < 0 || sampler.second > 31)
            throw Refused("scene sampler is outside the original native cache", "material scene resource association");
    out.ResourceRolesProved = out.ViewLayoutProved && !out.Textures.empty();
    return out;
}
inline SceneReadFactory MaterialSceneFactory(const MaterialSceneResourcePolicy& policy,
                                             const std::function<Value(int)>& textureIdentity,
                                             const std::function<std::string(int)>& samplerIdentity,
                                             const std::string& observedCopySamplerRole)
{
    if (!policy.ResourceRolesProved || !textureIdentity)
        throw Refused("scene formula resources have no checked original identity resolver",
                      "material scene factory identities");
    SceneReadFactory out;
    out.RolesProved = true;
    for (const auto& field : {std::make_pair("scene_depth", "depth"), std::make_pair("scene_color_copy", "color")})
    {
        const auto binding = policy.Textures.find(field.first);
        if (binding == policy.Textures.end())
            continue;
        const auto identity = textureIdentity(binding->second);
        if (identity.kind == Value::Null)
            throw Refused("scene texture identity resolver returned null", "material scene factory identities");
        out.TextureRoles[field.second] = identity;
    }
    if (out.TextureRoles.count("color"))
    {
        const auto sampler = policy.Samplers.find(observedCopySamplerRole);
        if (sampler == policy.Samplers.end() || !samplerIdentity)
            throw Refused("observed scene-copy sampler has no checked binding", "material scene factory sampler");
        out.CopySampler = samplerIdentity(sampler->second);
        if (out.CopySampler.empty())
            throw Refused("scene-copy sampler identity resolver returned an empty identity",
                          "material scene factory sampler");
    }
    return out;
}
}
}

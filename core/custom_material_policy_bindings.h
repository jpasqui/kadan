// Source-qualified original forward resource layouts. These structural facts
// do not qualify an emitted lighting policy or authorize game export.
#pragma once
#include "custom_material_bindings.h"
#include "material_engine_hashes.h"
namespace P4MapKit
{
namespace CustomMaterial
{
namespace Lighting
{
struct PolicyBindings
{
    std::map<std::string, unsigned> textures, samplers, srvs;
    unsigned RequireTexture(const std::string& name) const
    {
        return Require(textures, name, "texture");
    }
    unsigned RequireSampler(const std::string& name) const
    {
        return Require(samplers, name, "sampler");
    }
    unsigned RequireSRV(const std::string& name) const
    {
        return Require(srvs, name, "SRV");
    }

private:
    static unsigned Require(const std::map<std::string, unsigned>& table, const std::string& name, const char* kind)
    {
        auto found = table.find(name);
        if (found == table.end())
            throw std::runtime_error(std::string("forward policy missing ") + kind + ": " + name);
        return found->second;
    }
};
inline void PolicyResourceNames(const ResourceMap& table, unsigned buffer, const std::map<unsigned, std::string>& names,
                                unsigned maximum, std::map<std::string, unsigned>& out)
{
    auto row = table.find(buffer);
    if (row == table.end())
        return;
    std::set<unsigned> ordinals;
    for (const auto& entry : row->second)
    {
        if (entry.second > maximum || !ordinals.insert(entry.first).second)
            throw std::runtime_error("forward resource table range/ordinal conflict");
        auto name = names.find(entry.first);
        if (name != names.end())
        {
            if (!out.emplace(name->second, entry.second).second)
                throw std::runtime_error("forward resource role conflict");
        }
    }
}
inline PolicyBindings ReadForwardPolicyBindings(const ShaderBindings& header)
{
    constexpr std::uint32_t viewLayout = EngineHash::ViewLayout, opaqueLayout = 0x0d480502u;
    if (header.layout_hashes.size() < 3 || header.layout_hashes[0] != viewLayout ||
        header.layout_hashes[2] != opaqueLayout)
        throw std::runtime_error("forward policy View/BasePass layout unqualified");
    if ((header.resource_bits & 5u) != 5u)
        throw std::runtime_error("forward policy View/BasePass resources absent");
    if (!header.global_arrays.empty())
        throw std::runtime_error("forward packed global layout unqualified");
    for (const auto& arrays : header.uniform_buffers)
        if (!arrays.empty())
            throw std::runtime_error("forward packed uniform layout unqualified");
    auto textureTable = ReadResourceTable(header.textures), samplerTable = ReadResourceTable(header.samplers),
         srvTable = ReadResourceTable(header.srv);
    PolicyBindings result;
    PolicyResourceNames(textureTable, 0,
                        {{2, "VolumeIndirection"},
                         {3, "VolumeAmbient"},
                         {4, "VolumeSH0"},
                         {5, "VolumeSH1"},
                         {6, "VolumeSH2"},
                         {7, "VolumeSH3"},
                         {8, "VolumeSH4"},
                         {9, "VolumeSH5"},
                         {10, "VolumeBentNormal"},
                         {11, "VolumeDirectionalShadow"}},
                        63, result.textures);
    PolicyResourceNames(textureTable, 2,
                        {{16, "SkyCubemap"},
                         {18, "SkyBlendCubemap"},
                         {20, "ReflectionCubemap"},
                         {28, "IntegratedFog"},
                         {49, "ForwardShadowMask"},
                         {52, "DBufferA"},
                         {56, "DBufferC"},
                         {58, "DBufferMask"}},
                        63, result.textures);
    PolicyResourceNames(samplerTable, 0, {{43, "SharedBilinearClamped"}}, 31, result.samplers);
    PolicyResourceNames(samplerTable, 2,
                        {{17, "SkyCubemap"}, {19, "SkyBlendCubemap"}, {21, "ReflectionCubemap"}, {53, "DBufferA"}}, 31,
                        result.samplers);
    PolicyResourceNames(srvTable, 0, {{51, "SkyIrradiance"}}, 63, result.srvs);
    PolicyResourceNames(srvTable, 2, {{4, "LocalLights"}, {5, "CulledLightCounts"}, {6, "CulledLightIndices"}}, 63,
                        result.srvs);
    return result;
}
}
}
}

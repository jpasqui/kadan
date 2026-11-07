// Exact Material uniform-buffer shared wrap/clamp sampler tail.
// Virtual texture prefixes need independent layer metadata and stay unclassified.
#pragma once
#include "material_prepare.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialSharedSamplerContract
{
    std::map<int, std::string> Words;
    bool VirtualLayoutUnclassified = false;
};
inline MaterialSharedSamplerContract PrepareMaterialSharedSamplerContract(const FMaterialUniformSet& uniforms,
                                                                          const CustomMaterial::ShaderBindings& header,
                                                                          int materialBuffer)
{
    MaterialSharedSamplerContract result;
    if (materialBuffer < 0)
        return result;
    if (uniforms.VirtualStackCount)
    {
        result.VirtualLayoutUnclassified = true;
        return result;
    }
    for (const auto& texture : uniforms.Textures)
    {
        if (texture.TextureType == 4)
        {
            result.VirtualLayoutUnclassified = true;
            return result;
        }
        if (texture.TextureType > 3)
            throw Refused("unknown Material texture category prevents shared sampler binding",
                          "material shared sampler layout");
    }
    if (uniforms.Textures.size() > 1000000 || uniforms.ExternalTextureCount > 1000000)
        throw Refused("Material sampler resource count exceeds its bound", "material shared sampler layout");
    const auto first = 2u * std::uint64_t(uniforms.Textures.size() + uniforms.ExternalTextureCount);
    CustomMaterial::ResourceMap samplers;
    std::string failure;
    try
    {
        samplers = CustomMaterial::ReadResourceTable(header.samplers);
    }
    catch (const std::exception& error)
    {
        failure = error.what();
    }
    if (!failure.empty())
        throw Refused(failure, "material shared sampler table");
    const auto rows = samplers.find(unsigned(materialBuffer));
    if (rows == samplers.end())
        return result;
    for (const auto& row : rows->second)
    {
        const auto resource = std::uint64_t(row.first), slot = std::uint64_t(row.second);
        if (resource == first || resource == first + 1)
        {
            if (slot > (std::uint64_t(std::numeric_limits<int>::max()) - 1352) / 8)
                throw Refused("Material sampler slot overflows its native word", "material shared sampler identity");
            const auto word = 1352 + 8 * int(slot);
            const std::string source =
                resource == first ? "SSM_Wrap_WorldGroupSettings" : "SSM_Clamp_WorldGroupSettings";
            const auto previous = result.Words.find(word);
            if (previous != result.Words.end() && previous->second != source)
                throw Refused("Material shared sampler has conflicting native bindings",
                              "material shared sampler identity");
            result.Words[word] = source;
        }
        else if (resource >= first + 2)
            throw Refused("Material sampler resource exceeds the checked wrap/clamp tail",
                          "material shared sampler layout");
    }
    return result;
}
inline MaterialSharedSamplerContract PrepareOriginalMaterialSharedSamplers(const MaterialPreparedProgram& prepared)
{
    int material = -1;
    for (const auto& buffer : prepared.BaseBinding.Buffers)
        if (buffer.second == "Material")
        {
            if (material != -1)
                throw Refused("several buffers have Material sampler semantics", "material shared sampler identity");
            material = buffer.first;
        }
    return PrepareMaterialSharedSamplerContract(prepared.BaseBinding.Uniforms, prepared.Header, material);
}
inline void ApplyMaterialSharedSamplerContract(BindingContext& binding, const MaterialSharedSamplerContract& contract)
{
    for (const auto& word : contract.Words)
    {
        const auto known = binding.MaterialSharedSamplers.find(word.first);
        if (known != binding.MaterialSharedSamplers.end() && known->second != word.second)
            throw Refused("Material shared sampler contracts conflict", "material shared sampler identity");
        binding.MaterialSharedSamplers[word.first] = word.second;
    }
}
}
}

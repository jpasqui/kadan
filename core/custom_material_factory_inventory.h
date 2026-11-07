// Ephemeral original shader references joined to this compile's actual replacement payloads.
// Nothing here is a capability stamp or a serialized export receipt.
#pragma once
#include "custom_material_shader_set.h"
#include "custom_material_bindings.h"
#include "material_shader_map.h"
namespace P4MapKit
{
namespace CustomMaterial
{
inline std::string CreatorFactoryStageIssue(const SetMaterial& material,
                                            const std::vector<FMaterialShaderReference>& references,
                                            std::uint64_t factory)
{
    try
    {
        if (!factory || !material.map_shaders || material.map_shaders > 4096 || references.empty() ||
            references.size() > 65536)
            throw std::runtime_error("The material has no compiled stages for this renderer.");
        bool vertex = false, pixel = false;
        std::set<unsigned> replaced;
        for (const auto& shader : material.shaders)
        {
            if (shader.place >= material.map_shaders || !replaced.insert(shader.place).second ||
                shader.payload.empty() || shader.payload.size() > (1u << 20))
                throw std::runtime_error("The compiled material shader inventory is invalid.");
            (void)ReadPixelBindings(shader.payload);
            bool bound = false;
            for (const auto& ref : references)
                if (ref.ResourceIndex == std::int32_t(shader.place))
                {
                    if (ref.Frequency != 3 || !ref.VertexFactoryHash)
                        throw std::runtime_error("A compiled pixel payload has incompatible shader references.");
                    bound = true;
                    if (ref.VertexFactoryHash == factory)
                        pixel = true;
                }
            if (!bound)
                throw std::runtime_error("A compiled pixel payload has no original shader reference.");
        }
        for (const auto& ref : references)
        {
            if (ref.ResourceIndex < 0 || unsigned(ref.ResourceIndex) >= material.map_shaders)
                throw std::runtime_error("The original shader reference is outside this material map.");
            if (ref.VertexFactoryHash != factory)
                continue;
            if (ref.Frequency == 0)
                vertex = true;
            // A retained pixel that consumes Material is not creator coverage.
            // Original depth/shadow stages with no Material role remain unchanged.
            if (ref.Frequency == 3 && !replaced.count(unsigned(ref.ResourceIndex)))
                for (const auto& role : ref.NamedBuffers)
                    if (role.second == "Material")
                        throw std::runtime_error(
                            "This renderer still has an original material pixel stage instead of the creator material.");
        }
        if (!vertex || !pixel)
            throw std::runtime_error("The material has no compiled vertex/pixel pair for this renderer.");
        return {};
    }
    catch (const std::exception& error)
    {
        return error.what();
    }
}
}
}

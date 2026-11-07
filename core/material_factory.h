// Exact named pixel interpolants from local, skin and particle factory contracts.
// Tables establish input interpretation only; they do not prove a whole family.
#pragma once
#include "material_bind.h"
#include "material_engine_hashes.h"
#include "material_graph.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialFactoryInputs
{
    std::string Factory;
    std::map<std::string, Vec> Interpolants;
    bool Particle = false;
};
inline MaterialFactoryInputs MaterialFactoryInputContract(SymbolicArena& arena, const std::string& factory)
{
    MaterialFactoryInputs result;
    result.Factory = factory;
    const auto row = [&](const std::string& kind, int width, const Value& detail = Value::null())
    {
        Vec values = splat(0.0);
        for (int c = 0; c < width; ++c)
            values[c] = source(arena, kind, detail, c);
        return values;
    };
    const auto packed = [&](int index)
    {
        return Vec{{source(arena, "TexCoord", index, 0), source(arena, "TexCoord", index, 1),
                    source(arena, "TexCoord", index + 1, 0), source(arena, "TexCoord", index + 1, 1)}};
    };
    const bool skin = IsMember(factory, {"TGPUSkinVertexFactoryDefault", "TGPUSkinMorphVertexFactoryDefault"});
    if (skin ||
        IsMember(factory, {"FLocalVertexFactory", "FInstancedStaticMeshVertexFactory", "FSplineMeshVertexFactory"}))
    {
        for (int pair = 0; pair < 4; ++pair)
            result.Interpolants["in_texcoord" + std::to_string(pair)] = packed(2 * pair);
        result.Interpolants["in_color0"] = row("VertexColor", 4);
        result.Interpolants["in_texcoord10"] = row("TangentX", 3);
        auto tangent = row("TangentZ", 3);
        tangent[3] = source(arena, "TangentSign");
        result.Interpolants["in_texcoord11"] = tangent;
        result.Interpolants["in_texcoord7"] = {{0., 0., 0., 1.}};
        auto position = row("TranslatedWorldPositionNoOffsets", 3);
        position[3] = 1.;
        result.Interpolants["in_texcoord9"] = position;
        if (skin)
            for (int index = 0; index < 7; ++index)
            {
                const auto name = "in_texcoord" + std::to_string(index);
                result.Interpolants[name] = {{source(arena, "TexCoord", index, 0), source(arena, "TexCoord", index, 1),
                                              source(arena, "Interpolant", name, 2),
                                              source(arena, "Interpolant", name, 3)}};
            }
        return result;
    }
    const auto uv = packed(0), particle = row("ParticleColor", 4);
    auto position = row("ParticleTranslatedPosition", 3);
    position[3] = source(arena, "ParticleRadius");
    result.Particle = true;
    if (factory == "FMeshParticleVertexFactory")
        result.Interpolants = {{"in_texcoord0", uv},
                               {"in_color0", row("VertexColor", 4)},
                               {"in_color1", particle},
                               {"in_particle_position", position}};
    else if (factory == "FParticleSpriteVertexFactory")
        result.Interpolants = {{"in_texcoord3", uv}, {"in_texcoord2", particle}, {"in_particle_position", position}};
    else if (factory == "FGPUSpriteVertexFactory")
        result.Interpolants = {{"in_texcoord0", uv}, {"in_texcoord5", particle}, {"in_particle_position", position}};
    else if (factory == "FParticleBeamTrailVertexFactory")
        result.Interpolants = {{"in_texcoord2", uv}, {"in_texcoord3", particle}};
    else
        throw Refused("a vertex factory whose pixel inputs are not known: " + factory,
                      "material vertex factory inputs");
    return result;
}
inline std::string MaterialFactoryName(std::uint64_t hash)
{
    static const std::map<std::uint64_t, std::string> names = {
        {EngineHash::LocalVertexFactory, "FLocalVertexFactory"},
        {EngineHash::InstancedStaticMeshVertexFactory, "FInstancedStaticMeshVertexFactory"},
        {0x872b1640e244ca06ull, "FSplineMeshVertexFactory"},
        {EngineHash::GpuSkinVertexFactory, "TGPUSkinVertexFactoryDefault"},
        {0xdd651790dd308138ull, "TGPUSkinMorphVertexFactoryDefault"},
        {0x6d728306d56d412full, "FMeshParticleVertexFactory"},
        {0x1adefad780cd16c5ull, "FParticleSpriteVertexFactory"},
        {0x00080c76633abf12ull, "FGPUSpriteVertexFactory"},
        {0x2f7fb09897dda57cull, "FParticleBeamTrailVertexFactory"}};
    const auto found = names.find(hash);
    if (found == names.end())
        throw Refused("vertex factory hash has no exact native input contract", "material vertex factory inputs");
    return found->second;
}
inline void ConfigureMaterialFactoryBinding(SymbolicArena& arena, BindingContext& binding, const std::string& factory)
{
    if (!binding.Interpolants.empty())
        throw Refused("factory interpolants already have a separate binding contract",
                      "material factory binding conflict");
    binding.Interpolants = MaterialFactoryInputContract(arena, factory).Interpolants;
}
}
} // P4MapKit::Material

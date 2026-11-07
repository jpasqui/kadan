// Port of material_folded_engine_varyings.py (prepare): every base-pass pixel alias of a folded
// lighting program is paired with each original vertex program of its policy, no geometry or
// tessellation stage stands between them, and each vertex payload reads as a program. The reviewed
// producers of the game (payload and instruction digests, factory, policy, named buffers) are the
// certificate in material_folded_certificates_test.cpp, not a production table.
#pragma once
#include "material_source_owner.h"
#include "material_source_particle_subuv.h"
#include "material_source_common.h"
namespace P4MapKit
{
namespace Material
{
// Base-pass vertex shader type names by their hashed names (material_shadermap.known_names).
inline const std::map<std::uint64_t, std::string>& OriginalFoldedVertexShaderNames()
{
    static const std::map<std::uint64_t, std::string> names = {
        {0x87e27b98467669d3ull, "TBasePassVSFCachedPointIndirectLightingPolicy"},
        {0xae00056f8a3acc96ull, "TBasePassVSFCachedVolumeIndirectLightingPolicy"},
        {0xe99e2d4408e39e36ull, "TBasePassVSFNoLightMapPolicy"},
        {0x65b5d8cde5c1f164ull, "TBasePassVSFPrecomputedVolumetricLightmapLightingPolicy"},
        {0xba53fb0a20785ffcull, "TBasePassVSFSelfShadowedCachedPointIndirectLightingPolicy"},
        {0xe6b842b7733dcb47ull, "TBasePassVSFSelfShadowedTranslucencyPolicy"},
        {0x1a4855b2c0466f87ull, "TBasePassVSFSelfShadowedVolumetricLightmapPolicy"},
        {0xb260b484393ed837ull, "TBasePassVSTDistanceFieldShadowsAndLightMapPolicyHQ"},
        {0xa2d9e9f9cac9c7adull, "TBasePassVSTLightMapPolicyHQ"},
        {0xd51a1828310596dbull, "TBasePassVSTLightMapPolicyLQ"}};
    return names;
}
// A shader reference's uniform buffers as Python's ShaderRef.uniform_buffers gives them, stable-dumped:
// the explicit name, else the engine name of the hashed identity, else its 16-digit hash.
inline std::string OriginalFoldedStableBuffers(const FMaterialShaderReference& ref)
{
    std::map<std::uint16_t, std::string> names;
    for (const auto& item : ref.UniformBufferHashes)
    {
        const auto known = MaterialKnownBufferNames().find(item.second);
        if (known != MaterialKnownBufferNames().end())
            names[item.first] = known->second;
        else
        {
            std::ostringstream hex;
            hex << std::hex << std::setw(16) << std::setfill('0') << item.second;
            names[item.first] = hex.str();
        }
    }
    for (const auto& item : ref.NamedBuffers)
        names[item.first] = item.second;
    std::string out = "{";
    bool first = true;
    for (const auto& item : names)
    {
        if (!first)
            out += ",";
        first = false;
        out += "\"" + std::to_string(item.first) + "\":" + Json(item.second).Dump();
    }
    return out + "}";
}
struct MaterialFoldedVertexPairing
{
    std::string PixelType, VertexType;
    std::uint32_t VertexIndex = 0;
    std::string Buffers;
};
struct MaterialFoldedEngineVaryings
{
    std::vector<MaterialFoldedVertexPairing> Pairings;
    std::vector<std::string> VertexDigests;
    std::string PixelDigest;
};
// _refs + _read + _roles identity checks for one selected original pixel program.
inline MaterialFoldedEngineVaryings PrepareOriginalFoldedEngineVaryings(const MaterialOriginalOwnerSource& owner,
                                                                        const FMaterialShaderReference& pixel,
                                                                        const MaterialOriginalArchiveAccess& archive)
{
    if (!archive.MapIndices || !archive.Payload || !archive.Frequency)
        throw Refused("engine varying original archive access is incomplete", "folded engine varyings");
    std::vector<FMaterialShaderReference> refs;
    FReadError error;
    if (!ReadMaterialShaderReferences(owner.Map, refs, error))
        throw Refused(error.Code, "folded engine varyings");
    const auto indices = archive.MapIndices(owner.Map.ResourceHash);
    const auto native = [&](const FMaterialShaderReference& ref)
    {
        return ArchiveIndexOf(indices, ref, "engine varying reference is outside its archive map",
                              "folded engine varyings");
    };
    const auto pixelIndex = native(pixel);
    const auto pixelType = MaterialOriginalSourceShaderName(pixel.TypeHash);
    std::vector<const FMaterialShaderReference*> aliases;
    bool typed = false;
    for (const auto& ref : refs)
    {
        const auto name = MaterialOriginalSourceShaderName(ref.TypeHash);
        if (ref.Frequency == 3 && ref.VertexFactoryHash == pixel.VertexFactoryHash && native(ref) == pixelIndex &&
            IsBasePassShaderName(name))
        {
            aliases.push_back(&ref);
            typed = typed || name == pixelType;
        }
    }
    if (aliases.empty() || !typed)
        throw Refused("engine varying has no exact pixel alias/type/factory in this shader map",
                      "folded engine varyings");
    MaterialFoldedEngineVaryings result;
    for (const auto* alias : aliases)
    {
        auto wanted = MaterialOriginalSourceShaderName(alias->TypeHash);
        wanted.replace(0, 11, "TBasePassVS");
        const auto sky = wanted.find("Skylight");
        if (sky != std::string::npos)
            wanted.erase(sky, 8);
        bool matched = false;
        for (const auto& ref : refs)
        {
            if (ref.Frequency != 0 || ref.VertexFactoryHash != alias->VertexFactoryHash)
                continue;
            const auto name = OriginalFoldedVertexShaderNames().find(ref.TypeHash);
            if (name == OriginalFoldedVertexShaderNames().end() || name->second != wanted)
                continue;
            matched = true;
            result.Pairings.push_back({MaterialOriginalSourceShaderName(alias->TypeHash), wanted, native(ref),
                                       OriginalFoldedStableBuffers(ref)});
        }
        if (!matched)
            throw Refused("engine varying lacks an exact paired vertex policy", "folded engine varyings");
    }
    for (const auto& ref : refs)
        if ((ref.Frequency == 1 || ref.Frequency == 2 || ref.Frequency == 4) &&
            ref.VertexFactoryHash == pixel.VertexFactoryHash)
            throw Refused("engine varying has an unproved intermediate geometry/tessellation producer",
                          "folded engine varyings");
    result.PixelDigest = PayloadDigest(archive.Payload(pixelIndex));
    const auto factory = MaterialFactoryName(pixel.VertexFactoryHash);
    for (const auto& pairing : result.Pairings)
    {
        const auto payload = archive.Payload(pairing.VertexIndex);
        const auto digest = PayloadDigest(payload);
        CustomMaterial::ShaderBindings header;
        std::string failure;
        try
        {
            header = ReadOriginalParticleVertexBindings(payload);
        }
        catch (const Refused& e)
        {
            failure = e.what();
        }
        if (!failure.empty())
            throw Refused("engine varying original native payload is malformed", "folded engine varyings");
        MaterialInstructionLines(ReadOriginalMaterialProgramText(payload, header));
        result.VertexDigests.push_back(digest);
    }
    return result;
}
}
} // P4MapKit::Material

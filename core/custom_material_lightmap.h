// Original HQ lightmap resource and decode stage. No full policy admission.
#pragma once
#include "custom_material_policy_emitter.h"
namespace P4MapKit
{
namespace CustomMaterial
{
namespace Lighting
{
constexpr std::uint64_t PrecomputedLightingNameHash = 0x365a69720e7582b9ull;
constexpr std::uint64_t LightmapClusterNameHash = 0x198d8af2b3acf133ull;
struct HQBindings
{
    unsigned parameters, cluster, texture, sampler;
};
inline unsigned ExactUniformSlot(const std::map<std::uint16_t, std::uint64_t>& hashes, std::uint64_t identity)
{
    unsigned found = 32;
    for (const auto& item : hashes)
        if (item.second == identity)
        {
            if (found != 32 || item.first > 14)
                throw std::runtime_error("lightmap uniform identity ambiguous/outside hardware");
            found = item.first;
        }
    if (found == 32)
        throw std::runtime_error("lightmap uniform identity absent");
    return found;
}
inline unsigned ExactResourceOrdinal(const ResourceMap& map, unsigned buffer, unsigned ordinal, unsigned maximum)
{
    auto row = map.find(buffer);
    if (row == map.end())
        throw std::runtime_error("lightmap resource buffer absent");
    unsigned found = maximum + 1;
    for (const auto& entry : row->second)
        if (entry.first == ordinal)
        {
            if (found <= maximum || entry.second > maximum)
                throw std::runtime_error("lightmap resource binding ambiguous/range");
            found = entry.second;
        }
    if (found > maximum)
        throw std::runtime_error("lightmap resource ordinal absent");
    return found;
}
inline HQBindings ReadHQBindings(const ShaderBindings& header, const std::map<std::uint16_t, std::uint64_t>& hashes)
{
    // A translucent base pass buffer (0x05ec0dee) is checked by ReadTranslucentSurfaceBindings instead.
    if (header.layout_hashes.size() < 3 || header.layout_hashes[2] != 0x05ec0deeu)
        ReadForwardPolicyBindings(header);
    auto parameters = ExactUniformSlot(hashes, PrecomputedLightingNameHash),
         cluster = ExactUniformSlot(hashes, LightmapClusterNameHash);
    if (parameters == cluster || parameters >= header.layout_hashes.size() || cluster >= header.layout_hashes.size() ||
        header.layout_hashes[parameters] != 0 || header.layout_hashes[cluster] != 0x0b8d0b8cu ||
        (header.resource_bits & (1u << cluster)) == 0)
        throw std::runtime_error("HQ lightmap typed uniform layout unqualified");
    return {parameters, cluster, ExactResourceOrdinal(ReadResourceTable(header.textures), cluster, 0, 63),
            ExactResourceOrdinal(ReadResourceTable(header.samplers), cluster, 9, 31)};
}
inline Value HQSample(PolicyEmitter& policy, const HQBindings& bindings, Value uv)
{
    if (uv.size != 2)
        throw std::runtime_error("HQ UV requires xy");
    auto& emitter = policy.emitter;
    emitter.uses_texture = true;
    emitter.emit("LDC.U64 D1.x, buf14[" + std::to_string(1352 + 8 * bindings.sampler) + "]");
    emitter.emit("LDC.U64 D0.x, buf14[" + std::to_string(328 + 8 * bindings.texture) + "]");
    emitter.emit("OR.U64 D0.x, D0, D1");
    auto result = emitter.temp();
    emitter.emit("TEX.F " + result + ", " + uv.text + ", handle(D0.x), 2D");
    emitter.handle.clear();
    return {result, 4};
}
inline Value SampleHQ(PolicyEmitter& policy, const HQBindings& bindings, Value lightmapUV, Value normal)
{
    if (lightmapUV.size != 2 || normal.size != 3)
        throw std::runtime_error("HQ stage input shape");
    auto& emitter = policy.emitter;
    auto firstUV = emitter.op("MUL", 2, {lightmapUV, F32({1, .5})}),
         secondUV = emitter.op("ADD", 2, {firstUV, F32({0, .5})});
    auto first = HQSample(policy, bindings, firstUV), second = HQSample(policy, bindings, secondUV);
    auto scale0 = policy.Load(bindings.parameters, 64, 4), scale1 = policy.Load(bindings.parameters, 80, 4),
         add0 = policy.Load(bindings.parameters, 96, 4), add1 = policy.Load(bindings.parameters, 112, 4);
    return HighQualityLightmap(emitter, first, second, scale0, add0, scale1, add1, normal);
}
inline std::string HQStageProgram(const ShaderBindings& header, const std::map<std::uint16_t, std::uint64_t>& hashes,
                                  const ReservedDiagnosticBuffer& input)
{
    ValidateDiagnosticBuffer(header, input);
    auto bindings = ReadHQBindings(header, hashes);
    Emitter emitter;
    PolicyEmitter policy(emitter, header);
    auto uv = policy.Load(input.index, 0, 2), normal = policy.Load(input.index, 16, 3);
    auto result = emitter.append(SampleHQ(policy, bindings, uv, normal), F32({1}));
    emitter.emit("MOV.F32 result_color0, " + result.text);
    return Resolve(policy.Program(), {}, {});
}
}
}
}

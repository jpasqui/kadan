// Original distance-field lightmap shadow resource and coefficient decode.
#pragma once
#include "custom_material_lightmap.h"
namespace P4MapKit
{
namespace CustomMaterial
{
namespace Lighting
{
inline HQBindings ReadBakedShadowBindings(const ShaderBindings& header,
                                          const std::map<std::uint16_t, std::uint64_t>& hashes)
{
    auto bindings = ReadHQBindings(header, hashes);
    bindings.texture = ExactResourceOrdinal(ReadResourceTable(header.textures), bindings.cluster, 3, 63);
    return bindings;
}
inline Value SampleBakedShadow(PolicyEmitter& policy, const HQBindings& bindings, Value shadowUV)
{
    if (shadowUV.size != 2)
        throw std::runtime_error("baked shadow UV requires xy");
    auto inversePenumbra = policy.Load(bindings.parameters, 16, 4), sample = HQSample(policy, bindings, shadowUV),
         staticMask = policy.Load(bindings.parameters, 0, 4);
    return DistanceFieldShadow(policy.emitter, sample, inversePenumbra, staticMask);
}
inline std::string BakedShadowStageProgram(const ShaderBindings& header,
                                           const std::map<std::uint16_t, std::uint64_t>& hashes,
                                           const ReservedDiagnosticBuffer& input)
{
    ValidateDiagnosticBuffer(header, input);
    auto bindings = ReadBakedShadowBindings(header, hashes);
    Emitter emitter;
    PolicyEmitter policy(emitter, header);
    auto shadow = SampleBakedShadow(policy, bindings, policy.Load(input.index, 0, 2));
    emitter.emit("MOV.F32 result_color0, " + shadow.text);
    return Resolve(policy.Program(), {}, {});
}
}
}
}

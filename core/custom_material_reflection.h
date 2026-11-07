// Source-qualified nearest-capture sampling. Other reflection shapes/policies
// require separate original program proof and are not admitted by this helper.
#pragma once
#include "custom_material_policy_emitter.h"
namespace P4MapKit
{
namespace CustomMaterial
{
namespace Lighting
{
constexpr std::uint64_t ReflectionCaptureNameHash = 0xf135e29855d4d0beull;
struct ReflectionCaptureBinding
{
    unsigned buffer;
};
inline ReflectionCaptureBinding
ReadReflectionCaptureBinding(const ShaderBindings& header, const std::map<std::uint16_t, std::uint64_t>& uniformHashes)
{
    ReadForwardPolicyBindings(header);
    unsigned found = 32;
    for (const auto& entry : uniformHashes)
        if (entry.second == ReflectionCaptureNameHash)
        {
            if (found != 32 || entry.first > 31)
                throw std::runtime_error("reflection capture uniform identity ambiguous");
            found = entry.first;
        }
    if (found > 14 || found >= header.layout_hashes.size() || header.layout_hashes[found] != 0)
        throw std::runtime_error("reflection capture no-resource uniform identity unqualified");
    return {found};
}
struct NearestCapture
{
    Value radiance, coverage, averageBrightness, reflectionVector;
};
inline Value ReflectionVector(Emitter& emitter, Value normal, Value view)
{
    auto projection = emitter.op("MUL", 3, {emitter.dot(3, view, normal), normal});
    return emitter.op("MAD", 3, {projection, F32({2}), emitter.neg(view)});
}
inline Value ReflectionPolicySample(PolicyEmitter& policy, Value coordinates, Value mip)
{
    if (coordinates.size != 4 || mip.size != 1)
        throw std::runtime_error("array capture sample shape");
    auto texture = policy.bindings.RequireTexture("ReflectionCubemap"),
         sampler = policy.bindings.RequireSampler("ReflectionCubemap");
    auto& emitter = policy.emitter;
    emitter.uses_texture = true;
    emitter.emit("LDC.U64 D1.x, buf14[" + std::to_string(1352 + 8 * sampler) + "]");
    emitter.emit("LDC.U64 D0.x, buf14[" + std::to_string(328 + 8 * texture) + "]");
    emitter.emit("OR.U64 D0.x, D0, D1");
    auto sample = emitter.temp();
    emitter.emit("TXL.F " + sample + ", " + coordinates.text + ", " + Emitter::src(mip, 1) +
                 ", handle(D0.x), ARRAYCUBE");
    emitter.handle.clear();
    return {sample, 4};
}
inline NearestCapture SampleNearestCapture(PolicyEmitter& policy, const ReflectionCaptureBinding& binding, Value normal,
                                           Value view, Value roughness)
{
    auto& emitter = policy.emitter;
    if (normal.size != 3 || view.size != 3 || roughness.size != 1 || binding.buffer > 14)
        throw std::runtime_error("nearest capture input shape");
    emitter.uses_primitive = true;
    policy.buffers.insert(binding.buffer);
    auto vector = ReflectionVector(emitter, normal, view);
    auto index = emitter.temp();
    emitter.emit("LDC.S32 " + index + ".x, buf1[412]");
    emitter.emit("MUL.S " + index + ".x, " + index + ".x, {16, 0, 0, 0}.x");
    auto properties = emitter.temp();
    emitter.emit("LDC.F32X2 " + properties + ".xy, buf" + std::to_string(binding.buffer) + "[" + index + ".x + 5456]");
    auto coordinates = emitter.append(vector, emitter.swizzle({properties, 2}, "g"));
    auto maximumMip = policy.Load(0, 3624, 1), colorScale = policy.Load(0, 3008, 3);
    auto sample = ReflectionPolicySample(policy, coordinates, ReflectionMip(emitter, roughness, maximumMip));
    auto coverage = emitter.sub(1, F32({1}), emitter.swizzle(sample, "a"));
    auto radiance = emitter.op("MUL", 3, {emitter.swizzle(sample, "rgb"), emitter.swizzle({properties, 2}, "r")});
    radiance = emitter.op("MUL", 3, {radiance, colorScale});
    auto offsetAndAverage = emitter.temp();
    emitter.emit("LDC.F32X4 " + offsetAndAverage + ", buf" + std::to_string(binding.buffer) + "[" + index +
                 ".x + 10912]");
    auto luminance = emitter.dot(3, colorScale, F32({.3, .59, .11}));
    auto average = emitter.op("MUL", 1, {emitter.swizzle({offsetAndAverage, 4}, "a"), luminance});
    return {radiance, coverage, average, vector};
}
inline std::string NearestCaptureStageProgram(const ShaderBindings& header,
                                              const std::map<std::uint16_t, std::uint64_t>& uniformHashes,
                                              const ReservedDiagnosticBuffer& input)
{
    ValidateDiagnosticBuffer(header, input);
    auto binding = ReadReflectionCaptureBinding(header, uniformHashes);
    Emitter emitter;
    PolicyEmitter policy(emitter, header);
    auto normal = policy.Load(input.index, 0, 3), view = policy.Load(input.index, 16, 3),
         roughness = policy.Load(input.index, 32, 1);
    auto capture = SampleNearestCapture(policy, binding, normal, view, roughness);
    auto result = emitter.append(capture.radiance, capture.coverage);
    emitter.emit("MOV.F32 result_color0, " + result.text);
    return Resolve(policy.Program(), {}, {});
}
}
}
}

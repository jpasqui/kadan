// Complete independently emitted reflection stage for eight original forward
// permutation identities. This remains separate from whole-material admission.
#pragma once
#include "custom_material_reflection.h"
#include "custom_material_skylight.h"
#include "custom_material_reflection_response.h"
namespace P4MapKit
{
namespace CustomMaterial
{
namespace Lighting
{
struct ReflectionPolicy
{
    bool sky, indirectBrightness;
};
inline ReflectionPolicy OriginalReflectionPolicy(std::uint64_t typeHash)
{
    switch (typeHash)
    {
        case 0x4507f03a199c6526ull:
            return {false, false}; // NoLightMap
        case 0xa8d53bcb8fca168dull:
            return {true, false}; // NoLightMapSkylight
        case 0x666403914542b619ull:
            return {false, true}; // PrecomputedVolumetric
        case 0x18a82ebde5d3d122ull:
            return {true, true}; // PrecomputedVolumetricSkylight
        case 0x6d90922c199fe04full:
            return {false, true}; // HQ
        case 0xdbbfd1d594df3611ull:
            return {true, true}; // HQSkylight
        case 0x28b73d7ae30b556cull:
            return {false, true}; // DistanceFieldHQ
        case 0x661e2d85a2c9dbe6ull:
            return {true, true}; // DistanceFieldHQSkylight
        default:
            throw std::runtime_error("reflection source policy identity unqualified");
    }
}
struct ReflectionInputs
{
    Value normal, view, roughness, specular, materialAO, screenAO, shadowAO, indirectBrightness;
};
inline Value EmitReflectionPolicy(PolicyEmitter& policy, const ReflectionCaptureBinding& binding,
                                  const ReflectionPolicy& descriptor, ReflectionResponseKind kind,
                                  const ReflectionInputs& input)
{
    auto& emitter = policy.emitter;
    auto capture = SampleNearestCapture(policy, binding, input.normal, input.view, input.roughness);
    SkyContribution sky{capture.radiance, F32({0, 0, 0})};
    if (descriptor.sky)
        sky =
            ApplySkyContribution(policy, capture.reflectionVector, input.roughness, capture.radiance, capture.coverage);
    Value ratio = F32({0});
    if (descriptor.indirectBrightness)
        ratio = emitter.op("DIV", 1,
                           {input.indirectBrightness, emitter.op("MAX", 1, {capture.averageBrightness, F32({.0001})})});
    auto mixing = policy.Load(0, 3648, 3);
    auto radiance = ReflectionMix(emitter, sky.capture, sky.dynamic, capture.coverage, mixing, input.roughness, ratio);
    auto noV = emitter.temp();
    emitter.emit("DP3.F32.SAT " + noV + ".x, " + input.view.text + ", " + input.normal.text);
    auto expNoV = emitter.per_component("EX2", emitter.op("MUL", 1, {Value{noV, 1}, F32({-9.28})}));
    auto weight = policy.Load(0, 2560, 1);
    return kind == ReflectionResponseKind::OriginalOpaque
               ? ReflectionResponse(emitter, radiance, input.specular, input.roughness, expNoV, input.materialAO,
                                    input.screenAO, input.shadowAO, weight)
               : MaskedReflectionResponse(emitter, radiance, input.specular, input.roughness, expNoV, input.materialAO,
                                          input.screenAO, input.shadowAO, weight);
}
inline std::string ReflectionPolicyProgram(const ShaderBindings& header,
                                           const std::map<std::uint16_t, std::uint64_t>& uniformHashes,
                                           std::uint64_t typeHash, const ReservedDiagnosticBuffer& buffer,
                                           ReflectionResponseKind kind)
{
    ValidateDiagnosticBuffer(header, buffer);
    auto descriptor = OriginalReflectionPolicy(typeHash);
    auto binding = ReadReflectionCaptureBinding(header, uniformHashes);
    Emitter emitter;
    PolicyEmitter policy(emitter, header);
    ReflectionInputs input{policy.Load(buffer.index, 0, 3),  policy.Load(buffer.index, 16, 3),
                           policy.Load(buffer.index, 32, 1), policy.Load(buffer.index, 48, 3),
                           policy.Load(buffer.index, 64, 1), policy.Load(buffer.index, 80, 1),
                           policy.Load(buffer.index, 96, 1), policy.Load(buffer.index, 112, 1)};
    auto result = emitter.append(EmitReflectionPolicy(policy, binding, descriptor, kind, input), F32({1}));
    emitter.emit("MOV.F32 result_color0, " + result.text);
    return Resolve(policy.Program(), {}, {});
}
}
}
}

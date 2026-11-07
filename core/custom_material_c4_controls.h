// Original C4 ambient and shared capsule-shadow control boundaries.
#pragma once
#include "custom_material_policy_emitter.h"
namespace P4MapKit
{
namespace CustomMaterial
{
namespace Lighting
{
inline Value EmitC4Ambient(PolicyEmitter& policy, Value originalSpecular, Value metallic, Value materialOcclusion)
{
    if (originalSpecular.size != 3 || metallic.size != 1 || materialOcclusion.size != 1)
        throw std::runtime_error("C4 ambient input shape");
    auto& emitter = policy.emitter;
    auto color = policy.Load(0, 2544, 3);
    color = emitter.op("MAD", 3, {emitter.neg(metallic), color, color});
    auto tinted = emitter.op("MUL", 3, {originalSpecular, color});
    auto occlusion = C4SpecularOcclusion(emitter, emitter.swizzle(originalSpecular, "g"), materialOcclusion);
    return emitter.op("MUL", 3, {tinted, emitter.swizzle(occlusion, "r")});
}
struct C4ShadowControls
{
    Value delta, reflectionOcclusion, localAccumulationScale, directionalScale;
};
inline C4ShadowControls EmitC4ShadowControls(PolicyEmitter& policy, Value forwardSample, Value capsuleMultiplier)
{
    if (forwardSample.size != 4 || capsuleMultiplier.size != 1)
        throw std::runtime_error("C4 shadow control input shape");
    auto& emitter = policy.emitter;
    auto delta =
        emitter.op("MAD", 1, {emitter.swizzle(forwardSample, "b"), capsuleMultiplier, emitter.neg(capsuleMultiplier)});
    auto reflection = emitter.op("MAD", 1, {delta, policy.Load(0, 2584, 1), F32({1})});
    auto base = emitter.op("MAD", 1, {policy.Load(0, 2572, 1), delta, F32({1})});
    auto exponent = emitter.op("MAX", 1, {policy.Load(0, 2580, 1), F32({0})});
    return {delta, reflection, emitter.op("POW", 1, {base, exponent}), base};
}
inline std::string C4ControlsStageProgram(const ShaderBindings& header, const ReservedDiagnosticBuffer& input,
                                          const std::string& output)
{
    ValidateDiagnosticBuffer(header, input);
    Emitter emitter;
    PolicyEmitter policy(emitter, header);
    Value result;
    if (output == "ambient")
        result = EmitC4Ambient(policy, policy.Load(input.index, 0, 3), policy.Load(input.index, 16, 1),
                               policy.Load(input.index, 32, 1));
    else
    {
        auto controls = EmitC4ShadowControls(policy, policy.Load(input.index, 0, 4), policy.Load(input.index, 16, 1));
        if (output == "delta")
            result = controls.delta;
        else if (output == "reflection")
            result = controls.reflectionOcclusion;
        else if (output == "local")
            result = controls.localAccumulationScale;
        else
            throw std::runtime_error("C4 control output unavailable");
    }
    emitter.emit("MOV.F32 result_color0.xyz, " + Emitter::src(result, 3));
    return Resolve(policy.Program(), {}, {});
}
}
}
}

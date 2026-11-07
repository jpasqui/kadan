// Independently emitted original skylight sample/blend branches. The caller
// selects a source-qualified skylight permutation; this is not export admission.
#pragma once
#include "custom_material_policy_emitter.h"
namespace P4MapKit
{
namespace CustomMaterial
{
namespace Lighting
{
struct SkyContribution
{
    Value capture, dynamic;
};
inline Value SamplePolicySky(PolicyEmitter& policy, const std::string& role, Value direction, Value mip)
{
    if (direction.size != 3 || mip.size != 1)
        throw std::runtime_error("skylight sample shape");
    auto texture = policy.bindings.RequireTexture(role), sampler = policy.bindings.RequireSampler(role);
    auto& emitter = policy.emitter;
    emitter.uses_texture = true;
    emitter.emit("LDC.U64 D1.x, buf14[" + std::to_string(1352 + 8 * sampler) + "]");
    emitter.emit("LDC.U64 D0.x, buf14[" + std::to_string(328 + 8 * texture) + "]");
    emitter.emit("OR.U64 D0.x, D0, D1");
    auto coordinates = emitter.append(direction, mip);
    auto sample = emitter.temp();
    emitter.emit("TXL.F " + sample + ".xyz, " + coordinates.text + ", handle(D0.x), CUBE");
    emitter.handle.clear();
    return {sample, 3};
}
inline void SkyCondition(Emitter& emitter, Value condition)
{
    auto reg = emitter.temp();
    emitter.emit("TRUNC.U.CC " + reg + ".x, " + Emitter::src(condition, 1));
    emitter.emit("IF NE.x");
}
inline SkyContribution ApplySkyContribution(PolicyEmitter& policy, Value reflectionVector, Value roughness,
                                            Value capture, Value coverage)
{
    if (reflectionVector.size != 3 || roughness.size != 1 || capture.size != 3 || coverage.size != 1)
        throw std::runtime_error("skylight contribution shape");
    // Both exact descriptors must exist for a skylight-enabled permutation.
    policy.bindings.RequireTexture("SkyCubemap");
    policy.bindings.RequireTexture("SkyBlendCubemap");
    policy.bindings.RequireSampler("SkyCubemap");
    policy.bindings.RequireSampler("SkyBlendCubemap");
    auto& emitter = policy.emitter;
    auto parameters = policy.Load(2, 1280, 4);
    auto result = emitter.temp(), dynamic = emitter.temp();
    emitter.emit("MOV.F32 " + result + ".xyz, " + capture.text);
    emitter.emit("MOV.F32 " + dynamic + ".xyz, {0, 0, 0, 0}");
    SkyCondition(emitter, emitter.op("SGT", 1, {emitter.swizzle(parameters, "g"), F32({0})}));
    auto mip = ReflectionMip(emitter, roughness, emitter.swizzle(parameters, "r")), color = policy.Load(0, 3488, 3);
    auto sampled = SamplePolicySky(policy, "SkyCubemap", reflectionVector, mip);
    auto first = emitter.op("MUL", 3, {sampled, color});
    auto sky = emitter.temp();
    emitter.emit("MOV.F32 " + sky + ".xyz, " + first.text);
    SkyCondition(emitter, emitter.op("SGT", 1, {emitter.swizzle(parameters, "a"), F32({0})}));
    auto second = SamplePolicySky(policy, "SkyBlendCubemap", reflectionVector, mip);
    auto difference = emitter.op("MAD", 3, {second, color, emitter.neg(first)});
    auto blended = emitter.op("MAD", 3, {emitter.swizzle(parameters, "a"), difference, first});
    emitter.emit("MOV.F32 " + sky + ".xyz, " + blended.text);
    emitter.emit("ENDIF");
    SkyCondition(emitter, emitter.op("SLT", 1, {emitter.swizzle(parameters, "b"), F32({1})}));
    auto combined = emitter.op("MAD", 3, {coverage, Value{sky, 3}, capture});
    emitter.emit("MOV.F32 " + result + ".xyz, " + combined.text);
    emitter.emit("ELSE");
    emitter.emit("MOV.F32 " + dynamic + ".xyz, " + sky);
    emitter.emit("ENDIF");
    emitter.emit("ENDIF");
    return {{result, 3}, {dynamic, 3}};
}
inline std::string SkyStageProgram(const ShaderBindings& header, const ReservedDiagnosticBuffer& input,
                                   bool dynamicOutput)
{
    ValidateDiagnosticBuffer(header, input);
    Emitter emitter;
    PolicyEmitter policy(emitter, header);
    auto vector = policy.Load(input.index, 0, 3), roughness = policy.Load(input.index, 16, 1),
         capture = policy.Load(input.index, 32, 3), coverage = policy.Load(input.index, 48, 1);
    auto sky = ApplySkyContribution(policy, vector, roughness, capture, coverage);
    auto result = emitter.append(dynamicOutput ? sky.dynamic : sky.capture, F32({1}));
    emitter.emit("MOV.F32 result_color0, " + result.text);
    return Resolve(policy.Program(), {}, {});
}
}
}
}

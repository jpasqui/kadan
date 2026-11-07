// Original directional screen/baked shadow channel and distance-fade math.
// The caller separately qualifies the shadow samples and scene-depth producer.
#pragma once
#include "custom_material_shadow.h"
namespace P4MapKit
{
namespace CustomMaterial
{
namespace Lighting
{
inline void ShadowIntegerCondition(Emitter& emitter, Value integer)
{
    auto condition = emitter.temp();
    emitter.emit("SNE.U.CC " + condition + ".x, " + integer.text + ".x, {0, 0, 0, 0}.x");
    emitter.emit("IF NE.x");
}
inline Value ShadowChannels(Emitter& emitter, Value flags, unsigned shift)
{
    auto result = emitter.temp();
    for (unsigned channel = 0; channel < 4; ++channel)
    {
        auto bit = emitter.temp();
        emitter.emit("AND.U " + bit + ".x, " + flags.text + ".x, {" + std::to_string(1u << (channel + shift)) +
                     ", 0, 0, 0}.x");
        ShadowIntegerCondition(emitter, {bit, 1});
        emitter.emit("MOV.F32 " + result + "." + Comp(4)[channel] + ", {1, 0, 0, 0}.x");
        emitter.emit("ELSE");
        emitter.emit("MOV.F32 " + result + "." + Comp(4)[channel] + ", {0, 0, 0, 0}.x");
        emitter.emit("ENDIF");
    }
    return {result, 4};
}
struct DirectionalShadow
{
    Value surface, transmission;
};
inline DirectionalShadow EmitDirectionalShadow(PolicyEmitter& policy, Value squaredForward, Value baked,
                                               Value sceneDepth, Value defaultSurface,
                                               Value* reflectionShadow = nullptr)
{
    if (squaredForward.size != 4 || baked.size != 4 || sceneDepth.size != 1 || defaultSurface.size != 1)
        throw std::runtime_error("directional shadow input shape");
    auto& emitter = policy.emitter;
    auto surface = emitter.temp(), transmission = emitter.temp();
    emitter.emit("MOV.F32 " + surface + ".x, " + Emitter::src(defaultSurface, 1));
    emitter.emit("MOV.F32 " + transmission + ".x, {1, 0, 0, 0}.x");
    // The reflection response retains the raw directional surface before the C4
    // strength blend. Its no-directional fallback is independently one.
    std::string rawReflection;
    if (reflectionShadow)
    {
        rawReflection = emitter.temp();
        emitter.emit("MOV.F32 " + rawReflection + ".x, {1, 0, 0, 0}.x");
        *reflectionShadow = {rawReflection, 1};
    }
    auto hasDirectional = ShadowIntegerWord(policy, 2, 8, false);
    ShadowIntegerCondition(emitter, hasDirectional);
    auto directionalFlags = ShadowIntegerWord(policy, 2, 96, false),
         viewFlags = ShadowIntegerWord(policy, 0, 2656, false);
    auto directionalChannels = ShadowChannels(emitter, directionalFlags, 0),
         viewChannels = ShadowChannels(emitter, viewFlags, 0),
         transmissionChannels = ShadowChannels(emitter, directionalFlags, 4);
    auto object = emitter.temp();
    emitter.emit("MOV.F32 " + object + ".x, {1, 0, 0, 0}.x");
    auto anyView = emitter.op("SGE", 1, {emitter.dot(4, viewChannels, F32({1, 1, 1, 1})), F32({1})});
    auto viewCondition = emitter.temp();
    emitter.emit("TRUNC.U.CC " + viewCondition + ".x, " + Emitter::src(anyView, 1));
    emitter.emit("IF NE.x");
    auto objectShadow = emitter.dot(4, squaredForward, viewChannels);
    emitter.emit("MOV.F32 " + object + ".x, " + Emitter::src(objectShadow, 1));
    emitter.emit("ENDIF");
    auto lowBit = emitter.temp();
    emitter.emit("AND.U " + lowBit + ".x, " + directionalFlags.text + ".x, {255, 0, 0, 0}.x");
    ShadowIntegerCondition(emitter, {lowBit, 1});
    auto channelCount = emitter.dot(4, directionalChannels, F32({1, 1, 1, 1})),
         bakedCoverage = emitter.dot(4, baked, directionalChannels);
    auto staticShadow = emitter.op("MAD", 1, {bakedCoverage, channelCount, emitter.neg(channelCount)});
    staticShadow = emitter.op("ADD", 1, {staticShadow, F32({1})});
    auto dynamicShadow = emitter.dot(4, transmissionChannels, squaredForward), fade = policy.Load(2, 104, 2);
    auto amount = emitter.op("MAD", 1, {sceneDepth, emitter.swizzle(fade, "r"), emitter.swizzle(fade, "g")}, true);
    amount = emitter.op("MUL", 1, {amount, amount});
    auto combined = emitter.op("MAD", 1, {amount, emitter.sub(1, staticShadow, dynamicShadow), dynamicShadow});
    auto surfaceShadow = emitter.op("MIN", 1, {combined, Value{object, 1}}),
         transmissionShadow = emitter.op("MIN", 1, {Value{object, 1}, emitter.op("MIN", 1, {combined, F32({1})})});
    emitter.emit("MOV.F32 " + surface + ".x, " + Emitter::src(surfaceShadow, 1));
    emitter.emit("MOV.F32 " + transmission + ".x, " + Emitter::src(transmissionShadow, 1));
    emitter.emit("ENDIF");
    if (reflectionShadow)
        emitter.emit("MOV.F32 " + rawReflection + ".x, " + surface + ".x");
    emitter.emit("ENDIF");
    auto strength = emitter.op("MOV", 1, {policy.Load(0, 2588, 1)}, true);
    auto surfaceAmount = emitter.op("MAD", 1, {Value{surface, 1}, strength, emitter.neg(strength)});
    surfaceAmount = emitter.op("ADD", 1, {surfaceAmount, F32({1})});
    auto transmissionAmount = emitter.op("MAD", 1, {strength, Value{transmission, 1}, emitter.neg(strength)});
    transmissionAmount = emitter.op("ADD", 1, {transmissionAmount, F32({1})});
    return {surfaceAmount, transmissionAmount};
}
inline std::string DirectionalShadowStageProgram(const ShaderBindings& header, const ReservedDiagnosticBuffer& input)
{
    ValidateDiagnosticBuffer(header, input);
    Emitter emitter;
    PolicyEmitter policy(emitter, header);
    auto dynamic = policy.Load(input.index, 0, 4), baked = policy.Load(input.index, 16, 4),
         depth = policy.Load(input.index, 32, 1), defaultSurface = policy.Load(input.index, 48, 1);
    auto weights = EmitDirectionalShadow(policy, dynamic, baked, depth, defaultSurface);
    auto result = emitter.append(emitter.append(weights.surface, weights.transmission), F32({0, 1}));
    emitter.emit("MOV.F32 result_color0, " + result.text);
    return Resolve(policy.Program(), {}, {});
}
inline DirectionalShadow SampleDirectionalShadow(PolicyEmitter& policy, const ForwardShadowResources& resources,
                                                 Value bufferUV, Value baked, Value sceneDepth, Value defaultSurface)
{
    auto sample = SampleForwardShadow(policy, resources, bufferUV);
    auto squared = policy.emitter.op("MUL", 4, {sample, sample});
    return EmitDirectionalShadow(policy, squared, baked, sceneDepth, defaultSurface);
}
inline std::string SampleDirectionalShadowStageProgram(const ShaderBindings& header,
                                                       const ReservedDiagnosticBuffer& input)
{
    ValidateDiagnosticBuffer(header, input);
    Emitter emitter;
    PolicyEmitter policy(emitter, header);
    auto uv = policy.Load(input.index, 0, 2), baked = policy.Load(input.index, 16, 4),
         depth = policy.Load(input.index, 32, 1), defaultSurface = policy.Load(input.index, 48, 1);
    auto weights =
        SampleDirectionalShadow(policy, ReadForwardShadowResources(header), uv, baked, depth, defaultSurface);
    auto result = emitter.append(emitter.append(weights.surface, weights.transmission), F32({0, 1}));
    emitter.emit("MOV.F32 result_color0, " + result.text);
    return Resolve(policy.Program(), {}, {});
}
}
}
}

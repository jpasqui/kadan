// Original opaque final joins and explicit caller roles. These boundaries do
// not authorize full material export or substitute unproved source producers.
#pragma once
#include "custom_material_c4_controls.h"
#include "custom_material_reflection_policy.h"

namespace P4MapKit
{
namespace CustomMaterial
{
namespace Lighting
{
struct OpaqueReflectionInputs
{
    Value normal, view, roughness, specular, actualMaterialAO, reflectionOcclusion, reflectionShadowAO,
        indirectBrightness;
};
struct OpaqueReflectionTerms
{
    Value radianceBeforeWeight, shadowBlend;
};
inline OpaqueReflectionTerms EmitOpaqueReflectionResponseTerms(Emitter& emitter, Value radiance, Value specular,
                                                               Value roughness, Value expNoV, Value actualMaterialAO,
                                                               Value reflectionOcclusion, Value reflectionShadowAO,
                                                               Value shadowWeight)
{
    if (radiance.size != 3 || specular.size != 3 || roughness.size != 1 || expNoV.size != 1 ||
        actualMaterialAO.size != 1 || reflectionOcclusion.size != 1 || reflectionShadowAO.size != 1 ||
        shadowWeight.size != 1)
        throw std::runtime_error("opaque reflection response input shape");
    radiance = emitter.op("MUL", 3, {radiance, EnvironmentBRDFExp(emitter, specular, roughness, expNoV)});
    // The old generic response's materialAO parameter is the C4 factor here;
    // its screenAO parameter is the actual material AmbientOcclusion producer.
    radiance = emitter.op("MUL", 3, {radiance, reflectionOcclusion});
    radiance = emitter.op("MUL", 3, {radiance, C4SpecularOcclusion(emitter, specular, actualMaterialAO)});
    auto shadow = emitter.op("MAD", 1, {emitter.neg(shadowWeight), reflectionShadowAO, reflectionShadowAO});
    shadow = emitter.op("ADD", 1, {shadowWeight, shadow});
    return {radiance, shadow};
}
inline OpaqueReflectionTerms EmitOpaqueReflectionPolicyTerms(PolicyEmitter& policy,
                                                             const ReflectionCaptureBinding& binding,
                                                             const ReflectionPolicy& descriptor,
                                                             const OpaqueReflectionInputs& input)
{
    if (input.normal.size != 3 || input.view.size != 3 || input.roughness.size != 1 || input.specular.size != 3 ||
        input.actualMaterialAO.size != 1 || input.reflectionOcclusion.size != 1 || input.reflectionShadowAO.size != 1 ||
        input.indirectBrightness.size != 1)
        throw std::runtime_error("opaque reflection policy input shape");
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
    auto radiance = ReflectionMix(emitter, sky.capture, sky.dynamic, capture.coverage, policy.Load(0, 3648, 3),
                                  input.roughness, ratio);
    auto noV = emitter.temp();
    emitter.emit("DP3.F32.SAT " + noV + ".x, " + input.view.text + ", " + input.normal.text);
    auto expNoV = emitter.per_component("EX2", emitter.op("MUL", 1, {Value{noV, 1}, F32({-9.28})}));
    return EmitOpaqueReflectionResponseTerms(emitter, radiance, input.specular, input.roughness, expNoV,
                                             input.actualMaterialAO, input.reflectionOcclusion,
                                             input.reflectionShadowAO, policy.Load(0, 2560, 1));
}
inline Value EmitOpaqueAmbient(PolicyEmitter& policy, Value postDecalBaseColor, Value metallic, Value actualMaterialAO)
{
    // The existing ambient helper's historical originalSpecular name does not
    // describe the original caller: both tint and polynomial use BaseColor.
    return EmitC4Ambient(policy, postDecalBaseColor, metallic, actualMaterialAO);
}
struct OpaqueFinalInputs
{
    Value directDiffuse, directSpecular, irradiance, postCaptureDiffuse, ambient;
    OpaqueReflectionTerms reflection;
    Value reflectionOcclusion, emissive;
    bool hasIrradianceProducer, hasEmissiveProducer;
};
inline Value EmitOpaqueLinearComposition(Emitter& emitter, const OpaqueFinalInputs& input)
{
    if (input.directDiffuse.size != 3 || input.directSpecular.size != 3 || input.ambient.size != 3 ||
        input.reflection.radianceBeforeWeight.size != 3 || input.reflection.shadowBlend.size != 1 ||
        input.reflectionOcclusion.size != 1)
        throw std::runtime_error("opaque final composition input shape");
    Value baseline = input.ambient;
    if (input.hasIrradianceProducer)
    {
        if (input.irradiance.size != 3 || input.postCaptureDiffuse.size != 3)
            throw std::runtime_error("opaque indirect composition input shape");
        auto diffuse = emitter.op("MUL", 3, {input.irradiance, input.postCaptureDiffuse});
        baseline = emitter.op("MAD", 3, {input.reflectionOcclusion, diffuse, input.ambient});
    }
    auto direct = emitter.op("ADD", 3, {input.directDiffuse, input.directSpecular});
    auto linear = emitter.op("MAD", 3, {input.reflection.radianceBeforeWeight, input.reflection.shadowBlend, direct});
    linear = emitter.op("ADD", 3, {linear, baseline});
    if (input.hasEmissiveProducer)
    {
        if (input.emissive.size != 3)
            throw std::runtime_error("opaque emissive composition input shape");
        linear = emitter.op("ADD", 3, {linear, emitter.op("MAX", 3, {input.emissive, F32({0})})});
    }
    return linear;
}
inline Value EmitOpaqueFinalColor(PolicyEmitter& policy, const OpaqueFinalInputs& input, Value joinedFog)
{
    if (joinedFog.size != 4)
        throw std::runtime_error("opaque final fog input shape");
    auto linear = EmitOpaqueLinearComposition(policy.emitter, input);
    auto color = FogComposite(policy.emitter, linear, joinedFog, policy.Load(0, 2180, 1));
    return policy.emitter.append(color, F32({0}));
}
}
}
}

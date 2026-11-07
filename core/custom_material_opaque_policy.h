// Independently authored complete opaque forward policy composition.
// Bindings and producer availability are checked against the selected original policy.
#pragma once
#include "custom_material_dbuffer.h"
#include "custom_material_indirect.h"
#include "custom_material_baked_shadow.h"
#include "custom_material_directional.h"
#include "custom_material_c4_controls.h"
#include "custom_material_reflection_policy.h"
#include "custom_material_final_composition.h"
namespace P4MapKit
{
namespace CustomMaterial
{
namespace Lighting
{
enum class OpaqueDirectionalResponse
{
    SurfaceShadow,
    SeparateSpecularShadow
};
inline void ValidateOpaqueResponses(OriginalDecalResponse decal, OpaqueDirectionalResponse directional)
{
    ValidateOriginalDecalResponse(decal);
    if (directional != OpaqueDirectionalResponse::SurfaceShadow &&
        directional != OpaqueDirectionalResponse::SeparateSpecularShadow)
        throw std::runtime_error("opaque directional response unavailable");
}
inline SurfaceLighting EmitOpaqueDirectionalLighting(PolicyEmitter& policy, const DirectionalShadow& shadow,
                                                     const DirectionalInputs& inputs,
                                                     OpaqueDirectionalResponse response)
{
    if (response == OpaqueDirectionalResponse::SurfaceShadow)
        return EmitDirectionalLighting(policy, shadow, inputs);
    if (response != OpaqueDirectionalResponse::SeparateSpecularShadow)
        throw std::runtime_error("opaque directional response unavailable");
    auto& emitter = policy.emitter;
    const auto diffuse = emitter.temp(), specular = emitter.temp();
    emitter.emit("MOV.F32 " + diffuse + ".xyz, {0, 0, 0, 0}");
    emitter.emit("MOV.F32 " + specular + ".xyz, {0, 0, 0, 0}");
    ShadowIntegerCondition(emitter, ShadowIntegerWord(policy, 2, 8, false));
    const auto positive = emitter.op(
                   "SGT", 1,
                   {emitter.op("ADD", 1, {shadow.surface, emitter.op("ADD", 1, {shadow.transmission, F32({-1})})}),
                    F32({-1})}),
               control = policy.Load(0, 2592, 1);
    const auto admitted = emitter.op("MAX", 1, {positive, emitter.op("SGT", 1, {control, F32({0})})});
    IndirectCondition(emitter, admitted);
    const auto body = DirectionalSurface(emitter, inputs.normal, inputs.view, policy.Load(2, 64, 3), inputs.roughness,
                                         inputs.diffuse, inputs.specular, inputs.occlusion, policy.Load(0, 2568, 1));
    const auto color = policy.Load(2, 80, 3);
    const auto specularShadow = emitter.op(
                   "ADD", 1, {control, emitter.op("MAD", 1, {shadow.surface, emitter.neg(control), shadow.surface})}),
               specularColor = emitter.op("MUL", 3, {color, specularShadow}),
               s = emitter.op("MUL", 3, {body.specular, specularColor});
    emitter.emit("MOV.F32 " + specular + ".xyz, " + s.text);
    IndirectCondition(emitter, positive);
    const auto diffuseColor = emitter.op("MUL", 3, {color, shadow.surface}),
               d = emitter.op("MUL", 3, {body.diffuse, diffuseColor});
    emitter.emit("MOV.F32 " + diffuse + ".xyz, " + d.text);
    emitter.emit("ENDIF");
    emitter.emit("ENDIF");
    emitter.emit("ENDIF");
    return {{diffuse, 3}, {specular, 3}};
}
struct OpaquePolicyStages
{
    DecalSurface surface;
    SurfaceReflectance reflectance;
    IndirectResult indirect;
    Value forwardShadow;
    C4ShadowControls controls;
    Value bakedShadow;
    DirectionalShadow directionalShadow;
    Value reflectionShadow;
    SurfaceLighting direct;
};
inline SurfaceLighting EmitOriginalDirectionalAccumulation(PolicyEmitter& policy, const SurfaceLighting& direction,
                                                           const C4ShadowControls& controls,
                                                           ForwardWords primitiveChannels)
{
    if (direction.diffuse.size != 3 || direction.specular.size != 3 || controls.directionalScale.size != 1 ||
        primitiveChannels.size != 1)
        throw std::runtime_error("opaque directional accumulation input shape");
    auto& emitter = policy.emitter;
    const auto diffuse = emitter.temp(), specular = emitter.temp();
    emitter.emit("MOV.F32 " + diffuse + ".xyz, {0, 0, 0, 0}");
    emitter.emit("MOV.F32 " + specular + ".xyz, {0, 0, 0, 0}");
    ShadowIntegerCondition(emitter, ShadowIntegerWord(policy, 2, 8, false));
    const auto flags = ForwardWordLoad(policy, 2, 96, 1),
               channels = ForwardWordOp(emitter, "SHR.U", 1, {flags, ForwardWordConstant(8)}),
               active = ForwardWordOp(emitter, "AND.U", 1, {channels, primitiveChannels});
    ShadowIntegerCondition(emitter, {active.text, 1});
    const auto d = emitter.op("MUL", 3, {controls.directionalScale, direction.diffuse}),
               s = emitter.op("MUL", 3, {controls.directionalScale, direction.specular});
    emitter.emit("MOV.F32 " + diffuse + ".xyz, " + d.text);
    emitter.emit("MOV.F32 " + specular + ".xyz, " + s.text);
    emitter.emit("ENDIF");
    emitter.emit("ENDIF");
    return {{diffuse, 3}, {specular, 3}};
}
inline SurfaceReflectance EmitOriginalOpaqueReflectance(PolicyEmitter& policy, const DecalSurface& surface)
{
    auto& emitter = policy.emitter;
    const auto difference = emitter.op("MAD", 3, {emitter.neg(surface.specular), F32({.0799999982}), surface.base});
    const auto dielectric = emitter.op("MUL", 1, {surface.specular, F32({.0799999982})});
    const auto originalSpecular = emitter.op("MAD", 3, {surface.metallic, difference, dielectric});
    const auto diffuse = emitter.temp(), specular = emitter.temp();
    const auto originalDiffuse = emitter.op("MAD", 3, {surface.metallic, emitter.neg(surface.base), surface.base});
    emitter.emit("MOV.F32 " + diffuse + ".xyz, " + originalDiffuse.text);
    emitter.emit("MOV.F32 " + specular + ".xyz, " + originalSpecular.text);
    const auto capture = emitter.op("SNE", 1, {policy.Load(0, 3436, 1), F32({0})});
    IndirectCondition(emitter, capture);
    const auto absorbed = emitter.op("MAD", 3, {originalSpecular, F32({.449999988}), Value{diffuse, 3}});
    emitter.emit("MOV.F32 " + diffuse + ".xyz, " + absorbed.text);
    emitter.emit("MOV.F32 " + specular + ".xyz, {0, 0, 0, 0}");
    emitter.emit("ENDIF");
    return {{diffuse, 3}, {specular, 3}};
}
inline OpaquePolicyStages
EmitOpaquePolicyStages(PolicyEmitter& policy, IndirectEmitter& indirectEmitter, const ShaderBindings& header,
                       const std::map<std::uint16_t, std::uint64_t>& uniformHashes, std::uint64_t typeHash,
                       OriginalDecalResponse decalResponse, const ForwardGeometryResult& geometry,
                       const DecalSurface& material, Value materialAO,
                       OpaqueDirectionalResponse directionalResponse = OpaqueDirectionalResponse::SurfaceShadow)
{
    ValidateOpaqueResponses(decalResponse, directionalResponse);
    if (&indirectEmitter.policy != &policy || materialAO.size != 1)
        throw std::runtime_error("opaque policy context/input shape");
    const auto descriptor = OriginalIndirectPolicy(typeHash);
    OriginalReflectionPolicy(typeHash);
    if (descriptor.kind == IndirectKind::HighQuality && !geometry.hasLightmapUV)
        throw std::runtime_error("opaque baked policy lightmap varying absent");
    auto& emitter = policy.emitter;
    OpaquePolicyStages stages;
    // Pixel coordinates for the integer DBuffer fetch are absolute, unlike the
    // rect-relative cluster grid coordinates retained in geometry.pixelXY.
    auto absolutePixel = emitter.op("MOV", 2, {{"fragment.position", 4}});
    stages.surface = EmitDBufferSurface(policy, ReadDBufferBindings(header, uniformHashes, decalResponse), material,
                                        absolutePixel, geometry.bufferUV);
    stages.reflectance = EmitOriginalOpaqueReflectance(policy, stages.surface);
    stages.indirect = EmitIndirectPolicy(
        indirectEmitter, header, uniformHashes, descriptor,
        {geometry.worldPosition, stages.surface.normal, geometry.hasLightmapUV ? geometry.lightmapUV : F32({0, 0})},
        IndirectResponse::OriginalSingle);
    stages.forwardShadow = SampleForwardShadow(policy, ReadForwardShadowResources(header), geometry.bufferUV);
    stages.controls = EmitC4ShadowControls(policy, stages.forwardShadow, geometry.capsuleMultiplier);
    if (typeHash == 0x28b73d7ae30b556cull || typeHash == 0x661e2d85a2c9dbe6ull)
    {
        if (!geometry.hasLightmapUV)
            throw std::runtime_error("opaque distance field shadow varying absent");
        stages.bakedShadow =
            SampleBakedShadow(policy, ReadBakedShadowBindings(header, uniformHashes), geometry.shadowUV);
    }
    else if (descriptor.kind == IndirectKind::HighQuality)
        stages.bakedShadow = F32({0, 0, 0, 0});
    else
    {
        if (!stages.indirect.hasDirectionalShadowProducer)
            throw std::runtime_error("opaque stationary shadow producer absent");
        stages.bakedShadow = emitter.append(stages.indirect.directionalShadow, F32({1, 1, 1}));
    }
    const auto squared = emitter.op("MUL", 4, {stages.forwardShadow, stages.forwardShadow});
    stages.directionalShadow = EmitDirectionalShadow(policy, squared, stages.bakedShadow, geometry.sceneDepth,
                                                     materialAO, &stages.reflectionShadow);
    const DirectionalInputs directional{stages.surface.normal,       geometry.view,
                                        stages.surface.roughness,    stages.reflectance.diffuse,
                                        stages.reflectance.specular, materialAO};
    const auto direction =
        EmitOpaqueDirectionalLighting(policy, stages.directionalShadow, directional, directionalResponse);
    const auto initial =
        EmitOriginalDirectionalAccumulation(policy, direction, stages.controls, geometry.primitiveLightingChannels);
    const auto blend = emitter.op("MOV", 1, {policy.Load(0, 2588, 1)}, true);
    const ClusteredLocalInputs local{geometry.worldPosition,
                                     geometry.pixelXY,
                                     geometry.sceneDepth,
                                     stages.bakedShadow,
                                     squared,
                                     blend,
                                     materialAO,
                                     stages.controls.localAccumulationScale,
                                     geometry.primitiveLightingChannels};
    const SurfaceInputs surface{
        stages.surface.normal,       geometry.view, stages.surface.roughness, stages.reflectance.diffuse,
        stages.reflectance.specular, materialAO,    policy.Load(0, 2568, 1)};
    stages.direct = EmitClusteredLocalLighting(policy, ReadForwardLocalResources(header), local, surface, nullptr,
                                               &initial, &materialAO);
    return stages;
}
struct OpaqueMaterialStages
{
    Emitter emitter;
    std::set<unsigned> buffers, storage;
    ForwardGeometryResult geometry;
    OpaquePolicyStages stages;
    Value materialAO, emissive;
    bool hasEmissiveProducer = false;
    std::vector<std::string> nodes;
};
inline OpaqueMaterialStages BuildOpaqueMaterialStages(
    const Json& graph, const ShaderBindings& header, const std::map<std::uint16_t, std::uint64_t>& uniformHashes,
    std::uint64_t typeHash, OriginalDecalResponse decalResponse,
    const std::map<std::string, std::string>& textureParameters = {}, const Json& functions = Json(),
    OpaqueDirectionalResponse directionalResponse = OpaqueDirectionalResponse::SurfaceShadow,
    Emitter initial = Emitter())
{
    ValidateOpaqueResponses(decalResponse, directionalResponse);
    // A masked graph only with an emitter seeded for one (Kadan's own programs, which carry their own mask passes).
    if (Get(graph, "clip").kind != Json::Null && !initial.masked_material)
        throw std::runtime_error("opaque creator policy requires an unmasked graph");
    PolicyEmitter geometryPolicy(initial, header);
    const auto geometry = EmitOriginalForwardGeometry(geometryPolicy, header, uniformHashes);
    InstallOriginalForwardGeometryCache(initial, geometry);
    auto material = CompileMaterialInputsWithEmitter(graph, std::move(initial), textureParameters, functions);
    PolicyEmitter policy(material.emitter, header);
    policy.buffers = geometryPolicy.buffers;
    IndirectEmitter indirect(policy);
    const DecalSurface surface{material.pins.at("BaseColor"), material.pins.at("Metallic"),
                               material.pins.at("Specular"), material.pins.at("Roughness"), material.pins.at("Normal")};
    auto stages = EmitOpaquePolicyStages(policy, indirect, header, uniformHashes, typeHash, decalResponse, geometry,
                                         surface, material.pins.at("AmbientOcclusion"), directionalResponse);
    OpaqueMaterialStages result;
    result.geometry = geometry;
    result.stages = stages;
    result.materialAO = material.pins.at("AmbientOcclusion");
    result.emissive = material.pins.at("EmissiveColor");
    result.hasEmissiveProducer = Get(Get(graph, "outputs"), "EmissiveColor").kind != Json::Null;
    result.buffers = policy.buffers;
    result.storage = indirect.storage;
    result.nodes = material.nodes;
    result.emitter = std::move(material.emitter);
    return result;
}
inline std::map<std::string, int> OpaquePolicyVaryings(const ShaderBindings& header)
{
    std::map<std::string, int> varyings;
    for (const auto& entry : header.inputs)
    {
        const auto name = entry.name.substr(0, entry.name.find("_centroid"));
        auto prior = varyings.find(name);
        if (prior != varyings.end() && prior->second != entry.location)
            throw std::runtime_error("opaque policy varying identity conflict");
        varyings[name] = entry.location;
    }
    return varyings;
}
inline std::string
OpaqueMaterialStageProgram(const Json& graph, const ShaderBindings& header,
                           const std::map<std::uint16_t, std::uint64_t>& uniformHashes, std::uint64_t typeHash,
                           OriginalDecalResponse decalResponse, const std::string& output, bool reuse = true,
                           OpaqueDirectionalResponse directionalResponse = OpaqueDirectionalResponse::SurfaceShadow)
{
    auto made = BuildOpaqueMaterialStages(graph, header, uniformHashes, typeHash, decalResponse, {}, Json(),
                                          directionalResponse);
    PolicyEmitter policy(made.emitter, header);
    policy.buffers = made.buffers;
    IndirectEmitter indirect(policy);
    indirect.storage = made.storage;
    Value result;
    if (output == "direct-diffuse")
        result = made.stages.direct.diffuse;
    else if (output == "direct-specular")
        result = made.stages.direct.specular;
    else if (output == "irradiance")
        result = made.stages.indirect.irradiance;
    else if (output == "baked-shadow")
        result = made.stages.bakedShadow;
    else if (output == "base")
        result = made.stages.surface.base;
    else
        throw std::runtime_error("opaque material stage output unavailable");
    made.emitter.emit("MOV.F32 result_color0." + Comp(result.size) + ", " + Emitter::src(result, result.size));
    auto program = Resolve(indirect.Program(), {}, OpaquePolicyVaryings(header));
    return reuse ? ForwardReuseTemporaries(program) : program;
}
inline std::string OpaqueMaterialProgram(
    const Json& graph, const ShaderBindings& header, const std::map<std::uint16_t, std::uint64_t>& uniformHashes,
    std::uint64_t typeHash, OriginalDecalResponse decalResponse,
    const std::map<std::string, std::string>& textureParameters = {},
    const std::map<std::string, std::pair<int, int>>& textureSlots = {}, const Json& functions = Json(),
    bool reuse = true, OpaqueDirectionalResponse directionalResponse = OpaqueDirectionalResponse::SurfaceShadow,
    Emitter initial = Emitter())
{
    auto made = BuildOpaqueMaterialStages(graph, header, uniformHashes, typeHash, decalResponse, textureParameters,
                                          functions, directionalResponse, std::move(initial));
    PolicyEmitter policy(made.emitter, header);
    policy.buffers = made.buffers;
    IndirectEmitter indirect(policy);
    indirect.storage = made.storage;
    const auto reflectionPolicy = OriginalReflectionPolicy(typeHash);
    if (reflectionPolicy.indirectBrightness && !made.stages.indirect.hasBrightnessProducer)
        throw std::runtime_error("opaque reflection brightness producer absent");
    const OpaqueReflectionInputs reflectionInputs{made.stages.surface.normal,
                                                  made.geometry.view,
                                                  made.stages.surface.roughness,
                                                  made.stages.reflectance.specular,
                                                  made.materialAO,
                                                  made.stages.controls.reflectionOcclusion,
                                                  made.stages.reflectionShadow,
                                                  made.stages.indirect.brightness};
    const auto reflection = EmitOpaqueReflectionPolicyTerms(policy, ReadReflectionCaptureBinding(header, uniformHashes),
                                                            reflectionPolicy, reflectionInputs);
    const auto ambient =
        EmitOpaqueAmbient(policy, made.stages.surface.base, made.stages.surface.metallic, made.materialAO);
    const OpaqueFinalInputs finalInputs{made.stages.direct.diffuse,
                                        made.stages.direct.specular,
                                        made.stages.indirect.irradiance,
                                        made.stages.reflectance.diffuse,
                                        ambient,
                                        reflection,
                                        made.stages.controls.reflectionOcclusion,
                                        made.emissive,
                                        made.stages.indirect.hasIrradianceProducer,
                                        made.hasEmissiveProducer};
    const auto fog = policy.ApplyVolumetricFog(made.geometry.worldPosition, made.geometry.vertexFog),
               color = EmitOpaqueFinalColor(policy, finalInputs, fog);
    made.emitter.emit("MOV.F32 result_color0, " + color.text);
    auto program = Resolve(indirect.Program(), textureSlots, OpaquePolicyVaryings(header));
    return reuse ? ForwardReuseTemporaries(program) : program;
}
}
}
}

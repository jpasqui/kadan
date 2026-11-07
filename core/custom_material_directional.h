// Original opaque directional lighting, including typed forward shadow sample.
#pragma once
#include "custom_material_directional_shadow.h"
namespace P4MapKit
{
namespace CustomMaterial
{
namespace Lighting
{
struct DirectionalInputs
{
    Value normal, view, roughness, diffuse, specular, occlusion;
};
inline SurfaceLighting EmitDirectionalLighting(PolicyEmitter& policy, const DirectionalShadow& shadow,
                                               const DirectionalInputs& inputs)
{
    auto& emitter = policy.emitter;
    auto diffuse = emitter.temp(), specular = emitter.temp();
    emitter.emit("MOV.F32 " + diffuse + ".xyz, {0, 0, 0, 0}.x");
    emitter.emit("MOV.F32 " + specular + ".xyz, {0, 0, 0, 0}.x");
    auto hasDirectional = ShadowIntegerWord(policy, 2, 8, false);
    ShadowIntegerCondition(emitter, hasDirectional);
    auto shadowSum = emitter.op("ADD", 1, {shadow.surface, emitter.op("ADD", 1, {shadow.transmission, F32({-1})})});
    auto positive = emitter.op("SGT", 1, {shadowSum, F32({-1})});
    auto condition = emitter.temp();
    emitter.emit("TRUNC.U.CC " + condition + ".x, " + Emitter::src(positive, 1));
    emitter.emit("IF NE.x");
    auto light = policy.Load(2, 64, 3), cap = policy.Load(0, 2568, 1);
    auto surface = DirectionalSurface(emitter, inputs.normal, inputs.view, light, inputs.roughness, inputs.diffuse,
                                      inputs.specular, inputs.occlusion, cap);
    auto color = emitter.op("MUL", 3, {policy.Load(2, 80, 3), shadow.surface});
    auto diffuseLight = emitter.op("MUL", 3, {surface.diffuse, color}),
         specularLight = emitter.op("MUL", 3, {surface.specular, color});
    emitter.emit("MOV.F32 " + diffuse + ".xyz, " + diffuseLight.text);
    emitter.emit("MOV.F32 " + specular + ".xyz, " + specularLight.text);
    emitter.emit("ENDIF");
    emitter.emit("ENDIF");
    return {{diffuse, 3}, {specular, 3}};
}
inline std::string DirectionalLightingStageProgram(const ShaderBindings& header, const ReservedDiagnosticBuffer& input,
                                                   bool specularOutput)
{
    ValidateDiagnosticBuffer(header, input);
    Emitter emitter;
    PolicyEmitter policy(emitter, header);
    auto uv = policy.Load(input.index, 0, 2), baked = policy.Load(input.index, 16, 4),
         depth = policy.Load(input.index, 32, 1), defaultSurface = policy.Load(input.index, 48, 1);
    auto shadow = SampleDirectionalShadow(policy, ReadForwardShadowResources(header), uv, baked, depth, defaultSurface);
    DirectionalInputs inputs{policy.Load(input.index, 64, 3),  policy.Load(input.index, 80, 3),
                             policy.Load(input.index, 96, 1),  policy.Load(input.index, 112, 3),
                             policy.Load(input.index, 128, 3), policy.Load(input.index, 144, 1)};
    auto result = EmitDirectionalLighting(policy, shadow, inputs);
    auto selected = specularOutput ? result.specular : result.diffuse;
    emitter.emit("MOV.F32 result_color0.xyz, " + selected.text);
    return Resolve(policy.Program(), {}, {});
}
}
}
}

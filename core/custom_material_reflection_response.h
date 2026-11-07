// Source-qualified masked/opaque reflection response distinctions. Policy
// selection requires independent original permutation proof at the caller.
#pragma once
#include "custom_material_policy_emitter.h"
namespace P4MapKit
{
namespace CustomMaterial
{
namespace Lighting
{
enum class ReflectionResponseKind
{
    OriginalOpaque,
    OriginalMasked
};
inline Value MaskedReflectionResponse(Emitter& emitter, Value radiance, Value specular, Value roughness, Value expNoV,
                                      Value materialAO, Value screenAO, Value shadowAO, Value weight)
{
    auto brdf = EnvironmentBRDFExp(emitter, specular, roughness, expNoV);
    auto reflected = emitter.op("MUL", 3, {radiance, brdf});
    reflected = emitter.op("MUL", 3, {reflected, materialAO});
    auto weighted = emitter.op("MAD", 1, {emitter.neg(weight), shadowAO, shadowAO});
    weighted = emitter.op("ADD", 1, {weight, weighted});
    reflected = emitter.op("MUL", 3, {reflected, screenAO});
    return emitter.op("MUL", 3, {reflected, weighted});
}
inline std::string ReflectionResponseStageProgram(const ShaderBindings& header, const ReservedDiagnosticBuffer& input,
                                                  ReflectionResponseKind kind)
{
    ValidateDiagnosticBuffer(header, input);
    Emitter emitter;
    PolicyEmitter policy(emitter, header);
    auto radiance = policy.Load(input.index, 0, 3), specular = policy.Load(input.index, 16, 3),
         roughness = policy.Load(input.index, 32, 1), expNoV = policy.Load(input.index, 48, 1),
         materialAO = policy.Load(input.index, 64, 1), screenAO = policy.Load(input.index, 80, 1),
         shadowAO = policy.Load(input.index, 96, 1);
    auto weight = policy.Load(0, 2560, 1);
    auto response =
        kind == ReflectionResponseKind::OriginalOpaque
            ? ReflectionResponse(emitter, radiance, specular, roughness, expNoV, materialAO, screenAO, shadowAO, weight)
            : MaskedReflectionResponse(emitter, radiance, specular, roughness, expNoV, materialAO, screenAO, shadowAO,
                                       weight);
    auto result = emitter.append(response, F32({1}));
    emitter.emit("MOV.F32 result_color0, " + result.text);
    return Resolve(policy.Program(), {}, {});
}
}
}
}

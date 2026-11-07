// Exact original forward screen-space shadow fetch. Remaining directional,
// baked and material channel composition is a separate policy stage.
#pragma once
#include "custom_material_policy_emitter.h"
namespace P4MapKit
{
namespace CustomMaterial
{
namespace Lighting
{
struct ForwardShadowResources
{
    unsigned texture, sampler;
};
inline ForwardShadowResources ReadForwardShadowResources(const ShaderBindings& header)
{
    auto bindings = ReadForwardPolicyBindings(header);
    if (header.default_sampler < 0 || header.default_sampler > 31)
        throw std::runtime_error("forward shadow default sampler range");
    return {bindings.RequireTexture("ForwardShadowMask"), unsigned(header.default_sampler)};
}
inline Value ShadowIntegerWord(PolicyEmitter& policy, unsigned buffer, unsigned offset, bool signedWord)
{
    if (buffer > 14 || offset % 4)
        throw std::runtime_error("shadow integer load range");
    policy.buffers.insert(buffer);
    auto result = policy.emitter.temp();
    policy.emitter.emit(std::string("LDC.") + (signedWord ? "S32 " : "U32 ") + result + ".x, buf" +
                        std::to_string(buffer) + "[" + std::to_string(offset) + "]");
    return {result, 1};
}
inline Value SampleForwardShadow(PolicyEmitter& policy, const ForwardShadowResources& resources, Value bufferUV)
{
    if (bufferUV.size != 2)
        throw std::runtime_error("forward shadow buffer UV shape");
    auto& emitter = policy.emitter;
    auto size = policy.Load(0, 2128, 2), coordinates = emitter.op("MUL", 2, {bufferUV, size});
    coordinates = emitter.op("TRUNC", 2, {coordinates});
    auto useMask = ShadowIntegerWord(policy, 2, 2160, true);
    auto integer = emitter.temp();
    emitter.emit("TRUNC.S " + integer + ".xy, " + coordinates.text);
    emitter.emit("MUL.S " + integer + ".xy, " + integer + ", " + useMask.text + ".x");
    emitter.uses_texture = true;
    emitter.emit("LDC.U64 D1.x, buf14[" + std::to_string(1352 + 8 * resources.sampler) + "]");
    emitter.emit("LDC.U64 D0.x, buf14[" + std::to_string(328 + 8 * resources.texture) + "]");
    emitter.emit("OR.U64 D0.x, D0, D1");
    emitter.emit("MOV.S " + integer + ".z, {0, 0, 0, 0}.x");
    auto result = emitter.temp();
    emitter.emit("TXF.F " + result + ", " + integer + ".xyzz, handle(D0.x), 2D");
    emitter.handle.clear();
    return {result, 4};
}
inline std::string ForwardShadowStageProgram(const ShaderBindings& header, const ReservedDiagnosticBuffer& input)
{
    ValidateDiagnosticBuffer(header, input);
    auto resources = ReadForwardShadowResources(header);
    Emitter emitter;
    PolicyEmitter policy(emitter, header);
    auto uv = policy.Load(input.index, 0, 2), shadow = SampleForwardShadow(policy, resources, uv);
    emitter.emit("MOV.F32 result_color0, " + shadow.text);
    return Resolve(policy.Program(), {}, {});
}
}
}
}

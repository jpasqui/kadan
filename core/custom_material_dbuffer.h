// Original opaque decal surface stage. Resource ABI and response are explicit;
// this boundary alone does not admit a complete creator material in the game.
#pragma once
#include "material_forward_geometry.h"
namespace P4MapKit
{
namespace CustomMaterial
{
namespace Lighting
{
enum class OriginalDecalResponse
{
    Color,
    ColorRoughness
};
inline void ValidateOriginalDecalResponse(OriginalDecalResponse response)
{
    if (response != OriginalDecalResponse::Color && response != OriginalDecalResponse::ColorRoughness)
        throw std::runtime_error("opaque decal response unavailable");
}
struct DecalSurface
{
    Value base, metallic, specular, roughness, normal;
};
struct DBufferBindings
{
    unsigned mask, color, roughness, sampler, maskSampler;
    bool hasRoughness;
};
inline DBufferBindings ReadDBufferBindings(const ShaderBindings& header,
                                           const std::map<std::uint16_t, std::uint64_t>& hashes,
                                           OriginalDecalResponse response)
{
    ValidateOriginalDecalResponse(response);
    RequireOriginalForwardGeometryBindings(header, hashes);
    auto bindings = ReadForwardPolicyBindings(header);
    if (header.default_sampler < 0 || header.default_sampler > 31)
        throw std::runtime_error("DBuffer default sampler range");
    const bool rough = response == OriginalDecalResponse::ColorRoughness;
    return {bindings.RequireTexture("DBufferMask"),
            bindings.RequireTexture("DBufferA"),
            rough ? bindings.RequireTexture("DBufferC") : 0,
            bindings.RequireSampler("DBufferA"),
            unsigned(header.default_sampler),
            rough};
}
inline Value DBufferSample(PolicyEmitter& policy, unsigned texture, unsigned sampler, Value uv, bool integer)
{
    if (uv.size != 2 || texture > 63 || sampler > 31)
        throw std::runtime_error("DBuffer sample input shape/range");
    auto& emitter = policy.emitter;
    emitter.uses_texture = true;
    emitter.emit("LDC.U64 D1.x, buf14[" + std::to_string(1352 + 8 * sampler) + "]");
    emitter.emit("LDC.U64 D0.x, buf14[" + std::to_string(328 + 8 * texture) + "]");
    emitter.emit("OR.U64 D0.x, D0, D1");
    const auto coordinate = emitter.temp(), sample = emitter.temp();
    emitter.emit(std::string(integer ? "TRUNC.S " : "MOV.F32 ") + coordinate + ".xy, " + uv.text);
    emitter.emit(std::string(integer ? "MOV.S " : "MOV.F32 ") + coordinate + ".zw, {0, 0, 0, 0}");
    emitter.emit(std::string(integer ? "TXF.F " : "TXL.F ") + sample + ", " + coordinate + ", handle(D0.x), 2D");
    emitter.handle.clear();
    return {sample, 4};
}
inline void DBufferIf(Emitter& emitter, const std::string& operation, Value value, unsigned word)
{
    const auto condition = emitter.temp();
    emitter.emit(operation + ".U.CC " + condition + ".x, " + Emitter::src(value, 1) + ", {" + std::to_string(word) +
                 ", 0, 0, 0}.x");
    emitter.emit("IF NE.x");
}
inline DecalSurface EmitDBufferSurface(PolicyEmitter& policy, const DBufferBindings& bindings,
                                       const DecalSurface& input, Value pixelXY, Value bufferUV)
{
    if (input.base.size != 3 || input.metallic.size != 1 || input.specular.size != 1 || input.roughness.size != 1 ||
        input.normal.size != 3 || pixelXY.size != 2 || bufferUV.size != 2)
        throw std::runtime_error("DBuffer surface input shape");
    auto& emitter = policy.emitter;
    DecalSurface result;
    auto copy = [&](Value value)
    {
        const auto reg = emitter.temp();
        emitter.emit("MOV.F32 " + reg + "." + Comp(value.size) + ", " + Emitter::src(value, value.size));
        return Value{reg, value.size};
    };
    result = {copy(input.base), copy(input.metallic), copy(input.specular), copy(input.roughness), copy(input.normal)};
    const auto show = emitter.op("SGT", 1, {policy.Load(0, 3628, 1), F32({0})}),
               receive = emitter.op("SGT", 1, {policy.Load(1, 320, 1), F32({0})});
    const auto showWord = emitter.temp(), receiveWord = emitter.temp(), gate = emitter.temp();
    emitter.emit("TRUNC.U " + showWord + ".x, " + Emitter::src(show, 1));
    emitter.emit("TRUNC.U " + receiveWord + ".x, " + Emitter::src(receive, 1));
    emitter.emit("AND.U.CC " + gate + ".x, " + showWord + ".x, " + receiveWord + ".x");
    emitter.emit("IF NE.x");
    const auto mask = DBufferSample(policy, bindings.mask, bindings.maskSampler, pixelXY, true);
    auto packed = emitter.op("MUL", 1, {emitter.swizzle(mask, "a"), F32({4080})});
    packed = emitter.op("MAD", 1, {emitter.swizzle(mask, "r"), F32({255}), packed});
    const auto bits = emitter.temp();
    emitter.emit("TRUNC.U " + bits + ".x, " + Emitter::src(packed, 1));
    DBufferIf(emitter, "SNE", {bits, 1}, 0);
    const auto color = emitter.temp(), rough = emitter.temp();
    emitter.emit("MOV.F32 " + color + ", {0, 0, 0, 1}");
    emitter.emit("MOV.F32 " + rough + ", {0, 0, 0, 1}");
    const auto colorBit = emitter.temp();
    emitter.emit("AND.U " + colorBit + ".x, " + bits + ".x, {1, 0, 0, 0}.x");
    DBufferIf(emitter, "SNE", {colorBit, 1}, 0);
    auto sample = DBufferSample(policy, bindings.color, bindings.sampler, bufferUV, false);
    emitter.emit("MOV.F32 " + color + ", " + sample.text);
    emitter.emit("ENDIF");
    if (bindings.hasRoughness)
    {
        const auto roughBit = emitter.temp();
        emitter.emit("AND.U " + roughBit + ".x, " + bits + ".x, {4, 0, 0, 0}.x");
        DBufferIf(emitter, "SNE", {roughBit, 1}, 0);
        sample = DBufferSample(policy, bindings.roughness, bindings.sampler, bufferUV, false);
        emitter.emit("MOV.F32 " + rough + ", " + sample.text);
        emitter.emit("ENDIF");
    }
    auto mode = emitter.op("MUL", 1, {emitter.swizzle(mask, "a"), F32({255})});
    const auto modeWord = emitter.temp();
    emitter.emit("TRUNC.U " + modeWord + ".x, " + Emitter::src(mode, 1));
    DBufferIf(emitter, "SEQ", {modeWord, 1}, 2);
    auto blend = emitter.op("MUL", 1, {input.roughness, F32({10})}, true);
    auto delta = emitter.op("MAD", 3, {input.base, input.base, emitter.neg(input.base)});
    blend = emitter.op("MAD", 1, {emitter.neg(input.metallic), blend, blend});
    delta = emitter.op("MUL", 3, {blend, delta});
    delta = emitter.op("MUL", 3, {emitter.swizzle(mask, "g"), delta});
    auto altered = emitter.op("MAD", 3, {delta, F32({.75}), input.base});
    auto lit = emitter.op("MUL", 3, {emitter.swizzle(mask, "b"), altered});
    auto roughFloor = emitter.op("MAX", 1, {input.roughness, F32({.200000003})});
    auto normalAmount = emitter.op("MOV", 1, {emitter.swizzle(input.normal, "b")}, true);
    lit = emitter.op("MAD", 3, {lit, F32({10}), emitter.neg(altered)});
    const auto base = emitter.op("MAD", 3, {normalAmount, lit, altered});
    const auto roughValue = emitter.op(
        "MAD", 1, {emitter.swizzle(mask, "g"), emitter.sub(1, roughFloor, input.roughness), input.roughness});
    Value specDelta = input.specular == F32({.5})
                          ? F32({-.474999994})
                          : emitter.op("MAD", 1, {input.specular, F32({.0500000007}), emitter.neg(input.specular)});
    const auto specValue = emitter.op("MAD", 1, {emitter.swizzle(mask, "g"), specDelta, input.specular});
    emitter.emit("MOV.F32 " + result.base.text + ".xyz, " + base.text);
    emitter.emit("MOV.F32 " + result.roughness.text + ".x, " + Emitter::src(roughValue, 1));
    emitter.emit("MOV.F32 " + result.specular.text + ".x, " + Emitter::src(specValue, 1));
    emitter.emit("ENDIF");
    DBufferIf(emitter, "SEQ", {modeWord, 1}, 1);
    const auto factor = emitter.op("ADD", 3, {emitter.swizzle({color, 4}, "a"), emitter.swizzle({color, 4}, "rgb")});
    const auto multiplied = emitter.op("MUL", 3, {result.base, factor});
    emitter.emit("MOV.F32 " + result.base.text + ".xyz, " + multiplied.text);
    emitter.emit("ELSE");
    const auto added =
        emitter.op("MAD", 3, {result.base, emitter.swizzle({color, 4}, "a"), emitter.swizzle({color, 4}, "rgb")});
    emitter.emit("MOV.F32 " + result.base.text + ".xyz, " + added.text);
    emitter.emit("ENDIF");
    if (bindings.hasRoughness)
    {
        const auto channels = Value{rough, 4};
        const auto r =
            emitter.op("MAD", 1, {emitter.swizzle(channels, "a"), result.roughness, emitter.swizzle(channels, "b")});
        const auto m =
            emitter.op("MAD", 1, {emitter.swizzle(channels, "a"), result.metallic, emitter.swizzle(channels, "r")});
        const auto s =
            emitter.op("MAD", 1, {emitter.swizzle(channels, "a"), result.specular, emitter.swizzle(channels, "g")});
        emitter.emit("MOV.F32 " + result.roughness.text + ".x, " + Emitter::src(r, 1));
        emitter.emit("MOV.F32 " + result.metallic.text + ".x, " + Emitter::src(m, 1));
        emitter.emit("MOV.F32 " + result.specular.text + ".x, " + Emitter::src(s, 1));
    }
    const auto normal = emitter.op("ADD", 3, {input.normal, F32({-.003921628})});
    emitter.emit("MOV.F32 " + result.normal.text + ".xyz, " + normal.text);
    emitter.emit("ENDIF");
    emitter.emit("ENDIF");
    return result;
}
inline std::string DBufferStageProgram(const ShaderBindings& header,
                                       const std::map<std::uint16_t, std::uint64_t>& hashes,
                                       const ReservedDiagnosticBuffer& input, OriginalDecalResponse response,
                                       const std::string& output)
{
    ValidateDiagnosticBuffer(header, input);
    const auto bindings = ReadDBufferBindings(header, hashes, response);
    Emitter emitter;
    PolicyEmitter policy(emitter, header);
    DecalSurface surface{policy.Load(input.index, 0, 3), policy.Load(input.index, 16, 1),
                         response == OriginalDecalResponse::ColorRoughness ? F32({.5})
                                                                           : policy.Load(input.index, 32, 1),
                         policy.Load(input.index, 48, 1), policy.Load(input.index, 64, 3)};
    const auto result =
        EmitDBufferSurface(policy, bindings, surface, policy.Load(input.index, 80, 2), policy.Load(input.index, 96, 2));
    Value value;
    if (output == "base")
        value = result.base;
    else if (output == "metallic")
        value = result.metallic;
    else if (output == "specular")
        value = result.specular;
    else if (output == "roughness")
        value = result.roughness;
    else if (output == "normal")
        value = result.normal;
    else
        throw std::runtime_error("DBuffer output unavailable");
    emitter.emit("MOV.F32 result_color0." + Comp(value.size) + ", " + Emitter::src(value, value.size));
    return Resolve(policy.Program(), {}, {});
}
}
}
}

// Typed operations from the original clustered forward-light programs.
// Resource association and complete policy admission are separate contracts.
#pragma once
#include "custom_material_lighting.h"
#include "custom_material_policy_emitter.h"
#include <regex>
#include <sstream>
namespace P4MapKit
{
namespace CustomMaterial
{
namespace Lighting
{
struct LocalLightInputs
{
    Value position, inverseRadius, color, falloffExponent, direction, spotAngles;
    Value sourceRadius, sourceLength, tangent, softSourceRadius;
};
struct SurfaceInputs
{
    Value normal, view, roughness, diffuse, specular, occlusion, specularCap;
};
struct LocalAttenuationResult
{
    Value toLight, direction, mask;
};
inline void ForwardShape(Value value, int size, const char* field)
{
    if (value.size != size)
        throw std::runtime_error(std::string("forward light input shape: ") + field);
}
inline Value ForwardSquare(Emitter& e, Value value)
{
    return e.op("MUL", value.size, {value, value});
}
inline Value ForwardRoot(Emitter& e, Value value)
{
    return e.per_component("RCP", e.per_component("RSQ", value));
}
inline Value ForwardChoose(Emitter& e, Value condition, Value yes, Value no)
{
    if (condition.size != 1 || yes.size != no.size)
        throw std::runtime_error("forward light selection shape");
    auto result = e.temp(), control = e.temp();
    e.emit("TRUNC.U.CC " + control + ".x, " + Emitter::src(condition, 1));
    e.emit("IF NE.x");
    e.emit("MOV.F32 " + result + "." + Comp(yes.size) + ", " + Emitter::src(yes, yes.size));
    e.emit("ELSE");
    e.emit("MOV.F32 " + result + "." + Comp(no.size) + ", " + Emitter::src(no, no.size));
    e.emit("ENDIF");
    return {result, yes.size};
}
inline Value OriginalPointFalloff(Emitter& e, Value toLight, Value inverseRadius, Value exponent)
{
    ForwardShape(toLight, 3, "toLight");
    ForwardShape(inverseRadius, 1, "inverseRadius");
    ForwardShape(exponent, 1, "falloffExponent");
    auto distance = e.dot(3, toLight, toLight), inverseSquared = ForwardSquare(e, inverseRadius);
    auto normalizedDistance = e.op("MUL", 1, {distance, inverseSquared});
    auto cutoff = e.op("MAD", 1, {e.neg(normalizedDistance), normalizedDistance, F32({1})}, true);
    auto inverseFalloff = ForwardSquare(e, cutoff);
    auto scaled = e.op("MUL", 3, {toLight, inverseRadius});
    auto radial = e.dot(3, scaled, scaled);
    radial = e.op("MOV", 1, {radial}, true);
    auto powered = e.op("POW", 1, {e.sub(1, F32({1}), radial), exponent});
    return ForwardChoose(e, e.op("SEQ", 1, {exponent, F32({0})}), inverseFalloff, powered);
}
inline Value OriginalSpotFalloff(Emitter& e, Value unitToLight, Value direction, Value angles)
{
    ForwardShape(unitToLight, 3, "unitToLight");
    ForwardShape(direction, 3, "direction");
    ForwardShape(angles, 2, "spotAngles");
    auto cone = e.sub(1, e.dot(3, unitToLight, direction), e.swizzle(angles, "r"));
    cone = e.op("MUL", 1, {cone, e.swizzle(angles, "g")}, true);
    return ForwardSquare(e, cone);
}
inline LocalAttenuationResult OriginalLocalAttenuation(Emitter& e, Value worldPosition, const LocalLightInputs& light)
{
    ForwardShape(worldPosition, 3, "worldPosition");
    ForwardShape(light.position, 3, "position");
    auto delta = e.sub(3, light.position, worldPosition);
    auto direction = e.op("MUL", 3, {delta, e.per_component("RSQ", e.dot(3, delta, delta))});
    auto point = OriginalPointFalloff(e, delta, light.inverseRadius, light.falloffExponent);
    auto spot = e.op("MUL", 1, {point, OriginalSpotFalloff(e, direction, light.direction, light.spotAngles)});
    auto enabled = e.op("SGT", 1, {e.swizzle(light.spotAngles, "r"), F32({-2})});
    return {delta, direction, ForwardChoose(e, enabled, spot, point)};
}
inline Value OriginalSphereHorizon(Emitter& e, Value noL, Value radius, Value inverseBiasedDistanceSquared)
{
    auto sineSquared = e.op("MUL", 1, {ForwardSquare(e, radius), inverseBiasedDistanceSquared}, true);
    auto sine = ForwardRoot(e, sineSquared);
    auto shifted = e.op("ADD", 1, {sine, e.op("MAX", 1, {noL, e.neg(sine)})});
    auto wrapped = e.op("MUL", 1, {e.op("DIV", 1, {ForwardSquare(e, shifted), sine}), F32({.25})});
    auto result = ForwardChoose(e, e.op("SLT", 1, {noL, sine}), wrapped, noL);
    return ForwardChoose(e, e.op("SGT", 1, {radius, F32({0})}), result, noL);
}
struct ForwardBxDFContext
{
    Value noL, noV, voL, noH, voH;
};
inline ForwardBxDFContext OriginalBxDFContext(Emitter& e, Value normal, Value view, Value light)
{
    auto noL = e.dot(3, normal, light), noV = e.dot(3, normal, view), voL = e.dot(3, light, view);
    auto inverse = e.per_component("RSQ", e.op("MAD", 1, {voL, F32({2}), F32({2})}));
    return {noL, noV, voL, e.op("MUL", 1, {inverse, e.op("ADD", 1, {noL, noV})}, true),
            e.op("ADD", 1, {inverse, e.op("MUL", 1, {inverse, voL})}, true)};
}
// The source programs use one Newton refinement of the finite sphere cap.
inline ForwardBxDFContext OriginalSphereContext(Emitter& e, const ForwardBxDFContext& c, Value sine)
{
    auto cosine = ForwardRoot(e, e.op("MAD", 1, {e.neg(sine), sine, F32({1})}));
    auto reflection = e.op("MAD", 1, {e.op("MUL", 1, {c.noL, c.noV}), F32({2}), e.neg(c.voL)});
    auto inverse =
        e.op("MUL", 1, {sine, e.per_component("RSQ", e.op("MAD", 1, {e.neg(reflection), reflection, F32({1})}))});
    auto tangentN = e.op("MUL", 1, {inverse, e.op("MAD", 1, {e.neg(reflection), c.noL, c.noV})});
    auto vv = ForwardSquare(e, c.noV);
    auto tangentV = e.op(
        "MUL", 1, {inverse, e.sub(1, e.op("MAD", 1, {vv, F32({2}), F32({-1})}), e.op("MUL", 1, {reflection, c.voL}))});
    auto determinant = e.op("MAD", 1, {e.neg(c.noL), c.noL, e.neg(vv)});
    determinant = e.op("MAD", 1, {e.neg(c.voL), c.voL, determinant});
    determinant = e.op("MAD", 1, {e.op("MUL", 1, {e.op("MUL", 1, {c.noL, c.noV}), c.voL}), F32({2}), determinant});
    determinant = e.op("ADD", 1, {determinant, F32({1})}, true);
    auto crossN = e.op("MUL", 1, {inverse, ForwardRoot(e, determinant)});
    auto crossV = e.op("MUL", 1, {e.op("MUL", 1, {crossN, c.noV}), F32({2})});
    auto baseN = e.op("MAD", 1, {c.noL, cosine, c.noV});
    auto n = e.op("ADD", 1, {baseN, tangentN});
    auto v = e.op("ADD", 1, {e.op("MAD", 1, {c.voL, cosine, tangentV}), F32({1})});
    auto p = e.op("MUL", 1, {crossN, v}), q = e.op("MUL", 1, {n, v}), s = e.op("MUL", 1, {crossV, n});
    auto numerator = e.op("MUL", 1, {q, e.op("MAD", 1, {p, F32({-.5}), e.op("MUL", 1, {s, F32({.25})})})});
    auto denominator = e.op("MAD", 1, {p, p, e.op("MUL", 1, {s, e.op("MAD", 1, {p, F32({-2}), s})})});
    auto correction = e.op("MAD", 1, {e.op("ADD", 1, {v, e.op("MUL", 1, {c.voL, cosine})}), F32({-.5}), F32({-.5})});
    denominator =
        e.op("MAD", 1, {n, e.op("MAD", 1, {baseN, ForwardSquare(e, v), e.op("MUL", 1, {q, correction})}), denominator});
    auto twice =
        e.op("MUL", 1,
             {e.op("DIV", 1, {numerator, e.op("MAD", 1, {denominator, denominator, ForwardSquare(e, numerator)})}),
              F32({2})});
    auto sinTheta = e.op("MUL", 1, {twice, denominator}),
         cosTheta = e.op("MAD", 1, {e.neg(twice), numerator, F32({1})});
    tangentN = e.op("MAD", 1, {cosTheta, tangentN, e.op("MUL", 1, {sinTheta, crossN})});
    tangentV = e.op("MAD", 1, {cosTheta, tangentV, e.op("MUL", 1, {sinTheta, crossV})});
    auto noL = e.op("MAD", 1, {c.noL, cosine, tangentN}), voL = e.op("MAD", 1, {c.voL, cosine, tangentV});
    auto inverseH = e.per_component("RSQ", e.op("MAD", 1, {voL, F32({2}), F32({2})}));
    auto noH = e.op("MUL", 1, {inverseH, e.op("ADD", 1, {noL, c.noV})}, true);
    auto voH = e.op("ADD", 1, {inverseH, e.op("MUL", 1, {inverseH, voL})}, true);
    auto aligned = e.op("SGE", 1, {reflection, cosine});
    noH = ForwardChoose(e, aligned, F32({1}), noH);
    voH = ForwardChoose(e, aligned, e.abs(c.noV), voH);
    auto active = e.op("SGT", 1, {sine, F32({0})});
    return {noL, c.noV, voL, ForwardChoose(e, active, noH, c.noH), ForwardChoose(e, active, voH, c.voH)};
}
inline Value OriginalAreaFastRoot(Emitter& e, Value alphaSquared)
{
    auto bits = e.temp();
    e.emit("MOV.S " + bits + ".x, " + Emitter::src(alphaSquared, 1));
    e.emit("SHR.S " + bits + ".x, " + bits + ".x, {1, 0, 0, 0}.x");
    e.emit("ADD.S " + bits + ".x, " + bits + ".x, {532487669, 0, 0, 0}.x");
    return {bits, 1};
}
struct ForwardAreaEnergy
{
    Value alphaSquared, normalization;
};
inline ForwardAreaEnergy OriginalAreaEnergy(Emitter& e, Value roughness, Value voH, Value sine, Value softSine)
{
    auto alpha = ForwardSquare(e, roughness), alphaSquared = ForwardSquare(e, alpha);
    auto soft = e.op("DIV", 1, {ForwardSquare(e, softSine), e.op("MAD", 1, {voH, F32({3.6}), F32({.4})})});
    auto adjusted = ForwardChoose(e, e.op("SGT", 1, {softSine, F32({0})}), e.op("ADD", 1, {alphaSquared, soft}, true),
                                  alphaSquared);
    auto newAlpha = e.op("MAD", 1, {OriginalAreaFastRoot(e, adjusted), F32({3}), sine});
    newAlpha = e.op("MUL", 1, {sine, newAlpha});
    newAlpha = e.op("DIV", 1, {newAlpha, e.op("ADD", 1, {voH, F32({.001})})});
    newAlpha = e.op("MAD", 1, {newAlpha, F32({.25}), adjusted});
    auto energy = ForwardChoose(e, e.op("SGT", 1, {sine, F32({0})}), e.op("DIV", 1, {adjusted, newAlpha}), F32({1}));
    return {adjusted, energy};
}
inline SurfaceLighting OriginalCapsuleSurface(Emitter& e, Value toLight, const LocalLightInputs& light,
                                              const SurfaceInputs& surface)
{
    ForwardShape(toLight, 3, "toLight");
    ForwardShape(light.tangent, 3, "tangent");
    ForwardShape(surface.normal, 3, "normal");
    ForwardShape(surface.view, 3, "view");
    auto halfLength = e.op("MUL", 3, {light.sourceLength, light.tangent});
    auto endpoint = e.op("MAD", 3, {e.neg(halfLength), F32({.5}), toLight});
    auto distance = e.dot(3, endpoint, endpoint), inverseDistance = e.per_component("RSQ", distance);
    auto biased = e.per_component("RCP", e.op("ADD", 1, {distance, F32({1})}));
    auto unit = e.op("MUL", 3, {endpoint, inverseDistance});
    auto noL = OriginalSphereHorizon(e, e.dot(3, surface.normal, unit), light.sourceRadius, biased);
    noL = e.op("MOV", 1, {noL}, true);
    auto falloff = ForwardChoose(e, e.op("SEQ", 1, {light.falloffExponent, F32({0})}), biased, F32({1}));
    auto roughness = e.op("MAX", 1, {surface.roughness, F32({.02})});
    auto alpha = ForwardSquare(e, roughness);
    auto radius = e.op("MUL", 1, {inverseDistance, light.sourceRadius});
    auto sine = e.op("MAD", 1, {e.neg(alpha), radius, radius}, true);
    auto softSine = e.op("MUL", 1, {inverseDistance, light.softSourceRadius}, true);
    auto context = OriginalSphereContext(e, OriginalBxDFContext(e, surface.normal, surface.view, unit), sine);
    auto area = OriginalAreaEnergy(e, roughness, context.voH, sine, softSine);
    auto noV = e.op("ADD", 1, {e.abs(context.noV), F32({.00001})}, true);
    auto grazing = e.sub(1, F32({1}), context.voH), square = ForwardSquare(e, grazing);
    auto power = e.op("MUL", 1, {ForwardSquare(e, square), grazing});
    auto fresnel = e.op("MAD", 3, {surface.specular, e.neg(power), surface.specular});
    fresnel = e.op("MAD", 3, {e.op("MUL", 1, {e.swizzle(surface.specular, "g"), F32({50})}, true), power, fresnel});
    auto denominator = e.op("MAD", 1, {area.alphaSquared, context.noH, e.neg(context.noH)});
    denominator = e.op("MAD", 1, {denominator, context.noH, F32({1})});
    auto actualAlpha = ForwardRoot(e, area.alphaSquared);
    auto first = e.op("MAD", 1, {noL, e.neg(actualAlpha), noL});
    first = e.op("ADD", 1, {actualAlpha, first});
    first = e.op("MUL", 1, {noV, first});
    auto second = e.op("MAD", 1, {e.neg(actualAlpha), noV, noV});
    second = e.op("ADD", 1, {second, actualAlpha});
    auto visibility = e.op("DIV", 1, {F32({.5}), e.op("MAD", 1, {noL, second, first})});
    auto distribution = e.op("DIV", 1, {area.alphaSquared, ForwardSquare(e, denominator)});
    distribution = e.op("MUL", 1, {distribution, area.normalization});
    auto scale = e.op("MUL", 1, {e.op("MUL", 1, {distribution, visibility}), F32({.318309844})});
    scale = ForwardChoose(e, e.op("SGT", 1, {surface.specularCap, F32({0})}),
                          e.op("MIN", 1, {scale, surface.specularCap}), scale);
    auto energy = e.op("MUL", 1, {falloff, noL});
    auto diffuse = e.op(
        "MUL", 3, {e.op("MUL", 3, {surface.diffuse, e.op("MUL", 1, {energy, surface.occlusion})}), F32({.318309903})});
    auto specular = e.op("MUL", 3, {e.op("MUL", 3, {scale, fresnel}), energy});
    specular = e.op("MUL", 3, {specular, surface.occlusion});
    return {diffuse, specular};
}
inline Value OriginalMaskedBackscatter(Emitter& e, Value unitToLight, Value normal, Value view, Value biasedFalloff)
{
    ForwardShape(unitToLight, 3, "masked unitToLight");
    ForwardShape(normal, 3, "masked normal");
    ForwardShape(view, 3, "masked view");
    ForwardShape(biasedFalloff, 1, "masked biasedFalloff");
    auto back = e.op("MIN", 1, {e.neg(e.dot(3, unitToLight, view)), F32({1})});
    back = e.op("MAX", 1, {back, F32({0})});
    auto denominator = e.op("MAD", 1, {back, F32({.360000014}), e.neg(back)});
    denominator = e.op("MAD", 1, {denominator, back, F32({1})});
    auto horizon = e.op("ADD", 1, {e.neg(e.dot(3, normal, unitToLight)), F32({.5})});
    horizon = e.op("MUL", 1, {horizon, F32({.444444448})}, true);
    return e.op("DIV", 1, {e.op("MUL", 1, {biasedFalloff, horizon}), ForwardSquare(e, denominator)});
}
struct MaskedLocalCompositionInputs
{
    Value lightColor, attenuation, directShadow, transmissionShadow, subsurfaceColor, opacity, opacityExponent;
};
// These distinct shadow factors are raw typed boundaries. Their production
// association requires the corresponding original policy producer proof.
inline SurfaceLighting OriginalMaskedLocalComposition(Emitter& e, const SurfaceLighting& core, Value backscatter,
                                                      const MaskedLocalCompositionInputs& inputs)
{
    ForwardShape(core.diffuse, 3, "masked core diffuse");
    ForwardShape(core.specular, 3, "masked core specular");
    ForwardShape(inputs.lightColor, 3, "masked lightColor");
    ForwardShape(inputs.subsurfaceColor, 3, "masked subsurfaceColor");
    for (auto value : {backscatter, inputs.attenuation, inputs.directShadow, inputs.transmissionShadow, inputs.opacity,
                       inputs.opacityExponent})
        ForwardShape(value, 1, "masked scalar");
    auto color = e.op("MUL", 3, {inputs.lightColor, inputs.attenuation});
    auto transmitted = e.op("MUL", 3, {color, inputs.transmissionShadow});
    auto extra = e.op("MUL", 3, {backscatter, ForwardSquare(e, inputs.subsurfaceColor)});
    extra = e.op("MUL", 3, {extra, transmitted});
    extra = e.op("MUL", 3, {extra, F32({.114591546})});
    color = e.op("MUL", 3, {color, inputs.directShadow});
    auto diffuse = e.op("MAD", 3, {core.diffuse, color, extra});
    auto shaped = e.op("MUL", 3, {core.specular, e.op("POW", 1, {inputs.opacity, inputs.opacityExponent})});
    return {diffuse, e.op("MUL", 3, {color, shaped})};
}
inline std::string ForwardLightingBlockProgram(const std::string& block)
{
    Emitter e;
    auto load = [&](unsigned index, int size)
    {
        auto reg = e.temp();
        e.emit("LDC.F32X4 " + reg + ", buf0[" + std::to_string(index * 16) + "]");
        return Value{reg, size};
    };
    Value result;
    if (block == "point-falloff")
        result = OriginalPointFalloff(e, load(0, 3), load(1, 1), load(2, 1));
    else if (block == "spot-falloff")
        result = OriginalSpotFalloff(e, load(0, 3), load(1, 3), load(2, 2));
    else if (block == "sphere-horizon")
        result = OriginalSphereHorizon(e, load(0, 1), load(1, 1), load(2, 1));
    else if (block == "area-energy")
    {
        auto output = OriginalAreaEnergy(e, load(0, 1), load(1, 1), load(2, 1), load(3, 1));
        result = e.append(output.alphaSquared, output.normalization);
    }
    else if (block == "masked-backscatter")
        result = OriginalMaskedBackscatter(e, load(0, 3), load(1, 3), load(2, 3), load(3, 1));
    else if (block == "masked-compose-diffuse" || block == "masked-compose-specular")
    {
        SurfaceLighting core{load(0, 3), load(1, 3)};
        MaskedLocalCompositionInputs inputs{load(3, 3), load(4, 1), load(5, 1), load(6, 1),
                                            load(7, 3), load(8, 1), load(9, 1)};
        auto output = OriginalMaskedLocalComposition(e, core, load(2, 1), inputs);
        result = block == "masked-compose-diffuse" ? output.diffuse : output.specular;
    }
    else if (block == "capsule-diffuse" || block == "capsule-specular")
    {
        LocalLightInputs light{};
        light.sourceLength = load(1, 1);
        light.tangent = load(2, 3);
        light.sourceRadius = load(3, 1);
        light.softSourceRadius = load(4, 1);
        light.falloffExponent = load(5, 1);
        SurfaceInputs surface{load(6, 3), load(7, 3), load(8, 1), load(9, 3), load(10, 3), load(11, 1), load(12, 1)};
        auto output = OriginalCapsuleSurface(e, load(0, 3), light, surface);
        result = block == "capsule-diffuse" ? output.diffuse : output.specular;
    }
    else
        throw std::runtime_error("unknown forward lighting block: " + block);
    e.emit("MOV.F32 result_color0." + Comp(result.size) + ", " + Emitter::src(result, result.size));
    auto program = e.program();
    auto marker = program.find("{LAST:}");
    program.replace(marker, 7, "0");
    return program;
}
struct ForwardWords
{
    std::string text;
    int size = 1;
};
inline std::string ForwardWordSource(const ForwardWords& word)
{
    if (word.size < 1 || word.size > 4)
        throw std::runtime_error("forward integer word shape");
    return word.text + (word.size == 1 ? ".x" : "");
}
inline ForwardWords ForwardWordConstant(unsigned value)
{
    return {"{" + std::to_string(value) + ", 0, 0, 0}", 1};
}
inline ForwardWords ForwardWordOp(Emitter& e, const std::string& opcode, int size,
                                  std::initializer_list<ForwardWords> inputs)
{
    auto reg = e.temp();
    std::string line = opcode + " " + reg + "." + Comp(size);
    for (const auto& word : inputs)
        line += ", " + ForwardWordSource(word);
    e.emit(line);
    return {reg, size};
}
inline ForwardWords ForwardWordLane(Emitter& e, ForwardWords words, char lane)
{
    if (std::string("xyzw").find(lane) >= std::size_t(words.size))
        throw std::runtime_error("forward integer lane range");
    auto reg = e.temp();
    e.emit("MOV.U " + reg + ".x, " + words.text + "." + lane);
    return {reg, 1};
}
inline ForwardWords ForwardWordLoad(PolicyEmitter& p, unsigned buffer, unsigned offset, int size)
{
    if (buffer > 31 || offset % 4 || size < 1 || size > 4)
        throw std::runtime_error("forward word load range");
    p.buffers.insert(buffer);
    auto reg = p.emitter.temp();
    p.emitter.emit("LDC." + std::string(size == 1 ? "U32" : "U32X4") + " " + reg + "." + Comp(size) + ", buf" +
                   std::to_string(buffer) + "[" + std::to_string(offset) + "]");
    return {reg, size};
}
struct ForwardLocalResources
{
    unsigned localLights, counts, indices, defaultSampler;
};
inline ForwardLocalResources ReadForwardLocalResources(const ShaderBindings& header)
{
    auto bindings = ReadForwardPolicyBindings(header);
    if (header.default_sampler < 0 || header.default_sampler > 31)
        throw std::runtime_error("forward local default sampler absent/range");
    return {bindings.RequireSRV("LocalLights"), bindings.RequireSRV("CulledLightCounts"),
            bindings.RequireSRV("CulledLightIndices"), unsigned(header.default_sampler)};
}
inline Value ForwardBufferFetch(Emitter& e, unsigned slot, unsigned sampler, ForwardWords index, bool integer)
{
    if (slot > 63 || sampler > 31 || index.size != 1)
        throw std::runtime_error("forward buffer fetch slot/index range");
    e.uses_texture = true;
    e.handle.clear();
    e.emit("LDC.U64 D0.x, buf14[" + std::to_string(328 + 8 * slot) + "]");
    e.emit("LDC.U64 D1.x, buf14[" + std::to_string(1352 + 8 * sampler) + "]");
    e.emit("OR.U64 D0.x, D0, D1");
    auto reg = e.temp();
    e.emit(std::string(integer ? "TXF.U " : "TXF.F ") + reg + (integer ? ".x" : "") + ", " + ForwardWordSource(index) +
           ", handle(D0.x), BUFFER");
    return {reg, integer ? 1 : 4};
}
inline ForwardWords ForwardGridIndex(PolicyEmitter& p, Value pixelCoordinates, Value sceneDepth)
{
    ForwardShape(pixelCoordinates, 2, "gridPixelCoordinates");
    ForwardShape(sceneDepth, 1, "gridSceneDepth");
    auto& e = p.emitter;
    auto depth = p.Load(2, 48, 3);
    auto z = e.op("MUL", 1,
                  {e.per_component("LG2", e.op("MAD", 1, {sceneDepth, e.swizzle(depth, "r"), e.swizzle(depth, "g")})),
                   e.swizzle(depth, "b")});
    z = e.op("MAX", 1, {z, F32({0})});
    auto zreg = e.temp();
    e.emit("TRUNC.U " + zreg + ".x, " + Emitter::src(z, 1));
    auto pixels = e.temp();
    e.emit("TRUNC.U " + pixels + ".xy, " + Emitter::src(pixelCoordinates, 2));
    auto shift = ForwardWordLoad(p, 2, 32, 1), dimensions = ForwardWordLoad(p, 2, 16, 3);
    auto tiled = ForwardWordOp(e, "SHR.U", 2, {{pixels, 2}, shift});
    auto maxZ = ForwardWordOp(e, "ADD.U", 1, {ForwardWordLane(e, dimensions, 'z'), ForwardWordConstant(0xffffffffu)});
    auto slice = ForwardWordOp(e, "MIN.U", 1, {{zreg, 1}, maxZ});
    auto row =
        ForwardWordOp(e, "MAD.U", 1, {slice, ForwardWordLane(e, dimensions, 'y'), ForwardWordLane(e, tiled, 'y')});
    return ForwardWordOp(e, "MAD.U", 1, {row, ForwardWordLane(e, dimensions, 'x'), ForwardWordLane(e, tiled, 'x')});
}
inline Value ForwardShadowMask(Emitter& e, ForwardWords packed, unsigned shift)
{
    if (packed.size != 1 || shift > 28)
        throw std::runtime_error("forward shadow word shape");
    auto shifted = shift ? ForwardWordOp(e, "SHR.U", 1, {packed, ForwardWordConstant(shift)}) : packed;
    std::vector<Value> lanes;
    for (unsigned i = 0; i < 4; ++i)
    {
        auto word = ForwardWordOp(e, "AND.U", 1, {shifted, ForwardWordConstant(1u << i)});
        auto reg = e.temp();
        e.emit("SNE.U.CC " + e.temp() + ".x, " + ForwardWordSource(word) + ", {0, 0, 0, 0}.x");
        e.emit("IF NE.x");
        e.emit("MOV.F32 " + reg + ".x, {1, 0, 0, 0}.x");
        e.emit("ELSE");
        e.emit("MOV.F32 " + reg + ".x, {0, 0, 0, 0}.x");
        e.emit("ENDIF");
        lanes.push_back({reg, 1});
    }
    return e.append(e.append(lanes[0], lanes[1]), e.append(lanes[2], lanes[3]));
}
struct ClusteredLocalInputs
{
    Value worldPosition, pixelCoordinates, sceneDepth, precomputedShadow, dynamicShadow;
    Value shadowBlendWeight, unshadowedSurfaceShadow, accumulationScale;
    ForwardWords primitiveLightingChannels;
};
struct ClusteredMaskedInputs
{
    Value subsurfaceColor, opacity, opacityExponent;
};
// The original forward loop ignores the rect-type tag and uses its capsule
// path. The six-record stride retains the unused barn-door record.
inline SurfaceLighting EmitClusteredLocalLighting(PolicyEmitter& p, const ForwardLocalResources& resources,
                                                  const ClusteredLocalInputs& inputs, const SurfaceInputs& surface,
                                                  const ClusteredMaskedInputs* masked = nullptr,
                                                  const SurfaceLighting* initial = nullptr,
                                                  const Value* accumulationOcclusion = nullptr)
{
    ForwardShape(inputs.worldPosition, 3, "worldPosition");
    ForwardShape(inputs.precomputedShadow, 4, "precomputedShadow");
    ForwardShape(inputs.dynamicShadow, 4, "dynamicShadow");
    ForwardShape(inputs.shadowBlendWeight, 1, "shadowBlendWeight");
    ForwardShape(inputs.unshadowedSurfaceShadow, 1, "unshadowedSurfaceShadow");
    ForwardShape(inputs.accumulationScale, 1, "accumulationScale");
    if (inputs.primitiveLightingChannels.size != 1)
        throw std::runtime_error("forward primitive lighting channel shape");
    if (initial)
    {
        ForwardShape(initial->diffuse, 3, "initial diffuse");
        ForwardShape(initial->specular, 3, "initial specular");
        if (initial->diffuse.text.empty() || initial->specular.text.empty())
            throw std::runtime_error("forward initial accumulator identity absent");
    }
    if (accumulationOcclusion)
    {
        if (!initial)
            throw std::runtime_error("forward accumulation occlusion requires initial accumulators");
        ForwardShape(*accumulationOcclusion, 1, "accumulation occlusion");
        if (accumulationOcclusion->text.empty())
            throw std::runtime_error("forward accumulation occlusion identity absent");
    }
    auto& e = p.emitter;
    auto grid = ForwardGridIndex(p, inputs.pixelCoordinates, inputs.sceneDepth);
    auto gridBase = ForwardWordOp(e, "MUL.U", 1, {grid, ForwardWordConstant(2)});
    auto countValue = ForwardBufferFetch(e, resources.counts, resources.defaultSampler, gridBase, true);
    auto count = ForwardWordOp(e, "MIN.U", 1, {{countValue.text, 1}, ForwardWordLoad(p, 2, 0, 1)});
    auto startValue = ForwardBufferFetch(e, resources.counts, resources.defaultSampler,
                                         ForwardWordOp(e, "ADD.U", 1, {gridBase, ForwardWordConstant(1)}), true);
    auto cursor = e.temp(), diffuse = e.temp(), specular = e.temp();
    e.emit("MOV.U " + cursor + ".x, {0, 0, 0, 0}.x");
    if (initial)
    {
        e.emit("MOV.F32 " + diffuse + ".xyz, " + Emitter::src(initial->diffuse, 3));
        e.emit("MOV.F32 " + specular + ".xyz, " + Emitter::src(initial->specular, 3));
    }
    else
    {
        e.emit("MOV.F32 " + diffuse + ".xyz, {0, 0, 0, 0}.x");
        e.emit("MOV.F32 " + specular + ".xyz, {0, 0, 0, 0}.x");
    }
    e.emit("REP.S");
    e.emit("SGE.U.CC " + e.temp() + ".x, " + cursor + ".x, " + ForwardWordSource(count));
    e.emit("BRK (NE.x)");
    auto listIndex = ForwardWordOp(e, "ADD.U", 1, {{startValue.text, 1}, {cursor, 1}});
    auto lightIndex = ForwardBufferFetch(e, resources.indices, resources.defaultSampler, listIndex, true);
    auto base = ForwardWordOp(e, "MUL.U", 1, {{lightIndex.text, 1}, ForwardWordConstant(6)});
    std::array<Value, 5> records;
    for (unsigned i = 0; i < 5; ++i)
        records[i] = ForwardBufferFetch(e, resources.localLights, resources.defaultSampler,
                                        i ? ForwardWordOp(e, "ADD.U", 1, {base, ForwardWordConstant(i)}) : base, false);
    auto length = e.temp();
    e.emit("UP2H.F " + length + ".x, " + records[3].text + ".w");
    LocalLightInputs light{e.swizzle(records[0], "rgb"), e.swizzle(records[0], "a"),
                           e.swizzle(records[1], "rgb"), e.swizzle(records[1], "a"),
                           e.swizzle(records[2], "rgb"), e.swizzle(records[3], "rg"),
                           e.swizzle(records[3], "b"),   {length, 1},
                           e.swizzle(records[4], "rgb"), e.swizzle(records[4], "a")};
    auto packed = e.temp();
    e.emit("MOV.U " + packed + ".x, " + records[2].text + ".w");
    ForwardWords mask{packed, 1};
    auto attenuation = OriginalLocalAttenuation(e, inputs.worldPosition, light);
    auto previews = ForwardShadowMask(e, mask, 4), staticMask = ForwardShadowMask(e, mask, 0);
    auto dynamic = e.dot(4, previews, inputs.dynamicShadow);
    dynamic = e.op("MAD", 1, {dynamic, inputs.shadowBlendWeight, e.neg(inputs.shadowBlendWeight)});
    dynamic = e.op("ADD", 1, {dynamic, F32({1})});
    auto usedChannels = e.dot(4, staticMask, F32({1})), precomputed = e.dot(4, inputs.precomputedShadow, staticMask);
    auto staticFactor = e.op("MAD", 1, {precomputed, usedChannels, e.neg(usedChannels)});
    staticFactor = e.op("ADD", 1, {staticFactor, F32({1})});
    auto shadowBits = ForwardWordOp(e, "AND.U", 1, {mask, ForwardWordConstant(255)});
    auto shadow = e.temp();
    e.emit("SNE.U.CC " + e.temp() + ".x, " + ForwardWordSource(shadowBits) + ", {0, 0, 0, 0}.x");
    e.emit("IF NE.x");
    auto shadowed = e.op("MUL", 1, {dynamic, staticFactor});
    e.emit("MOV.F32 " + shadow + ".x, " + Emitter::src(shadowed, 1));
    e.emit("ELSE");
    e.emit("MOV.F32 " + shadow + ".x, " + Emitter::src(inputs.unshadowedSurfaceShadow, 1));
    e.emit("ENDIF");
    auto channels =
        ForwardWordOp(e, "AND.U", 1,
                      {ForwardWordOp(e, "SHR.U", 1, {mask, ForwardWordConstant(8)}), inputs.primitiveLightingChannels});
    e.emit("SNE.U.CC " + e.temp() + ".x, " + ForwardWordSource(channels) + ", {0, 0, 0, 0}.x");
    e.emit("IF NE.x");
    auto positive = e.op("SGT", 1, {attenuation.mask, F32({0})});
    e.emit("TRUNC.U.CC " + e.temp() + ".x, " + Emitter::src(positive, 1));
    e.emit("IF NE.x");
    auto surfacePositive = e.op("SGT", 1, {{shadow, 1}, {"-" + shadow, 1}});
    e.emit("TRUNC.U.CC " + e.temp() + ".x, " + Emitter::src(surfacePositive, 1));
    e.emit("IF NE.x");
    auto coreSurface = surface;
    if (masked)
        coreSurface.occlusion = F32({1});
    auto lighting = OriginalCapsuleSurface(e, attenuation.toLight, light, coreSurface);
    // The masked diffuse capsule uses unit AO, while its retained specular term
    // consumes the explicit material AO before masked surface composition.
    if (masked && accumulationOcclusion)
        lighting.specular = e.op("MUL", 3, {lighting.specular, *accumulationOcclusion});
    Value diffuseLight, specularLight;
    if (masked)
    {
        auto endpoint = e.op(
            "MAD", 3, {e.neg(e.op("MUL", 3, {light.sourceLength, light.tangent})), F32({.5}), attenuation.toLight});
        auto distance = e.dot(3, endpoint, endpoint),
             unit = e.op("MUL", 3, {endpoint, e.per_component("RSQ", distance)});
        auto biased = e.per_component("RCP", e.op("ADD", 1, {distance, F32({1})}));
        auto falloff = ForwardChoose(e, e.op("SEQ", 1, {light.falloffExponent, F32({0})}), biased, F32({1}));
        auto transmission = e.temp();
        e.emit("SNE.U.CC " + e.temp() + ".x, " + ForwardWordSource(shadowBits) + ", {0, 0, 0, 0}.x");
        e.emit("IF NE.x");
        e.emit("MOV.F32 " + transmission + ".x, " + shadow + ".x");
        e.emit("ELSE");
        e.emit("MOV.F32 " + transmission + ".x, {1, 0, 0, 0}.x");
        e.emit("ENDIF");
        MaskedLocalCompositionInputs composition{
            light.color,     attenuation.mask,       {shadow, 1}, {transmission, 1}, masked->subsurfaceColor,
            masked->opacity, masked->opacityExponent};
        auto result = OriginalMaskedLocalComposition(
            e, lighting, OriginalMaskedBackscatter(e, unit, surface.normal, surface.view, falloff), composition);
        diffuseLight = result.diffuse;
        specularLight = result.specular;
    }
    else
    {
        auto color = e.op("MUL", 3, {light.color, attenuation.mask});
        color = e.op("MUL", 3, {color, {shadow, 1}});
        diffuseLight = e.op("MUL", 3, {lighting.diffuse, color});
        specularLight = e.op("MUL", 3, {lighting.specular, color});
    }
    if (accumulationOcclusion)
    {
        diffuseLight = e.op("MUL", 3, {diffuseLight, inputs.accumulationScale});
        specularLight = e.op("MUL", 3, {specularLight, inputs.accumulationScale});
        e.emit("MAD.F32 " + diffuse + ".xyz, " + diffuseLight.text + ", " + Emitter::src(*accumulationOcclusion, 1) +
               ", " + diffuse);
        e.emit("MAD.F32 " + specular + ".xyz, " + specularLight.text + ", " + Emitter::src(*accumulationOcclusion, 1) +
               ", " + specular);
    }
    else if (initial)
    {
        e.emit("MAD.F32 " + diffuse + ".xyz, " + diffuseLight.text + ", " + Emitter::src(inputs.accumulationScale, 1) +
               ", " + diffuse);
        e.emit("MAD.F32 " + specular + ".xyz, " + specularLight.text + ", " +
               Emitter::src(inputs.accumulationScale, 1) + ", " + specular);
    }
    else
    {
        diffuseLight = e.op("MUL", 3, {diffuseLight, inputs.accumulationScale});
        specularLight = e.op("MUL", 3, {specularLight, inputs.accumulationScale});
        e.emit("ADD.F32 " + diffuse + ".xyz, " + diffuse + ", " + diffuseLight.text);
        e.emit("ADD.F32 " + specular + ".xyz, " + specular + ", " + specularLight.text);
    }
    e.emit("ENDIF");
    e.emit("ENDIF");
    e.emit("ENDIF");
    e.emit("ADD.U " + cursor + ".x, " + cursor + ".x, {1, 0, 0, 0}.x");
    e.emit("ENDREP");
    return {{diffuse, 3}, {specular, 3}};
}
// Offline compiler fixture inputs occupy an explicit diagnostic buffer. This
// helper does not supply that buffer in a game material or admit an export.
inline std::string ForwardReuseTemporaries(const std::string& program)
{
    if (program.size() > (2u << 20))
        throw std::runtime_error("forward generated program limit");
    std::vector<std::string> lines;
    std::istringstream stream(program);
    std::string line;
    while (std::getline(stream, line))
    {
        if (lines.size() >= 8192)
            throw std::runtime_error("forward generated instruction limit");
        lines.push_back(line);
    }
    const std::regex token("\\bV[0-9]+\\b");
    std::map<std::string, std::pair<std::size_t, std::size_t>> ranges;
    std::vector<std::pair<std::size_t, std::size_t>> loops;
    std::vector<std::size_t> stack;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        if (lines[i].compare(0, 6, "TEMP V") == 0)
            continue;
        if (lines[i] == "REP.S" || lines[i] == "REP.S;")
            stack.push_back(i);
        if (lines[i] == "ENDREP" || lines[i] == "ENDREP;")
        {
            if (stack.empty())
                throw std::runtime_error("forward unmatched loop end");
            loops.push_back({stack.back(), i});
            stack.pop_back();
        }
        for (std::sregex_iterator it(lines[i].begin(), lines[i].end(), token), end; it != end; ++it)
        {
            auto name = it->str();
            auto found = ranges.find(name);
            if (found == ranges.end())
                ranges[name] = {i, i};
            else
                found->second.second = i;
        }
    }
    if (!stack.empty())
        throw std::runtime_error("forward unmatched loop start");
    for (auto& item : ranges)
        for (const auto& loop : loops)
            if (item.second.first < loop.first && item.second.second >= loop.first && item.second.second < loop.second)
                item.second.second = loop.second;
    std::map<std::string, unsigned> allocated;
    std::set<unsigned> free;
    unsigned slots = 0;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        if (lines[i].compare(0, 6, "TEMP V") == 0)
            continue;
        std::string rewritten;
        std::size_t at = 0;
        for (std::sregex_iterator it(lines[i].begin(), lines[i].end(), token), end; it != end; ++it)
        {
            auto name = it->str();
            auto found = allocated.find(name);
            if (found == allocated.end())
            {
                unsigned slot;
                if (free.empty())
                    slot = ++slots;
                else
                {
                    slot = *free.begin();
                    free.erase(free.begin());
                }
                found = allocated.emplace(name, slot).first;
            }
            auto position = std::size_t(it->position());
            rewritten += lines[i].substr(at, position - at) + "V" + std::to_string(found->second);
            at = position + std::size_t(it->length());
        }
        rewritten += lines[i].substr(at);
        lines[i] = rewritten;
        for (auto it = allocated.begin(); it != allocated.end();)
            if (ranges.at(it->first).second == i)
            {
                free.insert(it->second);
                it = allocated.erase(it);
            }
            else
                ++it;
    }
    std::string declarations = "TEMP ";
    for (unsigned i = 1; i <= slots; ++i)
        declarations += (i > 1 ? ", " : "") + std::string("V") + std::to_string(i);
    declarations += ";";
    std::string result;
    for (const auto& original : lines)
        result += (original.compare(0, 6, "TEMP V") == 0 ? declarations : original) + "\n";
    return result;
}
inline std::string ForwardClusteredBlockProgram(const ShaderBindings& header, const std::string& output,
                                                const ReservedDiagnosticBuffer& input, bool reuse = true,
                                                bool seedInitial = false, bool seedOcclusion = false)
{
    if (seedOcclusion && !seedInitial)
        throw std::runtime_error("forward accumulation occlusion requires initial accumulators");
    ValidateDiagnosticBuffer(header, input);
    if (output != "diffuse" && output != "specular" && output != "masked-diffuse" && output != "masked-specular")
        throw std::runtime_error("forward clustered output kind");
    Emitter e;
    PolicyEmitter policy(e, header);
    auto resources = ReadForwardLocalResources(header);
    auto load = [&](unsigned index, int size)
    {
        return policy.Load(input.index, index * 16, size);
    };
    ClusteredLocalInputs inputs{load(0, 3),  load(1, 2),  load(2, 1),
                                load(10, 4), load(11, 4), load(12, 1),
                                load(13, 1), load(14, 1), ForwardWordLoad(policy, input.index, 15 * 16, 1)};
    SurfaceInputs surface{load(3, 3), load(4, 3), load(5, 1), load(6, 3), load(7, 3), load(8, 1), load(9, 1)};
    ClusteredMaskedInputs masked{load(16, 3), load(17, 1), load(18, 1)};
    const bool maskedKind = output == "masked-diffuse" || output == "masked-specular";
    SurfaceLighting initial{{"", 0}, {"", 0}};
    if (seedInitial)
        initial = {load(19, 3), load(20, 3)};
    Value accumulationOcclusion{"", 0};
    if (seedOcclusion)
        accumulationOcclusion = load(21, 1);
    auto result =
        EmitClusteredLocalLighting(policy, resources, inputs, surface, maskedKind ? &masked : nullptr,
                                   seedInitial ? &initial : nullptr, seedOcclusion ? &accumulationOcclusion : nullptr);
    auto selected = output == "diffuse" || output == "masked-diffuse" ? result.diffuse : result.specular;
    e.emit("MOV.F32 result_color0.xyz, " + selected.text);
    auto program = policy.Program();
    auto marker = program.find("{LAST:}");
    if (marker == std::string::npos)
        throw std::runtime_error("forward diagnostic declaration marker");
    program.replace(marker, 7, "0");
    return reuse ? ForwardReuseTemporaries(program) : program;
}
}
}
}

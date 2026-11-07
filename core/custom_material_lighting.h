// Independently emitted mathematical blocks of the original forward policies.
// Block qualification does not authorize a complete base-pass lighting route.
#pragma once
#include "custom_material_graph.h"
namespace P4MapKit
{
namespace CustomMaterial
{
namespace Lighting
{
inline Value F32(std::initializer_list<double> values)
{
    std::vector<double> out;
    for (double value : values)
        out.push_back(double(float(value)));
    return Constant(out);
}
struct SurfaceReflectance
{
    Value diffuse, specular;
};
struct SurfaceLighting
{
    Value diffuse, specular;
};
inline SurfaceReflectance Reflectance(Emitter& e, Value base, Value metallic, Value specular)
{
    auto dielectric = e.op("MUL", 1, {specular, F32({.08})});
    auto difference = e.sub(3, base, dielectric);
    return {e.op("MAD", 3, {metallic, e.neg(base), base}), e.op("MAD", 3, {metallic, difference, dielectric})};
}
// ExpNoV is exp2(-9.28*NoV), evaluated separately so the oracle can bind the
// exact scheduled original value at the BRDF block boundary.
inline Value EnvironmentBRDFParameters(Emitter& e, Value roughness, Value expNoV)
{
    auto curve = e.op("MAD", 4, {roughness, F32({-1, -.0275, -.572, .022}), F32({1, .0425, 1.04, -.04})});
    auto x = e.swizzle(curve, "r"), y = e.swizzle(curve, "g"), zw = e.swizzle(curve, "ba");
    auto squared = e.op("MUL", 1, {x, x});
    auto fitted = e.op("MAD", 1, {e.op("MIN", 1, {squared, expNoV}), x, y});
    return e.op("MAD", 2, {fitted, F32({-1.04, 1.04}), zw});
}
inline Value EnvironmentBRDFExp(Emitter& e, Value specular, Value roughness, Value expNoV)
{
    auto ab = EnvironmentBRDFParameters(e, roughness, expNoV);
    auto correction = e.op("MUL", 1, {e.swizzle(specular, "g"), F32({50})}, true);
    auto bias = e.op("MUL", 1, {e.swizzle(ab, "g"), correction});
    return e.op("MAD", 3, {specular, e.swizzle(ab, "r"), bias});
}
inline Value EnvironmentBRDF(Emitter& e, Value specular, Value roughness, Value noV)
{
    return EnvironmentBRDFExp(e, specular, roughness, e.per_component("EX2", e.op("MUL", 1, {noV, F32({-9.28})})));
}
inline Value HighQualityLightmap(Emitter& e, Value texture0, Value texture1, Value scale0, Value add0, Value scale1,
                                 Value add1, Value normal)
{
    auto sh = e.op("MAD", 4, {texture1, scale1, add1});
    auto rotated = e.swizzle(normal, "gbr");
    auto direction = e.append(rotated, F32({1}));
    auto directional = e.op("MAX", 1, {e.dot(4, sh, direction), F32({0})});
    auto residual = e.op("MAD", 1, {e.swizzle(texture1, "a"), F32({1.0 / 255}), e.swizzle(texture0, "a")});
    // The original compiled half residual uses this representable bias, rather
    // than a freshly rounded floating-point division of -.5 by 255.
    residual = e.op("ADD", 1, {residual, F32({-.00196078396})});
    auto logLight = e.op("MAD", 1, {residual, e.swizzle(scale0, "a"), e.swizzle(add0, "a")});
    auto luminance = e.op("ADD", 1, {e.per_component("EX2", logLight), F32({-.01858136})});
    auto textureRGB = e.swizzle(texture0, "rgb");
    auto uvw =
        e.op("MAD", 3, {e.op("MUL", 3, {textureRGB, textureRGB}), e.swizzle(scale0, "rgb"), e.swizzle(add0, "rgb")});
    return e.op("MUL", 3, {e.op("MUL", 1, {luminance, directional}), uvw});
}
inline Value DistanceFieldShadow(Emitter& e, Value distance, Value inversePenumbra, Value masks)
{
    auto product = e.op("MUL", 4, {inversePenumbra, distance});
    auto shifted = e.op("MAD", 4, {inversePenumbra, F32({-.5}), product});
    auto coverage = e.op("ADD", 4, {shifted, F32({.5})}, true);
    return e.op("MUL", 4, {e.op("MUL", 4, {masks, coverage}), coverage});
}
inline Value ReflectionMip(Emitter& e, Value roughness, Value maximumMip)
{
    auto level = e.per_component("LG2", e.op("MAX", 1, {roughness, F32({.001})}));
    auto curve = e.op("MAD", 1, {e.neg(level), F32({1.2}), F32({1})});
    return e.op("ADD", 1, {e.sub(1, maximumMip, curve), F32({-1})});
}
inline Value FogComposite(Emitter& e, Value linearColor, Value fog, Value exposure)
{
    auto fogged = e.op("MAD", 3, {linearColor, e.swizzle(fog, "a"), e.swizzle(fog, "rgb")});
    return e.op("MUL", 3, {fogged, exposure});
}
inline Value ComposeVolumetricFog(Emitter& e, Value vertexFog, Value volumeFog)
{
    auto color = e.op("MAD", 3, {e.swizzle(vertexFog, "rgb"), e.swizzle(volumeFog, "a"), e.swizzle(volumeFog, "rgb")});
    return e.append(color, e.op("MUL", 1, {e.swizzle(vertexFog, "a"), e.swizzle(volumeFog, "a")}));
}
// C4 reflection occlusion correction, derived from the original policy's
// scheduled polynomial. This is separate from reflection sampling/blending.
inline Value C4SpecularOcclusion(Emitter& e, Value specular, Value occlusion)
{
    auto linear = e.op("MAD", 3, {specular, F32({2.0404}), F32({-.3324})});
    auto weighted = e.op("MUL", 3, {occlusion, linear});
    auto correction = e.op("MAD", 3, {specular, F32({-4.7951}), weighted});
    correction = e.op("ADD", 3, {correction, F32({.6417})});
    correction = e.op("MUL", 3, {occlusion, correction});
    correction = e.op("MAD", 3, {specular, F32({2.7552}), correction});
    correction = e.op("ADD", 3, {correction, F32({.6903})});
    correction = e.op("MUL", 3, {occlusion, correction});
    return e.op("MAX", 3, {occlusion, correction});
}
// Original opaque directional BRDF, before light color and shadow factors.
// Normal/view are unit vectors; the original policy normalizes light direction.
inline SurfaceLighting DirectionalSurface(Emitter& e, Value normal, Value view, Value light, Value roughness,
                                          Value diffuse, Value specular, Value occlusion, Value specularCap)
{
    light = e.normalize(light);
    auto voL = e.dot(3, light, view), noVRaw = e.dot(3, normal, view);
    auto inverseLength = e.per_component("RSQ", e.op("MAD", 1, {voL, F32({2}), F32({2})}));
    auto voH = e.op("ADD", 1, {inverseLength, e.op("MUL", 1, {inverseLength, voL})}, true);
    auto grazing = e.sub(1, F32({1}), voH);
    auto square = e.op("MUL", 1, {grazing, grazing});
    auto fresnelPower = e.op("MUL", 1, {e.op("MUL", 1, {square, square}), grazing});
    auto noLRaw = e.dot(3, normal, light);
    auto noH = e.op("MUL", 1, {inverseLength, e.op("ADD", 1, {noLRaw, noVRaw})}, true);
    auto floorRoughness = e.op("MAX", 1, {roughness, F32({.02})});
    auto alpha = e.op("MUL", 1, {floorRoughness, floorRoughness});
    auto alphaSquared = e.op("MUL", 1, {alpha, alpha});
    auto denominator = e.op("MAD", 1, {alphaSquared, noH, e.neg(noH)});
    denominator = e.op("MAD", 1, {denominator, noH, F32({1})});
    auto noL = e.op("MOV", 1, {noLRaw}, true);
    auto actualAlpha = e.per_component("RCP", e.per_component("RSQ", alphaSquared));
    auto noV = e.op("ADD", 1, {e.abs(noVRaw), F32({.00001})}, true);
    auto first = e.op("MAD", 1, {noL, e.neg(actualAlpha), noL});
    first = e.op("ADD", 1, {actualAlpha, first});
    auto second = e.op("MAD", 1, {e.neg(actualAlpha), noV, noV});
    first = e.op("MUL", 1, {noV, first});
    second = e.op("ADD", 1, {second, actualAlpha});
    auto visibilityDenominator = e.op("MAD", 1, {noL, second, first});
    auto distribution = e.op("DIV", 1, {alphaSquared, e.op("MUL", 1, {denominator, denominator})});
    auto visibility = e.op("DIV", 1, {F32({.5}), visibilityDenominator});
    auto scale = e.op("MUL", 1, {e.op("MUL", 1, {distribution, visibility}), F32({.318309844})});
    auto fresnel = e.op("MAD", 3, {specular, e.neg(fresnelPower), specular});
    auto grazingCorrection = e.op("MUL", 1, {e.swizzle(specular, "g"), F32({50})}, true);
    fresnel = e.op("MAD", 3, {grazingCorrection, fresnelPower, fresnel});
    auto energy = e.op("MUL", 1, {noL, occlusion});
    auto diffuseResult = e.op("MUL", 3, {e.op("MUL", 3, {diffuse, energy}), F32({.318309903})});
    auto capped = e.op("MIN", 1, {scale, specularCap});
    auto capEnabled = e.op("SGT", 1, {specularCap, F32({0})});
    auto selected = e.temp();
    auto condition = e.temp();
    e.emit("TRUNC.U.CC " + condition + ".x, " + Emitter::src(capEnabled, 1));
    e.emit("IF NE.x");
    e.emit("MOV.F32 " + selected + ".x, " + Emitter::src(capped, 1));
    e.emit("ELSE");
    e.emit("MOV.F32 " + selected + ".x, " + Emitter::src(scale, 1));
    e.emit("ENDIF");
    auto specularResult = e.op("MUL", 3, {e.op("MUL", 3, {e.op("MUL", 3, {{selected, 1}, fresnel}), noL}), occlusion});
    return {diffuseResult, specularResult};
}
// Opaque nearest-capture reflection composition after the explicit HDR sample.
// Captured sample, brightness, color scale and mixing parameters remain typed
// caller inputs; this helper never guesses a capture or sampler binding.
inline Value ReflectionMix(Emitter& e, Value capture, Value extraSky, Value coverage, Value mixing, Value roughness,
                           Value indirectToAverageRatio)
{
    auto amount = e.op("MAD", 1, {e.swizzle(mixing, "r"), roughness, e.swizzle(mixing, "g")}, true);
    auto smoothFactor = e.op("MAD", 1, {e.neg(amount), F32({2}), F32({3})});
    auto smooth = e.op("MUL", 1, {e.op("MUL", 1, {amount, amount}), smoothFactor});
    auto mixingRatio = e.op("MIN", 1, {indirectToAverageRatio, e.swizzle(mixing, "b")});
    auto blend = e.op("MAD", 1, {mixingRatio, smooth, e.neg(smooth)});
    auto radiance = e.op("MAD", 3, {blend, capture, capture});
    return e.op("MAD", 3, {coverage, extraSky, radiance});
}
inline Value ReflectionResponse(Emitter& e, Value radiance, Value specular, Value roughness, Value expNoV,
                                Value materialAO, Value screenAO, Value shadowAO, Value shadowWeight)
{
    radiance = e.op("MUL", 3, {radiance, EnvironmentBRDFExp(e, specular, roughness, expNoV)});
    radiance = e.op("MUL", 3, {radiance, materialAO});
    radiance = e.op("MUL", 3, {radiance, C4SpecularOcclusion(e, specular, screenAO)});
    auto shadow = e.op("MAD", 1, {e.neg(shadowWeight), shadowAO, shadowAO});
    shadow = e.op("ADD", 1, {shadowWeight, shadow});
    return e.op("MUL", 3, {radiance, shadow});
}
inline Value NearestReflection(Emitter& e, Value sample, Value brightness, Value colorScale, Value mixing,
                               Value specular, Value roughness, Value expNoV, Value materialAO, Value screenAO,
                               Value shadowAO, Value shadowWeight, Value indirectToAverageRatio)
{
    auto capture = e.op("MUL", 3, {sample, brightness});
    capture = e.op("MUL", 3, {capture, colorScale});
    auto radiance = ReflectionMix(e, capture, F32({0, 0, 0}), F32({0}), mixing, roughness, indirectToAverageRatio);
    return ReflectionResponse(e, radiance, specular, roughness, expNoV, materialAO, screenAO, shadowAO, shadowWeight);
}
// The captured policy projects world position to clip x/y/w.
// Row dimensions, depth parameters and resource availability are caller facts.
inline Value VolumetricFogCoordinates(Emitter& e, Value worldPosition, Value row0, Value row1, Value row2, Value row3,
                                      Value depthParameters, Value depthScale)
{
    auto projected = e.op("MUL", 3, {e.swizzle(worldPosition, "r"), e.swizzle(row0, "rga")});
    projected = e.op("MAD", 3, {e.swizzle(worldPosition, "g"), e.swizzle(row1, "rga"), projected});
    projected = e.op("MAD", 3, {e.swizzle(worldPosition, "b"), e.swizzle(row2, "rga"), projected});
    projected = e.op("ADD", 3, {e.swizzle(row3, "rga"), projected});
    auto logarithm =
        e.op("MAD", 1, {e.swizzle(projected, "b"), e.swizzle(depthParameters, "r"), e.swizzle(depthParameters, "g")});
    logarithm = e.per_component("LG2", logarithm);
    auto depth = e.op("MUL", 1, {logarithm, e.swizzle(depthParameters, "b")});
    depth = e.op("MUL", 1, {depth, depthScale});
    auto uv = e.op("DIV", 2, {e.swizzle(projected, "rg"), e.swizzle(projected, "b")});
    uv = e.op("MAD", 2, {uv, F32({.5, -.5}), F32({.5})});
    return e.append(uv, depth);
}
// A bounded standalone assembly interface for original-program block oracles.
// Inputs are explicit float4 words, not guessed game uniform offsets.
inline std::string BlockProgram(const std::string& block)
{
    Emitter e;
    auto input = [&](unsigned index, int components)
    {
        auto reg = e.temp();
        e.emit("LDC.F32X4 " + reg + ", buf0[" + std::to_string(index * 16) + "]");
        return Value{reg, components};
    };
    Value result = F32({0, 0, 0});
    if (block == "environment-brdf-exp")
    {
        auto a = input(0, 3), b = input(1, 1), c = input(2, 1);
        result = EnvironmentBRDFExp(e, a, b, c);
    }
    else if (block == "environment-brdf-parameters")
    {
        auto a = input(0, 1), b = input(1, 1);
        result = EnvironmentBRDFParameters(e, a, b);
    }
    else if (block == "environment-brdf")
    {
        auto a = input(0, 3), b = input(1, 1), c = input(2, 1);
        result = EnvironmentBRDF(e, a, b, c);
    }
    else if (block == "hq-lightmap")
    {
        auto a = input(0, 4), b = input(1, 4), c = input(2, 4), d = input(3, 4), f = input(4, 4), g = input(5, 4),
             n = input(6, 3);
        result = HighQualityLightmap(e, a, b, c, d, f, g, n);
    }
    else if (block == "distance-field-shadow")
    {
        auto a = input(0, 4), b = input(1, 4), c = input(2, 4);
        result = DistanceFieldShadow(e, a, b, c);
    }
    else if (block == "reflection-mip")
    {
        auto a = input(0, 1), b = input(1, 1);
        result = ReflectionMip(e, a, b);
    }
    else if (block == "fog-composite")
    {
        auto a = input(0, 3), b = input(1, 4), c = input(2, 1);
        result = FogComposite(e, a, b, c);
    }
    else if (block == "volumetric-fog")
    {
        auto a = input(0, 4), b = input(1, 4);
        result = ComposeVolumetricFog(e, a, b);
    }
    else if (block == "c4-specular-occlusion")
    {
        auto a = input(0, 3), b = input(1, 1);
        result = C4SpecularOcclusion(e, a, b);
    }
    else if (block == "directional-diffuse" || block == "directional-specular")
    {
        auto normal = input(0, 3), view = input(1, 3), light = input(2, 3), rough = input(3, 1), diffuse = input(4, 3),
             specular = input(5, 3), ao = input(6, 1), cap = input(7, 1);
        auto lit = DirectionalSurface(e, normal, view, light, rough, diffuse, specular, ao, cap);
        result = block == "directional-diffuse" ? lit.diffuse : lit.specular;
    }
    else if (block == "reflection-mix")
    {
        auto capture = input(0, 3), sky = input(1, 3), coverage = input(2, 1), mixing = input(3, 3),
             rough = input(4, 1), ratio = input(5, 1);
        result = ReflectionMix(e, capture, sky, coverage, mixing, rough, ratio);
    }
    else if (block == "reflection-response")
    {
        auto radiance = input(0, 3), specular = input(1, 3), rough = input(2, 1), exp = input(3, 1),
             materialAO = input(4, 1), screenAO = input(5, 1), shadowAO = input(6, 1), weight = input(7, 1);
        result = ReflectionResponse(e, radiance, specular, rough, exp, materialAO, screenAO, shadowAO, weight);
    }
    else if (block == "volumetric-fog-coordinates")
    {
        auto position = input(0, 3), row0 = input(1, 4), row1 = input(2, 4), row2 = input(3, 4), row3 = input(4, 4),
             depth = input(5, 3), scale = input(6, 1);
        result = VolumetricFogCoordinates(e, position, row0, row1, row2, row3, depth, scale);
    }
    else if (block == "nearest-reflection")
    {
        auto sample = input(0, 3), brightness = input(1, 1), color = input(2, 3), mixing = input(3, 3),
             specular = input(4, 3), rough = input(5, 1), exp = input(6, 1), materialAO = input(7, 1),
             screenAO = input(8, 1), shadowAO = input(9, 1), weight = input(10, 1);
        auto ratio = input(11, 1);
        result = NearestReflection(e, sample, brightness, color, mixing, specular, rough, exp, materialAO, screenAO,
                                   shadowAO, weight, ratio);
    }
    else
        throw std::runtime_error("unknown original lighting block");
    if (result.size == 4)
        e.emit("MOV.F32 result_color0, " + result.text);
    else if (result.size == 2)
    {
        e.emit("MOV.F32 result_color0.xy, " + result.text);
        e.emit("MOV.F32 result_color0.zw, {0, 0, 0, 0}");
    }
    else
    {
        e.emit("MOV.F32 result_color0.xyz, " + Emitter::src(result, 3));
        e.emit("MOV.F32 result_color0.w, {0, 0, 0, 0}.x");
    }
    return Resolve(e.program(), {}, {});
}
}
}
}

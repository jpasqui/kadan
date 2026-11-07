// Independently emitted original forward indirect-lighting producers.
// Exact source policy identities and resource layouts remain prerequisites;
// this stage does not authorize complete material or game export admission.
#pragma once
#include "custom_material_lightmap.h"
#include "material_engine_hashes.h"

namespace P4MapKit
{
namespace CustomMaterial
{
namespace Lighting
{
enum class IndirectKind
{
    None,
    Volumetric,
    HighQuality
};
enum class IndirectResponse
{
    OriginalSingle,
    OriginalTwoSidedVolumetric,
    OriginalTwoSidedSurface
};
struct IndirectPolicy
{
    IndirectKind kind;
    bool sky;
};
constexpr std::uint64_t IndirectPrimitiveNameHash = 0x60929d69929b2b59ull;
constexpr std::uint64_t IndirectViewNameHash = EngineHash::ViewUniformBuffer;
inline void RequireIndirectView(const std::map<std::uint16_t, std::uint64_t>& hashes)
{
    auto found = hashes.find(0);
    if (found == hashes.end() || found->second != IndirectViewNameHash)
        throw std::runtime_error("indirect View identity absent/unqualified");
    for (const auto& entry : hashes)
        if (entry.first != 0 && entry.second == IndirectViewNameHash)
            throw std::runtime_error("indirect View identity ambiguous");
}
inline void RequireIndirectPrimitive(const ShaderBindings& header, const std::map<std::uint16_t, std::uint64_t>& hashes)
{
    unsigned slot = 32;
    for (const auto& entry : hashes)
        if (entry.second == IndirectPrimitiveNameHash)
        {
            if (slot != 32)
                throw std::runtime_error("indirect Primitive identity ambiguous");
            slot = entry.first;
        }
    if (slot == 32)
        throw std::runtime_error("indirect Primitive identity absent");
    if (slot != 1 || slot >= header.layout_hashes.size() || header.layout_hashes[slot] != 0 ||
        (header.resource_bits & (1u << slot)))
        throw std::runtime_error("indirect Primitive source binding unqualified");
}
inline IndirectPolicy OriginalIndirectPolicy(std::uint64_t hash)
{
    switch (hash)
    {
        case 0x4507f03a199c6526ull:
            return {IndirectKind::None, false};
        case 0xa8d53bcb8fca168dull:
            return {IndirectKind::None, true};
        case 0x666403914542b619ull:
            return {IndirectKind::Volumetric, false};
        case 0x18a82ebde5d3d122ull:
            return {IndirectKind::Volumetric, true};
        case 0x6d90922c199fe04full:
        case 0x28b73d7ae30b556cull:
            return {IndirectKind::HighQuality, false};
        case 0xdbbfd1d594df3611ull:
        case 0x661e2d85a2c9dbe6ull:
            return {IndirectKind::HighQuality, true};
        default:
            throw std::runtime_error("indirect source policy identity unqualified");
    }
}
struct IndirectEmitter
{
    PolicyEmitter& policy;
    std::set<unsigned> storage;
    explicit IndirectEmitter(PolicyEmitter& output) : policy(output)
    {
    }
    Value SkyWord(unsigned offset, int size)
    {
        if (offset > 96 || offset % 16 || size < 1 || size > 4)
            throw std::runtime_error("sky irradiance word range");
        auto slot = policy.bindings.RequireSRV("SkyIrradiance");
        storage.insert(slot);
        auto result = policy.emitter.temp();
        policy.emitter.emit("LDB.F32X4 " + result + "." + Comp(size) + ", sbo_buf" + std::to_string(slot) + "[" +
                            std::to_string(offset) + "]");
        return {result, size};
    }
    std::string Program() const
    {
        auto text = policy.Program();
        auto at = text.find("ATTRIB fragment_position");
        if (at == std::string::npos)
            throw std::runtime_error("indirect emitter declaration boundary missing");
        std::string declarations;
        for (auto slot : storage)
            declarations +=
                "STORAGE sbo_buf" + std::to_string(slot) + "[] = { program.storage[" + std::to_string(slot) + "] };\n";
        text.insert(at, declarations);
        return text;
    }
};
inline void IndirectCondition(Emitter& emitter, Value condition)
{
    if (condition.size != 1)
        throw std::runtime_error("indirect condition shape");
    auto reg = emitter.temp();
    emitter.emit("TRUNC.U.CC " + reg + ".x, " + Emitter::src(condition, 1));
    emitter.emit("IF NE.x");
}
inline Value IndirectSample(PolicyEmitter& policy, unsigned texture, unsigned sampler, Value coordinates,
                            bool integerFetch = false)
{
    if (texture > 63 || sampler > 31 || coordinates.size != 3)
        throw std::runtime_error("indirect sample binding/shape");
    auto& emitter = policy.emitter;
    emitter.uses_texture = true;
    emitter.emit("LDC.U64 D1.x, buf14[" + std::to_string(1352 + 8 * sampler) + "]");
    emitter.emit("LDC.U64 D0.x, buf14[" + std::to_string(328 + 8 * texture) + "]");
    emitter.emit("OR.U64 D0.x, D0, D1");
    auto words = emitter.append(coordinates, F32({0}));
    auto sample = emitter.temp();
    emitter.emit(std::string(integerFetch ? "TXF.U " : "TXL.F ") + sample + ", " + words.text + ", handle(D0.x), 3D");
    emitter.handle.clear();
    return {sample, 4};
}
inline Value IndirectVolumeSample(PolicyEmitter& policy, const std::string& role, Value coordinates)
{
    return IndirectSample(policy, policy.bindings.RequireTexture(role),
                          policy.bindings.RequireSampler("SharedBilinearClamped"), coordinates);
}
inline Value VolumeCoordinates(PolicyEmitter& policy, Value worldPosition, const ShaderBindings& header)
{
    if (worldPosition.size != 3 || header.default_sampler < 0 || header.default_sampler > 31)
        throw std::runtime_error("volume indirection input/default sampler unqualified");
    auto& emitter = policy.emitter;
    auto scale = policy.Load(0, 3872, 3), add = policy.Load(0, 3888, 3), size = policy.Load(0, 3904, 3);
    auto uv = emitter.op("MIN", 3, {emitter.op("MAD", 3, {worldPosition, scale, add}), F32({.99})}, true);
    auto cell = emitter.op("MUL", 3, {uv, size});
    auto integer = emitter.temp();
    emitter.emit("TRUNC.S " + integer + ".xyz, " + cell.text);
    auto fetched = IndirectSample(policy, policy.bindings.RequireTexture("VolumeIndirection"),
                                  unsigned(header.default_sampler), {integer, 3}, true);
    auto entry = emitter.temp();
    emitter.emit("I2F.U " + entry + ", " + fetched.text);
    auto brick = policy.Load(0, 3916, 1), base = emitter.op("MAD", 3, {Value{entry, 3}, brick, Value{entry, 3}});
    auto local = emitter.per_component("FRC", emitter.op("DIV", 3, {cell, emitter.swizzle({entry, 4}, "a")}));
    auto position = emitter.op("MAD", 3, {local, brick, base});
    position = emitter.op("ADD", 3, {position, F32({.5})});
    return emitter.op("MUL", 3, {position, policy.Load(0, 3920, 3)});
}
struct VolumeBasis
{
    Value linear, quadratic, difference;
};
inline VolumeBasis VolumeIrradianceBasis(Emitter& emitter, Value normal)
{
    if (normal.size != 3)
        throw std::runtime_error("volume normal shape");
    auto x = emitter.swizzle(normal, "r"), y = emitter.swizzle(normal, "g"), z = emitter.swizzle(normal, "b");
    auto xy = emitter.op("MUL", 1, {x, y}), yz = emitter.op("MUL", 1, {y, z}), xz = emitter.op("MUL", 1, {x, z});
    auto z2 = emitter.op("MUL", 1, {z, z}), triple = emitter.op("MUL", 1, {z2, F32({3})});
    auto zonal = emitter.op("MAD", 1, {triple, F32({.315391988}), F32({-.315391988})});
    auto qxy = emitter.op("MUL", 1, {xy, F32({1.09254801})}), qyz = emitter.op("MUL", 1, {yz, F32({-1.09254801})}),
         qxz = emitter.op("MUL", 1, {xz, F32({-1.09254801})});
    auto quadratic =
        emitter.op("MUL", 4, {emitter.append(emitter.append(qxy, qyz), emitter.append(zonal, qxz)), F32({.785398185})});
    auto linear = emitter.op("MUL", 3, {emitter.swizzle(normal, "gbr"), F32({-.488602996, .488602996, -.488602996})});
    linear = emitter.op("MUL", 3, {linear, F32({2.09439492})});
    linear = emitter.append(F32({.886227489}), linear);
    auto squareX = emitter.op("MUL", 1, {x, x}), squareY = emitter.op("MUL", 1, {y, y});
    auto difference = emitter.op("MUL", 1, {emitter.sub(1, squareX, squareY), F32({.429042608})});
    return {linear, quadratic, difference};
}
inline Value DecodeVolumeChannel(Emitter& emitter, Value ambient, Value lowSample, Value highSample,
                                 const VolumeBasis& basis)
{
    if (ambient.size != 1 || lowSample.size != 4 || highSample.size != 4)
        throw std::runtime_error("volume coefficient shape");
    auto decode = [&](Value sample, Value factors)
    {
        auto product = emitter.op("MUL", 4, {sample, ambient});
        auto signedValue = emitter.op("MAD", 4, {product, F32({2}), emitter.neg(ambient)});
        return emitter.op("MUL", 4, {signedValue, factors});
    };
    auto low = decode(lowSample, F32({1.73205101, 1.73205101, 1.73205101, 3.87297893}));
    auto high = decode(highSample, F32({3.87297893, 4.47213888, 3.87297893, 3.87297893}));
    auto quadratic = emitter.append(emitter.swizzle(low, "a"), emitter.swizzle(high, "rgb"));
    auto linear = emitter.append(ambient, emitter.swizzle(low, "rgb"));
    auto second = emitter.dot(4, quadratic, basis.quadratic), first = emitter.dot(4, linear, basis.linear);
    return emitter.op("MAD", 1, {basis.difference, emitter.swizzle(high, "a"), emitter.op("ADD", 1, {first, second})});
}
struct VolumeCoefficients
{
    Value ambient;
    std::array<Value, 6> samples;
};
inline VolumeCoefficients SampleVolumeCoefficients(PolicyEmitter& policy, Value coordinates,
                                                   IndirectResponse response = IndirectResponse::OriginalSingle)
{
    VolumeCoefficients coefficients;
    coefficients.ambient = IndirectVolumeSample(policy, "VolumeAmbient", coordinates);
    const auto order = response != IndirectResponse::OriginalSingle ? std::array<unsigned, 6>{{5, 4, 1, 0, 2, 3}}
                                                                    : std::array<unsigned, 6>{{5, 4, 2, 3, 0, 1}};
    for (unsigned i : order)
        coefficients.samples[i] = IndirectVolumeSample(policy, "VolumeSH" + std::to_string(i), coordinates);
    return coefficients;
}
inline Value VolumeIrradiance(PolicyEmitter& policy, const VolumeCoefficients& coefficients, Value normal,
                              unsigned colorOffset = 2976)
{
    if (colorOffset != 2976 && colorOffset != 2992)
        throw std::runtime_error("volume indirect color scale unqualified");
    auto& emitter = policy.emitter;
    auto basis = VolumeIrradianceBasis(emitter, normal);
    std::array<Value, 3> channels;
    for (unsigned c = 0; c < 3; ++c)
        channels[c] = DecodeVolumeChannel(emitter, emitter.swizzle(coefficients.ambient, std::string(1, "rgb"[c])),
                                          coefficients.samples[2 * c], coefficients.samples[2 * c + 1], basis);
    auto value = emitter.append(emitter.append(channels[0], channels[1]), channels[2]);
    value = emitter.op("MAX", 3, {value, F32({0})});
    value = emitter.op("MUL", 3, {value, policy.Load(0, colorOffset, 3)});
    return emitter.op("MUL", 3, {value, F32({.318309844})});
}
inline Value ApplyHQIndirectShaping(PolicyEmitter& policy, Value decoded)
{
    if (decoded.size != 3)
        throw std::runtime_error("HQ indirect decoded shape");
    return policy.emitter.op("MUL", 3, {decoded, policy.Load(0, 2960, 3)});
}
struct HQIndirectPair
{
    Value front, back;
};
inline HQIndirectPair SampleIndirectHQ(PolicyEmitter& policy, const HQBindings& bindings, Value lightmapUV,
                                       Value normal, bool sky, IndirectResponse response)
{
    if (lightmapUV.size != 2 || normal.size != 3)
        throw std::runtime_error("HQ indirect stage shape");
    auto& emitter = policy.emitter;
    auto firstUV = emitter.op("MUL", 2, {lightmapUV, F32({1, .5})}),
         secondUV = emitter.op("ADD", 2, {firstUV, F32({0, .5})});
    const bool twoSided = response != IndirectResponse::OriginalSingle;
    Value first, second;
    if (sky != twoSided)
    {
        first = HQSample(policy, bindings, firstUV);
        second = HQSample(policy, bindings, secondUV);
    }
    else
    {
        second = HQSample(policy, bindings, secondUV);
        first = HQSample(policy, bindings, firstUV);
    }
    auto scale0 = policy.Load(bindings.parameters, 64, 4), scale1 = policy.Load(bindings.parameters, 80, 4),
         add0 = policy.Load(bindings.parameters, 96, 4), add1 = policy.Load(bindings.parameters, 112, 4);
    auto front = HighQualityLightmap(emitter, first, second, scale0, add0, scale1, add1, normal);
    Value back = F32({0, 0, 0});
    if (twoSided)
        back = HighQualityLightmap(emitter, first, second, scale0, add0, scale1, add1, emitter.neg(normal));
    return {front, back};
}
struct IndirectBentNormal
{
    Value direction, visibility, weight;
};
inline IndirectBentNormal BentNormal(PolicyEmitter& policy, Value normal, Value sample, bool volume,
                                     IndirectResponse response = IndirectResponse::OriginalSingle)
{
    auto& emitter = policy.emitter;
    if (normal.size != 3 || sample.size != 4)
        throw std::runtime_error("indirect bent normal shape");
    auto unpacked = emitter.op("MAD", 3, {emitter.swizzle(sample, "rgb"), F32({2}), F32({-1})});
    auto squared = emitter.dot(3, unpacked, unpacked);
    Value visibility, direction;
    if (volume)
    {
        auto length = emitter.per_component("RCP", emitter.per_component("RSQ", squared));
        visibility = response != IndirectResponse::OriginalSingle
                         ? length
                         : emitter.op("MAX", 1, {length, policy.Load(0, 2576, 1)});
        direction = emitter.op("DIV", 3, {unpacked, emitter.op("MAX", 1, {length, F32({.0001})})});
    }
    else
    {
        auto alpha = emitter.swizzle(sample, "a"), squaredAlpha = emitter.op("MUL", 1, {alpha, alpha});
        visibility = response != IndirectResponse::OriginalSingle
                         ? squaredAlpha
                         : emitter.op("MAX", 1, {squaredAlpha, policy.Load(0, 2576, 1)});
        direction = emitter.op("MUL", 3, {emitter.per_component("RSQ", squared), unpacked});
    }
    auto complement = emitter.sub(1, F32({1}), visibility);
    auto smooth = emitter.op("MAD", 1, {emitter.neg(visibility), complement, complement});
    smooth = emitter.sub(1, F32({1}), smooth);
    auto dot = emitter.dot(3, normal, direction);
    dot = emitter.op("MOV", 1, {dot}, true);
    auto result = emitter.op("MAD", 3, {smooth, emitter.sub(3, normal, direction), direction});
    auto correction = emitter.op("MAD", 1, {smooth, emitter.neg(dot), smooth});
    return {result, visibility, emitter.op("ADD", 1, {dot, correction})};
}
struct SkyCoefficients
{
    std::array<Value, 7> words;
};
inline SkyCoefficients ReadSkyCoefficients(IndirectEmitter& context)
{
    SkyCoefficients result;
    for (unsigned i = 0; i < 7; ++i)
        result.words[i] = context.SkyWord(i * 16, i == 6 ? 3 : 4);
    return result;
}
inline Value SkyIrradiance(IndirectEmitter& context, const SkyCoefficients& coefficients, Value direction)
{
    if (direction.size != 3)
        throw std::runtime_error("sky irradiance direction shape");
    auto& emitter = context.policy.emitter;
    auto vector = emitter.append(direction, F32({1}));
    std::array<Value, 3> linear, quadratic;
    auto x = emitter.swizzle(direction, "r"), y = emitter.swizzle(direction, "g");
    auto products = emitter.op("MUL", 4, {emitter.swizzle(direction, "rgbb"), emitter.swizzle(direction, "gbbr")});
    for (unsigned c = 0; c < 3; ++c)
    {
        linear[c] = emitter.dot(4, coefficients.words[c], vector);
        quadratic[c] = emitter.dot(4, coefficients.words[3 + c], products);
    }
    auto difference = emitter.op("MAD", 1, {x, x, emitter.neg(emitter.op("MUL", 1, {y, y}))});
    auto sum = emitter.op("ADD", 3,
                          {emitter.append(emitter.append(linear[0], linear[1]), linear[2]),
                           emitter.append(emitter.append(quadratic[0], quadratic[1]), quadratic[2])});
    auto irradiance = emitter.op("MAD", 3, {coefficients.words[6], difference, sum});
    irradiance = emitter.op("MAX", 3, {irradiance, F32({0})});
    return emitter.op("MUL", 3, {irradiance, context.policy.Load(0, 3488, 3)});
}
struct IndirectInputs
{
    Value worldPosition, normal, lightmapUV;
};
struct IndirectResult
{
    Value irradiance, diffuseDirection, skyVisibility, directionalShadow, brightness, coordinates, backIrradiance;
    bool hasBack, hasIrradianceProducer, hasBrightnessProducer, hasDirectionalShadowProducer;
};
inline IndirectResult EmitIndirectPolicy(IndirectEmitter& context, const ShaderBindings& header,
                                         const std::map<std::uint16_t, std::uint64_t>& uniformHashes,
                                         const IndirectPolicy& descriptor, const IndirectInputs& input,
                                         IndirectResponse response = IndirectResponse::OriginalSingle)
{
    if (input.worldPosition.size != 3 || input.normal.size != 3 || input.lightmapUV.size != 2)
        throw std::runtime_error("indirect policy input shape");
    if (response != IndirectResponse::OriginalSingle && response != IndirectResponse::OriginalTwoSidedVolumetric &&
        response != IndirectResponse::OriginalTwoSidedSurface)
        throw std::runtime_error("indirect source response unqualified");
    if (descriptor.kind != IndirectKind::None && descriptor.kind != IndirectKind::Volumetric &&
        descriptor.kind != IndirectKind::HighQuality)
        throw std::runtime_error("indirect source policy kind unqualified");
    const bool twoSided = response != IndirectResponse::OriginalSingle;
    if (response == IndirectResponse::OriginalTwoSidedVolumetric && descriptor.kind != IndirectKind::Volumetric)
        throw std::runtime_error("two-sided volumetric source policy unqualified");
    RequireIndirectView(uniformHashes);
    auto& policy = context.policy;
    auto& emitter = policy.emitter;
    Value coordinates = F32({0, 0, 0}), irradiance = F32({0, 0, 0}), back = F32({0, 0, 0}), shadow = F32({1});
    HQBindings hq{};
    if (descriptor.kind != IndirectKind::HighQuality)
    {
        RequireIndirectPrimitive(header, uniformHashes);
        if (descriptor.kind == IndirectKind::Volumetric)
            coordinates = VolumeCoordinates(policy, input.worldPosition, header);
        auto shadowResult = emitter.temp(), coordinateResult = emitter.temp();
        emitter.emit("MOV.F32 " + shadowResult + ".x, {1, 0, 0, 0}.x");
        emitter.emit("MOV.F32 " + coordinateResult + ".xyz, " + coordinates.text);
        IndirectCondition(emitter, emitter.op("SGT", 1, {policy.Load(1, 328, 1), F32({0})}));
        if (descriptor.kind != IndirectKind::Volumetric)
        {
            coordinates = VolumeCoordinates(policy, input.worldPosition, header);
            emitter.emit("MOV.F32 " + coordinateResult + ".xyz, " + coordinates.text);
        }
        auto sampledShadow = IndirectVolumeSample(policy, "VolumeDirectionalShadow", coordinates);
        emitter.emit("MOV.F32 " + shadowResult + ".x, " + sampledShadow.text + ".x");
        emitter.emit("ENDIF");
        shadow = {shadowResult, 1};
        coordinates = {coordinateResult, 3};
    }
    if (descriptor.kind == IndirectKind::Volumetric)
    {
        auto coefficients = SampleVolumeCoefficients(policy, coordinates, response);
        irradiance = VolumeIrradiance(policy, coefficients, input.normal);
        if (twoSided)
            back = VolumeIrradiance(policy, coefficients, emitter.neg(input.normal), 2992);
    }
    else if (descriptor.kind == IndirectKind::HighQuality)
    {
        hq = ReadHQBindings(header, uniformHashes);
        auto pair = SampleIndirectHQ(policy, hq, input.lightmapUV, input.normal, descriptor.sky, response);
        irradiance = ApplyHQIndirectShaping(policy, pair.front);
        if (twoSided)
            back = ApplyHQIndirectShaping(policy, pair.back);
    }
    IndirectBentNormal bent{input.normal, F32({1}), F32({1})};
    if (descriptor.sky && descriptor.kind != IndirectKind::None)
    {
        auto direction = emitter.temp(), visibility = emitter.temp(), weight = emitter.temp();
        emitter.emit("MOV.F32 " + direction + ".xyz, " + input.normal.text);
        emitter.emit("MOV.F32 " + visibility + ".x, {1, 0, 0, 0}.x");
        emitter.emit("MOV.F32 " + weight + ".x, {1, 0, 0, 0}.x");
        IndirectCondition(emitter, emitter.op("SNE", 1, {policy.Load(0, 3476, 1), F32({0})}));
        Value sample;
        if (descriptor.kind == IndirectKind::Volumetric)
            sample = IndirectVolumeSample(policy, "VolumeBentNormal", coordinates);
        else
        {
            auto texture = ExactResourceOrdinal(ReadResourceTable(header.textures), hq.cluster, 1, 63);
            sample = HQSample(policy, {hq.parameters, hq.cluster, texture, hq.sampler}, input.lightmapUV);
        }
        auto adjusted = BentNormal(policy, input.normal, sample, descriptor.kind == IndirectKind::Volumetric, response);
        emitter.emit("MOV.F32 " + direction + ".xyz, " + adjusted.direction.text);
        emitter.emit("MOV.F32 " + visibility + ".x, " + Emitter::src(adjusted.visibility, 1));
        emitter.emit("MOV.F32 " + weight + ".x, " + Emitter::src(adjusted.weight, 1));
        emitter.emit("ENDIF");
        bent = {{direction, 3}, {visibility, 1}, {weight, 1}};
    }
    if (descriptor.sky)
    {
        auto coefficients = ReadSkyCoefficients(context);
        auto sky = SkyIrradiance(context, coefficients, bent.direction);
        if (descriptor.kind == IndirectKind::None)
            irradiance = sky;
        else
            irradiance = emitter.op("MAD", 3, {sky, emitter.op("MUL", 1, {bent.visibility, bent.weight}), irradiance});
        if (twoSided)
        {
            auto backSky = SkyIrradiance(context, coefficients, emitter.neg(input.normal));
            back = descriptor.kind == IndirectKind::None ? backSky
                                                         : emitter.op("MAD", 3, {backSky, bent.visibility, back});
        }
    }
    auto brightness = emitter.dot(3, irradiance, F32({.300000012, .589999974, .109999999}));
    const bool hasIrradiance = descriptor.kind != IndirectKind::None || descriptor.sky;
    return {irradiance,
            bent.direction,
            bent.visibility,
            shadow,
            brightness,
            coordinates,
            back,
            twoSided && hasIrradiance,
            hasIrradiance,
            descriptor.kind != IndirectKind::None,
            descriptor.kind != IndirectKind::HighQuality};
}
inline std::string IndirectStageProgram(const ShaderBindings& header,
                                        const std::map<std::uint16_t, std::uint64_t>& uniformHashes,
                                        std::uint64_t typeHash, const ReservedDiagnosticBuffer& input,
                                        const std::string& output,
                                        IndirectResponse response = IndirectResponse::OriginalSingle)
{
    ValidateDiagnosticBuffer(header, input);
    auto descriptor = OriginalIndirectPolicy(typeHash);
    Emitter emitter;
    PolicyEmitter policy(emitter, header);
    IndirectEmitter context(policy);
    IndirectInputs inputs{policy.Load(input.index, 0, 3), policy.Load(input.index, 16, 3),
                          policy.Load(input.index, 32, 2)};
    auto made = EmitIndirectPolicy(context, header, uniformHashes, descriptor, inputs, response);
    Value result;
    if (output == "irradiance")
        result = made.irradiance;
    else if (output == "brightness")
        result = made.brightness;
    else if (output == "coordinates")
        result = made.coordinates;
    else if (output == "direction")
        result = made.diffuseDirection;
    else if (output == "visibility")
        result = made.skyVisibility;
    else if (output == "shadow")
        result = made.directionalShadow;
    else if (output == "back_irradiance" && made.hasBack)
        result = made.backIrradiance;
    else
        throw std::runtime_error("indirect output unavailable");
    result = emitter.op("MOV", 3, {result});
    emitter.emit("MOV.F32 result_color0, " + emitter.append(result, F32({1})).text);
    return Resolve(context.Program(), {}, {});
}
}
}
}

// Original opaque forward pixel inputs, emitted from explicit checked metadata.
// Material, decal and lighting composition retain separate source contracts.
#pragma once
#include "material_forward_lighting.h"
#include "material_engine_hashes.h"
namespace P4MapKit
{
namespace CustomMaterial
{
namespace Lighting
{
struct ForwardGeometryResult
{
    Value translatedWorld, worldPosition, view, sceneDepth, pixelXY, screenPosition, bufferUV;
    Value lightmapUV{"", 0}, shadowUV{"", 0}, vertexFog;
    Value capsuleMultiplier;
    bool hasLightmapUV = false;
    ForwardWords primitiveLightingChannels, primitiveFlags, capsuleSelfShadowEnabled;
};
inline void RequireOriginalForwardGeometryBindings(const ShaderBindings& header,
                                                   const std::map<std::uint16_t, std::uint64_t>& hashes)
{
    const auto view = hashes.find(0), primitive = hashes.find(1);
    if (hashes.size() > 32 || view == hashes.end() || view->second != EngineHash::ViewUniformBuffer ||
        primitive == hashes.end() || primitive->second != 0x60929d69929b2b59ull)
        throw std::runtime_error("forward geometry View/Primitive identity absent or unqualified");
    for (const auto& item : hashes)
        if ((item.first != 0 && item.second == view->second) || (item.first != 1 && item.second == primitive->second))
            throw std::runtime_error("forward geometry View/Primitive identity ambiguous");
    if (header.layout_hashes.size() < 2 || header.layout_hashes[1] || (header.resource_bits & 2))
        throw std::runtime_error("forward geometry Primitive layout unqualified");
    for (const auto* table : {&header.textures, &header.samplers, &header.srv, &header.uav})
    {
        const auto resources = ReadResourceTable(*table);
        const auto found = resources.find(1);
        if (found != resources.end() && !found->second.empty())
            throw std::runtime_error("forward geometry Primitive resources unqualified");
    }
}
inline int OriginalForwardVarying(const ShaderBindings& header, const std::string& name, bool required)
{
    if (header.inputs.size() > 32)
        throw std::runtime_error("forward geometry varying count");
    int location = -1;
    for (const auto& varying : header.inputs)
    {
        const auto canonical = varying.name.substr(0, varying.name.find("_centroid"));
        if (canonical != name)
            continue;
        if (varying.location < 0 || varying.location > 127 || (location >= 0 && location != varying.location))
            throw std::runtime_error("forward geometry varying identity conflict: " + name);
        location = varying.location;
    }
    if (required && location < 0)
        throw std::runtime_error("forward geometry varying absent: " + name);
    return location;
}
inline Value OriginalForwardAttribute(Emitter& emitter, int location)
{
    if (location < 0 || location > 127)
        throw std::runtime_error("forward geometry attribute range");
    const auto result = emitter.temp();
    emitter.emit("MOV.F32 " + result + ", fragment.attrib[" + std::to_string(location) + "]");
    return {result, 4};
}
inline ForwardGeometryResult EmitOriginalForwardGeometry(PolicyEmitter& policy, const ShaderBindings& header,
                                                         const std::map<std::uint16_t, std::uint64_t>& uniformHashes)
{
    RequireOriginalForwardGeometryBindings(header, uniformHashes);
    auto& emitter = policy.emitter;
    emitter.uses_primitive = true;
    ForwardGeometryResult result;
    const auto fogLocation = OriginalForwardVarying(header, "in_texcoord7", true);
    const auto lightmapLocation = OriginalForwardVarying(header, "in_texcoord4", false);
    const auto row0 = policy.Load(0, 704, 4), row1 = policy.Load(0, 720, 4), row2 = policy.Load(0, 736, 4),
               row3 = policy.Load(0, 752, 4);
    const auto homogeneous = emitter.temp();
    emitter.emit("MUL.F32 " + homogeneous + ", fragment.position.x, " + row0.text);
    emitter.emit("MAD.F32 " + homogeneous + ", fragment.position.y, " + row1.text + ", " + homogeneous);
    emitter.emit("MAD.F32 " + homogeneous + ", fragment.position.z, " + row2.text + ", " + homogeneous);
    emitter.emit("ADD.F32 " + homogeneous + ", " + row3.text + ", " + homogeneous);
    const auto translated = emitter.temp();
    emitter.emit("DIV.F32 " + translated + ".xyz, " + homogeneous + ", " + homogeneous + ".w");
    result.translatedWorld = {translated, 3};
    const auto length = emitter.temp(), inverse = emitter.temp();
    emitter.emit("DP3.F32 " + length + ".x, -" + translated + ", -" + translated);
    emitter.emit("RSQ.F32 " + inverse + ".x, " + length + ".x");
    const auto translation = policy.Load(0, 1120, 3);
    result.worldPosition = emitter.op("ADD", 3, {result.translatedWorld, emitter.neg(translation)});
    result.view = emitter.op("MUL", 3, {{inverse, 1}, emitter.neg(result.translatedWorld)});
    const auto rect = policy.Load(0, 2080, 2), size = policy.Load(0, 2096, 4);
    result.pixelXY = emitter.op("ADD", 2, {{"fragment.position", 4}, emitter.neg(rect)});
    auto ndc = emitter.op("MAD", 2, {result.pixelXY, emitter.swizzle(size, "ba"), F32({-.5})});
    ndc = emitter.op("MUL", 2, {ndc, F32({2, -2})});
    const auto clip = emitter.append(ndc, F32({1}));
    const auto screen = emitter.temp();
    emitter.emit("DIV.F32 " + screen + ".xyz, " + clip.text + ", fragment.position.w");
    result.screenPosition = {screen, 3};
    const auto depth = emitter.temp();
    emitter.emit("RCP.F32 " + depth + ".x, fragment.position.w");
    result.sceneDepth = {depth, 1};
    const auto bias = policy.Load(0, 1056, 4);
    auto uv = emitter.op("DIV", 2,
                         {emitter.swizzle(result.screenPosition, "rg"), emitter.swizzle(result.screenPosition, "b")});
    uv = emitter.op("MAD", 2, {uv, emitter.swizzle(bias, "rg"), emitter.swizzle(bias, "ab")});
    const auto limits = policy.Load(0, 2144, 4);
    uv = emitter.op("MIN", 2, {emitter.swizzle(limits, "ba"), uv});
    result.bufferUV = emitter.op("MAX", 2, {emitter.swizzle(limits, "rg"), uv});
    result.vertexFog = OriginalForwardAttribute(emitter, fogLocation);
    if (lightmapLocation >= 0)
    {
        const auto coords = OriginalForwardAttribute(emitter, lightmapLocation);
        result.lightmapUV = emitter.swizzle(coords, "rg");
        result.shadowUV = emitter.swizzle(coords, "ba");
        result.hasLightmapUV = true;
    }
    result.primitiveLightingChannels = ForwardWordLoad(policy, 1, 380, 1);
    const auto encoded = policy.Load(1, 324, 1), scaled = emitter.op("MUL", 1, {encoded, F32({3.99900007})});
    const auto packed = emitter.temp();
    emitter.emit("TRUNC.U " + packed + ".x, " + Emitter::src(scaled, 1));
    result.primitiveFlags = {packed, 1};
    const auto bit = ForwardWordOp(emitter, "AND.U", 1, {result.primitiveFlags, ForwardWords{"{2, 0, 0, 0}", 1}});
    result.capsuleSelfShadowEnabled = ForwardWordOp(emitter, "SNE.U", 1, {bit, ForwardWords{"{0, 0, 0, 0}", 1}});
    const auto condition = emitter.temp(), multiplier = emitter.temp();
    emitter.emit("SNE.U.CC " + condition + ".x, " + bit.text + ".x, {0, 0, 0, 0}.x");
    emitter.emit("IF NE.x");
    const auto intensity = policy.Load(0, 3636, 1);
    emitter.emit("MOV.F32 " + multiplier + ".x, " + Emitter::src(intensity, 1));
    emitter.emit("ELSE");
    emitter.emit("MOV.F32 " + multiplier + ".x, {1, 0, 0, 0}.x");
    emitter.emit("ENDIF");
    result.capsuleMultiplier = {multiplier, 1};
    return result;
}
inline void InstallOriginalForwardGeometryCache(Emitter& emitter, const ForwardGeometryResult& geometry)
{
    const std::vector<std::pair<std::string, Value>> entries = {
        {"tw", geometry.translatedWorld}, {"wp", geometry.worldPosition}, {"cv", geometry.view}};
    for (const auto& item : entries)
    {
        if (item.second.size != 3 || item.second.text.empty())
            throw std::runtime_error("forward geometry cache value shape");
        const auto prior = emitter.cache.find(item.first);
        if (prior != emitter.cache.end() && !(prior->second == item.second))
            throw std::runtime_error("forward geometry cache already emitted: " + item.first);
    }
    for (const auto& item : entries)
        emitter.cache[item.first] = item.second;
}
inline std::string ForwardGeometryBlockProgram(const ShaderBindings& header, const std::string& output,
                                               const std::map<std::uint16_t, std::uint64_t>& uniformHashes)
{
    Emitter emitter;
    PolicyEmitter policy(emitter, header);
    const auto geometry = EmitOriginalForwardGeometry(policy, header, uniformHashes);
    InstallOriginalForwardGeometryCache(emitter, geometry);
    Value result{"", 0};
    if (output == "world")
        result = geometry.worldPosition;
    else if (output == "translated")
        result = geometry.translatedWorld;
    else if (output == "view")
        result = geometry.view;
    else if (output == "depth")
        result = geometry.sceneDepth;
    else if (output == "pixel")
        result = geometry.pixelXY;
    else if (output == "screen")
        result = geometry.screenPosition;
    else if (output == "buffer-uv")
        result = geometry.bufferUV;
    else if (output == "fog")
        result = geometry.vertexFog;
    else if (output == "capsule-multiplier")
        result = geometry.capsuleMultiplier;
    else if (output == "lightmap" || output == "shadow")
    {
        if (!geometry.hasLightmapUV)
            throw std::runtime_error("forward geometry lightmap varying absent");
        result = output == "lightmap" ? geometry.lightmapUV : geometry.shadowUV;
    }
    else if (output == "channels" || output == "flags" || output == "capsule")
    {
        const auto word = output == "channels" ? geometry.primitiveLightingChannels
                          : output == "flags"  ? geometry.primitiveFlags
                                               : geometry.capsuleSelfShadowEnabled;
        const auto converted = emitter.temp();
        emitter.emit("I2F.U " + converted + ".x, " + word.text + ".x");
        result = {converted, 1};
    }
    else
        throw std::runtime_error("forward geometry output not supported");
    std::map<std::string, int> varyings;
    for (const auto& varying : header.inputs)
    {
        const auto name = varying.name.substr(0, varying.name.find("_centroid"));
        const auto prior = varyings.find(name);
        if (prior != varyings.end() && prior->second != varying.location)
            throw std::runtime_error("forward geometry varying identity conflict: " + name);
        varyings[name] = varying.location;
    }
    emitter.emit("MOV.F32 result_color0." + Comp(result.size) + ", " + Emitter::src(result, result.size));
    return Resolve(policy.Program(), {}, varyings);
}
}
}
}

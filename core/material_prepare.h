// Native original pixel-program preparation from the user's NVN payload.
// This reads embedded NVASM and structural bindings; it does not prove a vertex
// factory's input interpretation, shader family completeness, or compiler match.
#pragma once
#include "material_bind.h"
#include "material_engine_hashes.h"
#include "material_graph.h"
#include "material_shader_parameters.h"
#include "custom_material_bindings.h"
#include <sstream>
#include <iomanip>
namespace P4MapKit
{
namespace Material
{
// Exact hashed semantic names from the engine uniform-buffer declarations.
inline const std::map<std::uint64_t, std::string>& MaterialKnownBufferNames()
{
    static const std::map<std::uint64_t, std::string> names = {{0x11ff15aa33b66366ull, "PrimitiveDither"},
                                                               {0x198d8af2b3acf133ull, "LightmapResourceCluster"},
                                                               {0x1ba949e325161e48ull, "MaterialCollection1"},
                                                               {0x1f60f64a6c34c969ull, "PlanarReflectionStruct"},
                                                               {0x23b8d21894310793ull, "MobileBasePass"},
                                                               {0x2a1964235ae8eb20ull, "DrawRectangleParameters"},
                                                               {0x2bb777d8c448c5a9ull, "Landscape"},
                                                               {0x2c25853e3e50b771ull, "InstancedStaticMeshVF"},
                                                               {0x325f1bb90b36e5b7ull, "MaterialCollection2"},
                                                               {0x365a69720e7582b9ull, "PrecomputedLightingBuffer"},
                                                               {0x36c018814eba0c73ull, "SpeedTreeData"},
                                                               {0x3ed39ae099ca3b3dull, "GPUSkinAPEXCloth"},
                                                               {0x40580199bb817992ull, "LandscapeFixedGrid"},
                                                               {0x426d1240410b8fa2ull, "LocalVF"},
                                                               {0x44b21151854eccc3ull, "ForwardLightData"},
                                                               {0x464d5173dc80b663ull, "Material"},
                                                               {0x4b9128ed9a2936d8ull, "MaterialCollection0"},
                                                               {0x4ffc0f8f46aab3daull, "ShadowDepthPass"},
                                                               {0x556b984992829372ull, "FogStruct"},
                                                               {0x57bf33ef55bbbf78ull, "InstancedVF"},
                                                               {0x60929d69929b2b59ull, "Primitive"},
                                                               {0x7cf243dd8b26d506ull, "PrimitiveFade"},
                                                               {0x9f079ec4ea0e07fcull, "TranslucentBasePass"},
                                                               {0xa1220657911b1226ull, "SceneTexturesStruct"},
                                                               {0xa53d18d8cf4a7978ull, "VolumetricCloud"},
                                                               {0xa9ed34ac5c5a35abull, "DistanceFieldObjectBuffers"},
                                                               {0xacc984ec4397b8baull, "DepthPass"},
                                                               {0xadc2e4ecc44e7b8bull, "SkyAtmosphere"},
                                                               {0xbcfa67715e1a66d6ull, "MaterialCollection3"},
                                                               {0xcafbf523acdd8b5cull, "IndirectLightingCache"},
                                                               {0xd1ccebfe9e528e18ull, "InstancedView"},
                                                               {0xdb2cde9471cc30d3ull, "OpaqueBasePass"},
                                                               {0xdf91c3f6bbe11f25ull, "LandscapeParameters"},
                                                               {0xe17ce853d932ca36ull, "MobileSceneTextures"},
                                                               {0xe8a8b5ab6b98b6c9ull, "DeferredLightUniforms"},
                                                               {EngineHash::ViewUniformBuffer, "View"},
                                                               {0xf135e29855d4d0beull, "ReflectionCapture"}};
    return names;
}
struct MaterialPreparedProgram
{
    std::vector<std::string> Lines;
    BindingContext BaseBinding;
    std::map<int, std::string> SamplerParameters;
    CustomMaterial::ShaderBindings Header;
    FMaterialShaderReference Reference;
    bool FactoryInputsProved = false, FamilyComplete = false, CompilerProven = false;
};
inline std::vector<std::string> MaterialInstructionLines(const std::string& program)
{
    if (program.size() > 1024u * 1024u || !JsonDetail::ValidUtf8(program))
        throw Refused("original shader text is not bounded UTF-8", "material program text");
    std::vector<std::string> lines;
    std::istringstream input(program);
    std::string line;
    while (std::getline(input, line))
    {
        line = trim(line);
        while (!line.empty() && line.back() == ';')
            line.pop_back();
        line = trim(line);
        if (line.empty() || line == "END")
            continue;
        bool declaration = false;
        for (const auto* prefix :
             {"!!", "OPTION", "CBUFFER", "ATTRIB", "TEMP", "LONG TEMP", "SHORT TEMP", "OUTPUT", "PARAM"})
            declaration = declaration || starts(line, prefix);
        if (declaration)
            continue;
        if (line.find('{') != std::string::npos &&
            (line.find("VAR:") != std::string::npos || line.find("TEX:") != std::string::npos ||
             line.find("SAMP:") != std::string::npos || line.find("LAST:") != std::string::npos))
            throw Refused("original shader still contains unresolved operands", "material program unresolved");
        if (line.size() > 65536 || lines.size() >= 100000)
            throw Refused("original shader instruction budget exceeded", "material program budget");
        lines.push_back(std::move(line));
    }
    if (lines.empty())
        throw Refused("original shader has no executable instructions", "material program empty");
    return lines;
}
inline std::string ReadOriginalMaterialProgramText(const CustomMaterial::Bytes& payload,
                                                   const CustomMaterial::ShaderBindings& header)
{
    // ReadPixelBindings has already validated the entire payload and tail.
    const auto headerSize = CustomMaterial::ShaderHeader(header).size();
    std::size_t at = headerSize;
    const auto require = [&](std::size_t n)
    {
        if (at > payload.size() || n > payload.size() - at)
            throw Refused("original program payload is truncated", "material program payload");
    };
    require(4);
    const auto codeSize = CustomMaterial::Read32(payload, at);
    at += 4;
    require(codeSize);
    at += codeSize;
    require(4);
    const auto controlSize = CustomMaterial::Read32(payload, at);
    at += 4;
    require(controlSize);
    if (controlSize < 28)
        throw Refused("original shader control header is truncated", "material program control");
    const auto textAt = CustomMaterial::Read32(payload, at + 20), textSize = CustomMaterial::Read32(payload, at + 24);
    if (textAt < 28 || textAt > controlSize || textSize > controlSize - textAt || textSize > 1024u * 1024u)
        throw Refused("embedded NVASM offset or size is outside the control block", "material program control");
    const auto begin = payload.begin() + at + textAt, end = begin + textSize;
    const auto nul = std::find(begin, end, std::uint8_t(0));
    std::string text(begin, nul);
    if (!JsonDetail::ValidUtf8(text))
        throw Refused("embedded NVASM is not valid UTF-8", "material program text");
    for (const auto c : text)
        if ((static_cast<unsigned char>(c) < 32 && c != '\n' && c != '\r' && c != '\t') || c == 127)
            throw Refused("embedded NVASM contains an invalid control character", "material program text");
    return text;
}
inline MaterialPreparedProgram PrepareOriginalMaterialPixelProgram(const FMaterialShaderMapEnvelope& map,
                                                                   const FMaterialShaderReference& reference,
                                                                   const CustomMaterial::Bytes& payload,
                                                                   std::uint8_t archiveFrequency)
{
    if (reference.Frequency != 3 || archiveFrequency != reference.Frequency)
        throw Refused("shader archive frequency differs from the pixel reference",
                      "material program archive association");
    FMaterialShaderParameters checked;
    FReadError error;
    if (!ReadMaterialShaderParameters(map, reference, checked, error))
        throw Refused(error.Code, "material shader reference");
    MaterialPreparedProgram result;
    result.Reference = reference;
    std::string failure;
    try
    {
        result.Header = CustomMaterial::ReadPixelBindings(payload);
    }
    catch (const std::exception& e)
    {
        failure = e.what();
    }
    if (!failure.empty())
        throw Refused(failure, "material program payload");
    result.Lines = MaterialInstructionLines(ReadOriginalMaterialProgramText(payload, result.Header));
    auto& context = result.BaseBinding;
    context.Uniforms = map.Uniforms;
    for (const auto& buffer : reference.UniformBufferHashes)
    {
        if (buffer.first > 31)
            throw Refused("material uniform buffer is outside the native cache", "material program buffer");
        const auto named = MaterialKnownBufferNames().find(buffer.second);
        if (named != MaterialKnownBufferNames().end())
            context.Buffers[int(buffer.first)] = named->second;
        else
        {
            std::ostringstream hash;
            hash << std::hex << std::setfill('0') << std::setw(16) << buffer.second;
            context.Buffers[int(buffer.first)] = hash.str();
        }
    }
    for (const auto& buffer : reference.NamedBuffers)
    {
        if (buffer.first > 31)
            throw Refused("named material buffer is outside the native cache", "material program buffer");
        const auto previous = context.Buffers.find(int(buffer.first));
        if (previous != context.Buffers.end() && previous->second != buffer.second)
        {
            const auto hashed = reference.UniformBufferHashes.find(buffer.first);
            const bool known =
                hashed != reference.UniformBufferHashes.end() && MaterialKnownBufferNames().count(hashed->second);
            const bool passAlias = buffer.second == "Pass" &&
                                   IsMember(previous->second, {"OpaqueBasePass", "TranslucentBasePass",
                                                               "MobileBasePass", "DepthPass", "ShadowDepthPass"});
            if (known && !passAlias)
                throw Refused("typed uniform buffer semantics conflict with hashed metadata",
                              "material program buffer");
        }
        context.Buffers[int(buffer.first)] = buffer.second;
    }
    for (const auto& varying : result.Header.inputs)
    {
        const auto name = varying.name.substr(0, varying.name.find("_centroid"));
        const auto previous = context.Varyings.find(name);
        if (previous != context.Varyings.end() && previous->second != varying.location)
            throw Refused("centroid aliases disagree about varying location", "material program varying");
        context.Varyings[name] = varying.location;
    }
    const auto uniqueBuffer = [&](const std::string& name)
    {
        int found = -1;
        for (const auto& buffer : context.Buffers)
            if (buffer.second == name)
            {
                if (found != -1)
                    throw Refused("more than one buffer has the same semantic name: " + name,
                                  "material program buffer");
                found = buffer.first;
            }
        return found;
    };
    const auto material = uniqueBuffer("Material"), view = uniqueBuffer("View");
    CustomMaterial::ResourceMap textures, srvs, samplers;
    try
    {
        textures = CustomMaterial::ReadResourceTable(result.Header.textures);
        srvs = CustomMaterial::ReadResourceTable(result.Header.srv);
        samplers = CustomMaterial::ReadResourceTable(result.Header.samplers);
    }
    catch (const std::exception& e)
    {
        failure = e.what();
    }
    if (!failure.empty())
        throw Refused(failure, "material program resources");
    if (material >= 0)
    {
        for (const auto& table : {textures, srvs})
        {
            const auto rows = table.find(unsigned(material));
            if (rows == table.end())
                continue;
            for (const auto& row : rows->second)
            {
                const auto entry = std::size_t(row.first / 2);
                if (entry >= map.Uniforms.Textures.size())
                    continue;
                const auto slot = int(row.second);
                const auto previous = context.TextureEntries.find(slot);
                if (previous != context.TextureEntries.end() && previous->second != entry)
                    throw Refused("material texture slot names multiple frozen entries",
                                  "material program texture association");
                context.TextureEntries[slot] = entry;
                const auto& parameter = map.Uniforms.Textures[entry].Parameter;
                context.TextureParameters[slot] = parameter.NameIsNone ? "?" : parameter.Name;
            }
        }
        const auto rows = samplers.find(unsigned(material));
        if (rows != samplers.end())
            for (const auto& row : rows->second)
            {
                const auto entry = std::size_t(row.first / 2);
                if (entry >= map.Uniforms.Textures.size())
                    continue;
                context.Samplers.insert(int(row.second));
                const auto& parameter = map.Uniforms.Textures[entry].Parameter;
                result.SamplerParameters[int(row.second)] = parameter.NameIsNone ? "?" : parameter.Name;
            }
    }
    context.ViewSamplersKnown = true;
    if (view >= 0)
    {
        const auto rows = samplers.find(unsigned(view));
        if (rows != samplers.end())
            for (const auto& row : rows->second)
            {
                const auto previous = context.ViewSamplers.find(int(row.second));
                if (previous != context.ViewSamplers.end() && previous->second != int(row.first))
                    throw Refused("View sampler slot has multiple semantic resources",
                                  "material program sampler association");
                context.ViewSamplers[int(row.second)] = int(row.first);
            }
    }
    return result;
}
}
}

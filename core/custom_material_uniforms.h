// Creator-owned material uniform expressions. Names and defaults belong to the
// creator; shader-map edits relocate the original descriptor in the running
// game.
#pragma once
#include "custom_material_texture_list.h"
#include <cstring>
namespace P4MapKit
{
namespace CustomMaterial
{
struct UniformParameter
{
    std::string name;
    std::vector<double> defaults;
};
struct OwnedUniforms
{
    std::vector<UniformParameter> scalars, vectors;
    std::vector<TextureParameter> textures;
    std::map<std::string, std::pair<unsigned, int>> bindings;
};
inline OwnedUniforms GraphUniforms(const Json& graph, const std::vector<TextureParameter>& textures)
{
    OwnedUniforms result;
    result.textures = textures;
    std::map<std::string, UniformParameter> scalars, vectors;
    std::map<std::string, std::string> names;
    auto admit = [&](const std::string& name, const std::string& kind)
    {
        auto folded = name;
        std::transform(folded.begin(), folded.end(), folded.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        if (!Plain(name, 127) || folded == "none" || NumericNameSuffix(name))
            throw std::runtime_error("owned parameter name invalid");
        auto old = names.find(folded);
        if (old != names.end() && (old->second != kind + ":" + name))
            throw std::runtime_error("owned parameter name collision");
        names[folded] = kind + ":" + name;
    };
    for (const auto& texture : textures)
        admit(texture.name, "texture");
    for (const auto& node : Get(graph, "nodes").object)
    {
        const auto& value = node.second;
        const auto type = Text(value, "type");
        if (type != "ScalarParameter" && type != "VectorParameter")
            continue;
        auto& props = Get(value, "props");
        const auto name = Text(props, "ParameterName");
        auto defaults = type == "ScalarParameter" ? std::vector<double>{Number(props, "DefaultValue")}
                                                  : Numbers(Get(props, "DefaultValue"), {0, 0, 0, 1});
        const auto size = type == "ScalarParameter" ? 1u : 4u;
        if (defaults.size() != size)
            throw std::runtime_error("owned parameter default shape");
        for (auto v : defaults)
            if (!std::isfinite(v) || !std::isfinite(float(v)))
                throw std::runtime_error("owned parameter default not finite float");
        admit(name, type);
        auto& into = type == "ScalarParameter" ? scalars : vectors;
        auto old = into.find(name);
        if (old != into.end() && old->second.defaults != defaults)
            throw std::runtime_error("owned parameter defaults disagree");
        into[name] = {name, defaults};
    }
    if (scalars.size() > 256 || vectors.size() > 256 || textures.size() > 25)
        throw std::runtime_error("owned uniform count limit");
    for (const auto& p : vectors)
    {
        result.bindings[p.first] = {unsigned(16 * result.vectors.size()), 4};
        result.vectors.push_back(p.second);
    }
    for (const auto& p : scalars)
    {
        result.bindings[p.first] = {unsigned(16 * result.vectors.size() + 4 * result.scalars.size()), 1};
        result.scalars.push_back(p.second);
    }
    return result;
}
inline TextureListResult ReplaceOwnedUniforms(const P4MapKit::FMaterialShaderMapEnvelope& source,
                                              const OwnedUniforms& own, bool PreserveUnusedCollections = false)
{
    if (source.Frozen.size() > (1u << 18) || source.Uniforms.ExternalTextureCount ||
        source.Uniforms.VirtualStackCount || (!PreserveUnusedCollections && !source.Uniforms.Collections.empty()))
        throw std::runtime_error("owned uniform donor uses unsupported external resources");
    for (const auto& t : source.Uniforms.Textures)
        if (t.TextureType)
            throw std::runtime_error("owned uniform donor has non-2D texture expressions");
    P4MapKit::FMaterialFrozenReader layoutReader(source.Frozen);
    std::size_t resourceAt = 0, resourceCount = 0;
    if (!layoutReader.Array(352, 4, resourceAt, resourceCount))
        throw std::runtime_error(layoutReader.Error.Code);
    const auto originalLayout =
        UniformLayout(unsigned(source.Uniforms.VectorPrograms.size()), unsigned(source.Uniforms.ScalarPrograms.size()),
                      unsigned(source.Uniforms.Textures.size()));
    if (originalLayout.first != Read32(source.Frozen, 336) || resourceCount != originalLayout.second.size())
        throw std::runtime_error("owned uniform source layout mismatch");
    for (std::size_t i = 0; i < resourceCount; ++i)
    {
        const auto at = resourceAt + 4 * i;
        const unsigned offset = unsigned(source.Frozen.at(at)) | (unsigned(source.Frozen.at(at + 1)) << 8);
        if (originalLayout.second[i] != std::make_pair(offset, unsigned(source.Frozen.at(at + 2))))
            throw std::runtime_error("owned uniform source resource mismatch");
    }
    if (LayoutHash(originalLayout.first, source.Frozen.at(340), originalLayout.second) != Read32(source.Frozen, 472))
        throw std::runtime_error("owned uniform source layout hash mismatch");
    if (source.ScriptNameIdentities.size() != source.ScriptNames.size())
        throw std::runtime_error("owned uniform source raw names missing");
    FrozenSections image(source.Frozen);
    std::set<std::size_t> changed;
    for (unsigned field : {112u, 128u, 144u, 160u, 176u, 288u, 352u})
        if (image.pointers.count(field))
            changed.insert(image.pointers.at(field));
    Bytes scalarBytes, vectorBytes, textureBytes, scalarPrograms, vectorPrograms, programData, resourceBytes;
    auto param = [&](Bytes& data, const UniformParameter& p)
    {
        data.resize(data.size() + 12, 0);
        U32(data, 0xffffffff);
        data.push_back(2);
        data.resize(data.size() + 3, 0);
        for (auto value : p.defaults)
        {
            float f = float(value);
            std::uint32_t bits;
            std::memcpy(&bits, &f, 4);
            U32(data, bits);
        }
    };
    for (const auto& p : own.scalars)
    {
        param(scalarBytes, p);
        U32(scalarPrograms, unsigned(programData.size()));
        U32(scalarPrograms, 3);
        programData.push_back(3);
        U16(programData, unsigned((scalarPrograms.size() / 8) - 1));
    }
    for (const auto& p : own.vectors)
    {
        param(vectorBytes, p);
        U32(vectorPrograms, unsigned(programData.size()));
        U32(vectorPrograms, 3);
        programData.push_back(4);
        U16(programData, unsigned((vectorPrograms.size() / 8) - 1));
    }
    for (std::size_t i = 0; i < own.textures.size(); ++i)
    {
        Bytes entry(12, 0);
        U32(entry, 0xffffffff);
        entry.push_back(2);
        entry.resize(20, 0);
        U32(entry, unsigned(i));
        entry.push_back(own.textures[i].sampler_source);
        entry.push_back(255);
        entry.resize(28, 0);
        Append(textureBytes, entry);
    }
    const auto layout =
        UniformLayout(unsigned(own.vectors.size()), unsigned(own.scalars.size()), unsigned(own.textures.size()));
    const unsigned staticSlot = source.Frozen.at(340);
    if (layout.first > 65535)
        throw std::runtime_error("owned uniform layout too large");
    for (const auto& r : layout.second)
    {
        U16(resourceBytes, r.first);
        resourceBytes.push_back(std::uint8_t(r.second));
        resourceBytes.push_back(0);
    }
    image.replace_or_insert_array(112, std::move(vectorPrograms), unsigned(own.vectors.size()), 4);
    image.replace_or_insert_array(128, std::move(scalarPrograms), unsigned(own.scalars.size()), 4);
    image.replace_or_insert_array(144, std::move(scalarBytes), unsigned(own.scalars.size()), 4);
    image.replace_or_insert_array(160, std::move(vectorBytes), unsigned(own.vectors.size()), 4);
    image.replace_or_insert_array(176, std::move(textureBytes), unsigned(own.textures.size()), 4);
    image.replace_or_insert_array(288, std::move(programData), unsigned(3 * (own.scalars.size() + own.vectors.size())),
                                  1);
    image.replace_or_insert_array(352, std::move(resourceBytes), unsigned(layout.second.size()), 2);
    Set32(image.sections[0].bytes, 336, layout.first);
    TextureListResult result;
    result.old_count = unsigned(source.Uniforms.Textures.size());
    result.uniform_size = layout.first;
    result.layout_hash = LayoutHash(layout.first, staticSlot, layout.second);
    Set32(image.sections[0].bytes, 472, result.layout_hash);
    result.frozen = image.write();
    result.edits.old_size = unsigned(source.Frozen.size());
    result.edits.new_size = unsigned(result.frozen.size());
    std::size_t made = 0;
    for (const auto& section : image.sections)
    {
        const auto at = image.relocated.at(section.old_start);
        if (at > made)
            result.edits.operations.push_back({false, 0, 0, Bytes(at - made, 0)});
        std::size_t j = 0;
        while (j < section.bytes.size())
        {
            const auto isCopy = [&](std::size_t x)
            {
                return x < section.old_size && section.old_start < source.Frozen.size() &&
                       result.frozen[at + x] == source.Frozen[section.old_start + x];
            };
            const bool copy = isCopy(j);
            const auto begin = j++;
            while (j < section.bytes.size() && isCopy(j) == copy)
                ++j;
            if (copy)
                result.edits.operations.push_back({true, unsigned(section.old_start + begin), unsigned(j - begin), {}});
            else
                result.edits.operations.push_back(
                    {false, 0, 0, Bytes(result.frozen.begin() + at + begin, result.frozen.begin() + at + j)});
        }
        made = at + section.bytes.size();
    }
    std::map<std::pair<std::string, std::uint32_t>, std::vector<std::uint32_t>> names;
    for (const auto& original : source.ScriptNameIdentities)
    {
        if (original.second.second < 0)
            throw std::runtime_error("negative donor name number");
        auto owner = image.sections.at(image.owner(original.first)).old_start;
        if (changed.count(owner))
            continue;
        names[{original.second.first, unsigned(original.second.second)}].push_back(
            unsigned(image.relocate(original.first)));
    }
    auto addNames = [&](unsigned field, const auto& params, unsigned stride)
    {
        if (params.empty())
            return;
        const auto at = image.relocated.at(image.pointers.at(field));
        for (std::size_t i = 0; i < params.size(); ++i)
            names[{params[i].name, 0}].push_back(unsigned(at + i * stride));
    };
    addNames(144, own.scalars, 24);
    addNames(160, own.vectors, 36);
    addNames(176, own.textures, 28);
    for (const auto& name : names)
        result.edits.names.push_back({name.first.first, name.first.second, name.second});
    Bytes checked;
    WriteEdits(checked, result.edits);
    // Read back the exact frozen image and names through the engine-layout
    // reader.
    std::map<std::size_t, std::string> emittedNames;
    for (const auto& name : result.edits.names)
        for (auto offset : name.offsets)
        {
            auto text = name.name;
            if (name.number)
                text += "_" + std::to_string(name.number - 1);
            emittedNames[offset] = text;
        }
    P4MapKit::FMaterialUniformSet decoded;
    P4MapKit::FReadError error;
    if (!P4MapKit::ReadMaterialUniformSet(result.frozen, emittedNames, decoded, error))
        throw std::runtime_error("owned uniform readback: " + error.Code);
    if (decoded.Collections != source.Uniforms.Collections)
        throw std::runtime_error("owned uniform changed retained collection identities");
    if (decoded.Scalars.size() != own.scalars.size() || decoded.Vectors.size() != own.vectors.size() ||
        decoded.ScalarPrograms.size() != own.scalars.size() || decoded.VectorPrograms.size() != own.vectors.size() ||
        decoded.Textures.size() != own.textures.size())
        throw std::runtime_error("owned uniform readback count");
    for (std::size_t i = 0; i < own.scalars.size(); ++i)
        if (decoded.Scalars[i].Name != own.scalars[i].name || decoded.ScalarPrograms[i].Instructions.size() != 1 ||
            decoded.ScalarPrograms[i].Instructions[0].Opcode != P4MapKit::EMaterialPreshaderOpcode::ScalarParameter ||
            decoded.ScalarPrograms[i].Instructions[0].Parameter != i)
            throw std::runtime_error("owned scalar readback association");
    for (std::size_t i = 0; i < own.vectors.size(); ++i)
        if (decoded.Vectors[i].Name != own.vectors[i].name || decoded.VectorPrograms[i].Instructions.size() != 1 ||
            decoded.VectorPrograms[i].Instructions[0].Opcode != P4MapKit::EMaterialPreshaderOpcode::VectorParameter ||
            decoded.VectorPrograms[i].Instructions[0].Parameter != i)
            throw std::runtime_error("owned vector readback association");
    const auto CheckDefaults = [](const auto& Parameters, const auto& Decoded)
    {
        for (std::size_t i = 0; i < Parameters.size(); ++i)
        {
            if (Decoded[i].LayerIndex != -1 || Decoded[i].Association != 2 || Decoded[i].NameIsNone)
                throw std::runtime_error("owned uniform parameter association/default identity");
            for (std::size_t j = 0; j < Parameters[i].defaults.size(); ++j)
            {
                float Value = float(Parameters[i].defaults[j]);
                std::uint32_t Bits;
                std::memcpy(&Bits, &Value, 4);
                if (Decoded[i].DefaultBits[j] != Bits)
                    throw std::runtime_error("owned uniform changed authored default bits");
            }
        }
    };
    CheckDefaults(own.scalars, decoded.Scalars);
    CheckDefaults(own.vectors, decoded.Vectors);
    return result;
}
} // namespace CustomMaterial
} // namespace P4MapKit

// Change only the frozen material's texture-expression and resource arrays.
// Existing sections are relocated with the original FMemoryImage alignments.
// The set copies original ranges at runtime and stores only changed metadata.
#pragma once
#include "custom_material_bindings.h"
#include "material_shader_map.h"
#include "custom_material_frozen.h"
namespace P4MapKit
{
namespace CustomMaterial
{
struct TextureParameter
{
    std::string name;
    std::uint8_t sampler_source = 0;
};
struct TextureListResult
{
    ContentEdits edits;
    std::uint32_t layout_hash = 0, uniform_size = 0;
    unsigned old_count = 0;
    Bytes frozen;
};
inline std::uint32_t LayoutHash(std::uint32_t size, unsigned staticSlot,
                                const std::vector<std::pair<unsigned, unsigned>>& resources)
{
    std::uint32_t h = (size << 16) | staticSlot;
    for (auto r : resources)
        h ^= r.first;
    std::size_t n = resources.size();
    while (n >= 4)
        for (unsigned s : {0u, 8u, 16u, 24u})
            h ^= resources[--n].second << s;
    while (n >= 2)
        for (unsigned s : {0u, 16u})
            h ^= resources[--n].second << s;
    while (n)
        h ^= resources[--n].second;
    return h;
}
inline std::pair<std::uint32_t, std::vector<std::pair<unsigned, unsigned>>>
UniformLayout(unsigned vectorPrograms, unsigned scalarPrograms, unsigned textures)
{
    unsigned at = 16 * vectorPrograms + 16 * ((scalarPrograms + 3) / 4);
    std::vector<std::pair<unsigned, unsigned>> resources;
    for (unsigned i = 0; i < textures; i++)
    {
        resources.push_back({at, 5});
        resources.push_back({at + 8, 8});
        at += 16;
    }
    resources.push_back({at, 8});
    resources.push_back({at + 8, 8});
    return {at + 16, resources};
}
inline void U64(Bytes& b, std::uint64_t x)
{
    for (int i = 0; i < 8; i++)
        b.push_back(std::uint8_t(x >> (8 * i)));
}
inline Bytes FrozenArray(std::size_t field, std::size_t target, unsigned count)
{
    Bytes out;
    U64(out, count ? ((std::uint64_t(target - field) << 1) | 1) : 0);
    U32(out, count);
    U32(out, count);
    return out;
}
inline bool NumericNameSuffix(const std::string& s)
{
    auto p = s.find_last_of('_');
    if (p == std::string::npos || p == 0 || p + 1 == s.size())
        return false;
    auto tail = s.substr(p + 1);
    if (tail.size() > 1 && tail[0] == '0')
        return false;
    for (char c : tail)
        if (c < '0' || c > '9')
            return false;
    try
    {
        return std::stoull(tail) < 0x7fffffffu;
    }
    catch (...)
    {
        return false;
    }
}
inline TextureListResult
ReplaceTextureList(const P4MapKit::FMaterialShaderMapEnvelope& source, const std::vector<TextureParameter>& textures,
                   const std::map<std::size_t, std::pair<std::string, std::uint32_t>>& nameIdentities)
{
    if (textures.size() > 25 || source.Frozen.size() > (1u << 18))
        throw std::runtime_error("texture list limit");
    if (source.Uniforms.ExternalTextureCount || source.Uniforms.VirtualStackCount)
        throw std::runtime_error("external/virtual material textures unavailable");
    for (auto& t : source.Uniforms.Textures)
        if (t.TextureType != 0)
            throw std::runtime_error("non-2D material texture unavailable");
    std::set<std::string> names;
    for (auto& t : textures)
    {
        auto lower = t.name;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](char c) { return char(std::tolower(static_cast<unsigned char>(c))); });
        if (!Plain(t.name, 127) || NumericNameSuffix(t.name) || !names.insert(lower).second || t.sampler_source > 2)
            throw std::runtime_error("texture parameter identity invalid");
    }
    P4MapKit::FMaterialFrozenReader reader(source.Frozen);
    std::size_t oldTextureAt = 0, oldCount = 0, oldResourcesAt = 0, oldResourcesCount = 0;
    if (!reader.Array(176, 28, oldTextureAt, oldCount) || !reader.Array(352, 4, oldResourcesAt, oldResourcesCount))
        throw std::runtime_error(reader.Error.Code);
    if (oldCount && textures.empty())
        throw std::runtime_error("cannot remove every template texture");
    if (nameIdentities.size() != source.ScriptNames.size())
        throw std::runtime_error("raw script name identities required for texture edits");
    for (auto& p : source.ScriptNames)
        if (!nameIdentities.count(p.first))
            throw std::runtime_error("raw script name identity missing");
    auto expected = UniformLayout(unsigned(source.Uniforms.VectorPrograms.size()),
                                  unsigned(source.Uniforms.ScalarPrograms.size()), unsigned(oldCount));
    if (expected.first != Read32(source.Frozen, 336) || expected.second.size() != oldResourcesCount)
        throw std::runtime_error("template material uniform layout mismatch");
    for (std::size_t i = 0; i < oldResourcesCount; i++)
    {
        unsigned at = oldResourcesAt + 4 * i;
        unsigned offset = source.Frozen[at] | unsigned(source.Frozen[at + 1]) << 8, kind = source.Frozen[at + 2];
        if (expected.second[i] != std::make_pair(offset, kind))
            throw std::runtime_error("template material resource layout mismatch");
    }
    unsigned staticSlot = source.Frozen[340];
    if (LayoutHash(expected.first, staticSlot, expected.second) != Read32(source.Frozen, 472))
        throw std::runtime_error("template material layout hash mismatch");
    auto want = UniformLayout(unsigned(source.Uniforms.VectorPrograms.size()),
                              unsigned(source.Uniforms.ScalarPrograms.size()), unsigned(textures.size()));
    if (want.first > 65535)
        throw std::runtime_error("custom material uniform layout too large");
    TextureListResult result;
    result.old_count = unsigned(oldCount);
    result.uniform_size = want.first;
    result.layout_hash = LayoutHash(want.first, staticSlot, want.second);
    FrozenSections image(source.Frozen);
    Bytes textureBytes, resourceBytes;
    for (std::size_t i = 0; i < textures.size(); i++)
    {
        Bytes entry(12, 0);
        U32(entry, 0xffffffff);
        entry.push_back(2);
        entry.resize(20, 0);
        U32(entry, unsigned(i));
        entry.push_back(textures[i].sampler_source);
        entry.push_back(255);
        entry.resize(28, 0);
        Append(textureBytes, entry);
    }
    for (auto& r : want.second)
    {
        U16(resourceBytes, r.first);
        resourceBytes.push_back(std::uint8_t(r.second));
        resourceBytes.push_back(0);
    }
    image.replace_array(176, textureBytes, unsigned(textures.size()));
    image.replace_array(352, resourceBytes, unsigned(want.second.size()));
    Set32(image.sections[0].bytes, 336, want.first);
    Set32(image.sections[0].bytes, 472, result.layout_hash);
    result.frozen = image.write();
    if (result.frozen.size() > (1u << 18))
        throw std::runtime_error("custom frozen description size limit");
    result.edits.old_size = unsigned(source.Frozen.size());
    result.edits.new_size = unsigned(result.frozen.size());
    // Produce copy/own runs per original section. Unchanged program metadata and
    // engine defaults are copied from the player's material, never stored here.
    std::size_t made = 0;
    for (auto& section : image.sections)
    {
        auto newAt = image.relocated.at(section.old_start);
        if (newAt > made)
            result.edits.operations.push_back({false, 0, 0, Bytes(newAt - made, 0)});
        std::size_t j = 0;
        while (j < section.bytes.size())
        {
            bool copy = j < section.old_size && result.frozen[newAt + j] == source.Frozen[section.old_start + j];
            auto start = j++;
            while (j < section.bytes.size() &&
                   (j < section.old_size && result.frozen[newAt + j] == source.Frozen[section.old_start + j]) == copy)
                ++j;
            if (copy)
                result.edits.operations.push_back({true, unsigned(section.old_start + start), unsigned(j - start), {}});
            else
                result.edits.operations.push_back(
                    {false, 0, 0, Bytes(result.frozen.begin() + newAt + start, result.frozen.begin() + newAt + j)});
        }
        made = newAt + section.bytes.size();
    }
    std::map<std::pair<std::string, std::uint32_t>, std::vector<std::uint32_t>> grouped;
    for (auto& p : nameIdentities)
    {
        if (p.first >= oldTextureAt && p.first < oldTextureAt + oldCount * 28)
        {
            if ((p.first - oldTextureAt) % 28)
                throw std::runtime_error("shared texture name span unavailable");
            continue;
        }
        if (p.first > source.Frozen.size() || source.Frozen.size() - p.first < 12)
            throw std::runtime_error("name identity outside template image");
        grouped[p.second].push_back(unsigned(image.relocate(p.first)));
    }
    auto textureAt = image.relocated.at(oldTextureAt);
    for (std::size_t i = 0; i < textures.size(); i++)
        grouped[{textures[i].name, 0}].push_back(unsigned(textureAt + 28 * i));
    for (auto& p : grouped)
        result.edits.names.push_back({p.first.first, p.first.second, p.second});
    Bytes check;
    WriteEdits(check, result.edits);

    return result;
}
}
}

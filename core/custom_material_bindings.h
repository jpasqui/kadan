// Read only structural resource/varying metadata from the game's pixel payload.
// Original program bytes are skipped; emitted shaders are built from new code.
#pragma once
#include "custom_material_shader_set.h"
namespace P4MapKit
{
namespace CustomMaterial
{
class BindingReader
{
    const Bytes& data;
    std::size_t at = 0;

public:
    explicit BindingReader(const Bytes& b) : data(b)
    {
    }
    void need(std::size_t n)
    {
        if (at > data.size() || n > data.size() - at)
            throw std::runtime_error("truncated pixel binding metadata");
    }
    std::uint8_t u8()
    {
        need(1);
        return data[at++];
    }
    unsigned u16()
    {
        unsigned a = u8();
        return a | unsigned(u8()) << 8;
    }
    std::uint32_t u32()
    {
        need(4);
        auto n = Read32(data, at);
        at += 4;
        return n;
    }
    std::uint32_t count(std::uint32_t limit)
    {
        auto n = u32();
        if (n > limit)
            throw std::runtime_error("pixel binding count limit");
        return n;
    }
    void skip(std::size_t n)
    {
        need(n);
        at += n;
    }
    std::size_t offset() const
    {
        return at;
    }
    std::vector<PackedArray> packed()
    {
        std::vector<PackedArray> v;
        auto n = count(64);
        for (unsigned i = 0; i < n; i++)
        {
            PackedArray a;
            a.size = u16();
            a.type_name = u8();
            a.type_index = u8();
            v.push_back(a);
        }
        return v;
    }
    std::vector<Varying> varyings()
    {
        std::vector<Varying> v;
        auto n = count(32);
        std::set<std::string> used;
        for (unsigned i = 0; i < n; i++)
        {
            auto size = count(128);
            if (!size)
                throw std::runtime_error("empty varying");
            need(size);
            std::string name;
            for (unsigned j = 0; j < size; j++)
            {
                char c = char(u8());
                if (j + 1 == size)
                {
                    if (c)
                        throw std::runtime_error("varying missing terminator");
                }
                else
                {
                    if (c < 32 || c > 126)
                        throw std::runtime_error("invalid varying name");
                    name += c;
                }
            }
            auto location = u32();
            if (location > 127 || !used.insert(name).second)
                throw std::runtime_error("invalid varying location");
            v.push_back({name, std::int32_t(location)});
        }
        return v;
    }
    std::vector<std::uint32_t> words()
    {
        std::vector<std::uint32_t> v;
        auto n = count(65536);
        need(std::size_t(n) * 4);
        for (unsigned i = 0; i < n; i++)
            v.push_back(u32());
        return v;
    }
};
inline ShaderBindings ReadPixelBindings(const Bytes& payload)
{
    if (payload.size() > (1u << 20) || payload.size() < 4 || Read32(payload, payload.size() - 4) != 4)
        throw std::runtime_error("pixel payload optional data unsupported");
    BindingReader r(payload);
    ShaderBindings h;
    if (r.u32() != 0x474c534c || r.u16() != 0x5053)
        throw std::runtime_error("pixel payload header marker mismatch");
    auto buffers = r.count(32);
    for (unsigned i = 0; i < buffers; i++)
        h.uniform_buffers.push_back(r.packed());
    h.global_arrays = r.packed();
    h.inputs = r.varyings();
    h.outputs = r.varyings();
    h.resource_bits = r.u32();
    h.srv = r.words();
    h.samplers = r.words();
    h.uav = r.words();
    h.layout_hashes = r.words();
    h.textures = r.words();
    h.inout_mask = r.u16();
    for (auto& c : h.counts)
        c = r.u8();
    auto flatten = r.u32();
    if (flatten > 1)
        throw std::runtime_error("invalid flatten setting");
    h.flatten = flatten != 0;
    if (r.u32() != 0 || r.u32() != 0)
        throw std::runtime_error("material shader names/copy-info unsupported");
    auto codeSize = r.count(1u << 20);
    r.skip(codeSize);
    auto controlSize = r.count(1u << 20);
    r.skip(controlSize);
    auto slots = r.count(64);
    std::set<unsigned> used;
    for (unsigned i = 0; i < slots; i++)
    {
        auto key = r.u8();
        auto value = r.u32();
        if (value > 31 || !used.insert(key).second)
            throw std::runtime_error("invalid packed global slot");
        h.packed_global_slots.push_back({key, std::int32_t(value)});
    }
    h.default_sampler = std::int32_t(r.u32());
    if (h.default_sampler < -1 || h.default_sampler > 31)
        throw std::runtime_error("default sampler outside cache");
    auto scratchBytes = r.u32(), scratch = r.u32();
    if (scratch > 0xffffff || scratchBytes != scratch << 8)
        throw std::runtime_error("scratch size mismatch");
    if (r.u32() != 0 || r.u32() != 4 || r.offset() != payload.size())
        throw std::runtime_error("unexpected pixel payload tail");
    return h;
}
using ResourceMap = std::map<unsigned, std::vector<std::pair<unsigned, unsigned>>>;
inline unsigned TableBuffers(const std::vector<std::uint32_t>& table)
{
    for (std::size_t i = 0; i < table.size(); i++)
    {
        auto word = table[i];
        if (word == 0xffffffff)
            return unsigned(i);
        if (word)
        {
            if (word >= table.size())
                throw std::runtime_error("resource table head outside payload");
            return word;
        }
    }
    return unsigned(table.size());
}
inline ResourceMap ReadResourceTable(const std::vector<std::uint32_t>& table)
{
    ResourceMap out;
    auto buffers = TableBuffers(table);
    if (buffers > 32)
        throw std::runtime_error("resource table buffer limit");
    std::set<std::size_t> used;
    for (unsigned ub = 0; ub < buffers; ub++)
    {
        auto at = table[ub];
        if (!at)
            continue;
        if (at < buffers || at >= table.size())
            throw std::runtime_error("resource table invalid pointer");
        while (at < table.size() && table[at] != 0xffffffff && (table[at] >> 24) == ub)
        {
            if (!used.insert(at).second)
                throw std::runtime_error("resource table overlap");
            auto word = table[at++];
            out[ub].push_back({(word >> 8) & 65535, word & 255});
        }
    }
    return out;
}
inline std::vector<std::uint32_t> BuildResourceTable(const ResourceMap& rows, unsigned buffers)
{
    if (buffers > 32)
        throw std::runtime_error("resource table buffer limit");
    std::vector<std::uint32_t> head(buffers, 0), body;
    for (auto& p : rows)
        if (p.first >= buffers && !p.second.empty())
            throw std::runtime_error("resource table buffer missing");
    for (unsigned ub = 0; ub < buffers; ub++)
    {
        auto row = rows.find(ub);
        if (row == rows.end() || row->second.empty())
            continue;
        head[ub] = buffers + unsigned(body.size());
        for (auto& p : row->second)
        {
            if (p.first > 65535 || p.second > 255)
                throw std::runtime_error("resource binding overflow");
            body.push_back(ub << 24 | p.first << 8 | p.second);
        }
    }
    if (buffers)
    {
        head.insert(head.end(), body.begin(), body.end());
        head.push_back(0xffffffff);
    }
    return head;
}
struct ResolvedBindings
{
    std::map<std::string, std::pair<int, int>> textures;
    std::map<std::string, int> varyings;
};
inline ResolvedBindings ResolveMaterialBindings(const ShaderBindings& h, unsigned materialBuffer,
                                                const std::vector<std::string>& names)
{
    if (materialBuffer > 31)
        throw std::runtime_error("invalid Material uniform buffer");
    ResolvedBindings out;
    auto textures = ReadResourceTable(h.textures), srvs = ReadResourceTable(h.srv),
         samplers = ReadResourceTable(h.samplers);
    for (auto table : {textures, srvs})
    {
        for (auto& row : table[materialBuffer])
            if (row.first / 2 < names.size())
            {
                auto name = names[row.first / 2];
                if (out.textures.count(name) && out.textures[name].first != int(row.second))
                    throw std::runtime_error("ambiguous material texture slot");
                if (!out.textures.count(name))
                    out.textures[name] = {-1, -1};
                out.textures[name].first = int(row.second);
            }
    }
    for (auto& row : samplers[materialBuffer])
        if (row.first / 2 < names.size())
        {
            auto name = names[row.first / 2];
            if (!out.textures.count(name))
                out.textures[name] = {-1, -1};
            if (out.textures[name].second != -1 && out.textures[name].second != int(row.second))
                throw std::runtime_error("ambiguous material sampler slot");
            out.textures[name].second = int(row.second);
        }
    for (auto& row : out.textures)
        if (row.second.first < 0 || row.second.first > 63 || row.second.second < 0 || row.second.second > 31)
            throw std::runtime_error("material texture/sampler pairing missing");
    for (auto& v : h.inputs)
    {
        auto name = v.name.substr(0, v.name.find("_centroid"));
        if (out.varyings.count(name) && out.varyings[name] != v.location)
            throw std::runtime_error("ambiguous varying location");
        out.varyings[name] = v.location;
    }
    return out;
}
struct ReboundTextures
{
    ShaderBindings bindings;
    std::vector<std::pair<unsigned, unsigned>> slots;
    unsigned old_textures = 0, old_samplers = 0, new_textures = 0, new_samplers = 0;
};
inline ReboundTextures RebindMaterialTextures(const ShaderBindings& h, unsigned materialBuffer, unsigned count,
                                              std::uint32_t layoutHash, bool shareSampler = false,
                                              const std::vector<unsigned>& looseTextures = {},
                                              const std::vector<unsigned>& looseSamplers = {})
{
    if (materialBuffer > 31 || count > 25)
        throw std::runtime_error("custom material texture binding limit");
    auto textures = ReadResourceTable(h.textures), samplers = ReadResourceTable(h.samplers),
         srvs = ReadResourceTable(h.srv);
    if (!srvs[materialBuffer].empty())
        throw std::runtime_error("material SRV/virtual textures unsupported");
    std::set<unsigned> takenT(looseTextures.begin(), looseTextures.end()),
        takenS(looseSamplers.begin(), looseSamplers.end());
    for (auto table : {textures, srvs})
        for (auto& row : table)
            if (row.first != materialBuffer)
                for (auto p : row.second)
                    takenT.insert(p.second);
    for (auto& row : samplers)
        if (row.first != materialBuffer)
            for (auto p : row.second)
                takenS.insert(p.second);
    if (h.default_sampler >= 0)
        takenS.insert(unsigned(h.default_sampler));
    for (auto n : takenT)
        if (n > 63)
            throw std::runtime_error("texture slot outside cache");
    for (auto n : takenS)
        if (n > 31)
            throw std::runtime_error("sampler slot outside cache");
    std::vector<unsigned> freeT, freeS;
    for (unsigned i = 0; i < 64; i++)
        if (!takenT.count(i))
            freeT.push_back(i);
    for (unsigned i = 0; i < 32; i++)
        if (!takenS.count(i))
            freeS.push_back(i);
    unsigned neededS = shareSampler ? std::min(1u, count) : count;
    if (count > freeT.size() || neededS > freeS.size())
        throw std::runtime_error("lighting/loose bindings leave insufficient custom texture slots");
    ReboundTextures out;
    out.bindings = h;
    out.old_textures = unsigned(textures[materialBuffer].size());
    out.old_samplers = unsigned(samplers[materialBuffer].size());
    textures[materialBuffer].clear();
    samplers[materialBuffer].clear();
    for (unsigned i = 0; i < count; i++)
    {
        unsigned s = freeS[shareSampler ? 0 : i];
        out.slots.push_back({freeT[i], s});
        textures[materialBuffer].push_back({2 * i, freeT[i]});
        if (!shareSampler || i == 0)
            samplers[materialBuffer].push_back({2 * i + 1, s});
    }
    unsigned buffers = std::max(
        {unsigned(h.layout_hashes.size()), TableBuffers(h.textures), TableBuffers(h.samplers), materialBuffer + 1});
    out.bindings.textures = BuildResourceTable(textures, buffers);
    out.bindings.samplers = BuildResourceTable(samplers, buffers);
    out.bindings.layout_hashes.resize(buffers, 0);
    out.bindings.layout_hashes[materialBuffer] = count ? layoutHash : 0;
    if (count)
        out.bindings.resource_bits |= 1u << materialBuffer;
    else
        out.bindings.resource_bits &= ~(1u << materialBuffer);
    out.new_textures = count;
    out.new_samplers = neededS;
    // Original patch_payload_tables updates b0 (textures) and b1 (samplers),
    // dropping only old material resources that actually occur in this pass.
    int samplerCount = int(h.counts[1]) + int(neededS) - int(out.old_samplers),
        textureCount = int(h.counts[0]) + int(count) - int(out.old_textures);
    if (samplerCount < 0 || samplerCount > 32 || textureCount < 0 || textureCount > 64)
        throw std::runtime_error("shader resource count mismatch");
    out.bindings.counts[1] = std::uint8_t(samplerCount);
    out.bindings.counts[0] = std::uint8_t(textureCount);
    return out;
}
}
}

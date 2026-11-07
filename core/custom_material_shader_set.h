// Construct a format-3 set from compiler-owned programs and structural render
// bindings. This writer never copies a game shader's code or control section.
#pragma once
#include "custom_material_hash.h"
#include "custom_material_support.h"
#include <limits>
namespace P4MapKit
{
namespace CustomMaterial
{
inline void U32(Bytes& b, std::uint32_t x)
{
    for (int i = 0; i < 4; i++)
        b.push_back(std::uint8_t(x >> (8 * i)));
}
inline void U16(Bytes& b, unsigned x)
{
    if (x > 65535)
        throw std::runtime_error("u16 overflow");
    b.push_back(std::uint8_t(x));
    b.push_back(std::uint8_t(x >> 8));
}
inline void Append(Bytes& b, const Bytes& x)
{
    b.insert(b.end(), x.begin(), x.end());
}
inline std::uint32_t Read32(const Bytes& b, std::size_t at)
{
    if (at > b.size() || b.size() - at < 4)
        throw std::runtime_error("truncated shader word");
    return std::uint32_t(b[at]) | std::uint32_t(b[at + 1]) << 8 | std::uint32_t(b[at + 2]) << 16 |
           std::uint32_t(b[at + 3]) << 24;
}
inline void Set32(Bytes& b, std::size_t at, std::uint32_t x)
{
    if (at > b.size() || b.size() - at < 4)
        throw std::runtime_error("invalid shader word offset");
    for (int i = 0; i < 4; i++)
        b[at + i] = std::uint8_t(x >> (8 * i));
}
inline bool Plain(const std::string& s, std::size_t max)
{
    if (s.empty() || s.size() > max)
        return false;
    for (unsigned char c : s)
        if (c < 32 || c > 126)
            return false;
    return true;
}
inline bool AssetPath(const std::string& s, bool object)
{
    if (s.size() < 2 || s.size() > 511 || s[0] != '/')
        return false;
    unsigned dots = 0;
    std::size_t leaf = 0;
    for (std::size_t i = 0; i < s.size(); i++)
    {
        auto c = s[i];
        if (c == '/')
        {
            if (dots || i + 1 == s.size() || s[i + 1] == '/')
                return false;
            leaf = i + 1;
        }
        else if (c == '.')
        {
            if (++dots > 1 || i == leaf || i + 1 == s.size())
                return false;
        }
        else if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    }
    return object ? dots == 1 : dots == 0;
}
struct PackedArray
{
    unsigned size = 0;
    std::uint8_t type_name = 0, type_index = 0;
};
struct Varying
{
    std::string name;
    std::int32_t location = 0;
};
struct ShaderBindings
{
    unsigned version = kRenderBindingVersion;
    std::vector<std::vector<PackedArray>> uniform_buffers;
    std::vector<PackedArray> global_arrays;
    std::vector<Varying> inputs, outputs;
    std::uint32_t resource_bits = 0;
    std::vector<std::uint32_t> srv, samplers, uav, layout_hashes, textures;
    unsigned inout_mask = 0;
    std::array<std::uint8_t, 5> counts = {{0, 0, 0, 0, 0}};
    bool flatten = false;
    std::vector<std::pair<std::uint8_t, std::int32_t>> packed_global_slots;
    std::int32_t default_sampler = -1;
};
inline void WritePacked(Bytes& b, const std::vector<PackedArray>& arrays)
{
    if (arrays.size() > 64)
        throw std::runtime_error("packed array limit");
    U32(b, std::uint32_t(arrays.size()));
    for (auto& a : arrays)
    {
        U16(b, a.size);
        b.push_back(a.type_name);
        b.push_back(a.type_index);
    }
}
inline void WriteVaryings(Bytes& b, const std::vector<Varying>& arrays)
{
    if (arrays.size() > 32)
        throw std::runtime_error("varying limit");
    U32(b, std::uint32_t(arrays.size()));
    std::set<std::string> seen;
    for (auto& a : arrays)
    {
        if (!Plain(a.name, 127) || !seen.insert(a.name).second || a.location < 0 || a.location > 127)
            throw std::runtime_error("invalid varying binding");
        U32(b, std::uint32_t(a.name.size() + 1));
        Append(b, ByteText(a.name));
        b.push_back(0);
        U32(b, std::uint32_t(a.location));
    }
}
inline void WriteWords(Bytes& b, const std::vector<std::uint32_t>& words)
{
    if (words.size() > 65536)
        throw std::runtime_error("resource table limit");
    U32(b, std::uint32_t(words.size()));
    for (auto n : words)
        U32(b, n);
}
inline Bytes ShaderHeader(const ShaderBindings& h)
{
    if (h.version != kRenderBindingVersion)
        throw std::runtime_error("render binding version mismatch");
    Bytes out;
    U32(out, 0x474c534c);
    U16(out, 0x5053);
    if (h.uniform_buffers.size() > 32)
        throw std::runtime_error("uniform buffer limit");
    U32(out, std::uint32_t(h.uniform_buffers.size()));
    for (auto& a : h.uniform_buffers)
        WritePacked(out, a);
    WritePacked(out, h.global_arrays);
    WriteVaryings(out, h.inputs);
    WriteVaryings(out, h.outputs);
    U32(out, h.resource_bits);
    WriteWords(out, h.srv);
    WriteWords(out, h.samplers);
    WriteWords(out, h.uav);
    WriteWords(out, h.layout_hashes);
    WriteWords(out, h.textures);
    U16(out, h.inout_mask);
    for (auto c : h.counts)
        out.push_back(c);
    U32(out, h.flatten ? 1 : 0);
    U32(out, 0);
    U32(out, 0);
    return out;
}
inline Bytes CleanControl(const Bytes& computed, const std::string& assembly, const Bytes& code)
{
    constexpr std::size_t header = 0x878;
    if (computed.size() < header || code.empty() || code.size() > (1u << 20) || code.size() % 256 ||
        Read32(code, 0) != 0x12345678 || ((Read32(code, 0x30) >> 10) & 15) != 5)
        throw std::runtime_error("compiler pixel code shape refused");
    if (Read32(computed, 0x704) != code.size() || Read32(computed, 0x714) != 1)
        throw std::runtime_error("compiler control/code mismatch");
    if (assembly.size() > (1u << 18) || assembly.compare(0, 9, "!!NVfp5.0") != 0 || assembly.size() < 4 ||
        assembly.compare(assembly.size() - 4, 4, "END\n") != 0)
        throw std::runtime_error("custom pixel assembly shape refused");
    for (unsigned char c : assembly)
        if ((c < 32 && c != '\n' && c != '\t' && c != '\r') || c > 126)
            throw std::runtime_error("assembly must be plain text");
    auto total = (header + assembly.size() + 1 + 7) & ~std::size_t(7);
    Bytes out(total, 0);
    static const std::map<unsigned, std::uint32_t> fixed = {{0, 0x98761234},    {4, 1},
                                                            {8, 0x10},          {0xc, 0x120},
                                                            {0x10, 0xb},        {0x14, 0x878},
                                                            {0x2c, 0x20},       {0x34, 0x340},
                                                            {0x38, 0xffffff01}, {0x3c, 0xffffffff},
                                                            {0x40, 0xffffffff}, {0x44, 0xffffffff},
                                                            {0x48, 0xffffffff}, {0x4c, 0xffffffff},
                                                            {0x50, 0xffffffff}, {0x54, 0xffffffff},
                                                            {0x58, 0xff},       {0x708, 0x30},
                                                            {0x790, 0x20001f},  {0x7f4, 0xc},
                                                            {0x714, 1},         {0x734, 1}};
    for (auto& p : fixed)
        Set32(out, p.first, p.second);
    for (auto at : {0x6f8, 0x6fc, 0x700, 0x704, 0x70c, 0x710})
        Set32(out, at, Read32(computed, at));
    Set32(out, 0x18, std::uint32_t(assembly.size()));
    for (auto at : {0x20, 0x6f0, 0x7c8, 0x7e8})
        Set32(out, at, std::uint32_t(total));
    auto own = ByteText("p4-custom-program");
    Append(own, code);
    auto digest = Sha256(own);
    std::copy(digest.begin(), digest.begin() + 24, out.begin() + 0x7d0);
    std::copy(assembly.begin(), assembly.end(), out.begin() + header);
    return out;
}
inline Bytes PixelPayload(const ShaderBindings& bindings, const Bytes& code, const Bytes& computed_control,
                          const std::string& assembly)
{
    auto out = ShaderHeader(bindings);
    auto control = CleanControl(computed_control, assembly, code);
    U32(out, std::uint32_t(code.size()));
    Append(out, code);
    U32(out, std::uint32_t(control.size()));
    Append(out, control);
    if (bindings.packed_global_slots.size() > 64)
        throw std::runtime_error("packed slot limit");
    std::set<unsigned> taken;
    U32(out, std::uint32_t(bindings.packed_global_slots.size()));
    for (auto& p : bindings.packed_global_slots)
    {
        if (!taken.insert(p.first).second || p.second < 0 || p.second > 31)
            throw std::runtime_error("invalid packed global slot");
        out.push_back(p.first);
        U32(out, std::uint32_t(p.second));
    }
    U32(out, std::uint32_t(bindings.default_sampler));
    auto scratch = Read32(control, 0x710);
    if (scratch > 0xffffff)
        throw std::runtime_error("scratch size overflow");
    U32(out, scratch << 8);
    U32(out, scratch);
    U32(out, 0);
    U32(out, 4);
    if (out.size() > (1u << 20))
        throw std::runtime_error("shader payload limit");
    return out;
}
struct ContentOperation
{
    bool copy = false;
    std::uint32_t offset = 0, length = 0;
    Bytes own;
};
struct ContentName
{
    std::string name;
    std::uint32_t number = 0;
    std::vector<std::uint32_t> offsets;
};
struct ContentEdits
{
    std::uint32_t old_size = 0, new_size = 0;
    std::vector<ContentOperation> operations;
    std::vector<ContentName> names;
};
inline void WriteEdits(Bytes& out, const ContentEdits& e)
{
    if (!e.old_size)
    {
        if (e.new_size || !e.operations.empty() || !e.names.empty())
            throw std::runtime_error("incomplete content edits");
        U32(out, 0);
        return;
    }
    if (e.old_size > (1u << 18) || !e.new_size || e.new_size > (1u << 18) || e.operations.size() > e.new_size)
        throw std::runtime_error("content edit limit");
    U32(out, 1);
    U32(out, e.old_size);
    U32(out, e.new_size);
    U32(out, std::uint32_t(e.operations.size()));
    std::uint32_t made = 0;
    for (auto& o : e.operations)
    {
        std::size_t size = o.copy ? o.length : o.own.size();
        if (!size || size > e.new_size - made)
            throw std::runtime_error("content edit exceeds output");
        if (o.copy)
        {
            if (!o.own.empty() || o.offset > e.old_size || size > e.old_size - o.offset)
                throw std::runtime_error("invalid content copy");
            out.push_back(0);
            U32(out, o.offset);
            U32(out, o.length);
        }
        else
        {
            if (o.offset || o.length)
                throw std::runtime_error("ambiguous inserted content");
            out.push_back(1);
            U32(out, std::uint32_t(size));
            Append(out, o.own);
        }
        made += std::uint32_t(size);
    }
    if (made != e.new_size || e.names.size() > e.new_size)
        throw std::runtime_error("content edit size mismatch");
    U32(out, std::uint32_t(e.names.size()));
    std::set<std::pair<std::string, std::uint32_t>> names;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> spans;
    for (auto& n : e.names)
    {
        if (!Plain(n.name, 127) || !names.insert({n.name, n.number}).second || n.offsets.size() > e.new_size / 12)
            throw std::runtime_error("invalid content name");
        U16(out, unsigned(n.name.size()));
        Append(out, ByteText(n.name));
        U32(out, n.number);
        U32(out, std::uint32_t(n.offsets.size()));
        for (auto offset : n.offsets)
        {
            if (offset > e.new_size || e.new_size - offset < 12)
                throw std::runtime_error("content name outside description");
            spans.push_back({offset, offset + 12});
            U32(out, offset);
        }
    }
    std::sort(spans.begin(), spans.end());
    for (std::size_t i = 1; i < spans.size(); i++)
        if (spans[i].first < spans[i - 1].second)
            throw std::runtime_error("overlapping content names");
}
struct SetShader
{
    std::uint32_t place = 0;
    Bytes payload;
};
struct SetMaterial
{
    std::string material, game_material;
    Bytes map_id_hash;
    std::uint32_t map_shaders = 0;
    std::vector<SetShader> shaders;
    ContentEdits edits;
};
inline Bytes WriteShaderSet3(const std::vector<SetMaterial>& materials)
{
    if (materials.size() > 1024)
        throw std::runtime_error("material set limit");
    Bytes out = ByteText("P4MSHDR3");
    U32(out, std::uint32_t(materials.size()));
    std::set<std::string> taken;
    for (auto& m : materials)
    {
        if (!AssetPath(m.material, true) || m.material.compare(0, 6, "/Game/") != 0 ||
            m.material.compare(0, 14, "/Game/Carrot4/") == 0 || !AssetPath(m.game_material, false) ||
            !taken.insert(m.material).second)
            throw std::runtime_error("invalid or duplicate material path");
        for (auto s : {m.material, m.game_material})
        {
            U16(out, unsigned(s.size()));
            Append(out, ByteText(s));
        }
        if (m.map_id_hash.size() != 20 || !m.map_shaders || m.map_shaders > 4096 || m.shaders.empty() ||
            m.shaders.size() > m.map_shaders)
            throw std::runtime_error("invalid shader map binding");
        Append(out, m.map_id_hash);
        U32(out, m.map_shaders);
        U32(out, std::uint32_t(m.shaders.size()));
        std::set<std::uint32_t> places;
        for (auto& s : m.shaders)
        {
            if (s.place >= m.map_shaders || !places.insert(s.place).second || s.payload.empty() ||
                s.payload.size() > (1u << 20))
                throw std::runtime_error("invalid replaced shader");
            U32(out, s.place);
            out.push_back(3);
            Append(out, Sha1(s.payload));
            U32(out, std::uint32_t(s.payload.size()));
            Append(out, s.payload);
        }
        WriteEdits(out, m.edits);
        if (out.size() > std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("shader set size limit");
    }
    return out;
}
// Digest binds graph, settings, template map identity and per-pass binding facts.
// Runtime format3 carries the map ID hash; a recipe's SHA256 is an authoring check.
inline std::string RecipeDigest(const Json& recipe)
{
    return Hex(Sha256(ByteText(recipe.Dump())));
}
inline void ValidateRecipe(const Json& recipe, const std::string& expected)
{
    if (Number(recipe, "version") != kGraphContractVersion ||
        Number(recipe, "render_binding_version") != kRenderBindingVersion ||
        Text(recipe, "kind") != "p4-custom-material-recipe")
        throw std::runtime_error("compile recipe version mismatch");
    if (expected.size() != 64 || RecipeDigest(recipe) != expected)
        throw std::runtime_error("compile recipe digest mismatch");
    auto hash = Unhex(Text(recipe, "map_id_hash"));
    if (hash.size() != 20)
        throw std::runtime_error("compile recipe needs the game map ID hash");
    if (!AssetPath(Text(recipe, "material"), true) || !AssetPath(Text(recipe, "game_material"), false))
        throw std::runtime_error("compile recipe material identity invalid");
}
}
}

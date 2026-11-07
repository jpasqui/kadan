// The uniform expression set from named facts (own_uniforms.py), for the own map writer.
// Part of own_map_writer.h, which includes every part.
#pragma once
#include "own_map_freeze.h"

namespace P4MapKit
{
namespace CustomMaterial
{
namespace OwnMap
{
// ----- the uniform expression set (own_uniforms.py) ----------------------------------------------------------------
struct ParameterInfo
{
    std::optional<std::string> name;
    std::int64_t number = 0;
    std::int32_t layer = -1;
    std::uint8_t association = 2;
};
struct ScalarParameter : ParameterInfo
{
    std::uint32_t defaultBits = 0;
};
struct VectorParameter : ParameterInfo
{
    std::array<std::uint32_t, 4> defaultBits{};
};
struct TextureParameter : ParameterInfo
{
    std::int32_t index = 0;
    std::uint8_t sampler = 0;
    std::uint8_t vtLayer = 255;
};
// One preshader opcode and its operands: numbers, or 16 bytes for an 's' operand.
struct PreshaderOperand
{
    std::int64_t number = 0;
    Bytes bytes;
    bool isBytes = false;
};
struct PreshaderOp
{
    std::string name;
    std::vector<PreshaderOperand> operands;
};
using PreshaderProgram = std::vector<PreshaderOp>;
struct UniformFacts
{
    std::vector<ScalarParameter> scalars;
    std::vector<VectorParameter> vectors;
    std::map<std::string, std::vector<TextureParameter>> textures; // Standard2D, Cube, Array2D, Volume
    std::vector<PreshaderProgram> scalarPrograms, vectorPrograms;
    std::vector<NameKey> preshaderNames;
    std::vector<Bytes> collections; // 16-byte state GUIDs
    Bytes flags = Bytes(12, 0);
};
inline const std::vector<std::string>& TextureTypes()
{
    static const std::vector<std::string> t{"Standard2D", "Cube", "Array2D", "Volume"};
    return t;
}
// EMaterialPreshaderOpcode (UE 4.26) and the operands each writes after its byte (struct formats; S is 16 bytes).
inline const std::vector<std::pair<std::string, std::string>>& Opcodes()
{
    static const std::vector<std::pair<std::string, std::string>> ops{{"Nop", ""}, {"ConstantZero", ""},
        {"Constant", "IIII"}, {"ScalarParameter", "H"}, {"VectorParameter", "H"}, {"Add", ""}, {"Sub", ""}, {"Mul", ""},
        {"Div", ""}, {"Fmod", ""}, {"Min", ""}, {"Max", ""}, {"Clamp", ""}, {"Sin", ""}, {"Cos", ""}, {"Tan", ""},
        {"Asin", ""}, {"Acos", ""}, {"Atan", ""}, {"Atan2", ""}, {"Dot", "B"}, {"Cross", "B"}, {"Sqrt", ""},
        {"Length", "B"}, {"Saturate", ""}, {"Abs", ""}, {"Floor", ""}, {"Ceil", ""}, {"Round", ""}, {"Trunc", ""},
        {"Sign", ""}, {"Frac", ""}, {"Fractional", ""}, {"Log2", ""}, {"Log10", ""}, {"ComponentSwizzle", "BBBBB"},
        {"AppendVector", "B"}, {"TextureSize", "HiBi"}, {"TexelSize", "HiBi"},
        {"ExternalTextureCoordinateScaleRotation", "HSi"}, {"ExternalTextureCoordinateOffset", "HSi"},
        {"RuntimeVirtualTextureUniform", "HiBii"}};
    return ops;
}
inline Bytes EncodeProgram(const PreshaderProgram& ops)
{
    Bytes out;
    for (const auto& op : ops)
    {
        const auto& table = Opcodes();
        auto it = std::find_if(table.begin(), table.end(), [&](const auto& e) { return e.first == op.name; });
        Need(it != table.end(), "unknown preshader opcode " + op.name);
        const auto& format = it->second;
        Need(op.operands.size() == format.size(), "operand count of " + op.name);
        out.push_back(std::uint8_t(it - table.begin()));
        for (std::size_t i = 0; i < format.size(); i++)
        {
            const auto& v = op.operands[i];
            const char f = format[i];
            if (f == 'S')
            {
                Need(v.isBytes && v.bytes.size() == 16, "16-byte operand of " + op.name);
                PutBytes(out, v.bytes);
                continue;
            }
            Need(!v.isBytes, "number operand of " + op.name);
            const auto n = v.number;
            switch (f)
            {
                case 'B':
                    Need(n >= 0 && n <= 0xff, "byte operand");
                    Put8(out, std::uint64_t(n));
                    break;
                case 'H':
                    Need(n >= 0 && n <= 0xffff, "short operand");
                    Put16(out, std::uint64_t(n));
                    break;
                case 'I':
                    Need(n >= 0 && n <= 0xffffffffLL, "unsigned operand");
                    Put32(out, std::uint64_t(n));
                    break;
                case 'i':
                    Need(n >= INT32_MIN && n <= INT32_MAX, "int operand");
                    Put32(out, std::uint64_t(n));
                    break;
                default:
                    Need(false, "operand format");
            }
        }
    }
    return out;
}

struct UniformSet
{
    Bytes tail;
    std::map<std::size_t, NodeRef> nodes;
}; // root bytes USET..content hash, arrays

inline std::pair<std::uint32_t, std::vector<std::pair<std::uint16_t, std::uint8_t>>> MaterialLayout(
    std::size_t vectorPrograms, std::size_t scalarPrograms,
    const std::map<std::string, std::vector<TextureParameter>>& textures)
{
    std::size_t at = 16 * vectorPrograms + 16 * ((scalarPrograms + 3) / 4);
    std::vector<std::pair<std::uint16_t, std::uint8_t>> resources;
    for (const auto& kind : TextureTypes())
    {
        auto it = textures.find(kind);
        if (it == textures.end())
            continue;
        for (std::size_t i = 0; i < it->second.size(); i++)
        {
            Need(at + 8 < 0x10000, "the Material uniform buffer is too large");
            resources.push_back({std::uint16_t(at), UBMT_TEXTURE});
            resources.push_back({std::uint16_t(at + 8), UBMT_SAMPLER});
            at += 16;
        }
    }
    Need(at + 8 < 0x10000, "the Material uniform buffer is too large");
    resources.push_back({std::uint16_t(at), UBMT_SAMPLER}); // Wrap_WorldGroupSettings
    resources.push_back({std::uint16_t(at + 8), UBMT_SAMPLER}); // Clamp_WorldGroupSettings
    return {std::uint32_t(AlignUp(at + 16, 16)), resources};
}
inline std::uint32_t MaterialLayoutHashOf(const UniformFacts& f)
{
    const auto [size, resources] = MaterialLayout(f.vectorPrograms.size(), f.scalarPrograms.size(), f.textures);
    return LayoutHash(size, 0xFF, resources);
}

inline UniformSet WriteUniformSet(const UniformFacts& facts)
{
    for (const auto& [kind, list] : facts.textures)
    {
        (void)list;
        Need(std::find(TextureTypes().begin(), TextureTypes().end(), kind) != TextureTypes().end(),
            "texture type not written here: " + kind);
    }
    UniformSet out;
    out.tail.assign(ROOT_CONTENT_HASH - USET, 0);
    auto put = [&](std::size_t field, const std::string& kind, unsigned align, Bytes rows, std::size_t count,
                   std::vector<NamePatch> names = {})
    {
        if (!count)
            return;
        At32(out.tail, field - USET + 8, count);
        At32(out.tail, field - USET + 12, count);
        out.nodes[field] = MakeNode(kind, align, std::move(rows), {}, std::move(names));
    };
    auto info = [](Bytes& o, const ParameterInfo& p)
    {
        o.insert(o.end(), 12, 0);
        Put32(o, std::uint32_t(p.layer));
        Put8(o, p.association);
        o.insert(o.end(), 3, 0);
    };
    auto named = [](const auto& items, std::size_t stride)
    {
        std::vector<NamePatch> names;
        for (std::size_t i = 0; i < items.size(); i++)
            if (items[i].name)
            {
                NeedName(*items[i].name);
                names.push_back({stride * i, NameKey{*items[i].name, items[i].number}});
            }
        return names;
    };
    // the preshader programs: one byte stream, scalar programs first
    Bytes data, scalarRows, vectorRows;
    for (auto [programs, rows] :
        {std::pair{&facts.scalarPrograms, &scalarRows}, std::pair{&facts.vectorPrograms, &vectorRows}})
        for (const auto& ops : *programs)
        {
            const auto code = EncodeProgram(ops);
            Put32(*rows, data.size());
            Put32(*rows, code.size());
            PutBytes(data, code);
        }
    put(U_VECTOR_PRESHADERS, "uniform.vector-preshaders", 4, vectorRows, facts.vectorPrograms.size());
    put(U_SCALAR_PRESHADERS, "uniform.scalar-preshaders", 4, scalarRows, facts.scalarPrograms.size());
    Bytes rows;
    for (const auto& p : facts.scalars)
    {
        info(rows, p);
        Put32(rows, p.defaultBits);
    }
    put(U_SCALAR_PARAMS, "uniform.scalar-parameters", 4, rows, facts.scalars.size(), named(facts.scalars, 24));
    rows.clear();
    for (const auto& p : facts.vectors)
    {
        info(rows, p);
        for (auto v : p.defaultBits)
            Put32(rows, v);
    }
    put(U_VECTOR_PARAMS, "uniform.vector-parameters", 4, rows, facts.vectors.size(), named(facts.vectors, 36));
    for (std::size_t t = 0; t < TextureTypes().size(); t++)
    {
        auto it = facts.textures.find(TextureTypes()[t]);
        if (it == facts.textures.end())
            continue;
        rows.clear();
        for (const auto& p : it->second)
        {
            info(rows, p);
            Put32(rows, std::uint32_t(p.index));
            Put8(rows, p.sampler);
            Put8(rows, p.vtLayer);
            Put16(rows, 0);
        }
        put(U_TEXTURES + 16 * t, "uniform.textures." + TextureTypes()[t], 4, rows, it->second.size(),
            named(it->second, 28));
    }
    std::vector<NamePatch> names;
    for (std::size_t i = 0; i < facts.preshaderNames.size(); i++)
    {
        NeedName(facts.preshaderNames[i].text);
        names.push_back({12 * i, facts.preshaderNames[i]});
    }
    put(U_PRESHADER_NAMES, "uniform.preshader-names", 4, Bytes(12 * facts.preshaderNames.size(), 0),
        facts.preshaderNames.size(), names);
    put(U_PRESHADER_DATA, "uniform.preshader-data", 1, data, data.size());
    rows.clear();
    for (const auto& c : facts.collections)
    {
        Need(c.size() == 16, "a collection state id is 16 bytes");
        PutBytes(rows, c);
    }
    put(U_COLLECTIONS, "uniform.parameter-collections", 4, rows, facts.collections.size());

    const auto [size, resources] = MaterialLayout(
        facts.vectorPrograms.size(), facts.scalarPrograms.size(), facts.textures);
    Need(size < 0x10000, "the Material uniform buffer is too large");
    At32(out.tail, LAYOUT - USET, size);
    out.tail[LAYOUT - USET + 4] = 0xFF;
    out.tail[LAYOUT - USET + 5] = 0;
    At16(out.tail, LAYOUT - USET + 6, 0xFFFF);
    rows.clear();
    for (const auto& [o, k] : resources)
    {
        Put16(rows, o);
        Put8(rows, k);
        Put8(rows, 0);
    }
    put(L_RESOURCES, "layout.resources", 2, rows, resources.size());
    const std::string layoutName = "Material";
    rows.clear();
    for (char c : layoutName)
        Put16(rows, std::uint8_t(c));
    Put16(rows, 0);
    put(L_NAME, "layout.name", 2, rows, layoutName.size() + 1);
    At32(out.tail, L_HASH - USET, LayoutHash(size, 0xFF, resources));
    Need(facts.flags.size() == 12, "the set's flags are 12 bytes");
    std::copy(facts.flags.begin(), facts.flags.end(), out.tail.begin() + std::ptrdiff_t(ROOT_SCENE_TEXTURES - USET));
    return out;
}

// Python's repr() of own_uniforms.flat_set's nodes, sorted (the id hash covers it): [(offset, (kind, align, data,
// [(offset, (text, number))])), ...].
inline std::string PyRepr(const Bytes& b, bool isBytes)
{
    bool single = false, dbl = false;
    for (auto c : b)
    {
        single |= c == '\'';
        dbl |= c == '"';
    }
    const char quote = (single && !dbl) ? '"' : '\'';
    std::string o = isBytes ? "b" : "";
    o.push_back(quote);
    const char* hex = "0123456789abcdef";
    for (std::uint8_t c : b)
    {
        if (c == std::uint8_t(quote) || c == '\\')
        {
            o.push_back('\\');
            o.push_back(char(c));
        }
        else if (c == '\t')
            o += "\\t";
        else if (c == '\n')
            o += "\\n";
        else if (c == '\r')
            o += "\\r";
        else if (c < 0x20 || c == 0x7f || (isBytes && c >= 0x80))
        {
            o += "\\x";
            o.push_back(hex[c >> 4]);
            o.push_back(hex[c & 15]);
        }
        else
            o.push_back(char(c));
    }
    o.push_back(quote);
    return o;
}
inline std::string ReprUniformNodes(const std::map<std::size_t, NodeRef>& nodes)
{
    std::string o = "[";
    bool first = true;
    for (const auto& [offset, n] : nodes)
    {
        if (!first)
            o += ", ";
        first = false;
        auto names = n->names;
        std::sort(names.begin(), names.end());
        o += "(" + std::to_string(offset) + ", (" + PyRepr(Text(n->kind), false) + ", " + std::to_string(n->align) +
            ", " + PyRepr(n->data, true) + ", [";
        for (std::size_t i = 0; i < names.size(); i++)
        {
            if (i)
                o += ", ";
            o += "(" + std::to_string(names[i].first) + ", (" + PyRepr(Text(names[i].second.text), false) + ", " +
                std::to_string(names[i].second.number) + "))";
        }
        o += "]))";
    }
    return o + "]";
}

}
}
} // namespace P4MapKit::CustomMaterial::OwnMap

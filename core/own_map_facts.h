// Facts from JSON, for the own map writer.
// Part of own_map_writer.h, which includes every part.
#pragma once
#include "own_map_versions.h"

namespace P4MapKit
{
namespace CustomMaterial
{
namespace OwnMap
{
// ----- facts from JSON ---------------------------------------------------------------------------------------------
inline std::int64_t JsonInt(const Json& j)
{
    const double v = j.Numeric();
    Need(v == double(std::int64_t(v)), "an integer is required");
    return std::int64_t(v);
}
inline Bytes FromHex(const std::string& hex)
{
    Need(hex.size() % 2 == 0, "hex text");
    Bytes out;
    auto nib = [](char c) -> int
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        throw Error("hex text");
    };
    for (std::size_t i = 0; i < hex.size(); i += 2)
        out.push_back(std::uint8_t(nib(hex[i]) * 16 + nib(hex[i + 1])));
    return out;
}
// Kadan's uniforms.json (own_opaque_tool build, OwnOpaque::UniformFacts) as uniform facts, as
// own_material.material_facts makes them: one preshader per parameter, vectors then scalars, each texture a 2D texture
// parameter. indices: {texture name: its index in the material's referenced textures}, else the list's order.
inline UniformFacts MaterialFacts(const Json& own, const std::map<std::string, std::int32_t>& indices = {},
    const std::vector<Bytes>& collections = {})
{
    UniformFacts f;
    if (own.Has("vectors"))
        for (const auto& p : own.At("vectors").array)
        {
            VectorParameter v;
            v.name = p.At("name").Text();
            const auto& d = p.At("default").array;
            Need(d.size() == 4, "a vector default has four values");
            for (int i = 0; i < 4; i++)
                v.defaultBits[i] = std::uint32_t(JsonInt(d[i]));
            f.vectors.push_back(v);
        }
    if (own.Has("scalars"))
        for (const auto& p : own.At("scalars").array)
        {
            ScalarParameter s;
            s.name = p.At("name").Text();
            const auto& d = p.At("default").array;
            Need(!d.empty(), "a scalar default");
            s.defaultBits = std::uint32_t(JsonInt(d[0]));
            f.scalars.push_back(s);
        }
    f.collections = collections;
    for (std::size_t i = 0; i < f.vectors.size(); i++)
        f.vectorPrograms.push_back({PreshaderOp{"VectorParameter", {PreshaderOperand{std::int64_t(i)}}}});
    for (std::size_t i = 0; i < f.scalars.size(); i++)
        f.scalarPrograms.push_back({PreshaderOp{"ScalarParameter", {PreshaderOperand{std::int64_t(i)}}}});
    if (own.Has("textures") && !own.At("textures").array.empty())
    {
        auto& list = f.textures["Standard2D"];
        const auto& textures = own.At("textures").array;
        for (std::size_t i = 0; i < textures.size(); i++)
        {
            TextureParameter t;
            t.name = textures[i].At("name").Text();
            auto it = indices.find(*t.name);
            t.index = it != indices.end() ? it->second : std::int32_t(i);
            t.sampler = textures[i].Has("sampler") ? std::uint8_t(JsonInt(textures[i].At("sampler"))) : 0;
            list.push_back(t);
        }
    }
    return f;
}

// Uniform facts in their JSON form (own_map_check.py writes it from own_uniforms' facts): parameters as objects with
// name (or null), number, layer, association and default bits; textures {type: [{name, number, index, sampler,
// layer, association, vt_layer}]}; programs as [[op, operands...]] with 16-byte operands as hex; preshader_names as
// [[text, number]]; collections and flags as hex.
inline UniformFacts UniformFactsFromJson(const Json& j)
{
    UniformFacts f;
    auto info = [](ParameterInfo& p, const Json& o)
    {
        if (o.Has("name") && o.At("name").kind != Json::Null)
            p.name = o.At("name").Text();
        if (o.Has("number"))
            p.number = JsonInt(o.At("number"));
        if (o.Has("layer"))
            p.layer = std::int32_t(JsonInt(o.At("layer")));
        if (o.Has("association"))
            p.association = std::uint8_t(JsonInt(o.At("association")));
    };
    if (j.Has("scalars"))
        for (const auto& o : j.At("scalars").array)
        {
            ScalarParameter p;
            info(p, o);
            p.defaultBits = std::uint32_t(JsonInt(o.At("default")));
            f.scalars.push_back(p);
        }
    if (j.Has("vectors"))
        for (const auto& o : j.At("vectors").array)
        {
            VectorParameter p;
            info(p, o);
            const auto& d = o.At("default").array;
            Need(d.size() == 4, "a vector default has four values");
            for (int i = 0; i < 4; i++)
                p.defaultBits[i] = std::uint32_t(JsonInt(d[i]));
            f.vectors.push_back(p);
        }
    if (j.Has("textures"))
        for (const auto& [kind, list] : j.At("textures").object)
        {
            auto& out = f.textures[kind];
            for (const auto& o : list.array)
            {
                TextureParameter p;
                info(p, o);
                p.index = std::int32_t(JsonInt(o.At("index")));
                if (o.Has("sampler"))
                    p.sampler = std::uint8_t(JsonInt(o.At("sampler")));
                if (o.Has("vt_layer"))
                    p.vtLayer = std::uint8_t(JsonInt(o.At("vt_layer")));
                out.push_back(p);
            }
        }
    auto programs = [](const Json& list)
    {
        std::vector<PreshaderProgram> out;
        for (const auto& program : list.array)
        {
            PreshaderProgram ops;
            for (const auto& op : program.array)
            {
                Need(!op.array.empty(), "a preshader op");
                PreshaderOp o;
                o.name = op.array[0].Text();
                for (std::size_t i = 1; i < op.array.size(); i++)
                {
                    PreshaderOperand v;
                    if (op.array[i].kind == Json::String)
                    {
                        v.isBytes = true;
                        v.bytes = FromHex(op.array[i].Text());
                    }
                    else
                        v.number = JsonInt(op.array[i]);
                    o.operands.push_back(v);
                }
                ops.push_back(o);
            }
            out.push_back(ops);
        }
        return out;
    };
    if (j.Has("scalar_programs"))
        f.scalarPrograms = programs(j.At("scalar_programs"));
    if (j.Has("vector_programs"))
        f.vectorPrograms = programs(j.At("vector_programs"));
    if (j.Has("preshader_names"))
        for (const auto& n : j.At("preshader_names").array)
            f.preshaderNames.push_back({n.array.at(0).Text(), JsonInt(n.array.at(1))});
    if (j.Has("collections"))
        for (const auto& c : j.At("collections").array)
            f.collections.push_back(FromHex(c.Text()));
    if (j.Has("flags"))
        f.flags = FromHex(j.At("flags").Text());
    return f;
}

// A version in its JSON form: {cls, frequency, buffers {slot: struct}, program, material [slot, size] or null,
// collections [[slot, size] or null, ...]}.
inline Version VersionFromJson(const Json& j)
{
    Version v;
    v.cls = j.At("cls").Text();
    v.frequency = unsigned(JsonInt(j.At("frequency")));
    for (const auto& [slot, name] : j.At("buffers").object)
        v.buffers[std::uint16_t(std::stoul(slot))] = name.Text();
    v.program = j.At("program").Text();
    auto binding = [](const Json& b) -> std::optional<Binding>
    {
        if (b.kind == Json::Null)
            return std::nullopt;
        return Binding{std::uint16_t(JsonInt(b.array.at(0))), std::uint32_t(JsonInt(b.array.at(1)))};
    };
    if (j.Has("material"))
        v.material = binding(j.At("material"));
    if (j.Has("collections"))
        for (const auto& c : j.At("collections").array)
            v.collections.push_back(binding(c));
    // stage 7: type_hash and vf as 16 hex digits, tail and vf_params as hex, pass_slot, loose [[base, size, [[offset,
    // size], ...]], ...]
    auto present = [&](const char* key) { return j.Has(key) && j.At(key).kind != Json::Null; };
    if (present("type_hash"))
        v.typeHash = std::stoull(j.At("type_hash").Text(), nullptr, 16);
    if (present("vf"))
        v.vf = std::stoull(j.At("vf").Text(), nullptr, 16);
    if (present("tail"))
        v.tail = FromHex(j.At("tail").Text());
    if (present("vf_params"))
        v.vfParams = FromHex(j.At("vf_params").Text());
    if (present("pass_slot"))
        v.passSlot = std::uint16_t(JsonInt(j.At("pass_slot")));
    if (present("loose"))
        for (const auto& b : j.At("loose").array)
        {
            LooseBuffer l{std::uint16_t(JsonInt(b.array.at(0))), std::uint16_t(JsonInt(b.array.at(1))), {}};
            for (const auto& p : b.array.at(2).array)
                l.parameters.push_back({std::uint16_t(JsonInt(p.array.at(0))), std::uint16_t(JsonInt(p.array.at(1)))});
            v.loose.push_back(l);
        }
    return v;
}

}
}
} // namespace P4MapKit::CustomMaterial::OwnMap

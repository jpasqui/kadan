// The game's facts the canon rewrites read, and the helpers every rewrite uses: Python's random
// numbers, graph edits, the uniform set score and sampler types (part of material_graph_canon.h).
#pragma once
#include "material_graph.h"
#include "material_uniform_order.h"
#include <cctype>
#include <cstring>
#include <regex>
namespace P4MapKit
{
namespace Material
{

// The game's facts the rewrites read (material_graph_canon.Game): the master's own uniform expression set, its texture
// defaults, the sampler types of textures and its instanced vertex program. Absent facts leave their rewrites out.
struct MaterialCanonUniformSet
{
    bool Known = false; // a uniform set was read
    bool Error = false; // the master's map was not readable: the rewrites that compare sets are left out
    std::vector<std::string> Vectors, Scalars;
    std::vector<std::pair<std::string, std::vector<UniformOrder::TextureName>>> Textures;
    std::map<std::string, double> ScalarDefaults;
    std::map<std::string, std::vector<double>> VectorDefaults;
};
struct MaterialCanonGame
{
    MaterialCanonUniformSet Uniforms;
    std::map<std::string, std::string> DefaultTextures; // texture parameter -> object path, from the cooked master
    std::function<std::string(const std::string&)> SamplerType; // texture package -> sampler type, empty when unknown
    bool HasInstancedProgram = false; // FInstancedStaticMeshVertexFactory TBasePassVSFNoLightMapPolicy
    std::string InstancedProgram;
    std::vector<std::pair<std::string, int>> InstancedOutputs; // varying name, location
};
// The rewrites met a graph the Python model stops on (it has no form for it).
struct MaterialCanonError : std::runtime_error
{
    explicit MaterialCanonError(const std::string& why) : std::runtime_error(why)
    {
    }
};

// A uniform set in the form glsl_probe/uniform_sets.py writes it (game-uniform-sets.json): vectors, scalars, textures by
// type, scalar_defaults, vector_defaults; or {"error": ...}.
inline MaterialCanonUniformSet MaterialCanonUniformSetFromJson(const Json& set)
{
    MaterialCanonUniformSet out;
    if (set.kind != Json::Object)
        return out;
    out.Known = true;
    if (set.Has("error"))
    {
        out.Error = true;
        return out;
    }
    for (const auto& v : set.At("vectors").array)
        out.Vectors.push_back(v.Text());
    for (const auto& v : set.At("scalars").array)
        out.Scalars.push_back(v.Text());
    if (set.Has("textures"))
        for (const auto& kind : set.At("textures").object)
        {
            std::vector<UniformOrder::TextureName> names;
            for (const auto& n : kind.second.array)
            {
                UniformOrder::TextureName name;
                name.None = n.kind != Json::String;
                if (!name.None)
                    name.Name = n.string;
                names.push_back(name);
            }
            out.Textures.emplace_back(kind.first, names);
        }
    if (set.Has("scalar_defaults"))
        for (const auto& d : set.At("scalar_defaults").object)
            out.ScalarDefaults[d.first] = d.second.Numeric();
    if (set.Has("vector_defaults"))
        for (const auto& d : set.At("vector_defaults").object)
        {
            std::vector<double> v;
            for (const auto& x : d.second.array)
                v.push_back(x.Numeric());
            out.VectorDefaults[d.first] = v;
        }
    return out;
}
inline Json MaterialCanonUniformSetJson(const MaterialCanonUniformSet& set)
{
    if (!set.Known)
        return Json();
    auto out = Json::MakeObject();
    if (set.Error)
    {
        out["error"] = Json("the master's shader map is not readable");
        return out;
    }
    auto list = [](const std::vector<std::string>& v)
    {
        auto a = Json::MakeArray();
        for (const auto& s : v)
            a.array.push_back(Json(s));
        return a;
    };
    out["vectors"] = list(set.Vectors);
    out["scalars"] = list(set.Scalars);
    auto textures = Json::MakeObject();
    for (const auto& kind : set.Textures)
    {
        auto a = Json::MakeArray();
        for (const auto& n : kind.second)
            a.array.push_back(n.None ? Json() : Json(n.Name));
        textures[kind.first] = a;
    }
    out["textures"] = textures;
    auto scalars = Json::MakeObject(), vectors = Json::MakeObject();
    for (const auto& d : set.ScalarDefaults)
        scalars[d.first] = Json(d.second);
    for (const auto& d : set.VectorDefaults)
    {
        auto a = Json::MakeArray();
        for (const double x : d.second)
            a.array.push_back(Json(x));
        vectors[d.first] = a;
    }
    out["scalar_defaults"] = scalars;
    out["vector_defaults"] = vectors;
    return out;
}
// The facts as JSON: {"uniform_set", "default_textures": {parameter: object}, "sampler_types": {package: type or null},
// "instanced_program": text, "instanced_outputs": [[name, location]]}.
inline MaterialCanonGame MaterialCanonGameFromJson(const Json& facts)
{
    MaterialCanonGame out;
    if (facts.kind != Json::Object)
        return out;
    if (facts.Has("uniform_set"))
        out.Uniforms = MaterialCanonUniformSetFromJson(facts.At("uniform_set"));
    if (facts.Has("default_textures"))
        for (const auto& d : facts.At("default_textures").object)
            out.DefaultTextures[d.first] = d.second.Text();
    if (facts.Has("sampler_types"))
    {
        auto types = std::make_shared<std::map<std::string, std::string>>();
        for (const auto& d : facts.At("sampler_types").object)
            if (d.second.kind == Json::String)
                (*types)[d.first] = d.second.string;
        out.SamplerType = [types](const std::string& texture)
        {
            const auto f = types->find(texture.substr(0, texture.find('.')));
            return f == types->end() ? std::string() : f->second;
        };
    }
    if (facts.Has("instanced_program") && facts.At("instanced_program").kind == Json::String)
    {
        out.HasInstancedProgram = true;
        out.InstancedProgram = facts.At("instanced_program").string;
        if (facts.Has("instanced_outputs"))
            for (const auto& o : facts.At("instanced_outputs").array)
                out.InstancedOutputs.emplace_back(o.array.at(0).Text(), int(o.array.at(1).Numeric()));
    }
    return out;
}

namespace Canon
{
using namespace UniformOrder;

// ----- Python's random.Random(seed): uniform() and choice() -----

class PyRandom
{
    std::uint32_t mt[624];
    int mti = 625;
    void InitGenrand(std::uint32_t s)
    {
        mt[0] = s;
        for (mti = 1; mti < 624; ++mti)
            mt[mti] = 1812433253u * (mt[mti - 1] ^ (mt[mti - 1] >> 30)) + std::uint32_t(mti);
    }
    void InitByArray(const std::uint32_t* key, std::size_t length)
    {
        InitGenrand(19650218u);
        std::size_t i = 1, j = 0;
        for (std::size_t k = 624 > length ? 624 : length; k; --k)
        {
            mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1664525u)) + key[j] + std::uint32_t(j);
            ++i;
            ++j;
            if (i >= 624)
            {
                mt[0] = mt[623];
                i = 1;
            }
            if (j >= length)
                j = 0;
        }
        for (std::size_t k = 623; k; --k)
        {
            mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1566083941u)) - std::uint32_t(i);
            ++i;
            if (i >= 624)
            {
                mt[0] = mt[623];
                i = 1;
            }
        }
        mt[0] = 0x80000000u;
    }
    std::uint32_t Genrand()
    {
        static const std::uint32_t mag01[2] = {0u, 0x9908b0dfu};
        std::uint32_t y;
        if (mti >= 624)
        {
            int kk = 0;
            for (; kk < 624 - 397; ++kk)
            {
                y = (mt[kk] & 0x80000000u) | (mt[kk + 1] & 0x7fffffffu);
                mt[kk] = mt[kk + 397] ^ (y >> 1) ^ mag01[y & 1u];
            }
            for (; kk < 623; ++kk)
            {
                y = (mt[kk] & 0x80000000u) | (mt[kk + 1] & 0x7fffffffu);
                mt[kk] = mt[kk + (397 - 624)] ^ (y >> 1) ^ mag01[y & 1u];
            }
            y = (mt[623] & 0x80000000u) | (mt[0] & 0x7fffffffu);
            mt[623] = mt[396] ^ (y >> 1) ^ mag01[y & 1u];
            mti = 0;
        }
        y = mt[mti++];
        y ^= y >> 11;
        y ^= (y << 7) & 0x9d2c5680u;
        y ^= (y << 15) & 0xefc60000u;
        y ^= y >> 18;
        return y;
    }

public:
    explicit PyRandom(std::uint32_t seed)
    {
        const std::uint32_t key[1] = {seed};
        InitByArray(key, 1);
    }
    double Random()
    {
        const std::uint32_t a = Genrand() >> 5, b = Genrand() >> 6;
        return (a * 67108864.0 + b) * (1.0 / 9007199254740992.0);
    }
    double Uniform(double a, double b)
    {
        return a + (b - a) * Random();
    }
    std::size_t Below(std::size_t n)
    {
        int k = 0;
        for (std::size_t m = n; m; m >>= 1)
            ++k;
        const auto bits = [&]
        {
            return std::size_t(Genrand() >> (32 - k));
        };
        std::size_t r = bits();
        while (r >= n)
            r = bits();
        return r;
    }
};

// ----- graph helpers -----

inline const CanonNode* NodeOf(const CanonGraph& graph, const Json* link)
{
    return link && LinkNamesNode(*link) ? graph.Nodes.Find(LinkNode(*link)) : nullptr;
}
inline std::string NewId(const CanonGraph& graph, const std::string& kind)
{
    for (int n = 0;; ++n)
    {
        const auto id = kind + "_" + std::to_string(n);
        if (!graph.Nodes.Has(id))
            return id;
    }
}
inline std::string Add(CanonGraph& graph, const std::string& kind, Json props = Json::MakeObject(),
                       std::vector<std::pair<std::string, Json>> inputs = {})
{
    const auto id = NewId(graph, kind);
    CanonNode node;
    node.Type = kind;
    node.Props = props.kind == Json::Object ? props : Json::MakeObject();
    for (auto& input : inputs)
        node.Inputs.Set(input.first, input.second);
    graph.Nodes.Set(id, std::move(node));
    return id;
}
struct Where
{
    bool Pin = false;
    std::string Id, Key;
};
inline std::vector<Where> LinksTo(const CanonGraph& graph, const std::string& id)
{
    std::vector<Where> out;
    for (const auto& node : graph.Nodes.Items)
        for (const auto& input : node.second.Inputs.Items)
            if (LinkNamesNode(input.second) && LinkNode(input.second) == id)
                out.push_back({false, node.first, input.first});
    for (const auto& pin : graph.Outputs.Items)
        if (LinkNamesNode(pin.second) && LinkNode(pin.second) == id)
            out.push_back({true, pin.first, ""});
    return out;
}
inline void SetWhere(CanonGraph& graph, const Where& where, const Json& link)
{
    if (where.Pin)
        graph.Outputs.Set(where.Id, link);
    else if (auto* node = graph.Nodes.Find(where.Id))
        node->Inputs.Set(where.Key, link);
}
// A link a Python list is truthy as: a nonempty array whose first item names something.
inline bool Truthy(const Json* link)
{
    return link && link->kind == Json::Array && !link->array.empty();
}
inline void RemoveUnused(CanonGraph& graph)
{
    std::set<std::string> seen;
    std::vector<std::string> stack;
    for (const auto& pin : graph.Outputs.Items)
        if (Truthy(&pin.second) && pin.second.array[0].kind == Json::String)
            stack.push_back(pin.second.array[0].string);
    while (!stack.empty())
    {
        const auto id = stack.back();
        stack.pop_back();
        if (seen.count(id))
            continue;
        const auto* node = graph.Nodes.Find(id);
        if (!node)
            continue;
        seen.insert(id);
        for (const auto& input : node->Inputs.Items)
            if (Truthy(&input.second) && input.second.array[0].kind == Json::String)
                stack.push_back(input.second.array[0].string);
    }
    OrderedMap<CanonNode> kept;
    for (auto& item : graph.Nodes.Items)
        if (seen.count(item.first))
            kept.Set(item.first, std::move(item.second));
    graph.Nodes = std::move(kept);
}

// An operand of a binary node: an input's link, or its own constant ('const', value).
struct Operand
{
    bool Const = false;
    double Value = 0;
    Json Link;
};
inline std::vector<Operand> Operands(const CanonNode& node)
{
    std::vector<Operand> out;
    for (const auto& item : std::vector<std::pair<const char*, double>>{
             {"A", node.Type == "Add" || node.Type == "Subtract" ? 0.0 : 1.0}, {"B", 1.0}})
    {
        Operand o;
        const auto* link = node.Inputs.Find(item.first);
        if (Truthy(link))
            o.Link = *link;
        else
        {
            o.Const = true;
            o.Value = PropNumber(node.Props, std::string("Const") + item.first, item.second);
        }
        out.push_back(o);
    }
    return out;
}
inline std::vector<std::pair<Operand, Operand>> Pairs(const CanonNode& node)
{
    const auto o = Operands(node);
    return {{o[0], o[1]}, {o[1], o[0]}};
}
inline bool SameLink(const Json* a, const Json* b)
{
    if (!Truthy(a) || !Truthy(b))
        return false;
    return a->array[0].Dump() == b->array[0].Dump() && LinkChannels(*a) == LinkChannels(*b) &&
           LinkIndex(*a) == LinkIndex(*b);
}
inline bool Same(const Operand& a, const Operand& b)
{
    if (a.Const || b.Const)
        return a.Const && b.Const && a.Value == b.Value;
    return SameLink(&a.Link, &b.Link);
}
inline bool Constant(const CanonGraph& graph, const Operand& operand, double value)
{
    if (operand.Const)
        return operand.Value == value;
    const auto* node = NodeOf(graph, &operand.Link);
    return node && node->Type == "Constant" && PropNumber(node->Props, "R", 0.0) == value &&
           LinkChannels(operand.Link).empty();
}

// ----- the game's uniform set: how close a graph's predicted set comes to it -----

inline std::vector<TextureName> Normalized(const std::vector<TextureName>& names)
{
    std::vector<TextureName> out;
    for (auto n : names)
    {
        if (!n.None && (n.Name == "None" || n.Name == "?"))
        {
            n.None = true;
            n.Name.clear();
        }
        out.push_back(n);
    }
    return out;
}
template <class T>
int Lcs(const std::vector<T>& a, const std::vector<T>& b)
{
    std::vector<int> prev(b.size() + 1, 0);
    for (const auto& x : a)
    {
        std::vector<int> cur{0};
        for (std::size_t j = 0; j < b.size(); ++j)
            cur.push_back(x == b[j] ? prev[j] + 1 : std::max(prev[j + 1], cur[j]));
        prev = cur;
    }
    return prev.back();
}
inline bool SetScore(const CanonGraph& graph, const MaterialCanonUniformSet& uset, long& score)
{
    Prediction pr;
    try
    {
        pr = PredictUniformSet(graph);
    }
    catch (const Unsupported&)
    {
        return false;
    }
    score = 0;
    for (int k = 0; k < 2; ++k)
    {
        const auto& ours = k ? pr.Scalars : pr.Vectors;
        const auto& theirs = k ? uset.Scalars : uset.Vectors;
        const std::set<std::string> a(ours.begin(), ours.end()), b(theirs.begin(), theirs.end());
        long shared = 0, extra = 0;
        for (const auto& t : a)
            (b.count(t) ? shared : extra)++;
        score += 4 * shared - 4 * extra + Lcs(ours, theirs);
    }
    for (const auto& item : uset.Textures)
    {
        const auto* ours = pr.TexturesOf(item.first);
        score += Lcs(Normalized(ours ? *ours : std::vector<TextureName>()), Normalized(item.second));
    }
    return true;
}
inline bool Comparable(const MaterialCanonGame& game)
{
    return game.Uniforms.Known && !game.Uniforms.Error;
}

// ----- sampler types -----

inline std::string Package(const std::string& path)
{
    return path.substr(0, path.find('.'));
}
inline void SamplerTypes(CanonGraph& graph, const MaterialCanonGame& game)
{
    const auto& cooked = game.DefaultTextures;
    for (auto& item : graph.Nodes.Items)
    {
        auto& props = item.second.Props;
        if (props.object.empty())
            continue;
        const bool named = props.Has("ParameterName") && props.At("ParameterName").kind == Json::String;
        const auto found = named ? cooked.find(props.At("ParameterName").string) : cooked.end();
        if (found == cooked.end() || !props.Has("Texture") || props.At("Texture").kind != Json::String ||
            props.At("Texture").string.empty())
            continue;
        if (Package(props.At("Texture").string) == Package(found->second))
            props["Texture"] = Json(found->second);
    }
    static const std::map<std::string, std::string> engine = {
        {"/Engine/EngineResources/DefaultTextureCube_Low", "SAMPLERTYPE_Color"},
        {"/Engine/EngineResources/DefaultTextureCube", "SAMPLERTYPE_Color"}};
    for (auto& item : graph.Nodes.Items)
    {
        auto& props = item.second.Props;
        const auto& type = item.second.Type;
        if (!(type.compare(0, 13, "TextureSample") == 0 || type.compare(0, 13, "TextureObject") == 0) ||
            PropTruthy(props, "SamplerType") || !PropTruthy(props, "Texture"))
            continue;
        if (props.At("Texture").kind != Json::String)
            throw MaterialCanonError("a texture path that is not text");
        const auto texture = props.At("Texture").string;
        std::string kind = game.SamplerType ? game.SamplerType(texture) : std::string();
        if (kind.empty())
        {
            const auto e = engine.find(Package(texture));
            if (e != engine.end())
                kind = e->second;
        }
        if (!kind.empty())
            props["SamplerType"] = Json(kind);
    }
}
} // Canon
}
} // P4MapKit::Material

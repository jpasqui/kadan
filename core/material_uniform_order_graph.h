// Uniform order model (part of material_uniform_order.h): the insertion-ordered graph, links as the
// Python model reads them, and Python's own text and number forms.
#pragma once
#include "material_json.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <set>
#include <unordered_map>
namespace P4MapKit
{
namespace Material
{

// ----- an insertion-ordered graph -----

template <class T>
struct OrderedMap
{
    std::vector<std::pair<std::string, T>> Items;
    std::unordered_map<std::string, std::size_t> Index;
    const T* Find(const std::string& key) const
    {
        const auto at = Index.find(key);
        return at == Index.end() ? nullptr : &Items[at->second].second;
    }
    T* Find(const std::string& key)
    {
        const auto at = Index.find(key);
        return at == Index.end() ? nullptr : &Items[at->second].second;
    }
    bool Has(const std::string& key) const
    {
        return Index.count(key) != 0;
    }
    // A new key goes last; an existing one keeps its place (a Python dict).
    T& Set(const std::string& key, T value)
    {
        const auto at = Index.find(key);
        if (at != Index.end())
            return Items[at->second].second = std::move(value);
        Index[key] = Items.size();
        Items.emplace_back(key, std::move(value));
        return Items.back().second;
    }
    bool Erase(const std::string& key)
    {
        const auto at = Index.find(key);
        if (at == Index.end())
            return false;
        Items.erase(Items.begin() + std::ptrdiff_t(at->second));
        Index.clear();
        for (std::size_t i = 0; i < Items.size(); ++i)
            Index[Items[i].first] = i;
        return true;
    }
    std::vector<std::string> Keys() const
    {
        std::vector<std::string> out;
        for (const auto& item : Items)
            out.push_back(item.first);
        return out;
    }
    std::size_t Size() const
    {
        return Items.size();
    }
};

struct CanonNode
{
    std::string Type;
    OrderedMap<Json> Inputs; // input name -> link ([node, channels] or [node, channels, output])
    Json Props = Json::MakeObject();
    Json Rest = Json::MakeObject(); // any other key of the node, kept as it is
};
struct CanonGraph
{
    Json Settings = Json::MakeObject(); // every key but nodes and outputs
    OrderedMap<CanonNode> Nodes;
    OrderedMap<Json> Outputs;
};

// A Json graph in sorted key order (nodes, pins, each node's inputs): the order Kadan's Json holds them in, and the
// order material_graph_canon.py starts from (sorted_graph).
inline CanonGraph CanonGraphFrom(const Json& graph)
{
    CanonGraph out;
    for (const auto& item : graph.object)
        if (item.first != "nodes" && item.first != "outputs")
            out.Settings[item.first] = item.second;
    if (graph.Has("nodes"))
        for (const auto& item : graph.At("nodes").object)
        {
            CanonNode node;
            const auto& source = item.second;
            for (const auto& field : source.object)
            {
                if (field.first == "type")
                    node.Type = field.second.Text();
                else if (field.first == "inputs")
                {
                    if (field.second.kind == Json::Object)
                        for (const auto& input : field.second.object)
                            node.Inputs.Set(input.first, input.second);
                }
                else if (field.first == "props")
                {
                    if (field.second.kind == Json::Object)
                        node.Props = field.second;
                }
                else
                    node.Rest[field.first] = field.second;
            }
            out.Nodes.Set(item.first, std::move(node));
        }
    if (graph.Has("outputs"))
        for (const auto& item : graph.At("outputs").object)
            out.Outputs.Set(item.first, item.second);
    return out;
}
inline Json CanonGraphJson(const CanonGraph& graph)
{
    Json out = graph.Settings;
    auto nodes = Json::MakeObject(), outputs = Json::MakeObject();
    for (const auto& item : graph.Nodes.Items)
    {
        Json node = item.second.Rest;
        node["type"] = Json(item.second.Type);
        auto inputs = Json::MakeObject();
        for (const auto& input : item.second.Inputs.Items)
            inputs[input.first] = input.second;
        node["inputs"] = inputs;
        node["props"] = item.second.Props;
        nodes[item.first] = node;
    }
    for (const auto& item : graph.Outputs.Items)
        outputs[item.first] = item.second;
    out["nodes"] = nodes;
    out["outputs"] = outputs;
    return out;
}

// ----- links, as the Python model reads them -----

// A link is truthy as a Python list is (a nonempty array), and names a node when its first item is a nonempty string.
inline bool LinkNamesNode(const Json& link)
{
    return link.kind == Json::Array && !link.array.empty() && link.array[0].kind == Json::String &&
           !link.array[0].string.empty();
}
inline std::string LinkNode(const Json& link)
{
    return LinkNamesNode(link) ? link.array[0].string : std::string();
}
inline std::string LinkChannels(const Json& link)
{
    return link.kind == Json::Array && link.array.size() > 1 && link.array[1].kind == Json::String
               ? link.array[1].string
               : std::string();
}
inline double LinkIndex(const Json& link)
{
    return link.kind == Json::Array && link.array.size() > 2 && link.array[2].kind == Json::Number
               ? link.array[2].number
               : 0.0;
}
inline Json MakeLink(const std::string& node, const std::string& channels)
{
    return Json(std::vector<Json>{Json(node), Json(channels)});
}

// ----- Python's own text and number forms -----

// '%g' % x
inline std::string PyG(double x)
{
    if (std::isnan(x))
        return "nan";
    if (std::isinf(x))
        return x < 0 ? "-inf" : "inf";
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%g", x);
    return buffer;
}
// repr(str)
inline std::string PyRepr(const std::string& s)
{
    const bool single = s.find('\'') != std::string::npos && s.find('"') == std::string::npos;
    const char quote = single ? '"' : '\'';
    std::string out(1, quote);
    for (unsigned char c : s)
    {
        if (c == quote || c == '\\')
        {
            out += '\\';
            out += char(c);
        }
        else if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else if (c == '\t')
            out += "\\t";
        else if (c < 32 || c == 127)
        {
            char b[8];
            std::snprintf(b, sizeof b, "\\x%02x", c);
            out += b;
        }
        else
            out += char(c);
    }
    return out + quote;
}
// Python's min(a, b) and max(a, b): the first unless the second compares below (above) it.
inline double PyMin(double a, double b)
{
    return b < a ? b : a;
}
inline double PyMax(double a, double b)
{
    return b > a ? b : a;
}

}
} // P4MapKit::Material

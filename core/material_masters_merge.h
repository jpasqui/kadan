// Master material merge (part of material_masters.h): owners' graphs pooled into one graph, with
// the static switch defaults under which no two owner graphs contradict each other.
#pragma once
#include "material_source_import.h"
#include "material_camera_fade.h"
#include "material_game_reads.h"
#include <algorithm>
#include <cstring>
#include <functional>
#include <iomanip>
#include <set>
namespace P4MapKit
{
namespace Material
{

struct MaterialMasterStatic
{
    bool Mask = false, Value = false;
};
struct MaterialMasterOwner
{
    std::string Package;
    std::map<std::string, MaterialMasterStatic> Statics; // what the instance chain sets, nearest first
    Json Graph;
    bool Shapes = true; // false: kept apart, does not shape the master
};

inline std::map<std::string, bool> MaterialMasterSwitches(const std::map<std::string, MaterialMasterStatic>& statics)
{
    std::map<std::string, bool> out;
    for (const auto& item : statics)
        if (!item.second.Mask)
            out[item.first] = item.second.Value;
    return out;
}

inline std::string MaterialMasterLeaf(const std::string& package)
{
    const auto slash = package.find_last_of('/');
    return slash == std::string::npos ? package : package.substr(slash + 1);
}

// A pin only some blend modes use: a material of another blend mode has no shader that computes it.
inline bool MaterialMasterPinApplies(const Json& graph, const std::string& pin)
{
    if (pin != "OpacityMask" && pin != "Opacity")
        return true;
    if (!graph.Has("blend_mode") || graph.At("blend_mode").kind != Json::String)
        throw Refused("owner graph has no blend mode", "master merge settings");
    const auto mode = graph.At("blend_mode").Text();
    if (pin == "OpacityMask")
        return mode == "BLEND_Masked";
    return mode == "BLEND_Translucent" || mode == "BLEND_Additive" || mode == "BLEND_Modulate" ||
           mode == "BLEND_AlphaComposite" || mode == "BLEND_AlphaHoldout";
}
inline bool MaterialMasterSays(const MaterialMasterOwner& owner, const std::string& pin)
{
    return owner.Shapes && MaterialMasterPinApplies(owner.Graph, pin);
}

inline const Json& MaterialMasterNode(const Json& graph, const std::string& id)
{
    const auto& nodes = graph.At("nodes");
    const auto found = nodes.object.find(id);
    if (found == nodes.object.end() || found->second.kind != Json::Object)
        throw Refused("graph link names a node it does not have: " + id, "master merge graph");
    return found->second;
}
inline void MaterialMasterCheckLink(const Json& link)
{
    if (link.kind != Json::Array || link.array.empty() || link.array[0].kind != Json::String)
        throw Refused("graph link is not [node, channels]", "master merge graph");
}

// Nodes of several graphs in one: a node with the same type, properties and inputs is one node. The names
// are the decompiler's (type_index, in the order nodes are first made).
struct MaterialMasterPool
{
    Json Nodes = Json::MakeObject();
    std::map<std::string, std::string> ByKey;
    std::map<std::string, int> Count;
    std::string Node(const std::string& kind, const Json& inputs, const Json& props)
    {
        // Shader scheduling can swap operands of a commutative binary operation. Reuse the first node. A dot
        // product is one too: each component product is exact either way and the sum keeps its order.
        if ((kind == "Multiply" || kind == "Add" || kind == "Min" || kind == "Max" || kind == "DotProduct") &&
            inputs.Has("A") && inputs.Has("B"))
        {
            auto other = inputs;
            other["A"] = inputs.At("B");
            other["B"] = inputs.At("A");
            const auto found = ByKey.find(Array({kind, other, props}).Dump());
            if (found != ByKey.end())
                return found->second;
        }
        const auto key = Array({kind, inputs, props}).Dump();
        const auto found = ByKey.find(key);
        if (found != ByKey.end())
            return found->second;
        const auto id = kind + "_" + std::to_string(Count[kind]++);
        Nodes[id] = Object({{"type", kind}, {"inputs", inputs}, {"props", props}});
        ByKey[key] = id;
        return id;
    }
    Json Absorb(const Json& graph, const Json& link, std::map<std::string, std::string>& memo, std::size_t depth = 0)
    {
        MaterialMasterCheckLink(link);
        if (depth > 4096)
            throw Refused("graph is deeper than the merge follows", "master merge graph");
        const auto id = link.array[0].Text();
        if (!memo.count(id))
        {
            const auto& node = MaterialMasterNode(graph, id);
            auto inputs = Json::MakeObject();
            if (node.Has("inputs"))
                for (const auto& pin : node.At("inputs").object)
                    inputs[pin.first] = Absorb(graph, pin.second, memo, depth + 1);
            if (!node.Has("type") || node.At("type").kind != Json::String)
                throw Refused("graph node has no type", "master merge graph");
            memo[id] = Node(node.At("type").Text(), inputs, node.Has("props") ? node.At("props") : Json::MakeObject());
        }
        auto out = Json::MakeArray();
        out.array.push_back(Json(memo[id]));
        out.array.insert(out.array.end(), link.array.begin() + 1, link.array.end());
        return out;
    }
    // What a pin with nothing connected is (material_graph_eval.DEFAULTS), for a graph that leaves it out.
    Json Default(const std::string& pin)
    {
        static const std::map<std::string, std::vector<double>> values = {{"BaseColor", {0, 0, 0}},
                                                                          {"Metallic", {0}},
                                                                          {"Specular", {0.5}},
                                                                          {"Roughness", {0.5}},
                                                                          {"EmissiveColor", {0, 0, 0}},
                                                                          {"Opacity", {1}},
                                                                          {"OpacityMask", {1}},
                                                                          {"Normal", {0, 0, 1}},
                                                                          {"SubsurfaceColor", {1, 1, 1}},
                                                                          {"AmbientOcclusion", {1}},
                                                                          {"WorldPositionOffset", {0, 0, 0}}};
        const auto found = values.find(pin);
        if (found == values.end())
            throw Refused("a graph leaves out a pin that has no default: " + pin, "master merge default pin");
        std::string id;
        if (found->second.size() == 1)
            id = Node("Constant", Json::MakeObject(), Object({{"R", found->second[0]}}));
        else
        {
            auto value = Json::MakeArray();
            for (const auto v : found->second)
                value.array.push_back(Json(v));
            id = Node("Constant3Vector", Json::MakeObject(), Object({{"Constant", value}}));
        }
        return Array({id, ""});
    }
};

inline std::vector<std::string> MaterialMasterPins(const std::vector<MaterialMasterOwner>& owners)
{
    std::set<std::string> pins;
    for (const auto& owner : owners)
        for (const auto& pin : owner.Graph.At("outputs").object)
            pins.insert(pin.first);
    return {pins.begin(), pins.end()};
}

// [per owner: {pin: its link in the pool} for the pins the owner's graph says], pin by pin.
inline std::vector<std::map<std::string, Json>> MaterialMasterLinks(MaterialMasterPool& pool,
                                                                    const std::vector<MaterialMasterOwner>& owners,
                                                                    const std::vector<std::string>& pins)
{
    std::vector<std::map<std::string, Json>> out(owners.size());
    std::vector<std::map<std::string, std::string>> memos(owners.size());
    for (const auto& pin : pins)
        for (std::size_t i = 0; i < owners.size(); ++i)
            if (MaterialMasterSays(owners[i], pin))
            {
                const auto& outputs = owners[i].Graph.At("outputs");
                out[i][pin] =
                    outputs.Has(pin) ? pool.Absorb(owners[i].Graph, outputs.At(pin), memos[i]) : pool.Default(pin);
            }
    return out;
}

// The switch defaults under which no two graphs contradict each other (material_masters.defaults_of).
inline std::map<std::string, bool> MaterialMasterDefaults(const std::vector<MaterialMasterOwner>& owners,
                                                          const std::vector<std::string>& pins)
{
    MaterialMasterPool signaturePool;
    const auto links = MaterialMasterLinks(signaturePool, owners, pins);
    std::vector<std::map<std::string, std::string>> signatures(owners.size());
    for (std::size_t i = 0; i < owners.size(); ++i)
        for (const auto& link : links[i])
            signatures[i][link.first] = link.second.Dump();
    std::vector<std::map<std::string, bool>> set;
    for (const auto& owner : owners)
        set.push_back(MaterialMasterSwitches(owner.Statics));
    std::map<std::string, std::set<bool>> seen;
    for (const auto& switches : set)
        for (const auto& item : switches)
            seen[item.first].insert(item.second);
    std::map<std::string, bool> defaults;
    std::vector<std::string> open, names;
    for (const auto& item : seen)
    {
        names.push_back(item.first);
        if (item.second.size() == 1)
            defaults[item.first] = !*item.second.begin();
        else
            open.push_back(item.first);
    }
    std::pair<std::size_t, std::size_t> last{0, 0};
    const auto contradiction =
        [&](const std::map<std::string, bool>& decided, std::pair<std::size_t, std::size_t>& clash)
    {
        std::map<std::vector<bool>, std::map<std::string, std::pair<std::string, std::size_t>>> known;
        for (std::size_t i = 0; i < set.size(); ++i)
        {
            std::vector<bool> key;
            bool complete = true;
            for (const auto& name : names)
            {
                const auto own = set[i].find(name);
                if (own != set[i].end())
                {
                    key.push_back(own->second);
                    continue;
                }
                const auto made = decided.find(name);
                if (made == decided.end())
                {
                    complete = false;
                    break;
                }
                key.push_back(made->second);
            }
            if (!complete)
                continue;
            auto& said = known[key];
            for (const auto& signature : signatures[i])
            {
                const auto first = said.emplace(signature.first, std::make_pair(signature.second, i)).first->second;
                if (first.first != signature.second)
                {
                    clash = {first.second, i};
                    return true;
                }
            }
        }
        return false;
    };
    std::function<bool(std::size_t, std::map<std::string, bool>&)> search =
        [&](std::size_t at, std::map<std::string, bool>& decided)
    {
        std::pair<std::size_t, std::size_t> clash;
        if (contradiction(decided, clash))
        {
            last = clash;
            return false;
        }
        if (at == open.size())
            return true;
        for (const bool value : {true, false})
        {
            auto next = decided;
            next[open[at]] = value;
            if (search(at + 1, next))
            {
                decided = next;
                return true;
            }
        }
        return false;
    };
    auto decided = defaults;
    if (search(0, decided))
        return decided;
    // Two that set the very same switches differ whatever the defaults are: the pair to show.
    auto a = last.first, b = last.second;
    std::map<std::pair<std::string, std::string>, std::pair<std::string, std::size_t>> said;
    for (std::size_t i = 0; i < set.size(); ++i)
    {
        std::string key;
        for (const auto& item : set[i])
            key += item.first + (item.second ? "=1;" : "=0;");
        for (const auto& signature : signatures[i])
        {
            const auto first =
                said.emplace(std::make_pair(key, signature.first), std::make_pair(signature.second, i)).first->second;
            if (first.first != signature.second)
            {
                a = first.second;
                b = i;
            }
        }
    }
    std::set<std::string> masks;
    for (const auto& owner : owners)
        for (const auto& item : owner.Statics)
            if (item.second.Mask)
                masks.insert(item.first);
    if (!masks.empty())
    {
        std::string list;
        for (const auto& name : masks)
            list += (list.empty() ? "" : ", ") + name;
        throw Refused("graphs that differ where the instances set a static component mask (" + list +
                          "): " + MaterialMasterLeaf(owners[a].Package) + ", " + MaterialMasterLeaf(owners[b].Package),
                      "a static component mask parameter");
    }
    throw Refused("graphs that differ under the same switch values: " + MaterialMasterLeaf(owners[a].Package) + ", " +
                      MaterialMasterLeaf(owners[b].Package),
                  "graphs that differ under the same switch values");
}

struct MaterialMasterMerge
{
    Json Graph;
    std::map<std::string, bool> Defaults; // the switches the graph uses
    std::vector<std::string> SwitchesNotNeeded;
    Json Overrides = Json::MakeObject(); // {owner: {setting: value}} what an owner changes that is not a switch
};

inline bool MaterialMasterSameRenderSettings(const Json& a, const Json& b)
{
    if (a.kind != b.kind)
        return false;
    if (a.kind == Json::Number)
    {
        const float x = float(a.number), y = float(b.number);
        std::uint32_t p, q;
        std::memcpy(&p, &x, 4);
        std::memcpy(&q, &y, 4);
        return p == q;
    }
    if (a.kind == Json::Object)
    {
        if (a.object.size() != b.object.size())
            return false;
        for (const auto& item : a.object)
        {
            const auto other = b.object.find(item.first);
            if (other == b.object.end() || !MaterialMasterSameRenderSettings(item.second, other->second))
                return false;
        }
        return true;
    }
    if (a.kind == Json::Array)
        throw Refused("render settings hold a list", "master merge render settings");
    return a.Dump() == b.Dump();
}

inline MaterialMasterMerge MergeMaterialMasterGraphs(const std::vector<MaterialMasterOwner>& owners,
                                                     const std::vector<std::string>& pins,
                                                     const std::map<std::string, bool>& defaults)
{
    static const char* const settings[] = {"blend_mode", "two_sided", "shading_model", "clip", "tangent_space_normal"};
    MaterialMasterPool pool;
    std::vector<std::string> names;
    for (const auto& item : defaults)
        names.push_back(item.first);
    std::vector<std::map<std::string, bool>> full;
    for (const auto& owner : owners)
    {
        const auto switches = MaterialMasterSwitches(owner.Statics);
        std::map<std::string, bool> row;
        for (const auto& name : names)
        {
            const auto own = switches.find(name);
            row[name] = own != switches.end() ? own->second : defaults.at(name);
        }
        full.push_back(row);
    }
    std::set<std::string> used;
    std::function<Json(const std::vector<std::size_t>&, const std::map<std::size_t, Json>&,
                       const std::set<std::string>&)>
        tree = [&](const std::vector<std::size_t>& members, const std::map<std::size_t, Json>& links,
                   const std::set<std::string>& taken) -> Json
    {
        const auto distinct = [&](const std::vector<std::size_t>& set)
        {
            std::set<std::string> texts;
            for (const auto i : set)
                texts.insert(links.at(i).Dump());
            return texts.size();
        };
        if (distinct(members) == 1)
            return links.at(members.front());
        bool found = false;
        std::size_t best = 0;
        std::string bestName;
        std::vector<std::size_t> bestOn, bestOff;
        for (const auto& name : names)
        {
            if (taken.count(name))
                continue;
            std::vector<std::size_t> on, off;
            for (const auto i : members)
                (full[i].at(name) ? on : off).push_back(i);
            if (on.empty() || off.empty())
                continue;
            const auto score = distinct(on) + distinct(off);
            if (!found || score < best)
            {
                found = true;
                best = score;
                bestName = name;
                bestOn = on;
                bestOff = off;
            }
        }
        if (!found)
        {
            std::string list;
            for (std::size_t k = 0; k < members.size() && k < 6; ++k)
                list += (k ? ", " : "") + MaterialMasterLeaf(owners[members[k]].Package);
            throw Refused("graphs that differ under the same switch values: " + list,
                          "graphs that differ under the same switch values");
        }
        used.insert(bestName);
        auto next = taken;
        next.insert(bestName);
        const auto a = tree(bestOn, links, next);
        const auto b = tree(bestOff, links, next);
        return Array({pool.Node("StaticSwitchParameter", Object({{"A", a}, {"B", b}}),
                                Object({{"ParameterName", bestName}, {"DefaultValue", defaults.at(bestName)}})),
                      ""});
    };
    const auto said = MaterialMasterLinks(pool, owners, pins);
    auto outputs = Json::MakeObject();
    for (const auto& pin : pins)
    {
        std::vector<std::size_t> members;
        std::map<std::size_t, Json> links;
        for (std::size_t i = 0; i < owners.size(); ++i)
        {
            const auto found = said[i].find(pin);
            if (found != said[i].end())
            {
                members.push_back(i);
                links[i] = found->second;
            }
        }
        if (!members.empty())
            outputs[pin] = tree(members, links, {});
    }
    std::set<std::string> reached;
    std::vector<std::string> stack;
    for (const auto& link : outputs.object)
        stack.push_back(link.second.array[0].Text());
    while (!stack.empty())
    {
        const auto id = stack.back();
        stack.pop_back();
        if (!reached.insert(id).second)
            continue;
        for (const auto& input : pool.Nodes.At(id).At("inputs").object)
            stack.push_back(input.second.array[0].Text());
    }
    const auto& master = owners.front().Graph;
    MaterialMasterMerge result;
    result.Graph = Json::MakeObject();
    for (const auto* key : {"format", "domain", "render_settings"})
        if (master.Has(key))
            result.Graph[key] = master.At(key);
    auto nodes = Json::MakeObject();
    for (const auto& node : pool.Nodes.object)
        if (reached.count(node.first))
            nodes[node.first] = node.second;
    result.Graph["nodes"] = nodes;
    result.Graph["outputs"] = outputs;
    for (const auto* key : settings)
    {
        if (!master.Has(key))
            throw Refused(std::string("master graph has no setting ") + key, "master merge settings");
        result.Graph[key] = master.At(key);
    }
    for (const auto& name : names)
        if (used.count(name))
            result.Defaults[name] = defaults.at(name);
        else
            result.SwitchesNotNeeded.push_back(name);
    // What an instance changes that is not a switch: its own blend mode, sidedness, clip value, and the
    // renderer settings its own shaders were checked under.
    for (std::size_t i = 1; i < owners.size(); ++i)
    {
        auto changed = Json::MakeObject();
        const auto& graph = owners[i].Graph;
        for (const auto* key : settings)
        {
            if (!graph.Has(key))
                throw Refused(std::string("owner graph has no setting ") + key, "master merge settings");
            if (graph.At(key).Dump() != master.At(key).Dump())
                changed[key] = graph.At(key);
        }
        const Json base = master.Has("render_settings") ? master.At("render_settings") : Json(),
                   own = graph.Has("render_settings") ? graph.At("render_settings") : Json();
        if (!MaterialMasterSameRenderSettings(own, base))
            changed["render_settings"] = own;
        if (!changed.object.empty())
            result.Overrides[owners[i].Package] = changed;
    }
    return result;
}
inline MaterialMasterMerge MergeMaterialMasterGraphs(const std::vector<MaterialMasterOwner>& owners)
{
    const auto pins = MaterialMasterPins(owners);
    return MergeMaterialMasterGraphs(owners, pins, MaterialMasterDefaults(owners, pins));
}
}
} // P4MapKit::Material

// Master material checks (part of material_masters.h): two-sided variants, graph fingerprints
// and the recovered family record.
#pragma once
#include "material_masters_merge.h"
#include "material_source_common.h"
#include <unordered_map>
namespace P4MapKit
{
namespace Material
{
// Two-sided variants (material_two_sided_variants.py): a one-sided graph whose formulas are a two-sided
// owner's with TwoSidedSign taken as front facing (1) gets that owner's nodes back, so the sign the
// one-sided compile optimized away is not lost.
struct MaterialMasterSignature
{
    const Json& Graph;
    bool Front;
    std::map<std::string, std::string> Memo;
    // A node's text holds its inputs' short names, not their whole texts: a graph that reuses a node would
    // otherwise spell it out again at every use, and the texts grew to most of a material import. A name stands for
    // one text in this thread, so signatures are compared only with signatures made in the same thread.
    static std::string Name(std::string text)
    {
        static thread_local std::unordered_map<std::string, std::string> names;
        auto at = names.find(text);
        if (at == names.end())
        {
            std::string name = "#" + std::to_string(names.size());
            at = names.emplace(std::move(text), std::move(name)).first;
        }
        return at->second;
    }
    static std::string Number(double value)
    {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::setprecision(17) << value;
        return out.str();
    }
    std::string Of(const Json& link, std::size_t depth = 0)
    {
        MaterialMasterCheckLink(link);
        if (depth > 4096)
            throw Refused("graph is deeper than the signature follows", "master two-sided variants");
        const auto key = link.Dump();
        const auto memo = Memo.find(key);
        if (memo != Memo.end())
            return memo->second;
        const auto& node = MaterialMasterNode(Graph, link.array[0].Text());
        const auto kind = node.At("type").Text();
        const auto props = node.Has("props") ? node.At("props") : Json::MakeObject();
        const auto inputs = node.Has("inputs") ? node.At("inputs") : Json::MakeObject();
        auto rest = Json::MakeArray();
        rest.array.assign(link.array.begin() + 1, link.array.end());
        const bool whole = rest.Dump() == "[\"\"]";
        std::string result;
        if (kind == "TwoSidedSign" && Front)
        {
            if (!props.object.empty() || !inputs.object.empty() || !whole)
                throw Refused("two-sided sign is not an exact scalar node", "master two-sided variants");
            result = "scalar:1";
        }
        else if (kind == "Constant" && (whole || rest.Dump() == "[\"R\"]"))
        {
            double r = 0;
            if (props.Has("R"))
                r = props.At("R").Numeric();
            result = "scalar:" + Number(r);
        }
        else
        {
            std::vector<std::pair<std::string, std::string>> values;
            for (const auto& input : inputs.object)
                values.emplace_back(input.first, Of(input.second, depth + 1));
            const bool pair = values.size() == 2 && values[0].first == "A" && values[1].first == "B";
            if (kind == "Multiply" && pair && props.object.empty() && whole &&
                (values[0].second == "scalar:1" || values[1].second == "scalar:1"))
                result = values[0].second == "scalar:1" ? values[1].second : values[0].second;
            else if (pair &&
                     (kind == "Add" || kind == "Min" || kind == "Max" || kind == "Multiply" || kind == "DotProduct"))
            {
                std::vector<std::string> sorted{values[0].second, values[1].second};
                std::sort(sorted.begin(), sorted.end());
                result = "(" + kind + "|commutative|" + sorted[0] + "|" + sorted[1] + "|" + props.Dump() + "|" +
                         rest.Dump() + ")";
            }
            else
            {
                result = "(" + kind + "|";
                for (const auto& value : values)
                    result += value.first + "=" + value.second + "|";
                result += props.Dump() + "|" + rest.Dump() + ")";
            }
            if (!result.empty() && result.front() == '(')
                result = Name(std::move(result));
        }
        Memo[key] = result;
        return result;
    }
};
inline std::string MaterialMasterGraphSignature(const Json& graph, bool front)
{
    MaterialMasterSignature signature{graph, front, {}};
    std::string out;
    for (const auto& pin : graph.At("outputs").object)
        out += pin.first + "=" + signature.Of(pin.second) + ";";
    return out;
}
inline bool MaterialMasterCompatible(const Json& a, const Json& b, bool blendMode = true)
{
    for (const auto* key : {"domain", "blend_mode", "shading_model", "clip", "tangent_space_normal", "render_settings"})
    {
        if (!blendMode && std::string(key) == "blend_mode")
            continue;
        const Json x = a.Has(key) ? a.At(key) : Json(), y = b.Has(key) ? b.At(key) : Json();
        if (x.Dump() != y.Dump())
            return false;
    }
    return true;
}
inline bool MaterialMasterTruthy(const Json& graph, const char* key)
{
    return graph.Has(key) && graph.At(key).kind == Json::Boolean && graph.At(key).boolean;
}
inline bool MaterialMasterHasSign(const Json& graph)
{
    std::set<std::string> reached;
    std::vector<std::string> stack;
    for (const auto& link : graph.At("outputs").object)
    {
        MaterialMasterCheckLink(link.second);
        stack.push_back(link.second.array[0].Text());
    }
    while (!stack.empty())
    {
        const auto id = stack.back();
        stack.pop_back();
        if (!reached.insert(id).second)
            continue;
        const auto& node = MaterialMasterNode(graph, id);
        if (node.At("type").Text() == "TwoSidedSign")
            return true;
        if (node.Has("inputs"))
            for (const auto& input : node.At("inputs").object)
            {
                MaterialMasterCheckLink(input.second);
                stack.push_back(input.second.array[0].Text());
            }
    }
    return false;
}
struct MaterialMasterTwoSided
{
    std::vector<MaterialMasterOwner> Owners;
    std::set<std::string> Included;
    std::map<std::string, std::string> Sources;
};
// Unfolded: additive owners given in their unfolded form (MaterialMasterUnfoldAdditive), whose pins read as a
// translucent owner's do; their blend mode alone does not keep them from a translucent two-sided peer.
inline MaterialMasterTwoSided PrepareMaterialMasterTwoSidedVariants(const std::vector<MaterialMasterOwner>& owners,
                                                                    const std::set<std::string>& unfolded = {})
{
    MaterialMasterTwoSided result;
    result.Owners = owners;
    if (owners.empty() || MaterialMasterTruthy(owners.front().Graph, "two_sided") ||
        !owners.front().Graph.Has("domain") || owners.front().Graph.At("domain").Dump() != "\"MD_Surface\"")
        return result;
    std::vector<const MaterialMasterOwner*> candidates;
    for (const auto& owner : owners)
        if (MaterialMasterTruthy(owner.Graph, "two_sided") && MaterialMasterHasSign(owner.Graph) &&
            MaterialMasterCompatible(owners.front().Graph, owner.Graph))
            candidates.push_back(&owner);
    std::vector<MaterialMasterOwner> lifted;
    std::map<std::string, std::string> sources;
    // A candidate's front-facing signature is the same for every owner compared with it: made once, when first needed.
    std::map<const MaterialMasterOwner*, std::string> fronts;
    const auto front = [&](const MaterialMasterOwner* candidate) -> const std::string&
    {
        auto at = fronts.find(candidate);
        if (at == fronts.end())
            at = fronts.emplace(candidate, MaterialMasterGraphSignature(candidate->Graph, true)).first;
        return at->second;
    };
    for (const auto& owner : owners)
    {
        if (MaterialMasterTruthy(owner.Graph, "two_sided"))
        {
            lifted.push_back(owner);
            continue;
        }
        const auto original = MaterialMasterGraphSignature(owner.Graph, false);
        const MaterialMasterOwner* peer = nullptr;
        for (const auto* candidate : candidates)
            if (MaterialMasterCompatible(owner.Graph, candidate->Graph, !unfolded.count(owner.Package)) &&
                original == front(candidate))
            {
                peer = candidate;
                break;
            }
        if (!peer)
        {
            lifted.push_back(owner);
            continue;
        }
        auto restored = owner;
        restored.Graph["nodes"] = peer->Graph.At("nodes");
        restored.Graph["outputs"] = peer->Graph.At("outputs");
        lifted.push_back(restored);
        sources[owner.Package] = peer->Package;
    }
    if (!sources.count(owners.front().Package))
        return result;
    result.Owners = lifted;
    result.Sources = sources;
    for (const auto* candidate : candidates)
        result.Included.insert(candidate->Package);
    return result;
}

// Exact graph topology of a link, independent of node names, with the master's switches set as an owner
// sets them (tools/navigation/native_material_corpus.root_fingerprints, with the operands of Multiply,
// Add, Min and Max taken in either order as the merge takes them).
struct MaterialMasterFingerprint
{
    const Json& Graph;
    const std::map<std::string, bool>* Switches;
    std::map<std::string, std::string> Memo;
    std::string Of(const Json& link, std::size_t depth = 0)
    {
        MaterialMasterCheckLink(link);
        if (depth > 4096)
            throw Refused("graph is deeper than the check follows", "master merge check");
        const auto id = link.array[0].Text();
        const auto& node = MaterialMasterNode(Graph, id);
        const auto kind = node.At("type").Text();
        auto rest = Json::MakeArray();
        rest.array.assign(link.array.begin() + 1, link.array.end());
        if (Switches && kind == "StaticSwitchParameter" && rest.Dump() == "[\"\"]")
        {
            const auto name = node.At("props").At("ParameterName").Text();
            const auto value = Switches->find(name);
            if (value == Switches->end())
                throw Refused("merged graph switch has no value for an owner: " + name, "master merge check");
            const auto& inputs = node.At("inputs");
            const char* side = value->second ? "A" : "B";
            if (!inputs.Has(side))
                throw Refused("merged switch lacks a side", "master merge check");
            return Of(inputs.At(side), depth + 1);
        }
        const auto memo = Memo.find(id);
        std::string body;
        if (memo != Memo.end())
            body = memo->second;
        else
        {
            std::vector<std::pair<std::string, std::string>> inputs;
            if (node.Has("inputs"))
                for (const auto& input : node.At("inputs").object)
                    inputs.emplace_back(input.first, Of(input.second, depth + 1));
            // The merge takes a commutative operation with its operands swapped as the same node.
            if ((kind == "Multiply" || kind == "Add" || kind == "Min" || kind == "Max" || kind == "DotProduct") &&
                inputs.size() == 2 && inputs[0].first == "A" && inputs[1].first == "B" &&
                inputs[1].second < inputs[0].second)
                std::swap(inputs[0].second, inputs[1].second);
            body = "[" + Json(kind).Dump() + ",{";
            bool first = true;
            for (const auto& input : inputs)
            {
                body += (first ? "" : ",") + Json(input.first).Dump() + ":" + input.second;
                first = false;
            }
            body += "}," + (node.Has("props") ? node.At("props") : Json::MakeObject()).Dump() + "]";
            body = TextDigest(body);
            Memo[id] = body;
        }
        return body + rest.Dump();
    }
};

struct MaterialMasterFamily
{
    std::string Master;
    std::vector<std::string> Members;                    // the master and the instances given, whose chain ends at it
    std::map<std::string, std::string> OwnerOf;          // member: the material whose shaders draw it
    std::vector<MaterialMasterOwner> Recovered;          // every owner graph that came out, master first
    std::vector<std::string> Owners;                     // the owners the graph covers
    std::map<std::string, std::string> Refused, Refusal; // owner or member: why, and the refusal's name
    std::string RefusedMerge, RefusalMerge;
    bool HasGraph = false;
    MaterialMasterMerge Merge;
    std::vector<std::string> Users; // members the graph draws
    std::map<std::string, std::string> TwoSidedSources;
    std::string GameForm; // why the graph is not in UE's form of the game's map
                          // (material_canon_game.h), empty when it is or not asked
};

}
} // P4MapKit::Material

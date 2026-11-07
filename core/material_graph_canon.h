// A recovered game master graph in the form UE compiles to the game's own shader map
// (tools/navigation/material_graph_canon.py, the same rewrites in C++).
//
// The decompiler proves a graph by evaluating it against the game's programs. A proved graph can still compile in UE
// to another map: other parameters and folded uniform expressions in the Material buffer, a sample whose sampler type
// UE asks for, an output the game computes in its vertex program, the game's own clipping. Each rewrite keeps the
// value the graph computes and changes how it is written, from what the game's map holds:
//
// - textures: a texture parameter's default as the master's own import names the object, and a sample without a
//   sampler type gets its texture's;
// - XluCut (C4MaterialParam3 = C4XC_Enable): the game's clip below its global clipping plane, as Kadan's game Custom
//   node around AmbientOcclusion, in place of the decompiler's 0-or-1 opacity mask;
// - customized UVs from per-instance custom data, read from the game's instanced vertex program;
// - UE's own nodes where the decompiler wrote their expanded formula (the rim Lerp, Panner, Normalize), the game's
//   own uniform expressions and arithmetic forms, and the channel and operand forms the game's uniform set prefers.
//
// Every choice is the Python model's: the same node order (sorted keys, then insertion), the same uniform expression
// model (material_uniform_order.h) and the same random value checks (Python's Mersenne Twister, seeds 7 and 11).
#pragma once
#include "material_graph.h"
#include "material_uniform_order.h"
#include <cctype>
#include <cstring>
#include <regex>
#include "material_graph_canon_game.h"
#include "material_graph_canon_nodes.h"
#include "material_graph_canon_uniforms.h"
namespace P4MapKit
{
namespace Material
{
namespace Canon
{
using namespace UniformOrder;
// ----- forms UE compiles the same, chosen by the game's map -----

inline std::vector<Json> VectorParameterLinks(const CanonGraph& graph)
{
    std::vector<Json> seen;
    const auto visit = [&](const Json& link)
    {
        if (!Truthy(&link) || link.array[0].kind != Json::String || link.array[0].string.empty() ||
            (link.array.size() > 2 && LinkIndex(link) != 0))
            return;
        const auto* node = graph.Nodes.Find(link.array[0].string);
        const auto chans = LinkChannels(link);
        std::string ordered;
        for (const char c : std::string("RGBA"))
            if (chans.find(c) != std::string::npos)
                ordered += c;
        const auto key = MakeLink(link.array[0].string, chans);
        if (node && node->Type == "VectorParameter" && (chans.size() == 2 || chans.size() == 3) && chans != "RGB" &&
            chans == ordered &&
            std::find_if(seen.begin(), seen.end(), [&](const Json& s) { return s.Dump() == key.Dump(); }) == seen.end())
            seen.push_back(key);
    };
    for (const auto& node : graph.Nodes.Items)
        for (const auto& input : node.second.Inputs.Items)
            visit(input.second);
    for (const auto& pin : graph.Outputs.Items)
        visit(pin.second);
    return seen;
}
inline void Relink(CanonGraph& graph, const Json& old, const Json& fresh)
{
    const auto id = LinkNode(old), chans = LinkChannels(old), self = LinkNode(fresh);
    for (auto& node : graph.Nodes.Items)
    {
        if (node.first == self)
            continue;
        for (auto& input : node.second.Inputs.Items)
        {
            const auto& v = input.second;
            if (Truthy(&v) && v.array[0].kind == Json::String && v.array[0].string == id && LinkChannels(v) == chans &&
                !(v.array.size() > 2 && LinkIndex(v) != 0))
                input.second = fresh;
        }
    }
    for (auto& pin : graph.Outputs.Items)
    {
        const auto& v = pin.second;
        if (Truthy(&v) && v.array[0].kind == Json::String && v.array[0].string == id && LinkChannels(v) == chans)
            pin.second = fresh;
    }
}
inline int ChosenForms(CanonGraph& graph, const MaterialCanonGame& game)
{
    if (!Comparable(game))
        return 0;
    long best = 0;
    if (!SetScore(graph, game.Uniforms, best))
        return 0;
    int count = 0;
    for (const auto& link : VectorParameterLinks(graph))
    {
        const auto id = LinkNode(link);
        const auto chans = LinkChannels(link);
        bool chosen = false;
        CanonGraph choice;
        std::vector<std::function<Json(CanonGraph&)>> forms;
        if (chans.find('A') == std::string::npos)
            forms.push_back(
                [&](CanonGraph& g)
                { return MakeLink(Add(g, "ComponentMask", Masks(chans), {{"Input", MakeLink(id, "RGB")}}), ""); });
        forms.push_back(
            [&](CanonGraph& g)
            {
                Json acc = MakeLink(id, std::string(1, chans[0]));
                for (std::size_t i = 1; i < chans.size(); ++i)
                    acc = MakeLink(Add(g, "AppendVector", Json::MakeObject(),
                                       {{"A", acc}, {"B", MakeLink(id, std::string(1, chans[i]))}}),
                                   "");
                return acc;
            });
        for (const auto& form : forms)
        {
            auto trial = graph;
            const auto fresh = form(trial);
            Relink(trial, link, fresh);
            long score = 0;
            if (SetScore(trial, game.Uniforms, score) && score > best)
            {
                best = score;
                choice = std::move(trial);
                chosen = true;
            }
        }
        if (chosen)
        {
            graph = std::move(choice);
            ++count;
        }
    }
    for (int round = 0; round < 3; ++round)
    {
        bool improved = false;
        for (const auto& id : graph.Nodes.Keys())
        {
            auto& node = *graph.Nodes.Find(id);
            if (!InSet(node.Type, {"Add", "Multiply", "Min", "Max"}) || !Truthy(node.Inputs.Find("A")) ||
                !Truthy(node.Inputs.Find("B")))
                continue;
            const Json a = *node.Inputs.Find("A"), b = *node.Inputs.Find("B");
            node.Inputs.Set("A", b);
            node.Inputs.Set("B", a);
            long score = 0;
            if (SetScore(graph, game.Uniforms, score) && score > best)
            {
                best = score;
                ++count;
                improved = true;
            }
            else
            {
                auto& again = *graph.Nodes.Find(id);
                again.Inputs.Set("A", a);
                again.Inputs.Set("B", b);
            }
        }
        if (!improved)
            break;
    }
    if (count)
        RemoveUnused(graph);
    return count;
}
inline bool OneComponent(const CanonGraph& graph, const Json* link)
{
    const auto* node = NodeOf(graph, link);
    const auto chans = link ? LinkChannels(*link) : std::string();
    return node && (chans.size() == 1 || (chans.empty() && InSet(node->Type, {"Time", "ScalarParameter", "Constant"})));
}
inline int UniformUses(CanonGraph& graph, const MaterialCanonGame& game)
{
    if (!Comparable(game))
        return 0;
    const auto& uset = game.Uniforms;
    const auto names = Names(graph, uset);
    PyRandom rng(11);
    long best = 0;
    if (!SetScore(graph, uset, best))
        return 0;
    int count = 0;
    const auto pr = PredictUniformSet(graph);
    const auto ours = Joined(pr.Vectors, pr.Scalars);
    for (const auto& gtext : Joined(uset.Vectors, uset.Scalars))
    {
        if (Contains(ours, gtext))
            continue;
        Tree gtree;
        try
        {
            gtree = Parse(gtext, names.All);
        }
        catch (const ParseError&)
        {
            continue;
        }
        for (const auto& text : ours)
        {
            const auto origin = pr.Origins.find(text);
            if (origin == pr.Origins.end() || !Truthy(&origin->second))
                continue;
            try
            {
                if (!SameValue(Parse(text, names.All), gtree, names.Scalars, names.Vectors, rng))
                    continue;
            }
            catch (const ParseError&)
            {
                continue;
            }
            std::vector<std::pair<std::string, std::string>> uses;
            for (const auto& node : graph.Nodes.Items)
                for (const auto& input : node.second.Inputs.Items)
                {
                    const auto& l = input.second;
                    if (Truthy(&l) && l.array[0].Dump() == origin->second.array[0].Dump() &&
                        LinkChannels(l) == LinkChannels(origin->second))
                        uses.emplace_back(node.first, input.first);
                }
            for (const auto& use : uses)
            {
                auto trial = graph;
                const auto fresh = Build(trial, gtree, names.Scalars, names.Defaults);
                if (fresh.kind == Json::Null)
                    break;
                trial.Nodes.Find(use.first)->Inputs.Set(use.second, fresh);
                long score = 0;
                if (SetScore(trial, uset, score) && score > best)
                {
                    graph = std::move(trial);
                    best = score;
                    ++count;
                    break;
                }
            }
            break;
        }
    }
    if (count)
        RemoveUnused(graph);
    return count;
}
// Applies mutate to a copy and keeps it when the game's set is unknown or comes closer.
inline bool Try(CanonGraph& graph, const MaterialCanonGame& game, const std::function<bool(CanonGraph&)>& mutate)
{
    auto trial = graph;
    if (!mutate(trial))
        return false;
    if (Comparable(game))
    {
        long before = 0, after = 0;
        const bool a = SetScore(graph, game.Uniforms, before), b = SetScore(trial, game.Uniforms, after);
        if (!a || !b || after <= before)
            return false;
    }
    RemoveUnused(trial);
    graph = std::move(trial);
    return true;
}
// The translator's type, rather than a guess from a node's name (Lerp's alpha and SmoothStep must be scalar).
inline int Width(const CanonGraph& graph, const Json& link)
{
    try
    {
        Translator translator(graph);
        const auto value = translator.Link(link);
        return value ? Components(value->Type) : 0;
    }
    catch (const std::exception&)
    {
        return 0;
    }
}
inline int LerpDifference(CanonGraph& graph, const MaterialCanonGame& game)
{
    int count = 0;
    const auto snapshot = graph.Nodes.Items;
    for (const auto& item : snapshot)
    {
        const auto& node = item.second;
        if (node.Type != "Add")
            continue;
        std::vector<Json> hit;
        for (const auto& xm : Pairs(node))
        {
            if (xm.first.Const || xm.second.Const)
                continue;
            const auto* mn = NodeOf(graph, &xm.second.Link);
            if (!mn || mn->Type != "Multiply" || !LinkChannels(xm.second.Link).empty())
                continue;
            for (const auto& pd : Pairs(*mn))
            {
                if (pd.first.Const || pd.second.Const)
                    continue;
                const auto* dn = NodeOf(graph, &pd.second.Link);
                if (!dn || dn->Type != "Subtract" || !LinkChannels(pd.second.Link).empty())
                    continue;
                const auto* a = dn->Inputs.Find("A");
                const auto* b = dn->Inputs.Find("B");
                if (Truthy(a) && Truthy(b) && SameLink(b, &xm.first.Link) && Width(graph, pd.first.Link) == 1)
                    hit = {xm.first.Link, *a, pd.first.Link};
            }
            if (!hit.empty())
                break;
        }
        if (!hit.empty())
            count += KeepIfCloser(graph, game, item.first,
                                  NodeWith("LinearInterpolate", {{"A", hit[0]}, {"B", hit[1]}, {"Alpha", hit[2]}}));
    }
    if (count)
        RemoveUnused(graph);
    return count;
}
inline bool ConstantValue(const CanonGraph& graph, const Operand& operand, double& value)
{
    if (operand.Const)
    {
        value = operand.Value;
        return true;
    }
    const auto* node = NodeOf(graph, &operand.Link);
    if (node && node->Type == "Constant" && LinkChannels(operand.Link).empty())
    {
        value = PropNumber(node->Props, "R", 0);
        return true;
    }
    return false;
}
inline bool TimesMinusTwo(const CanonGraph& graph, const Operand& link, const Operand& s)
{
    if (link.Const)
        return false;
    const auto* node = NodeOf(graph, &link.Link);
    if (!node || node->Type != "Multiply" || !LinkChannels(link.Link).empty())
        return false;
    for (const auto& xc : Pairs(*node))
    {
        double k = 0;
        if (xc.first.Const || !ConstantValue(graph, xc.second, k))
            continue;
        if (Same(xc.first, s) && k == -2.0)
            return true;
        const auto* inner = NodeOf(graph, &xc.first.Link);
        if (inner && inner->Type == "Multiply" && LinkChannels(xc.first.Link).empty())
            for (const auto& yc : Pairs(*inner))
            {
                double k2 = 0;
                if (!yc.first.Const && ConstantValue(graph, yc.second, k2) && Same(yc.first, s) && k * k2 == -2.0)
                    return true;
            }
    }
    return false;
}
inline int SmoothstepForm(CanonGraph& graph, const MaterialCanonGame& game)
{
    int count = 0;
    const auto snapshot = graph.Nodes.Items;
    for (const auto& item : snapshot)
    {
        if (item.second.Type != "Multiply")
            continue;
        std::vector<Json> hit;
        for (const auto& sp : Pairs(item.second))
        {
            if (sp.first.Const || sp.second.Const)
                continue;
            const auto* sq = NodeOf(graph, &sp.first.Link);
            const auto* poly = NodeOf(graph, &sp.second.Link);
            if (!sq || !poly || sq->Type != "Multiply" || poly->Type != "Add")
                continue;
            const auto ss = Operands(*sq);
            if (ss[0].Const || !Same(ss[0], ss[1]) || Width(graph, ss[0].Link) != 1)
                continue;
            bool polynomial = false;
            for (const auto& qc : Pairs(*poly))
            {
                double c = 0;
                if (ConstantValue(graph, qc.second, c) && c == 3.0 && TimesMinusTwo(graph, qc.first, ss[0]))
                    polynomial = true;
            }
            if (!polynomial)
                continue;
            const auto* sat = NodeOf(graph, &ss[0].Link);
            const auto* div = sat && sat->Type == "Saturate" ? NodeOf(graph, sat->Inputs.Find("Input")) : nullptr;
            if (!div || div->Type != "Divide")
                continue;
            const auto* d = div->Inputs.Find("A");
            const auto* w = div->Inputs.Find("B");
            const auto* wn = NodeOf(graph, w);
            if (!Truthy(d) || !wn || wn->Type != "Subtract" || !LinkChannels(*w).empty())
                continue;
            const auto* hi = wn->Inputs.Find("A");
            const auto* lo = wn->Inputs.Find("B");
            if (Truthy(hi) && Truthy(lo))
            {
                hit = {*d, *hi, *lo};
                break;
            }
        }
        if (!hit.empty())
        {
            const auto value = Add(graph, "Add", Json::MakeObject(), {{"A", hit[0]}, {"B", hit[2]}});
            count += KeepIfCloser(
                graph, game, item.first,
                NodeWith("SmoothStep", {{"Min", hit[2]}, {"Max", hit[1]}, {"Value", MakeLink(value, "")}}));
        }
    }
    if (count)
        RemoveUnused(graph);
    return count;
}
inline int ClearCoatSaturate(CanonGraph& graph, const MaterialCanonGame& game)
{
    if (SettingText(graph.Settings, "shading_model", "") != "MSM_ClearCoat")
        return 0;
    return Try(graph, game,
               [](CanonGraph& trial)
               {
                   bool hit = false;
                   for (const auto* pin : {"ClearCoat", "ClearCoatRoughness"})
                   {
                       const auto* link = trial.Outputs.Find(pin);
                       const auto* node = NodeOf(trial, link);
                       if (node && node->Type == "Saturate" && LinkChannels(*link).empty() &&
                           Truthy(node->Inputs.Find("Input")))
                       {
                           const Json input = *node->Inputs.Find("Input");
                           trial.Outputs.Set(pin, input);
                           hit = true;
                       }
                   }
                   return hit;
               });
}
inline bool ScalarParameter(const CanonGraph& graph, const Json& link, const std::string& name)
{
    const auto* node = NodeOf(graph, &link);
    return node && node->Type == "ScalarParameter" && LinkChannels(link).empty() &&
           PropText(node->Props, "ParameterName", "") == name;
}
inline Json Channels(Json link, const std::string& channels)
{
    if (link.array.size() < 2)
        link.array.push_back(Json(channels));
    else
        link.array[1] = Json(channels);
    return link;
}
struct ChannelOperation
{
    std::string Id, Op;
    Json X;
};
inline std::vector<ChannelOperation> ChannelOps(const CanonGraph& graph, const std::string& channel,
                                                const std::function<bool(const Json&)>& parameter)
{
    std::vector<ChannelOperation> out;
    for (const auto& item : graph.Nodes.Items)
    {
        const auto& node = item.second;
        if (!InSet(node.Type, {"Add", "Subtract", "Multiply"}))
            continue;
        auto orders = Pairs(node);
        if (node.Type == "Subtract")
            orders.resize(1);
        for (const auto& xp : orders)
        {
            if (xp.first.Const || xp.second.Const || LinkChannels(xp.first.Link) != channel ||
                !parameter(xp.second.Link))
                continue;
            const auto* x = NodeOf(graph, &xp.first.Link);
            if (!x || x->Type != "VectorParameter")
            {
                out.push_back({item.first, node.Type, xp.first.Link});
                break;
            }
        }
    }
    return out;
}
inline int ChannelPairs(CanonGraph& graph, const MaterialCanonGame& game)
{
    if (!Comparable(game))
        return 0;
    int count = 0;
    for (const auto& text : game.Uniforms.Vectors)
    {
        const std::string suffix = ".rgb.rg";
        if (text.size() < suffix.size() || text.compare(text.size() - suffix.size(), suffix.size(), suffix) != 0)
            continue;
        const auto name = text.substr(0, text.size() - suffix.size());
        const auto parameter = [&](const Json& link, const std::string& channel)
        {
            const auto* node = NodeOf(graph, &link);
            return node && node->Type == "VectorParameter" && PropText(node->Props, "ParameterName", "") == name &&
                   LinkChannels(link) == channel;
        };
        for (const auto& first : ChannelOps(graph, "R", [&](const Json& link) { return parameter(link, "R"); }))
        {
            std::string second;
            const auto expected = Channels(first.X, "G");
            for (const auto& candidate : ChannelOps(graph, "G", [&](const Json& link) { return parameter(link, "G"); }))
                if (candidate.Op == first.Op && SameLink(&candidate.X, &expected))
                {
                    second = candidate.Id;
                    break;
                }
            if (second.empty())
                continue;
            if (Try(graph, game,
                    [&](CanonGraph& trial)
                    {
                        Json param;
                        for (const auto& o : Operands(*trial.Nodes.Find(first.Id)))
                            if (!o.Const && LinkNode(o.Link) != LinkNode(first.X))
                            {
                                param = o.Link;
                                break;
                            }
                        if (!Truthy(&param))
                            return false;
                        const auto mask =
                            Add(trial, "ComponentMask", Masks("RG"), {{"Input", MakeLink(LinkNode(param), "RGB")}});
                        const auto vec = Add(trial, first.Op, Json::MakeObject(),
                                             {{"A", Channels(first.X, "RG")}, {"B", MakeLink(mask, "")}});
                        Relink(trial, MakeLink(first.Id, ""), MakeLink(vec, "R"));
                        Relink(trial, MakeLink(second, ""), MakeLink(vec, "G"));
                        return true;
                    }))
            {
                ++count;
                break;
            }
        }
    }
    const auto names = Names(graph, game.Uniforms);
    for (const auto& text : game.Uniforms.Vectors)
    {
        if (text.compare(0, 8, "append1(") != 0)
            continue;
        Tree tree;
        try
        {
            tree = Parse(text, names.All);
        }
        catch (const ParseError&)
        {
            continue;
        }
        if (tree.Kind != "append" || tree.Count != 1 || tree.Args.size() != 2 || tree.Args[0].Kind != "param" ||
            !InSet(tree.Args[1].Kind, {"param", "const"}))
            continue;
        const auto p = tree.Args[0].Name, q = tree.Args[1].Name;
        const bool qConstant = tree.Args[1].Kind == "const";
        const auto qValue = qConstant ? tree.Args[1].Values.at(0) : 0.0;
        for (const auto& first :
             ChannelOps(graph, "R", [&](const Json& link) { return ScalarParameter(graph, link, p); }))
        {
            if (Try(graph, game,
                    [&](CanonGraph& trial)
                    {
                        Json pl, ql;
                        for (const auto& o : Operands(*trial.Nodes.Find(first.Id)))
                            if (!o.Const && ScalarParameter(trial, o.Link, p))
                            {
                                pl = o.Link;
                                break;
                            }
                        std::string second;
                        if (qConstant)
                        {
                            if (qValue != (first.Op == "Multiply" ? 1.0 : 0.0))
                                return false;
                            ql = MakeLink(Add(trial, "Constant", Object({{"R", qValue}})), "");
                        }
                        else
                        {
                            const auto expected = Channels(first.X, "G");
                            for (const auto& candidate : ChannelOps(trial, "G", [&](const Json& link)
                                                                    { return ScalarParameter(trial, link, q); }))
                                if (candidate.Op == first.Op && SameLink(&candidate.X, &expected))
                                {
                                    second = candidate.Id;
                                    break;
                                }
                            if (second.empty())
                                return false;
                            for (const auto& o : Operands(*trial.Nodes.Find(second)))
                                if (!o.Const && ScalarParameter(trial, o.Link, q))
                                {
                                    ql = o.Link;
                                    break;
                                }
                        }
                        const auto pair = Add(trial, "AppendVector", Json::MakeObject(), {{"A", pl}, {"B", ql}});
                        const auto vec = Add(trial, first.Op, Json::MakeObject(),
                                             {{"A", Channels(first.X, "RG")}, {"B", MakeLink(pair, "")}});
                        Relink(trial, MakeLink(first.Id, ""), MakeLink(vec, "R"));
                        Relink(trial, second.empty() ? MakeLink(LinkNode(first.X), "G") : MakeLink(second, ""),
                               MakeLink(vec, "G"));
                        return true;
                    }))
            {
                ++count;
                break;
            }
        }
    }
    return count;
}
inline bool DynamicScalar(const CanonGraph& graph, const Json* link, int depth = 0)
{
    const auto* node = NodeOf(graph, link);
    if (!node || depth > 8)
        return false;
    if (node->Type == "DynamicParameter")
    {
        const double index = LinkIndex(*link);
        return link->array.size() > 2 && index >= 0 && index <= 3 && index == std::floor(index);
    }
    if (node->Type == "Multiply" && LinkChannels(*link).empty())
    {
        for (const auto& o : Operands(*node))
            if (o.Const || !DynamicScalar(graph, &o.Link, depth + 1))
                return false;
        return true;
    }
    return false;
}
inline int ScaledAppends(CanonGraph& graph, const MaterialCanonGame& game)
{
    if (!Comparable(game))
        return 0;
    int count = 0;
    const auto snapshot = graph.Nodes.Keys();
    for (const auto& id : snapshot)
    {
        const auto* current = graph.Nodes.Find(id);
        if (!current || current->Type != "Multiply")
            continue;
        const auto node = *current;
        for (const auto& keys : std::vector<std::pair<std::string, std::string>>{{"A", "B"}, {"B", "A"}})
        {
            const auto* link = node.Inputs.Find(keys.first);
            const auto* other = node.Inputs.Find(keys.second);
            const auto* app = Truthy(link) && LinkChannels(*link).empty() ? NodeOf(graph, link) : nullptr;
            Json scale;
            if (DynamicScalar(graph, other))
                scale = *other;
            else if (Truthy(other) && LinkChannels(*other).empty())
            {
                const auto* twice = NodeOf(graph, other);
                if (twice && twice->Type == "AppendVector" &&
                    SameLink(twice->Inputs.Find("A"), twice->Inputs.Find("B")) &&
                    DynamicScalar(graph, twice->Inputs.Find("A")))
                    scale = *twice->Inputs.Find("A");
            }
            if (!app || app->Type != "AppendVector" || !Truthy(&scale))
                continue;
            const auto* a = app->Inputs.Find("A");
            const auto* b = app->Inputs.Find("B");
            const auto* u = Truthy(a) && LinkChannels(*a).empty() ? NodeOf(graph, a) : nullptr;
            const auto* v = Truthy(b) && LinkChannels(*b).empty() ? NodeOf(graph, b) : nullptr;
            if (!u || !v || u->Type != "ScalarParameter" || v->Type != "ScalarParameter")
                continue;
            const auto un = PropText(u->Props, "ParameterName", ""), vn = PropText(v->Props, "ParameterName", "");
            if (un.empty() || vn.empty() || Contains(game.Uniforms.Vectors, "append1(" + un + ", " + vn + ")") ||
                !Contains(game.Uniforms.Scalars, un) || !Contains(game.Uniforms.Scalars, vn))
                continue;
            const Json al = *a, bl = *b;
            count += Try(graph, game,
                         [&](CanonGraph& trial)
                         {
                             const auto ma =
                                 Add(trial, "Multiply", Json::MakeObject(), {{keys.first, al}, {keys.second, scale}});
                             const auto mb =
                                 Add(trial, "Multiply", Json::MakeObject(), {{keys.first, bl}, {keys.second, scale}});
                             const auto whole = Add(trial, "AppendVector", Json::MakeObject(),
                                                    {{"A", MakeLink(ma, "")}, {"B", MakeLink(mb, "")}});
                             Relink(trial, MakeLink(id, ""), MakeLink(whole, ""));
                             return true;
                         });
            break;
        }
    }
    return count;
}
inline Json Reciprocal(const CanonGraph& graph, const Json* link)
{
    const auto* n = NodeOf(graph, link);
    if (!n || n->Type != "Divide" || !LinkChannels(*link).empty())
        return Json();
    const auto o = Operands(*n);
    return Constant(graph, o[0], 1.0) && !o[1].Const ? o[1].Link : Json();
}
inline void Replace(CanonGraph& g, const std::string& id, CanonNode node)
{
    g.Nodes.Set(id, std::move(node));
}
inline int ArithmeticForms(CanonGraph& graph, const MaterialCanonGame& game)
{
    int count = 0;
    for (const auto& id : graph.Nodes.Keys())
    {
        const auto* live = graph.Nodes.Find(id);
        if (!live)
            continue;
        const CanonNode node = *live;
        const auto& kind = node.Type;
        const auto& ins = node.Inputs;
        const auto in = [&](const char* key) -> const Json*
        {
            return ins.Find(key);
        };
        if (kind == "Multiply")
        {
            for (const auto& keys : std::vector<std::pair<const char*, const char*>>{{"A", "B"}, {"B", "A"}})
            {
                const auto* rn = NodeOf(graph, in(keys.second));
                if (rn && rn->Type == "Divide" && Truthy(in(keys.first)) && LinkChannels(*in(keys.second)).empty() &&
                    Constant(graph, Operands(*rn)[0], 1.0) && Truthy(rn->Inputs.Find("B")))
                {
                    const Json a = *in(keys.first), x = *rn->Inputs.Find("B");
                    if (Try(graph, game,
                            [&](CanonGraph& g)
                            {
                                Replace(g, id, NodeWith("Divide", {{"A", a}, {"B", x}}));
                                return true;
                            }))
                    {
                        ++count;
                        break;
                    }
                }
            }
        }
        else if (kind == "Add" &&
                 (Reciprocal(graph, in("A")).kind != Json::Null || Reciprocal(graph, in("B")).kind != Json::Null))
        {
            // a * (1 / x) + (1 / x) as (a + 1) / x
            for (const auto& keys : std::vector<std::pair<const char*, const char*>>{{"A", "B"}, {"B", "A"}})
            {
                const Json x = Reciprocal(graph, in(keys.first));
                const auto* mn = NodeOf(graph, in(keys.second));
                if (x.kind == Json::Null || !mn || mn->Type != "Multiply" || !LinkChannels(*in(keys.second)).empty())
                    continue;
                for (const auto& ar : Pairs(*mn))
                {
                    const auto& a = ar.first;
                    const auto& r = ar.second;
                    Operand self;
                    self.Link = *in(keys.first);
                    if (a.Const || r.Const || !Same(r, self))
                        continue;
                    const Json al = a.Link;
                    count += Try(graph, game,
                                 [&](CanonGraph& g)
                                 {
                                     const auto one = Add(g, "Constant", Object({{"R", 1.0}}));
                                     const auto s =
                                         Add(g, "Add", Json::MakeObject(), {{"A", al}, {"B", MakeLink(one, "")}});
                                     Replace(g, id, NodeWith("Divide", {{"A", MakeLink(s, "")}, {"B", x}}));
                                     return true;
                                 });
                    break;
                }
            }
        }
        else if (kind == "Add")
        {
            for (const auto& keys : std::vector<std::pair<const char*, const char*>>{{"A", "B"}, {"B", "A"}})
            {
                // The nodes looked up here are read as they were, after a rewrite replaced the graph too (as Python
                // keeps reading its old node objects).
                const auto* pl = in(keys.first);
                const auto* nl = in(keys.second);
                const auto* found = NodeOf(graph, nl);
                if (!Truthy(pl) || !found || found->Type != "Multiply" || !LinkChannels(*nl).empty())
                    continue;
                const Json p = *pl;
                bool done = false;
                const CanonNode nn = *found;
                for (const auto& xc : Pairs(nn))
                {
                    if (xc.first.Const || !Constant(graph, xc.second, -1.0))
                        continue;
                    const Json x = xc.first.Link; // p + (-x)
                    done = Try(graph, game,
                               [&](CanonGraph& g)
                               {
                                   Replace(g, id, NodeWith("Subtract", {{"A", p}, {"B", x}}));
                                   return true;
                               });
                }
                if (!done)
                    for (const auto& nq : Pairs(nn))
                    {
                        const auto* negFound = nq.first.Const ? nullptr : NodeOf(graph, &nq.first.Link);
                        if (nq.second.Const || !negFound || negFound->Type != "Multiply")
                            continue;
                        const Json q = nq.second.Link;
                        const CanonNode negn = *negFound;
                        for (const auto& xc : Pairs(negn))
                        {
                            if (xc.first.Const || !Constant(graph, xc.second, -1.0))
                                continue;
                            const Json x = xc.first.Link; // p + (-x) * q
                            done = done ||
                                   Try(graph, game,
                                       [&](CanonGraph& g)
                                       {
                                           const auto m = Add(g, "Multiply", Json::MakeObject(), {{"A", x}, {"B", q}});
                                           Replace(g, id, NodeWith("Subtract", {{"A", p}, {"B", MakeLink(m, "")}}));
                                           return true;
                                       });
                        }
                    }
                if (done)
                {
                    ++count;
                    break;
                }
            }
        }
        else if (kind == "Divide" && Reciprocal(graph, in("A")).kind != Json::Null &&
                 Reciprocal(graph, in("B")).kind != Json::Null)
        {
            // (1 / x) / (1 / y) as y / x
            const Json x = Reciprocal(graph, in("A")), y = Reciprocal(graph, in("B"));
            count += Try(graph, game,
                         [&](CanonGraph& g)
                         {
                             Replace(g, id, NodeWith("Divide", {{"A", y}, {"B", x}}));
                             return true;
                         });
        }
        else if (kind == "Divide")
        {
            const auto* v = in("A");
            const auto* guard = in("B");
            const auto* gn = NodeOf(graph, guard);
            if (!Truthy(v) || !gn || gn->Type != "If")
                continue;
            const auto* absn = NodeOf(graph, gn->Inputs.Find("A"));
            const auto* sq = absn && absn->Type == "Abs" ? NodeOf(graph, absn->Inputs.Find("Input")) : nullptr;
            const auto* dot = sq && sq->Type == "SquareRoot" ? NodeOf(graph, sq->Inputs.Find("Input")) : nullptr;
            if (!dot || dot->Type != "DotProduct")
                continue;
            const auto* da = dot->Inputs.Find("A");
            const auto* db = dot->Inputs.Find("B");
            const Json vl = *v;
            if (SameLink(da, v) && SameLink(db, v))
            {
                count += Try(graph, game,
                             [&](CanonGraph& g)
                             {
                                 Replace(g, id, NodeWith("Normalize", {{"VectorInput", vl}}));
                                 return true;
                             });
            }
            else if (SameLink(da, db) && vl.array.size() > 1 && LinkChannels(vl).size() == 1 && da &&
                     vl.array[0].Dump() == da->array[0].Dump() &&
                     (LinkChannels(*da).empty() ? std::string("RGBA") : LinkChannels(*da)).find(LinkChannels(vl)) !=
                         std::string::npos)
            {
                // One channel of a guarded normalize: that channel of Normalize(v).
                const Json vec = *da;
                const auto ch = LinkChannels(vl);
                count +=
                    Try(graph, game,
                        [&](CanonGraph& g)
                        {
                            std::string shared;
                            for (const auto& n : g.Nodes.Items)
                                if (n.second.Type == "Normalize" && SameLink(n.second.Inputs.Find("VectorInput"), &vec))
                                {
                                    shared = n.first;
                                    break;
                                }
                            if (shared.empty())
                                shared = Add(g, "Normalize", Json::MakeObject(), {{"VectorInput", vec}});
                            const auto chans = LinkChannels(vec).empty() ? std::string("RGBA") : LinkChannels(vec);
                            const auto index = chans.find(ch);
                            for (const auto& where : LinksTo(g, id))
                                SetWhere(g, where, MakeLink(shared, std::string(1, "RGBA"[index])));
                            return true;
                        });
            }
        }
    }
    return count;
}
inline int Distributed(CanonGraph& graph, const MaterialCanonGame& game)
{
    int count = 0;
    const auto snapshot = graph.Nodes.Items;
    for (const auto& item : snapshot)
    {
        const auto& id = item.first;
        const auto& node = item.second;
        if (node.Type != "Multiply" || !graph.Nodes.Has(id))
            continue;
        for (const auto& keys : std::vector<std::pair<std::string, std::string>>{{"A", "B"}, {"B", "A"}})
        {
            const auto* x = node.Inputs.Find(keys.first);
            const auto* v = node.Inputs.Find(keys.second);
            const auto* vn = NodeOf(graph, v);
            if (!Truthy(x) || !vn || vn->Type != "AppendVector" || !LinkChannels(*v).empty() || !OneComponent(graph, x))
                continue;
            const auto* a = vn->Inputs.Find("A");
            const auto* b = vn->Inputs.Find("B");
            if (!Truthy(a) || !Truthy(b))
                continue;
            auto trial = graph;
            const auto ma = Add(trial, "Multiply", Json::MakeObject(), {{keys.first, *x}, {keys.second, *a}});
            const auto mb = Add(trial, "Multiply", Json::MakeObject(), {{keys.first, *x}, {keys.second, *b}});
            Replace(trial, id, NodeWith("AppendVector", {{"A", MakeLink(ma, "")}, {"B", MakeLink(mb, "")}}));
            if (Comparable(game))
            {
                long before = 0, after = 0;
                const bool p = SetScore(graph, game.Uniforms, before), q = SetScore(trial, game.Uniforms, after);
                if (!p || !q || after <= before)
                    continue;
            }
            graph = std::move(trial);
            ++count;
            break;
        }
    }
    if (count)
        RemoveUnused(graph);
    return count;
}
} // Canon

// The graph in the form UE compiles to the game's map (material_graph_canon.canon): its nodes and pins in sorted key
// order, then each rewrite in the Python model's order. MaterialCanonError when the model has no form for the graph.
inline Json CanonMaterialGraph(const Json& graph, const MaterialCanonGame& game)
{
    if (graph.kind != Json::Object || !graph.Has("nodes"))
        return graph;
    try
    {
        auto g = CanonGraphFrom(graph);
        using namespace Canon;
        SamplerTypes(g, game);
        XluCut(g, game);
        CustomizedUvs(g, game);
        ClearCoatSaturate(g, game);
        RimLerp(g, game);
        LerpDifference(g, game);
        ChannelPairs(g, game);
        ScaledAppends(g, game);
        Panner(g, game);
        ArithmeticForms(g, game);
        SmoothstepForm(g, game);
        UniformExpressions(g, game);
        Panner(g, game);
        Distributed(g, game);
        UniformUses(g, game);
        ChosenForms(g, game);
        return CanonGraphJson(g);
    }
    catch (const MaterialCanonError&)
    {
        throw;
    }
    catch (const std::exception& error)
    {
        throw MaterialCanonError(error.what());
    }
}
}
} // P4MapKit::Material

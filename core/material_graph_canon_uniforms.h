// Canon rewrites: the game's own uniform expressions (part of material_graph_canon.h).
#pragma once
#include "material_graph_canon_nodes.h"
namespace P4MapKit
{
namespace Material
{
namespace Canon
{
using namespace UniformOrder;
// ----- the game's own uniform expressions -----

inline std::string ParamNode(CanonGraph& graph, const std::string& name, bool scalar,
                             const std::map<std::string, Json>& defaults)
{
    const std::string kind = scalar ? "ScalarParameter" : "VectorParameter";
    for (const auto& node : graph.Nodes.Items)
        if (node.second.Type == kind && node.second.Props.Has("ParameterName") &&
            node.second.Props.At("ParameterName").kind == Json::String &&
            node.second.Props.At("ParameterName").string == name)
            return node.first;
    const auto d = defaults.find(name);
    const Json value = d != defaults.end() ? d->second
                       : scalar            ? Json(0.0)
                                           : Json(std::vector<Json>{Json(0.0), Json(0.0), Json(0.0), Json(1.0)});
    return Add(graph, kind, Object({{"ParameterName", Json(name)}, {"DefaultValue", value}}));
}
inline Json Masks(const std::string& letters)
{
    auto props = Json::MakeObject();
    for (const char c : letters)
        props[std::string(1, char(std::toupper(static_cast<unsigned char>(c))))] = Json(true);
    return props;
}
// Nodes whose UE uniform expression is the tree, as a link; null when UE has no node for it.
inline Json Build(CanonGraph& graph, const Tree& tree, const std::set<std::string>& scalars,
                  const std::map<std::string, Json>& defaults)
{
    if (tree.Kind == "param")
        return MakeLink(ParamNode(graph, tree.Name, scalars.count(tree.Name) != 0, defaults), "");
    if (tree.Kind == "const")
    {
        const auto& v = tree.Values;
        int same = 1;
        for (std::size_t i = 1; i < 4; ++i)
            same += v[i] == v[0];
        if (same == 4)
            return MakeLink(Add(graph, "Constant", Object({{"R", v[0]}})), "");
        if (v[2] == 0.0 && v[3] == 0.0)
            return MakeLink(Add(graph, "Constant2Vector", Object({{"R", v[0]}, {"G", v[1]}})), "");
        if (v[3] == 0.0)
            return MakeLink(
                Add(graph, "Constant3Vector",
                    Object({{"Constant", Json(std::vector<Json>{Json(v[0]), Json(v[1]), Json(v[2]), Json(1.0)})}})),
                "");
        return MakeLink(
            Add(graph, "Constant4Vector",
                Object({{"Constant", Json(std::vector<Json>{Json(v[0]), Json(v[1]), Json(v[2]), Json(v[3])})}})),
            "");
    }
    if (tree.Kind == "swz")
    {
        const auto& inner = tree.Args[0];
        const auto& letters = tree.Name;
        std::string ordered;
        for (const char c : std::string("rgba"))
            if (letters.find(c) != std::string::npos)
                ordered += c;
        if (letters != ordered)
            return Json();
        std::string upper;
        for (const char c : letters)
            upper += char(std::toupper(static_cast<unsigned char>(c)));
        if (inner.Kind == "param" && !scalars.count(inner.Name))
        {
            if (InSet(letters, {"rgb", "r", "g", "b", "a"}))
                return MakeLink(ParamNode(graph, inner.Name, false, defaults), upper);
            return Json();
        }
        if (inner.Kind == "swz" && inner.Args[0].Kind == "param" && inner.Name == "rgb" &&
            !scalars.count(inner.Args[0].Name))
        {
            const auto base = MakeLink(ParamNode(graph, inner.Args[0].Name, false, defaults), "RGB");
            return MakeLink(Add(graph, "ComponentMask", Masks(letters), {{"Input", base}}), "");
        }
        const auto base = Build(graph, inner, scalars, defaults);
        if (base.kind == Json::Null)
            return Json();
        return MakeLink(Add(graph, "ComponentMask", Masks(letters), {{"Input", base}}), "");
    }
    if (tree.Kind == "append" || tree.Kind == "op")
    {
        const auto a = Build(graph, tree.Args[0], scalars, defaults);
        const auto b = Build(graph, tree.Args[1], scalars, defaults);
        if (a.kind == Json::Null || b.kind == Json::Null)
            return Json();
        const std::string kind = tree.Kind == "append" ? "AppendVector"
                                 : tree.Name == "+"    ? "Add"
                                 : tree.Name == "-"    ? "Subtract"
                                 : tree.Name == "*"    ? "Multiply"
                                                       : "Divide";
        return MakeLink(Add(graph, kind, Json::MakeObject(), {{"A", a}, {"B", b}}), "");
    }
    if (tree.Kind == "fn")
    {
        static const std::map<std::string, std::string> nodes = {
            {"saturate", "Saturate"}, {"floor", "Floor"},     {"ceil", "Ceil"}, {"frac", "Frac"},
            {"abs", "Abs"},           {"sqrt", "SquareRoot"}, {"sign", "Sign"}, {"min", "Min"},
            {"max", "Max"},           {"dot", "DotProduct"},  {"sin", "Sine"},  {"cos", "Cosine"}};
        const auto found = nodes.find(tree.Name);
        if (found != nodes.end() || tree.Name == "clamp")
        {
            std::vector<Json> args;
            bool missing = false;
            for (const auto& a : tree.Args)
            {
                args.push_back(Build(graph, a, scalars, defaults));
                missing |= args.back().kind == Json::Null;
            }
            if (missing)
                return Json();
            if (tree.Name == "clamp")
                return MakeLink(Add(graph, "Clamp", Json::MakeObject(),
                                    {{"Input", args.at(0)}, {"Min", args.at(1)}, {"Max", args.at(2)}}),
                                "");
            const auto& name = found->second;
            if (InSet(name, {"Min", "Max", "DotProduct"}))
                return MakeLink(Add(graph, name, Json::MakeObject(), {{"A", args.at(0)}, {"B", args.at(1)}}), "");
            if (InSet(name, {"Sine", "Cosine"}))
                return MakeLink(Add(graph, name, Object({{"Period", 0.0}}), {{"Input", args.at(0)}}), "");
            return MakeLink(Add(graph, name, Json::MakeObject(), {{"Input", args.at(0)}}), "");
        }
    }
    return Json();
}
inline bool SameValue(const Tree& a, const Tree& b, const std::set<std::string>& scalars,
                      const std::set<std::string>& vectors, PyRandom& rng, int draws = 4)
{
    for (int d = 0; d < draws; ++d)
    {
        const auto pick = [&]
        {
            const double u[3] = {rng.Uniform(-2.0, 2.0), rng.Uniform(0.2, 2.0), rng.Uniform(-1e-4, 1e-4)};
            return u[rng.Below(3)];
        };
        std::map<std::string, double> sv;
        std::map<std::string, std::vector<double>> vv;
        for (const auto& n : scalars)
            sv[n] = pick();
        for (const auto& n : vectors)
        {
            std::vector<double> v;
            for (int i = 0; i < 4; ++i)
                v.push_back(pick());
            vv[n] = v;
        }
        std::vector<double> x, y;
        try
        {
            x = Evaluate(a, sv, vv);
            y = Evaluate(b, sv, vv);
        }
        catch (const EvalFailure&)
        {
            return false;
        }
        catch (const ParseError&)
        {
            return false;
        }
        if (x.size() != y.size())
            return false;
        for (std::size_t i = 0; i < x.size(); ++i)
            if (std::fabs(x[i] - y[i]) > 1e-5 * PyMax(1.0, std::fabs(x[i])))
                return false;
    }
    return true;
}
// Every input and pin linked to origin's value takes link instead (link has the same value).
inline void Rewire(CanonGraph& graph, const Json& origin, const Json& link)
{
    const auto id = LinkNode(origin);
    const auto chans = LinkChannels(origin);
    const double index = LinkIndex(origin);
    const auto fix = [&](const Json& old) -> Json
    {
        if (!Truthy(&old) || old.array[0].kind != Json::String || old.array[0].string != id || LinkIndex(old) != index)
            return old;
        const auto oldChans = LinkChannels(old);
        if (!chans.empty())
            return oldChans == chans ? link : old;
        if (!oldChans.empty() && !LinkChannels(link).empty())
            return old;
        return !oldChans.empty() ? MakeLink(LinkNode(link), oldChans) : link;
    };
    const auto self = LinkNode(link);
    for (auto& node : graph.Nodes.Items)
    {
        if (node.first == self)
            continue;
        for (auto& input : node.second.Inputs.Items)
            input.second = fix(input.second);
    }
    for (auto& pin : graph.Outputs.Items)
        pin.second = fix(pin.second);
}
struct GameNames
{
    std::set<std::string> Scalars, Vectors, All;
    std::map<std::string, Json> Defaults;
};
inline GameNames Names(const CanonGraph& graph, const MaterialCanonUniformSet& uset)
{
    GameNames out;
    for (const auto& d : uset.ScalarDefaults)
    {
        out.Scalars.insert(d.first);
        out.Defaults[d.first] = Json(d.second);
    }
    for (const auto& d : uset.VectorDefaults)
    {
        out.Vectors.insert(d.first);
        std::vector<Json> v;
        for (const double x : d.second)
            v.push_back(Json(x));
        out.Defaults[d.first] = Json(v);
    }
    for (const auto& node : graph.Nodes.Items)
    {
        const auto& p = node.second.Props;
        if (!p.Has("ParameterName") || p.At("ParameterName").kind != Json::String)
            continue;
        if (node.second.Type == "ScalarParameter")
            out.Scalars.insert(p.At("ParameterName").string);
        else if (node.second.Type == "VectorParameter")
            out.Vectors.insert(p.At("ParameterName").string);
    }
    out.All = out.Scalars;
    out.All.insert(out.Vectors.begin(), out.Vectors.end());
    return out;
}
inline std::vector<std::string> Joined(const std::vector<std::string>& a, const std::vector<std::string>& b)
{
    auto out = a;
    out.insert(out.end(), b.begin(), b.end());
    return out;
}
inline bool Contains(const std::vector<std::string>& list, const std::string& text)
{
    return std::find(list.begin(), list.end(), text) != list.end();
}
inline bool Head(const Tree& a, const Tree& b)
{
    const bool named = InSet(a.Kind, {"op", "fn", "swz"});
    return a.Kind == b.Kind && (!named || a.Name == b.Name);
}

inline int UniformExpressions(CanonGraph& graph, const MaterialCanonGame& game)
{
    if (!Comparable(game))
        return 0;
    const auto& uset = game.Uniforms;
    const auto names = Names(graph, uset);
    const auto gameTexts = Joined(uset.Vectors, uset.Scalars);
    std::map<std::string, Tree> trees;
    for (const auto& text : gameTexts)
    {
        try
        {
            trees.emplace(text, Parse(text, names.All));
        }
        catch (const ParseError&)
        {
        }
    }
    PyRandom rng(7);
    int count = 0;
    std::set<std::string> tried;
    for (int round = 0; round < 64; ++round)
    {
        const auto pr = PredictUniformSet(graph);
        const auto ours = Joined(pr.Vectors, pr.Scalars);
        std::vector<std::string> gameOnly;
        for (const auto& t : gameTexts)
            if (!Contains(ours, t) && trees.count(t))
                gameOnly.push_back(t);
        bool done = false;
        for (const auto& text : ours)
        {
            const auto origin = pr.Origins.find(text);
            if (Contains(gameTexts, text) || tried.count(text) || origin == pr.Origins.end() ||
                !Truthy(&origin->second))
                continue;
            tried.insert(text);
            Tree mine;
            try
            {
                mine = Parse(text, names.All);
            }
            catch (const ParseError&)
            {
                continue;
            }
            std::vector<std::string> matches;
            for (const auto& g : gameOnly)
                if (SameValue(mine, trees.at(g), names.Scalars, names.Vectors, rng))
                    matches.push_back(g);
            if (matches.size() > 1)
            {
                std::vector<std::string> kept;
                for (const auto& g : matches)
                    if (Head(trees.at(g), mine))
                        kept.push_back(g);
                matches = kept;
            }
            if (matches.size() != 1)
                continue;
            const auto link = Build(graph, trees.at(matches[0]), names.Scalars, names.Defaults);
            if (link.kind == Json::Null)
                continue;
            Rewire(graph, origin->second, link);
            RemoveUnused(graph);
            ++count;
            done = true;
            break;
        }
        if (!done)
            break;
    }
    return count;
}
} // Canon
}
} // P4MapKit::Material

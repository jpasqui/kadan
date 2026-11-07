// Canon rewrites: XluCut, customized UVs from per-instance custom data and UE's own nodes for
// expanded formulas (part of material_graph_canon.h).
#pragma once
#include "material_graph_canon_game.h"
namespace P4MapKit
{
namespace Material
{
namespace Canon
{
using namespace UniformOrder;
// ----- XluCut -----

inline const char* XluCutCode()
{
    return "if (ResolvedView.C4GlobalClippingPlane.x > 0.5 && Enable > 1.0 && !((Position.z - Height) * "
           "(ResolvedView.C4GlobalClippingPlane.y * ResolvedView.C4XluGlobalClippingPlaneSign) > 0.0)) "
           "{ clip(-1.0); } return Value;";
}
// The node a link points at when it is a scalar parameter, or empty.
inline std::string XluCutScalarAt(const CanonGraph& graph, const Json* link)
{
    if (!Truthy(link) || link->array[0].kind != Json::String)
        return std::string();
    const auto* node = graph.Nodes.Find(link->array[0].string);
    return node && node->Type == "ScalarParameter" ? link->array[0].string : std::string();
}
// The decompiler's stand-in for the clip, read for its two scalars: the height test is an If whose A input subtracts a
// scalar parameter from the world position; the enable switch is the one other scalar parameter the If nodes it
// chains compare. False when the graph holds no such test.
inline bool XluCutStandIn(const CanonGraph& graph, std::string& enable, std::string& height)
{
    for (const auto& item : graph.Nodes.Items)
    {
        if (item.second.Type != "If")
            continue;
        const auto* a = item.second.Inputs.Find("A");
        if (!Truthy(a) || a->array[0].kind != Json::String)
            continue;
        const auto* subtract = graph.Nodes.Find(a->array[0].string);
        if (!subtract || subtract->Type != "Subtract")
            continue;
        const auto* position = subtract->Inputs.Find("A");
        if (!Truthy(position) || position->array[0].kind != Json::String ||
            !graph.Nodes.Find(position->array[0].string) ||
            graph.Nodes.Find(position->array[0].string)->Type != "WorldPosition")
            continue;
        const auto scalar = XluCutScalarAt(graph, subtract->Inputs.Find("B"));
        if (scalar.empty())
            continue;
        std::set<std::string> others, seen;
        std::vector<std::string> pending{item.first};
        while (!pending.empty())
        {
            const auto id = pending.back();
            pending.pop_back();
            if (!seen.insert(id).second)
                continue;
            for (const auto& input : graph.Nodes.Find(id)->Inputs.Items)
            {
                if (!Truthy(&input.second) || input.second.array[0].kind != Json::String)
                    continue;
                const auto& target = input.second.array[0].string;
                const auto* node = graph.Nodes.Find(target);
                if (!node)
                    continue;
                if (node->Type == "If")
                    pending.push_back(target);
                else if (node->Type == "ScalarParameter" && target != scalar)
                    others.insert(target);
            }
        }
        if (others.size() != 1)
            continue;
        height = scalar;
        enable = *others.begin();
        return true;
    }
    return false;
}
inline void XluCut(CanonGraph& graph, const MaterialCanonGame& game)
{
    const auto& s = graph.Settings;
    const Json render = s.Has("render_settings") && s.At("render_settings").kind == Json::Object
                            ? s.At("render_settings")
                            : Json::MakeObject();
    if (!(render.Has("C4MaterialParam3") && render.At("C4MaterialParam3").kind == Json::String &&
          render.At("C4MaterialParam3").string == "C4XC_Enable"))
        return;
    if (s.Has("shading_model") && s.At("shading_model").kind == Json::String &&
        s.At("shading_model").string == "MSM_Unlit")
        return;
    std::map<std::string, Json> params;
    for (const auto& id : graph.Nodes.Keys())
    {
        const auto* node = graph.Nodes.Find(id);
        const auto code = node->Props.Has("Code") && node->Props.At("Code").kind == Json::String
                              ? node->Props.At("Code").string
                              : std::string();
        if (!((node->Type == "Custom" || node->Type == "KadanGameCustom") &&
              code.find("C4GlobalClippingPlane") != std::string::npos))
            continue;
        const Json value = node->Inputs.Find("Value") ? *node->Inputs.Find("Value") : Json();
        for (const auto& where : LinksTo(graph, id))
            SetWhere(graph, where, value);
        for (const char* key : {"Enable", "Height", "Position"})
        {
            const auto* link = node->Inputs.Find(key);
            if (Truthy(link))
                params[key] = *link;
        }
        graph.Nodes.Erase(id);
    }
    // The clip's scalars come from the recovered clip, else from the decompiler's stand-in for it; the game's own
    // clip reads both, so a graph with neither has no form here.
    if (!params.count("Enable") || !params.count("Height"))
    {
        std::string enable, height;
        if (!XluCutStandIn(graph, enable, height))
            throw MaterialCanonError("XluCut scalars are in neither a recovered clip nor its stand-in");
        // A parameter can have several nodes; the graph's first node of that parameter stands for it.
        const auto first = [&graph](const std::string& id)
        {
            const auto& name = graph.Nodes.Find(id)->Props.At("ParameterName");
            for (const auto& node : graph.Nodes.Items)
                if (node.second.Type == "ScalarParameter" && node.second.Props.Has("ParameterName") &&
                    node.second.Props.At("ParameterName").Dump() == name.Dump())
                    return node.first;
            return id;
        };
        if (!params.count("Enable"))
            params["Enable"] = MakeLink(first(enable), "");
        if (!params.count("Height"))
            params["Height"] = MakeLink(first(height), "");
    }
    (void)game;
    if (!params.count("Position"))
        params["Position"] = MakeLink(Add(graph, "WorldPosition"), "");
    const auto* ao = graph.Outputs.Find("AmbientOcclusion");
    const Json value = Truthy(ao) ? *ao : MakeLink(Add(graph, "Constant", Object({{"R", 1.0}})), "");
    auto inputs = Json::MakeArray();
    for (const char* name : {"Value", "Enable", "Height", "Position"})
        inputs.array.push_back(Object({{"InputName", Json(name)}}));
    const auto cut = Add(graph, "KadanGameCustom",
                         Object({{"Code", Json(XluCutCode())},
                                 {"OutputType", Json("CMOT_Float1")},
                                 {"Description", Json("XluCut")},
                                 {"Inputs", inputs}}),
                         {{"Value", value},
                          {"Enable", params["Enable"]},
                          {"Height", params["Height"]},
                          {"Position", params["Position"]}});
    graph.Outputs.Set("AmbientOcclusion", MakeLink(cut, ""));
    // The decompiler's stand-in for the clip: a 0-or-1 mask of If nodes on the same parameters, multiplied into the
    // opacity. The clip replaces it.
    std::set<std::string> names, dependent;
    for (const char* key : {"Enable", "Height"})
    {
        const auto scalar = XluCutScalarAt(graph, &params[key]);
        const auto* node = scalar.empty() ? nullptr : graph.Nodes.Find(scalar);
        if (node && node->Props.Has("ParameterName") && node->Props.At("ParameterName").kind == Json::String)
            names.insert(node->Props.At("ParameterName").string);
    }
    for (const auto& node : graph.Nodes.Items)
    {
        const auto& p = node.second.Props;
        if (p.Has("ParameterName") && p.At("ParameterName").kind == Json::String &&
            names.count(p.At("ParameterName").string))
            dependent.insert(node.first);
    }
    for (bool grew = true; grew;)
    {
        grew = false;
        for (const auto& node : graph.Nodes.Items)
        {
            if (dependent.count(node.first) || !(node.second.Type == "If" || node.second.Type == "Subtract"))
                continue;
            bool reads = false;
            for (const auto& input : node.second.Inputs.Items)
                reads |= Truthy(&input.second) && input.second.array[0].kind == Json::String &&
                         dependent.count(input.second.array[0].string) != 0;
            if (reads)
            {
                dependent.insert(node.first);
                grew = true;
            }
        }
    }
    std::vector<std::string> masks;
    for (const auto& id : dependent)
    {
        if (graph.Nodes.Find(id)->Type != "If")
            continue;
        bool outside = false;
        for (const auto& where : LinksTo(graph, id))
            outside |= where.Pin || !dependent.count(where.Id);
        if (outside)
            masks.push_back(id);
    }
    if (!masks.empty())
    {
        const auto one = Add(graph, "Constant", Object({{"R", 1.0}}));
        for (const auto& mask : masks)
            for (const auto& where : LinksTo(graph, mask))
                if (where.Pin || !dependent.count(where.Id))
                    SetWhere(graph, where, MakeLink(one, ""));
        RemoveUnused(graph);
    }
    RemoveUnused(graph);
}

// ----- customized UVs from per-instance custom data -----

struct UvPart
{
    enum Kind
    {
        CustomData,
        Fixed
    } Kind = Fixed;
    int Index = 0;
    double Value = 0;
};
inline double TextFloat(const std::string& text)
{
    char* end = nullptr;
    const double v = std::strtod(text.c_str(), &end);
    if (text.empty() || end != text.c_str() + text.size())
        throw MaterialCanonError("not a number: " + text);
    return v;
}
inline std::string Strip(const std::string& s)
{
    const auto a = s.find_first_not_of(" \t\r\n\f\v");
    if (a == std::string::npos)
        return std::string();
    const auto b = s.find_last_not_of(" \t\r\n\f\v");
    return s.substr(a, b - a + 1);
}
// {customized UV index: (data index, default) or constant per component} from an instanced vertex program
// (material_graph_canon.instance_custom_uvs).
inline std::map<int, std::vector<UvPart>> InstanceCustomUvs(const std::string& text,
                                                            const std::vector<std::pair<std::string, int>>& outputs)
{
    std::vector<std::string> lines;
    {
        std::size_t at = 0;
        while (at <= text.size())
        {
            const auto end = text.find_first_of("\r\n", at);
            lines.push_back(Strip(text.substr(at, end == std::string::npos ? std::string::npos : end - at)));
            if (end == std::string::npos)
                break;
            at = end + (text.compare(end, 2, "\r\n") == 0 ? 2 : 1);
            if (at == text.size())
                break;
        }
    }
    struct Value
    {
        bool Set = false;
        bool Data = false;
        int Index = 0;
        double Number = 0;
    };
    std::map<std::pair<std::string, char>, Value> regs;
    std::map<int, double> defaults;
    std::map<std::pair<int, char>, Value> attribs;
    bool pending = false;
    int pendingIndex = 0;
    static const std::regex fetch(R"(MIN\.S R\d+\.\w, R\d+, \{(\d+), 0, 0, 0\})"),
        mov(R"(^MOV\.F (R\d+|result\.attrib\[(\d+)\])(?:\.([xyzw]+))?, (.+);$)"), reg(R"(^(R\d+)(?:\.([xyzw]+))?$)"),
        cst(R"(^\{([-0-9.e]+), ([-0-9.e]+), ([-0-9.e]+), ([-0-9.e]+)\}(?:\.([xyzw]+))?$)"), rprefix(R"(^R\d+)");
    const std::string xyzw = "xyzw";
    for (std::size_t at = 0; at < lines.size(); ++at)
    {
        const auto& line = lines[at];
        std::smatch m;
        if (std::regex_search(line, m, fetch))
        {
            pending = true;
            pendingIndex = std::stoi(m[1].str());
            continue;
        }
        if (pending && line.compare(0, 6, "TXF.F ") == 0 && line.find("BUFFER") != std::string::npos)
        {
            std::istringstream words(line);
            std::string first, dest;
            words >> first >> dest;
            while (!dest.empty() && dest.back() == ',')
                dest.pop_back();
            const auto dot = dest.find('.');
            if (dot == std::string::npos || dest.find('.', dot + 1) != std::string::npos)
                throw MaterialCanonError("an instance data fetch without one component");
            const auto r = dest.substr(0, dot), comp = dest.substr(dot + 1);
            if (comp.size() == 1)
            {
                Value v;
                v.Set = true;
                v.Data = true;
                v.Index = pendingIndex;
                regs[{r, comp[0]}] = v;
            }
            for (std::size_t f = at + 1; f < at + 4 && f < lines.size(); ++f)
            {
                std::smatch d;
                const std::regex followed("^MOV\\.F " +
                                          std::regex_replace(r, std::regex(R"([.^$|()\[\]{}*+?\\])"), R"(\$&)") +
                                          "\\." + comp + ", \\{([-0-9.e]+)");
                if (std::regex_search(lines[f], d, followed))
                {
                    defaults[pendingIndex] = TextFloat(d[1].str());
                    break;
                }
            }
            pending = false;
            continue;
        }
        if (line.compare(0, 5, "ELSE;") == 0 || line.compare(0, 6, "ENDIF;") == 0 || line.compare(0, 3, "IF ") == 0)
            continue;
        if (!std::regex_match(line, m, mov))
        {
            std::string dest;
            const auto space = line.find(' ');
            if (space != std::string::npos)
            {
                dest = line.substr(space + 1);
                dest = dest.substr(0, dest.find(','));
            }
            if (std::regex_search(dest, rprefix))
            {
                const auto dot = dest.find('.');
                const auto r = dest.substr(0, dot);
                const auto mask = dot == std::string::npos ? std::string() : dest.substr(dot + 1);
                for (const char comp : mask.empty() ? xyzw : mask)
                    regs.erase({r, comp});
            }
            continue;
        }
        const std::string dest = m[1].str(), mask = m[3].matched ? m[3].str() : xyzw, source = Strip(m[4].str());
        const bool attrib = m[2].matched;
        const int attribIndex = attrib ? std::stoi(m[2].str()) : 0;
        std::smatch sm, cm;
        const bool isReg = std::regex_match(source, sm, reg), isConst = std::regex_match(source, cm, cst);
        std::vector<Value> values;
        for (const char comp : mask)
        {
            Value v;
            if (isConst)
            {
                const auto swz = cm[5].matched ? cm[5].str() : xyzw;
                const auto ci = xyzw.find(comp);
                if (swz.size() != 1 && ci >= swz.size())
                    throw MaterialCanonError("a swizzle shorter than its mask");
                const char picked = swz.size() == 1 ? swz[0] : swz[ci];
                v.Set = true;
                v.Number = TextFloat(cm[1 + xyzw.find(picked)].str());
            }
            else if (isReg)
            {
                const auto swz = sm[2].matched ? sm[2].str() : xyzw;
                const auto ci = xyzw.find(comp);
                if (swz.size() != 1 && ci >= swz.size())
                    throw MaterialCanonError("a swizzle shorter than its mask");
                const char picked = swz.size() == 1 ? swz[0] : swz[ci];
                const auto found = regs.find({sm[1].str(), picked});
                if (found != regs.end())
                    v = found->second;
            }
            values.push_back(v);
        }
        if (attrib)
        {
            for (std::size_t i = 0; i < mask.size(); ++i)
                attribs[{attribIndex, mask[i]}] = values[i];
        }
        else
        {
            const std::string prev = at ? lines[at - 1] : std::string();
            if (prev.compare(0, 5, "ELSE;") == 0 && source.compare(0, 1, "{") == 0)
                continue;
            for (std::size_t i = 0; i < mask.size(); ++i)
            {
                if (!values[i].Set)
                    regs.erase({dest, mask[i]});
                else
                    regs[{dest, mask[i]}] = values[i];
            }
        }
    }
    std::map<int, std::vector<UvPart>> out;
    static const std::regex texcoord(R"(^out_texcoord(\d+)$)");
    for (const auto& output : outputs)
    {
        std::smatch m;
        if (!std::regex_match(output.first, m, texcoord))
            continue;
        const int pair = std::stoi(m[1].str());
        for (int half = 0; half < 2; ++half)
        {
            const std::string comps = half ? "zw" : "xy";
            std::vector<Value> got;
            for (const char c : comps)
            {
                const auto f = attribs.find({output.second, c});
                got.push_back(f == attribs.end() ? Value() : f->second);
            }
            const bool all = got[0].Set && got[1].Set,
                       anyData = (got[0].Set && got[0].Data) || (got[1].Set && got[1].Data),
                       allFixed = got[0].Set && !got[0].Data && got[1].Set && !got[1].Data;
            if ((all && anyData) || allFixed)
            {
                std::vector<UvPart> parts;
                for (const auto& g : got)
                {
                    UvPart p;
                    if (g.Data)
                    {
                        p.Kind = UvPart::CustomData;
                        p.Index = g.Index;
                        const auto d = defaults.find(g.Index);
                        p.Value = d == defaults.end() ? 0.0 : d->second;
                    }
                    else
                        p.Value = g.Number;
                    parts.push_back(p);
                }
                out[2 * pair + half] = parts;
            }
        }
    }
    return out;
}
inline void CustomizedUvs(CanonGraph& graph, const MaterialCanonGame& game)
{
    for (const auto& pin : graph.Outputs.Items)
        if (pin.first.compare(0, 12, "CustomizedUV") == 0)
            return;
    if (!game.HasInstancedProgram || game.InstancedProgram.empty())
        return;
    const auto uvs = InstanceCustomUvs(game.InstancedProgram, game.InstancedOutputs);
    std::set<double> reads;
    for (const auto& node : graph.Nodes.Items)
        if (node.second.Type == "TextureCoordinate")
            reads.insert(PropNumber(node.second.Props, "CoordinateIndex", 0));
    bool anyData = false;
    for (const auto& item : uvs)
        for (const auto& p : item.second)
            anyData |= p.Kind == UvPart::CustomData;
    if (!anyData || reads.empty() || !uvs.count(int(*reads.rbegin())))
        return;
    const int count = int(*reads.rbegin()) + 1;
    for (int index = 0; index < count; ++index)
    {
        Json link;
        const auto found = uvs.find(index);
        if (found != uvs.end() && found->second[0].Kind == UvPart::Fixed && found->second[1].Kind == UvPart::Fixed)
            link = MakeLink(
                Add(graph, "Constant2Vector", Object({{"R", found->second[0].Value}, {"G", found->second[1].Value}})),
                "");
        else if (found != uvs.end())
        {
            std::vector<std::string> parts;
            for (const auto& c : found->second)
                parts.push_back(c.Kind == UvPart::CustomData
                                    ? Add(graph, "PerInstanceCustomData",
                                          Object({{"DataIndex", double(c.Index)}, {"ConstDefaultValue", c.Value}}))
                                    : Add(graph, "Constant", Object({{"R", c.Value}})));
            link = MakeLink(Add(graph, "AppendVector", Json::MakeObject(),
                                {{"A", MakeLink(parts[0], "")}, {"B", MakeLink(parts[1], "")}}),
                            "");
        }
        else
            link = MakeLink(Add(graph, "TextureCoordinate", Object({{"CoordinateIndex", double(index)}})), "");
        graph.Outputs.Set("CustomizedUV" + std::to_string(index), link);
    }
}

// ----- UE's own nodes where the decompiler wrote their expanded formula -----

// Replaces the node (in place) when the game's set is unknown, or when the replacement brings the predicted set closer
// to it. True when replaced.
inline bool KeepIfCloser(CanonGraph& graph, const MaterialCanonGame& game, const std::string& id,
                         const CanonNode& replacement)
{
    long before = 0;
    const bool known = Comparable(game) && SetScore(graph, game.Uniforms, before);
    auto& node = *graph.Nodes.Find(id);
    const CanonNode old = node;
    node.Type = replacement.Type;
    node.Inputs = replacement.Inputs;
    node.Props = replacement.Props;
    if (!known)
        return true;
    long after = 0;
    if (SetScore(graph, game.Uniforms, after) && after > before)
        return true;
    node.Type = old.Type;
    node.Inputs = old.Inputs;
    node.Props = old.Props;
    return false;
}
inline CanonNode NodeWith(const std::string& type, std::vector<std::pair<std::string, Json>> inputs,
                          Json props = Json::MakeObject())
{
    CanonNode n;
    n.Type = type;
    for (auto& i : inputs)
        n.Inputs.Set(i.first, i.second);
    n.Props = props;
    return n;
}

inline int RimLerp(CanonGraph& graph, const MaterialCanonGame& game)
{
    int count = 0;
    for (const auto& id : graph.Nodes.Keys())
    {
        const auto* node = graph.Nodes.Find(id);
        if (!node || node->Type != "Add")
            continue;
        for (const auto& xy : Pairs(*node))
        {
            const auto& x = xy.first;
            const auto& y = xy.second;
            if (x.Const || y.Const)
                continue;
            const auto* yn = NodeOf(graph, &y.Link);
            if (!yn || yn->Type != "Add")
                continue;
            bool matched = false;
            Json match;
            for (const auto& mp : Pairs(*yn))
            {
                const auto& m = mp.first;
                const auto& p = mp.second;
                const auto* mn = m.Const ? nullptr : NodeOf(graph, &m.Link);
                if (p.Const || !mn || mn->Type != "Multiply")
                    continue;
                for (const auto& np : Pairs(*mn))
                {
                    const auto& neg = np.first;
                    const auto& p2 = np.second;
                    const auto* nn = neg.Const ? nullptr : NodeOf(graph, &neg.Link);
                    if (!Same(p2, p) || !nn || nn->Type != "Multiply")
                        continue;
                    bool any = false;
                    for (const auto& xc : Pairs(*nn))
                        any |= Same(xc.first, x) && Constant(graph, xc.second, -1.0);
                    if (any)
                    {
                        matched = true;
                        match = p.Link;
                    }
                }
            }
            if (matched)
            {
                count += KeepIfCloser(
                    graph, game, id,
                    NodeWith("LinearInterpolate", {{"A", x.Link}, {"Alpha", match}}, Object({{"ConstB", 1.0}})));
                break;
            }
        }
    }
    if (count)
        RemoveUnused(graph);
    return count;
}
inline bool Scalar(const CanonGraph& graph, const Json* link, int depth = 0)
{
    const auto* n = NodeOf(graph, link);
    if (!n || depth > 32)
        return false;
    const auto chans = LinkChannels(*link);
    if (!chans.empty())
        return chans.size() == 1;
    if (InSet(n->Type, {"ScalarParameter", "Constant", "CollectionParameter", "Time"}))
        return true;
    if (InSet(n->Type, {"Add", "Subtract", "Multiply", "Divide"}))
    {
        for (const auto& o : Operands(*n))
            if (!o.Const && !Scalar(graph, &o.Link, depth + 1))
                return false;
        return true;
    }
    return false;
}
inline bool ScalarAppend(const CanonGraph& graph, const CanonNode& node)
{
    const auto* a = node.Inputs.Find("A");
    const auto* b = node.Inputs.Find("B");
    return Truthy(a) && Truthy(b) && Scalar(graph, a) && Scalar(graph, b);
}
inline int Panner(CanonGraph& graph, const MaterialCanonGame& game)
{
    int count = 0;
    for (const auto& id : graph.Nodes.Keys())
    {
        const auto* node = graph.Nodes.Find(id);
        if (!node || node->Type != "Add")
            continue;
        for (const auto& cs : Pairs(*node))
        {
            const auto& coord = cs.first;
            const auto& scroll = cs.second;
            const auto* sn = scroll.Const || coord.Const ? nullptr : NodeOf(graph, &scroll.Link);
            if (!sn || sn->Type != "Multiply" || !LinkChannels(scroll.Link).empty())
                continue;
            bool hit = false;
            Json time, speed;
            for (const auto& ts : Pairs(*sn))
            {
                const auto* tn = ts.first.Const ? nullptr : NodeOf(graph, &ts.first.Link);
                const auto* spn = ts.second.Const ? nullptr : NodeOf(graph, &ts.second.Link);
                if (tn && tn->Type == "Time" && tn->Props.object.empty() && spn && spn->Type == "AppendVector" &&
                    LinkChannels(ts.second.Link).empty() && ScalarAppend(graph, *spn))
                {
                    hit = true;
                    time = ts.first.Link;
                    speed = ts.second.Link;
                }
            }
            if (hit)
            {
                count +=
                    KeepIfCloser(graph, game, id,
                                 NodeWith("Panner", {{"Coordinate", coord.Link}, {"Time", time}, {"Speed", speed}}));
                break;
            }
        }
    }
    if (count)
        RemoveUnused(graph);
    return count;
}
} // Canon
}
} // P4MapKit::Material

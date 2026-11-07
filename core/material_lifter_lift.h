// Lifter: whole formulas lifted to graph links (part of material_lifter.h).
#pragma once
#include "material_lifter.h"
namespace P4MapKit
{
namespace Material
{
inline Json Lifter::lift(const std::vector<Value>& values)
{
    if (values.empty() || values.size() > 4)
        throw Refused("a material value has one to four components", "value width");
    const auto keyOf = [](const std::vector<Value>& row)
    {
        std::string key;
        for (const auto& v : row)
            key += ValueKey(v) + ";";
        return key;
    };
    const auto key = keyOf(values);
    auto known = memo.find(key);
    if (known != memo.end())
        return known->second;
    if (resolving)
        throw NeedLift{values};
    resolving = true;
    struct Reset
    {
        bool& flag;
        ~Reset()
        {
            flag = false;
        }
    } reset{resolving};
    std::vector<std::pair<std::vector<Value>, std::string>> pending{{values, key}};
    std::set<std::string> active{key};
    while (!pending.empty())
    {
        const auto current = pending.back();
        if (memo.count(current.second))
        {
            active.erase(current.second);
            pending.pop_back();
            continue;
        }
        try
        {
            auto out = liftInternal(current.first);
            memo.emplace(current.second, out);
            active.erase(current.second);
            pending.pop_back();
        }
        catch (const NeedLift& child)
        {
            const auto childKey = keyOf(child.values);
            if (!active.insert(childKey).second)
                throw Refused("material lifting requested its own unresolved value", "cyclic material lift");
            if (pending.size() >= 1000000)
                throw Refused("material lift dependency budget exceeded", "material lift budget");
            pending.push_back({child.values, childKey});
        }
    }
    return memo.at(key);
}
inline std::vector<Value> Lifter::pixelValues(const std::vector<Value>& values)
{
    std::vector<Value> out;
    for (const auto& value : values)
    {
        if (value.kind != Value::Expr)
        {
            out.push_back(value);
            continue;
        }
        std::vector<Value> pending{value};
        while (!pending.empty())
        {
            const auto current = pending.back();
            const auto* identity = current.expression.get();
            if (viewport_memo.count(identity))
            {
                pending.pop_back();
                continue;
            }
            bool waiting = false;
            for (const auto& arg : Args(current))
                if (arg.kind == Value::Expr && !viewport_memo.count(arg.expression.get()))
                {
                    pending.push_back(arg);
                    waiting = true;
                    break;
                }
            if (waiting)
                continue;
            std::vector<Value> args;
            bool changed = false;
            for (const auto& arg : Args(current))
            {
                auto next = arg.kind == Value::Expr ? viewport_memo.at(arg.expression.get()) : arg;
                changed |= !Same(next, arg);
                args.push_back(next);
            }
            auto rewritten = changed ? arena.mk(current.expression->op, args) : current;
            for (int c = 0; c < 2; ++c)
            {
                const auto expected = simp("ADD", {src("SvPosition", Value::null(), c),
                                                   simp("NEG", {src("View", Value("view_rect_min"), c)})});
                if (Same(rewritten, expected))
                {
                    rewritten = src("ViewportPixelPosition", Value::null(), c);
                    break;
                }
            }
            viewport_memo[identity] = rewritten;
            pending.pop_back();
        }
        out.push_back(viewport_memo.at(value.expression.get()));
    }
    return out;
}
inline Json Lifter::liftInternal(const std::vector<Value>& original)
{
    auto values = original;
    auto primitive = primitiveLift(values);
    if (primitive.kind != Json::Null)
        return primitive;
    auto particle = particleLift(values);
    if (particle.kind != Json::Null)
        return particle;
    if (values.size() >= 3)
    {
        bool all = true, prefix = true;
        for (const auto& v : values)
            all &= Same(v, values[0]);
        for (std::size_t i = 0; i + 1 < values.size(); ++i)
            prefix &= Same(values[i], values[0]);
        if (!all && prefix)
        {
            auto scalar = lift({values[0]}), out = scalar;
            for (std::size_t i = 1; i + 1 < values.size(); ++i)
                out = link(node("AppendVector", Object({{"A", out}, {"B", scalar}})));
            return link(node("AppendVector", Object({{"A", out}, {"B", lift({values.back()})}})));
        }
    }
    values = pixelValues(values);
    bool numeric = true, same = true;
    for (const auto& v : values)
    {
        numeric &= IsNumber(v);
        same &= Same(v, values[0]);
    }
    if (numeric)
    {
        bool infinite = true;
        for (const auto& v : values)
            infinite &= std::isinf(v.number()) && v.number() > 0;
        // material_positive_infinity.h lowers this literal or refuses the graph.
        if (infinite)
            return link(node("Constant", Json::MakeObject(), Object({{"PositiveInfinity", true}})));
    }
    if (numeric)
    {
        std::vector<Json> nums;
        for (const auto& v : values)
        {
            if (!std::isfinite(v.number()))
                throw Refused("nonfinite material constant", "nonfinite graph property");
            nums.emplace_back(v.number());
        }
        bool uniform = true;
        for (const auto& v : values)
            uniform &= v.number() == values[0].number() && std::signbit(v.number()) == std::signbit(values[0].number());
        if (uniform)
            return link(node("Constant", Json::MakeObject(), Object({{"R", nums[0]}})));
        if (values.size() == 2)
            return link(node("Constant2Vector", Json::MakeObject(), Object({{"R", nums[0]}, {"G", nums[1]}})));
        return link(node(values.size() == 3 ? "Constant3Vector" : "Constant4Vector", Json::MakeObject(),
                         Object({{"Constant", Json(nums)}})));
    }
    if (values.size() > 1 && same)
        return lift({values[0]});
    std::vector<FormulaPart> parts;
    bool whole = true, hasNormal = false;
    for (const auto& v : values)
    {
        parts.push_back(part(v));
        whole &= parts.back().valid;
        hasNormal |= parts.back().valid && parts.back().normal;
    }
    if (whole)
    {
        for (const auto& p : parts)
            whole &= Same(p.source, parts[0].source) && p.normal == parts[0].normal;
    }
    if (whole)
    {
        auto s = source(parts[0].source, parts[0].normal);
        std::string mask;
        bool all = values.size() == std::size_t(s.second);
        for (std::size_t k = 0; k < parts.size(); ++k)
        {
            if (parts[k].component < 0 || parts[k].component > 3)
                throw Refused("source component outside RGBA", "source component");
            mask += "RGBA"[parts[k].component];
            all &= parts[k].component == int(k);
        }
        const auto& props = nodes.At(s.first).At("props");
        if (props.Has("SamplerType") &&
            IsMember(props.At("SamplerType").Text(), {"SAMPLERTYPE_Alpha", "SAMPLERTYPE_Grayscale",
                                                      "SAMPLERTYPE_LinearGrayscale", "SAMPLERTYPE_DistanceFieldFont"}))
            for (const auto& p : parts)
                if (p.component != 0)
                    throw Refused("sampler type: shader reads a non-red channel from a red-only sampler",
                                  "settings disagree: sampler type");
        auto out = link(s.first, all ? "" : mask);
        if (IsExpr(parts[0].source, "SRC") && Args(parts[0].source)[0].text == "ViewportPixelPosition")
            out.array.push_back(Json(1));
        return out;
    }
    if (values.size() > 1 && hasNormal)
        return link(node("AppendVector", Object({{"A", lift(std::vector<Value>(values.begin(), values.end() - 1))},
                                                 {"B", lift({values.back()})}})));
    if (values.size() == 1)
    {
        const auto& x = values[0];
        if (IsExpr(x, "SEL"))
            return choice(Args(x)[0], {Args(x)[1]}, {Args(x)[2]});
        if (x.kind != Value::Expr)
            throw Refused("a material value is neither numeric nor an expression", "formula type");
        if (IsExpr(x, "COMP") && IsExpr(Args(x)[0], "ENGINE"))
        {
            const auto& engine = Args(Args(x)[0]);
            const auto label = engine.empty() || engine[0].kind != Value::Text ? "ENGINE" : engine[0].text;
            const auto detail = engine.size() > 1 && engine[1].kind == Value::Text ? engine[1].text : std::string();
            throw Refused(detail.empty() ? label : label + " (" + detail.substr(0, 400) + ")", label);
        }
        if (IsExpr(x, "COMP") || IsExpr(x, "SRC") || IsExpr(x, "SAMPLE") || IsExpr(x, "ENGINE"))
            throw Refused("a use of " + x.expression->op + " that has no node");
        std::vector<std::vector<Value>> columns;
        for (const auto& a : Args(x))
            columns.push_back({a});
        return operation(x.expression->op, columns);
    }
    bool normalize = true;
    for (const auto& v : values)
        normalize &= IsExpr(v, "MUL") && IsExpr(Args(v)[0], "RSQ") && Same(Args(v)[0], Args(values[0])[0]);
    if (normalize)
    {
        std::vector<Value> inner;
        Value dot(0);
        for (const auto& v : values)
        {
            inner.push_back(Args(v)[1]);
            dot = simp("ADD", {dot, simp("MUL", {inner.back(), inner.back()})});
        }
        if (Same(Args(Args(values[0])[0])[0], dot))
            return link(node("Normalize", Object({{"VectorInput", lift(inner)}})));
    }
    bool grouped = values[0].kind == Value::Expr;
    const auto op = grouped ? values[0].expression->op : "";
    for (const auto& v : values)
        grouped &= v.kind == Value::Expr && v.expression->op == op && Args(v).size() == Args(values[0]).size();
    if (grouped && op != "COMP" && op != "SRC" && op != "SAMPLE" && op != "SEL")
    {
        std::vector<std::vector<Value>> columns(Args(values[0]).size());
        for (const auto& v : values)
            for (std::size_t k = 0; k < columns.size(); ++k)
                columns[k].push_back(Args(v)[k]);
        return operation(op, columns);
    }
    if (grouped && op == "SEL")
    {
        bool one = true;
        for (const auto& v : values)
            one &= Same(Args(v)[0], Args(values[0])[0]);
        if (one)
        {
            std::vector<Value> a, b;
            for (const auto& v : values)
            {
                a.push_back(Args(v)[1]);
                b.push_back(Args(v)[2]);
            }
            return choice(Args(values[0])[0], a, b);
        }
    }
    return link(node("AppendVector", Object({{"A", lift(std::vector<Value>(values.begin(), values.end() - 1))},
                                             {"B", lift({values.back()})}})));
}
inline Json Lifter::transformInput(const std::vector<Value>& values)
{
    if (values.size() != 3)
        throw Refused("a basis transform must have three original components", "transform shape");
    auto a = lift({values[0]}), b = lift({values[1]}), c = lift({values[2]});
    auto two = link(node("AppendVector", Object({{"A", a}, {"B", b}})));
    return link(node("AppendVector", Object({{"A", two}, {"B", c}})));
}
inline Json Lifter::particleLift(const std::vector<Value>& values)
{
    Value common;
    std::vector<int> components;
    for (const auto& v : values)
    {
        if (!IsExpr(v, "COMP") || !IsExpr(Args(v)[0], "SRC"))
            return Json();
        const auto& a = Args(Args(v)[0]);
        if (a[0].kind != Value::Text || !IsMember(a[0].text, {"ParticleSubUVProperty", "ParticleDynamicParameter"}))
            return Json();
        if (!components.empty() && !Same(common, Args(v)[0]))
            return Json();
        common = Args(v)[0];
        const auto& c = Args(v)[1];
        if (!IsNumber(c) || std::floor(c.number()) != c.number() || c.number() < 0 || c.number() > 3)
            throw Refused("particle component outside its output", "particle component");
        components.push_back(int(c.number()));
    }
    const auto& a = Args(common);
    if (!IsNumber(a[1]) || a[1].number() < 0 || std::floor(a[1].number()) != a[1].number() || a[1].number() > INT32_MAX)
        throw Refused("unknown particle property output", "particle property");
    int detail = int(a[1].number());
    if (a[0].text == "ParticleSubUVProperty")
    {
        if (detail > 2)
            throw Refused("unknown particle SubUV property output", "particle SubUV property");
        int width = detail == 2 ? 1 : 2;
        std::string mask;
        bool all = components.size() == std::size_t(width);
        for (std::size_t i = 0; i < components.size(); ++i)
        {
            if (components[i] >= width)
                throw Refused("particle SubUV component outside its output", "particle SubUV property");
            all &= components[i] == int(i);
            mask += "RGBA"[components[i]];
        }
        return Array({node("ParticleSubUVProperties"), all ? "" : mask, detail});
    }
    const auto d = Default("particle_dynamic_defaults", std::to_string(detail));
    if (!d || d->kind != Json::Array || d->array.size() != 4)
        throw Refused("the dynamic parameter has no checked non-particle default", "particle dynamic defaults");
    auto id = node("DynamicParameter", Json::MakeObject(), Object({{"ParameterIndex", detail}, {"DefaultValue", *d}}));
    Json out = Array({id, "", components[0]});
    for (std::size_t i = 1; i < components.size(); ++i)
        out = link(node("AppendVector", Object({{"A", out}, {"B", Array({id, "", components[i]})}})));
    return out;
}
inline Json Lifter::offsetScene(const Value& x)
{
    const auto& a = Args(x);
    if (a[0].kind != Value::Text || !IsMember(a[0].text, {"Color", "Depth"}))
        return Json();
    std::vector<Value> unclamped;
    for (int c = 0; c < 2; ++c)
    {
        const auto& v = a[c + 1];
        if (!IsExpr(v, "ADD"))
            return Json();
        auto bias = src("View", Value("screen_scale_bias"), c == 0 ? 3 : 2);
        const auto& t = Args(v);
        if (!Same(t[0], bias) && !Same(t[1], bias))
            return Json();
        auto scaled = Same(t[0], bias) ? t[1] : t[0];
        if (!IsExpr(scaled, "MUL"))
            return Json();
        auto scale = src("View", Value("screen_scale_bias"), c);
        const auto& m = Args(scaled);
        if (!Same(m[0], scale) && !Same(m[1], scale))
            return Json();
        auto clamped = Same(m[0], scale) ? m[1] : m[0];
        if (!IsExpr(clamped, "MAX"))
            return Json();
        const auto& lo = Args(clamped);
        if (!NumberIs(lo[0], -.999000013) && !NumberIs(lo[1], -.999000013))
            return Json();
        auto high = NumberIs(lo[0], -.999000013) ? lo[1] : lo[0];
        if (!IsExpr(high, "MIN"))
            return Json();
        const auto& hi = Args(high);
        if (!NumberIs(hi[0], .999000013) && !NumberIs(hi[1], .999000013))
            return Json();
        unclamped.push_back(NumberIs(hi[0], .999000013) ? hi[1] : hi[0]);
    }
    auto position = node("ScreenPosition"),
         flips = node("Constant2Vector", Json::MakeObject(), Object({{"R", 2.0}, {"G", -2.0}})),
         center = node("Constant2Vector", Json::MakeObject(), Object({{"R", -1.0}, {"G", 1.0}}));
    auto mapped = node("Multiply", Object({{"A", link(position)}, {"B", link(flips)}})),
         ndc = node("Add", Object({{"A", link(mapped)}, {"B", link(center)}}));
    auto shift = node("Subtract", Object({{"A", lift(unclamped)}, {"B", link(ndc)}})),
         offset = node("Divide", Object({{"A", link(shift)}, {"B", link(flips)}}));
    return link(node(a[0].text == "Color" ? "SceneColor" : "SceneDepth", Object({{"Input", link(offset)}}),
                     Object({{"InputMode", "OffsetFraction"}})));
}
inline Json Lifter::primitiveLift(const std::vector<Value>& values)
{
    for (const auto& v : values)
        if (!IsExpr(v, "COMP"))
            return Json();
    std::vector<Json> rows;
    for (const auto& v : values)
    {
        const auto& source = Args(v)[0];
        if (!IsExpr(source, "SRC") || Args(source)[0].text != "PrimitiveMatrix" ||
            Args(source)[1].kind != Value::Text || !IsMember(Args(source)[1].text, {"LocalToWorld", "WorldToLocal"}))
            break;
        const auto& component = Args(v)[1];
        if (component.kind != Value::Int || component.number() < 0 || component.number() >= 16)
            throw Refused("Primitive matrix component is outside float4x4", "Primitive matrix component");
        int at = int(component.number()), row = at / 4, col = at % 4;
        bool local = Args(source)[1].text == "LocalToWorld";
        if (col == 3)
        {
            rows.push_back(link(node("Constant", Json::MakeObject(), Object({{"R", double(row == 3)}}))));
            continue;
        }
        std::vector<Json> basis;
        for (int k = 0; k < 3; ++k)
            basis.emplace_back(double(row < 3 && k == row));
        auto input = link(node("Constant3Vector", Json::MakeObject(), Object({{"Constant", Json(basis)}})));
        auto props =
            row == 3 ? Object({{"TransformSourceType", local ? "TRANSFORMPOSSOURCE_Local" : "TRANSFORMPOSSOURCE_World"},
                               {"TransformType", local ? "TRANSFORMPOSSOURCE_World" : "TRANSFORMPOSSOURCE_Local"}})
                     : Object({{"TransformSourceType", local ? "TRANSFORMSOURCE_Local" : "TRANSFORMSOURCE_World"},
                               {"TransformType", local ? "TRANSFORM_World" : "TRANSFORM_Local"}});
        auto output = node(row == 3 ? "TransformPosition" : "Transform", Object({{"Input", input}}), props);
        rows.push_back(link(output, std::string(1, "RGB"[col])));
    }
    if (rows.size() == values.size())
    {
        auto out = rows[0];
        for (std::size_t i = 1; i < rows.size(); ++i)
            out = link(node("AppendVector", Object({{"A", out}, {"B", rows[i]}})));
        return out;
    }
    const auto& common = Args(values[0])[0];
    if (!IsExpr(common, "WORLD_TO_LOCAL_POSITION"))
        return Json();
    for (const auto& v : values)
        if (!Same(Args(v)[0], common))
            return Json();
    auto output = node(
        "TransformPosition", Object({{"Input", transformInput(Args(common))}}),
        Object({{"TransformSourceType", "TRANSFORMPOSSOURCE_World"}, {"TransformType", "TRANSFORMPOSSOURCE_Local"}}));
    std::string mask;
    bool all = values.size() == 3;
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        const auto& c = Args(values[i])[1];
        if (!IsNumber(c) || c.number() < 0 || c.number() >= 3 || std::floor(c.number()) != c.number())
            throw Refused("position transform output component is not float3", "position transform output component");
        all &= c.number() == double(i);
        mask += "RGB"[int(c.number())];
    }
    return link(output, all ? "" : mask);
}
}
} // P4MapKit::Material

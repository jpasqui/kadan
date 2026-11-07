// Production scene/sun formula stages. Resource identities are supplied by a
// separately checked metadata adapter; these functions never infer a native
// resource layout from its numerical values or emit original shader code.
#pragma once
#include "material_rewrite.h"

namespace P4MapKit
{
namespace Material
{
using SceneRoles = std::map<std::string, Value>;
struct SceneReadFactory
{
    bool RolesProved = false;
    SceneRoles TextureRoles;
    std::string CopySampler;
};
struct SceneRewriteFacts
{
    std::map<std::string, std::size_t> replacements;
    bool inferred_color = false, requires_independent_sky_proof = false;
};
namespace SceneDetail
{
inline bool number(const Value& v)
{
    return v.kind == Value::Float || v.kind == Value::Int || v.kind == Value::Boolean;
}
inline bool equal(const Value& a, const Value& b)
{
    if (number(a) && number(b))
        return !has_nan(a) && !has_nan(b) && compare_numbers(a, b) == 0;
    return same(a, b);
}
inline Value role(const SceneRoles& roles, const std::string& name)
{
    auto at = roles.find(name);
    return at == roles.end() ? Value::null() : at->second;
}
inline std::vector<Value> apply(SymbolicArena& arena, const std::vector<Value>& values, const RewriteFunction& fn,
                                SceneRewriteFacts* facts, const char* stage)
{
    return rewrite(arena, values,
                   [&](const Value& x)
                   {
                       auto out = fn(x);
                       if (facts && !same(out, x))
                           ++facts->replacements[stage];
                       return out;
                   });
}
inline Value component(SymbolicArena& arena, const std::string& kind, const Value& u, const Value& v, const Value& c)
{
    return arena.mk("COMP", {arena.mk("SCENE", {kind, u, v}), c});
}
inline Value depth_of(SymbolicArena& arena, const Value& read)
{
    auto z = arena.mk("COMP", {read, Value(0)});
    Value t[4];
    for (int k = 0; k < 4; ++k)
        t[k] = source(arena, "View", "inv_device_z", k);
    return simplify(arena, "ADD",
                    {simplify(arena, "ADD", {simplify(arena, "MUL", {z, t[0]}), t[1]}),
                     arena.mk("RCP", {simplify(arena, "ADD",
                                               {simplify(arena, "MUL", {z, t[2]}), simplify(arena, "NEG", {t[3]})})})});
}
inline std::map<const Expression*, Value> pixel_table(SymbolicArena& arena, bool all_spellings)
{
    std::map<const Expression*, Value> table;
    auto w = source(arena, "SvPosition", Value::null(), 3);
    std::vector<Value> inverses = {simplify(arena, "DIV", {1.0, w})};
    if (all_spellings)
        inverses.push_back(arena.mk("RCP", {w}));
    for (int c = 0; c < 2; ++c)
    {
        auto pixel = simplify(arena, "ADD",
                              {source(arena, "SvPosition", Value::null(), c),
                               simplify(arena, "NEG", {source(arena, "View", "view_rect_min", c)})});
        std::vector<Value> relatives = {pixel};
        if (all_spellings)
            relatives.push_back(source(arena, "ViewportPixelPosition", Value::null(), c));
        for (const auto& relative : relatives)
        {
            auto ndc = simplify(
                arena, "MUL",
                {simplify(arena, "ADD",
                          {simplify(arena, "MUL", {relative, source(arena, "View", "view_size", c + 2)}), -.5}),
                 c == 0 ? 2.0 : -2.0});
            std::vector<Value> numerators = {simplify(arena, "DIV", {ndc, w})};
            if (all_spellings)
                for (const auto& inverse : inverses)
                {
                    numerators.push_back(simplify(arena, "MUL", {ndc, inverse}));
                    numerators.push_back(simplify(arena, "MUL", {inverse, ndc}));
                }
            for (const auto& numerator : numerators)
                for (const auto& denominator : inverses)
                {
                    auto clip = simplify(arena, "DIV", {numerator, denominator});
                    auto scaled = simplify(arena, "MUL", {clip, source(arena, "View", "screen_scale_bias", c)});
                    auto own =
                        simplify(arena, "ADD", {scaled, source(arena, "View", "screen_scale_bias", c == 0 ? 3 : 2)});
                    table[own.expression.get()] = source(arena, "ScreenUV", Value::null(), c);
                }
        }
    }
    return table;
}
}
inline std::vector<Value> PixelOwn(SymbolicArena& arena, const std::vector<Value>& values,
                                   SceneRewriteFacts* facts = nullptr)
{
    const auto table = SceneDetail::pixel_table(arena, true);
    return SceneDetail::apply(
        arena, values,
        [&](const Value& x)
        {
            auto hit = table.find(x.expression.get());
            return hit == table.end() ? x : hit->second;
        },
        facts, "pixel_own");
}
inline std::vector<Value> PixelDepth(SymbolicArena& arena, const std::vector<Value>& values,
                                     SceneRewriteFacts* facts = nullptr)
{
    auto reciprocal = arena.mk("RCP", {source(arena, "SvPosition", Value::null(), 3)});
    return SceneDetail::apply(
        arena, values, [&](const Value& x) { return same(x, reciprocal) ? source(arena, "PixelDepth") : x; }, facts,
        "pixel_depth");
}
// Exact legacy structural recognizer, exposed separately because its color
// identity is a heuristic. Production SceneReads below disables that heuristic.
inline std::vector<Value> SceneReadsOrdinary(SymbolicArena& arena, const std::vector<Value>& values, SceneRoles& roles,
                                             SceneRewriteFacts* facts = nullptr)
{
    auto table = SceneDetail::pixel_table(arena, false);
    auto out = SceneDetail::apply(
        arena, values,
        [&](const Value& x)
        {
            auto hit = table.find(x.expression.get());
            return hit == table.end() ? x : hit->second;
        },
        facts, "ordinary_pixel_own");
    std::map<const Expression*, Value> depths;
    for (const auto& read : walk_formulas(out))
        if (is(read, "SCENETEX"))
            depths[SceneDetail::depth_of(arena, read).expression.get()] = read;
    out = SceneDetail::apply(
        arena, out,
        [&](const Value& x)
        {
            auto hit = depths.find(x.expression.get());
            if (hit == depths.end())
                return x;
            const auto& a = hit->second.expression->args;
            roles["depth"] = a.at(0);
            return SceneDetail::component(arena, "Depth", a.at(1), a.at(2), Value(0));
        },
        facts, "linear_depth");
    auto pixel = simplify(arena, "DIV", {1.0, source(arena, "SvPosition", Value::null(), 3)});
    out = SceneDetail::apply(
        arena, out, [&](const Value& x) { return same(x, pixel) ? source(arena, "PixelDepth") : x; }, facts,
        "ordinary_pixel_depth");
    return SceneDetail::apply(
        arena, out,
        [&](const Value& x)
        {
            if (!is(x, "COMP") || x.expression->args.size() != 2 || !is(x.expression->args[0], "SCENETEX") ||
                !SceneDetail::number(x.expression->args[1]) || x.expression->args[1].number() >= 3)
                return x;
            const auto& a = x.expression->args[0].expression->args;
            auto slot = a.at(0);
            if (SceneDetail::equal(SceneDetail::role(roles, "depth"), slot))
                return x;
            auto color = roles.emplace("color", slot);
            if (!SceneDetail::equal(color.first->second, slot))
                return x;
            if (facts)
                facts->inferred_color = true;
            return SceneDetail::component(arena, "Color", a.at(1), a.at(2), x.expression->args[1]);
        },
        facts, "inferred_color");
}
inline std::vector<Value> SceneCopyValues(SymbolicArena& arena, const std::vector<Value>& values,
                                          const SceneReadFactory& factory, SceneRoles& roles,
                                          SceneRewriteFacts* facts = nullptr)
{
    auto slot = SceneDetail::role(factory.TextureRoles, "scene_color_copy");
    if (slot.kind == Value::Null)
        return values;
    if (!factory.RolesProved)
        throw Refused("scene resource identities have no checked native metadata",
                      "unverified scene resource metadata");
    return SceneDetail::apply(
        arena, values,
        [&](const Value& x)
        {
            if (!is(x, "POW") || x.expression->args.size() != 2 ||
                !SceneDetail::equal(x.expression->args[1], Value(4.0)) || !is(x.expression->args[0], "COMP"))
                return x;
            const auto& a = x.expression->args[0].expression->args;
            if (a.size() != 2 || !SceneDetail::number(a[1]) || a[1].number() > 2 || !is(a[0], "SCENETEX"))
                return x;
            const auto& read = a[0].expression->args;
            if (!SceneDetail::equal(read.at(0), slot))
                return x;
            roles["color"] = slot;
            roles["verified_scene_color_copy"] = slot;
            const auto kind = factory.CopySampler == "view_clamp" ? "ColorViewClamp"
                              : factory.CopySampler == "point"    ? "ColorPointClamp"
                                                                  : "Color";
            return simplify(arena, "MUL", {SceneDetail::component(arena, kind, read.at(1), read.at(2), a[1]), .1});
        },
        facts, "verified_color_copy");
}
inline std::vector<Value> DeviceDepthValues(SymbolicArena& arena, const std::vector<Value>& values,
                                            const SceneReadFactory& factory, SceneRewriteFacts* facts = nullptr)
{
    auto slot = SceneDetail::role(factory.TextureRoles, "scene_depth");
    if (slot.kind == Value::Null)
        return values;
    if (!factory.RolesProved)
        throw Refused("scene resource identities have no checked native metadata",
                      "unverified scene resource metadata");
    return SceneDetail::apply(
        arena, values,
        [&](const Value& x)
        {
            if (!is(x, "COMP") || x.expression->args.size() != 2 ||
                !SceneDetail::equal(x.expression->args[1], Value(0)) || !is(x.expression->args[0], "SCENETEX"))
                return x;
            const auto& read = x.expression->args[0].expression->args;
            if (!SceneDetail::equal(read.at(0), slot))
                return x;
            return SceneDetail::component(arena, "DeviceDepth", read.at(1), read.at(2), Value(0));
        },
        facts, "device_depth");
}
inline std::vector<Value> SceneReads(SymbolicArena& arena, const std::vector<Value>& values,
                                     const SceneReadFactory& factory, SceneRoles& roles,
                                     SceneRewriteFacts* facts = nullptr)
{
    auto out = SceneCopyValues(arena, values, factory, roles, facts);
    roles["color"] = -1;
    out = PixelDepth(arena, SceneReadsOrdinary(arena, PixelOwn(arena, out, facts), roles, facts), facts);
    if (SceneDetail::equal(SceneDetail::role(roles, "color"), Value(-1)))
        roles.erase("color");
    auto depth = roles.find("depth");
    if (depth != roles.end() &&
        (depth->second.kind == Value::Null ||
         SceneDetail::role(factory.TextureRoles, "scene_depth").kind == Value::Null ||
         !SceneDetail::equal(depth->second, SceneDetail::role(factory.TextureRoles, "scene_depth"))))
        throw Refused("scene depth expression disagrees with its native metadata resource",
                      "unverified scene depth resource");
    if (depth != roles.end() && !factory.RolesProved)
        throw Refused("scene resource identities have no checked native metadata",
                      "unverified scene resource metadata");
    auto color = roles.find("verified_scene_color_copy");
    if (color != roles.end())
        roles["color"] = color->second;
    return DeviceDepthValues(arena, out, factory, facts);
}
inline std::vector<Value> SunDisk(SymbolicArena& arena, const std::vector<Value>& values, SceneRoles& roles,
                                  SceneRewriteFacts* facts = nullptr)
{
    const auto mask = source(arena, "View", "reflection_capture_mask");
    bool found = false;
    Value found_slot;
    const std::set<std::string> fields = {"atmosphere_light_direction",
                                          "atmosphere_light_disc_luminance",
                                          "atmosphere_light_disc_cos_half_apex",
                                          "sky_world_camera_origin",
                                          "sky_planet_center",
                                          "sky_bottom_radius_km",
                                          "sky_top_radius_km"};
    auto out = SceneDetail::apply(
        arena, values,
        [&](const Value& x)
        {
            const auto& a = x.expression->args;
            if (x.expression->op != "SEL" || a.size() != 3 || !SceneDetail::number(a[2]) ||
                !SceneDetail::equal(a[2], Value(0.0)))
                return x;
            auto test = a[0];
            if (!is(test, "T.NE") || test.expression->args.size() != 1)
                return x;
            test = test.expression->args[0];
            if (is(test, "TRUNC.U") && test.expression->args.size() == 1)
                test = test.expression->args[0];
            if (!is(test, "F.SEQ") || test.expression->args.size() != 2 || !same(test.expression->args[0], mask) ||
                !SceneDetail::number(test.expression->args[1]) ||
                !SceneDetail::equal(test.expression->args[1], Value(0.0)))
                return x;
            std::map<std::string, Value> luminance, tables;
            bool other = false;
            std::set<const Expression*> seen;
            std::vector<Value> stack = {a[1]};
            while (!stack.empty())
            {
                auto v = stack.back();
                stack.pop_back();
                if (!symbolic(v) || !seen.insert(v.expression.get()).second)
                    continue;
                const auto& args = v.expression->args;
                const auto& op = v.expression->op;
                if (op == "COMP" && args.size() == 2 && is(args[0], "SRC"))
                {
                    const auto& src = args[0].expression->args;
                    if (src.size() != 2)
                    {
                        other = true;
                        continue;
                    }
                    auto kind = src[0].text, detail = src[1].kind == Value::Text ? src[1].text : "";
                    if (kind == "View" && detail == "atmosphere_light_disc_luminance")
                        luminance[value_key(args[1])] = args[1];
                    else if (kind != "CameraVector" && (kind != "View" || !fields.count(detail)))
                        other = true;
                    continue;
                }
                if (op == "SRC" || op == "SAMPLE" || op == "ENGINE" || op == "SCENE")
                {
                    other = true;
                    continue;
                }
                if (op == "SCENETEX")
                    tables[value_key(args.at(0))] = args.at(0);
                for (auto item = args.rbegin(); item != args.rend(); ++item)
                    stack.push_back(*item);
            }
            if (other || luminance.size() != 1 || tables.size() != 1)
                return x;
            auto slot = tables.begin()->second;
            if (found && !SceneDetail::equal(found_slot, slot))
                return x;
            found = true;
            found_slot = slot;
            if (facts)
                facts->requires_independent_sky_proof = true;
            return source(arena, "SkyAtmosphereLightDiskLuminance", Value(0), int(luminance.begin()->second.number()));
        },
        facts, "sun_disk");
    if (found)
        roles["transmittance"] = found_slot;
    return out;
}
}
}

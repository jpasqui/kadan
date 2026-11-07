// Source-checked postprocess parameter, viewport and input-zero sample contract.
// The metadata reader supplies fields and resource identities; this stage never
// guesses constant-buffer or texture slots.
#pragma once
#include "material_bind.h"
#include "material_graph.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialPostBindingOptions
{
    bool MappedRawContract = true, SupplementalInputs = false, ViewLayoutProved = false;
    std::map<int, std::string> FixedTextures;
    std::function<Value(const Value&)> SupplementalBoundRewrite;
};
inline Value PostBoundValue(SymbolicArena& arena, const Value& value, bool mappedRaw, bool supplemental)
{
    for (int c = 0; c < 2; ++c)
    {
        const auto field = [&](const std::string& name)
        {
            return source(arena, "PostField", name, c);
        };
        const auto pixel =
            simplify(arena, "ADD",
                     {source(arena, "SvPosition", Value::null(), c),
                      simplify(arena, "NEG", {arena.mk("I2F.U", {arena.mk("TOINT", {field("OutputMin")})})})});
        const auto viewport = simplify(arena, "MUL", {pixel, field("OutputInverse")});
        if (same(value, viewport))
            return source(arena, "PostViewportUV", Value::null(), c);
        if (mappedRaw)
        {
            const auto direct = simplify(
                arena, "ADD",
                {simplify(arena, "MUL", {source(arena, "PostViewportUV", Value::null(), c), field("InputScale")}),
                 field("InputBias")});
            if (same(value, direct))
                return source(arena, "PostDefaultBufferUV", Value::null(), c);
        }
        if (supplemental)
        {
            if (same(value, pixel))
                return source(arena, "ViewportPixelPosition", Value::null(), c);
            if (same(value,
                     simplify(arena, "MUL",
                              {source(arena, "ViewportPixelPosition", Value::null(), c), field("OutputInverse")})))
                return source(arena, "PostViewportUV", Value::null(), c);
        }
    }
    return value;
}
template <class T>
inline void InstallPostProcessBinding(SymbolicArena& arena, BindingContext& context, const T& bindings,
                                      const MaterialPostBindingOptions& options = {})
{
    if (bindings.SceneSlot < 0 || bindings.Samplers.empty())
        throw BindingRefused("postprocess input zero has no checked resource bindings", "postprocess resources");
    if (!context.Varyings.empty())
        throw BindingRefused("postprocess custom vertex interpolators are not recovered", "postprocess interpolators");
    const auto previousLeaf = context.LeafOverride, previousBound = context.BoundOverride;
    const auto previousTexture = context.TextureOverride;
    auto base = std::make_shared<BindingContext>(context);
    auto resolver = std::make_shared<Binder>(arena, *base);
    context.LeafOverride = [&arena, bindings, options, previousLeaf](const Value& value, Value& out)
    {
        if (is(value, "IN") && value.expression->args.size() == 3)
        {
            const auto& args = value.expression->args;
            if (args[0].kind == Value::Text && args[0].text == "buffer")
            {
                const int buffer = int(args[1].number()), offset = int(args[2].number());
                const auto word = bindings.Words.find({buffer, offset});
                if (word != bindings.Words.end())
                {
                    out = source(arena, "PostField", word->second.first, word->second.second);
                    return true;
                }
            }
        }
        return previousLeaf && previousLeaf(value, out);
    };
    // View-size identities and supplemental row guards depend on the checked
    // buffer name. Retain a stable copy rather than capturing the caller's map.
    const auto buffers = context.Buffers;
    const auto leaf = context.LeafOverride;
    context.LeafOverride = [&arena, buffers, options, leaf](const Value& value, Value& out)
    {
        if (is(value, "IN") && value.expression->args.size() == 3)
        {
            const auto& args = value.expression->args;
            if (args[0].kind == Value::Text && args[0].text == "buffer")
            {
                const int buffer = int(args[1].number()), offset = int(args[2].number());
                const auto named = buffers.find(buffer);
                if (named != buffers.end() && named->second == "View")
                {
                    if (offset >= 2096 && offset < 2112 && offset % 4 == 0)
                    {
                        const auto size = source(arena, "PostViewSize", Value::null(), ((offset - 2096) / 4) % 2);
                        out = offset < 2104 ? size : simplify(arena, "DIV", {Value(1.), size});
                        return true;
                    }
                    if (options.SupplementalInputs && offset >= 1120 && offset < 1132 && !options.ViewLayoutProved)
                        throw BindingRefused("postprocess translation lacks checked View layout",
                                             "postprocess View layout");
                    if (options.SupplementalInputs && offset >= 704 && offset < 768)
                    {
                        if (!options.ViewLayoutProved)
                            throw BindingRefused("postprocess world matrix lacks checked View layout",
                                                 "postprocess View layout");
                        if (offset % 4)
                            throw BindingRefused("postprocess View matrix is not word aligned",
                                                 "buffer word alignment");
                        const int component = (offset - 704) / 4;
                        out = source(arena, "SupplementalViewRow",
                                     Value::tuple_value({Value("SVPositionToTranslatedWorld"), Value(component / 4)}),
                                     component % 4);
                        return true;
                    }
                }
            }
        }
        return leaf && leaf(value, out);
    };
    context.BoundOverride = [&arena, options, previousBound](const Value& value, Value& out)
    {
        auto bound = PostBoundValue(arena, value, options.MappedRawContract, options.SupplementalInputs);
        if (options.SupplementalInputs && options.SupplementalBoundRewrite)
            bound = options.SupplementalBoundRewrite(bound);
        if (!same(bound, value))
        {
            out = bound;
            return true;
        }
        return previousBound && previousBound(value, out);
    };
    context.TextureOverride = [&arena, bindings, options, base, resolver,
                               previousTexture](const Value& value, const Vec& coordinates, Value& out)
    {
        const auto& args = value.expression->args;
        const int slot = args[2].kind == Value::Null ? -1 : (int(args[2].number()) - 328) / 8;
        if (slot != bindings.SceneSlot)
        {
            const auto fixed = options.FixedTextures.find(slot);
            if (fixed == options.FixedTextures.end())
                return previousTexture && previousTexture(value, coordinates, out);
            if (args[0].text != "TEX" || args[1].text != "2D")
                throw BindingRefused("fixed postprocess texture uses an unsupported sampling operation",
                                     "postprocess fixed texture");
            const auto entry = base->TextureEntries.find(slot);
            if (entry == base->TextureEntries.end() || entry->second >= base->Uniforms.Textures.size())
                throw BindingRefused("fixed postprocess texture has no cooked uniform entry",
                                     "postprocess fixed texture");
            const auto sampler = resolver->sampler_source("?", args[3], &base->Uniforms.Textures[entry->second]);
            if (sampler == "?" || sampler.empty())
                throw BindingRefused("fixed postprocess texture sampler is not bound", "postprocess sampler");
            out = arena.mk("POSTFIXED", {fixed->second, sampler, coordinates[0], coordinates[1]});
            return true;
        }
        const int sampler = args[3].kind == Value::Null ? -1 : (int(args[3].number()) - 1352) / 8;
        const auto binding = bindings.Samplers.find(sampler);
        if (args[0].text != "TEX" || args[1].text != "2D" || binding == bindings.Samplers.end() ||
            (options.MappedRawContract && binding->second))
            throw BindingRefused("postprocess input-zero sampling operation/sampler is not bound",
                                 "postprocess sampler");
        if (options.MappedRawContract)
        {
            bool raw = true, mapped = true;
            for (int c = 0; c < 2; ++c)
            {
                raw &= same(coordinates[c], source(arena, "PostViewportUV", Value::null(), c));
                mapped &= same(coordinates[c], source(arena, "PostDefaultBufferUV", Value::null(), c));
            }
            if (raw)
            {
                out = arena.mk("POSTRAWSAMPLE", {});
                return true;
            }
            if (mapped)
            {
                out = arena.mk("POSTMAPPED", {source(arena, "PostViewportUV", Value::null(), 0),
                                              source(arena, "PostViewportUV", Value::null(), 1)});
                return true;
            }
            std::vector<Value> unscaled;
            for (int c = 0; c < 2; ++c)
            {
                const auto field = [&](const std::string& name)
                {
                    return source(arena, "PostField", name, c);
                };
                const auto& v = coordinates[c];
                if (!is(v, "ADD") || v.expression->args.size() != 2 ||
                    !same(v.expression->args[1], field("InputBias")) || !is(v.expression->args[0], "MUL"))
                    break;
                const auto& product = v.expression->args[0].expression->args;
                if (product.size() != 2)
                    break;
                if (same(product[0], field("InputScale")))
                    unscaled.push_back(product[1]);
                else if (same(product[1], field("InputScale")))
                    unscaled.push_back(product[0]);
                else
                    break;
            }
            if (unscaled.size() == 2)
            {
                out = arena.mk("POSTMAPPED", unscaled);
                return true;
            }
        }
        std::vector<Value> viewport;
        for (int c = 0; c < 2; ++c)
        {
            const auto field = [&](const std::string& name)
            {
                return source(arena, "PostField", name, c);
            };
            const auto& coord = coordinates[c];
            if (!is(coord, "MAX") || coord.expression->args.size() != 2 ||
                !same(coord.expression->args[0], field("InputMin")) || !is(coord.expression->args[1], "MIN"))
                throw BindingRefused("input-zero coordinates are not the checked viewport/clamp contract",
                                     "postprocess coordinates");
            const auto& low = coord.expression->args[1].expression->args;
            if (low.size() != 2 || !same(low[0], field("InputMax")) || !is(low[1], "ADD"))
                throw BindingRefused("input-zero coordinates are not the checked viewport/clamp contract",
                                     "postprocess coordinates");
            const auto& transformed = low[1].expression->args;
            if (transformed.size() != 2 || !same(transformed[1], field("InputBias")) || !is(transformed[0], "MUL"))
                throw BindingRefused("input-zero coordinates lack the checked viewport transform",
                                     "postprocess coordinates");
            const auto& product = transformed[0].expression->args;
            if (product.size() != 2)
                throw BindingRefused("input-zero coordinates have invalid viewport scale", "postprocess coordinates");
            if (same(product[0], field("InputScale")))
                viewport.push_back(product[1]);
            else if (same(product[1], field("InputScale")))
                viewport.push_back(product[0]);
            else
                throw BindingRefused("input-zero coordinates lack the checked viewport scale",
                                     "postprocess coordinates");
        }
        out = arena.mk("POSTSCENE", {Value(binding->second), viewport[0], viewport[1]});
        return true;
    };
}
}
}

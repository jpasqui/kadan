// Authored original reflection resources keep direction, layer and explicit LOD.
// Constructor/SRT identity is distinct from renderer availability or admission.
#pragma once
#include "material_source_particle_lit_bind.h"
#include "material_engine_hashes.h"
namespace P4MapKit
{
namespace Material
{
inline void InstallOriginalParticleLitReflection(SymbolicArena& arena, BindingContext& context,
                                                 const FMaterialShaderReference& reference,
                                                 const CustomMaterial::ShaderBindings& header, const Json& settings)
{
    bool known = false;
    for (const auto hash : header.layout_hashes)
        known = known || hash == 0x0d480502 || hash == 0x05ec0dee;
    if (!known)
        return;
    ReadMaterialNativeSceneResourcePolicy(reference, header, settings.At("domain").Text(),
                                          settings.At("blend_mode").Text(), true);
    std::map<std::pair<int, int>, std::pair<std::string, int>> words;
    using Key = std::tuple<int, int, std::string>;
    std::map<Key, std::string> resources;
    const auto textures = CustomMaterial::ReadResourceTable(header.textures),
               samplers = CustomMaterial::ReadResourceTable(header.samplers);
    for (std::size_t buffer = 0; buffer < header.layout_hashes.size(); ++buffer)
    {
        const auto layout = header.layout_hashes[buffer];
        if (layout == EngineHash::ViewLayout)
            for (const auto& field :
                 std::vector<std::tuple<const char*, int, int>>{{"IndirectLightingColorScale", 2960, 3},
                                                                {"SkyLightColor", 3488, 4},
                                                                {"ReflectionCubemapMaxMip", 3624, 1}})
                for (int c = 0; c < std::get<2>(field); ++c)
                    words[{int(buffer), std::get<1>(field) + 4 * c}] = {std::get<0>(field), c};
        else if (layout == 0x0d480502 || layout == 0x05ec0dee)
        {
            words[{int(buffer), 1280}] = {"SkyLightMaxMip", 0};
            const auto ts = textures.find(unsigned(buffer)), ss = samplers.find(unsigned(buffer));
            if (ts == textures.end() || ss == samplers.end())
                continue;
            std::map<unsigned, unsigned> textureSlots, samplerSlots;
            for (const auto& row : ts->second)
                textureSlots[row.first] = row.second;
            for (const auto& row : ss->second)
                samplerSlots[row.first] = row.second;
            for (const auto& role : std::vector<std::tuple<unsigned, unsigned, const char*, const char*>>{
                     {16, 17, "CUBE", "sky"}, {18, 19, "CUBE", "sky_blend"}, {20, 21, "ARRAYCUBE", "reflection"}})
            {
                const auto t = textureSlots.find(std::get<0>(role)), s = samplerSlots.find(std::get<1>(role));
                if (t == textureSlots.end() || s == samplerSlots.end())
                    continue;
                const Key key{328 + 8 * int(t->second), 1352 + 8 * int(s->second), std::get<2>(role)};
                const auto old = resources.find(key);
                if (old != resources.end() && old->second != std::get<3>(role))
                    throw Refused("conflicting compiled reflection resource bindings",
                                  "material particle reflection resource");
                resources[key] = std::get<3>(role);
            }
        }
    }
    const auto priorLeaf = context.LeafOverride;
    context.LeafOverride = [&arena, words, priorLeaf](const Value& value, Value& out)
    {
        if (is(value, "IN") && Args(value).size() == 3 && Args(value)[0].kind == Value::Text &&
            Args(value)[0].text == "buffer" && IsNumber(Args(value)[1]) && IsNumber(Args(value)[2]))
        {
            const auto b = Args(value)[1].number(), w = Args(value)[2].number();
            if (b >= 0 && b <= 31 && std::floor(b) == b && w >= 0 && w <= 65536 && std::floor(w) == w)
            {
                const auto field = words.find({int(b), int(w)});
                if (field != words.end())
                {
                    out = source(arena, "ReflectionField", field->second.first, field->second.second);
                    return true;
                }
            }
        }
        return priorLeaf && priorLeaf(value, out);
    };
    const auto children = context;
    const auto previous = context.ComponentOverride;
    context.ComponentOverride = [&arena, resources, children, previous](const Value& value, Value& out)
    {
        if (is(value, "TEX") && Args(value).size() >= 4)
        {
            const auto& a = Args(value);
            if (a[1].kind == Value::Text && IsMember(a[1].text, {"CUBE", "ARRAYCUBE"}) && IsNumber(a[2]) &&
                IsNumber(a[3]))
            {
                const auto t = a[2].number(), s = a[3].number();
                if (t >= 0 && t <= 4096 && s >= 0 && s <= 4096 && std::floor(t) == t && std::floor(s) == s)
                {
                    const auto found = resources.find({int(t), int(s), a[1].text});
                    if (found != resources.end())
                    {
                        if (a[0].kind != Value::Text || a[0].text != "TXL" ||
                            a.size() != (a[1].text == "ARRAYCUBE" ? 9u : 8u))
                            throw Refused("authored reflection sample has an unchecked instruction or operand shape",
                                          "material particle reflection sample");
                        Binder binder(arena, children);
                        std::vector<Value> args = {found->second};
                        for (auto i = std::size_t(4); i < a.size(); ++i)
                            args.push_back(BindMaterialComponent(binder, a[i]));
                        out = arena.mk("REFLECTION_SAMPLE", args);
                        return true;
                    }
                }
            }
        }
        return previous && previous(value, out);
    };
}
}
} // P4MapKit::Material

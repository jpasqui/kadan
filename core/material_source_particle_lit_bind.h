// Source-qualified engine View rows used by lit particle material expressions.
// A field is exposed only when the actual serialized ref and layout agree.
#pragma once
#include "material_source_particle_plan.h"
#include "material_engine_hashes.h"
#include "material_source_unlit_scene.h"
#include <tuple>
namespace P4MapKit
{
namespace Material
{
inline void InstallOriginalParticleLitViewRows(SymbolicArena& arena, BindingContext& binding,
                                               const FMaterialShaderReference& reference,
                                               const CustomMaterial::ShaderBindings& header)
{
    const auto previous = binding.LeafOverride;
    const auto buffers = binding.Buffers;
    binding.LeafOverride = [&arena, previous, buffers, reference, header](const Value& value, Value& out)
    {
        if (is(value, "IN") && Args(value).size() == 3 && Args(value)[0].kind == Value::Text &&
            Args(value)[0].text == "buffer" && IsNumber(Args(value)[1]) && IsNumber(Args(value)[2]))
        {
            const auto index = Args(value)[1].number(), word = Args(value)[2].number();
            if (index >= 0 && index <= 65535 && std::floor(index) == index && word >= 0 && word <= 1048576 &&
                std::floor(word) == word)
            {
                const auto named = buffers.find(int(index));
                if (named != buffers.end() && named->second == "View")
                {
                    for (const auto& field :
                         std::vector<std::tuple<const char*, int, int>>{{"TranslatedWorldToView", 192, 16},
                                                                        {"TranslatedWorldToCameraView", 320, 16},
                                                                        {"ViewToClip", 448, 16},
                                                                        {"ClipToTranslatedWorld", 640, 16},
                                                                        {"BufferSizeAndInvSize", 2128, 4}})
                    {
                        const auto start = std::get<1>(field), width = std::get<2>(field);
                        if (word < start || word >= start + 4 * width)
                            continue;
                        const auto hash = reference.UniformBufferHashes.find(std::uint16_t(index));
                        if (reference.Frequency != 3 || std::size_t(index) >= header.layout_hashes.size() ||
                            header.layout_hashes[std::size_t(index)] != EngineHash::ViewLayout ||
                            hash == reference.UniformBufferHashes.end() ||
                            hash->second != EngineHash::ViewUniformBuffer)
                            throw BindingRefused("water matrix read does not have the checked compiled View layout",
                                                 "View matrix bindings");
                        if (int(word) % 4)
                            throw BindingRefused("water matrix word is not aligned", "buffer word alignment");
                        const auto c = (int(word) - start) / 4;
                        out = source(arena, "WaterViewRow",
                                     Value::tuple_value({Value(std::get<0>(field)), Value(c / 4)}), c % 4);
                        return true;
                    }
                }
            }
        }
        return previous && previous(value, out);
    };
}
inline void InstallOriginalParticleLitEyeAdaptation(SymbolicArena& arena, BindingContext& binding,
                                                    const FMaterialShaderReference& reference,
                                                    const CustomMaterial::ShaderBindings& header, const Json& settings)
{
    InstallOriginalMaterialEyeAdaptation(arena, binding, reference, header, settings);
}
inline void InstallOriginalParticleLitSceneSamples(SymbolicArena& arena, BindingContext& binding,
                                                   const FMaterialShaderReference& reference,
                                                   const CustomMaterial::ShaderBindings& header, const Json& settings)
{
    bool known = false;
    for (const auto hash : header.layout_hashes)
        known = known || MaterialBuiltinSceneNativeRegistry().count(hash);
    if (!known)
        return;
    const auto policy = ReadMaterialNativeSceneResourcePolicy(reference, header, settings.At("domain").Text(),
                                                              settings.At("blend_mode").Text(), true)
                            .ResourcePolicy;
    const auto previous = binding.TextureOverride;
    binding.TextureOverride = [&arena, previous, policy](const Value& read, const Vec& coordinates, Value& out)
    {
        const auto slot = OriginalUnlitSceneDetail::TextureSlot(read);
        std::string role;
        for (const auto* candidate : {"scene_depth", "scene_color_copy"})
        {
            const auto found = policy.Textures.find(candidate);
            if (found != policy.Textures.end() && found->second == slot)
                role = candidate;
        }
        if (role.empty())
            return previous && previous(read, coordinates, out);
        const auto& a = Args(read);
        int sampler = -1;
        if (IsNumber(a[3]) && a[3].number() >= 1352 && a[3].number() <= 1352 + 31 * 8 &&
            std::floor(a[3].number()) == a[3].number() && (int(a[3].number()) - 1352) % 8 == 0)
            sampler = (int(a[3].number()) - 1352) / 8;
        bool qualified = false;
        for (const auto& candidate : policy.Samplers)
            if (candidate.second == sampler &&
                (candidate.first == "point" ||
                 (role == "scene_color_copy" && IsMember(candidate.first, {"copy", "view_clamp"}))))
                qualified = true;
        if (!qualified || a[0].kind != Value::Text || a[0].text != (role == "scene_depth" ? "TXL" : "TEX") ||
            a[1].kind != Value::Text || a[1].text != "2D" || (role == "scene_depth" && !NumberIs(a[7], 0)))
            throw Refused("scene resource sample differs from its exact original operation, sampler or mip",
                          "scene sampler contracts");
        out = arena.mk("SCENETEX", {Value(slot), coordinates[0], coordinates[1]});
        return true;
    };
}
}
} // P4MapKit::Material

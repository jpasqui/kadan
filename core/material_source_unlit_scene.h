// Exact original unlit engine-resource expressions. Resource availability does
// not establish original pass scheduling or stock renderer admission.
#pragma once
#include "material_unlit_projection.h"
#include "material_engine_hashes.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialOriginalUnlitSceneFacts
{
    std::map<std::string, int> ConsumedRoles;
    std::set<std::size_t> EyeAdaptationExpressions, SceneStencilExpressions;
    bool ResourceAbiProved = false;
    bool SchedulingProved = false, CompilerProven = false, RendererProven = false, ProductionAdmitted = false;
};
namespace OriginalUnlitSceneDetail
{
inline int TextureSlot(const Value& read)
{
    if (!is(read, "TEX") || Args(read).size() != 8)
        return -1;
    const auto& texture = Args(read)[2];
    if (!IsNumber(texture) || !std::isfinite(texture.number()) || texture.number() < 328 ||
        texture.number() > 328 + 63 * 8 || std::floor(texture.number()) != texture.number() ||
        (int(texture.number()) - 328) % 8)
        return -1;
    return (int(texture.number()) - 328) / 8;
}
inline std::map<std::string, int> CandidateRoles(const CustomMaterial::ShaderBindings& header)
{
    std::map<std::string, int> roles;
    const auto textures = CustomMaterial::ReadResourceTable(header.textures),
               srvs = CustomMaterial::ReadResourceTable(header.srv);
    for (std::size_t buffer = 0; buffer < header.layout_hashes.size(); ++buffer)
    {
        const auto hash = header.layout_hashes[buffer];
        std::map<std::string, unsigned> ordinals;
        if (hash == 0x0d480502)
            ordinals = {{"eye_adaptation", 65}, {"scene_stencil", 47}};
        else if (hash == 0x05ec0dee)
            ordinals = {{"eye_adaptation", 77}, {"scene_stencil", 47}};
        else if (hash == 0x097f0609)
            ordinals = {{"scene_stencil", 12}};
        else
            continue;
        for (const auto& table : {textures, srvs})
        {
            const auto row = table.find(unsigned(buffer));
            if (row == table.end())
                continue;
            for (const auto& field : row->second)
                for (const auto& role : ordinals)
                    if (field.first == role.second)
                    {
                        const auto old = roles.find(role.first);
                        if (old != roles.end() && old->second != int(field.second))
                            throw Refused("engine expression has conflicting original resource slots",
                                          "material unlit scene resource");
                        roles[role.first] = int(field.second);
                    }
        }
    }
    return roles;
}
}
// Shared exact origin/red-channel lookup for generic surface and lit-particle recovery.
inline void InstallOriginalMaterialEyeAdaptation(SymbolicArena& arena, BindingContext& binding,
                                                 const FMaterialShaderReference& reference,
                                                 const CustomMaterial::ShaderBindings& header, const Json& settings)
{
    const auto candidates = OriginalUnlitSceneDetail::CandidateRoles(header);
    const auto previous = binding.ComponentOverride;
    binding.ComponentOverride =
        [&arena, previous, candidates, reference, header, settings](const Value& value, Value& out)
    {
        if (is(value, "COMP") && Args(value).size() == 2 && is(Args(value)[0], "TEX"))
        {
            const auto& read = Args(value)[0];
            const auto candidate = candidates.find("eye_adaptation");
            if (candidate != candidates.end() && OriginalUnlitSceneDetail::TextureSlot(read) == candidate->second)
            {
                const auto policy = ReadMaterialNativeSceneResourcePolicy(
                    reference, header, settings.At("domain").Text(), settings.At("blend_mode").Text(), true);
                const auto qualified = policy.ResourcePolicy.Textures.find("eye_adaptation");
                if (qualified == policy.ResourcePolicy.Textures.end() || qualified->second != candidate->second)
                    throw Refused("EyeAdaptation lacks its exact constructor and resource-table association",
                                  "material particle EyeAdaptation resource");
                const auto& a = Args(read);
                if (a[0].kind != Value::Text || a[0].text != "TXF" || a[1].kind != Value::Text || a[1].text != "2D" ||
                    !NumberIs(Args(value)[1], 0) ||
                    !std::all_of(a.begin() + 4, a.end(), [](const Value& coordinate)
                                 { return IsNumber(coordinate) && NumberIs(coordinate, 0); }))
                    throw Refused("EyeAdaptation is not its exact origin texel red-channel lookup",
                                  "material particle EyeAdaptation expression");
                out = source(arena, "EyeAdaptation");
                return true;
            }
        }
        return previous && previous(value, out);
    };
}
inline void InstallOriginalUnlitSceneComponents(SymbolicArena& arena, BindingContext& binding,
                                                const FMaterialShaderReference& reference,
                                                const CustomMaterial::ShaderBindings& header, const Json& settings,
                                                bool originalAssociationProved,
                                                const std::shared_ptr<MaterialOriginalUnlitSceneFacts>& facts)
{
    if (settings.At("domain").Text() != "MD_Surface" || settings.At("shading_model").Text() != "MSM_Unlit")
        throw Refused("original resource expressions require unlit surface settings", "material unlit scene inputs");
    const auto candidates = OriginalUnlitSceneDetail::CandidateRoles(header);
    const auto previous = binding.ComponentOverride;
    const auto buffers = binding.Buffers;
    const auto policy = std::make_shared<MaterialOriginalSceneResourcePolicy>();
    const auto checked = std::make_shared<bool>(false);
    binding.ComponentOverride = [&arena, previous, buffers, candidates, reference, header, settings,
                                 originalAssociationProved, facts, policy, checked](const Value& value, Value& out)
    {
        Value read;
        bool eye = false, stencil = false;
        if (is(value, "COMP") && Args(value).size() == 2 && is(Args(value)[0], "TEX"))
        {
            read = Args(value)[0];
            const auto found = candidates.find("eye_adaptation");
            eye = found != candidates.end() && OriginalUnlitSceneDetail::TextureSlot(read) == found->second;
        }
        else if (is(value, "I2F.U") && Args(value).size() == 1)
        {
            const auto& masked = Args(value)[0];
            if (is(masked, "I.AND") && Args(masked).size() == 2 && NumberIs(Args(masked)[1], 2))
            {
                const auto& component = Args(masked)[0];
                if (is(component, "COMP") && Args(component).size() == 2 && NumberIs(Args(component)[1], 1) &&
                    is(Args(component)[0], "TEX"))
                {
                    read = Args(component)[0];
                    const auto found = candidates.find("scene_stencil");
                    stencil = found != candidates.end() && OriginalUnlitSceneDetail::TextureSlot(read) == found->second;
                }
            }
        }
        if (!eye && !stencil)
            return previous && previous(value, out);
        if (!*checked)
        {
            *policy =
                ReadMaterialNativeSceneResourcePolicy(reference, header, settings.At("domain").Text(),
                                                      settings.At("blend_mode").Text(), originalAssociationProved);
            *checked = true;
        }
        const auto role = eye ? "eye_adaptation" : "scene_stencil";
        const auto qualified = policy->ResourcePolicy.Textures.find(role);
        if (qualified == policy->ResourcePolicy.Textures.end() ||
            qualified->second != OriginalUnlitSceneDetail::TextureSlot(read))
            throw Refused("engine expression lacks its exact constructor/SRT role", "material unlit scene resource");
        const auto& a = Args(read);
        if (a[0].kind != Value::Text || a[0].text != "TXF" || a[1].kind != Value::Text || a[1].text != "2D")
            throw Refused("engine expression is not its exact original 2D texel load",
                          "material unlit scene expression");
        if (eye)
        {
            if (!NumberIs(Args(value)[1], 0) ||
                !std::all_of(a.begin() + 4, a.end(),
                             [](const Value& coordinate) { return IsNumber(coordinate) && NumberIs(coordinate, 0); }))
                throw Refused("EyeAdaptation differs from the original origin red-channel lookup",
                              "material unlit EyeAdaptation expression");
            out = source(arena, "EyeAdaptation");
            if (facts)
                facts->EyeAdaptationExpressions.insert(value.expression->id);
        }
        else
        {
            int view = -1;
            for (const auto& buffer : buffers)
                if (buffer.second == "View")
                {
                    if (view != -1)
                        throw Refused("stencil expression has several View buffers", "material unlit stencil View");
                    view = buffer.first;
                }
            const auto named = reference.UniformBufferHashes.find(std::uint16_t(view));
            if (view < 0 || std::size_t(view) >= header.layout_hashes.size() ||
                header.layout_hashes[std::size_t(view)] != EngineHash::ViewLayout ||
                named == reference.UniformBufferHashes.end() || named->second != EngineHash::ViewUniformBuffer)
                throw Refused("stencil coordinates lack the original View constructor association",
                              "material unlit stencil View");
            for (int c = 0; c < 2; ++c)
            {
                // Original View's constructor-proved BufferSizeAndInvSize float4
                // begins at2128. Preserve the source multiplication/cast order.
                auto coordinate = arena.op("MUL", {arena.op("MUL", {arena.mk("IN", {"position", 0, c}),
                                                                    arena.mk("IN", {"buffer", view, 2136 + c * 4})}),
                                                   arena.mk("IN", {"buffer", view, 2128 + c * 4})});
                coordinate = arena.op("TRUNC.S", {arena.op("U.TRUNC", {coordinate})});
                if (!same(a[std::size_t(c) + 4], coordinate))
                    throw Refused("stencil coordinates differ from the original buffer pixel",
                                  "material unlit stencil expression");
            }
            if (!NumberIs(a[7], 0))
                throw Refused("stencil load has a nonzero or symbolic LOD", "material unlit stencil expression");
            // Z is unused by this 2D load; the compiler may repeat Y there.
            out = source(arena, "SceneStencilMask2");
            if (facts)
                facts->SceneStencilExpressions.insert(value.expression->id);
        }
        if (facts)
        {
            facts->ConsumedRoles[role] = qualified->second;
            facts->ResourceAbiProved = true;
        }
        return true;
    };
}
inline MaterialRecoveryResult
RecoverOriginalUnlitSceneExpressionGraph(const MaterialRecoveryPlan& plan, const FMaterialShaderReference& reference,
                                         const CustomMaterial::ShaderBindings& header, bool originalAssociationProved,
                                         MaterialOriginalUnlitSceneFacts* sceneFacts = nullptr,
                                         MaterialUnlitNoFogProjectionFacts* fogFacts = nullptr)
{
    auto qualified = plan;
    const auto previous = plan.ConfigureBinding;
    const auto settings = plan.Settings;
    const auto facts = std::make_shared<MaterialOriginalUnlitSceneFacts>();
    qualified.ConfigureBinding = [previous, reference, header, settings, originalAssociationProved,
                                  facts](SymbolicArena& arena, BindingContext& binding)
    {
        if (previous)
            previous(arena, binding);
        InstallOriginalUnlitSceneComponents(arena, binding, reference, header, settings, originalAssociationProved,
                                            facts);
    };
    auto result =
        RecoverOriginalUnlitNoFogExpressionGraph(qualified, reference, header, originalAssociationProved, fogFacts);
    if (sceneFacts)
        *sceneFacts = *facts;
    return result;
}
}
} // P4MapKit::Material

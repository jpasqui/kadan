// Original unlit material inputs in the explicitly recorded no-fog projection.
// Every other unknown engine field retains the ordinary Binder refusal.
#pragma once
#include "material_masked_unlit.h"
namespace P4MapKit
{
namespace Material
{
inline MaterialRecoveryResult
RecoverOriginalUnlitNoFogExpressionGraph(const MaterialRecoveryPlan& plan, const FMaterialShaderReference& reference,
                                         const CustomMaterial::ShaderBindings& header, bool originalAssociationProved,
                                         MaterialUnlitNoFogProjectionFacts* facts = nullptr)
{
    if (plan.Settings.At("domain").Text() != "MD_Surface" || plan.Settings.At("shading_model").Text() != "MSM_Unlit" ||
        !IsMember(plan.Settings.At("blend_mode").Text(), {"BLEND_Opaque", "BLEND_Translucent", "BLEND_Additive"}))
        throw Refused("original no-fog recovery requires an ordinary unlit surface", "material unlit fog inputs");
    auto projected = plan;
    const auto prior = plan.ConfigureBinding;
    const auto evidence = std::make_shared<MaterialUnlitNoFogProjectionFacts>();
    const auto checked = std::make_shared<std::function<bool(const Value&, Value&)>>();
    const auto settings = plan.Settings;
    projected.ConfigureBinding = [prior, evidence, checked, reference, header, settings,
                                  originalAssociationProved](SymbolicArena& arena, BindingContext& binding)
    {
        if (prior)
            prior(arena, binding);
        const auto previous = binding.LeafOverride;
        // Resolve the original constructor contract only when the final color
        // actually consumes its fog switch. A program with no such read does
        // not require an available scene/fog layout or infer any missing word.
        binding.LeafOverride = [previous, evidence, checked, reference, header, settings,
                                originalAssociationProved](const Value& value, Value& out)
        {
            bool fog = false;
            if (is(value, "IN") && Args(value).size() == 3)
            {
                const auto& a = Args(value);
                if (a[0].kind == Value::Text && a[0].text == "buffer" && IsNumber(a[1]) && IsNumber(a[2]) &&
                    std::isfinite(a[1].number()) && a[1].number() >= 0 &&
                    a[1].number() < double(header.layout_hashes.size()) && std::floor(a[1].number()) == a[1].number() &&
                    NumberIs(a[2], 1836))
                {
                    const auto hash = header.layout_hashes[std::size_t(a[1].number())];
                    fog = hash == 0x0d480502 || hash == 0x05ec0dee;
                }
            }
            if (!fog)
                return previous && previous(value, out);
            if (!*checked)
            {
                const auto projection =
                    PrepareMaterialUnlitNoFogProjection(reference, header, settings, originalAssociationProved);
                BindingContext qualified;
                qualified.LeafOverride = previous;
                InstallMaterialUnlitNoFogProjection(qualified, projection, evidence);
                *checked = qualified.LeafOverride;
            }
            return (*checked)(value, out);
        };
    };
    auto result = RecoverUnlitMaterialExpressionGraph(projected);
    for (const auto& field : evidence->ProjectedFields)
        result.AssumedEngineFields[{"buffer " + std::to_string(field.first.first), field.first.second}] =
            field.second + ": no-fog material-input projection (off)";
    if (facts)
        *facts = *evidence;
    result.CompilerProven = false;
    result.FamilyComplete = false;
    result.RendererValidated = false;
    return result;
}
}
} // P4MapKit::Material

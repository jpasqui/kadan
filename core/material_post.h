// Whole-program postprocess output shell from material_domain_extras.py.
// Exact metadata binding is installed through ConfigureBinding before tracing.
#pragma once
#include "material_decompile.h"
#include "material_post_bind.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialPostProcessSettings
{
    std::string Location = "BL_AfterTonemapping";
    bool OutputAlpha = false, IsBlendable = true;
    int Priority = 0;
};
inline MaterialRecoveryResult RecoverPostProcessExpressionGraph(MaterialRecoveryPlan plan,
                                                                const MaterialPostProcessSettings& settings = {})
{
    if (plan.Settings.At("domain").Text() != "MD_PostProcess")
        throw Refused("postprocess engine contract cannot stand for another domain", "material postprocess domain");
    if (!IsMember(settings.Location,
                  {"BL_BeforeTonemapping", "BL_AfterTonemapping", "BL_BeforeTranslucency", "BL_ReplacingTonemapper"}))
        throw Refused("unbound postprocess blendable location: " + settings.Location, "material postprocess location");
    plan.Stop = plan.Lines.size();
    plan.Outputs = {{"EmissiveColor", "result_color0", {0, 1, 2}}, {"Opacity", "result_color0", {3}}};
    auto bound = BindMaterialOutputs(plan);
    if (bound.KillCount)
        throw Refused("postprocess discard/stencil behavior is not recovered", "material postprocess discard");
    if (!bound.AssumedEngineFields.empty())
        throw Refused("postprocess buffer fields require exact binding", "material postprocess unbound fields");
    for (auto& value : bound.Values[0])
    {
        const auto original = value;
        if (IsExpr(value, "MAX") && Args(value).size() == 2 && NumberIs(Args(value)[1], 0))
            value = Args(value)[0];
        if (symbolic(original) && same(original, value))
            throw Refused("postprocess emissive clamp has not been isolated", "material postprocess emissive clamp");
    }
    if (settings.OutputAlpha)
    {
        auto& alpha = bound.Values[1][0];
        if (IsExpr(alpha, "SAT"))
            alpha = Args(alpha)[0];
    }
    else
    {
        if (!NumberIs(bound.Values[1][0], 1))
            throw Refused("postprocess alpha disagrees with cooked output-alpha flag", "material postprocess alpha");
        bound.Pins.pop_back();
        bound.Values.pop_back();
    }
    plan.Settings["tangent_space_normal"] = true;
    plan.Settings["render_settings"] = Object({{"BlendableLocation", settings.Location},
                                               {"BlendableOutputAlpha", settings.OutputAlpha},
                                               {"BlendablePriority", settings.Priority},
                                               {"bIsBlendable", settings.IsBlendable}});
    return LiftMaterialBoundOutputs(plan, std::move(bound));
}
}
}

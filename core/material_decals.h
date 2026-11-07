// Original editor-material arithmetic for the three source-qualified decal
// output contracts. Projected MRT/vertex wrappers require separate pass proof.
#pragma once
#include "material_surface.h"
#include "material_emission_pipeline.h"
namespace P4MapKit
{
namespace Material
{
inline std::vector<std::string> MaterialDecalOutputContract(const std::string& mode)
{
    if (mode == "DBM_Emissive")
        return {"EmissiveColor", "Opacity"};
    if (mode == "DBM_Stain")
        return {"BaseColor", "Metallic", "Specular", "Roughness", "Opacity", "Normal"};
    if (mode == "DBM_DBuffer_C4_WaterBox")
        return {"BaseColor"};
    throw Refused("unrecovered original decal blend contract: " + mode, "material decal blend contract");
}
inline MaterialRecoveryResult RecoverDecalMaterialExpressionGraph(const MaterialRecoveryPlan& input,
                                                                  const MaterialAnchors& sourceAnchors,
                                                                  const std::string& mode,
                                                                  const MaterialSurfaceRewrites& rewrites = {})
{
    if (input.Settings.At("domain").Text() != "MD_DeferredDecal")
        throw Refused("decal material recovery received another domain", "material decal domain");
    const auto outputs = MaterialDecalOutputContract(mode);
    const auto wants = [&](const std::string& pin)
    {
        return std::find(outputs.begin(), outputs.end(), pin) != outputs.end();
    };
    std::vector<AnchorInstruction> code;
    std::string failure;
    try
    {
        code = ParseMaterialAnchorInstructions(input.Lines);
    }
    catch (const EvalError& error)
    {
        failure = error.what();
    }
    if (!failure.empty())
        throw Refused(failure, "material decal syntax");
    MaterialAnchors found;
    failure.clear();
    try
    {
        found = FindMaterialLightingAnchors(
            code, ForwardMaterialWorldNormal(code, CorrectMaterialAmbientOcclusion(code, sourceAnchors)),
            input.Settings.At("blend_mode").Text());
    }
    catch (const EvalError& error)
    {
        failure = error.what();
    }
    if (!failure.empty())
        throw Refused(failure, "material decal material anchors");
    std::vector<std::string> pins;
    for (const auto& pin : outputs)
        if (pin != "Normal" && pin != "EmissiveColor")
            pins.push_back(pin);
    if (wants("EmissiveColor") && found.count("EmissiveColor"))
        pins.push_back("EmissiveColor");
    MaterialRecoveryPlan plan;
    bool held = false;
    for (const auto* name : {"Normal", "NormalDecal", "NormalWorld"})
        if (found.count(name))
        {
            auto selected = pins;
            selected.push_back(name);
            try
            {
                plan = PlanHeldMaterialOutputs(input, found, selected);
                held = true;
                break;
            }
            catch (const Refused& error)
            {
                if (error.name != "a value whose register is written before it is read")
                    throw;
            }
        }
    if (!held)
        throw Refused("decal material world normal is not held at its material boundary",
                      "material decal material normal");
    auto bound = BindMaterialOutputs(plan);
    const auto normal = bound.Values.back();
    bound.Values.pop_back();
    bound.Pins.pop_back();
    if (wants("EmissiveColor") && !found.count("EmissiveColor"))
    {
        const auto emission = RecoverScheduledMaterialEmissionInputs(plan);
        if (!emission.Found)
            throw Refused("decal proposal is missing source emissive input", "material decal material outputs");
        bound = AppendScheduledMaterialEmission(std::move(bound), emission);
    }
    // The source contract retains original MD_DeferredDecal throughout. The
    // common frame/normal arithmetic is reused without a domain reassignment.
    if (!plan.Settings.Has("render_settings"))
        plan.Settings["render_settings"] = Json::MakeObject();
    RequireKind(plan.Settings.At("render_settings"), Json::Object, "decal render settings");
    plan.Settings["render_settings"]["DecalBlendMode"] = mode;
    auto result = LiftMaterialSurfaceInputs(plan, std::move(bound), normal, rewrites);
    auto selected = Json::MakeObject();
    for (const auto& pin : outputs)
    {
        if (!result.Graph.At("outputs").Has(pin))
            throw Refused("decal proposal is missing material input: " + pin, "material decal material outputs");
        selected[pin] = result.Graph.At("outputs").At(pin);
    }
    result.Graph["outputs"] = selected;
    result.RecoveredPins = outputs;
    result.Validation = ValidateGraph(result.Graph);
    return result;
}
}
} // P4MapKit::Material

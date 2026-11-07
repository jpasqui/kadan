// Bind held and independently scheduled original material values in one arena.
#pragma once
#include "material_source_anchors.h"
#include "material_decompile.h"
namespace P4MapKit
{
namespace Material
{
inline MaterialBoundOutputs BindOriginalMaterialSourceOutputs(const MaterialRecoveryPlan& input,
                                                              const MaterialOriginalSourceAnchors& source,
                                                              const std::vector<std::string>& pins)
{
    if (pins.empty() || pins.size() > 64)
        throw Refused("original scheduled input selection is empty or exceeds its budget", "material output selection");
    std::vector<std::string> held;
    std::set<std::string> unique;
    for (const auto& pin : pins)
    {
        if (pin.empty() || !unique.insert(pin).second)
            throw Refused("original scheduled input selection repeats a pin", "material output selection");
        if (!source.Scheduled.count(pin))
            held.push_back(pin);
        else if (!source.Surface.count(pin))
            throw Refused("scheduled output lacks its original anchor", "material source scheduled anchor");
    }
    const auto plan = PlanHeldMaterialOutputs(input, source.Surface, held);
    RequireKind(plan.Settings, Json::Object, "material settings");
    RequireKind(plan.Defaults, Json::Object, "material defaults");
    if (plan.Lines.empty() || plan.Lines.size() > 100000 || plan.Stop > plan.Lines.size())
        throw Refused("invalid original scheduled prefix", "material trace range");
    std::size_t bytes = 0;
    for (const auto& line : plan.Lines)
    {
        if (line.size() > 65536 || line.size() > 64u * 1024u * 1024u - bytes)
            throw Refused("original scheduled program exceeds its byte limit", "material trace byte limit");
        bytes += line.size();
    }
    auto arena = std::make_shared<SymbolicArena>();
    SymbolicMachine prefix(plan.Lines, arena, MaterialPlanArrayBounds(plan, *arena));
    std::string failure;
    try
    {
        prefix.run(plan.Stop);
    }
    catch (const EvalError& error)
    {
        failure = error.what();
    }
    if (!failure.empty())
        throw Refused(failure, "material symbolic trace");
    auto binding = plan.Binding;
    if (plan.ConfigureBinding)
        plan.ConfigureBinding(*arena, binding);
    Binder binder(*arena, binding);
    std::map<std::string, MaterialOutputRegister> outputs;
    for (const auto& output : plan.Outputs)
        outputs.emplace(output.Pin, output);
    MaterialBoundOutputs result;
    result.Arena = arena;
    result.KillCount = prefix.kills.size() + prefix.symbolic_kills->size();
    result.Pins = pins;
    for (const auto& pin : pins)
    {
        std::vector<Value> raw;
        bool saturate = false, removeClamp = IsMember(pin, {"BaseColor", "Metallic", "Specular", "Roughness",
                                                            "AmbientOcclusion", "SubsurfaceColor", "Opacity"});
        const auto scheduled = source.Scheduled.find(pin);
        if (scheduled != source.Scheduled.end())
        {
            const auto& checked = scheduled->second;
            const auto& anchor = source.Surface.at(pin);
            if (checked.ValueAnchor.line != plan.Stop || checked.ValueAnchor.register_name != anchor.register_name ||
                checked.ValueAnchor.comps != anchor.comps || checked.ScheduledLine > plan.Lines.size() ||
                checked.Indices.empty() || checked.Indices.back() + 1 != checked.ScheduledLine)
                throw Refused("scheduled original output disagrees with its material boundary",
                              "material source scheduled anchor");
            failure.clear();
            try
            {
                raw = ReadScheduledMaterialValue(plan.Lines, checked, prefix);
            }
            catch (const EvalError& error)
            {
                failure = error.what();
            }
            if (!failure.empty())
                throw Refused(failure, "material scheduled trace");
            saturate = anchor.saturate;
        }
        else
        {
            const auto& output = outputs.at(pin);
            saturate = output.Saturate;
            removeClamp = output.RemoveEngineClamp;
            if (output.HasConstant)
                raw = {Value(output.Constant)};
            else
            {
                const auto reg = prefix.reg.find(output.Register);
                if (reg == prefix.reg.end())
                    throw Refused("original selected register was never written: " + output.Register,
                                  "material output register");
                for (const auto component : output.Components)
                    raw.push_back(reg->second.at(std::size_t(component)));
            }
        }
        for (auto& value : raw)
            value = BindMaterialComponent(binder, value);
        if (saturate)
            for (auto& value : raw)
                value = arena->op("SAT", {value});
        if (removeClamp)
        {
            const bool all =
                std::all_of(raw.begin(), raw.end(), [](const Value& value) { return IsExpr(value, "SAT"); });
            if (all)
                for (auto& value : raw)
                    value = Args(value)[0];
        }
        result.Values.push_back(std::move(raw));
    }
    result.AssumedEngineFields = binder.assumed;
    result.CanonicalUnitFields = binder.canonical_unit_fields;
    return result;
}
}
} // P4MapKit::Material

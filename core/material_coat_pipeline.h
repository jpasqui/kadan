// Native clearcoat input orchestration; shader/family/compiler proof is separate.
#pragma once
#include "material_coat.h"
#include "material_depth.h"
namespace P4MapKit
{
namespace Material
{
inline MaterialBoundOutputs RecoverMaterialClearCoatControlInputs(const MaterialRecoveryPlan& plan)
{
    if (plan.Lines.empty() || plan.Lines.size() > 100000 || plan.Stop > plan.Lines.size())
        throw Refused("invalid native clearcoat trace range", "clearcoat trace range");
    std::size_t bytes = 0;
    for (const auto& line : plan.Lines)
    {
        if (line.size() > 65536 || line.size() > 64u * 1024u * 1024u - bytes)
            throw Refused("native clearcoat trace exceeds its byte limit", "clearcoat trace range");
        bytes += line.size();
    }
    auto arena = std::make_shared<SymbolicArena>();
    SymbolicMachine machine(plan.Lines, arena, MaterialPlanArrayBounds(plan, *arena));
    try
    {
        machine.run(plan.Stop);
    }
    catch (const EvalError& e)
    {
        throw Refused(e.what(), "material symbolic trace");
    }
    auto context = plan.Binding;
    if (plan.ConfigureBinding)
        plan.ConfigureBinding(*arena, context);
    Binder binder(*arena, context);
    const auto controls =
        FindMaterialClearCoatControls(ParseMaterialAnchorInstructions(plan.Lines), machine, binder, plan.Stop);
    MaterialBoundOutputs result;
    result.Arena = arena;
    result.KillCount = machine.kills.size() + machine.symbolic_kills->size();
    for (const auto* pin : {"ClearCoat", "ClearCoatRoughness"})
    {
        result.Pins.push_back(pin);
        result.Values.push_back({controls.at(pin).Formula});
    }
    result.AssumedEngineFields = binder.assumed;
    result.CanonicalUnitFields = binder.canonical_unit_fields;
    return result;
}
inline void MergeMaterialBoundFacts(MaterialBoundOutputs& target, const MaterialBoundOutputs& source)
{
    for (const auto& fact : source.AssumedEngineFields)
    {
        const auto previous = target.AssumedEngineFields.find(fact.first);
        if (previous != target.AssumedEngineFields.end() && previous->second != fact.second)
            throw Refused("material programs disagree about engine assumptions", "material bound fact conflict");
        target.AssumedEngineFields.insert(fact);
    }
    target.CanonicalUnitFields.insert(source.CanonicalUnitFields.begin(), source.CanonicalUnitFields.end());
}
inline MaterialBoundOutputs CombineMaterialClearCoatInputs(MaterialBoundOutputs base,
                                                           const MaterialBoundOutputs& controls,
                                                           const MaterialClearCoatBottomNormal& bottom)
{
    if (!base.Arena || !controls.Arena || !bottom.Bound.Arena ||
        controls.Pins != std::vector<std::string>{"ClearCoat", "ClearCoatRoughness"} || controls.Values.size() != 2 ||
        controls.Values[0].size() != 1 || controls.Values[1].size() != 1 ||
        bottom.Bound.Pins != std::vector<std::string>{"ClearCoatBottomNormal"} || bottom.Bound.Values.size() != 1 ||
        bottom.Bound.Values[0].size() != 3)
        throw Refused("invalid independent clearcoat input packet", "clearcoat bound inputs");
    for (const auto& pin : base.Pins)
        if (IsMember(pin, {"ClearCoat", "ClearCoatRoughness", "ClearCoatBottomNormal"}))
            throw Refused("duplicate clearcoat material output", "clearcoat bound inputs");
    MaterialImportMemo controlsMemo, bottomMemo;
    std::vector<std::vector<Value>> values;
    for (const auto& control : controls.Values)
        values.push_back({ImportMaterialValue(*base.Arena, control[0], controlsMemo)});
    values.insert(values.end(), base.Values.begin(), base.Values.end());
    std::vector<Value> normal;
    for (const auto& value : bottom.Bound.Values[0])
        normal.push_back(ImportMaterialValue(*base.Arena, value, bottomMemo));
    values.push_back(normal);
    auto pins = controls.Pins;
    pins.insert(pins.end(), base.Pins.begin(), base.Pins.end());
    pins.push_back("ClearCoatBottomNormal");
    base.Pins = std::move(pins);
    base.Values = std::move(values);
    MergeMaterialBoundFacts(base, controls);
    MergeMaterialBoundFacts(base, bottom.Bound);
    return base;
}
}
}

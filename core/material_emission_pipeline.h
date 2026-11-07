// Recover a late emissive value by its native consumer and exact dependency slice.
// A missing consumer remains explicit; it is never interpreted as constant zero.
#pragma once
#include "material_emission.h"
#include "material_coat_pipeline.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialLateEmissionInputs
{
    bool Found = false, Zero = false;
    MaterialEmissiveAnchor Anchor;
    MaterialBoundOutputs Bound;
};
inline MaterialLateEmissionInputs RecoverScheduledMaterialEmissionInputs(const MaterialRecoveryPlan& plan)
{
    if (plan.Stop > plan.Lines.size() || plan.Lines.size() > 100000)
        throw Refused("invalid late emission prefix", "material trace range");
    std::size_t bytes = 0;
    for (const auto& line : plan.Lines)
    {
        if (line.size() > 65536 || line.size() > 64u * 1024u * 1024u - bytes)
            throw Refused("late emission program exceeds its byte limit", "material trace byte limit");
        bytes += line.size();
    }
    std::vector<AnchorInstruction> code;
    std::string failure;
    try
    {
        code = ParseMaterialAnchorInstructions(plan.Lines);
    }
    catch (const EvalError& error)
    {
        failure = error.what();
    }
    if (!failure.empty())
        throw Refused(failure, "material anchor scan");
    MaterialLateEmissionInputs result;
    failure.clear();
    try
    {
        result.Anchor = FindScheduledMaterialEmissiveAnchor(code, plan.Stop);
    }
    catch (const EvalError& error)
    {
        failure = error.what();
    }
    if (!failure.empty())
        throw Refused(failure, "material anchor scan");
    result.Found = result.Anchor.Found;
    if (!result.Found)
        return result;
    result.Bound.Arena = std::make_shared<SymbolicArena>();
    auto& arena = *result.Bound.Arena;
    auto binding = plan.Binding;
    if (plan.ConfigureBinding)
        plan.ConfigureBinding(arena, binding);
    SymbolicMachine prefix(plan.Lines, result.Bound.Arena, MaterialPlanArrayBounds(plan, arena));
    failure.clear();
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
    std::vector<Value> values;
    failure.clear();
    try
    {
        values = ReadScheduledMaterialValue(plan.Lines, result.Anchor.Value, prefix);
    }
    catch (const EvalError& error)
    {
        failure = error.what();
    }
    if (!failure.empty())
        throw Refused(failure, "material late emission trace");
    std::unique_ptr<Binder> binder;
    std::string category;
    failure.clear();
    try
    {
        binder = std::make_unique<Binder>(arena, binding);
        for (auto& value : values)
            value = binder->bind(value);
    }
    catch (const BindingRefused& error)
    {
        failure = error.what();
        category = error.name;
    }
    catch (const EvalError& error)
    {
        failure = error.what();
        category = "material input binding";
    }
    if (!failure.empty())
        throw Refused(failure, category);
    result.Zero = std::all_of(values.begin(), values.end(),
                              [](const Value& value) { return IsNumber(value) && value.number() == 0.; });
    if (!result.Zero)
    {
        result.Bound.Pins = {"EmissiveColor"};
        result.Bound.Values = {values};
    }
    result.Bound.KillCount = prefix.kills.size() + prefix.symbolic_kills->size();
    result.Bound.AssumedEngineFields = binder->assumed;
    result.Bound.CanonicalUnitFields = binder->canonical_unit_fields;
    return result;
}
inline MaterialBoundOutputs AppendScheduledMaterialEmission(MaterialBoundOutputs base,
                                                            const MaterialLateEmissionInputs& emission)
{
    if (!emission.Found)
        return base;
    if (!base.Arena || !emission.Bound.Arena)
        throw Refused("late emission input has no owned arena", "material emission bound inputs");
    MergeMaterialBoundFacts(base, emission.Bound);
    if (emission.Zero)
        return base;
    if (emission.Bound.Pins != std::vector<std::string>{"EmissiveColor"} || emission.Bound.Values.size() != 1 ||
        emission.Bound.Values[0].size() != 3 ||
        std::find(base.Pins.begin(), base.Pins.end(), "EmissiveColor") != base.Pins.end())
        throw Refused("late emission inputs disagree with the selected pins", "material emission bound inputs");
    MaterialImportMemo memo;
    std::vector<Value> values;
    for (const auto& value : emission.Bound.Values[0])
        values.push_back(ImportMaterialValue(*base.Arena, value, memo));
    base.Pins.push_back("EmissiveColor");
    base.Values.push_back(std::move(values));
    return base;
}
}
}

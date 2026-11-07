// Original held emission contracts after the exact scheduled tail recognizer.
#pragma once
#include "material_emission_pipeline.h"
namespace P4MapKit
{
namespace Material
{
inline bool OriginalMaterialReadsBeforeWrite(const std::vector<AnchorInstruction>& code, const std::string& reg,
                                             char component, std::size_t stop)
{
    if (stop > code.size())
        throw Refused("invalid emission read boundary", "material trace range");
    for (std::size_t at = stop; at < code.size(); ++at)
    {
        const auto& line = code[at];
        for (std::size_t index = 0; index < line.src.size(); ++index)
        {
            const auto& operand = line.src[index];
            if (operand.base != reg)
                continue;
            std::string used;
            if (IsMember(line.op, {"DP2", "DP3", "DP4"}))
            {
                for (int c = 0; c < line.op[2] - '0'; ++c)
                    used += operand.component(std::size_t(c));
            }
            else if (IsMember(line.op, {"RSQ", "RCP", "SIN", "COS", "EX2", "LG2", "POW"}) ||
                     (line.op == "DIV" && index == 1))
                used += operand.component(0);
            else if (IsMember(line.op, {"TEX", "TXL", "TXB", "TXD", "TXF", "TXQ", "TXP", "TXFMS", "TXG"}) ||
                     line.mask.empty())
                used = "xyzw";
            else
                for (const auto c : line.mask)
                    used += operand.component(std::string("xyzw").find(c));
            if (used.find(component) != std::string::npos)
                return true;
        }
        if (line.op == "KIL" && line.text.find(reg) != std::string::npos)
            return true;
        if (line.dst == reg && line.mask.find(component) != std::string::npos &&
            line.text.substr(0, line.text.find(',')).find('(') == std::string::npos)
            return false;
    }
    return false;
}
inline bool OriginalMaterialDependsOnEmission(const Value& value, const std::set<const Expression*>& leaves)
{
    std::vector<Value> pending{value};
    std::set<const Expression*> seen;
    while (!pending.empty())
    {
        const auto current = pending.back();
        pending.pop_back();
        if (!symbolic(current))
            continue;
        if (leaves.count(current.expression.get()))
            return true;
        if (!seen.insert(current.expression.get()).second)
            continue;
        if (seen.size() > 1000000)
            throw Refused("emission dependency exceeds its node limit", "material expression budget");
        for (const auto& arg : current.expression->args)
            pending.push_back(arg);
    }
    return false;
}
inline MaterialLateEmissionInputs RecoverOriginalMaterialEmissionInputs(const MaterialRecoveryPlan& plan)
{
    auto result = RecoverScheduledMaterialEmissionInputs(plan);
    if (result.Found)
        return result;
    const auto code = ParseMaterialAnchorInstructions(plan.Lines);
    std::string registerName, components;
    // The original held-tail recognizer precedes collection sensitivity.
    if (!code.empty() && starts(code.back().dst, "result"))
    {
        const auto end = std::max(plan.Stop, code.size() > 26 ? code.size() - 26 : std::size_t(0));
        for (std::size_t at = code.size() > 1 ? code.size() - 2 : 0; at > end; --at)
        {
            const auto& line = code[at];
            if (line.op != "MAX" || line.mask != "xyz" || line.src.size() != 2 || !line.src[1].literal ||
                line.src[1].scalar() != 0 || line.src[0].literal)
                continue;
            const auto input = line.reads(0, "xyz");
            if (!AnchorWritten(code, input.first, input.second, plan.Stop, at))
            {
                registerName = input.first;
                components = input.second;
            }
            break;
        }
    }
    int collection = -1;
    for (const auto& buffer : plan.Binding.Buffers)
        if (starts(buffer.second, "MaterialCollection"))
        {
            collection = buffer.first;
            break;
        }
    if (registerName.empty() && collection < 0)
        return result;
    result.Bound.Arena = std::make_shared<SymbolicArena>();
    auto& arena = *result.Bound.Arena;
    SymbolicMachine prefix(plan.Lines, result.Bound.Arena, MaterialPlanArrayBounds(plan, arena));
    try
    {
        prefix.run(plan.Stop);
    }
    catch (const EvalError& error)
    {
        throw Refused(error.what(), "material symbolic trace");
    }
    if (registerName.empty())
    {
        std::set<const Expression*> leaves;
        for (int offset : {0, 4})
            leaves.insert(arena.mk("IN", {Value("buffer"), Value(collection), Value(offset)}).expression.get());
        std::vector<std::string> candidates;
        for (const auto& reg : prefix.reg)
        {
            if (std::any_of(reg.second.begin(), reg.second.end(),
                            [](const Value& value) { return value.kind == Value::Tuple; }))
                continue;
            bool depending = false;
            for (std::size_t component = 0; component < 3; ++component)
                depending = depending || OriginalMaterialDependsOnEmission(reg.second[component], leaves);
            if (depending && OriginalMaterialReadsBeforeWrite(code, reg.first, 'x', plan.Stop) &&
                OriginalMaterialReadsBeforeWrite(code, reg.first, 'y', plan.Stop) &&
                OriginalMaterialReadsBeforeWrite(code, reg.first, 'z', plan.Stop))
                candidates.push_back(reg.first);
        }
        if (candidates.empty())
            return MaterialLateEmissionInputs();
        if (candidates.size() != 1)
            throw Refused("several registers could be the original emissive color",
                          "several registers could be the emissive color");
        registerName = candidates.front();
        components = "xyz";
    }
    const auto held = prefix.reg.find(registerName);
    if (held == prefix.reg.end())
        throw Refused("original emission register was never written", "material output register");
    auto binding = plan.Binding;
    if (plan.ConfigureBinding)
        plan.ConfigureBinding(arena, binding);
    Binder binder(arena, binding);
    std::vector<Value> values;
    for (const auto component : components)
        values.push_back(BindMaterialComponent(binder, held->second.at(std::string("xyzw").find(component))));
    result.Found = true;
    result.Zero = std::all_of(values.begin(), values.end(),
                              [](const Value& value) { return IsNumber(value) && value.number() == 0.; });
    if (!result.Zero)
    {
        result.Bound.Pins = {"EmissiveColor"};
        result.Bound.Values = {values};
    }
    result.Bound.KillCount = prefix.kills.size() + prefix.symbolic_kills->size();
    result.Bound.AssumedEngineFields = binder.assumed;
    result.Bound.CanonicalUnitFields = binder.canonical_unit_fields;
    return result;
}
}
} // P4MapKit::Material

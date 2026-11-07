// Original GetLightFunctionColor material-call boundary. Engine wrappers and
// vertex stages remain independently unqualified; unsupplied caller data refuses.
#pragma once
#include "material_source_plan.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialLightFunctionCall
{
    std::size_t First = 0, Start = 0, Stop = 0;
    int PositionBuffer = -1, PositionOffset = 0;
    std::string PositionParameter, WorldRegister, OutputRegister, Components;
};
inline MaterialLightFunctionCall FindMaterialLightFunctionCall(const MaterialRecoveryPlan& plan)
{
    if (plan.Lines.empty() || plan.Lines.size() > 100000)
        throw Refused("light-function program has no bounded instruction set", "material light function program");
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
        throw Refused(failure, "material light function syntax");
    MaterialLightFunctionCall result;
    std::vector<std::size_t> dots;
    for (std::size_t i = 0; i < code.size(); ++i)
    {
        const auto& line = code[i];
        if (line.op == "DP3" && line.src.size() == 2 && line.src[1].literal &&
            std::fabs(line.src[1].scalar() - .3333) < 1e-7)
            dots.push_back(i);
    }
    if (dots.size() != 1)
        throw Refused("light-function has no unique grayscale material consumer", "material light function anchor");
    result.Stop = dots[0];
    bool found = false;
    for (std::size_t i = 0; i < result.Stop && !found; ++i)
    {
        const auto& line = code[i];
        if (line.op != "LDC" || line.mask != "xyz" || line.src.size() != 1)
            continue;
        for (const auto& buffer : plan.Binding.Buffers)
        {
            const bool collection = starts(buffer.second, "MaterialCollection");
            if (buffer.second != "Material" && !collection)
                continue;
            const int offset = collection ? 80 : 16;
            if (line.src[0].base != "buf" + std::to_string(buffer.first) + "[" + std::to_string(offset) + "]" ||
                !line.src[0].swizzle.empty() || line.src[0].negative || line.src[0].absolute)
                continue;
            result.First = i;
            result.PositionBuffer = buffer.first;
            result.PositionOffset = offset;
            result.PositionParameter = collection ? "ShadowProjectionParams02" : "LightPosition";
            found = true;
            break;
        }
    }
    if (!found)
        throw Refused("light-function has no checked light-position load", "material light function position binding");
    const auto& uniform = code[result.First];
    std::vector<std::pair<std::size_t, std::string>> candidates;
    for (std::size_t i = result.First + 1; i < std::min(result.First + 12, result.Stop); ++i)
    {
        const auto& line = code[i];
        if (line.op != "ADD" || (line.mask != "xy" && line.mask != "xyz") || line.src.size() != 2)
            continue;
        for (std::size_t k = 0; k < 2; ++k)
        {
            const auto& operand = line.src[k];
            const auto& other = line.src[1 - k];
            if (operand.base == uniform.dst && operand.negative && !operand.absolute && !other.negative &&
                !other.absolute && !other.literal && !AnchorWritten(code, uniform.dst, "xyz", result.First + 1, i))
                candidates.push_back({i, other.base});
        }
    }
    if (candidates.size() != 1)
        throw Refused("light-function world-position call argument is not unique",
                      "material light function world argument");
    result.Start = candidates[0].first;
    result.WorldRegister = candidates[0].second;
    if (!std::regex_match(result.WorldRegister, std::regex("R[0-9]+")))
        throw Refused("light-function world argument is not a native register",
                      "material light function world argument");
    failure.clear();
    try
    {
        const auto read = code[result.Stop].reads(0, "xyz");
        result.OutputRegister = read.first;
        result.Components = read.second;
    }
    catch (const EvalError& error)
    {
        failure = error.what();
    }
    if (!failure.empty())
        throw Refused(failure, "material light function syntax");
    return result;
}
// Ordinary 2D TEX reads only xy. Remove its inactive zw expressions before the
// generic binder visits children; never prune TXL/TXB or other texture targets.
inline Value MaterialLightFunctionActiveTextureInputs(SymbolicArena& arena, const Value& root,
                                                      std::map<const Expression*, Value>& memo)
{
    if (!symbolic(root))
        return root;
    std::vector<Value> work = {root};
    while (!work.empty())
    {
        const auto value = work.back();
        const auto* node = value.expression.get();
        if (memo.count(node))
        {
            work.pop_back();
            continue;
        }
        if (memo.size() + work.size() > 1000000)
            throw Refused("light-function expression exceeds its node budget",
                          "material light function expression budget");
        const bool ordinary = node->op == "TEX" && node->args.size() == 8 && node->args[0].kind == Value::Text &&
                              node->args[0].text == "TEX" && node->args[1].kind == Value::Text &&
                              node->args[1].text == "2D";
        bool pending = false;
        for (std::size_t i = 0; i < node->args.size(); ++i)
        {
            if (ordinary && i >= 6)
                continue;
            const auto& argument = node->args[i];
            if (symbolic(argument) && !memo.count(argument.expression.get()))
            {
                work.push_back(argument);
                pending = true;
                break;
            }
        }
        if (pending)
            continue;
        std::vector<Value> args;
        for (std::size_t i = 0; i < node->args.size(); ++i)
        {
            const auto& argument = node->args[i];
            args.push_back(ordinary && i >= 6   ? Value(0.)
                           : symbolic(argument) ? memo.at(argument.expression.get())
                                                : argument);
        }
        memo[node] = arena.mk(node->op, args);
        work.pop_back();
    }
    return memo.at(root.expression.get());
}
inline bool MaterialLightFunctionNamedPosition(const Value& value, const std::string& expected, int component)
{
    if (!is(value, "COMP") || Args(value).size() != 2 || !NumberIs(Args(value)[1], component) ||
        !is(Args(value)[0], "SRC"))
        return false;
    const auto& src = Args(Args(value)[0]);
    if (src.size() != 2)
        return false;
    const auto& detail = src[1];
    if (detail.kind == Value::Text)
        return detail.text == expected;
    return detail.kind == Value::Tuple && detail.tuple.size() == 2 && detail.tuple[1].kind == Value::Text &&
           detail.tuple[1].text == expected;
}
struct MaterialLightFunctionInputs
{
    MaterialLightFunctionCall Call;
    MaterialBoundOutputs Bound;
};
inline MaterialLightFunctionInputs RecoverMaterialLightFunctionInputs(const MaterialRecoveryPlan& plan)
{
    if (!plan.Settings.Has("domain") || plan.Settings.At("domain").Text() != "MD_LightFunction")
        throw Refused("light-function recovery received another domain", "material light function domain");
    MaterialLightFunctionInputs result;
    result.Call = FindMaterialLightFunctionCall(plan);
    result.Bound.Arena = std::make_shared<SymbolicArena>();
    auto& arena = *result.Bound.Arena;
    auto binding = plan.Binding;
    if (plan.ConfigureBinding)
        plan.ConfigureBinding(arena, binding);
    const auto prior = binding.LeafOverride;
    binding.LeafOverride = [&arena, prior](const Value& value, Value& out)
    {
        if (!is(value, "IN") || Args(value).size() != 3)
            return prior && prior(value, out);
        const auto& args = Args(value);
        if (args[0].kind == Value::Text && args[0].text == "unbound_register")
            throw BindingRefused("light-function material call reads an unsupplied caller register",
                                 "material light function caller input");
        if (args[0].kind == Value::Text && args[0].text == "attrib" && NumberIs(args[1], 99))
        {
            out = source(arena, "WorldPosition", Value::null(), int(args[2].number()));
            return true;
        }
        if (args[0].kind == Value::Text && args[0].text == "buffer" && NumberIs(args[1], 0) && NumberIs(args[2], 2440))
        {
            out = source(arena, "Time");
            return true;
        }
        return prior && prior(value, out);
    };
    std::unique_ptr<Binder> binder;
    std::string failure, category;
    try
    {
        binder = std::make_unique<Binder>(arena, binding);
    }
    catch (const BindingRefused& error)
    {
        failure = error.what();
        category = error.name;
    }
    catch (const EvalError& error)
    {
        failure = error.what();
        category = "material light function binding";
    }
    if (!failure.empty())
        throw Refused(failure, category);
    for (int c = 0; c < 3; ++c)
    {
        const auto value = BindMaterialComponent(
            *binder, arena.mk("IN", {"buffer", result.Call.PositionBuffer, result.Call.PositionOffset + 4 * c}));
        if (!MaterialLightFunctionNamedPosition(value, result.Call.PositionParameter, c))
            throw Refused("first world subtraction does not consume checked " + result.Call.PositionParameter,
                          "material light function position binding");
    }
    const auto code = ParseMaterialAnchorInstructions(plan.Lines);
    std::vector<std::string> lines(plan.Lines.begin() + result.Call.First, plan.Lines.begin() + result.Call.Stop);
    SymbolicMachine machine(lines, result.Bound.Arena, MaterialPlanArrayBounds(plan, arena));
    const auto registerName = [](const std::string& name)
    {
        return std::regex_match(name, std::regex("R[0-9]+"));
    };
    for (const auto& line : code)
    {
        std::vector<std::string> names = {line.dst};
        for (const auto& operand : line.src)
            names.push_back(operand.base);
        for (const auto& name : names)
            if (registerName(name))
            {
                Vec values;
                for (int c = 0; c < 4; ++c)
                    values[c] = arena.mk("IN", {"unbound_register", name, c});
                machine.reg[name] = values;
            }
    }
    failure.clear();
    try
    {
        const auto at = result.Call.Start - result.Call.First;
        machine.run_range(0, at, at);
        for (int c = 0; c < 3; ++c)
            machine.reg[result.Call.WorldRegister][c] = arena.mk("IN", {"attrib", 99, c});
        machine.run_range(at, lines.size(), lines.size());
    }
    catch (const EvalError& error)
    {
        failure = error.what();
    }
    if (!failure.empty())
        throw Refused(failure, "material light function trace");
    const auto output = machine.reg.find(result.Call.OutputRegister);
    if (output == machine.reg.end())
        throw Refused("light-function output register was not written", "material light function output");
    std::vector<Value> values;
    std::map<const Expression*, Value> memo;
    for (const auto c : result.Call.Components)
    {
        auto value = MaterialLightFunctionActiveTextureInputs(arena, output->second[std::string("xyzw").find(c)], memo);
        value = BindMaterialComponent(*binder, value);
        if (is(value, "MAX") && Args(value).size() == 2 && NumberIs(Args(value)[1], 0))
            value = Args(value)[0];
        values.push_back(value);
    }
    if (!binder->assumed.empty())
        throw Refused("light-function requires unnamed engine inputs", "material light function assumed inputs");
    result.Bound.Pins = {"EmissiveColor"};
    result.Bound.Values = {values};
    result.Bound.KillCount = machine.kills.size() + machine.symbolic_kills->size();
    result.Bound.CanonicalUnitFields = binder->canonical_unit_fields;
    return result;
}
inline MaterialRecoveryResult RecoverLightFunctionMaterialExpressionGraph(const MaterialRecoveryPlan& plan)
{
    return LiftMaterialBoundOutputs(plan, RecoverMaterialLightFunctionInputs(plan).Bound);
}
}
} // P4MapKit::Material

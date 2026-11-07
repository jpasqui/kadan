// Whole-program unlit input recovery. Other shader/factory/domain programs and
// vertex stages still require separate qualification before a family is complete.
#pragma once
#include "material_decompile.h"
#include "material_rewrite.h"
namespace P4MapKit
{
namespace Material
{
inline bool MaterialNonnegative(const Value& root)
{
    const auto literal = [](const Value& v)
    {
        return IsNumber(v) && std::isfinite(v.number()) && v.number() >= 0;
    };
    if (!symbolic(root))
        return literal(root);
    std::map<const Expression*, bool> memo;
    std::vector<Value> pending{root};
    while (!pending.empty())
    {
        auto value = pending.back();
        const auto& x = *value.expression;
        if (memo.count(&x))
        {
            pending.pop_back();
            continue;
        }
        bool waits = false;
        for (const auto& a : x.args)
            if (symbolic(a) && !memo.count(a.expression.get()))
            {
                pending.push_back(a);
                waits = true;
                break;
            }
        if (waits)
            continue;
        const auto fact = [&](const Value& a)
        {
            return symbolic(a) ? memo.at(a.expression.get()) : literal(a);
        };
        bool result = false;
        if (IsMember(x.op, {"SAT", "ABS", "TRUNC.U", "I2F.U", "POW"}) || starts(x.op, "F.S"))
            result = true;
        else if (x.op == "ADD" || x.op == "MIN")
        {
            result = true;
            for (const auto& a : x.args)
                result &= fact(a);
        }
        else if (x.op == "MAX")
        {
            for (const auto& a : x.args)
                result |= fact(a);
        }
        else if (x.op == "MUL" && x.args.size() == 2)
            result = Same(x.args[0], x.args[1]) || (fact(x.args[0]) && fact(x.args[1]));
        else if ((x.op == "U.FLR" || x.op == "U.CEIL") && !x.args.empty())
            result = fact(x.args[0]);
        else if (x.op == "SEL" && x.args.size() == 3)
            result = fact(x.args[1]) && fact(x.args[2]);
        memo[&x] = result;
        pending.pop_back();
    }
    return memo.at(root.expression.get());
}
inline MaterialRecoveryResult RecoverUnlitMaterialExpressionGraph(const MaterialRecoveryPlan& plan)
{
    RequireKind(plan.Settings, Json::Object, "material settings");
    const auto model = plan.Settings.At("shading_model").Text(), domain = plan.Settings.At("domain").Text(),
               blend = plan.Settings.At("blend_mode").Text();
    if (model != "MSM_Unlit")
        throw Refused("the whole-program unlit stage requires MSM_Unlit", "unlit shading model");
    if (!IsMember(domain, {"MD_Surface", "MD_UI"}))
        throw Refused("unlit domain requires a separate native shader contract: " + domain, "unlit domain " + domain);
    if (!IsMember(blend, {"BLEND_Opaque", "BLEND_Translucent", "BLEND_Additive"}))
        throw Refused("this unlit blend mode requires another native input contract: " + blend,
                      "unlit blend mode " + blend);
    if (plan.Lines.empty() || plan.Lines.size() > 1000000)
        throw Refused("invalid unlit native program range", "material trace range");
    std::size_t bytes = 0;
    for (const auto& line : plan.Lines)
    {
        if (line.size() > 1024u * 1024u || line.size() > 64u * 1024u * 1024u - bytes)
            throw Refused("unlit material program exceeds its byte limit", "material trace byte limit");
        bytes += line.size();
    }
    auto arena = std::make_shared<SymbolicArena>();
    auto binding = plan.Binding;
    if (plan.ConfigureBinding)
        plan.ConfigureBinding(*arena, binding);
    SymbolicMachine trace(plan.Lines, arena, MaterialPlanArrayBounds(plan, *arena));
    std::string traceFailure;
    try
    {
        trace.run();
    }
    catch (const EvalError& e)
    {
        traceFailure = e.what();
    }
    if (!traceFailure.empty())
        throw Refused(traceFailure, "material symbolic trace");
    if (!trace.kills.empty() || !trace.symbolic_kills->empty())
        throw Refused("an unlit material that discards pixels (not recovered yet)");
    const auto color = trace.reg.find("result_color0");
    if (color == trace.reg.end())
        throw Refused("native unlit program does not write result_color0", "unlit output register");
    std::unique_ptr<Binder> binder;
    Vec bound;
    std::string bindingFailure, bindingCategory;
    try
    {
        binder = std::make_unique<Binder>(*arena, binding);
        bound = binder->bind(color->second);
    }
    catch (const BindingRefused& e)
    {
        bindingFailure = e.what();
        bindingCategory = e.name;
    }
    catch (const EvalError& e)
    {
        bindingFailure = e.what();
        bindingCategory = "material input binding";
    }
    if (!bindingFailure.empty())
        throw Refused(bindingFailure, bindingCategory);
    bool allowNegative = false;
    if (plan.Settings.Has("render_settings") && plan.Settings.At("render_settings").Has("bAllowNegativeEmissiveColor"))
    {
        const auto& value = plan.Settings.At("render_settings").At("bAllowNegativeEmissiveColor");
        RequireKind(value, Json::Boolean, "bAllowNegativeEmissiveColor");
        allowNegative = value.boolean;
    }
    std::vector<Value> emissive;
    for (int c = 0; c < 3; ++c)
    {
        const auto& value = bound[c];
        const bool clamped = IsExpr(value, "MAX") && Args(value).size() == 2 && NumberIs(Args(value)[1], 0);
        if (!allowNegative && !clamped && !MaterialNonnegative(value))
            throw Refused("an unlit color that is not max(EmissiveColor, 0)");
        emissive.push_back(clamped ? Args(value)[0] : value);
    }
    const bool alpha = !(IsNumber(bound[3]) && NumberIs(bound[3], 0));
    if (alpha != (blend == "BLEND_Translucent"))
        throw Refused("unlit alpha output disagrees with the cooked blend mode", "settings disagree: blend mode");
    std::vector<std::vector<Value>> values{emissive};
    if (alpha)
        values.push_back({IsExpr(bound[3], "SAT") ? Args(bound[3])[0] : bound[3]});
    if (plan.Rewrite)
        plan.Rewrite(*arena, values);
    if (values.size() != std::size_t(alpha ? 2 : 1))
        throw Refused("native unlit rewrite changed its output count", "material rewrite outputs");
    Lifter lifter(*arena, plan.Defaults);
    std::vector<Value> all;
    for (const auto& row : values)
        all.insert(all.end(), row.begin(), row.end());
    lifter.normal_samples = plan.NormalSamples ? plan.NormalSamples(lifter, all) : normal_sample_ids(lifter, all);
    MaterialRecoveryResult result;
    result.Graph = plan.Settings;
    result.Graph["format"] = 2;
    result.Graph["tangent_space_normal"] = true;
    result.Graph["outputs"] = Object({{"EmissiveColor", lifter.lift(values[0])}});
    result.RecoveredPins = {"EmissiveColor"};
    if (alpha)
    {
        result.Graph["outputs"]["Opacity"] = lifter.lift(values[1]);
        result.RecoveredPins.push_back("Opacity");
    }
    result.Graph["nodes"] = lifter.nodes;
    result.Validation = ValidateGraph(result.Graph);
    result.TraceStop = plan.Lines.size();
    result.AssumedEngineFields = binder->assumed;
    result.CanonicalUnitFields = binder->canonical_unit_fields;
    return result;
}
}
}

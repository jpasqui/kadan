// Native material stage orchestration. A recovered expression graph and a
// compiler/family proof are separate results; this API never manufactures one.
#pragma once
#include "material_bind.h"
#include "material_lifter.h"
#include "material_anchor.h"

namespace P4MapKit
{
namespace Material
{
struct MaterialOutputRegister
{
    std::string Pin, Register;
    std::vector<int> Components;
    bool HasConstant = false;
    double Constant = 0;
    bool Saturate = false, RemoveEngineClamp = false;
};
struct MaterialRecoveryPlan
{
    std::vector<std::string> Lines;
    std::size_t Stop = 0;
    BindingContext Binding;
    InputBounds ArrayBounds;
    // Set only by original source preparation after the exact View identity and
    // layout check. The arena-specific expression IDs are created at trace time.
    int OriginalViewportBuffer = -1;
    Json Settings = Json::MakeObject(), Defaults = Json::MakeObject();
    std::vector<MaterialOutputRegister> Outputs;
    std::function<void(SymbolicArena&, BindingContext&)> ConfigureBinding;
    // Native family/anchor stages install only their source-checked rewrites.
    // The callback receives every output together to retain shared identities.
    std::function<void(SymbolicArena&, std::vector<std::vector<Value>>&)> Rewrite;
    std::function<std::set<std::size_t>(Lifter&, const std::vector<Value>&)> NormalSamples;
};
struct MaterialRecoveryResult
{
    Json Graph;
    GraphValidation Validation;
    std::map<std::pair<std::string, int>, std::string> AssumedEngineFields;
    std::set<std::pair<std::string, int>> CanonicalUnitFields;
    std::vector<std::string> RecoveredPins;
    std::size_t TraceStop = 0;
    // These stages reconstruct a selected shader prefix. Family dispatch,
    // other pixel programs, vertex stages and renderer execution need their
    // independent receipts; matching graph topology cannot qualify them.
    bool CompilerProven = false, FamilyComplete = false, RendererValidated = false;
};
inline MaterialRecoveryPlan PlanHeldMaterialOutputs(const MaterialRecoveryPlan& inputs, const MaterialAnchors& anchors,
                                                    const std::vector<std::string>& selectedPins)
{
    MaterialRecoveryPlan plan = inputs;
    plan.Outputs.clear();
    const auto marker = anchors.find("stop");
    if (marker == anchors.end())
        throw Refused("this native anchor plan requires a lighting stop", "material anchor stop");
    plan.Stop = marker->second.line;
    if (plan.Stop > plan.Lines.size())
        throw Refused("material anchor stop exceeds the program", "material anchor stop");
    // A leading IF has modified no register. It can be removed from the stop
    // boundary only while no branch instruction lies between it and the stop.
    while (plan.Stop && starts(plan.Lines[plan.Stop - 1], "IF "))
        --plan.Stop;
    std::vector<AnchorInstruction> code;
    try
    {
        code = ParseMaterialAnchorInstructions(plan.Lines);
    }
    catch (const EvalError& e)
    {
        throw Refused(e.what(), "material anchor scan");
    }
    for (const auto& pin : selectedPins)
    {
        const auto found = anchors.find(pin);
        if (found == anchors.end())
            throw Refused("selected material pin has no native anchor: " + pin, "material output anchor");
        const auto& anchor = found->second;
        MaterialOutputRegister output;
        output.Pin = pin;
        output.Saturate = anchor.saturate;
        output.RemoveEngineClamp = IsMember(
            pin, {"BaseColor", "Metallic", "Specular", "Roughness", "AmbientOcclusion", "SubsurfaceColor", "Opacity"});
        if (anchor.line > plan.Lines.size())
            throw Refused("native anchor line exceeds the shader program", "material anchor line");
        if (anchor.has_constant)
        {
            output.HasConstant = true;
            output.Constant = anchor.constant;
            if (!std::isfinite(output.Constant))
                throw Refused("material anchor constant is not finite", "material anchor constant");
            output.Components = {0};
        }
        else
        {
            const auto low = anchor.line >= plan.Stop ? plan.Stop : anchor.line + 1,
                       high = anchor.line >= plan.Stop ? anchor.line : plan.Stop;
            if (AnchorWritten(code, anchor.register_name, anchor.comps, low, high))
                throw Refused(pin + ": its register is overwritten before its native anchor reads it",
                              "a value whose register is written before it is read");
            output.Register = anchor.register_name;
            for (const auto c : anchor.comps)
            {
                const auto at = std::string("xyzw").find(c);
                if (at == std::string::npos)
                    throw Refused("native anchor has an invalid component", "material anchor component");
                output.Components.push_back(int(at));
            }
        }
        plan.Outputs.push_back(std::move(output));
    }
    return plan;
}
struct MaterialBoundOutputs
{
    std::shared_ptr<SymbolicArena> Arena;
    std::size_t KillCount = 0;
    std::vector<std::string> Pins;
    std::vector<std::vector<Value>> Values;
    std::map<std::pair<std::string, int>, std::string> AssumedEngineFields;
    std::set<std::pair<std::string, int>> CanonicalUnitFields;
};
inline Value BindMaterialComponent(Binder& binder, const Value& value)
{
    Value result;
    std::string message, name;
    bool failed = false;
    try
    {
        result = binder.bind(value);
    }
    catch (const BindingRefused& e)
    {
        message = e.what();
        name = e.name;
        failed = true;
    }
    catch (const EvalError& e)
    {
        message = e.what();
        name = "material input binding";
        failed = true;
    }
    if (failed)
        throw Refused(message, name);
    return result;
}
inline InputBounds MaterialPlanArrayBounds(const MaterialRecoveryPlan& plan, SymbolicArena& arena)
{
    auto result = plan.ArrayBounds;
    if (plan.OriginalViewportBuffer < 0)
        return result;
    for (int component = 0; component < 2; ++component)
    {
        const auto position = arena.mk("IN", {"position", 0, component});
        const auto rect = arena.mk("IN", {"buffer", plan.OriginalViewportBuffer, 2080 + 4 * component});
        const auto relative = arena.mk("ADD", {position, arena.mk("NEG", {rect})});
        result[relative.expression->id] = Bound::lower_only(0);
        for (const auto scale : {.5, 1.})
            result[arena.mk("MUL", {relative, scale}).expression->id] = Bound::lower_only(0);
    }
    return result;
}
inline MaterialBoundOutputs BindMaterialOutputs(const MaterialRecoveryPlan& plan)
{
    if (plan.Lines.empty() || plan.Lines.size() > 1000000 || plan.Stop > plan.Lines.size())
        throw Refused("invalid native material trace range", "material trace range");
    std::size_t bytes = 0;
    for (const auto& line : plan.Lines)
    {
        if (line.size() > 1024u * 1024u || line.size() > 64u * 1024u * 1024u - bytes)
            throw Refused("native material trace exceeds its byte limit", "material trace byte limit");
        bytes += line.size();
    }
    if (plan.Outputs.empty() || plan.Outputs.size() > 64)
        throw Refused("native material plan has no bounded output set", "material output selection");
    RequireKind(plan.Settings, Json::Object, "material settings");
    RequireKind(plan.Defaults, Json::Object, "material defaults");
    std::set<std::string> selectedPins;
    for (const auto& selected : plan.Outputs)
    {
        if (selected.Pin.empty() || !selectedPins.insert(selected.Pin).second || selected.Components.empty() ||
            selected.Components.size() > 4)
            throw Refused("invalid or duplicate native material output selection", "material output selection");
        if (selected.HasConstant && !std::isfinite(selected.Constant))
            throw Refused("material anchor constant is not finite", "material anchor constant");
        for (const auto c : selected.Components)
            if (c < 0 || c > 3)
                throw Refused("selected material component is outside float4", "material output component");
    }
    auto arena = std::make_shared<SymbolicArena>();
    SymbolicMachine trace(plan.Lines, arena, MaterialPlanArrayBounds(plan, *arena));
    try
    {
        trace.run(plan.Stop);
    }
    catch (const EvalError& e)
    {
        throw Refused(e.what(), "material symbolic trace");
    }
    auto context = plan.Binding;
    if (plan.ConfigureBinding)
        plan.ConfigureBinding(*arena, context);
    Binder binder(*arena, context);
    std::vector<std::vector<Value>> values;
    for (const auto& selected : plan.Outputs)
    {
        if (selected.HasConstant)
        {
            if (!std::isfinite(selected.Constant))
                throw Refused("material anchor constant is not finite", "material anchor constant");
            values.push_back({Value(selected.Constant)});
            continue;
        }
        const auto found = trace.reg.find(selected.Register);
        if (found == trace.reg.end())
            throw Refused("selected material register was never written: " + selected.Register,
                          "material output register");
        std::vector<Value> components;
        for (const auto c : selected.Components)
            components.push_back(BindMaterialComponent(binder, found->second[std::size_t(c)]));
        if (selected.Saturate)
            for (auto& value : components)
                value = arena->op("SAT", {value});
        if (selected.RemoveEngineClamp)
        {
            bool all = true;
            for (const auto& value : components)
                all &= IsExpr(value, "SAT");
            if (all)
                for (auto& value : components)
                    value = Args(value)[0];
        }
        values.push_back(std::move(components));
    }
    MaterialBoundOutputs bound;
    bound.Arena = arena;
    bound.Values = std::move(values);
    bound.KillCount = trace.kills.size() + trace.symbolic_kills->size();
    for (const auto& output : plan.Outputs)
        bound.Pins.push_back(output.Pin);
    bound.AssumedEngineFields = binder.assumed;
    bound.CanonicalUnitFields = binder.canonical_unit_fields;
    return bound;
}
inline MaterialRecoveryResult LiftMaterialBoundOutputs(const MaterialRecoveryPlan& plan, MaterialBoundOutputs bound)
{
    if (!bound.Arena || bound.Pins.empty() || bound.Pins.size() > 64 || bound.Pins.size() != bound.Values.size())
        throw Refused("invalid native bound output packet", "material bound outputs");
    auto& arena = *bound.Arena;
    auto& values = bound.Values;
    if (plan.Rewrite)
        plan.Rewrite(arena, values);
    if (values.size() != bound.Pins.size())
        throw Refused("native family rewrite changed the output count", "material rewrite outputs");
    Lifter lifter(arena, plan.Defaults);
    if (plan.NormalSamples)
    {
        std::vector<Value> flat;
        for (const auto& output : values)
            flat.insert(flat.end(), output.begin(), output.end());
        lifter.normal_samples = plan.NormalSamples(lifter, flat);
    }
    MaterialRecoveryResult result;
    result.Graph = plan.Settings;
    result.Graph["format"] = 2;
    result.Graph["outputs"] = Json::MakeObject();
    std::set<std::string> pins;
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (bound.Pins[i].empty() || !pins.insert(bound.Pins[i]).second)
            throw Refused("invalid native bound output name", "material bound outputs");
        if (values[i].empty() || values[i].size() > 4)
            throw Refused("native family rewrite produced an invalid vector width", "material rewrite outputs");
        result.Graph["outputs"][bound.Pins[i]] = lifter.lift(values[i]);
        result.RecoveredPins.push_back(bound.Pins[i]);
    }
    result.Graph["nodes"] = lifter.nodes;
    result.Validation = ValidateGraph(result.Graph);
    result.TraceStop = plan.Stop;
    result.AssumedEngineFields = std::move(bound.AssumedEngineFields);
    result.CanonicalUnitFields = std::move(bound.CanonicalUnitFields);
    return result;
}
inline MaterialRecoveryResult RecoverMaterialExpressionGraph(const MaterialRecoveryPlan& plan)
{
    return LiftMaterialBoundOutputs(plan, BindMaterialOutputs(plan));
}
}
} // P4MapKit::Material

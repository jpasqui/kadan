// Depth-pass opacity mask boundary and exact float32 cooked clip comparison.
// This reconstructs the held inputs; complementary shader proof is separate.
#pragma once
#include "material_decompile.h"
namespace P4MapKit
{
namespace Material
{
using MaterialImportMemo = std::map<const Expression*, Value>;
inline Value MaterialImportAtom(const Value& value, const MaterialImportMemo& memo, std::size_t depth = 0)
{
    if (depth > 128)
        throw Refused("cross-program material tuple nesting limit", "material expression import depth");
    if (symbolic(value))
        return memo.at(value.expression.get());
    if (value.kind != Value::Tuple)
        return value;
    std::vector<Value> items;
    for (const auto& item : value.tuple)
        items.push_back(MaterialImportAtom(item, memo, depth + 1));
    return Value::tuple_value(items);
}
inline Value ImportMaterialValue(SymbolicArena& arena, const Value& value, MaterialImportMemo& memo)
{
    std::vector<Value> pending, roots = {value};
    while (!roots.empty())
    {
        auto child = roots.back();
        roots.pop_back();
        if (symbolic(child))
            pending.push_back(child);
        else if (child.kind == Value::Tuple)
            roots.insert(roots.end(), child.tuple.begin(), child.tuple.end());
    }
    while (!pending.empty())
    {
        const auto current = pending.back();
        const auto* key = current.expression.get();
        if (memo.count(key))
        {
            pending.pop_back();
            continue;
        }
        bool need = false;
        std::vector<Value> args = current.expression->args;
        while (!args.empty())
        {
            auto child = args.back();
            args.pop_back();
            if (symbolic(child) && !memo.count(child.expression.get()))
            {
                pending.push_back(child);
                need = true;
                break;
            }
            if (child.kind == Value::Tuple)
                args.insert(args.end(), child.tuple.begin(), child.tuple.end());
        }
        if (need)
        {
            if (pending.size() > 1000000)
                throw Refused("cross-program material expression budget exceeded", "material expression import budget");
            continue;
        }
        std::vector<Value> imported;
        for (const auto& child : current.expression->args)
            imported.push_back(MaterialImportAtom(child, memo));
        memo[key] = arena.mk(current.expression->op, imported);
        pending.pop_back();
    }
    return MaterialImportAtom(value, memo);
}
struct MaterialDepthMaskInputs
{
    MaterialBoundOutputs Bound;
    double Clip = 0;
};
inline MaterialDepthMaskInputs RecoverMaterialDepthMaskInputs(MaterialRecoveryPlan plan, double cookedClip)
{
    if (!std::isfinite(cookedClip))
        throw Refused("cooked opacity mask clip is not finite", "material depth clip");
    std::vector<AnchorInstruction> code;
    try
    {
        code = ParseMaterialAnchorInstructions(plan.Lines);
    }
    catch (const EvalError& e)
    {
        throw Refused(e.what(), "material anchor scan");
    }
    MaterialMaskAnchor mask;
    if (!FindMaterialMaskAnchor(code, mask))
        throw Refused("the depth pass has no native opacity mask anchor", "material depth mask anchor");
    const auto component = [](const std::string& comps)
    {
        if (comps.size() != 1 || std::string("xyzw").find(comps[0]) == std::string::npos)
            throw Refused("depth mask anchor does not select one component", "material depth component");
        return int(std::string("xyzw").find(comps[0]));
    };
    plan.Stop = mask.line;
    plan.Outputs = {{"OpacityMask", mask.mask.first, {component(mask.mask.second)}}};
    if (!mask.constant_clip)
        plan.Outputs.push_back({"__Clip", mask.clip.first, {component(mask.clip.second)}});
    auto bound = BindMaterialOutputs(plan);
    double clip = mask.clip_value;
    if (!mask.constant_clip)
    {
        const auto& value = bound.Values.back()[0];
        if (!IsNumber(value))
            throw Refused("an opacity mask clip value that is not a number", "material depth clip");
        clip = value.number();
        bound.Values.pop_back();
        bound.Pins.pop_back();
    }
    if (!std::isfinite(clip))
        throw Refused("shader opacity mask clip is not finite", "material depth clip");
    float actual = float(clip), expected = float(cookedClip);
    std::uint32_t actualBits, expectedBits;
    std::memcpy(&actualBits, &actual, 4);
    std::memcpy(&expectedBits, &expected, 4);
    if (!std::isfinite(actual) || !std::isfinite(expected) || actualBits != expectedBits)
        throw Refused("cooked opacity mask clip disagrees with the depth shader",
                      "settings disagree: opacity mask clip value");
    MaterialDepthMaskInputs result;
    result.Bound = std::move(bound);
    result.Clip = clip;
    return result;
}
}
}

// Exact held clearcoat controls and independent octahedral bottom-normal
// boundary from material_shading.py. These recognizers are not compiler proof.
#pragma once
#include "material_decompile.h"
#include "material_normal.h"
#include "material_rewrite.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialClearCoatControl
{
    std::string Register;
    char Component = 'x';
    Value Formula;
};
using MaterialClearCoatControls = std::map<std::string, MaterialClearCoatControl>;
inline MaterialClearCoatControls FindMaterialClearCoatControls(const std::vector<AnchorInstruction>& code,
                                                               const SymbolicMachine& machine, Binder& binder,
                                                               std::size_t stop)
{
    if (stop > code.size())
        throw Refused("clearcoat stop is outside the program", "clearcoat trace range");
    std::vector<std::string> registers;
    std::set<std::string> seen;
    for (std::size_t at = 0; at < std::min(stop, code.size()); ++at)
        if (!code[at].dst.empty() && code[at].dst[0] == 'R' && machine.reg.count(code[at].dst) &&
            seen.insert(code[at].dst).second)
            registers.push_back(code[at].dst);
    for (const auto& reg : machine.reg)
        if (!reg.first.empty() && reg.first[0] == 'R' && seen.insert(reg.first).second)
            registers.push_back(reg.first);
    MaterialClearCoatControls result;
    for (const auto& name : registers)
    {
        const auto& values = machine.reg.at(name);
        bool tuple = false;
        for (const auto& value : values)
            tuple |= value.kind == Value::Tuple;
        if (tuple)
            continue;
        for (int c = 0; c < 4; ++c)
        {
            if (!symbolic(values[c]))
                continue;
            Value formula;
            try
            {
                formula = binder.bind(values[c]);
            }
            catch (const EvalError&)
            {
                continue;
            }
            catch (const std::out_of_range&)
            {
                continue;
            }
            for (const auto& pin : std::vector<std::pair<std::string, std::string>>{
                     {"ClearCoat", "ClearCoat_Scale"}, {"ClearCoatRoughness", "ClearCoatRoughness_Scale"}})
            {
                if (result.count(pin.first))
                    continue;
                const auto wanted = binder.arena.op("SAT", {source(binder.arena, "ScalarParameter", pin.second)});
                if (same(formula, wanted) && NormalDetail::first_read(code, name, std::string("xyzw")[c], stop))
                    result[pin.first] = {name, std::string("xyzw")[c], formula};
            }
        }
    }
    if (result.size() != 2)
        throw Refused("clearcoat controls are not independently held", "clearcoat held controls");
    return result;
}
inline std::size_t FindMaterialClearCoatDeltaEncoder(const std::vector<AnchorInstruction>& code,
                                                     const MaterialAnchor& normal)
{
    if (normal.line >= code.size())
        throw Refused("clearcoat normal anchor is outside the program", "clearcoat normal anchor");
    std::vector<std::size_t> encoders;
    for (std::size_t at = 0; at < code.size(); ++at)
    {
        const auto& line = code[at];
        if (line.op != "MAD" || line.mask.size() != 2 || line.src.size() != 3)
            continue;
        const auto scale = line.src[1].scalar(), bias = line.src[2].scalar();
        if (!line.src[1].literal || !line.src[2].literal || !std::isfinite(scale) || !std::isfinite(bias) ||
            std::abs(scale - .5) >= 1e-7 || std::abs(bias - 128. / 255.) >= 1e-7 || normal.line >= at)
            continue;
        bool delta = false;
        for (std::size_t before = normal.line + 1; before < at; ++before)
            if (code[before].op == "ADD" && code[before].dst == line.src[0].base && code[before].mask.size() == 2 &&
                std::any_of(code[before].src.begin(), code[before].src.end(),
                            [](const AnchorOperand& source) { return source.negative; }))
                delta = true;
        if (delta)
            encoders.push_back(at);
    }
    if (encoders.size() != 1)
        throw Refused("clearcoat bottom normal lacks one checked octahedral delta encoder",
                      "clearcoat bottom normal encoder");
    return encoders[0];
}
struct MaterialClearCoatBottomNormal
{
    MaterialBoundOutputs Bound;
    MaterialAnchor Anchor;
    std::size_t Encoder = 0;
    bool FlatNormal = false;
};
inline MaterialClearCoatBottomNormal RecoverMaterialClearCoatBottomNormal(MaterialRecoveryPlan plan,
                                                                          const MaterialAnchors& found)
{
    const auto normal = found.find("NormalWorld");
    if (normal == found.end())
        throw Refused("clearcoat bottom normal has no independent normalization anchor", "clearcoat normal anchor");
    std::vector<AnchorInstruction> code;
    try
    {
        code = ParseMaterialAnchorInstructions(plan.Lines);
    }
    catch (const EvalError& e)
    {
        throw Refused(e.what(), "material anchor scan");
    }
    MaterialClearCoatBottomNormal result;
    result.Anchor = normal->second;
    result.Encoder = FindMaterialClearCoatDeltaEncoder(code, result.Anchor);
    MaterialOutputRegister output;
    output.Pin = "ClearCoatBottomNormal";
    output.Register = result.Anchor.register_name;
    for (const auto component : result.Anchor.comps)
    {
        const auto at = std::string("xyzw").find(component);
        if (at == std::string::npos)
            throw Refused("invalid clearcoat normal component", "clearcoat normal anchor");
        output.Components.push_back(int(at));
    }
    plan.Stop = result.Anchor.line + 1;
    plan.Outputs = {output};
    auto bound = BindMaterialOutputs(plan);
    const auto world = frame_transforms(*bound.Arena, bound.Values[0]);
    const auto material = MaterialNormal(*bound.Arena, world);
    if (!material.tangent_space)
        throw Refused("clearcoat bottom normal is not recovered in the material tangent space",
                      "clearcoat bottom normal space");
    result.FlatNormal = !material.has_pin;
    bound.Values[0] = material.has_pin ? material.pin : std::vector<Value>{Value(0.), Value(0.), Value(1.)};
    result.Bound = std::move(bound);
    return result;
}
}
}

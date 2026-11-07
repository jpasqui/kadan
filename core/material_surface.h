// Native anchored surface recovery. This stage consumes source-checked policy
// anchors; selecting and proving every cooked policy remains a separate stage.
#pragma once
#include "material_decompile.h"
#include "material_normal.h"
#include "material_anchor_lighting.h"
#include "material_rewrite.h"
#include "material_depth.h"
#include "material_coat_pipeline.h"
#include "material_source_emission.h"
#include "material_source_schedule_pipeline.h"
#include "material_fully_rough.h"

namespace P4MapKit
{
namespace Material
{
struct MaterialSurfaceRewrites
{
    // Exact scene and sun recognizers run on all pins and world normal together.
    std::function<std::vector<Value>(SymbolicArena&, const std::vector<Value>&)> SceneAndSun;
};
// Values at the material stop which reach an output or discard. A conditional
// write does not kill a value, and loop-carried values are propagated to a
// fixed point (the same bounded liveness pass as material_decompile._live_at).
inline MaterialComponents MaterialSurfaceLiveAt(const std::vector<AnchorInstruction>& code, std::size_t stop)
{
    if (stop > code.size())
        throw Refused("invalid material liveness boundary", "material trace range");
    MaterialComponents outputs, live;
    std::map<std::size_t, MaterialComponents> tops;
    for (const auto& line : code)
        if (starts(line.dst, "result"))
            for (char c : std::string("xyzw"))
                outputs.insert({line.dst, c});
    static const std::regex condition("(?:NE|EQ|GT|GE|LT|LE)(?:\\.([xyzw]+))?"),
        kill("^KIL\\S*\\s+-?(\\w+)(?:\\.([xyzw]+))?$");
    for (int pass = 0; pass < 8; ++pass)
    {
        const auto previous = live;
        const auto oldTops = tops;
        live = outputs;
        int depth = 0;
        std::vector<std::size_t> ends;
        for (std::size_t at = code.size(); at > stop;)
        {
            const auto i = --at;
            const auto& line = code[i];
            if (IsMember(line.op, {"ENDIF", "ENDREP"}))
            {
                ++depth;
                if (line.op == "ENDREP")
                {
                    ends.push_back(i);
                    const auto found = tops.find(i);
                    if (found != tops.end())
                        live.insert(found->second.begin(), found->second.end());
                }
                continue;
            }
            if (IsMember(line.op, {"IF", "REP"}))
            {
                --depth;
                if (line.op == "REP" && !ends.empty())
                {
                    tops[ends.back()] = live;
                    ends.pop_back();
                }
                std::smatch m;
                if (line.op == "IF" && std::regex_search(line.text, m, condition))
                    live.insert({"CC", m[1].matched ? m[1].str()[0] : 'x'});
                const auto reads = MaterialInstructionReads(line, line.mask);
                live.insert(reads.begin(), reads.end());
                continue;
            }
            if (IsMember(line.op, {"ELSE", "BRK", "RET"}))
                continue;
            if (line.op == "KIL")
            {
                std::smatch m;
                if (std::regex_search(line.text, m, condition))
                    for (char c : m[1].matched ? m[1].str() : "xyzw")
                        live.insert({"CC", c});
                if (std::regex_match(line.text, m, kill))
                    for (char c : m[2].matched ? m[2].str() : "xyzw")
                        live.insert({m[1].str(), c});
                continue;
            }
            MaterialComponents writes;
            for (char c : line.mask)
            {
                if (!line.dst.empty())
                    writes.insert({line.dst, c});
                if (line.has("CC"))
                    writes.insert({"CC", c});
            }
            bool needed = line.dst.empty() || IsMember(line.op, {"CAL", "RET", "LOADIM", "STOREIM", "LDB", "STB",
                                                                 "ATOM", "ATOMIM", "ATOMB"});
            for (const auto& write : writes)
                needed = needed || live.count(write) != 0;
            if (!needed)
                continue;
            if (depth == 0 && line.text.substr(0, line.text.find(',')).find('(') == std::string::npos)
                for (const auto& write : writes)
                    live.erase(write);
            const auto reads = MaterialInstructionReads(line, line.mask);
            live.insert(reads.begin(), reads.end());
        }
        if (live == previous && tops == oldTops)
            break;
    }
    return live;
}
// The game's own lighting can consume AO without the stock AO anchor. Recover
// only one distinct live saturated texture channel that no selected material
// input already holds; two possible channels leave the pin unrecovered.
inline bool AppendUnclaimedMaterialAmbientOcclusion(const MaterialRecoveryPlan& plan, MaterialBoundOutputs& bound,
                                                    const MaterialOriginalSourceAnchors* original = nullptr)
{
    if (plan.Settings.At("shading_model").Text() == "MSM_Unlit" ||
        std::find(bound.Pins.begin(), bound.Pins.end(), "AmbientOcclusion") != bound.Pins.end())
        return false;
    auto& arena = *bound.Arena;
    SymbolicMachine trace(plan.Lines, bound.Arena, MaterialPlanArrayBounds(plan, arena));
    try
    {
        trace.run(plan.Stop);
    }
    catch (const EvalError& e)
    {
        throw Refused(e.what(), "material symbolic trace");
    }
    auto binding = plan.Binding;
    if (plan.ConfigureBinding)
        plan.ConfigureBinding(arena, binding);
    Binder binder(arena, binding);
    std::set<const Expression*> known;
    const auto remember = [&](const Value& raw)
    {
        const auto value = BindMaterialComponent(binder, raw);
        if (symbolic(value))
            known.insert(value.expression.get());
    };
    for (const auto& output : plan.Outputs)
        if (!output.HasConstant)
        {
            const auto reg = trace.reg.find(output.Register);
            if (reg != trace.reg.end())
                for (int c : output.Components)
                    remember(reg->second.at(std::size_t(c)));
        }
    if (original)
        for (const auto& item : original->Scheduled)
            for (const auto& value : ReadScheduledMaterialValue(plan.Lines, item.second, trace))
                remember(value);
    // Emission and coat controls can be bound independently of held outputs.
    for (const auto& row : bound.Values)
        for (const auto& value : row)
            if (symbolic(value))
                known.insert(value.expression.get());
    const auto live = MaterialSurfaceLiveAt(ParseMaterialAnchorInstructions(plan.Lines), plan.Stop);
    std::map<const Expression*, Value> candidates;
    for (const auto& reg : trace.reg)
        for (std::size_t c = 0; c < 4; ++c)
        {
            if (!live.count({reg.first, "xyzw"[c]}))
                continue;
            const auto& raw = reg.second[c];
            if (!IsExpr(raw, "SAT") || Args(raw).size() != 1 || !IsExpr(Args(raw)[0], "COMP") ||
                Args(Args(raw)[0]).empty() || !IsExpr(Args(Args(raw)[0])[0], "TEX"))
                continue;
            const auto value = BindMaterialComponent(binder, raw);
            if (!IsExpr(value, "SAT") || known.count(value.expression.get()) || Args(value).size() != 1 ||
                !IsExpr(Args(value)[0], "COMP") || Args(Args(value)[0]).empty() ||
                !IsExpr(Args(Args(value)[0])[0], "SAMPLE"))
                continue;
            candidates.emplace(value.expression.get(), value);
        }
    if (candidates.size() != 1)
        return false;
    // UE clamps this pin itself, as for the source-anchored surface inputs.
    bound.Pins.push_back("AmbientOcclusion");
    bound.Values.push_back({Args(candidates.begin()->second)[0]});
    bound.AssumedEngineFields.insert(binder.assumed.begin(), binder.assumed.end());
    bound.CanonicalUnitFields.insert(binder.canonical_unit_fields.begin(), binder.canonical_unit_fields.end());
    return true;
}
inline MaterialRecoveryResult LiftMaterialSurfaceInputs(MaterialRecoveryPlan plan, MaterialBoundOutputs bound,
                                                        const std::vector<Value>& inputWorld,
                                                        const MaterialSurfaceRewrites& policy = {})
{
    if (plan.Rewrite)
        throw Refused("surface rewrites must include the held world normal through SceneAndSun",
                      "material surface rewrite order");
    if (!bound.Arena || inputWorld.size() != 3 || bound.Pins.size() != bound.Values.size())
        throw Refused("invalid bound native surface inputs", "material surface inputs");
    auto& arena = *bound.Arena;
    std::vector<Value> flat;
    for (const auto& values : bound.Values)
        flat.insert(flat.end(), values.begin(), values.end());
    const auto outputCount = flat.size();
    flat.insert(flat.end(), inputWorld.begin(), inputWorld.end());
    if (policy.SceneAndSun)
        flat = policy.SceneAndSun(arena, flat);
    if (flat.size() != outputCount + 3)
        throw Refused("scene recovery changed the native surface width", "material surface rewrite width");
    flat = frame_transforms(arena, FacingSigns(arena, flat));
    std::vector<Value> world(flat.end() - 3, flat.end());
    const auto normal = MaterialNormal(arena, world);
    RewriteMemo memo;
    std::map<const Expression*, Value> pixel;
    for (int c = 0; c < 3; ++c)
    {
        if (!symbolic(world[c]))
            throw Refused("a world normal that is not a normalized vector");
        pixel[world[c].expression.get()] = source(arena, "PixelNormal", Value::null(), c);
    }
    const auto replace = [&](const Value& value)
    {
        if (symbolic(value))
        {
            const auto found = pixel.find(value.expression.get());
            if (found != pixel.end())
                return found->second;
        }
        return value;
    };
    std::size_t at = 0;
    for (auto& values : bound.Values)
        for (auto& value : values)
            value = rewrite(arena, flat.at(at++), replace, memo);
    if (normal.has_pin)
    {
        bound.Pins.push_back("Normal");
        bound.Values.push_back(normal.pin);
    }
    plan.Settings["tangent_space_normal"] = normal.tangent_space;
    if (!plan.NormalSamples)
        plan.NormalSamples = normal_sample_ids;
    return LiftMaterialBoundOutputs(plan, std::move(bound));
}
inline MaterialRecoveryResult
RecoverAnchoredSurfaceExpressionGraph(const MaterialRecoveryPlan& input, const MaterialAnchors& policyAnchors,
                                      const MaterialSurfaceRewrites& rewrites = {},
                                      const MaterialDepthMaskInputs* depth = nullptr,
                                      const MaterialOriginalSourceAnchors* originalAnchors = nullptr)
{
    const auto model = input.Settings.At("shading_model").Text();
    if (!IsMember(model, {"MSM_DefaultLit", "MSM_Subsurface", "MSM_PreintegratedSkin", "MSM_TwoSidedFoliage",
                          "MSM_SubsurfaceProfile", "MSM_ClearCoat"}))
        throw Refused("this surface stage requires a separately recovered shading policy: " + model,
                      "material surface shading policy");
    if (input.Settings.At("domain").Text() != "MD_Surface")
        throw Refused("surface recovery requires MD_Surface", "material surface domain");
    const auto blend = input.Settings.At("blend_mode").Text();
    if (blend == "BLEND_Masked" && !depth)
        throw Refused("masked surface recovery requires a separate depth-pass clip proof",
                      "material surface depth proof");
    if (!IsMember(blend, {"BLEND_Opaque", "BLEND_Masked", "BLEND_Translucent", "BLEND_Additive"}))
        throw Refused("unproved native surface blend policy", "material surface blend policy");
    std::vector<AnchorInstruction> code;
    try
    {
        code = ParseMaterialAnchorInstructions(input.Lines);
    }
    catch (const EvalError& e)
    {
        throw Refused(e.what(), "material anchor scan");
    }
    if (input.Settings.At("two_sided").kind != Json::Boolean ||
        MaterialTwoSided(code) != input.Settings.At("two_sided").boolean)
        throw Refused("cooked two-sided setting disagrees with the lighting shader", "settings disagree: two-sided");
    MaterialAnchors found;
    try
    {
        found = originalAnchors
                    ? originalAnchors->Surface
                    : FindMaterialLightingAnchors(
                          code, ForwardMaterialWorldNormal(code, CorrectMaterialAmbientOcclusion(code, policyAnchors)),
                          blend);
    }
    catch (const EvalError& e)
    {
        throw Refused(e.what(), "translucent lighting policy inputs");
    }
    found = ApplyOriginalMaterialFullyRoughAnchors(code, std::move(found), input.Settings);
    if (found.count("SubsurfaceColor") &&
        !IsMember(model, {"MSM_Subsurface", "MSM_PreintegratedSkin", "MSM_TwoSidedFoliage", "MSM_SubsurfaceProfile"}))
        throw Refused("a subsurface input disagrees with the cooked shading model", "settings disagree: shading model");
    std::vector<std::string> pins;
    for (const auto* name : {"BaseColor", "Metallic", "Specular", "Roughness", "AmbientOcclusion", "SubsurfaceColor"})
        if (found.count(name))
            pins.push_back(name);
    if (IsMember(blend, {"BLEND_Translucent", "BLEND_Additive"}) && found.count("Opacity"))
        pins.push_back("Opacity");
    // Emission is included only when a native policy has established its anchor.
    if (found.count("EmissiveColor"))
        pins.push_back("EmissiveColor");
    MaterialRecoveryPlan plan;
    MaterialBoundOutputs bound;
    bool held = false;
    std::vector<std::string> heldPins;
    for (const auto& pin : pins)
        if (!originalAnchors || !originalAnchors->Scheduled.count(pin))
            heldPins.push_back(pin);
    for (const auto* name : {"Normal", "NormalDecal", "NormalWorld"})
        if (!held && found.count(name))
        {
            auto selection = heldPins;
            if (!originalAnchors || !originalAnchors->Scheduled.count(name))
                selection.push_back(name);
            try
            {
                plan = PlanHeldMaterialOutputs(input, found, selection);
                held = true;
                if (originalAnchors && !originalAnchors->Scheduled.empty())
                {
                    auto all = pins;
                    all.push_back(name);
                    bound = BindOriginalMaterialSourceOutputs(input, *originalAnchors, all);
                }
                break;
            }
            catch (const Refused& e)
            {
                if (e.name != "a value whose register is written before it is read")
                    throw;
            }
        }
    if (!held)
        throw Refused("the world normal is not held at the native material stop", "material surface held normal");
    if (!bound.Arena)
        bound = BindMaterialOutputs(plan);
    const auto world = bound.Values.back();
    bound.Values.pop_back();
    bound.Pins.pop_back();
    // The original engine adds material emission after the lighting stop. Its
    // independently checked consumer and dependency slice recover those values
    // without tracing the unrelated lighting tail or guessing absent color.
    if (std::find(bound.Pins.begin(), bound.Pins.end(), "EmissiveColor") == bound.Pins.end())
        bound = AppendScheduledMaterialEmission(std::move(bound), RecoverOriginalMaterialEmissionInputs(plan));
    if (model == "MSM_ClearCoat")
    {
        const auto controls = RecoverMaterialClearCoatControlInputs(plan);
        const auto bottom = RecoverMaterialClearCoatBottomNormal(input, policyAnchors);
        bound = CombineMaterialClearCoatInputs(std::move(bound), controls, bottom);
    }
    if (!found.count("AmbientOcclusion"))
        AppendUnclaimedMaterialAmbientOcclusion(plan, bound, originalAnchors);
    if (depth)
    {
        if (blend != "BLEND_Masked" || depth->Bound.Values.size() != 1 || depth->Bound.Values[0].size() != 1)
            throw Refused("a depth mask input disagrees with the surface policy", "material surface depth proof");
        float cookedClip = float(plan.Settings.At("clip").Numeric()), depthClip = float(depth->Clip);
        std::uint32_t cookedBits, depthBits;
        std::memcpy(&cookedBits, &cookedClip, 4);
        std::memcpy(&depthBits, &depthClip, 4);
        if (!std::isfinite(cookedClip) || !std::isfinite(depthClip) || cookedBits != depthBits)
            throw Refused("cooked opacity mask clip disagrees with the depth shader",
                          "settings disagree: opacity mask clip value");
        MaterialImportMemo memo;
        bound.Pins.push_back("OpacityMask");
        bound.Values.push_back({ImportMaterialValue(*bound.Arena, depth->Bound.Values[0][0], memo)});
        plan.Settings["clip"] = depth->Clip;
        for (const auto& fact : depth->Bound.AssumedEngineFields)
        {
            const auto known = bound.AssumedEngineFields.find(fact.first);
            if (known != bound.AssumedEngineFields.end() && known->second != fact.second)
                throw Refused("depth and base engine assumptions disagree", "material surface depth inputs");
            bound.AssumedEngineFields.insert(fact);
        }
        bound.CanonicalUnitFields.insert(depth->Bound.CanonicalUnitFields.begin(),
                                         depth->Bound.CanonicalUnitFields.end());
    }
    return LiftMaterialSurfaceInputs(plan, std::move(bound), world, rewrites);
}
}
} // P4MapKit::Material

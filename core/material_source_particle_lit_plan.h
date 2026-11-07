// Native material prefixes retain every independently captured pin for the
// particle/local default proof. Engine clamps are removed only afterward.
#pragma once
#include "material_source_particle_lit_anchors.h"
#include "material_source_particle_lit_defaults.h"
#include "material_source_particle_owner.h"
#include "material_source_particle_lit_bind.h"
#include "material_source_particle_lit_trace.h"
#include "material_source_particle_lit_reflection.h"
#include "material_source_particle_lit_depth.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialOriginalParticleLitProgram
{
    MaterialDomainPreparedProgram Source;
    MaterialOriginalParticleSourcePlan Plan;
    MaterialOriginalParticleLitAnchors Anchors;
    MaterialOriginalParticleLitDepth Depth;
};
inline MaterialOriginalParticleLitProgram
PrepareOriginalParticleLitProgram(const MaterialOriginalOwnerSource& owner, const FMaterialShaderReference& ref,
                                  const MaterialOriginalArchiveAccess& archive, MaterialDomainSourceScope scope,
                                  const std::function<void(SymbolicArena&, BindingContext&)>& engineBinding = {})
{
    MaterialOriginalParticleLitProgram result;
    result.Source = PrepareOriginalMaterialArchiveProgram(owner, ref, archive, scope);
    MaterialParticleArchive access;
    access.MapIndices = archive.MapIndices;
    access.Payload = archive.Payload;
    access.Frequency = archive.Frequency;
    const auto fixed = owner.Fixed;
    const auto runtime = PrepareOriginalMaterialRuntimeTextures(owner.User, owner.Map, result.Source.Program);
    const auto binding = [fixed, runtime, engineBinding](SymbolicArena& arena, BindingContext& context)
    {
        ApplyOriginalMaterialFixedTextures(context, fixed);
        InstallOriginalMaterialRuntimeTextures(arena, context, runtime);
        if (engineBinding)
            engineBinding(arena, context);
    };
    result.Plan = PlanOriginalParticlePixelRecovery(owner.Map, result.Source.Program, access, owner.User.Settings,
                                                    OriginalMaterialGraphDefaults(owner), binding);
    const auto previous = result.Plan.Source.Recovery.ConfigureBinding;
    const auto reference = result.Source.Program.Reference;
    const auto header = result.Source.Program.Header;
    const auto settings = owner.User.Settings;
    result.Plan.Source.Recovery.ConfigureBinding =
        [previous, reference, header, settings](SymbolicArena& arena, BindingContext& context)
    {
        if (previous)
            previous(arena, context);
        InstallOriginalParticleLitViewRows(arena, context, reference, header);
        InstallOriginalParticleLitEyeAdaptation(arena, context, reference, header, settings);
        InstallOriginalParticleLitSceneSamples(arena, context, reference, header, settings);
        InstallOriginalParticleLitReflection(arena, context, reference, header, settings);
    };
    result.Anchors = PrepareOriginalParticleLitAnchors(result.Source.Program, result.Plan);
    result.Depth = CaptureOriginalParticleLitDepth(owner, ref, archive, scope, engineBinding);
    return result;
}
inline MaterialOriginalParticleLitProgram
SelectOriginalParticleLitProgram(const MaterialOriginalOwnerSource& owner, std::uint64_t factory,
                                 const MaterialOriginalArchiveAccess& archive, MaterialDomainSourceScope scope,
                                 const std::function<void(SymbolicArena&, BindingContext&)>& binding = {})
{
    // material_lighting.policy_refs groups aliases by factory/resource index,
    // then sorts those keys. The first actual normal-bearing policy wins.
    const auto refs = ShaderPolicyIndex(owner.References).FirstByResource(factory);
    std::unique_ptr<MaterialOriginalParticleLitProgram> fallback;
    const bool foliage = owner.User.Cooked.ShadingModel == "MSM_TwoSidedFoliage";
    std::pair<int, int> best{-1, -1};
    for (const auto& item : refs)
    {
        auto candidate = PrepareOriginalParticleLitProgram(owner, item.second, archive, scope, binding);
        const auto& found = candidate.Anchors.Source.Surface;
        if (!found.count("stop"))
            continue;
        const bool normal = found.count("Normal") || found.count("NormalWorld") || found.count("NormalDecal");
        if (foliage)
        {
            if (MaterialTwoSided(ParseMaterialAnchorInstructions(candidate.Plan.Source.Recovery.Lines)) !=
                owner.User.Settings.At("two_sided").boolean)
                continue;
            const std::pair<int, int> key{int(found.count("SubsurfaceColor") != 0), int(normal)};
            if (key > best)
            {
                best = key;
                fallback.reset(new MaterialOriginalParticleLitProgram(std::move(candidate)));
            }
        }
        else
        {
            if (normal)
                return candidate;
            if (!fallback)
                fallback.reset(new MaterialOriginalParticleLitProgram(std::move(candidate)));
        }
    }
    if (fallback)
        return *fallback;
    return PrepareOriginalParticleLitProgram(owner, SelectOriginalParticleFactoryProgram(owner, factory), archive,
                                             scope, binding);
}
inline MaterialBoundOutputs CaptureOriginalParticleLitPins(const MaterialOriginalParticleLitProgram& program,
                                                           const std::vector<std::string>* selected = nullptr)
{
    const auto& source = program.Anchors.Source;
    const auto& input = program.Plan.Source.Recovery;
    const auto marker = source.Surface.find("stop");
    if (marker == source.Surface.end())
        throw Refused("lit dynamic defaults lack a material boundary", "particle dynamic defaults");
    std::vector<std::string> pins, held;
    for (const auto* name : {"BaseColor", "Metallic", "Specular", "Roughness", "AmbientOcclusion", "SubsurfaceColor",
                             "Opacity", "Normal", "NormalWorld", "NormalDecal"})
        if (source.Surface.count(name) &&
            (!selected || std::find(selected->begin(), selected->end(), name) != selected->end()))
        {
            pins.push_back(name);
            if (!source.Scheduled.count(name) && !program.Anchors.Early.count(name))
                held.push_back(name);
        }
    if (pins.empty())
        throw Refused("lit dynamic defaults lack native material pins", "particle dynamic defaults");
    auto plan = PlanHeldMaterialOutputs(input, source.Surface, held);
    for (auto& output : plan.Outputs)
        output.RemoveEngineClamp = false;
    auto arena = std::make_shared<SymbolicArena>();
    OriginalParticleLitSymbolicMachine prefix(plan.Lines, arena, MaterialPlanArrayBounds(plan, *arena));
    try
    {
        prefix.run(plan.Stop);
    }
    catch (const EvalError& error)
    {
        throw Refused(error.what(), "material symbolic trace");
    }
    auto context = plan.Binding;
    if (plan.ConfigureBinding)
        plan.ConfigureBinding(*arena, context);
    Binder binder(*arena, context);
    std::map<std::string, MaterialOutputRegister> selectors;
    for (const auto& selector : plan.Outputs)
        selectors.emplace(selector.Pin, selector);
    MaterialBoundOutputs result;
    result.Arena = arena;
    result.Pins = pins;
    result.KillCount = prefix.kills.size() + prefix.symbolic_kills->size();
    for (const auto& pin : pins)
    {
        const auto& anchor = source.Surface.at(pin);
        std::vector<Value> raw;
        if (anchor.has_constant)
            raw = {Value(anchor.constant)};
        else if (program.Anchors.Early.count(pin))
        {
            const auto& snapshot = program.Anchors.Early.at(pin);
            OriginalParticleLitSymbolicMachine early(plan.Lines, arena, MaterialPlanArrayBounds(plan, *arena));
            try
            {
                early.run(snapshot.ScheduledLine);
            }
            catch (const EvalError& error)
            {
                throw Refused(error.what(), "material particle early normal trace");
            }
            const auto reg = early.reg.find(anchor.register_name);
            if (reg == early.reg.end())
                throw Refused("early particle normal register is absent", "material particle early normal");
            for (const auto c : anchor.comps)
                raw.push_back(reg->second.at(std::string("xyzw").find(c)));
        }
        else if (source.Scheduled.count(pin))
        {
            try
            {
                raw = ReadOriginalParticleLitScheduledValue(source.Scheduled.at(pin), prefix);
            }
            catch (const EvalError& error)
            {
                throw Refused(error.what(), "material scheduled trace");
            }
        }
        else
        {
            const auto& selector = selectors.at(pin);
            const auto reg = prefix.reg.find(selector.Register);
            if (reg == prefix.reg.end())
                throw Refused("lit native material register is absent", "material output register");
            for (const auto component : selector.Components)
                raw.push_back(reg->second.at(std::size_t(component)));
        }
        for (auto& value : raw)
        {
            value = BindMaterialComponent(binder, value);
            if (anchor.saturate)
                value = arena->op("SAT", {value});
        }
        result.Values.push_back(std::move(raw));
    }
    result.AssumedEngineFields = binder.assumed;
    result.CanonicalUnitFields = binder.canonical_unit_fields;
    if (!selected && std::find(result.Pins.begin(), result.Pins.end(), "EmissiveColor") == result.Pins.end())
        result =
            AppendScheduledMaterialEmission(std::move(result), RecoverOriginalParticleLitEmissionInputs(plan, prefix));
    if (!selected)
        result = AppendOriginalParticleLitDepth(std::move(result), program.Depth);
    return result;
}
inline MaterialParticlePinFormulas OriginalParticleLitFormalPins(SymbolicArena& arena,
                                                                 const MaterialBoundOutputs& bound,
                                                                 const MaterialPreparedProgram& program,
                                                                 const Json& settings)
{
    if (!bound.Arena || bound.Pins.size() != bound.Values.size())
        throw Refused("lit particle captured pin packet is invalid", "particle dynamic defaults");
    std::unique_ptr<MaterialOriginalSceneResourcePolicy> policy;
    MaterialImportMemo imports;
    MaterialParticlePinFormulas result;
    for (std::size_t i = 0; i < bound.Pins.size(); ++i)
    {
        std::vector<Value> row;
        for (const auto& value : bound.Values[i])
            row.push_back(ImportMaterialValue(arena, value, imports));
        result[bound.Pins[i]] = rewrite(
            arena, row,
            [&](const Value& value)
            {
                if (!is(value, "SCENETEX"))
                    return value;
                if (!policy)
                    policy.reset(new MaterialOriginalSceneResourcePolicy(ReadMaterialNativeSceneResourcePolicy(
                        program.Reference, program.Header, settings.At("domain").Text(),
                        settings.At("blend_mode").Text(), true)));
                const auto& roles = policy->ResourcePolicy;
                const auto depth = roles.Textures.find("scene_depth");
                if (depth == roles.Textures.end() || Args(value).empty() || !NumberIs(Args(value)[0], depth->second))
                    throw Refused("particle default scene lookup has no exact SceneDepth resource identity",
                                  "particle dynamic defaults scene contracts");
                if (!roles.Samplers.count("point"))
                    throw Refused("particle default SceneDepth lacks its checked point sampler",
                                  "particle dynamic defaults scene contracts");
                std::vector<Value> args = {Value("scene_depth")};
                args.insert(args.end(), Args(value).begin() + 1, Args(value).end());
                return arena.mk("PARTICLE_DEFAULT_RESOURCE", args);
            });
    }
    return result;
}
}
} // P4MapKit::Material

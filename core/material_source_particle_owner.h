// Automatic original particle owner recovery. Asset/archive identities select
// both native programs; symbolic defaults never establish authored values.
#pragma once
#include "material_source_owner.h"
#include "material_engine_hashes.h"
#include "material_source_particle_plan.h"
#include "material_source_unlit_scene.h"
#include "material_source_common.h"
namespace P4MapKit
{
namespace Material
{
inline const std::vector<std::uint64_t>& OriginalParticleFactoryOrder()
{
    static const std::vector<std::uint64_t> order = {0x6d728306d56d412full, 0x1adefad780cd16c5ull,
                                                     0x00080c76633abf12ull, 0x2f7fb09897dda57cull};
    return order;
}
inline std::vector<std::uint64_t> OriginalParticleOwnerFactories(const MaterialOriginalOwnerSource& owner)
{
    std::vector<std::uint64_t> result;
    for (const auto factory : OriginalParticleFactoryOrder())
        for (const auto& ref : owner.References)
            if (ref.VertexFactoryHash == factory)
            {
                result.push_back(factory);
                break;
            }
    return result;
}
inline FMaterialShaderReference SelectOriginalParticleFactoryProgram(const MaterialOriginalOwnerSource& owner,
                                                                     std::uint64_t factory)
{
    const bool translucent =
        owner.User.Cooked.BlendMode == "BLEND_Translucent" && owner.User.Cooked.ShadingModel != "MSM_Unlit";
    if (factory == EngineHash::LocalVertexFactory && !translucent)
    {
        const auto selected = SelectOriginalMaterialPrimaryProgram(owner);
        if (selected.VertexFactoryHash != factory)
            throw Refused("joint particle graph has no native local preview", "particle joint graph");
        return selected;
    }
    std::vector<FMaterialShaderReference> refs;
    const ShaderPolicyIndex policies(owner.References);
    for (const auto& policy : policies.BasePass())
        if (policy.Reference.VertexFactoryHash == factory && (translucent || !policy.Skylight))
            refs.push_back(policy.Reference);
    if (refs.empty())
        throw Refused("no base pass pixel shader for " + MaterialFactoryName(factory), "no base pass pixel shader");
    if (translucent)
        std::stable_sort(refs.begin(), refs.end(),
                         [](const FMaterialShaderReference& a, const FMaterialShaderReference& b)
                         {
                             const auto rank = [](const FMaterialShaderReference& ref)
                             {
                                 const auto name = MaterialOriginalSourceShaderName(ref.TypeHash);
                                 return std::make_tuple(name.find("LightMapPolicyHQ") == std::string::npos,
                                                        IsSkylightShaderName(name),
                                                        name.find("NoLightMap") == std::string::npos, name);
                             };
                             return rank(a) < rank(b);
                         });
    else
        for (const auto& ref : refs)
            if (ref.TypeHash == 0x4507f03a199c6526ull)
                return CanonicalOriginalMaterialReference(owner, ref);
    return CanonicalOriginalMaterialReference(owner, refs.front());
}
struct MaterialOriginalParticleColor
{
    std::vector<Value> Color;
    std::size_t KillCount = 0;
    std::map<std::pair<std::string, int>, std::string> AssumedEngineFields;
    std::set<std::pair<std::string, int>> CanonicalUnitFields;
};
inline std::shared_ptr<MaterialOriginalUnlitSceneFacts>
ConfigureOriginalParticleNoFogProjection(MaterialRecoveryPlan& plan, const MaterialPreparedProgram& program)
{
    const auto prior = plan.ConfigureBinding;
    const auto settings = plan.Settings;
    const auto reference = program.Reference;
    const auto header = program.Header;
    const auto facts = std::make_shared<MaterialOriginalUnlitSceneFacts>();
    const auto fogFacts = std::make_shared<MaterialUnlitNoFogProjectionFacts>();
    plan.ConfigureBinding =
        [prior, settings, reference, header, facts, fogFacts](SymbolicArena& arena, BindingContext& context)
    {
        if (prior)
            prior(arena, context);
        InstallOriginalUnlitSceneComponents(arena, context, reference, header, settings, true, facts);
        const auto previous = context.LeafOverride;
        const auto checked = std::make_shared<std::function<bool(const Value&, Value&)>>();
        context.LeafOverride =
            [previous, checked, settings, reference, header, fogFacts](const Value& value, Value& out)
        {
            bool fog = false;
            if (is(value, "IN") && Args(value).size() == 3)
            {
                const auto& args = Args(value);
                if (args[0].kind == Value::Text && args[0].text == "buffer" && IsNumber(args[1]) && IsNumber(args[2]) &&
                    std::isfinite(args[1].number()) && args[1].number() >= 0 &&
                    args[1].number() < double(header.layout_hashes.size()) &&
                    std::floor(args[1].number()) == args[1].number() && NumberIs(args[2], 1836))
                {
                    const auto hash = header.layout_hashes[std::size_t(args[1].number())];
                    fog = hash == 0x0d480502 || hash == 0x05ec0dee;
                }
            }
            if (!fog)
                return previous && previous(value, out);
            if (!*checked)
            {
                const auto projection = PrepareMaterialUnlitNoFogProjection(reference, header, settings, true);
                BindingContext qualified;
                qualified.LeafOverride = previous;
                InstallMaterialUnlitNoFogProjection(qualified, projection, fogFacts);
                *checked = qualified.LeafOverride;
            }
            return (*checked)(value, out);
        };
    };
    return facts;
}
inline MaterialOriginalParticleColor BindOriginalParticleOwnerColor(const std::shared_ptr<SymbolicArena>& arena,
                                                                    const MaterialRecoveryPlan& plan)
{
    if (!arena || plan.Lines.empty() || plan.Lines.size() > 100000 || plan.Stop != plan.Lines.size())
        throw Refused("particle native output requires the bounded complete source program",
                      "material particle native output");
    SymbolicMachine machine(plan.Lines, arena, MaterialPlanArrayBounds(plan, *arena));
    try
    {
        machine.run(plan.Stop);
    }
    catch (const EvalError& error)
    {
        throw Refused(error.what(), "material particle symbolic trace");
    }
    MaterialOriginalParticleColor result;
    result.KillCount = machine.kills.size() + machine.symbolic_kills->size();
    if (result.KillCount)
        throw Refused("an unlit material that discards pixels (not recovered yet)");
    const auto color = machine.reg.find("result_color0");
    if (color == machine.reg.end())
        throw Refused("joint particle graph has no native output", "particle joint graph");
    auto binding = plan.Binding;
    if (plan.ConfigureBinding)
        plan.ConfigureBinding(*arena, binding);
    Binder binder(*arena, binding);
    for (const auto& value : color->second)
        result.Color.push_back(BindMaterialComponent(binder, value));
    result.AssumedEngineFields = binder.assumed;
    result.CanonicalUnitFields = binder.canonical_unit_fields;
    return result;
}
struct MaterialOriginalParticleOwnerRecovery : MaterialEvidence
{
    MaterialOriginalOwnerRecovery Recovery;
    Json ParticleFacts = Json::MakeObject();
};
inline MaterialOriginalParticleOwnerRecovery RecoverOriginalParticleOwnerExpressionGraph(
    const MaterialOriginalOwnerSource& owner, const MaterialOriginalArchiveAccess& archive,
    MaterialDomainSourceScope scope = MaterialDomainSourceScope::QualifiedGlobalTail,
    const std::function<void(SymbolicArena&, BindingContext&)>& engineBinding = {},
    const MaterialSurfaceRewrites& rewrites = {})
{
    const auto factories = OriginalParticleOwnerFactories(owner);
    if (factories.empty())
        throw Refused("material has no checked particle factory", "material particle source family");
    // The immutable dispatcher gives authored, foliage, lit-default and
    // subsurface producers their own policy/default proofs before _recover.
    if (owner.User.Cooked.Domain != "MD_Surface" || owner.User.Cooked.ShadingModel != "MSM_Unlit")
        throw Refused("particle shading family requires its independently qualified source producer",
                      "material particle specialized source family");
    if (!IsMember(owner.User.Cooked.BlendMode, {"BLEND_Opaque", "BLEND_Translucent", "BLEND_Additive"}))
        throw Refused("unlit particle blend mode requires its separate source contract",
                      "unlit blend mode " + owner.User.Cooked.BlendMode);
    MaterialOriginalParticleOwnerRecovery result;
    const auto primary = SelectOriginalParticleFactoryProgram(owner, factories.front()),
               local = SelectOriginalParticleFactoryProgram(owner, EngineHash::LocalVertexFactory);
    result.Recovery.Primary.Source = PrepareOriginalMaterialArchiveProgram(owner, primary, archive, scope);
    auto preview = PrepareOriginalMaterialArchiveProgram(owner, local, archive, scope);
    MaterialParticleArchive access;
    access.MapIndices = archive.MapIndices;
    access.Payload = archive.Payload;
    access.Frequency = archive.Frequency;
    const auto fixed = owner.Fixed;
    const auto binding = [fixed, engineBinding](SymbolicArena& arena, BindingContext& context)
    {
        ApplyOriginalMaterialFixedTextures(context, fixed);
        if (engineBinding)
            engineBinding(arena, context);
    };
    const auto defaults = OriginalMaterialGraphDefaults(owner);
    auto particlePlan = PlanOriginalParticlePixelRecovery(owner.Map, result.Recovery.Primary.Source.Program, access,
                                                          owner.User.Settings, defaults, binding);
    auto localPlan =
        PlanOriginalParticlePixelRecovery(owner.Map, preview.Program, access, owner.User.Settings, defaults, binding);
    const auto particleEngine = ConfigureOriginalParticleNoFogProjection(particlePlan.Source.Recovery,
                                                                         result.Recovery.Primary.Source.Program),
               previewEngine = ConfigureOriginalParticleNoFogProjection(localPlan.Source.Recovery, preview.Program);
    auto arena = std::make_shared<SymbolicArena>();
    auto bound = BindOriginalParticleOwnerColor(arena, particlePlan.Source.Recovery);
    const auto previewBound = BindOriginalParticleOwnerColor(arena, localPlan.Source.Recovery);
    const auto joint =
        ResolveOriginalParticleFormulas(*arena, bound.Color, previewBound.Color, particlePlan.Inputs.Factory);
    const auto& color = joint.Color;
    bool allowNegative = false;
    if (owner.User.Settings.Has("render_settings") &&
        owner.User.Settings.At("render_settings").Has("bAllowNegativeEmissiveColor"))
    {
        const auto& field = owner.User.Settings.At("render_settings").At("bAllowNegativeEmissiveColor");
        RequireKind(field, Json::Boolean, "bAllowNegativeEmissiveColor");
        allowNegative = field.boolean;
    }
    MaterialBoundOutputs outputs;
    outputs.Arena = arena;
    outputs.Pins = {"EmissiveColor"};
    std::vector<Value> emissive;
    for (int c = 0; c < 3; ++c)
    {
        const auto& value = color.at(std::size_t(c));
        const bool clamped = IsExpr(value, "MAX") && Args(value).size() == 2 && NumberIs(Args(value)[1], 0);
        if (!allowNegative && !clamped && symbolic(value) && !MaterialNonnegative(value))
            throw Refused("an unlit color that is not max(EmissiveColor, 0)");
        emissive.push_back(clamped ? Args(value)[0] : value);
    }
    outputs.Values.push_back(emissive);
    const bool alpha = !NumberIs(color.at(3), 0);
    if (alpha != (owner.User.Cooked.BlendMode == "BLEND_Translucent"))
        throw Refused("unlit alpha output disagrees with the cooked blend mode", "settings disagree: blend mode");
    if (alpha)
    {
        outputs.Pins.push_back("Opacity");
        outputs.Values.push_back({IsExpr(color[3], "SAT") ? Args(color[3])[0] : color[3]});
    }
    outputs.AssumedEngineFields = bound.AssumedEngineFields;
    for (const auto& field : previewBound.AssumedEngineFields)
    {
        const auto prior = outputs.AssumedEngineFields.find(field.first);
        if (prior != outputs.AssumedEngineFields.end() && prior->second != field.second)
            throw Refused("particle and local engine assumptions disagree", "particle joint graph");
        outputs.AssumedEngineFields.insert(field);
    }
    outputs.CanonicalUnitFields = bound.CanonicalUnitFields;
    outputs.CanonicalUnitFields.insert(previewBound.CanonicalUnitFields.begin(),
                                       previewBound.CanonicalUnitFields.end());
    auto plan = particlePlan.Source.Recovery;
    plan.Defaults["particle_dynamic_defaults"] = joint.Defaults;
    plan.Settings["tangent_space_normal"] = true;
    plan.NormalSamples = normal_sample_ids;
    const auto scene =
        PrepareOriginalMaterialSceneRewrites(result.Recovery.Primary.Source.Program, owner.User.Settings, rewrites);
    plan.Rewrite = [scene](SymbolicArena& a, std::vector<std::vector<Value>>& values)
    {
        std::vector<Value> flat;
        for (const auto& row : values)
            flat.insert(flat.end(), row.begin(), row.end());
        const auto width = flat.size();
        if (scene.SceneAndSun)
            flat = scene.SceneAndSun(a, flat);
        if (flat.size() != width)
            throw Refused("particle source rewrite changed output width", "material surface rewrite width");
        std::size_t at = 0;
        for (auto& row : values)
            for (auto& value : row)
                value = flat.at(at++);
    };
    result.Recovery.Primary.Material = LiftMaterialBoundOutputs(plan, std::move(outputs));
    result.Recovery.Primary.OriginalLines = particlePlan.Source.OriginalLines;
    result.Recovery.Primary.Storage = particlePlan.Source.StorageDeclarations;
    auto available = Json::MakeArray(), candidates = Json::MakeArray(), unaccounted = Json::MakeArray(),
         engine = Json::MakeObject();
    for (const auto& role : particleEngine->ConsumedRoles)
        engine["particle"][role.first] = role.second;
    for (const auto& role : previewEngine->ConsumedRoles)
        engine["preview"][role.first] = role.second;
    for (const auto factory : factories)
        available.array.push_back(MaterialFactoryName(factory));
    for (const auto& candidate : joint.EquivalentCandidates)
    {
        auto row = Json::MakeArray();
        for (const auto value : candidate)
            row.array.push_back(value);
        candidates.array.push_back(row);
    }
    for (const auto& fact : joint.Unaccounted)
        unaccounted.array.push_back(fact);
    unaccounted.array.push_back("particle vertex stages are not recovered");
    result.ParticleFacts = Object({{"particle_factories", available},
                                   {"particle_dynamic_defaults", joint.Defaults},
                                   {"equivalent_candidates", candidates},
                                   {"vertex_color_restored", joint.VertexColorRestored},
                                   {"unaccounted_values", unaccounted},
                                   {"subuv", particlePlan.Inputs.SubUV},
                                   {"gpu_dynamic_zero", particlePlan.Inputs.GpuDynamicZero},
                                   {"preview_resource_index", local.ResourceIndex},
                                   {"engine_resources", engine}});
    return result;
}
}
} // P4MapKit::Material

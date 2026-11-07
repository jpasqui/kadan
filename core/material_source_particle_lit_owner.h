// DefaultLit particle source workflow: actual normal-bearing native policy,
// independent local pin comparison, then the existing surface expression lift.
#pragma once
#include "material_source_particle_lit_plan.h"
#include "material_engine_hashes.h"
namespace P4MapKit
{
namespace Material
{
inline void ValidateOriginalParticleLitSourceSettings(const MaterialOriginalParticleLitProgram& program,
                                                      const Json& settings, bool retainSubsurface = false)
{
    if (settings.At("two_sided").kind != Json::Boolean ||
        MaterialTwoSided(ParseMaterialAnchorInstructions(program.Plan.Source.Recovery.Lines)) !=
            settings.At("two_sided").boolean)
        throw Refused("cooked two-sided setting disagrees with the lighting shader", "settings disagree: two-sided");
    if ((settings.At("blend_mode").Text() == "BLEND_Masked") != program.Depth.Found)
        throw Refused("cooked mask blend disagrees with the actual factory depth program",
                      "settings disagree: blend mode");
    if (!retainSubsurface && program.Anchors.Source.Surface.count("SubsurfaceColor"))
        throw Refused("a subsurface input disagrees with the cooked DefaultLit source family",
                      "settings disagree: shading model");
}
inline MaterialOriginalParticleOwnerRecovery RecoverOriginalParticleLitOwnerExpressionGraph(
    const MaterialOriginalOwnerSource& owner, const MaterialOriginalArchiveAccess& archive,
    MaterialDomainSourceScope scope = MaterialDomainSourceScope::QualifiedGlobalTail,
    const std::function<void(SymbolicArena&, BindingContext&)>& engineBinding = {},
    const MaterialSurfaceRewrites& rewrites = {}, const std::string& sourceModel = "MSM_DefaultLit")
{
    const auto factories = OriginalParticleOwnerFactories(owner);
    if (factories.empty() || owner.User.Cooked.Domain != "MD_Surface" ||
        owner.User.Cooked.ShadingModel != sourceModel ||
        !IsMember(sourceModel, {"MSM_DefaultLit", "MSM_Subsurface", "MSM_TwoSidedFoliage"}))
        throw Refused("particle source route requires the exact checked lit surface model",
                      "material particle specialized source family");
    if (!IsMember(owner.User.Cooked.BlendMode, {"BLEND_Opaque", "BLEND_Masked", "BLEND_Translucent", "BLEND_Additive"}))
        throw Refused("particle additive inputs require their independently qualified source producer",
                      "material particle additive source family");
    if (sourceModel == "MSM_Subsurface" &&
        !IsMember(owner.User.Cooked.BlendMode, {"BLEND_Opaque", "BLEND_Masked", "BLEND_Translucent"}))
        throw Refused("subsurface particles require their original checked blend policy",
                      "material particle subsurface source family");
    auto selected = SelectOriginalParticleLitProgram(owner, factories.front(), archive, scope, engineBinding);
    ValidateOriginalParticleLitSourceSettings(selected, owner.User.Settings, sourceModel != "MSM_DefaultLit");
    auto bound = CaptureOriginalParticleLitPins(selected);
    auto& arena = *bound.Arena;
    const auto formal = OriginalParticleLitFormalPins(arena, bound, selected.Source.Program, owner.User.Settings);
    std::vector<Value> all;
    for (const auto& pin : formal)
        all.insert(all.end(), pin.second.begin(), pin.second.end());
    MaterialParticleLitDefaults dynamic;
    std::map<const Expression*, Value> replacements;
    const auto sceneProgram = [&](const MaterialOriginalParticleLitProgram& source)
    {
        auto program = source.Source.Program;
        program.Lines = source.Plan.Source.Recovery.Lines;
        return program;
    };
    const auto scene = PrepareOriginalMaterialSceneRewrites(sceneProgram(selected), owner.User.Settings, rewrites);
    if (IsMember(selected.Plan.Inputs.Factory,
                 {"FParticleSpriteVertexFactory", "FGPUSpriteVertexFactory", "FParticleBeamTrailVertexFactory"}) &&
        formal.count("BaseColor") && !ParticleSourceDetail::Contains(formal.at("BaseColor"), "VertexColor") &&
        !ParticleSourceDetail::Contains(formal.at("BaseColor"), "ParticleDynamicParameter"))
    {
        auto local =
            SelectOriginalParticleLitProgram(owner, EngineHash::LocalVertexFactory, archive, scope, engineBinding);
        const std::vector<std::string> baseOnly = {"BaseColor"};
        const auto localBound = CaptureOriginalParticleLitPins(local, &baseOnly);
        MaterialImportMemo imports;
        std::vector<Value> preview;
        for (const auto& value : localBound.Values.at(0))
            preview.push_back(ImportMaterialValue(arena, value, imports));
        preview = PixelOwn(arena, preview);
        const auto localScene =
            PrepareOriginalMaterialSceneRewrites(sceneProgram(local), owner.User.Settings, rewrites);
        if (localScene.SceneAndSun)
            preview = localScene.SceneAndSun(arena, preview);
        preview = PixelDepth(arena, preview);
        if (ParticleSourceDetail::Contains(preview, "VertexColor"))
        {
            std::size_t index = 0;
            while (index < bound.Pins.size() && bound.Pins[index] != "BaseColor")
                ++index;
            if (index == bound.Pins.size())
                throw Refused("joint lit color lacks native BaseColor", "particle joint graph");
            auto particle = PixelOwn(arena, bound.Values[index]);
            if (scene.SceneAndSun)
                particle = scene.SceneAndSun(arena, particle);
            particle = PixelDepth(arena, particle);
            particle.push_back(0.);
            preview.push_back(0.);
            const auto joint = ResolveOriginalParticleFormulas(arena, particle, preview, selected.Plan.Inputs.Factory);
            if (joint.VertexColorRestored)
                for (std::size_t c = 0; c < 3; ++c)
                    if (symbolic(bound.Values[index][c]))
                        replacements[bound.Values[index][c].expression.get()] = joint.Color[c];
        }
    }
    if (ParticleSourceDetail::Contains(all, "ParticleDynamicParameter"))
    {
        const FMaterialShaderReference* sameType = nullptr;
        for (const auto& ref : owner.References)
            if (ref.Frequency == 3 && ref.VertexFactoryHash == EngineHash::LocalVertexFactory &&
                ref.TypeHash == selected.Source.Program.Reference.TypeHash)
            {
                sameType = &ref;
                break;
            }
        const auto local = sameType
                               ? PrepareOriginalParticleLitProgram(owner, *sameType, archive, scope, engineBinding)
                               : PrepareOriginalParticleLitProgram(
                                     owner, SelectOriginalParticleFactoryProgram(owner, EngineHash::LocalVertexFactory),
                                     archive, scope, engineBinding);
        const auto preview = CaptureOriginalParticleLitPins(local);
        const auto localFormal =
            OriginalParticleLitFormalPins(arena, preview, local.Source.Program, owner.User.Settings);
        dynamic = ResolveOriginalParticleLitDefaults(arena, formal, localFormal, selected.Plan.Inputs.Factory);
        if (sourceModel == "MSM_Subsurface" &&
            dynamic.Defaults.Dump() != Object({{"0", Array({1., 1., 1., 1.})}}).Dump())
            throw Refused(
                "constructor white dynamic defaults were not independently established for subsurface particles",
                "particle dynamic defaults");
        for (std::size_t i = 0; i < bound.Pins.size(); ++i)
        {
            const auto& row = formal.at(bound.Pins[i]);
            for (std::size_t c = 0; c < row.size(); ++c)
                if (symbolic(row[c]))
                {
                    const auto replacement = dynamic.Replacements.find(row[c].expression.get());
                    if (replacement != dynamic.Replacements.end())
                    {
                        const auto native =
                            rewrite(arena, std::vector<Value>{replacement->second},
                                    [&](const Value& value)
                                    {
                                        if (!is(value, "PARTICLE_DEFAULT_RESOURCE"))
                                            return value;
                                        if (Args(value).empty() || Args(value)[0].kind != Value::Text ||
                                            Args(value)[0].text != "scene_depth")
                                            throw Refused("unknown joint scene resource", "particle joint graph");
                                        const auto policy = ReadMaterialNativeSceneResourcePolicy(
                                            selected.Source.Program.Reference, selected.Source.Program.Header,
                                            owner.User.Cooked.Domain, owner.User.Cooked.BlendMode, true);
                                        const auto depth = policy.ResourcePolicy.Textures.find("scene_depth");
                                        if (depth == policy.ResourcePolicy.Textures.end())
                                            throw Refused("joint scene depth has no actual original slot",
                                                          "particle joint graph");
                                        std::vector<Value> args = {Value(depth->second)};
                                        args.insert(args.end(), Args(value).begin() + 1, Args(value).end());
                                        return arena.mk("SCENETEX", args);
                                    })
                                .front();
                        if (symbolic(bound.Values[i][c]))
                            replacements[bound.Values[i][c].expression.get()] = native;
                    }
                }
        }
    }
    bool vertexRestored = false;
    for (const auto& row : bound.Values)
        for (const auto& value : row)
            if (symbolic(value))
            {
                const auto replacement = replacements.find(value.expression.get());
                if (replacement != replacements.end() && !ParticleSourceDetail::Contains({value}, "VertexColor") &&
                    ParticleSourceDetail::Contains({replacement->second}, "VertexColor"))
                    vertexRestored = true;
            }
    if (!replacements.empty())
        for (auto& row : bound.Values)
            row = rewrite(arena, row,
                          [&](const Value& value)
                          {
                              const auto found = replacements.find(value.expression.get());
                              return found == replacements.end() ? value : found->second;
                          });
    std::vector<Value> world;
    for (const auto* normal : {"Normal", "NormalDecal", "NormalWorld"})
    {
        const auto at = std::find(bound.Pins.begin(), bound.Pins.end(), normal);
        if (at != bound.Pins.end())
        {
            world = bound.Values[std::size_t(at - bound.Pins.begin())];
            break;
        }
    }
    if (world.size() != 3)
        throw Refused("the world normal is not held at the native material stop", "material surface held normal");
    MaterialBoundOutputs material;
    material.Arena = bound.Arena;
    material.AssumedEngineFields = bound.AssumedEngineFields;
    material.CanonicalUnitFields = bound.CanonicalUnitFields;
    material.KillCount = bound.KillCount;
    for (std::size_t i = 0; i < bound.Pins.size(); ++i)
    {
        const auto& pin = bound.Pins[i];
        if (!IsMember(pin, {"BaseColor", "Metallic", "Specular", "Roughness", "AmbientOcclusion", "SubsurfaceColor",
                            "Opacity", "EmissiveColor", "OpacityMask"}))
            continue;
        if (pin == "OpacityMask" && owner.User.Cooked.BlendMode != "BLEND_Masked")
            continue;
        if (pin == "Opacity" && !IsMember(owner.User.Cooked.BlendMode, {"BLEND_Translucent", "BLEND_Additive"}))
            continue;
        auto values = bound.Values[i];
        if (pin != "EmissiveColor" && pin != "OpacityMask" &&
            std::all_of(values.begin(), values.end(), [](const Value& value) { return is(value, "SAT"); }))
            for (auto& value : values)
                value = Args(value)[0];
        material.Pins.push_back(pin);
        material.Values.push_back(std::move(values));
    }
    auto plan = selected.Plan.Source.Recovery;
    plan.Stop = selected.Anchors.Source.Surface.at("stop").line;
    plan.Defaults["particle_dynamic_defaults"] = dynamic.Defaults;
    const MaterialSurfaceRewrites particleScene{[scene](SymbolicArena& a, const std::vector<Value>& values)
                                                {
                                                    auto out = PixelOwn(a, values);
                                                    if (scene.SceneAndSun)
                                                        out = scene.SceneAndSun(a, out);
                                                    return PixelDepth(a, out);
                                                }};
    if (owner.User.Cooked.BlendMode == "BLEND_Masked")
        plan.Settings["clip"] =
            ValidateOriginalParticleLitDepthClip(selected.Depth, owner.User.Settings.At("clip").Numeric());
    MaterialOriginalParticleOwnerRecovery result;
    result.Recovery.Primary.Material = LiftMaterialSurfaceInputs(plan, std::move(material), world, particleScene);
    result.Recovery.Primary.Source = selected.Source;
    result.Recovery.Primary.OriginalLines = selected.Plan.Source.OriginalLines;
    result.Recovery.Primary.Storage = selected.Plan.Source.StorageDeclarations;
    auto available = Json::MakeArray(), unaccounted = Json::MakeArray();
    for (const auto factory : factories)
        available.array.push_back(MaterialFactoryName(factory));
    for (const auto& fact : dynamic.Unaccounted)
        unaccounted.array.push_back(fact);
    unaccounted.array.push_back("particle vertex stages are not recovered");
    result.ParticleFacts = Object({{"particle_factories", available},
                                   {"particle_dynamic_defaults", dynamic.Defaults},
                                   {"vertex_color_restored", vertexRestored},
                                   {"unaccounted_values", unaccounted},
                                   {"subuv", selected.Plan.Inputs.SubUV},
                                   {"gpu_dynamic_zero", selected.Plan.Inputs.GpuDynamicZero}});
    return result;
}
}
} // P4MapKit::Material

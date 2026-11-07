// The ordered producer table of one owner's original-source recovery (RecoverOriginalMaterialSourceExpressionGraph,
// material_source_pipeline.h). Each rule reads the owner's cooked settings, its shader map, its programs or the
// recovered graph; none names a game asset (material_source_gates_test checks the data rules against the names they
// replaced).
#pragma once
#include "material_source_producer.h"
#include "material_source_subsurface.h"
#include "material_source_complements.h"
#include "material_source_particle_owner.h"
#include "material_source_particle_lit_owner.h"
#include "material_source_black.h"
#include "material_source_shadow_canonical.h"
#include "material_source_particle_reflection_owner.h"
#include "material_source_shadow_dispatch.h"
#include "material_source_anisotropy.h"
#include "material_source_sky_contract.h"
#include "material_source_post_settings.h"
#include "material_positive_infinity.h"
#include "material_source_folded_lighting.h"
#include "material_source_ice_reflection.h"
#include "material_source_normal_policy.h"
namespace P4MapKit
{
namespace Material
{
// What one owner's recovery reads, and the result and family facts its steps build.
struct MaterialSourceDispatch
{
    const MaterialOriginalOwnerSource& Owner;
    const MaterialOriginalArchiveAccess& Archive;
    MaterialDomainSourceScope Scope;
    const std::function<void(SymbolicArena&, BindingContext&)>& Binding;
    FMaterialShaderReference Ordinary;
    MaterialSurfaceRewrites Rewrites;
    MaterialOriginalOwnerRecovery Result;
    Json FamilyFacts;
};
inline bool MaterialSourceParticleSurface(const MaterialSourceDispatch& d, const char* shadingModel)
{
    return d.Owner.User.Cooked.Domain == "MD_Surface" && d.Owner.User.Cooked.ShadingModel == shadingModel &&
           !OriginalParticleOwnerFactories(d.Owner).empty();
}
inline void TakeMaterialSourceParticle(MaterialSourceDispatch& d, MaterialOriginalParticleOwnerRecovery particle,
                                       const char* producer)
{
    d.Result = std::move(particle.Recovery);
    d.FamilyFacts = std::move(particle.ParticleFacts);
    d.FamilyFacts["source_producer"] = producer;
}
inline void RunOriginalSkyDiskRewrites(MaterialSourceDispatch& d)
{
    const auto sky = PrepareOriginalSkyDiskContract(d.Owner, d.Archive);
    const auto facts = std::make_shared<MaterialSkyDiskResourceFacts>(sky.ResourceFacts());
    const auto original = sky.Rewrites(facts).SceneAndSun, additional = d.Rewrites.SceneAndSun;
    d.Rewrites.SceneAndSun = [original, additional](SymbolicArena& arena, const std::vector<Value>& values)
    {
        auto result = original(arena, values);
        return additional ? additional(arena, result) : result;
    };
    d.FamilyFacts["sky_disk_contract"] = Object({{"view_buffer", facts->ViewBuffer},
                                                 {"texture_slot", facts->TextureSlot},
                                                 {"sampler_slot", facts->SamplerSlot},
                                                 {"requires_independent_sky_proof", true},
                                                 {"renderer_proven", false}});
}
// The owner's own graph from its program of record, as every owner without a family of its own is recovered.
inline void RunOriginalOwnerGraphProducer(MaterialSourceDispatch& d)
{
    const auto primary = SelectOriginalMaterialNormalProgram(
        d.Owner, d.Archive, SelectOriginalMaterialSubsurfaceProgram(d.Owner, d.Archive, d.Ordinary, d.Scope), d.Scope);
    d.Result =
        RecoverOriginalMaterialOwnerExpressionGraph(d.Owner, d.Archive, d.Scope, d.Binding, d.Rewrites, &primary);
}
inline void RunOriginalFoliageParticleProducer(MaterialSourceDispatch& d)
{
    const auto& owner = d.Owner;
    auto source = owner;
    if (owner.User.Cooked.BlendMode == "BLEND_Additive")
    {
        // material_foliage_additive.settings retains the actual root renderer fields even when
        // the MIC overrides the blend to additive. Preserve any nearer original render overlay.
        FMaterialCookedSettings master;
        FReadError error;
        if (!ReadMaterialCookedSettings({owner.User.Chain.front()}, master, error))
            throw Refused(error.Code, "foliage additive cooked settings");
        const auto root = MaterialSourceSettings(master);
        Json render = root.Has("render_settings") ? root.At("render_settings") : Json::MakeObject();
        if (source.User.Settings.Has("render_settings"))
            for (const auto& field : source.User.Settings.At("render_settings").object)
                render[field.first] = field.second;
        source.User.Settings["render_settings"] = std::move(render);
    }
    TakeMaterialSourceParticle(d,
                               RecoverOriginalParticleLitOwnerExpressionGraph(source, d.Archive, d.Scope, d.Binding,
                                                                              d.Rewrites, "MSM_TwoSidedFoliage"),
                               "original-TwoSidedFoliage-particle");
}
inline void RunOriginalMaterialComplements(MaterialSourceDispatch& d)
{
    auto& result = d.Result;
    const auto shadowRoute = SelectOriginalMaterialShadowDitherRoute(d.Owner, d.Archive);
    const auto complementary =
        shadowRoute == MaterialOriginalShadowDitherRoute::None
            ? RecoverOriginalMaterialComplements(d.Owner, d.Archive, result.Primary.Material.Graph)
            : RecoverOriginalMaterialShadowDither(d.Owner, d.Archive, result.Primary.Material.Graph);
    result.Primary.Material.Graph = complementary.Graph;
    result.Primary.Material.Validation = ValidateOriginalComplementGraph(complementary.Graph);
    result.ComplementPrograms = complementary.Programs;
    result.ComplementFacts = complementary.Facts;
}
inline void RunOriginalAnisotropyComplement(MaterialSourceDispatch& d)
{
    auto& result = d.Result;
    const auto anisotropy = RecoverOriginalMaterialAnisotropy(d.Owner, d.Archive, result.Primary.Material.Graph);
    result.Primary.Material.Graph = anisotropy.Graph;
    result.Primary.Material.Validation = ValidateOriginalComplementGraph(anisotropy.Graph);
    result.ComplementFacts["anisotropy_contract"] = anisotropy.Facts;
    result.ComplementPrograms.insert(result.ComplementPrograms.end(), anisotropy.Programs.begin(),
                                     anisotropy.Programs.end());
}
inline const std::vector<MaterialSourceProducer<MaterialSourceDispatch>>& MaterialSourceProducers()
{
    using D = MaterialSourceDispatch;
    static const std::vector<MaterialSourceProducer<D>> table = {
        {"sky disk scene rewrites", MaterialSourceStage::Prepare,
         [](const D& d) { return OriginalSkyDiskOwner(d.Owner, d.Ordinary, d.Archive); }, RunOriginalSkyDiskRewrites},
        {"original-Black-fog-only", MaterialSourceStage::Produce, [](const D& d)
         { return OriginalBlackFogOwner(d.Owner, d.Ordinary, d.Archive, d.Scope); }, RunOriginalOwnerGraphProducer},
        {"original canonical Shadow", MaterialSourceStage::Produce,
         [](const D& d) { return OriginalShadowCanonicalOwner(d.Owner, d.Archive, d.Scope); },
         [](D& d)
         {
             d.Result = RecoverOriginalShadowCanonicalExpressionGraph(d.Owner, d.Archive, d.Scope);
             d.FamilyFacts = d.Result.ComplementFacts;
         }},
        // material_decompile.py dispatches these before every other surface family.
        {"original-folded-lighting", MaterialSourceStage::Produce,
         [](const D& d) { return OriginalFoldedLightingOwner(d.Owner, d.Archive, d.Scope, d.Binding); },
         [](D& d)
         {
             auto folded =
                 RecoverOriginalFoldedLightingOwnerExpressionGraph(d.Owner, d.Archive, d.Scope, d.Binding, d.Rewrites);
             d.Result = std::move(folded.Recovery);
             d.FamilyFacts = std::move(folded.ParticleFacts);
             d.FamilyFacts["source_producer"] = "original-folded-lighting";
         }},
        {"original-unlit-particle", MaterialSourceStage::Produce,
         [](const D& d) { return MaterialSourceParticleSurface(d, "MSM_Unlit"); },
         [](D& d)
         {
             TakeMaterialSourceParticle(
                 d, RecoverOriginalParticleOwnerExpressionGraph(d.Owner, d.Archive, d.Scope, d.Binding, d.Rewrites),
                 "original-unlit-particle");
         }},
        {"original-DefaultLit-particle authored reflection", MaterialSourceStage::Produce,
         [](const D& d)
         {
             return MaterialSourceParticleSurface(d, "MSM_DefaultLit") &&
                    OriginalParticleAuthoredReflectionEligible(d.Owner, d.Archive, d.Scope, d.Binding, d.Rewrites);
         },
         [](D& d)
         {
             TakeMaterialSourceParticle(d,
                                        RecoverOriginalParticleAuthoredReflectionOwnerExpressionGraph(
                                            d.Owner, d.Archive, d.Scope, d.Binding, d.Rewrites),
                                        "original-DefaultLit-particle");
         }},
        {"original-DefaultLit-particle", MaterialSourceStage::Produce,
         [](const D& d) { return MaterialSourceParticleSurface(d, "MSM_DefaultLit"); },
         [](D& d)
         {
             TakeMaterialSourceParticle(
                 d, RecoverOriginalParticleLitOwnerExpressionGraph(d.Owner, d.Archive, d.Scope, d.Binding, d.Rewrites),
                 "original-DefaultLit-particle");
         }},
        {"original-TwoSidedFoliage-particle", MaterialSourceStage::Produce, [](const D& d)
         { return MaterialSourceParticleSurface(d, "MSM_TwoSidedFoliage"); }, RunOriginalFoliageParticleProducer},
        {"original-Subsurface-particle", MaterialSourceStage::Produce,
         [](const D& d) { return MaterialSourceParticleSurface(d, "MSM_Subsurface"); },
         [](D& d)
         {
             TakeMaterialSourceParticle(d,
                                        RecoverOriginalParticleLitOwnerExpressionGraph(
                                            d.Owner, d.Archive, d.Scope, d.Binding, d.Rewrites, "MSM_Subsurface"),
                                        "original-Subsurface-particle");
         }},
        {"original owner graph", MaterialSourceStage::Produce, [](const D&) { return true; },
         RunOriginalOwnerGraphProducer},
        {"Ice reflection", MaterialSourceStage::Complete,
         [](const D& d)
         {
             // A graph that already chooses by ENABLE_SKY_LIGHT holds the sky side next to the magenta branch.
             const auto& graph = d.Result.Primary.Material.Graph;
             return !MaterialGraphHasSkyLightPolicy(graph) && !OriginalIceReflectionBranches(graph).empty();
         },
         [](D& d)
         {
             const auto parameter = OriginalIceReflectionBranches(d.Result.Primary.Material.Graph).front().Parameter;
             d.Result.Primary.Material.Graph = ApplyOriginalIceReflection(d.Result.Primary.Material.Graph);
             d.FamilyFacts["ice_reflection_contract"] = Object({{"parameter", parameter},
                                                                {"layer", 0.0},
                                                                {"mip", "native maximum mip - rcp(0)"},
                                                                {"shading_policy", "ENABLE_SKY_LIGHT"}});
         }},
        {"compiled mask coverage", MaterialSourceStage::Complete,
         [](const D& d) { return CertifyOriginalCompiledCoverage(d.Owner, d.Archive); },
         [](D& d)
         {
             d.FamilyFacts["compiled_mask_coverage"] = Object(
                 {{"predicate", true},
                  {"canonical_mask", 1.},
                  {"clip", d.Owner.User.Settings.At("clip")},
                  {"authored_mask",
                   "authored raw OpacityMask erased by optimization; only shipped compiled no-discard predicate recovered"},
                  {"scope",
                   "material mask coverage at shipped clip; original geometry and fixed render state preserved"},
                  {"complete", false}});
         }},
        {"positive infinity", MaterialSourceStage::Complete, [](const D&) { return true; }, [](D& d)
         { d.Result.Primary.Material.Graph = LowerMaterialPositiveInfinity(d.Result.Primary.Material.Graph); }},
        {"shadow dither or complements", MaterialSourceStage::Complete, [](const D&) { return true; },
         RunOriginalMaterialComplements},
        {"anisotropy", MaterialSourceStage::Complete,
         [](const D& d)
         {
             return OriginalAnisotropyFamily(d.Owner) &&
                    !d.Result.Primary.Material.Graph.At("outputs").Has("Anisotropy") &&
                    !d.Result.Primary.Material.Graph.At("outputs").Has("Tangent");
         },
         RunOriginalAnisotropyComplement},
        {"source family facts", MaterialSourceStage::Complete, [](const D& d) { return !d.FamilyFacts.object.empty(); },
         [](D& d) { d.Result.ComplementFacts["source_family"] = std::move(d.FamilyFacts); }},
        {"post process options", MaterialSourceStage::Complete,
         [](const D& d) { return d.Owner.User.Cooked.Domain == "MD_PostProcess"; },
         [](D& d)
         {
             auto& result = d.Result;
             result.Primary.Material.Graph =
                 RetainOriginalPostProcessSerializedOptions(d.Owner.User.Chain.front(), result.Primary.Material.Graph);
             result.Primary.Material.Validation = ValidateOriginalComplementGraph(result.Primary.Material.Graph);
             result.ComplementFacts["postprocess_optional_properties"] =
                 "exact serialized presence; original ARM64 constructor defaults unqualified";
         }},
    };
    return table;
}
}
} // P4MapKit::Material

// Automatic primary and complementary original-program orchestration.
// Graph recovery does not admit an original renderer pass or production program.
#pragma once
#include "material_source_types.h"
#include "material_masked_unlit.h"
#include "material_source_compiled_coverage.h"
#include "material_source_common.h"
namespace P4MapKit
{
namespace Material
{
inline FMaterialShaderReference SelectOriginalMaterialDepthProgram(const MaterialOriginalOwnerSource& owner,
                                                                   const FMaterialShaderReference& base)
{
    if (base.Frequency != 3 || !base.VertexFactoryHash)
        throw Refused("original depth complement requires a mesh pixel program", "material source depth program");
    for (const auto& ref : owner.References)
        if (ref.Frequency == 3 && ref.VertexFactoryHash == base.VertexFactoryHash &&
            ref.TypeHash == 0x925355faf735b81cull)
            return ref;
    throw Refused("original material has no source-qualified depth pixel program for its primary factory",
                  "material source depth program");
}
inline MaterialDomainPreparedProgram PrepareOriginalMaterialArchiveProgram(const MaterialOriginalOwnerSource& owner,
                                                                           const FMaterialShaderReference& reference,
                                                                           const MaterialOriginalArchiveAccess& archive,
                                                                           MaterialDomainSourceScope scope)
{
    RequireOriginalArchiveAccess(archive);
    const auto canonical = CanonicalOriginalMaterialReference(owner, reference);
    const auto shader = ArchiveProgram(owner, canonical, archive);
    auto prepared = PrepareOriginalMaterialDomainPixelProgram(owner.Map, canonical, shader.Payload, shader.Frequency,
                                                              owner.User.Settings, scope);
    prepared.Program.BaseBinding.Collections = owner.User.Collections;
    return prepared;
}
struct MaterialOriginalOwnerRecovery : MaterialEvidence
{
    MaterialOriginalProgramRecovery Primary;
    std::vector<MaterialDomainPreparedProgram> Complements;
    std::vector<MaterialPreparedProgram> ComplementPrograms;
    Json ComplementFacts = Json::MakeObject();
};
inline MaterialOriginalOwnerRecovery RecoverOriginalMaterialOwnerExpressionGraph(
    const MaterialOriginalOwnerSource& owner, const MaterialOriginalArchiveAccess& archive,
    MaterialDomainSourceScope scope = MaterialDomainSourceScope::QualifiedGlobalTail,
    const std::function<void(SymbolicArena&, BindingContext&)>& engineBinding = {},
    const MaterialSurfaceRewrites& rewrites = {}, const FMaterialShaderReference* selectedPrimary = nullptr)
{
    const auto primary = selectedPrimary ? CanonicalOriginalMaterialReference(owner, *selectedPrimary)
                                         : SelectOriginalMaterialPrimaryProgram(owner);
    MaterialOriginalOwnerRecovery result;
    if (owner.User.Cooked.Domain != "MD_Surface" || owner.User.Cooked.BlendMode != "BLEND_Masked")
    {
        result.Primary =
            RecoverOriginalMaterialSelectedProgram(owner, primary, archive, scope, engineBinding, rewrites);
        return result;
    }
    const auto depth = SelectOriginalMaterialDepthProgram(owner, primary);
    auto depthSource = PrepareOriginalMaterialArchiveProgram(owner, depth, archive, scope);
    const auto fixed = owner.Fixed;
    const auto depthRuntime = PrepareOriginalMaterialRuntimeTextures(owner.User, owner.Map, depthSource.Program);
    const auto depthBinding = [fixed, engineBinding, depthRuntime](SymbolicArena& arena, BindingContext& context)
    {
        ApplyOriginalMaterialFixedTextures(context, fixed);
        InstallOriginalMaterialRuntimeTextures(arena, context, depthRuntime);
        if (engineBinding)
            engineBinding(arena, context);
    };
    const auto depthPlan = PlanOriginalMaterialPixelRecovery(depthSource.Program, owner.User.Settings,
                                                             OriginalMaterialGraphDefaults(owner), depthBinding);
    result.Primary.Source = PrepareOriginalMaterialArchiveProgram(owner, primary, archive, scope);
    const auto baseRuntime =
        PrepareOriginalMaterialRuntimeTextures(owner.User, owner.Map, result.Primary.Source.Program);
    const auto baseProgram = result.Primary.Source.Program;
    const auto settings = owner.User.Settings;
    const auto baseBinding =
        [fixed, engineBinding, baseRuntime, baseProgram, settings](SymbolicArena& arena, BindingContext& context)
    {
        ApplyOriginalMaterialFixedTextures(context, fixed);
        InstallOriginalMaterialRuntimeTextures(arena, context, baseRuntime);
        InstallOriginalLitSurfaceEngineReads(arena, context, baseProgram, settings);
        if (engineBinding)
            engineBinding(arena, context);
    };
    auto basePlan = PlanOriginalMaterialPixelRecovery(result.Primary.Source.Program, owner.User.Settings,
                                                      OriginalMaterialGraphDefaults(owner), baseBinding);
    result.Primary.OriginalLines = basePlan.OriginalLines;
    result.Primary.Storage = basePlan.StorageDeclarations;
    const auto scene =
        PrepareOriginalMaterialSceneRewrites(result.Primary.Source.Program, owner.User.Settings, rewrites);
    if (owner.User.Cooked.ShadingModel == "MSM_Unlit")
    {
        basePlan.Recovery.Rewrite = [scene](SymbolicArena& arena, std::vector<std::vector<Value>>& values)
        {
            std::vector<Value> flat;
            for (const auto& value : values)
                flat.insert(flat.end(), value.begin(), value.end());
            const auto count = flat.size();
            if (scene.SceneAndSun)
                flat = scene.SceneAndSun(arena, flat);
            if (flat.size() != count)
                throw Refused("original masked unlit scene rewrite changed output width",
                              "material surface rewrite width");
            std::size_t at = 0;
            for (auto& value : values)
                for (auto& component : value)
                    component = flat.at(at++);
        };
        const auto projection = PrepareMaterialUnlitNoFogProjection(
            result.Primary.Source.Program.Reference, result.Primary.Source.Program.Header, owner.User.Settings, true);
        result.Primary.Material =
            RecoverMaskedUnlitMaterialExpressionGraph(basePlan.Recovery, depthPlan.Recovery, projection);
    }
    else
    {
        const auto mask =
            CertifyOriginalCompiledCoverage(owner, archive)
                ? OriginalCompiledCoverageInputs(depthPlan.Recovery)
                : RecoverMaterialDepthMaskInputs(depthPlan.Recovery, owner.User.Settings.At("clip").Numeric());
        MaterialOriginalSourceAnchors anchors;
        std::string failure;
        try
        {
            anchors = PrepareOriginalMaterialLateVectorAnchors(result.Primary.Source.Program, owner.User.Settings,
                                                               basePlan.Factory);
        }
        catch (const EvalError& error)
        {
            failure = error.what();
        }
        if (!failure.empty())
            throw Refused(failure, "material source anchor scan");
        result.Primary.Material =
            RecoverAnchoredSurfaceExpressionGraph(basePlan.Recovery, anchors.Generic, scene, &mask, &anchors);
    }
    result.Complements.push_back(std::move(depthSource));
    return result;
}
}
} // P4MapKit::Material

// An unlit program that reads View.RealTimeReflectionCapture, in an explicit capture-off input projection.
// A stock engine default is a compatibility observation, not proof of the
// original game's setter or runtime value. Full original capture code is kept.
#pragma once
#include "material_source_unlit_remaining_view.h"
#include "material_engine_hashes.h"
#include "material_source_common.h"
namespace P4MapKit
{
namespace Material
{
// View word 3440: RealTimeReflectionCapture, which the capture-off projection sets to 0.
constexpr int MaterialViewRealTimeReflectionCapture = 3440;
// An unlit opaque surface program with the game View at buffer 0 that reads View.RealTimeReflectionCapture.
inline bool OriginalCaptureReadingProgram(const MaterialPreparedProgram& program, const Json& settings)
{
    const auto buffer = program.BaseBinding.Buffers.find(0);
    const auto view = program.Reference.UniformBufferHashes.find(0);
    if (program.Reference.Frequency != 3 || buffer == program.BaseBinding.Buffers.end() || buffer->second != "View" ||
        program.Header.layout_hashes.empty() || program.Header.layout_hashes[0] != EngineHash::ViewLayout ||
        view == program.Reference.UniformBufferHashes.end() || view->second != EngineHash::ViewUniformBuffer ||
        settings.At("domain").Text() != "MD_Surface" || settings.At("blend_mode").Text() != "BLEND_Opaque" ||
        settings.At("shading_model").Text() != "MSM_Unlit")
        return false;
    const auto field = "buf0[" + std::to_string(MaterialViewRealTimeReflectionCapture) + "]";
    for (const auto& line : program.Lines)
        if (line.find(field) != std::string::npos)
            return true;
    return false;
}
struct MaterialOriginalCaveCaptureFacts : MaterialEvidence
{
    MaterialPreparedProgram Original;
    std::set<std::pair<int, int>> ProjectedFields;
    bool StockDefaultCompatibilityOnly = true;
};
inline MaterialOriginalCaveCaptureFacts PrepareOriginalCaveCaptureProjection(const MaterialPreparedProgram& program,
                                                                             const Json& settings,
                                                                             bool originalAssociationProved)
{
    const auto buffer = program.BaseBinding.Buffers.find(0);
    const auto view = program.Reference.UniformBufferHashes.find(0);
    if (!originalAssociationProved || program.Reference.Frequency != 3 || buffer == program.BaseBinding.Buffers.end() ||
        buffer->second != "View" || program.Header.layout_hashes.empty() ||
        program.Header.layout_hashes[0] != EngineHash::ViewLayout ||
        view == program.Reference.UniformBufferHashes.end() || view->second != EngineHash::ViewUniformBuffer)
        throw Refused("Cave sky capture projection lacks original View association",
                      "material Cave capture association");
    if (!OriginalCaptureReadingProgram(program, settings))
        throw Refused("capture projection needs an unlit opaque program that reads View.RealTimeReflectionCapture",
                      "material Cave capture contract");
    MaterialOriginalCaveCaptureFacts result;
    result.Original = program;
    return result;
}
inline MaterialRecoveryResult
RecoverOriginalCaveCaptureOffExpressionGraph(const MaterialRecoveryPlan& plan, const MaterialPreparedProgram& program,
                                             bool originalAssociationProved,
                                             MaterialOriginalCaveCaptureFacts* facts = nullptr)
{
    auto evidence = std::make_shared<MaterialOriginalCaveCaptureFacts>(
        PrepareOriginalCaveCaptureProjection(program, plan.Settings, originalAssociationProved));
    if (plan.Lines != program.Lines)
        throw Refused("Cave capture recovery must retain its whole original program", "material Cave capture contract");
    auto projected = plan;
    const auto prior = plan.ConfigureBinding;
    projected.ConfigureBinding = [prior, evidence](SymbolicArena& arena, BindingContext& binding)
    {
        if (prior)
            prior(arena, binding);
        const auto previous = binding.LeafOverride;
        binding.LeafOverride = [previous, evidence](const Value& value, Value& out)
        {
            if (!is(value, "IN") || Args(value).size() != 3 || Args(value)[0].kind != Value::Text ||
                Args(value)[0].text != "buffer" || !NumberIs(Args(value)[1], 0) || !NumberIs(Args(value)[2], 3440))
                return previous && previous(value, out);
            Value other;
            if (previous && previous(value, other) && !NumberIs(other, 0))
                throw BindingRefused("capture-off projection conflicts with an existing input",
                                     "material Cave capture conflict");
            out = 0.;
            evidence->ProjectedFields.insert({0, 3440});
            return true;
        };
    };
    auto result = RecoverOriginalUnlitSceneExpressionGraph(projected, program.Reference, program.Header, true);
    for (const auto& field : evidence->ProjectedFields)
        result.AssumedEngineFields[{"View", field.second}] =
            "RealTimeReflectionCapture: explicit capture-off material-input projection; stock default compatibility only, original game runtime value unproved";
    result.FamilyComplete = false;
    result.CompilerProven = false;
    result.RendererValidated = false;
    if (facts)
        *facts = *evidence;
    return result;
}
}
} // P4MapKit::Material

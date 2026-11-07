// Port of material_shadow_exact.py's fully-rough material interface, admitted by its programs.
// Canonical compiled lighting is recovered; authored raw factorization remains
// unidentified. This is not a lighting-stop fallback or whole-renderer proof.
#pragma once
#include "material_source_owner.h"
#include "material_engine_hashes.h"
#include <cmath>
#include <tuple>
#include "material_source_common.h"
namespace P4MapKit
{
namespace Material
{
// The settings of a canonical fully rough surface: lit, opaque or masked, and compiled fully rough.
inline bool OriginalShadowCanonicalSettings(const Json& settings)
{
    return settings.Has("domain") && settings.At("domain").Text() == "MD_Surface" && settings.Has("shading_model") &&
           settings.At("shading_model").Text() == "MSM_DefaultLit" && settings.Has("blend_mode") &&
           IsMember(settings.At("blend_mode").Text(), {"BLEND_Opaque", "BLEND_Masked"}) &&
           settings.Has("render_settings") && settings.At("render_settings").Has("bFullyRough") &&
           settings.At("render_settings").At("bFullyRough").kind == Json::Boolean &&
           settings.At("render_settings").At("bFullyRough").boolean;
}
inline void RequireOriginalShadowCanonicalSettings(const MaterialOriginalOwnerSource& owner)
{
    if (!OriginalShadowCanonicalSettings(owner.User.Settings))
        throw Refused("original Shadow lacks its fully-rough surface settings", "material Shadow compiled settings");
}
// One base-pass program of every vertex factory and resource group, in sorted order, as the Python producer's
// policy_refs keeps them.
inline std::vector<FMaterialShaderReference> OriginalShadowCanonicalReferences(const MaterialOriginalOwnerSource& owner)
{
    RequireOriginalShadowCanonicalSettings(owner);
    std::vector<FMaterialShaderReference> actual;
    FReadError error;
    if (!ReadMaterialShaderReferences(owner.Map, actual, error))
        throw Refused(error.Code, "material Shadow original references");
    std::map<std::pair<std::string, int>, FMaterialShaderReference> groups;
    const ShaderPolicyIndex policies(actual);
    for (const auto& policy : policies.BasePass())
        groups.emplace(
            std::make_pair(MaterialFactoryName(policy.Reference.VertexFactoryHash), policy.Reference.ResourceIndex),
            policy.Reference);
    std::vector<FMaterialShaderReference> result;
    for (const auto& group : groups)
        result.push_back(group.second);
    return result;
}
// The fully rough Lambert carriers of a program: each MUL of a colour by the folded constant (0.018, or 0.018 / pi).
inline std::vector<std::size_t> OriginalShadowCanonicalCarriers(const std::vector<AnchorInstruction>& code)
{
    std::vector<std::size_t> carriers;
    for (std::size_t at = 0; at < code.size(); ++at)
    {
        if (code[at].op != "MUL" || code[at].mask != "xyz" || code[at].src.size() != 2 || !code[at].src[1].literal)
            continue;
        const auto coefficient = code[at].src[1].scalar();
        if (std::isfinite(coefficient) &&
            (std::abs(coefficient - .018) <= 1e-9 || std::abs(coefficient - .018 / 3.14159265358979323846) <= 1e-9))
            carriers.push_back(at);
    }
    return carriers;
}
// A base-pass program with nothing of the material in it: it reads only the View and Primitive buffers, no texture
// or sampler, and holds the fully rough Lambert carrier. Its lighting is compiled; the material adds no input.
inline bool OriginalShadowCanonicalProgram(const MaterialPreparedProgram& program)
{
    for (const auto& buffer : program.BaseBinding.Buffers)
        if (buffer.second != "View" && buffer.second != "Primitive")
            return false;
    if (!program.BaseBinding.TextureParameters.empty() || !program.BaseBinding.TextureEntries.empty() ||
        !program.BaseBinding.Samplers.empty() || !program.SamplerParameters.empty())
        return false;
    const auto normalized = NormalizeMaterialTraceDeclarations(program.Lines);
    return !OriginalShadowCanonicalCarriers(ParseMaterialAnchorInstructions(normalized.Lines)).empty();
}
// An owner whose every base-pass program is such a program, under fully rough lit settings.
inline bool OriginalShadowCanonicalOwner(const MaterialOriginalOwnerSource& owner,
                                         const MaterialOriginalArchiveAccess& archive, MaterialDomainSourceScope scope)
{
    if (!OriginalShadowCanonicalSettings(owner.User.Settings))
        return false;
    const auto refs = OriginalShadowCanonicalReferences(owner);
    if (refs.empty())
        return false;
    for (const auto& ref : refs)
        if (!OriginalShadowCanonicalProgram(PrepareOriginalMaterialArchiveProgram(owner, ref, archive, scope).Program))
            return false;
    return true;
}
inline Json OriginalShadowCanonicalGraph(const Json& settings)
{
    auto graph = settings;
    graph["format"] = 2;
    graph["tangent_space_normal"] = true;
    graph["representation"] = "canonical compiled fully-rough lighting; authored raw pins unidentified";
    graph["nodes"] =
        Object({{"CanonicalBlack", Object({{"type", "Constant3Vector"},
                                           {"inputs", Json::MakeObject()},
                                           {"props", Object({{"Constant", Array({0., 0., 0.})}})}})},
                {"CanonicalZero",
                 Object({{"type", "Constant"}, {"inputs", Json::MakeObject()}, {"props", Object({{"R", 0.}})}})},
                {"CanonicalSpecular",
                 Object({{"type", "Constant"}, {"inputs", Json::MakeObject()}, {"props", Object({{"R", .5}})}})},
                {"EffectiveOne",
                 Object({{"type", "Constant"}, {"inputs", Json::MakeObject()}, {"props", Object({{"R", 1.}})}})},
                {"GeometricNormal", Object({{"type", "Constant3Vector"},
                                            {"inputs", Json::MakeObject()},
                                            {"props", Object({{"Constant", Array({0., 0., 1.})}})}})}});
    graph["outputs"] = Object({{"BaseColor", Array({"CanonicalBlack", ""})},
                               {"Metallic", Array({"CanonicalZero", ""})},
                               {"Specular", Array({"CanonicalSpecular", ""})},
                               {"Roughness", Array({"EffectiveOne", ""})},
                               {"AmbientOcclusion", Array({"EffectiveOne", ""})},
                               {"EmissiveColor", Array({"CanonicalBlack", ""})},
                               {"Normal", Array({"GeometricNormal", ""})}});
    return graph;
}
inline MaterialOriginalOwnerRecovery RecoverOriginalShadowCanonicalExpressionGraph(
    const MaterialOriginalOwnerSource& owner, const MaterialOriginalArchiveAccess& archive,
    MaterialDomainSourceScope scope = MaterialDomainSourceScope::QualifiedGlobalTail)
{
    const auto refs = OriginalShadowCanonicalReferences(owner);
    if (!archive.MapIndices || !archive.Payload || !archive.Frequency)
        throw Refused("original Shadow archive access is incomplete", "material Shadow source association");
    const auto indices = archive.MapIndices(owner.Map.ResourceHash);
    MaterialOriginalOwnerRecovery result;
    bool primary = false;
    const auto selected = SelectOriginalMaterialPrimaryProgram(owner);
    auto receipts = Json::MakeArray();
    for (const auto& ref : refs)
    {
        if (ref.ResourceIndex < 0 || std::size_t(ref.ResourceIndex) >= indices.size())
            throw Refused("original Shadow resource index exceeds its map", "material Shadow source association");
        const auto payload = archive.Payload(indices.at(std::size_t(ref.ResourceIndex)));
        const auto prepared = PrepareOriginalMaterialArchiveProgram(owner, ref, archive, scope);
        if (!OriginalShadowCanonicalProgram(prepared.Program))
            throw Refused("original Shadow program reads material data", "material Shadow program");
        receipts.array.push_back(Object(
            {{"payload_sha256", PayloadDigest(payload)},
             {"instruction_sha256", ProgramDigest(NormalizeMaterialTraceDeclarations(prepared.Program.Lines).Lines)},
             {"factory", MaterialFactoryName(ref.VertexFactoryHash)},
             {"type", MaterialOriginalSourceShaderName(ref.TypeHash)},
             {"resource_index", ref.ResourceIndex}}));
        if (ref.FrozenOffset == selected.FrozenOffset)
        {
            result.Primary.Source = prepared;
            primary = true;
        }
    }
    if (!primary)
        throw Refused("original Shadow primary is not among its sealed policies", "material Shadow primary source");
    const auto normalized = NormalizeMaterialTraceDeclarations(result.Primary.Source.Program.Lines);
    result.Primary.OriginalLines = normalized.OriginalLines;
    result.Primary.Storage = normalized.Storage;
    auto& material = result.Primary.Material;
    material.Graph = OriginalShadowCanonicalGraph(owner.User.Settings);
    material.TraceStop = normalized.Lines.size();
    if (owner.User.Cooked.BlendMode == "BLEND_Masked")
    {
        const auto depth = SelectOriginalMaterialDepthProgram(owner, selected);
        if (depth.ResourceIndex < 0 || std::size_t(depth.ResourceIndex) >= indices.size())
            throw Refused("original Shadow depth index exceeds its map", "material Shadow depth association");
        auto prepared = PrepareOriginalMaterialArchiveProgram(owner, depth, archive, scope);
        const auto fixed = owner.Fixed;
        const auto runtime = PrepareOriginalMaterialRuntimeTextures(owner.User, owner.Map, prepared.Program);
        const auto plan = PlanOriginalMaterialPixelRecovery(
            prepared.Program, owner.User.Settings, OriginalMaterialGraphDefaults(owner),
            [fixed, runtime](SymbolicArena& arena, BindingContext& binding)
            {
                ApplyOriginalMaterialFixedTextures(binding, fixed);
                InstallOriginalMaterialRuntimeTextures(arena, binding, runtime);
            });
        const auto mask = RecoverMaterialDepthMaskInputs(plan.Recovery, owner.User.Settings.At("clip").Numeric());
        Lifter lift(*mask.Bound.Arena, plan.Recovery.Defaults);
        const auto link = lift.lift(mask.Bound.Values.at(0));
        auto depthGraph = owner.User.Settings;
        depthGraph["nodes"] = lift.nodes;
        MaskedUnlitDetail::Pool pool;
        std::map<std::string, std::string> baseMemo, depthMemo;
        auto outputs = Json::MakeObject();
        for (const auto& output : material.Graph.At("outputs").object)
            outputs[output.first] = pool.absorb(material.Graph, output.second, baseMemo);
        outputs["OpacityMask"] = pool.absorb(depthGraph, link, depthMemo);
        material.Graph["nodes"] = pool.Lift.nodes;
        material.Graph["outputs"] = outputs;
        material.AssumedEngineFields = mask.Bound.AssumedEngineFields;
        material.CanonicalUnitFields = mask.Bound.CanonicalUnitFields;
        result.Complements.push_back(std::move(prepared));
    }
    material.Validation = ValidateGraph(material.Graph);
    for (const auto& output : material.Graph.At("outputs").object)
        material.RecoveredPins.push_back(output.first);
    result.ComplementFacts = Object(
        {{"source_producer", "original-Shadow-fully-rough-canonical"},
         {"native_base_programs_checked", double(refs.size())},
         {"compiled_policy_receipts", receipts},
         {"authored_raw_factorization_known", false},
         {"renderer_proven", false},
         {"unaccounted_values",
          Array(
              {"authored BaseColor/Metallic/Specular factorization and parameter topology are unidentified",
               "Roughness is effective one under fully-rough compilation; authored value is unidentified",
               "AO expression is unidentified; effective indirect material multiplier is one",
               "vertex displacement, customized UV and shadow/depth geometry remain outside this pixel-lighting graph"})}});
    return result;
}
}
} // P4MapKit::Material

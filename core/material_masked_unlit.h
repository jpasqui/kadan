// Paired base/depth masked unlit expression recovery. The original early-depth
// contract permits opaque color recovery; the returned material remains masked.
#pragma once
#include "material_unlit.h"
#include "material_depth.h"
#include "material_scene_native_registry.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialMaskedUnlitFacts
{
    double Clip = 0;
    std::size_t DepthTraceStop = 0, GraphNodes = 0;
    std::vector<std::string> Anchors;
    std::string EarlyDepthMask;
    std::map<std::pair<std::string, int>, std::string> DepthAssumedEngineFields;
    std::set<std::pair<std::string, int>> DepthCanonicalUnitFields;
};
struct MaterialUnlitNoFogProjection
{
    std::map<int, std::string> OriginalFogBuffers;
    // Constructor-qualified field chain: Shared.Fog at 1712, scalar
    // ApplyVolumetricFog at +124. This is a no-fog material input projection,
    // never a claim that the original renderer keeps this switch disabled.
    int ApplyVolumetricFogOffset = 1836;
    bool ConstructorAssociationProved = false;
};
struct MaterialUnlitNoFogProjectionFacts
{
    std::map<std::pair<int, int>, std::string> ProjectedFields;
};
inline MaterialUnlitNoFogProjection PrepareMaterialUnlitNoFogProjection(const FMaterialShaderReference& reference,
                                                                        const CustomMaterial::ShaderBindings& header,
                                                                        const Json& settings,
                                                                        bool originalAssociationProved)
{
    if (settings.At("domain").Text() != "MD_Surface" || settings.At("shading_model").Text() != "MSM_Unlit")
        throw Refused("no-fog projection requires an original unlit surface", "material unlit fog inputs");
    const auto policy = ReadMaterialNativeSceneResourcePolicy(
        reference, header, settings.At("domain").Text(), settings.At("blend_mode").Text(), originalAssociationProved);
    MaterialUnlitNoFogProjection projection;
    for (const auto buffer : policy.OriginalLayoutBuffers)
    {
        const auto hash = header.layout_hashes.at(std::size_t(buffer));
        if (hash == 0x0d480502 || hash == 0x05ec0dee)
            projection.OriginalFogBuffers[buffer] = MaterialBuiltinSceneNativeRegistry().at(hash).Name;
    }
    if (projection.OriginalFogBuffers.empty())
        throw Refused("unlit fog field has no checked original base-pass layout", "material unlit fog layout");
    projection.ConstructorAssociationProved = true;
    return projection;
}
inline void InstallMaterialUnlitNoFogProjection(BindingContext& binding, const MaterialUnlitNoFogProjection& projection,
                                                const std::shared_ptr<MaterialUnlitNoFogProjectionFacts>& facts)
{
    if (!projection.ConstructorAssociationProved || projection.ApplyVolumetricFogOffset != 1836 ||
        projection.OriginalFogBuffers.empty() || !facts)
        throw Refused("unlit fog projection has no checked source association", "material unlit fog association");
    for (const auto& buffer : projection.OriginalFogBuffers)
        if (buffer.first < 0 || buffer.first > 31 ||
            !IsMember(buffer.second, {"FOpaqueBasePassUniformParameters", "FTranslucentBasePassUniformParameters"}))
            throw Refused("unlit fog projection has an invalid original layout", "material unlit fog layout");
    const auto previous = binding.LeafOverride;
    binding.LeafOverride = [projection, facts, previous](const Value& value, Value& out)
    {
        if (is(value, "IN") && Args(value).size() == 3)
        {
            const auto& a = Args(value);
            if (a[0].kind == Value::Text && a[0].text == "buffer" && IsNumber(a[1]) && IsNumber(a[2]) &&
                std::isfinite(a[1].number()) && std::isfinite(a[2].number()) && a[1].number() >= 0 &&
                a[1].number() <= 31 && std::floor(a[1].number()) == a[1].number() && a[2].number() == 1836)
            {
                const int buffer = int(a[1].number());
                const auto layout = projection.OriginalFogBuffers.find(buffer);
                if (layout != projection.OriginalFogBuffers.end())
                {
                    Value supplied;
                    if (previous && previous(value, supplied) && !NumberIs(supplied, 0))
                        throw BindingRefused("unlit fog projection conflicts with an existing source binding",
                                             "material unlit fog conflict");
                    facts->ProjectedFields[{buffer, 1836}] = layout->second + ".Shared.Fog.ApplyVolumetricFog";
                    out = 0.0;
                    return true;
                }
            }
        }
        return previous && previous(value, out);
    };
}
namespace MaskedUnlitDetail
{
// The same structural pooling contract as the family graph merger: preserve
// operand order, but reuse an already present swapped commutative binary node.
struct Pool
{
    SymbolicArena Arena;
    Lifter Lift;
    Pool() : Lift(Arena)
    {
    }
    std::string node(const std::string& kind, const Json& inputs, const Json& props)
    {
        if (IsMember(kind, {"Multiply", "Add", "Min", "Max"}) && inputs.Has("A") && inputs.Has("B"))
        {
            auto swapped = inputs;
            swapped["A"] = inputs.At("B");
            swapped["B"] = inputs.At("A");
            const auto found = Lift.by_key.find(Array({kind, swapped, props}).Dump());
            if (found != Lift.by_key.end())
                return found->second;
        }
        return Lift.node(kind, inputs, props);
    }
    Json absorb(const Json& graph, const Json& link, std::map<std::string, std::string>& memo)
    {
        const auto root = link.array.at(0).Text();
        std::vector<std::string> pending{root};
        while (!pending.empty())
        {
            const auto current = pending.back();
            if (memo.count(current))
            {
                pending.pop_back();
                continue;
            }
            const auto& original = graph.At("nodes").At(current);
            const auto& inputs = original.At("inputs");
            bool waiting = false;
            for (const auto& input : inputs.object)
            {
                const auto child = input.second.array.at(0).Text();
                if (!memo.count(child))
                {
                    pending.push_back(child);
                    waiting = true;
                    break;
                }
            }
            if (waiting)
            {
                if (pending.size() > 1000000)
                    throw Refused("masked unlit graph import budget exceeded", "masked unlit graph budget");
                continue;
            }
            auto imported = Json::MakeObject();
            for (const auto& input : inputs.object)
            {
                auto target = input.second;
                target.array[0] = memo.at(target.array[0].Text());
                imported[input.first] = std::move(target);
            }
            memo[current] = node(original.At("type").Text(), imported, original.At("props"));
            pending.pop_back();
        }
        auto result = link;
        result.array[0] = memo.at(root);
        return result;
    }
};
inline MaterialMaskAnchor constant_mask(const MaterialRecoveryPlan& depth, double cookedClip)
{
    std::vector<AnchorInstruction> code;
    std::string failure;
    try
    {
        code = ParseMaterialAnchorInstructions(depth.Lines);
    }
    catch (const EvalError& error)
    {
        failure = error.what();
    }
    if (!failure.empty())
        throw Refused(failure, "material anchor scan");
    MaterialMaskAnchor mask;
    if (!FindMaterialMaskAnchor(code, mask) || !mask.constant_clip)
        throw Refused("masked unlit surface has no constant native clip", "masked unlit inputs");
    const float actual = float(mask.clip_value), expected = float(cookedClip);
    std::uint32_t actualBits = 0, expectedBits = 0;
    std::memcpy(&actualBits, &actual, 4);
    std::memcpy(&expectedBits, &expected, 4);
    if (!std::isfinite(mask.clip_value) || !std::isfinite(cookedClip) || !std::isfinite(actual) ||
        !std::isfinite(expected) || actualBits != expectedBits)
        throw Refused("masked unlit clip differs from the cooked material",
                      "settings disagree: opacity mask clip value");
    return mask;
}
}
inline MaterialRecoveryResult RecoverMaskedUnlitMaterialExpressionGraph(const MaterialRecoveryPlan& base,
                                                                        const MaterialRecoveryPlan& depth,
                                                                        MaterialMaskedUnlitFacts* facts = nullptr)
{
    RequireKind(base.Settings, Json::Object, "material settings");
    if (base.Settings.At("domain").Text() != "MD_Surface" || base.Settings.At("shading_model").Text() != "MSM_Unlit" ||
        base.Settings.At("blend_mode").Text() != "BLEND_Masked")
        throw Refused("not a masked unlit surface", "masked unlit inputs");
    if (base.Lines.empty() || depth.Lines.empty())
        throw Refused("masked unlit surface has no base/depth pair", "masked unlit inputs");
    RequireKind(base.Settings.At("clip"), Json::Number, "opacity mask clip");
    const auto mask = MaskedUnlitDetail::constant_mask(depth, base.Settings.At("clip").Numeric());
    auto opaque = base;
    opaque.Settings["blend_mode"] = "BLEND_Opaque";
    auto result = RecoverUnlitMaterialExpressionGraph(opaque);
    const auto inputs = RecoverMaterialDepthMaskInputs(depth, mask.clip_value);
    Lifter depthLifter(*inputs.Bound.Arena, base.Defaults);
    const auto maskLink = depthLifter.lift(inputs.Bound.Values.at(0));
    auto maskGraph = base.Settings;
    maskGraph["format"] = 2;
    maskGraph["nodes"] = depthLifter.nodes;
    maskGraph["outputs"] = Object({{"OpacityMask", maskLink}});
    ValidateGraph(maskGraph);
    MaskedUnlitDetail::Pool pool;
    std::map<std::string, std::string> baseMemo, depthMemo;
    auto outputs = Json::MakeObject();
    for (const auto& output : result.Graph.At("outputs").object)
        outputs[output.first] = pool.absorb(result.Graph, output.second, baseMemo);
    outputs["OpacityMask"] = pool.absorb(maskGraph, maskLink, depthMemo);
    result.Graph["nodes"] = pool.Lift.nodes;
    result.Graph["outputs"] = std::move(outputs);
    result.Graph["blend_mode"] = "BLEND_Masked";
    result.Graph["clip"] = mask.clip_value;
    result.RecoveredPins.push_back("OpacityMask");
    result.Validation = ValidateGraph(result.Graph);
    if (facts)
    {
        facts->Clip = mask.clip_value;
        facts->DepthTraceStop = mask.line;
        facts->GraphNodes = pool.Lift.nodes.object.size();
        facts->Anchors = {"OpacityMask"};
        facts->EarlyDepthMask = "checked native depth scalar/constant clip";
        facts->DepthAssumedEngineFields = inputs.Bound.AssumedEngineFields;
        facts->DepthCanonicalUnitFields = inputs.Bound.CanonicalUnitFields;
    }
    return result;
}
inline MaterialRecoveryResult RecoverMaskedUnlitMaterialExpressionGraph(const MaterialRecoveryPlan& base,
                                                                        const MaterialRecoveryPlan& depth,
                                                                        const MaterialUnlitNoFogProjection& projection,
                                                                        MaterialMaskedUnlitFacts* facts = nullptr)
{
    auto projected = base;
    const auto prior = base.ConfigureBinding;
    const auto evidence = std::make_shared<MaterialUnlitNoFogProjectionFacts>();
    projected.ConfigureBinding = [prior, projection, evidence](SymbolicArena& arena, BindingContext& binding)
    {
        if (prior)
            prior(arena, binding);
        InstallMaterialUnlitNoFogProjection(binding, projection, evidence);
    };
    auto result = RecoverMaskedUnlitMaterialExpressionGraph(projected, depth, facts);
    for (const auto& field : evidence->ProjectedFields)
        result.AssumedEngineFields[{"buffer " + std::to_string(field.first.first), field.first.second}] =
            field.second + ": no-fog material-input projection (off)";
    result.CompilerProven = false;
    result.FamilyComplete = false;
    result.RendererValidated = false;
    return result;
}
}
} // P4MapKit::Material

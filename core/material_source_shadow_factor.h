// A source-bound internal dither factor is separate from full shadow coverage.
// This does not qualify vertex/light-depth execution or renderer admission.
#pragma once
#include "material_source_complements.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialOriginalShadowFactorCertificate
{
    std::size_t FactorLine = 0, MaskLine = 0;
};
using MaterialOriginalShadowFactorCertifier =
    MaterialOriginalShadowFactorCertificate (*)(const MaterialPreparedProgram&, const MaterialPreparedProgram&, double);
inline MaterialOriginalShadowFactorCertificate CertifyOriginalShadowDitherFactor(const MaterialPreparedProgram& camera,
                                                                                 const MaterialPreparedProgram& shadow,
                                                                                 double clip)
{
    if (!std::isfinite(clip) || float(clip) < 0 || float(clip) > 1)
        throw Refused("dither shadow clip is outside the finite retained range", "material shadow factor clip");
    const auto a = ParseMaterialAnchorInstructions(camera.Lines), b = ParseMaterialAnchorInstructions(shadow.Lines);
    MaterialMaskAnchor mask, other;
    if (!FindMaterialMaskAnchor(a, mask) || !FindMaterialMaskAnchor(b, other) || !mask.constant_clip ||
        !other.constant_clip || mask.line != other.line || mask.mask != other.mask ||
        float(mask.clip_value) != float(clip) || float(other.clip_value) != float(clip) || mask.line < 5)
        throw Refused("dither shadow camera/auxiliary clip anchors disagree", "material shadow factor clip");
    if (b.size() != a.size() && b.size() != a.size() + 3)
        throw Refused("dither shadow prefix has an unqualified length", "material shadow factor prefix");
    const auto at = mask.line - 3;
    const auto& x = a[at];
    const auto& y = b[at];
    if (x.op != "MAX" || y.op != "MAX" || x.mods != std::vector<std::string>{"F"} || x.mods != y.mods ||
        x.dst != y.dst || x.mask != y.mask || x.mask.size() != 1 || x.src.size() != 2 || y.src.size() != 2 ||
        x.reads(0, x.mask) != y.reads(0, y.mask) || x.src[0].negative || x.src[0].absolute || y.src[0].negative ||
        y.src[0].absolute || !x.src[1].literal || x.src[1].negative || x.src[1].absolute || x.src[1].scalar() != 0 ||
        !y.src[1].literal || y.src[1].negative || y.src[1].absolute || y.src[1].scalar() != 1)
        throw Refused("shadow is not the exact internal dither MAX(0)->MAX(1) factor", "material shadow factor anchor");
    const auto& floor = a[at - 2];
    const auto& ceil = a[at - 1];
    const auto& first = a[at + 1];
    const auto& second = a[at + 2];
    if (floor.op != "FLR" || floor.mods != std::vector<std::string>({"F", "SAT"}) || floor.src.size() != 1 ||
        floor.dst != x.dst || floor.mask != x.mask || x.reads(0, x.mask) != std::make_pair(floor.dst, floor.mask) ||
        ceil.op != "CEIL" || ceil.mods != std::vector<std::string>({"F", "SAT"}) || ceil.src.size() != 1 ||
        ceil.mask.size() != 1 || first.op != "MUL" || second.op != "MUL" ||
        first.mods != std::vector<std::string>{"F32"} || second.mods != first.mods || first.src.size() != 2 ||
        second.src.size() != 2 || first.dst != mask.mask.first || first.mask != mask.mask.second ||
        second.dst != mask.mask.first || second.mask != mask.mask.second)
        throw Refused("internal dither floor/ceil/product dependency differs", "material shadow factor dependency");
    const auto unmodified = [](const AnchorInstruction& ins)
    {
        for (const auto& source : ins.src)
            if (source.negative || source.absolute)
                return false;
        return true;
    };
    const auto factor = std::make_pair(x.dst, x.mask), alpha = std::make_pair(ceil.dst, ceil.mask), product = mask.mask;
    if (!unmodified(first) || !unmodified(second) ||
        !((first.reads(0, first.mask) == alpha) || (first.reads(1, first.mask) == alpha)) ||
        !((second.reads(0, second.mask) == product && second.reads(1, second.mask) == factor) ||
          (second.reads(1, second.mask) == product && second.reads(0, second.mask) == factor)))
        throw Refused("shadow dither factor is not multiplied into the retained opacity",
                      "material shadow factor dependency");
    for (std::size_t i = 0; i < a.size(); ++i)
        if (i != at && OriginalComplementSemanticInstruction(camera, camera.Lines[i]) !=
                           OriginalComplementSemanticInstruction(shadow, shadow.Lines[i]))
        {
            if (b.size() == a.size() + 3 && i + 1 == a.size() &&
                camera.Lines[i] == "MOV.F result_color0, {0, 0, 0, 0}.x")
                continue;
            throw Refused("shadow changes another camera/dither instruction", "material shadow factor prefix");
        }
    if (b.size() != a.size())
        CertifyOriginalShadowPixelDepthTail(shadow);
    return {at, mask.line};
}
inline MaterialOriginalComplementRecovery RecoverOriginalMaterialShadowDitherFactor(
    const MaterialOriginalOwnerSource& owner, const MaterialOriginalArchiveAccess& archive, const Json& cameraGraph,
    MaterialOriginalShadowFactorCertifier certify = CertifyOriginalShadowDitherFactor)
{
    if (!certify)
        throw Refused("shadow dither needs its typed native source certificate", "material shadow factor certificate");
    ValidateOriginalComplementGraph(cameraGraph);
    if (owner.User.Cooked.Domain != "MD_Surface" || owner.User.Cooked.BlendMode != "BLEND_Masked" ||
        !cameraGraph.At("outputs").Has("OpacityMask"))
        throw Refused("shadow dither requires an actual masked camera input", "material shadow factor mode");
    for (const auto& setting : owner.User.Settings.object)
    {
        if (setting.first == "format")
            continue;
        bool matches = cameraGraph.Has(setting.first);
        if (matches && setting.first == "clip")
        {
            const float a = float(cameraGraph.At("clip").Numeric()), b = float(setting.second.Numeric());
            std::uint32_t x = 0, y = 0;
            std::memcpy(&x, &a, 4);
            std::memcpy(&y, &b, 4);
            matches = std::isfinite(a) && std::isfinite(b) && x == y;
        }
        else if (matches)
            matches = cameraGraph.At(setting.first).Dump() == setting.second.Dump();
        if (!matches)
            throw Refused("camera graph differs from the actual original cooked settings",
                          "material shadow factor settings");
    }
    const auto primary = SelectOriginalMaterialPrimaryProgram(owner);
    const auto camera =
        PrepareOriginalMaterialComplement(owner, SelectOriginalMaterialDepthProgram(owner, primary), archive);
    const auto cameraRuntime = PrepareOriginalMaterialRuntimeTextures(owner.User, owner.Map, camera);
    const auto fixedCamera = owner.Fixed;
    const auto cameraPlan =
        PlanOriginalMaterialPixelRecovery(camera, owner.User.Settings, OriginalMaterialGraphDefaults(owner),
                                          [fixedCamera, cameraRuntime](SymbolicArena& arena, BindingContext& context)
                                          {
                                              ApplyOriginalMaterialFixedTextures(context, fixedCamera);
                                              InstallOriginalMaterialRuntimeTextures(arena, context, cameraRuntime);
                                          });
    const auto cameraMask =
        RecoverMaterialDepthMaskInputs(cameraPlan.Recovery, owner.User.Settings.At("clip").Numeric());
    Lifter cameraLift(*cameraMask.Bound.Arena, OriginalMaterialGraphDefaults(owner));
    const auto cameraLink = cameraLift.lift(cameraMask.Bound.Values.at(0));
    auto nativeCamera = owner.User.Settings;
    nativeCamera["format"] = 2;
    nativeCamera["nodes"] = cameraLift.nodes;
    nativeCamera["outputs"] = Object({{"OpacityMask", cameraLink}});
    ValidateGraph(nativeCamera);
    MaskedUnlitDetail::Pool comparison;
    std::map<std::string, std::string> suppliedMemo, nativeMemo;
    const auto supplied = comparison.absorb(cameraGraph, cameraGraph.At("outputs").At("OpacityMask"), suppliedMemo),
               actual = comparison.absorb(nativeCamera, cameraLink, nativeMemo);
    if (supplied.Dump() != actual.Dump())
        throw Refused("camera graph mask differs from the actual original depth expression",
                      "material shadow factor camera mask");
    MaterialOriginalComplementRecovery result;
    result.Graph = cameraGraph;
    Json maskGraph;
    std::string signature;
    std::size_t variants = 0;
    for (const auto& ref : owner.References)
        if (ref.Frequency == 3 && ref.VertexFactoryHash == primary.VertexFactoryHash &&
            OriginalMaterialShadowType(ref.TypeHash))
        {
            auto prepared = PrepareOriginalMaterialComplement(owner, ref, archive);
            const auto certificate = certify(camera, prepared, owner.User.Settings.At("clip").Numeric());
            const auto runtime = PrepareOriginalMaterialRuntimeTextures(owner.User, owner.Map, prepared);
            const auto fixed = owner.Fixed;
            const auto plan =
                PlanOriginalMaterialPixelRecovery(prepared, owner.User.Settings, OriginalMaterialGraphDefaults(owner),
                                                  [fixed, runtime](SymbolicArena& arena, BindingContext& context)
                                                  {
                                                      ApplyOriginalMaterialFixedTextures(context, fixed);
                                                      InstallOriginalMaterialRuntimeTextures(arena, context, runtime);
                                                  });
            const auto mask = RecoverMaterialDepthMaskInputs(plan.Recovery, owner.User.Settings.At("clip").Numeric());
            Lifter lifter(*mask.Bound.Arena, OriginalMaterialGraphDefaults(owner));
            const auto link = lifter.lift(mask.Bound.Values.at(0));
            auto graph = owner.User.Settings;
            graph["format"] = 2;
            graph["nodes"] = lifter.nodes;
            graph["outputs"] = Object({{"OpacityMask", link}});
            ValidateGraph(graph);
            const auto current = Object({{"nodes", graph.At("nodes")}, {"outputs", graph.At("outputs")}}).Dump();
            if (variants && current != signature)
                throw Refused("shadow dither variants recover different opacity expressions",
                              "material shadow factor variants");
            signature = current;
            maskGraph = std::move(graph);
            ++variants;
            result.Programs.push_back(std::move(prepared));
            result.Facts["factor_line"] = double(certificate.FactorLine);
            result.Facts["mask_line"] = double(certificate.MaskLine);
        }
    if (!variants)
        throw Refused("no exact original shadow dither program for the selected factory",
                      "material shadow factor inventory");
    // Keep every camera node/operand intact. Importing the auxiliary branch
    // must not repool commutative nodes and rewrite an already recovered root.
    std::map<std::string, std::string> names;
    for (const auto& node : maskGraph.At("nodes").object)
    {
        const auto name = OriginalComplementFresh(result.Graph, "ShadowDitherInput");
        names[node.first] = name;
        result.Graph["nodes"][name] = node.second;
    }
    for (const auto& node : maskGraph.At("nodes").object)
    {
        auto imported = node.second;
        for (auto& input : imported["inputs"].object)
            input.second.array[0] = names.at(input.second.array.at(0).Text());
        result.Graph["nodes"][names.at(node.first)] = std::move(imported);
    }
    auto shadowLink = maskGraph.At("outputs").At("OpacityMask");
    shadowLink.array[0] = names.at(shadowLink.array.at(0).Text());
    const auto selector = OriginalComplementFresh(result.Graph, "ShadowDitherPass");
    result.Graph["nodes"][selector] = OriginalComplementNode(
        "ShadowReplace", Object({{"Default", cameraGraph.At("outputs").At("OpacityMask")}, {"Shadow", shadowLink}}),
        Json::MakeObject());
    result.Graph["outputs"]["OpacityMask"] = OriginalComplementLink(selector);
    result.Facts["shadow_dither_factor_recovered"] = true;
    result.Facts["shadow_full_coverage"] = false;
    result.Facts["shadow_variants"] = double(variants);
    ValidateOriginalComplementGraph(result.Graph);
    return result;
}
}
}

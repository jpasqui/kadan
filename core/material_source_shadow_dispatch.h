// Source-shape dispatch only. Every selected auxiliary remains certified and
// recovered independently; unrelated complementary refusals are never caught.
#pragma once
#include "material_source_shadow_span.h"
namespace P4MapKit
{
namespace Material
{
enum class MaterialOriginalShadowDitherRoute
{
    None,
    ImmediateProducts,
    SampleSpan
};
inline MaterialOriginalShadowDitherRoute OriginalShadowDitherProgramCandidate(const MaterialPreparedProgram& camera,
                                                                              const MaterialPreparedProgram& shadow)
{
    const auto a = ParseMaterialAnchorInstructions(camera.Lines), b = ParseMaterialAnchorInstructions(shadow.Lines);
    MaterialMaskAnchor mask, other;
    if (!FindMaterialMaskAnchor(a, mask) || !FindMaterialMaskAnchor(b, other) || mask.line != other.line ||
        mask.mask != other.mask || (b.size() != a.size() && b.size() != a.size() + 3))
        return MaterialOriginalShadowDitherRoute::None;
    std::vector<std::size_t> changed;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (OriginalComplementSemanticInstruction(camera, camera.Lines[i]) !=
            OriginalComplementSemanticInstruction(shadow, shadow.Lines[i]))
        {
            if (b.size() == a.size() + 3 && i + 1 == a.size() &&
                camera.Lines[i] == "MOV.F result_color0, {0, 0, 0, 0}.x")
                continue;
            changed.push_back(i);
        }
    if (changed.size() != 1 || changed[0] >= mask.line)
        return MaterialOriginalShadowDitherRoute::None;
    const auto at = changed[0];
    const auto& x = a[at];
    const auto& y = b[at];
    if (x.op != "MAX" || y.op != "MAX" || x.mods != std::vector<std::string>{"F"} || x.mods != y.mods ||
        x.dst != y.dst || x.mask != y.mask || x.mask.size() != 1 || x.src.size() != 2 || y.src.size() != 2 ||
        x.reads(0, x.mask) != y.reads(0, y.mask) || x.src[0].negative || x.src[0].absolute || y.src[0].negative ||
        y.src[0].absolute || !x.src[1].literal || x.src[1].negative || x.src[1].absolute || x.src[1].scalar() != 0 ||
        !y.src[1].literal || y.src[1].negative || y.src[1].absolute || y.src[1].scalar() != 1)
        return MaterialOriginalShadowDitherRoute::None;
    const auto distance = mask.line - at;
    return distance == 3                     ? MaterialOriginalShadowDitherRoute::ImmediateProducts
           : distance >= 4 && distance <= 12 ? MaterialOriginalShadowDitherRoute::SampleSpan
                                             : MaterialOriginalShadowDitherRoute::None;
}
inline MaterialOriginalShadowDitherRoute
SelectOriginalMaterialShadowDitherRoute(const MaterialOriginalOwnerSource& owner,
                                        const MaterialOriginalArchiveAccess& archive)
{
    if (owner.User.Cooked.Domain != "MD_Surface" || owner.User.Cooked.BlendMode != "BLEND_Masked")
        return MaterialOriginalShadowDitherRoute::None;
    const auto primary = SelectOriginalMaterialPrimaryProgram(owner);
    const auto camera =
        PrepareOriginalMaterialComplement(owner, SelectOriginalMaterialDepthProgram(owner, primary), archive);
    MaterialOriginalShadowDitherRoute route = MaterialOriginalShadowDitherRoute::None;
    std::size_t variants = 0;
    for (const auto& ref : owner.References)
        if (ref.Frequency == 3 && ref.VertexFactoryHash == primary.VertexFactoryHash &&
            OriginalMaterialShadowType(ref.TypeHash))
        {
            const auto shadow = PrepareOriginalMaterialComplement(owner, ref, archive);
            const auto candidate = OriginalShadowDitherProgramCandidate(camera, shadow);
            if (candidate == MaterialOriginalShadowDitherRoute::None)
                return candidate;
            if (variants && candidate != route)
                throw Refused("original shadow dither variants have different source routes",
                              "material shadow factor variants");
            if (candidate == MaterialOriginalShadowDitherRoute::ImmediateProducts)
                CertifyOriginalShadowDitherFactor(camera, shadow, owner.User.Settings.At("clip").Numeric());
            else
                CertifyOriginalShadowDitherSpan(camera, shadow, owner.User.Settings.At("clip").Numeric());
            route = candidate;
            ++variants;
        }
    return variants ? route : MaterialOriginalShadowDitherRoute::None;
}
inline MaterialOriginalComplementRecovery
RecoverOriginalMaterialShadowDither(const MaterialOriginalOwnerSource& owner,
                                    const MaterialOriginalArchiveAccess& archive, const Json& cameraGraph)
{
    const auto route = SelectOriginalMaterialShadowDitherRoute(owner, archive);
    if (route == MaterialOriginalShadowDitherRoute::ImmediateProducts)
        return RecoverOriginalMaterialShadowDitherFactor(owner, archive, cameraGraph);
    if (route == MaterialOriginalShadowDitherRoute::SampleSpan)
        return RecoverOriginalMaterialShadowDitherSpan(owner, archive, cameraGraph);
    throw Refused("original auxiliary is outside both certified shadow dither forms", "material shadow factor route");
}
}
}

// Exact internal floor/dither MAX across the original sample dependency span.
// This is separate from full shadow coverage and the immediate two-product form.
#pragma once
#include "material_source_shadow_factor.h"
namespace P4MapKit
{
namespace Material
{
inline MaterialOriginalShadowFactorCertificate CertifyOriginalShadowDitherSpan(const MaterialPreparedProgram& camera,
                                                                               const MaterialPreparedProgram& shadow,
                                                                               double clip)
{
    if (!std::isfinite(clip) || float(clip) < 0 || float(clip) > 1)
        throw Refused("shadow span clip is outside the finite retained range", "material shadow span clip");
    const auto a = ParseMaterialAnchorInstructions(camera.Lines), b = ParseMaterialAnchorInstructions(shadow.Lines);
    MaterialMaskAnchor mask, other;
    if (!FindMaterialMaskAnchor(a, mask) || !FindMaterialMaskAnchor(b, other) || !mask.constant_clip ||
        !other.constant_clip || mask.line != other.line || mask.mask != other.mask ||
        float(mask.clip_value) != float(clip) || float(other.clip_value) != float(clip))
        throw Refused("shadow span camera/auxiliary clip anchors disagree", "material shadow span clip");
    if (b.size() != a.size() && b.size() != a.size() + 3)
        throw Refused("shadow span has an unqualified length", "material shadow span prefix");
    std::vector<std::size_t> differences;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (OriginalComplementSemanticInstruction(camera, camera.Lines[i]) !=
            OriginalComplementSemanticInstruction(shadow, shadow.Lines[i]))
        {
            if (b.size() == a.size() + 3 && i + 1 == a.size() &&
                camera.Lines[i] == "MOV.F result_color0, {0, 0, 0, 0}.x")
                continue;
            differences.push_back(i);
        }
    if (differences.size() != 1)
        throw Refused("shadow span must change exactly one dither factor instruction", "material shadow span prefix");
    const auto at = differences[0];
    if (at >= mask.line || mask.line - at < 4 || mask.line - at > 12)
        throw Refused("shadow factor is outside its bounded original sample span", "material shadow span anchor");
    const auto& x = a[at];
    const auto& y = b[at];
    if (x.op != "MAX" || y.op != "MAX" || x.mods != std::vector<std::string>{"F"} || x.mods != y.mods ||
        x.dst != y.dst || x.mask != y.mask || x.mask.size() != 1 || x.src.size() != 2 || y.src.size() != 2 ||
        x.reads(0, x.mask) != y.reads(0, y.mask) || x.src[0].negative || x.src[0].absolute || y.src[0].negative ||
        y.src[0].absolute || !x.src[1].literal || x.src[1].negative || x.src[1].absolute || x.src[1].scalar() != 0 ||
        !y.src[1].literal || y.src[1].negative || y.src[1].absolute || y.src[1].scalar() != 1)
        throw Refused("shadow span is not the exact internal MAX(0)->MAX(1) factor", "material shadow span anchor");
    const auto raw = x.reads(0, x.mask), factor = std::make_pair(x.dst, x.mask);
    std::size_t floor = a.size();
    for (std::size_t i = at; i > 0; --i)
    {
        const auto& writer = a[i - 1];
        if (writer.dst == raw.first && writer.mask.find(raw.second) != std::string::npos)
        {
            floor = i - 1;
            break;
        }
    }
    if (floor == a.size() || at - floor > 12 || a[floor].op != "FLR" ||
        a[floor].mods != std::vector<std::string>({"F", "SAT"}) || a[floor].src.size() != 1 || raw.second.size() != 1)
        throw Refused("shadow span factor is not the retained saturated floor output",
                      "material shadow span dependency");
    const auto& product = a[mask.line - 1];
    if (product.op != "MUL" || product.mods != std::vector<std::string>{"F32"} || product.src.size() != 2 ||
        product.dst != mask.mask.first || product.mask != mask.mask.second || product.src[0].negative ||
        product.src[0].absolute || product.src[1].negative || product.src[1].absolute ||
        !((product.reads(0, product.mask) == factor) || (product.reads(1, product.mask) == factor)))
        throw Refused("shadow span factor is not multiplied into the final mask", "material shadow span dependency");
    for (std::size_t i = at + 1; i + 1 < mask.line; ++i)
    {
        const auto& ins = a[i];
        if (ins.dst == factor.first && ins.mask.find(factor.second) != std::string::npos)
            throw Refused("shadow span overwrites its retained dither factor", "material shadow span dependency");
        for (std::size_t source = 0; source < ins.src.size(); ++source)
        {
            const auto read = ins.reads(source, ins.mask);
            if (read.first == factor.first && read.second.find(factor.second) != std::string::npos)
                throw Refused("shadow span factor has another intervening consumer", "material shadow span dependency");
        }
    }
    if (b.size() != a.size())
        CertifyOriginalShadowPixelDepthTail(shadow);
    return {at, mask.line};
}
inline MaterialOriginalComplementRecovery
RecoverOriginalMaterialShadowDitherSpan(const MaterialOriginalOwnerSource& owner,
                                        const MaterialOriginalArchiveAccess& archive, const Json& cameraGraph)
{
    auto result =
        RecoverOriginalMaterialShadowDitherFactor(owner, archive, cameraGraph, CertifyOriginalShadowDitherSpan);
    result.Facts["shadow_dither_span_recovered"] = true;
    return result;
}
}
}

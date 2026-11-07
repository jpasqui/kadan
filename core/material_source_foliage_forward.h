// Port of material_foliage_forward.py: forward TwoSidedFoliage pins, including widely scheduled normals.
// BasePassPixelShader.usf GetPrecomputedIndirectLightingAndSkyLight computes backface SH from -DiffuseDir;
// its final color combines diffuse lighting times DiffuseColor and backface lighting times SubsurfaceColor
// (UE 4.26 lines 425-428, 469-472, 521-524 and 1138). These anchors keep the original material boundary.
// Python applies them to the TwoSidedFoliage particle family (material_foliage_particles.anchors).
#pragma once
#include "material_anchor.h"
#include "material_shader_map.h"
#include <regex>
#include "material_source_common.h"
namespace P4MapKit
{
namespace Material
{
namespace FoliageForwardDetail
{
inline bool Plain(const AnchorInstruction& line)
{
    const auto head = line.text.substr(0, line.text.find(','));
    if (head.find('(') != std::string::npos)
        return false;
    return std::none_of(line.src.begin(), line.src.end(),
                        [](const AnchorOperand& o) { return o.negative || o.absolute; });
}
inline bool FirstRead(const std::vector<AnchorInstruction>& code, const std::string& reg, char c, std::size_t start)
{
    for (std::size_t i = start; i < code.size(); ++i)
    {
        for (const auto& o : code[i].src)
            if (o.base == reg && (o.swizzle.empty() || o.swizzle.find(c) != std::string::npos))
                return true;
        if (code[i].dst == reg && code[i].mask.find(c) != std::string::npos)
            return false;
    }
    return false;
}
inline bool Uniform(const std::string& comps)
{
    return !comps.empty() && std::all_of(comps.begin(), comps.end(), [&](char c) { return c == comps[0]; });
}
}
inline bool OriginalFoliageForwardOwner(const std::string& domain, const std::string& shading, const std::string& blend,
                                        const std::vector<FMaterialShaderReference>& references)
{
    if (domain != "MD_Surface" || shading != "MSM_TwoSidedFoliage" ||
        (blend != "BLEND_Opaque" && blend != "BLEND_Translucent" && blend != "BLEND_Additive"))
        return false;
    for (const auto& ref : references)
    {
        const auto f = ref.VertexFactoryHash;
        if (f == 0x6d728306d56d412full || f == 0x1adefad780cd16c5ull || f == 0x00080c76633abf12ull ||
            f == 0x2f7fb09897dda57cull)
            return true;
    }
    return false;
}
// normal(): an exact unchanged DP3 -> RSQ -> MUL held to the boundary, without a scheduling-distance guess.
inline bool FindOriginalFoliageForwardNormal(const std::vector<AnchorInstruction>& code, std::size_t stop,
                                             MaterialAnchor& out)
{
    using namespace FoliageForwardDetail;
    const auto depths = MaterialControlDepths(code);
    std::map<std::string, MaterialAnchor> hits;
    for (std::size_t at = 0; at < stop && at < code.size(); ++at)
    {
        const auto& final = code[at];
        if (depths[at] || final.op != "MUL" || final.mask != "xyz" || final.has("SAT") || !Plain(final) ||
            final.src.size() < 2)
            continue;
        if (AnchorWritten(code, final.dst, "xyz", at + 1, stop))
            continue;
        if (!FirstRead(code, final.dst, 'x', stop) || !FirstRead(code, final.dst, 'y', stop) ||
            !FirstRead(code, final.dst, 'z', stop))
            continue;
        for (std::size_t n = 0; n < 2; ++n)
        {
            const auto vector = final.reads(n, "xyz"), inverse = final.reads(1 - n, "xyz");
            if (vector.second != "xyz" || !Uniform(inverse.second))
                continue;
            std::size_t rsqAt = 0;
            bool found = false;
            for (std::size_t i = 0; i < at; ++i)
                if (code[i].dst == inverse.first && code[i].mask.find(inverse.second[0]) != std::string::npos)
                {
                    rsqAt = i;
                    found = true;
                }
            if (!found)
                continue;
            const auto& rsq = code[rsqAt];
            if (depths[rsqAt] || rsq.op != "RSQ" || rsq.mask.size() != 1 || rsq.has("SAT") || !Plain(rsq) ||
                rsq.src.empty())
                continue;
            const auto length = rsq.reads(0, rsq.mask);
            std::size_t dotAt = 0;
            found = false;
            for (std::size_t i = 0; i < rsqAt; ++i)
                if (code[i].dst == length.first && code[i].mask.find(length.second) != std::string::npos)
                {
                    dotAt = i;
                    found = true;
                }
            if (!found)
                continue;
            const auto& dot = code[dotAt];
            if (depths[dotAt] || dot.op != "DP3" || dot.src.size() != 2 || dot.has("SAT") || !Plain(dot) ||
                dot.reads(0, "xyz") != vector || dot.reads(1, "xyz") != vector ||
                AnchorWritten(code, vector.first, vector.second, dotAt + 1, at))
                continue;
            bool control = false;
            for (std::size_t i = dotAt; i <= at; ++i)
            {
                const auto& op = code[i].op;
                if (depths[i] || op == "IF" || op == "ENDIF" || op == "ELSE" || op == "REP" || op == "ENDREP")
                    control = true;
            }
            if (control)
                continue;
            hits[final.dst] = MaterialAnchor::Register(at, final.dst, "xyz");
        }
    }
    if (hits.size() != 1)
        return false;
    out = hits.begin()->second;
    return true;
}
// subsurface(): a saturated raw base times a scalar (or a Material word), separately accumulated by backface light.
inline bool FindOriginalFoliageForwardSubsurface(const std::vector<AnchorInstruction>& code, std::size_t stop,
                                                 const std::map<int, std::string>& buffers, MaterialAnchor& out)
{
    using namespace FoliageForwardDetail;
    const auto depths = MaterialControlDepths(code);
    std::map<std::string, MaterialAnchor> hits;
    static const std::regex address("^buf(\\d+)\\[(\\d+)\\]$");
    for (std::size_t at = 0; at < stop && at < code.size(); ++at)
    {
        const auto& color = code[at];
        if (depths[at] || color.mask != "xyz" || !color.has("SAT") || !Plain(color))
            continue;
        if (color.op == "MUL" && color.src.size() == 2)
        {
            std::vector<std::size_t> scalar;
            for (std::size_t n = 0; n < 2; ++n)
                if (Uniform(color.reads(n, "xyz").second))
                    scalar.push_back(n);
            if (scalar.size() != 1)
                continue;
            const auto raw = color.reads(1 - scalar[0], "xyz");
            bool base = false;
            for (std::size_t i = 0; i < at; ++i)
            {
                const auto& line = code[i];
                if (line.op == "MOV" && line.mask == "xyz" && line.has("SAT") && line.src.size() == 1 && Plain(line) &&
                    line.reads(0, "xyz") == raw && !AnchorWritten(code, raw.first, raw.second, i + 1, at))
                {
                    base = true;
                    break;
                }
            }
            if (!base)
                continue;
        }
        else if (color.op == "LDC" && color.src.size() == 1)
        {
            std::smatch m;
            const auto text = color.src[0].base;
            if (!std::regex_match(text, m, address))
                continue;
            const auto buffer = buffers.find(std::stoi(m[1]));
            if (buffer == buffers.end() || buffer->second != "Material")
                continue;
        }
        else
            continue;
        if (AnchorWritten(code, color.dst, "xyz", at + 1, stop))
            continue;
        for (std::size_t mulAt = stop; mulAt < code.size(); ++mulAt)
        {
            const auto& mul = code[mulAt];
            if (depths[mulAt] || mul.op != "MUL" || mul.mask != "xyz" || mul.src.size() != 2 || !Plain(mul))
                continue;
            if (mul.reads(0, "xyz") != std::make_pair(color.dst, std::string("xyz")) &&
                mul.reads(1, "xyz") != std::make_pair(color.dst, std::string("xyz")))
                continue;
            if (AnchorWritten(code, color.dst, "xyz", at + 1, mulAt))
                continue;
            for (std::size_t madAt = mulAt + 1; madAt < code.size(); ++madAt)
            {
                const auto& mad = code[madAt];
                if (depths[madAt] || mad.op != "MAD" || mad.mask != "xyz" || mad.src.size() != 3 || !Plain(mad))
                    continue;
                if (mad.reads(2, "xyz") == std::make_pair(mul.dst, std::string("xyz")) &&
                    !AnchorWritten(code, mul.dst, "xyz", mulAt + 1, madAt))
                    hits[color.dst] = MaterialAnchor::Register(at, color.dst, "xyz");
            }
        }
    }
    if (hits.size() != 1)
        return false;
    out = hits.begin()->second;
    return true;
}
// anchors(): the forward normal and subsurface color, when the program's own anchors lack them.
inline MaterialAnchors ApplyOriginalFoliageForwardAnchors(const std::vector<AnchorInstruction>& code,
                                                          MaterialAnchors found,
                                                          const std::map<int, std::string>& buffers)
{
    const auto stop = found.find("stop");
    if (stop == found.end())
        return found;
    const auto line = stop->second.line;
    if (!found.count("Normal") && !found.count("NormalWorld") && !found.count("NormalDecal"))
    {
        MaterialAnchor value;
        if (FindOriginalFoliageForwardNormal(code, line, value))
            found["NormalWorld"] = value;
    }
    if (!found.count("SubsurfaceColor"))
    {
        MaterialAnchor value;
        if (FindOriginalFoliageForwardSubsurface(code, line, buffers, value))
            found["SubsurfaceColor"] = value;
    }
    return found;
}
}
} // P4MapKit::Material

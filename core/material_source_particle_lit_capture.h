// The runtime reflection-capture fold does not set MATERIAL_FULLY_ROUGH.
// Preserve the source's unchanged material roughness and rounded F0 carrier.
#pragma once
#include "material_source_water_anchors.h"
namespace P4MapKit
{
namespace Material
{
namespace ParticleCaptureSourceDetail
{
struct Origin
{
    bool Valid = false, Literal = false;
    std::string Register, Components;
    std::size_t Line = 0;
    std::vector<double> Values;
};
inline bool Equal(const Origin& a, const Origin& b)
{
    return a.Valid && b.Valid && a.Literal == b.Literal &&
           (a.Literal ? a.Values == b.Values
                      : (a.Register == b.Register && a.Components == b.Components && a.Line == b.Line));
}
inline Origin ReadOrigin(const std::vector<AnchorInstruction>& code, AnchorOperand operand, std::string components,
                         std::size_t until, const std::vector<int>& depths)
{
    if (until > code.size() || depths.size() != code.size())
        throw Refused("invalid original reflection origin range", "material particle capture range");
    while (!operand.literal)
    {
        if (operand.negative || operand.absolute)
            return {};
        std::string selected;
        for (const auto c : components)
            selected += operand.component(std::string("xyzw").find(c));
        std::size_t last = code.size();
        for (std::size_t i = 0; i < until; ++i)
            if (code[i].dst == operand.base && std::any_of(selected.begin(), selected.end(), [&](char c)
                                                           { return code[i].mask.find(c) != std::string::npos; }))
                last = i;
        if (last == code.size())
            return {true, false, operand.base, selected, last, {}};
        const auto& line = code[last];
        if (!depths[last] && line.op == "MOV" && line.src.size() == 1 && !line.has("SAT") &&
            std::all_of(selected.begin(), selected.end(),
                        [&](char c) { return line.mask.find(c) != std::string::npos; }))
        {
            operand = line.src[0];
            components = selected;
            until = last;
        }
        else
            return {true, false, operand.base, selected, last, {}};
    }
    Origin out;
    out.Valid = true;
    out.Literal = true;
    const auto values = operand.vector();
    for (const auto c : components)
        out.Values.push_back(values[std::string("xyzw").find(c)]);
    return out;
}
}
inline MaterialAnchors OriginalParticleReflectionCaptureAnchors(const std::vector<AnchorInstruction>& code,
                                                                MaterialAnchors found, const BindingContext& binding)
{
    int view = -1, material = -1;
    for (const auto& row : binding.Buffers)
    {
        if (row.second == "View" && view < 0)
            view = row.first;
        if (row.second == "Material" && material < 0)
            material = row.first;
    }
    if (view < 0 || material < 0)
        return found;
    std::vector<int> depths;
    int depth = 0;
    for (const auto& line : code)
    {
        if (line.op == "ENDIF" || line.op == "ENDREP")
            --depth;
        depths.push_back(depth);
        if (line.op == "IF" || line.op == "REP")
            ++depth;
    }
    std::vector<MaterialAnchors> candidates;
    for (std::size_t at = 0; at + 3 < code.size(); ++at)
    {
        if (code[at].op != "IF" || depths[at])
            continue;
        const auto& expanded = code[at + 1];
        const auto& zero = code[at + 2];
        if (code[at + 3].op != "ENDIF" || zero.op != "MOV" || zero.mask != "xyz" || zero.src.size() != 1 ||
            !zero.src[0].literal || zero.src[0].scalar() != 0 || expanded.mask != "xyz")
            continue;
        std::size_t cc = code.size();
        for (std::size_t before = at; before;)
        {
            const auto i = --before;
            if (code[i].has("CC"))
            {
                cc = i;
                break;
            }
        }
        if (cc == code.size() || code[cc].op != "LDC" ||
            trim(code[cc].text.substr(code[cc].text.find(',') + 1)) != "buf" + std::to_string(view) + "[3436]")
            continue;
        std::vector<std::size_t> initial, diffuse;
        for (std::size_t i = std::max(cc, at > 12 ? at - 12 : 0); i < at; ++i)
        {
            const auto& line = code[i];
            if (depths[i] || line.op != "MOV" || line.mask != "xyz" || line.src.size() != 1)
                continue;
            if (line.dst == zero.dst && line.src[0].literal)
                initial.push_back(i);
            if (line.dst == expanded.dst)
                diffuse.push_back(i);
        }
        if (initial.size() != 1 || diffuse.size() != 1)
            continue;
        const auto f0 = code[initial[0]].src[0].scalar();
        if (!std::isfinite(f0) || std::fabs(f0 - .04) > 2e-9)
            continue;
        const auto di = diffuse[0];
        const auto& mov = code[di];
        MaterialAnchor base;
        if (mov.src[0].literal && mov.src[0].scalar() == 1.)
        {
            if (expanded.op != "MOV" || expanded.src.size() != 1 || !expanded.src[0].literal ||
                !std::isfinite(expanded.src[0].scalar()) || std::fabs(expanded.src[0].scalar() - 1.018) > 2e-8)
                continue;
            base = MaterialAnchor::Register(di, mov.dst, "xyz");
        }
        else if (!mov.src[0].literal && found.count("BaseColor"))
        {
            const auto metal = found.find("Metallic"), spec = found.find("Specular");
            if (metal == found.end() || !metal->second.has_constant || metal->second.constant != 0 ||
                spec == found.end() || !spec->second.has_constant || std::fabs(spec->second.constant - .5) > 1e-7)
                continue;
            if (expanded.op != "ADD" || expanded.src.size() != 2 || !expanded.src[1].literal ||
                !std::isfinite(expanded.src[1].scalar()) || std::fabs(expanded.src[1].scalar() - .018) > 2e-9 ||
                !ParticleCaptureSourceDetail::Equal(
                    ParticleCaptureSourceDetail::ReadOrigin(code, expanded.src[0], "xyz", at + 1, depths),
                    ParticleCaptureSourceDetail::ReadOrigin(code, mov.src[0], "xyz", di, depths)))
                continue;
            base = MaterialAnchor::Register(di, mov.src[0].base, mov.reads(0, "xyz").second);
        }
        else
            continue;
        std::size_t consumer = code.size();
        for (std::size_t i = at + 4; i < code.size(); ++i)
        {
            const auto& line = code[i];
            if (line.op != "MAD" || line.src.size() != 3 || line.src[0].literal || !line.src[1].literal)
                continue;
            const auto coefficients = line.src[1].vector();
            const double expected[4] = {-1, -.0275, -.572, .022};
            bool match = true;
            for (int c = 0; c < 4; ++c)
                match = match && std::fabs(coefficients[std::size_t(c)] - expected[c]) < 1e-6;
            if (match)
            {
                consumer = i;
                break;
            }
        }
        if (consumer == code.size())
            continue;
        const auto read = code[consumer].reads(0, "x");
        std::size_t write = code.size();
        for (std::size_t i = 0; i < at; ++i)
            if (code[i].dst == read.first && code[i].mask.find(read.second[0]) != std::string::npos)
                write = i;
        if (write == code.size() || code[write].op != "LDC" || !code[write].has("SAT") ||
            trim(code[write].text.substr(code[write].text.find(',') + 1)) != "buf" + std::to_string(material) + "[0]")
            continue;
        MaterialOpacityAnchor rough;
        bool held = true;
        try
        {
            rough = MaterialCopySourceAnchor(code, consumer, AnchorOperand(read.first + "." + read.second),
                                             read.second[0], at);
        }
        catch (const EvalError&)
        {
            held = false;
        }
        if (!held)
            continue;
        auto candidate = found;
        candidate["BaseColor"] = base;
        candidate["Metallic"] = MaterialAnchor::Constant(di, 0);
        candidate["Specular"] = MaterialAnchor::Constant(initial[0], f0 / .08);
        candidate["Roughness"] = rough.value;
        candidate["Roughness"].saturate = rough.saturate;
        candidate["stop"].line = at;
        candidates.push_back(std::move(candidate));
    }
    return candidates.size() == 1 ? candidates[0] : found;
}
}
} // P4MapKit::Material

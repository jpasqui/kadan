// Effective fully-rough material input. The authored raw roughness is erased
// by this policy; its value is not recovered or replaced by an assumed default.
#pragma once
#include "material_anchor.h"
#include "material_json.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialFullyRoughFold
{
    std::size_t Fold = 0, Specular = 0, Diffuse = 0, DielectricDelta = 0;
};
inline bool FullyRoughPlain(const AnchorInstruction& line)
{
    return line.text.substr(0, line.text.find(',')).find('(') == std::string::npos && !line.has("SAT");
}
inline bool FullyRoughScalar(const AnchorOperand& value, double expected)
{
    return value.literal && !value.negative && !value.absolute && std::isfinite(value.scalar()) &&
           std::fabs(value.scalar() - expected) < 1e-7;
}
inline bool FullyRoughRead(const AnchorInstruction& line, std::size_t operand, const std::string& mask,
                           const std::pair<std::string, std::string>& expected, bool negative = false)
{
    const auto& value = line.src.at(operand);
    return !value.literal && value.negative == negative && !value.absolute && line.reads(operand, mask) == expected;
}
inline bool FullyRoughLatest(const std::vector<AnchorInstruction>& code,
                             const std::pair<std::string, std::string>& value, std::size_t end, std::size_t& result)
{
    bool first = true;
    for (const auto component : value.second)
    {
        bool found = false;
        std::size_t last = 0;
        for (std::size_t i = 0; i < end; ++i)
            if (code[i].dst == value.first && code[i].mask.find(component) != std::string::npos)
            {
                found = true;
                last = i;
            }
        if (!found || (!first && result != last))
            return false;
        result = last;
        first = false;
    }
    return !first;
}
inline bool FindOriginalMaterialFullyRoughFold(const std::vector<AnchorInstruction>& code, const MaterialAnchors& found,
                                               MaterialFullyRoughFold& receipt)
{
    for (const auto* name : {"BaseColor", "Metallic", "Specular", "stop"})
        if (!found.count(name))
            return false;
    const auto& base = found.at("BaseColor");
    const auto& metal = found.at("Metallic");
    const auto& spec = found.at("Specular");
    if (base.register_name.empty() || metal.register_name.empty() || base.comps.size() != 3 ||
        metal.comps.size() != 1 || !spec.has_constant || !std::isfinite(spec.constant) ||
        std::fabs(spec.constant - .5) > 1e-6 || found.at("stop").line > code.size())
        return false;
    const auto baseValue = std::make_pair(base.register_name, base.comps),
               metalValue = std::make_pair(metal.register_name, metal.comps);
    std::vector<std::size_t> depth(code.size());
    std::size_t current = 0;
    for (std::size_t i = 0; i < code.size(); ++i)
    {
        depth[i] = current;
        if (code[i].op == "IF" || code[i].op == "REP")
            ++current;
        else if (code[i].op == "ENDIF" || code[i].op == "ENDREP")
        {
            if (!current)
                return false;
            --current;
        }
    }
    if (current)
        return false;
    for (std::size_t at = 0; at < code.size(); ++at)
    {
        const auto& final = code[at];
        if (final.op != "MAD" || final.mask != "xyz" || final.src.size() != 3 || !FullyRoughPlain(final) || depth[at])
            continue;
        for (std::size_t n = 0; n < 2; ++n)
        {
            if (!FullyRoughScalar(final.src[1 - n], .45) || final.src[n].literal || final.src[n].negative ||
                final.src[n].absolute || final.src[2].literal || final.src[2].negative || final.src[2].absolute)
                continue;
            std::size_t si = 0, di = 0;
            if (!FullyRoughLatest(code, final.reads(n, "xyz"), at, si) ||
                !FullyRoughLatest(code, final.reads(2, "xyz"), at, di))
                continue;
            const auto& spectral = code[si];
            const auto& diffuse = code[di];
            if (spectral.op != "MAD" || diffuse.op != "MAD" || spectral.mask != "xyz" || diffuse.mask != "xyz" ||
                spectral.src.size() != 3 || diffuse.src.size() != 3 || !FullyRoughPlain(spectral) ||
                !FullyRoughPlain(diffuse) || depth[si] || depth[di])
                continue;
            bool diffuseOK = false;
            for (std::size_t b = 0; b < 2; ++b)
                diffuseOK = diffuseOK || (FullyRoughRead(diffuse, b, "xyz", baseValue, true) &&
                                          FullyRoughRead(diffuse, 1 - b, "x", metalValue) &&
                                          FullyRoughRead(diffuse, 2, "xyz", baseValue));
            if (!diffuseOK || !FullyRoughScalar(spectral.src[2], .04))
                continue;
            for (std::size_t m = 0; m < 2; ++m)
            {
                if (!FullyRoughRead(spectral, m, "x", metalValue) || spectral.src[1 - m].literal ||
                    spectral.src[1 - m].negative || spectral.src[1 - m].absolute)
                    continue;
                std::size_t deltaAt = 0;
                if (!FullyRoughLatest(code, spectral.reads(1 - m, "xyz"), si, deltaAt))
                    continue;
                const auto& delta = code[deltaAt];
                if (delta.op != "ADD" || delta.mask != "xyz" || delta.src.size() != 2 || !FullyRoughPlain(delta) ||
                    depth[deltaAt])
                    continue;
                bool deltaOK = false;
                for (std::size_t b = 0; b < 2; ++b)
                    deltaOK = deltaOK ||
                              (FullyRoughRead(delta, b, "xyz", baseValue) && FullyRoughScalar(delta.src[1 - b], -.04));
                if (!deltaOK ||
                    AnchorWritten(code, baseValue.first, baseValue.second, std::min(deltaAt, di) + 1,
                                  std::max(deltaAt, di)) ||
                    AnchorWritten(code, metalValue.first, metalValue.second, std::min(si, di) + 1, std::max(si, di)))
                    continue;
                receipt = {at, si, di, deltaAt};
                return true;
            }
        }
    }
    return false;
}
inline MaterialAnchors ApplyOriginalMaterialFullyRoughAnchors(const std::vector<AnchorInstruction>& code,
                                                              MaterialAnchors found, const Json& settings,
                                                              MaterialFullyRoughFold* receipt = nullptr)
{
    if (!settings.Has("render_settings") || !settings.At("render_settings").Has("bFullyRough"))
        return found;
    const auto& flag = settings.At("render_settings").At("bFullyRough");
    if (flag.kind != Json::Boolean || !flag.boolean || found.count("Roughness"))
        return found;
    MaterialFullyRoughFold fold;
    if (!FindOriginalMaterialFullyRoughFold(code, found, fold))
        return found;
    found["Roughness"] = MaterialAnchor::Constant(found.at("stop").line, 1);
    if (receipt)
        *receipt = fold;
    return found;
}
}
} // P4MapKit::Material

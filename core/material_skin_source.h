// Exact material-side custom skin AO carrier. The fetched screen occlusion is
// engine lighting input and is never installed as a material graph value.
#pragma once
#include "material_anchor.h"
#include "material_graph.h"
#include <tuple>
namespace P4MapKit
{
namespace Material
{
inline bool MaterialSkinFetchDependent(const std::vector<AnchorInstruction>& code, const std::string& name,
                                       const std::string& component, std::size_t stop, std::size_t at)
{
    if (stop > at || at > code.size() || component.size() != 1)
        throw Refused("invalid skin fetch dependency boundary", "material skin dependency");
    using Key = std::tuple<std::string, std::string, std::size_t>;
    std::vector<Key> pending = {{name, component, at}};
    std::set<Key> seen;
    while (!pending.empty())
    {
        const auto key = pending.back();
        pending.pop_back();
        if (!seen.insert(key).second)
            continue;
        std::size_t last = code.size();
        for (std::size_t i = stop; i < std::get<2>(key); ++i)
            if (code[i].dst == std::get<0>(key) && code[i].mask.find(std::get<1>(key)[0]) != std::string::npos)
                last = i;
        if (last == code.size())
            continue;
        const auto& line = code[last];
        if (line.op == "TXF")
            return true;
        if (!IsMember(line.op, {"MOV", "ADD", "MUL", "MAD", "MIN", "MAX"}))
            continue;
        bool controlled = false;
        for (std::size_t i = last + 1; i < std::get<2>(key); ++i)
            controlled |= IsMember(code[i].op, {"IF", "ELSE", "ENDIF"});
        if (controlled)
            continue;
        for (std::size_t k = 0; k < line.src.size(); ++k)
            if (!line.src[k].literal)
            {
                const auto source = line.reads(k, std::get<1>(key));
                if (source.second.size() == 1)
                    pending.push_back({source.first, source.second, last});
            }
    }
    return false;
}
inline MaterialAnchors RecoverOriginalMaterialSkinAnchors(const std::vector<AnchorInstruction>& code,
                                                          MaterialAnchors found, const Json& settings)
{
    if (code.size() > 100000)
        throw Refused("skin anchor program exceeds its bound", "material skin dependency");
    if (settings.At("domain").Text() != "MD_Surface" ||
        settings.At("shading_model").Text() != "MSM_PreintegratedSkin" ||
        !IsMember(settings.At("blend_mode").Text(), {"BLEND_Opaque", "BLEND_Masked"}) || !found.count("stop"))
        return found;
    const auto stop = found.at("stop").line;
    if (stop > code.size())
        throw Refused("skin anchor stop exceeds the original program", "material skin dependency");
    std::vector<MaterialAnchor> candidates;
    for (std::size_t i = stop; i + 2 < code.size(); ++i)
    {
        const auto& line = code[i];
        const auto& clamp = code[i + 1];
        const auto& floor = code[i + 2];
        if (line.op != "MIN" || line.mask.size() != 1 || line.src.size() != 2 || clamp.op != "MOV" ||
            !clamp.has("SAT") || clamp.mask.size() != 1 || clamp.src.empty() ||
            clamp.reads(0, clamp.mask) != std::make_pair(line.dst, line.mask) || floor.op != "MAX" ||
            floor.src.size() != 2 || !floor.src[1].literal || std::fabs(floor.src[1].scalar() - .01) >= 1e-7 ||
            floor.reads(0, floor.mask) != std::make_pair(clamp.dst, clamp.mask))
            continue;
        std::vector<MaterialAnchor> held;
        unsigned fetched = 0;
        for (std::size_t k = 0; k < line.src.size(); ++k)
        {
            const auto& operand = line.src[k];
            if (operand.literal)
            {
                held.push_back(MaterialAnchor::Constant(i, operand.scalar()));
                continue;
            }
            const auto source = line.reads(k, line.mask);
            if (!AnchorWritten(code, source.first, source.second, stop, i))
                held.push_back(MaterialAnchor::Register(i, source.first, source.second));
            else if (MaterialSkinFetchDependent(code, source.first, source.second, stop, i))
                ++fetched;
        }
        if (held.size() == 1 && fetched == 1)
            candidates.push_back(held[0]);
    }
    if (candidates.size() == 1)
        found["AmbientOcclusion"] = candidates[0];
    return found;
}
}
} // P4MapKit::Material

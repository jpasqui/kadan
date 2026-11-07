// Source-qualified DefaultLit additive opacity: distinguish the held material
// multiplier from the conditional fog product and registered View exposure.
#pragma once
#include "material_source_water_anchors.h"
namespace P4MapKit
{
namespace Material
{
inline MaterialOriginalSourceAnchors OriginalParticleLitAdditiveAnchors(const std::vector<AnchorInstruction>& code,
                                                                        MaterialOriginalSourceAnchors found,
                                                                        const BindingContext& binding)
{
    const auto marker = found.Surface.find("stop");
    if (marker == found.Surface.end())
        throw Refused("additive material boundary is not identified", "material particle additive source family");
    const auto stop = marker->second.line;
    std::vector<std::size_t> rgb, alpha;
    for (std::size_t i = 0; i < code.size(); ++i)
        if (code[i].dst == "result_color0")
        {
            if (code[i].mask == "xyz")
                rgb.push_back(i);
            if (code[i].mask.find('w') != std::string::npos)
                alpha.push_back(i);
        }
    const auto plain = [](const AnchorInstruction& line)
    {
        return line.text.substr(0, line.text.find(',')).find('(') == std::string::npos;
    };
    const auto fail = [](const std::string& message)
    {
        throw Refused(message, "material particle additive source family");
    };
    if (rgb.size() != 1 || alpha.size() != 1)
        fail("additive output is not one RGB product and one alpha write");
    const auto& a = code[alpha.front()];
    if (a.op != "MOV" || a.src.size() != 1 || !a.src[0].literal || a.src[0].scalar() != 0 || !plain(a))
        fail("additive output alpha is not explicit zero");
    std::size_t at = rgb.front();
    const auto* line = &code[at];
    const auto unmodified = [&](const AnchorInstruction& value)
    {
        return plain(value) &&
               std::all_of(value.mods.begin(), value.mods.end(),
                           [](const std::string& mod) { return mod == "F32" || mod == "F"; }) &&
               std::all_of(value.src.begin(), value.src.end(),
                           [](const AnchorOperand& operand) { return !operand.negative && !operand.absolute; });
    };
    if (line->op != "MUL" || line->src.size() != 2 || !unmodified(*line))
        fail("additive RGB output is not an unchanged exposure product");
    const auto lastWrite = [&](const AnchorOperand& operand, const std::string& components, std::size_t before)
    {
        std::string channels;
        for (const auto c : components)
            channels.push_back(operand.component(std::string("xyzw").find(c)));
        std::size_t result = code.size();
        for (std::size_t i = 0; i < before; ++i)
            if (code[i].dst == operand.base && std::any_of(channels.begin(), channels.end(), [&](char c)
                                                           { return code[i].mask.find(c) != std::string::npos; }))
                result = i;
        return result;
    };
    const auto scalar = [](const std::string& channels)
    {
        return !channels.empty() &&
               std::all_of(channels.begin(), channels.end(), [&](char c) { return c == channels.front(); });
    };
    std::vector<std::size_t> exposure;
    const std::regex buffer("^buf([0-9]+)\\[([0-9]+)\\]$");
    for (std::size_t n = 0; n < 2; ++n)
    {
        if (!scalar(line->reads(n, "xyz").second))
            continue;
        const auto definition = lastWrite(line->src[n], "x", at);
        if (definition == code.size())
            continue;
        const auto& load = code[definition];
        std::smatch match;
        if (load.op != "LDC" || load.src.size() != 1 || !std::regex_match(load.src[0].base, match, buffer))
            continue;
        const auto slot = std::stoi(match[1]), offset = std::stoi(match[2]);
        const auto name = binding.Buffers.find(slot);
        if (name != binding.Buffers.end() && name->second == "View" &&
            offset + 4 * int(std::string("xyzw").find(line->reads(n, "x").second.front())) == 2180)
            exposure.push_back(n);
    }
    if (exposure.size() != 1)
        fail("additive output exposure is not the registered View input");
    if (line->reads(1 - exposure.front(), "xyz").second != "xyz")
        fail("additive output transforms the RGB source");
    auto operand = line->src[1 - exposure.front()];
    std::vector<MaterialScheduledAnchor> candidates;
    for (int product = 0; product < 2; ++product)
    {
        const auto definition = lastWrite(operand, "xyz", at);
        if (definition == code.size())
            fail("additive opacity/fog product is missing");
        at = definition;
        line = &code[at];
        if (line->op != "MUL" || line->mask != "xyz" || line->src.size() != 2 || !unmodified(*line))
            fail("additive opacity/fog product is transformed");
        std::vector<std::size_t> scalars;
        for (std::size_t n = 0; n < 2; ++n)
            if (scalar(line->reads(n, "xyz").second))
                scalars.push_back(n);
        if (scalars.size() != 1)
            fail("additive opacity/fog scalar is not distinct from RGB");
        const auto n = scalars.front();
        if (line->reads(1 - n, "xyz").second != "xyz")
            fail("additive opacity/fog product transforms its color source");
        bool valid = false;
        MaterialScheduledAnchor anchor;
        try
        {
            anchor = BuildDelayedMaterialAnchor(code, stop, at, line->src[n], 'x');
            valid = true;
        }
        catch (const Refused&)
        {
        }
        catch (const EvalError&)
        {
        }
        if (valid)
            candidates.push_back(std::move(anchor));
        if (product == 0 && !valid)
            fail("additive material opacity is not independently held");
        operand = line->src[1 - n];
    }
    if (candidates.size() != 1)
        fail("additive material opacity is not distinct from conditional fog");
    found.Surface["Opacity"] = candidates.front().ValueAnchor;
    if (!candidates.front().Indices.empty())
        found.Scheduled["Opacity"] = std::move(candidates.front());
    else
        found.Scheduled.erase("Opacity");
    return found;
}
}
} // P4MapKit::Material

// Exact late material emission consumer from material_remaining_emission.py.
// No consumer is a missing boundary, not proof that EmissiveColor is zero.
#pragma once
#include "material_schedule.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialEmissiveAnchor
{
    bool Found = false;
    MaterialScheduledAnchor Value;
    std::size_t ClampLine = 0, AddLine = 0;
};
inline MaterialScheduledAnchor BuildDelayedMaterialVectorAnchor(const std::vector<AnchorInstruction>& code,
                                                                std::size_t stop, std::size_t at,
                                                                const AnchorOperand& operand,
                                                                const std::string& components = "xyz")
{
    if (components.empty() || components.size() > 4)
        throw Refused("invalid delayed vector width", "material scheduled vector");
    MaterialScheduledAnchor out;
    std::set<std::size_t> indices;
    std::string registerName;
    for (const auto component : components)
    {
        const auto piece = BuildDelayedMaterialAnchor(code, stop, at, operand, component);
        if (piece.ValueAnchor.has_constant)
            throw Refused("scheduled vector is a literal rather than a register", "material scheduled vector");
        if (!registerName.empty() && registerName != piece.ValueAnchor.register_name)
            throw Refused("scheduled vector is split across registers", "material scheduled vector");
        registerName = piece.ValueAnchor.register_name;
        out.ValueAnchor.comps += piece.ValueAnchor.comps;
        indices.insert(piece.Indices.begin(), piece.Indices.end());
    }
    out.ValueAnchor.register_name = registerName;
    out.ValueAnchor.line = stop;
    out.Indices.assign(indices.begin(), indices.end());
    if (!out.Indices.empty())
        out.ScheduledLine = out.Indices.back() + 1;
    return out;
}
inline MaterialEmissiveAnchor FindScheduledMaterialEmissiveAnchor(const std::vector<AnchorInstruction>& code,
                                                                  std::size_t stop)
{
    if (stop > code.size())
        throw Refused("invalid emission boundary range", "material scheduled range");
    MaterialEmissiveAnchor out;
    if (code.empty() || code.back().dst != "result_color0")
        return out;
    const auto begin = std::max(stop, code.size() > 26 ? code.size() - 26 : std::size_t(0));
    for (std::size_t at = begin; at + 2 < code.size(); ++at)
    {
        const auto& line = code[at];
        if (line.op != "MAX" || line.mask != "xyz" || line.src.size() != 2 || !line.src[1].literal ||
            line.src[1].scalar() != 0.0 || line.src[0].literal)
            continue;
        bool consumed = false;
        std::size_t consumer = 0;
        for (std::size_t next = at + 1; next < std::min(at + 4, code.size()); ++next)
        {
            const auto& other = code[next];
            if (other.op == "ADD" && other.mask == "xyz")
                for (std::size_t i = 0; i < other.src.size(); ++i)
                {
                    const auto& value = other.src[i];
                    if (value.base == line.dst && !value.negative && !value.absolute &&
                        other.reads(i, "xyz").second == "xyz")
                    {
                        consumed = true;
                        consumer = next;
                    }
                }
            bool overwritten =
                other.dst == line.dst && std::any_of(other.mask.begin(), other.mask.end(), [](char c)
                                                     { return std::string("xyz").find(c) != std::string::npos; });
            if (overwritten)
                break;
        }
        if (!consumed)
            continue;
        const auto value = BuildDelayedMaterialVectorAnchor(code, stop, at, line.src[0]);
        if (out.Found)
            throw Refused("several native emissive tail consumers", "material emissive consumers");
        out.Found = true;
        out.Value = value;
        out.ClampLine = at;
        out.AddLine = consumer;
    }
    return out;
}
}
}

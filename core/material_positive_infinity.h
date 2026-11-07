// Finite transport for a strict +Inf equality choice of an original UI graph
// (tools/navigation/material_positive_infinity.py). The lifter keeps a +Inf scalar literal as a
// Constant with the property PositiveInfinity; this lowering replaces each of its two uses, B of a
// zero-threshold If whose unequal arms are the same, with a Custom that tests the exact +Inf word.
// Anything else that holds the literal is refused: no nonfinite value reaches a graph.
#pragma once
#include "material_graph.h"
#include <set>
namespace P4MapKit
{
namespace Material
{
inline const char* MaterialPositiveInfinityDescription()
{
    return "Original positive infinity equality";
}
inline const char* MaterialPositiveInfinityCode()
{
    return "return asuint(Value) == 0x7f800000u ? Equal : Unequal;";
}
inline Json MaterialPositiveInfinityInputs()
{
    return Array(
        {Object({{"InputName", "Value"}}), Object({{"InputName", "Equal"}}), Object({{"InputName", "Unequal"}})});
}
inline bool MaterialPositiveInfinityLiteral(const Json& node)
{
    return node.Has("type") && node.At("type").kind == Json::String && node.At("type").string == "Constant" &&
           node.Has("props") && node.At("props").Has("PositiveInfinity");
}

// The number of channels a link gives, for the node kinds the original UI graphs use there.
inline int MaterialPositiveInfinityWidth(const Json& nodes, const Json& link, std::set<std::string> active = {})
{
    if (link.kind != Json::Array || link.array.size() != 2 || link.array[0].kind != Json::String ||
        !nodes.Has(link.array[0].string) || link.array[1].kind != Json::String)
        throw Refused("positive infinity choice has an invalid link", "positive infinity choice");
    const auto key = link.array[0].string, mask = link.array[1].string;
    if (active.count(key) || active.size() >= 128)
        throw Refused("positive infinity choice has a cyclic or unbounded input", "positive infinity choice");
    const auto& node = nodes.At(key);
    const auto kind = node.At("type").Text();
    const auto props = node.Has("props") ? node.At("props") : Json::MakeObject();
    const auto inputs = node.Has("inputs") ? node.At("inputs") : Json::MakeObject();
    active.insert(key);
    static const std::map<std::string, int> sizes = {
        {"Constant", 1},          {"ScalarParameter", 1}, {"Constant2Vector", 2},
        {"Constant3Vector", 3},   {"Constant4Vector", 4}, {"VectorParameter", 4},
        {"TextureCoordinate", 2}, {"TextureSample", 4},   {"TextureSampleParameter2D", 4},
        {"DotProduct", 1},        {"Length", 1},          {"Panner", 2}};
    static const std::set<std::string> arithmetic = {"Add",        "Subtract",  "Multiply", "Divide",
                                                     "Power",      "Min",       "Max",      "LinearInterpolate",
                                                     "Abs",        "Sine",      "Cosine",   "Floor",
                                                     "Ceil",       "Frac",      "Saturate", "OneMinus",
                                                     "SquareRoot", "Normalize", "If",       "StaticSwitchParameter"};
    int size = 0;
    const auto found = sizes.find(kind);
    if (found != sizes.end())
        size = found->second;
    else if (kind == "ComponentMask")
    {
        for (const auto* c : {"R", "G", "B", "A"})
            if (props.Has(c) && props.At(c).kind == Json::Boolean && props.At(c).boolean)
                ++size;
    }
    else if (kind == "Custom" && props.Has("Code") && props.At("Code").kind == Json::String &&
             props.At("Code").string == MaterialPositiveInfinityCode() && props.Has("OutputType") &&
             props.At("OutputType").Dump() == "\"CMOT_Float1\"")
        size = 1;
    else if (kind == "AppendVector")
    {
        for (const auto& input : inputs.object)
            size += MaterialPositiveInfinityWidth(nodes, input.second, active);
    }
    else if (arithmetic.count(kind))
    {
        size = 1;
        bool any = false;
        int widest = 0;
        for (const auto& input : inputs.object)
        {
            if (kind == "If" && input.first != "AGreaterThanB" && input.first != "AEqualsB" &&
                input.first != "ALessThanB")
                continue;
            any = true;
            widest = std::max(widest, MaterialPositiveInfinityWidth(nodes, input.second, active));
        }
        if (any)
            size = widest;
    }
    else
        throw Refused("positive infinity choice input width is not proved: " + kind, "positive infinity choice");
    if (!mask.empty())
    {
        for (const auto c : mask)
        {
            const auto at = std::string("RGBA").find(c);
            if (at == std::string::npos || int(at) >= size)
                throw Refused("positive infinity choice has an invalid channel mask", "positive infinity choice");
        }
        size = int(mask.size());
    }
    return size;
}

// The lowered graph; the graph as it is when it holds no +Inf literal.
inline Json LowerMaterialPositiveInfinity(const Json& graph)
{
    if (!graph.Has("nodes"))
        return graph;
    const auto& nodes = graph.At("nodes");
    std::vector<std::string> literals;
    for (const auto& item : nodes.object)
        if (MaterialPositiveInfinityLiteral(item.second))
            literals.push_back(item.first);
    if (literals.empty())
        return graph;
    const auto text = [&](const char* key)
    {
        return graph.Has(key) && graph.At(key).kind == Json::String ? graph.At(key).string : std::string();
    };
    if (!graph.Has("format") || graph.At("format").Dump() != "2" || text("domain") != "MD_UI" ||
        text("blend_mode") != "BLEND_Translucent" || text("shading_model") != "MSM_Unlit")
        throw Refused("nonfinite material constant outside a translucent unlit UI graph", "nonfinite graph property");
    if (literals.size() != 1)
        throw Refused("positive infinity graph has more than one literal", "nonfinite graph property");
    const auto literal = literals.front();
    const auto& source = nodes.At(literal);
    if ((source.Has("inputs") && !source.At("inputs").object.empty()) || source.At("props").object.size() != 1)
        throw Refused("positive infinity literal has unexpected inputs or properties", "nonfinite graph property");
    std::vector<std::pair<std::string, std::string>> uses;
    for (const auto& item : nodes.object)
        if (item.second.Has("inputs"))
            for (const auto& input : item.second.At("inputs").object)
                if (input.second.kind == Json::Array && !input.second.array.empty() &&
                    input.second.array[0].kind == Json::String && input.second.array[0].string == literal)
                    uses.emplace_back(item.first, input.first);
    for (const auto& output : graph.At("outputs").object)
        if (output.second.kind == Json::Array && !output.second.array.empty() &&
            output.second.array[0].kind == Json::String && output.second.array[0].string == literal)
            throw Refused("positive infinity literal is a material output", "nonfinite graph property");
    if (uses.size() != 2)
        throw Refused("positive infinity literal has other than two equality uses", "nonfinite graph property");
    auto changed = graph;
    for (const auto& use : uses)
    {
        const auto& node = nodes.At(use.first);
        const auto& inputs = node.At("inputs");
        std::set<std::string> pins;
        for (const auto& input : inputs.object)
            pins.insert(input.first);
        if (use.second != "B" || inputs.At("B").Dump() != Array({literal, ""}).Dump() ||
            node.At("type").Text() != "If" || !node.Has("props") ||
            node.At("props").Dump() != Object({{"EqualsThreshold", 0.0}}).Dump() ||
            pins != std::set<std::string>{"A", "B", "AEqualsB", "AGreaterThanB", "ALessThanB"} ||
            inputs.At("AGreaterThanB").Dump() != inputs.At("ALessThanB").Dump())
            throw Refused("positive infinity use is not a strict equality with identical unequal branches",
                          "nonfinite graph property");
        const auto links =
            Object({{"Value", inputs.At("A")}, {"Equal", inputs.At("AEqualsB")}, {"Unequal", inputs.At("ALessThanB")}});
        for (const auto& link : links.object)
            if (MaterialPositiveInfinityWidth(nodes, link.second) != 1)
                throw Refused("positive infinity choice is not the proved scalar branch contract",
                              "nonfinite graph property");
        changed["nodes"][use.first] = Object({{"type", "Custom"},
                                              {"inputs", links},
                                              {"props", Object({{"Code", MaterialPositiveInfinityCode()},
                                                                {"OutputType", "CMOT_Float1"},
                                                                {"Description", MaterialPositiveInfinityDescription()},
                                                                {"Inputs", MaterialPositiveInfinityInputs()}})}});
    }
    changed["nodes"].object.erase(literal);
    return changed;
}
}
} // P4MapKit::Material

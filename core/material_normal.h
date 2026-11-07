// Component-accurate compiled normal/AO carriers and Normal pin recovery.
// A scalar-sign carrier remains a candidate until MaterialNormal validates it.
#pragma once
#include "material_anchor.h"
#include "material_bind.h"
#include "material_graph.h"

namespace P4MapKit
{
namespace Material
{
inline MaterialAnchors CorrectMaterialAmbientOcclusion(const std::vector<AnchorInstruction>& code,
                                                       const MaterialAnchors& found)
{
    auto out = found;
    auto anchor = out.find("AmbientOcclusion");
    if (anchor == out.end() || anchor->second.line + 1 >= code.size())
        return out;
    const auto at = anchor->second.line;
    const auto& begin = code[at];
    const auto& multiply = code[at + 1];
    if (begin.op != "MAD" || multiply.op != "MUL" || multiply.src.size() != 2 || multiply.mask.size() != 1)
        return out;
    for (std::size_t k = 0; k < multiply.src.size(); ++k)
        if (multiply.src[k].base != begin.dst && !multiply.src[k].literal)
        {
            auto source = multiply.reads(k, multiply.mask);
            out["AmbientOcclusion"] = MaterialAnchor::Register(at, source.first, source.second);
            break;
        }
    return out;
}
namespace NormalDetail
{
inline bool written(const std::vector<AnchorInstruction>& code, const std::pair<std::string, std::string>& v,
                    std::size_t begin, std::size_t end)
{
    return begin < end && AnchorWritten(code, v.first, v.second, begin, end);
}
inline bool control(const AnchorInstruction& line)
{
    return line.op == "IF" || line.op == "ELSE" || line.op == "ENDIF";
}
inline bool modified(const AnchorOperand& operand)
{
    return operand.negative || operand.absolute;
}
inline bool number_is(const Value& value, int number)
{
    return (value.kind == Value::Float || value.kind == Value::Int || value.kind == Value::Boolean) &&
           value.number() == number;
}
inline bool first_read(const std::vector<AnchorInstruction>& code, const std::string& name, char component,
                       std::size_t start)
{
    for (std::size_t i = start; i < code.size(); ++i)
    {
        for (const auto& operand : code[i].src)
            if (operand.base == name &&
                (operand.swizzle.empty() || operand.swizzle.find(component) != std::string::npos))
                return true;
        if (code[i].dst == name && code[i].mask.find(component) != std::string::npos)
            return false;
    }
    return false;
}
inline Value dot(SymbolicArena& arena, const std::vector<Value>& values)
{
    Value total = 0;
    for (const auto& v : values)
        total = simplify(arena, "ADD", {total, simplify(arena, "MUL", {v, v})});
    return total;
}
inline bool normalized(const std::vector<Value>& values)
{
    if (values.empty())
        return false;
    for (const auto& x : values)
        if (!is(x, "MUL") || x.expression->args.size() != 2 || !is(x.expression->args[0], "RSQ") ||
            x.expression->args[0].expression->args.size() != 1 ||
            !same(x.expression->args[0], values[0].expression->args[0]))
            return false;
    return true;
}
}
inline MaterialAnchors ScheduledMaterialReflection(const std::vector<AnchorInstruction>& code,
                                                   const MaterialAnchors& found)
{
    auto out = found;
    auto stop_anchor = out.find("stop");
    if (stop_anchor == out.end())
        return out;
    const auto stop = stop_anchor->second.line;
    if (stop > code.size())
        throw EvalError("normal-anchor-stop-range");
    auto existing = out.find("Normal");
    if (existing != out.end() && !NormalDetail::written(code, {existing->second.register_name, existing->second.comps},
                                                        stop, existing->second.line))
        return out;
    std::map<std::pair<std::string, std::string>, MaterialAnchor> hits;
    std::vector<int> depths;
    int depth = 0;
    for (const auto& line : code)
    {
        depths.push_back(depth);
        if (line.op == "IF")
            ++depth;
        else if (line.op == "ENDIF")
            --depth;
    }
    for (std::size_t i = stop; i < code.size(); ++i)
    {
        const auto& dot = code[i];
        if (depths[i] || dot.op != "DP3" || dot.src.size() != 2 || dot.mask.size() != 1 || dot.has("SAT"))
            continue;
        if (std::any_of(dot.src.begin(), dot.src.end(),
                        [](const AnchorOperand& v) { return v.literal || NormalDetail::modified(v); }))
            continue;
        for (std::size_t j = i + 1; j < std::min(i + 16, code.size()); ++j)
        {
            const auto& multiply = code[j];
            if (NormalDetail::control(multiply))
                break;
            if (multiply.op != "MUL" || multiply.mask != "xyz" || multiply.src.size() != 2 || multiply.has("SAT") ||
                std::any_of(multiply.src.begin(), multiply.src.end(), NormalDetail::modified))
                continue;
            for (std::size_t n = 0; n < 2; ++n)
            {
                auto normal = dot.reads(n, "xyz"), view = dot.reads(1 - n, "xyz");
                const auto a = multiply.reads(0, "xyz"), b = multiply.reads(1, "xyz"),
                           scalar = std::make_pair(dot.dst, dot.mask + dot.mask + dot.mask);
                if ((normal != a && normal != b) || (scalar != a && scalar != b) ||
                    NormalDetail::written(code, {dot.dst, dot.mask}, i + 1, j) ||
                    NormalDetail::written(code, normal, i + 1, j))
                    continue;
                for (std::size_t k = j + 1; k < std::min(j + 16, code.size()); ++k)
                {
                    const auto& final = code[k];
                    if (NormalDetail::control(final))
                        break;
                    if (final.op != "MAD" || final.mask != "xyz" || final.src.size() != 3 || final.has("SAT") ||
                        final.src[1].scalar() != 2.0 || NormalDetail::modified(final.src[0]) ||
                        !final.src[2].negative || final.src[2].absolute ||
                        final.reads(0, "xyz") != std::make_pair(multiply.dst, std::string("xyz")) ||
                        final.reads(2, "xyz") != view || NormalDetail::written(code, {multiply.dst, "xyz"}, j + 1, k) ||
                        NormalDetail::written(code, view, i + 1, k) || NormalDetail::written(code, normal, stop, i))
                        continue;
                    hits[normal] = MaterialAnchor::Register(i, normal.first, normal.second);
                }
            }
        }
    }
    if (hits.size() == 1)
        out["Normal"] = hits.begin()->second;
    return out;
}
inline MaterialAnchors ForwardMaterialWorldNormal(const std::vector<AnchorInstruction>& code,
                                                  const MaterialAnchors& found)
{
    auto out = ScheduledMaterialReflection(code, found);
    auto normal = out.find("NormalWorld"), stop_anchor = out.find("stop");
    if (normal == out.end() || stop_anchor == out.end() || normal->second.line >= stop_anchor->second.line)
        return out;
    const auto stop = stop_anchor->second.line;
    const auto original = normal->second;
    if (!NormalDetail::written(code, {original.register_name, original.comps}, original.line + 1, stop))
        return out;
    using Carrier = std::pair<std::string, std::string>;
    std::map<Carrier, std::size_t> carriers;
    carriers[{original.register_name, original.comps}] = original.line;
    int depth = 0;
    for (std::size_t i = 0; i <= original.line; ++i)
    {
        if (code[i].op == "IF")
            ++depth;
        else if (code[i].op == "ENDIF")
            --depth;
    }
    for (std::size_t i = original.line + 1; i < stop; ++i)
    {
        const auto& line = code[i];
        if (line.op == "ENDIF")
            --depth;
        bool candidate = false;
        const auto comma = line.text.find(',');
        bool predicated = line.text.substr(0, comma).find('(') != std::string::npos;
        if (!depth && !predicated && line.mask == "xyz" && !line.has("SAT"))
        {
            if (line.op == "MOV" && line.src.size() == 1 && !NormalDetail::modified(line.src[0]))
                candidate = carriers.count(line.reads(0, "xyz")) != 0;
            else if (line.op == "MUL" && line.src.size() == 2 &&
                     !std::any_of(line.src.begin(), line.src.end(), NormalDetail::modified))
                for (std::size_t k = 0; k < 2; ++k)
                {
                    auto source = line.reads(k, "xyz"), factor = line.reads(1 - k, "xyz");
                    std::set<char> components(factor.second.begin(), factor.second.end());
                    if (carriers.count(source) && components.size() == 1)
                        candidate = true;
                }
        }
        for (auto key = carriers.begin(); key != carriers.end();)
        {
            bool overwritten = false;
            if (key->first.first == line.dst)
                for (auto component : key->first.second)
                    if (line.mask.find(component) != std::string::npos)
                        overwritten = true;
            if (overwritten)
                key = carriers.erase(key);
            else
                ++key;
        }
        if (candidate)
            carriers[{line.dst, "xyz"}] = i;
        if (line.op == "IF")
            ++depth;
    }
    std::vector<std::pair<Carrier, std::size_t>> held;
    for (const auto& carrier : carriers)
    {
        bool read = true;
        for (auto c : carrier.first.second)
            read = read && NormalDetail::first_read(code, carrier.first.first, c, stop);
        if (read)
            held.push_back(carrier);
    }
    if (held.size() == 1)
        out["NormalWorld"] = MaterialAnchor::Register(held[0].second, held[0].first.first, held[0].first.second);
    return out;
}
struct MaterialNormalResult
{
    std::vector<Value> pin;
    bool has_pin = false, tangent_space = true;
};
inline MaterialNormalResult MaterialNormal(SymbolicArena& arena, const std::vector<Value>& input)
{
    auto world = input;
    auto side = source(arena, "TwoSidedSign");
    if (world.size() != 3)
        throw Refused("a world normal that is not a normalized vector");
    bool signed_normal = true;
    for (const auto& x : world)
        if (!is(x, "MUL") || x.expression->args.size() != 2 || !same(x.expression->args[1], side))
            signed_normal = false;
    if (signed_normal)
        for (auto& x : world)
            x = x.expression->args[0];
    if (!NormalDetail::normalized(world))
        throw Refused("a world normal that is not a normalized vector");
    std::vector<Value> inner;
    for (const auto& x : world)
        inner.push_back(x.expression->args[1]);
    if (!same(world[0].expression->args[0].expression->args[0], NormalDetail::dot(arena, inner)))
        throw Refused("a world normal that is not a normalized vector");
    MaterialNormalResult out;
    bool interpolated = true;
    for (int c = 0; c < 3; ++c)
        if (!same(inner[c], source(arena, "TangentZ", Value::null(), c)))
            interpolated = false;
    if (interpolated)
        return out;
    bool tangent = true;
    for (int c = 0; c < 3; ++c)
        if (!is(inner[c], "COMP") || inner[c].expression->args.size() != 2 ||
            !is(inner[c].expression->args[0], "TOWORLD") ||
            !same(inner[c].expression->args[0], inner[0].expression->args[0]) ||
            !NormalDetail::number_is(inner[c].expression->args[1], c))
            tangent = false;
    if (tangent)
    {
        auto n = inner[0].expression->args[0].expression->args;
        if (n.size() != 3)
            throw Refused("a tangent normal that does not have three components", "normal width");
        if (NormalDetail::normalized(n))
        {
            std::vector<Value> pin;
            for (const auto& v : n)
                pin.push_back(v.expression->args[1]);
            if (same(n[0].expression->args[0].expression->args[0], NormalDetail::dot(arena, pin)))
            {
                out.pin = pin;
                out.has_pin = true;
                return out;
            }
        }
        out.pin = n;
        out.has_pin = true;
        return out;
    }
    out.pin = inner;
    out.has_pin = true;
    out.tangent_space = false;
    return out;
}
}
}

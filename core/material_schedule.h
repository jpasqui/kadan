// Exact backward component slice for material values scheduled after the
// lighting stop. Conditional writes, side effects and unbounded arrays refuse.
#pragma once
#include "material_anchor.h"
#include "material_symbolic.h"
#include "material_graph.h"
namespace P4MapKit
{
namespace Material
{
using MaterialComponent = std::pair<std::string, char>;
using MaterialComponents = std::set<MaterialComponent>;
struct MaterialScheduledAnchor
{
    MaterialAnchor ValueAnchor;
    std::vector<std::size_t> Indices;
    std::size_t ScheduledLine = 0;
};
inline MaterialComponents MaterialInstructionReads(const AnchorInstruction& line, const std::string& mask)
{
    MaterialComponents out;
    static const std::set<std::string> scalar = {"RSQ", "RCP", "SIN", "COS", "EX2", "LG2", "POW"};
    static const std::set<std::string> textures = {"TEX", "TXB", "TXL", "TXF", "TXD", "TXP", "TXFMS", "TXQ", "TXG"};
    static const std::regex handle("^handle\\((\\w+)(?:\\.([xyzw]))?\\)$");
    for (std::size_t at = 0; at < line.src.size(); ++at)
    {
        const auto& operand = line.src[at];
        std::smatch match;
        if (std::regex_match(operand.base, match, handle))
        {
            out.insert({match[1].str(), match[2].matched ? match[2].str()[0] : 'x'});
            continue;
        }
        if (operand.literal)
            continue;
        std::set<char> components;
        if (line.op == "DP2" || line.op == "DP3" || line.op == "DP4")
            for (int c = 0; c < line.op[2] - '0'; ++c)
                components.insert(operand.component(std::size_t(c)));
        else if (scalar.count(line.op) || (line.op == "DIV" && at == 1))
            components.insert(operand.component(0));
        else if (textures.count(line.op) || mask.empty())
            for (std::size_t c = 0; c < 4; ++c)
                components.insert(operand.component(c));
        else
            for (const auto c : mask)
            {
                const auto k = std::string("xyzw").find(c);
                if (k == std::string::npos)
                    throw Refused("invalid native dependency component", "material scheduled component");
                components.insert(operand.component(k));
            }
        for (const auto c : components)
            out.insert({operand.base, c});
    }
    static const std::regex indirect("\\[(\\w+)\\.([xyzw])\\]");
    for (std::sregex_iterator item(line.text.begin(), line.text.end(), indirect), end; item != end; ++item)
        out.insert({(*item)[1].str(), (*item)[2].str()[0]});
    static const std::regex condition("\\((?:NE|EQ|GT|GE|LT|LE)(?:\\.([xyzw]+))?\\)");
    auto head = line.text.substr(0, line.text.find(','));
    std::smatch match;
    if (std::regex_search(head, match, condition))
        for (const auto c : match[1].matched ? match[1].str() : "xyzw")
            out.insert({"CC", c});
    return out;
}
inline MaterialComponents MaterialDelayedReads(const std::vector<AnchorInstruction>& code,
                                               const AnchorInstruction& line, const std::string& mask, std::size_t at)
{
    auto out = MaterialInstructionReads(line, mask);
    const auto original = out;
    static const std::regex indirect("^([A-Za-z_]\\w*)\\[([A-Za-z_]\\w*)\\.([xyzw])\\]$");
    for (const auto& component : original)
    {
        std::smatch match;
        if (!std::regex_match(component.first, match, indirect))
            continue;
        const auto name = match[1].str();
        std::set<std::string> entries;
        const std::regex pattern("^" + name + "\\[([0-9]+)\\]$");
        for (std::size_t i = 0; i < at; ++i)
            if (std::regex_match(code[i].dst, pattern))
                entries.insert(code[i].dst);
        if (entries.size() > 64)
            throw Refused("delayed local array exceeds the finite slice limit: " + name,
                          "material scheduled array limit");
        if (entries.empty())
            throw Refused("delayed local array has no native initialization: " + name,
                          "material scheduled array initialization");
        for (const auto& entry : entries)
        {
            bool initialized = false;
            for (std::size_t i = 0; i < at; ++i)
                if (code[i].dst == entry && code[i].mask.find(component.second) != std::string::npos)
                    initialized = true;
            if (!initialized)
                throw Refused("delayed local array component has no native initialization: " + entry + "." +
                                  component.second,
                              "material scheduled array initialization");
        }
        out.erase(component);
        for (const auto& entry : entries)
            out.insert({entry, component.second});
    }
    return out;
}
inline MaterialScheduledAnchor BuildDelayedMaterialAnchor(const std::vector<AnchorInstruction>& code, std::size_t stop,
                                                          std::size_t at, const AnchorOperand& operand,
                                                          char component = 'w')
{
    if (code.size() > 100000 || stop > at || at > code.size() ||
        std::string("xyzw").find(component) == std::string::npos)
        throw Refused("invalid delayed material range", "material scheduled range");
    MaterialScheduledAnchor out;
    if (operand.literal)
    {
        out.ValueAnchor = MaterialAnchor::Constant(at, operand.vector()[std::string("xyzw").find(component)]);
        return out;
    }
    if (operand.negative || operand.absolute)
        throw Refused("output is not an unchanged component", "material scheduled transformed output");
    component = operand.component(std::string("xyzw").find(component));
    MaterialComponents live = {{operand.base, component}};
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
    for (std::size_t end = at; end > stop;)
    {
        const auto i = --end;
        const auto& line = code[i];
        std::string affected;
        for (const auto c : line.mask)
            if (live.count({line.dst, c}))
                affected.push_back(c);
        if (affected.empty())
            continue;
        if (depths[i] || line.text.substr(0, line.text.find(',')).find('(') != std::string::npos)
            throw Refused("late material has a conditional or loop write at line " + std::to_string(i),
                          "material scheduled conditional write");
        if (IsMember(line.op, {"ST", "STORE", "KIL", "RET", "IF", "REP"}))
            throw Refused("late material contains a side effect at line " + std::to_string(i),
                          "material scheduled side effect");
        out.Indices.push_back(i);
        for (const auto c : affected)
            live.erase({line.dst, c});
        const auto reads = MaterialDelayedReads(code, line, affected, i);
        live.insert(reads.begin(), reads.end());
    }
    std::reverse(out.Indices.begin(), out.Indices.end());
    out.ValueAnchor = MaterialAnchor::Register(stop, operand.base, std::string(1, component));
    if (!out.Indices.empty())
        out.ScheduledLine = out.Indices.back() + 1;
    return out;
}
inline std::vector<Value> ReadScheduledMaterialValue(const std::vector<std::string>& lines,
                                                     const MaterialScheduledAnchor& anchor,
                                                     const SymbolicMachine& prefix)
{
    if (lines != prefix.lines)
        throw Refused("scheduled material program differs from the prefix", "material scheduled program");
    if (anchor.ValueAnchor.has_constant)
        return {Value(anchor.ValueAnchor.constant)};
    auto later = prefix;
    std::size_t previous = 0;
    bool first = true;
    for (const auto at : anchor.Indices)
    {
        if (at >= lines.size() || (!first && at <= previous) || at < anchor.ValueAnchor.line)
            throw Refused("invalid native material schedule", "material scheduled range");
        later.step(at, lines[at]);
        previous = at;
        first = false;
    }
    const auto found = later.reg.find(anchor.ValueAnchor.register_name);
    if (found == later.reg.end())
        throw Refused("scheduled material register is absent", "material scheduled register");
    std::vector<Value> result;
    for (const auto c : anchor.ValueAnchor.comps)
    {
        const auto at = std::string("xyzw").find(c);
        if (at == std::string::npos)
            throw Refused("invalid native scheduled component", "material scheduled component");
        result.push_back(found->second[at]);
    }
    return result;
}
}
}

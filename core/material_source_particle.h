// Exact source preparation for Cascade pixel inputs. These stages identify
// original executable prefixes; they confer no family or renderer admission.
#pragma once
#include "material_prepare.h"
#include "material_factory.h"
#include "material_program.h"
#include "material_decompile.h"
#include "material_schedule.h"
namespace P4MapKit
{
namespace Material
{
inline bool MaterialParticleFactory(const std::string& name)
{
    return IsMember(name, {"FMeshParticleVertexFactory", "FParticleSpriteVertexFactory", "FGPUSpriteVertexFactory",
                           "FParticleBeamTrailVertexFactory"});
}
struct MaterialParticleInstructions
{
    std::vector<std::string> Lines;
    std::vector<std::size_t> OriginalLines, FlatDeclarations;
    std::size_t ExpandedLoops = 0;
};
inline MaterialParticleInstructions PrepareOriginalParticleInstructions(const std::vector<std::string>& raw)
{
    if (raw.empty() || raw.size() > 100000)
        throw Refused("invalid particle instruction count", "material particle program budget");
    MaterialParticleInstructions result;
    std::size_t bytes = 0;
    static const std::regex flat(
        "^FLAT ATTRIB fragment_attrib[0-9]*\\[\\] = \\{ fragment\\.attrib\\[[0-9]+\\.\\.[0-9]+\\] \\}$");
    for (std::size_t i = 0; i < raw.size(); ++i)
    {
        const auto& line = raw[i];
        if (line.size() > 65536 || line.size() > 64u * 1024u * 1024u - bytes)
            throw Refused("particle source byte budget exceeded", "material particle program budget");
        bytes += line.size();
        if (starts(line, "FLAT "))
        {
            if (!std::regex_match(line, flat))
                throw Refused("unknown NV flat declaration: " + line, "material particle flat declaration");
            result.FlatDeclarations.push_back(i);
        }
        else
        {
            result.Lines.push_back(line);
            result.OriginalLines.push_back(i);
        }
    }
    const std::vector<std::string> original = {"MOV.S R0.x, {0, 0, 0, 0}",
                                               "REP.S",
                                               "SEQ.U.CC HC.x, {1, 0, 0, 0}, {0, 0, 0, 0}",
                                               "BRK (NE.x)",
                                               "MAD.S R0.y, R0.x, {2, 0, 0, 0}.x, {1, 0, 0, 0}.x",
                                               "SGE.S R0.y, R0, {1, 0, 0, 0}.x",
                                               "MOV.U.CC RC.x, -R0.y",
                                               "IF NE.x",
                                               "MOV.U.CC RC.x, {1, 0, 0, 0}",
                                               "BRK (NE.x)",
                                               "ENDIF",
                                               "ADD.S R0.x, R0, {1, 0, 0, 0}",
                                               "ENDREP"};
    std::vector<std::vector<std::string>> variants = {original};
    auto z = original;
    for (auto& line : z)
    {
        std::size_t at = 0;
        while ((at = line.find("R0.y", at)) != std::string::npos)
        {
            line.replace(at, 4, "R0.z");
            at += 4;
        }
    }
    variants.push_back(z);
    for (const char counter : std::string("yz"))
    {
        auto other = original;
        const std::string r = "R0." + std::string(1, counter);
        other[0] = "MOV.S " + r + ", {0, 0, 0, 0}.x";
        other[4] = "MAD.S R0.x, " + r + ", {2, 0, 0, 0}, {1, 0, 0, 0}";
        other[5] = "SGE.S R0.x, R0, {1, 0, 0, 0}";
        other[6] = "MOV.U.CC RC.x, -R0";
        other[11] = "ADD.S " + r + ", R0, {1, 0, 0, 0}.x";
        variants.push_back(other);
    }
    for (std::size_t at = 0; at + original.size() <= result.Lines.size();)
    {
        const std::vector<std::string>* hit = nullptr;
        for (const auto& variant : variants)
            if (std::equal(variant.begin(), variant.end(), result.Lines.begin() + at))
            {
                hit = &variant;
                break;
            }
        if (!hit)
        {
            ++at;
            continue;
        }
        std::vector<std::string> expanded;
        std::vector<std::size_t> locations;
        for (const auto k : {0, 2, 4, 5, 6, 8})
        {
            expanded.push_back((*hit)[std::size_t(k)]);
            locations.push_back(result.OriginalLines[at + std::size_t(k)]);
        }
        result.Lines.erase(result.Lines.begin() + at, result.Lines.begin() + at + original.size());
        result.Lines.insert(result.Lines.begin() + at, expanded.begin(), expanded.end());
        result.OriginalLines.erase(result.OriginalLines.begin() + at,
                                   result.OriginalLines.begin() + at + original.size());
        result.OriginalLines.insert(result.OriginalLines.begin() + at, locations.begin(), locations.end());
        ++result.ExpandedLoops;
        at += expanded.size();
    }
    if (result.Lines.empty())
        throw Refused("flat declarations contain no executable particle code", "material particle program empty");
    return result;
}
struct MaterialParticleEarlyNormal
{
    MaterialAnchor Anchor;
    std::size_t ScheduledLine = 0;
};
inline bool FindOriginalParticleEarlyNormal(const std::vector<AnchorInstruction>& code, const MaterialAnchors& found,
                                            MaterialParticleEarlyNormal& output)
{
    const auto marker = found.find("stop"), normal = found.find("NormalWorld");
    if (marker == found.end())
        return false;
    const auto stop = marker->second.line;
    if (stop > code.size())
        throw Refused("particle material stop exceeds original code", "material particle anchor range");
    for (const auto* pin : {"Normal", "NormalDecal"})
    {
        const auto held = found.find(pin);
        if (held != found.end() && !held->second.has_constant && held->second.comps == "xyz" &&
            held->second.line < stop &&
            !AnchorWritten(code, held->second.register_name, "xyz", held->second.line + 1, stop))
            return false;
    }
    if (normal == found.end() || normal->second.has_constant || normal->second.line >= stop ||
        normal->second.comps != "xyz")
        return false;
    const auto at = normal->second.line;
    if (!AnchorWritten(code, normal->second.register_name, "xyz", at + 1, stop))
        return false;
    int depth = 0;
    for (std::size_t i = 0; i <= at; ++i)
    {
        if (code[i].op == "IF" || code[i].op == "REP")
            ++depth;
        else if (code[i].op == "ENDIF" || code[i].op == "ENDREP")
            --depth;
    }
    if (depth)
        return false;
    for (std::size_t i = at > 28 ? at - 28 : 0; i <= at; ++i)
        if (code[i].text.substr(0, code[i].text.find(',')).find('(') != std::string::npos)
            return false;
    const auto& final = code[at];
    if (final.op != "MUL" || final.mask != "xyz" || final.src.size() != 2)
        return false;
    bool proved = false;
    for (std::size_t side = 0; side < 2; ++side)
        for (std::size_t start = at > 28 ? at - 28 : 0; start < at; ++start)
        {
            MaterialAnchor check;
            if (AnchorNormalizedVector(code, final.reads(side, "xyz"), start, check) && check.line == at &&
                check.register_name == normal->second.register_name && check.comps == "xyz")
                proved = true;
        }
    if (!proved)
        return false;
    for (const auto c : std::string("xyz"))
    {
        bool read = false;
        for (std::size_t i = at + 1; i < code.size(); ++i)
        {
            const auto& line = code[i];
            for (const auto& operand : line.src)
                if (operand.base == normal->second.register_name &&
                    (operand.swizzle.empty() || operand.swizzle.find(c) != std::string::npos))
                {
                    read = true;
                    break;
                }
            if (read)
                break;
            if (line.dst == normal->second.register_name && line.mask.find(c) != std::string::npos)
                break;
        }
        if (!read)
            return false;
    }
    output.Anchor = normal->second;
    output.ScheduledLine = at + 1;
    return true;
}
inline MaterialBoundOutputs RecoverOriginalParticleEarlyNormal(const MaterialRecoveryPlan& input,
                                                               const MaterialParticleEarlyNormal& normal)
{
    if (normal.ScheduledLine != normal.Anchor.line + 1 || normal.ScheduledLine > input.Lines.size() ||
        normal.Anchor.has_constant || normal.Anchor.comps != "xyz")
        throw Refused("invalid original particle early normal checkpoint", "material particle early normal");
    auto plan = input;
    plan.Stop = normal.ScheduledLine;
    plan.Outputs.clear();
    MaterialOutputRegister output;
    output.Pin = "NormalWorld";
    output.Register = normal.Anchor.register_name;
    output.Components = {0, 1, 2};
    plan.Outputs.push_back(output);
    return BindMaterialOutputs(plan);
}
}
} // P4MapKit::Material

// Original NVASM descriptor-cache dataflow. The native program's buf14 cache
// is separate from the renderer uniform buffers and their SRT buffer ordinals.
// This adapter retains the strict historical registry API unchanged.
#pragma once
#include "material_prepare.h"
#include "material_scene_native_registry.h"
namespace P4MapKit
{
namespace Material
{
inline MaterialNativeSceneSamplerFacts
ObserveOriginalMaterialSceneCopySampler(const std::vector<std::string>& originalLines,
                                        const MaterialSceneResourcePolicy& policy,
                                        int originalHeaderDefaultSampler = -1)
{
    MaterialNativeSceneSamplerFacts facts;
    const auto copy = policy.Textures.find("scene_color_copy");
    if (copy == policy.Textures.end())
        return facts;
    const auto program = NormalizeMaterialTraceDeclarations(originalLines);
    NativeSceneDetail::Registers registers;
    struct Branch
    {
        NativeSceneDetail::Registers Before, Then;
        bool HasElse = false;
    };
    std::vector<Branch> branches;
    for (std::size_t line = 0; line < program.Lines.size(); ++line)
    {
        const auto& text = program.Lines[line];
        const auto space = text.find(' ');
        const auto head = text.substr(0, space);
        const auto dot = head.find('.');
        const auto op = head.substr(0, dot);
        const auto args = split_operands(space == std::string::npos ? "" : text.substr(space + 1));
        if (op == "REP")
        {
            // SceneColorCopy's constructor member is Texture2D. A bounded
            // lighting loop with only explicit BUFFER/3D/ARRAYCUBE consumers
            // cannot consume that role. Do not export any handle through the
            // loop; later consumers must establish their producers again.
            unsigned depth = 1;
            std::size_t end = line + 1;
            for (; end < program.Lines.size() && depth; ++end)
            {
                const auto& body = program.Lines[end];
                const auto split = body.find(' ');
                const auto operation = body.substr(0, body.find_first_of(". "));
                if (operation == "REP")
                    ++depth;
                else if (operation == "ENDREP")
                    --depth;
                else if (IsMember(operation, {"CAL", "RET", "BRA"}))
                    throw Refused("original loop has unproved handle control flow", "material original scene loop");
                else if (IsMember(operation, {"TEX", "TXB", "TXL", "TXF", "TXD", "TXP", "TXFMS", "TXQ", "TXG"}))
                {
                    const auto operands = split_operands(split == std::string::npos ? "" : body.substr(split + 1));
                    if (operands.empty() || !IsMember(operands.back(), {"BUFFER", "3D", "ARRAYCUBE"}))
                        throw Refused("original lighting loop may consume a 2D scene resource",
                                      "material original scene loop");
                }
            }
            if (depth)
                throw Refused("original handle loop has no terminator", "material original scene loop");
            registers.clear();
            line = end - 1;
            continue;
        }
        if (op == "IF")
        {
            branches.push_back({registers, {}, false});
            continue;
        }
        if (op == "ELSE")
        {
            if (branches.empty() || branches.back().HasElse)
                throw Refused("unpaired scene handle branch", "material native scene handle");
            branches.back().Then = registers;
            branches.back().HasElse = true;
            registers = branches.back().Before;
            continue;
        }
        if (op == "ENDIF")
        {
            if (branches.empty())
                throw Refused("unpaired scene handle branch", "material native scene handle");
            const auto branch = branches.back();
            branches.pop_back();
            registers = NativeSceneDetail::merge(branch.HasElse ? branch.Then : branch.Before, registers);
            continue;
        }
        if (IsMember(op, {"REP", "ENDREP", "BRK", "CAL", "RET", "BRA"}))
            throw Refused("scene handle control flow is not proved", "material native scene handle");
        NativeSceneDetail::Handles values;
        if (op == "LDC" && head.find(".U64") != std::string::npos && args.size() == 2)
        {
            static const std::regex word("^buf([0-9]+)\\[([0-9]+)\\]$");
            std::smatch match;
            if (std::regex_match(args[1], match, word))
            {
                const auto buffer = MaterialDeclarationIndex(match[1]), offset = MaterialDeclarationIndex(match[2]);
                if (buffer <= 31 && offset <= 4096)
                    for (auto& value : values)
                    {
                        value.Known = true;
                        value.Words = {{int(buffer), int(offset)}};
                    }
            }
        }
        else if (op == "MOV" && head.find(".SAT") == std::string::npos && args.size() == 2)
            values = NativeSceneDetail::read(registers, args[1]);
        else if (op == "OR" && head.find(".U64") != std::string::npos && args.size() == 3)
        {
            const auto a = NativeSceneDetail::read(registers, args[1]), b = NativeSceneDetail::read(registers, args[2]);
            for (int c = 0; c < 4; ++c)
                if (a[c].Known && b[c].Known)
                {
                    values[c].Known = true;
                    values[c].Words = a[c].Words;
                    values[c].Words.insert(values[c].Words.end(), b[c].Words.begin(), b[c].Words.end());
                    std::sort(values[c].Words.begin(), values[c].Words.end());
                    values[c].Words.erase(std::unique(values[c].Words.begin(), values[c].Words.end()),
                                          values[c].Words.end());
                }
        }
        else if (IsMember(op, {"TEX", "TXB", "TXL", "TXF", "TXD", "TXP", "TXFMS", "TXQ", "TXG"}))
        {
            if (args.size() < 3)
                throw Refused("texture instruction is incomplete", "material native scene handle");
            static const std::regex handle("^handle\\(([A-Za-z_]\\w*)(?:\\.([xyzw]))?\\)$");
            std::smatch match;
            std::size_t handleAt = 0;
            for (std::size_t a = 2; a < args.size(); ++a)
            {
                std::smatch candidate;
                if (std::regex_match(args[a], candidate, handle))
                {
                    if (handleAt)
                        throw Refused("texture instruction has several handle operands",
                                      "material original scene cache");
                    handleAt = a;
                    match = candidate;
                }
            }
            if (!handleAt)
                throw Refused("texture instruction has no immutable handle operand", "material original scene cache");
            const auto name = match[1].str() + "." + (match[2].matched ? match[2].str() : "x");
            const auto originalHandle = NativeSceneDetail::read(registers, name)[0];
            NativeSceneDetail::Handle canonicalHandle = originalHandle;
            for (auto& word : canonicalHandle.Words)
            {
                if (word.first != 14)
                    throw Refused("texture producer is outside original buf14 cache", "material original scene cache");
                word.first = 0;
            }
            const auto slots = NativeSceneDetail::cache(canonicalHandle);
            if (slots.first == copy->second)
            {
                if (op != "TEX" || (handleAt != 2 || args.size() != 4 || args[3] != "2D"))
                    throw Refused("scene copy uses an unproved sampling operation",
                                  "material native scene copy sampler");
                int sampler = slots.second;
                if (sampler < 0)
                {
                    sampler = originalHeaderDefaultSampler;
                    facts.UsesHeaderDefault = true;
                }
                std::vector<std::string> roles;
                for (const auto& candidate : policy.Samplers)
                    if (candidate.second == sampler && IsMember(candidate.first, {"view_clamp", "copy", "point"}))
                        roles.push_back(candidate.first);
                if (roles.size() != 1 || sampler < 0 || sampler > 31)
                    throw Refused("observed scene copy sampler lacks one original role",
                                  "material native scene copy sampler");
                if (facts.CopySamplerSlot >= 0 &&
                    (facts.CopySamplerSlot != sampler || facts.CopySamplerRole != roles[0]))
                    throw Refused("scene copy consumers use different native samplers",
                                  "material native scene copy sampler");
                facts.CopySamplerSlot = sampler;
                facts.CopySamplerRole = roles[0];
                facts.CopyConsumerLines.push_back(program.OriginalLines[line]);
            }
        }
        if (!args.empty())
            NativeSceneDetail::write(registers, args[0], values);
    }
    if (!branches.empty())
        throw Refused("scene handle branch has no terminator", "material native scene handle");
    return facts;
}
inline MaterialNativeSceneResources BindOriginalMaterialSceneResources(const MaterialPreparedProgram& program,
                                                                       const Json& settings,
                                                                       bool originalAssociationProved)
{
    MaterialNativeSceneResources out;
    out.Policy = ReadMaterialNativeSceneResourcePolicy(program.Reference, program.Header, settings.At("domain").Text(),
                                                       settings.At("blend_mode").Text(), originalAssociationProved);
    out.SamplerFacts = ObserveOriginalMaterialSceneCopySampler(program.Lines, out.Policy.ResourcePolicy,
                                                               program.Header.default_sampler);
    out.Factory.RolesProved = true;
    for (const auto& resource : out.Policy.ResourcePolicy.Textures)
    {
        if (resource.first == "scene_color_copy" && out.SamplerFacts.CopySamplerSlot < 0)
            continue;
        out.Factory.TextureRoles[resource.first] = Value(resource.second);
    }
    if (out.SamplerFacts.CopySamplerSlot >= 0)
    {
        out.SamplerFacts.CopySamplerIdentity =
            "original-sampler-slot:" + std::to_string(out.SamplerFacts.CopySamplerSlot);
        out.Factory.CopySampler = out.SamplerFacts.CopySamplerRole;
    }
    return out;
}
}
} // P4MapKit::Material

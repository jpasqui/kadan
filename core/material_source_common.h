// Helpers every original-source producer shares: a program's text and digest, a payload digest, the nesting depth
// of a program's control blocks, a shader's archive index, and the evidence flags a recovery reports.
#pragma once
#include "custom_material_hash.h"
#include "material_anchor.h"
#include "material_graph.h"
#include "material_shader_map.h"
#include <string>
#include <vector>
namespace P4MapKit
{
namespace Material
{
// A program's lines joined by newlines, as its recorded digests were taken: a line follows a newline only when
// some text came before it.
inline std::string ProgramText(const std::vector<std::string>& lines)
{
    std::string text;
    for (const auto& line : lines)
    {
        if (!text.empty())
            text += '\n';
        text += line;
    }
    return text;
}
// SHA-256 of a text's bytes, lower-case hex.
inline std::string TextDigest(const std::string& text)
{
    return CustomMaterial::Hex(CustomMaterial::Sha256(CustomMaterial::ByteText(text)));
}
// SHA-256 of ProgramText, lower-case hex.
inline std::string ProgramDigest(const std::vector<std::string>& lines)
{
    return TextDigest(ProgramText(lines));
}
// SHA-256 of an archive payload, lower-case hex.
inline std::string PayloadDigest(const CustomMaterial::Bytes& payload)
{
    return CustomMaterial::Hex(CustomMaterial::Sha256(payload));
}
// The IF/REP nesting depth of every instruction (ENDIF and ENDREP count at the depth they close to), and the
// depth after the last one in end.
inline std::vector<int> MaterialControlDepths(const std::vector<AnchorInstruction>& code, int* end = nullptr)
{
    int depth = 0;
    std::vector<int> out;
    for (const auto& line : code)
    {
        if (line.op == "ENDIF" || line.op == "ENDREP")
            --depth;
        out.push_back(depth);
        if (line.op == "IF" || line.op == "REP")
            ++depth;
    }
    if (end)
        *end = depth;
    return out;
}
// The archive index of a shader of a map (indices: the archive's indices of the map, in resource order), refused
// with the caller's reason when the reference's resource index is outside the map.
inline std::uint32_t ArchiveIndexOf(const std::vector<std::uint32_t>& indices, const FMaterialShaderReference& ref,
                                    const char* reason, const char* name)
{
    if (ref.ResourceIndex < 0 || std::size_t(ref.ResourceIndex) >= indices.size())
        throw Refused(reason, name);
    return indices[std::size_t(ref.ResourceIndex)];
}
// What a source recovery has proven beyond its graph. A producer sets none of these today.
struct MaterialEvidence
{
    bool FamilyComplete = false;
    bool CompilerProven = false;
    bool RendererProven = false;
    bool ProductionAdmitted = false;
};
}
} // P4MapKit::Material

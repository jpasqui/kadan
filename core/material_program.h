// Trace preparation keeps raw source identity separate from executable lines.
// NV STORAGE declarations allocate a binding name; they execute no operation.
#pragma once
#include "material_graph.h"
#include <cstdint>
#include <limits>
#include <vector>
namespace P4MapKit
{
namespace Material
{
struct MaterialStorageDeclaration
{
    std::string Name;
    std::uint32_t Binding = 0;
    std::size_t OriginalLine = 0;
};
struct MaterialTraceProgram
{
    std::vector<std::string> Lines;
    std::vector<std::size_t> OriginalLines;
    std::vector<MaterialStorageDeclaration> Storage;
};
inline std::uint32_t MaterialDeclarationIndex(const std::string& text)
{
    if (text.empty() || text.size() > 10 || text.find_first_not_of("0123456789") != std::string::npos)
        throw Refused("invalid NV storage declaration index", "material storage declaration");
    std::uint64_t value = 0;
    for (const auto c : text)
    {
        value = value * 10 + std::uint64_t(c - '0');
        if (value > std::numeric_limits<std::uint32_t>::max())
            throw Refused("NV storage declaration index overflows", "material storage declaration");
    }
    return std::uint32_t(value);
}
inline MaterialTraceProgram NormalizeMaterialTraceDeclarations(const std::vector<std::string>& raw)
{
    if (raw.empty() || raw.size() > 100000)
        throw Refused("invalid material trace preparation range", "material program budget");
    MaterialTraceProgram result;
    std::size_t bytes = 0;
    const std::string prefix = "STORAGE sbo_buf", middle = "[] = { program.storage[", suffix = "] }";
    for (std::size_t i = 0; i < raw.size(); ++i)
    {
        const auto& line = raw[i];
        if (line.size() > 65536 || line.size() > 64u * 1024u * 1024u - bytes)
            throw Refused("material trace preparation byte limit", "material program budget");
        bytes += line.size();
        if (line.compare(0, 7, "STORAGE") == 0)
        {
            if (line.size() > 256 || line.compare(0, prefix.size(), prefix) != 0 ||
                line.size() < prefix.size() + middle.size() + suffix.size() + 2 ||
                line.compare(line.size() - suffix.size(), suffix.size(), suffix) != 0)
                throw Refused("unrecognized NV storage declaration", "material storage declaration");
            const auto at = line.find(middle, prefix.size());
            if (at == std::string::npos)
                throw Refused("unrecognized NV storage declaration", "material storage declaration");
            const auto nameIndex = line.substr(prefix.size(), at - prefix.size());
            MaterialDeclarationIndex(nameIndex);
            const auto bindingStart = at + middle.size();
            const auto binding = line.substr(bindingStart, line.size() - suffix.size() - bindingStart);
            result.Storage.push_back({"sbo_buf" + nameIndex, MaterialDeclarationIndex(binding), i});
            continue;
        }
        result.Lines.push_back(line);
        result.OriginalLines.push_back(i);
    }
    if (result.Lines.empty())
        throw Refused("storage declarations do not form an executable program", "material program empty");
    return result;
}
}
} // P4MapKit::Material

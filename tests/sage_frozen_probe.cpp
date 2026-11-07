// Runs Kadan's 4.26 package and material shader map readers on a 4.27 (Switch 2) material instance converted by
// retoc to-legacy: where they hold and where they stop (docs/SWITCH2_SHADERS.md, the 4.27 frozen content).
//   sage_frozen_probe <package.uasset> <package.uexp>
#include "../core/material_resource_boundary.h"
#include "../core/material_shader_map.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>

static std::vector<std::uint8_t> Read(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) return 2;
    const auto head = Read(argv[1]), body = Read(argv[2]);
    try {
        const auto header = P4MapKit::ReadPackageHeader(head, body.size());
        std::cout << "exports " << header.Exports.size() << "\n";
        for (std::size_t i = 0; i < header.Exports.size(); ++i) {
            P4MapKit::FMaterialResourceBoundary boundary;
            P4MapKit::FReadError error;
            if (!P4MapKit::DiscoverMaterialResourceBoundary(header, body, i, boundary, error)) {
                std::cout << "export " << i << " " << header.Exports[i].Name << ": no boundary: " << error.Code << " at " << error.Offset << "\n";
                continue;
            }
            std::cout << "export " << i << " " << header.Exports[i].Name << ": resource at " << boundary.ResourceStart << "\n";
            P4MapKit::FMaterialShaderMapEnvelope map;
            if (!P4MapKit::ReadMaterialShaderMapEnvelope(body, boundary.ResourceStart, boundary.ExportEnd, map, error)) {
                std::cout << "  envelope: " << error.Code << " at " << error.Offset << "\n";
                continue;
            }
            if (argc == 4) { std::ofstream o(argv[3], std::ios::binary); o.write(reinterpret_cast<const char*>(map.Frozen.data()), std::streamsize(map.Frozen.size())); }
            std::cout << "  envelope ok: frozen " << map.Frozen.size() << " bytes, quality " << map.Quality << " feature " << map.Feature << "\n";
            P4MapKit::FMaterialUniformSet uniforms;
            if (!P4MapKit::ReadMaterialUniformSet(map.Frozen, map.ScriptNames, uniforms, error))
                std::cout << "  uniform set: " << error.Code << " at " << error.Offset << "\n";
            else
                std::cout << "  uniform set: " << uniforms.Scalars.size() << " scalars, " << uniforms.Vectors.size() << " vectors, "
                          << uniforms.Textures.size() << " textures, " << uniforms.ScalarPrograms.size() << " scalar programs\n";
            for (const auto& t : uniforms.Textures) std::cout << "    texture parameter " << t.Parameter.Name << "\n";
            std::vector<P4MapKit::FMaterialShaderReference> refs;
            if (!P4MapKit::ReadMaterialShaderReferences(map, refs, error))
                std::cout << "  shader references: " << error.Code << " at " << error.Offset << "\n";
            else {
                std::cout << "  shader references: " << refs.size() << "\n";
                if (std::getenv("PROBE_REFS"))
                    for (const auto& r : refs)
                        std::printf("    ref type %016llx vf %016llx freq %u perm %d target %08x\n", (unsigned long long)r.TypeHash,
                                    (unsigned long long)r.VertexFactoryHash, unsigned(r.Frequency), r.Permutation,
                                    unsigned(map.Frozen[r.FrozenOffset + 192] | map.Frozen[r.FrozenOffset + 193] << 8 |
                                             map.Frozen[r.FrozenOffset + 194] << 16 | unsigned(map.Frozen[r.FrozenOffset + 195]) << 24));
            }
        }
    } catch (const std::exception& e) {
        std::cout << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

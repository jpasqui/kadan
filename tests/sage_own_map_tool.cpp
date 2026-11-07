// core/sage_own_map.h offline: converts a creator material's own map (block and library) for the Switch 2 builds and
// reads the result back with Kadan's map readers.
//   sage_own_map_tool <block.bin | material.uasset> <switch library> [out folder]
// A .uasset names its .uexp beside it; the block is the material's inline map there. With a folder, writes
// block.bin and ShaderArchive-Kadan-SF_GLSL_SAGE.ushaderbytecode there.
// Build against the staged compiler (native_custom_backend_sources.stage_core):
//   clang++ -std=c++17 -I core -I <staged> tests/sage_own_map_tool.cpp <staged>/KadanMaxwell/*.cpp
#include "sage_own_map.h"
#include "own_map_build.h"
#include "material_resource_boundary.h"
#include <fstream>
#include <iostream>
#include <iterator>

using namespace P4MapKit::CustomMaterial;
using OwnSage::Bytes;

static Bytes Load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path);
    return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
static void Save(const std::string& path, const Bytes& b) {
    std::ofstream o(path, std::ios::binary);
    o.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
}

// The material's inline map block in its .uexp.
static Bytes BlockOf(const std::string& uasset) {
    const auto head = Load(uasset), body = Load(uasset.substr(0, uasset.size() - 7) + ".uexp");
    const auto header = P4MapKit::ReadPackageHeader(head, body.size());
    for (std::size_t i = 0; i < header.Exports.size(); ++i) {
        P4MapKit::FMaterialResourceBoundary boundary;
        P4MapKit::FReadError error;
        if (!P4MapKit::DiscoverMaterialResourceBoundary(header, body, i, boundary, error)) continue;
        Bytes tail(body.begin() + std::ptrdiff_t(boundary.ResourceStart), body.begin() + std::ptrdiff_t(boundary.ExportEnd));
        const auto range = OwnSage::FrozenRange(tail);
        // After the frozen bytes: patches, types, dependencies and the resource hash; the reader knows the end.
        P4MapKit::FMaterialShaderMapEnvelope map;
        if (!P4MapKit::ReadMaterialShaderMapEnvelope(body, boundary.ResourceStart, boundary.ExportEnd, map, error))
            throw std::runtime_error("map envelope: " + error.Code);
        const auto at = std::search(tail.begin() + std::ptrdiff_t(range.first + range.second), tail.end(),
                                    map.ResourceHash.begin(), map.ResourceHash.end());
        if (at == tail.end()) throw std::runtime_error("no resource hash after the frozen map");
        tail.erase(at + 20, tail.end());
        return tail;
    }
    throw std::runtime_error("no material map in " + uasset);
}

static void Check(const Bytes& block, std::size_t programs, std::uint32_t platform) {
    P4MapKit::FMaterialShaderMapEnvelope map;
    P4MapKit::FReadError error;
    if (!P4MapKit::ReadMaterialShaderMapEnvelope(block, 0, block.size(), map, error))
        throw std::runtime_error("converted envelope: " + error.Code);
    std::vector<P4MapKit::FMaterialShaderReference> refs;
    if (!P4MapKit::ReadMaterialShaderReferences(map, refs, error)) throw std::runtime_error("converted refs: " + error.Code);
    std::size_t ok = 0;
    for (const auto& r : refs) {
        const std::uint32_t target = OwnSage::Get32(&map.Frozen[r.FrozenOffset + 192]);
        if ((target >> 4) == platform && r.ResourceIndex >= 0 && std::size_t(r.ResourceIndex) < programs) ++ok;
    }
    std::cout << "  " << refs.size() << " shaders, " << ok << " with platform " << platform << " and a program; root platform "
              << unsigned(map.Frozen[88]) << "\n";
    if (ok != refs.size() || map.Frozen[88] != platform) throw std::runtime_error("converted map check failed");
}

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) { std::cerr << "sage_own_map_tool <block.bin | material.uasset> <library> [out]\n"; return 2; }
    try {
        const std::string first = argv[1];
        const Bytes block = first.size() > 7 && first.substr(first.size() - 7) == ".uasset" ? BlockOf(first) : Load(first);
        const Bytes data = Load(argv[2]);
        const auto library = OwnSage::ReadArchive(data);
        if (library.maps.size() != 1) throw std::runtime_error("a library with one map expected");
        std::vector<Bytes> programs;
        for (auto s : library.MapShaders(0)) programs.push_back(library.Payload(s));
        const Bytes mapHash(library.map_hashes[0].begin(), library.map_hashes[0].end());
        std::cout << "switch: " << programs.size() << " programs\n";
        Check(block, programs.size(), OwnSage::SwitchShaderPlatform);
        const auto sage = OwnSage::ConvertOwnMapToSage(block, mapHash, programs);
        std::cout << "sage: map " << Hex(sage.map_hash) << "\n";
        Check(sage.block, sage.programs.size(), OwnSage::SageShaderPlatform);
        const auto out = OwnMap::Library({{sage.map_hash, sage.programs}});
        const auto back = OwnSage::ReadArchive(out);
        for (std::size_t k = 0; k < sage.programs.size(); ++k) (void)OwnSage::ReadPayload(back.Payload(back.MapShaders(0)[k]));
        std::cout << "  library " << out.size() << " bytes, every payload reads back\n";
        if (argc == 4) {
            Save(std::string(argv[3]) + "/block.bin", sage.block);
            Save(std::string(argv[3]) + "/ShaderArchive-Kadan-SF_GLSL_SAGE.ushaderbytecode", out);
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}

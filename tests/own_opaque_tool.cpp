// Stage 5 of tools/navigation/OWN_SHADER_MAPS.md: Kadan's own lit opaque pixel programs, outside Unreal.
//   own_opaque_tool check <folder of the game's lit opaque pixel payloads, <class>.payload> <material layout hash>
//   own_opaque_tool build <graph.json> <out folder> [material layout hash]
//   own_opaque_tool compare <graph.json> <converted game packages, Carrot4/Content> <game shader archive>
// check: each policy's binding description, written for one material texture, against the game program's header,
// byte for byte. build: every policy's program for a creator graph, as <class>.payload. compare: our programs
// against the ones Kadan 0.2.0 makes for the same graph on its opaque donor (MI_stg_Area001_BayWindow01).
#include "../core/own_opaque.h"
#include <fstream>
#include <iostream>
#include <sstream>
using namespace P4MapKit::CustomMaterial;

static Bytes ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot read " + path);
    std::ostringstream s; s << f.rdbuf(); const auto text = s.str();
    return Bytes(text.begin(), text.end());
}
static void WriteFile(const std::string& path, const Bytes& bytes) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    if (!f) throw std::runtime_error("cannot write " + path);
}

int main(int argc, char** argv) { try {
    if (argc >= 4 && std::string(argv[1]) == "check") {
        const auto layout = std::uint32_t(std::stoul(argv[3]));
        unsigned same = 0;
        for (const auto& p : OwnOpaque::Policies()) {
            const auto game = ReadFile(std::string(argv[2]) + "/" + p.name + ".payload");
            const auto ours = ShaderHeader(OwnOpaque::OneTextureBindings(p, layout));
            const bool equal = game.size() >= ours.size() && std::equal(ours.begin(), ours.end(), game.begin());
            same += equal;
            if (!equal) WriteFile(std::string(argv[2]) + "/" + p.name + ".ours-header", ours);
            std::cout << p.name << ": header " << ours.size() << " bytes, " << (equal ? "identical" : "DIFFERENT") << "\n";
        }
        std::cout << same << " of " << OwnOpaque::Policies().size() << " identical\n";
        return same == OwnOpaque::Policies().size() ? 0 : 1;
    }
    if (argc >= 4 && std::string(argv[1]) == "build") {
        const auto bytes = ReadFile(argv[2]);
        const auto graph = Json::Parse(std::string(bytes.begin(), bytes.end()));
        const auto textures = OwnOpaque::GraphTextures(graph);
        const auto uniforms = OwnOpaque::Uniforms(graph, textures);
        const auto layout = OwnOpaque::MaterialLayoutHash(uniforms);
        // build <graph> <out> instanced: for the instanced static mesh vertex factory (inputs one location later)
        const bool instanced = argc > 4 && std::string(argv[4]) == "instanced";
        for (const auto& program : OwnOpaque::Compile(graph, {}, textures, layout, instanced)) {
            WriteFile(std::string(argv[3]) + "/" + program.policy->name + ".payload", program.compiled.payload);
            std::cout << program.policy->name << ": " << program.compiled.code.size() << " bytes of code, "
                      << program.compiled.registers << " registers, payload " << program.compiled.payload.size() << "\n";
        }
        // What the map's uniform expression set is written from (own_uniforms.py), in the order the programs read it.
        const auto text = OwnOpaque::UniformFacts(uniforms);
        WriteFile(std::string(argv[3]) + "/uniforms.json", Bytes(text.begin(), text.end()));
        std::cout << "Material layout hash " << layout << ", " << textures.size() << " textures, "
                  << uniforms.vectors.size() << " vector and " << uniforms.scalars.size() << " scalar parameters\n";
        return 0;
    }
    if (argc >= 5 && std::string(argv[1]) == "compare") {
        const auto bytes = ReadFile(argv[2]);
        const auto graph = Json::Parse(std::string(bytes.begin(), bytes.end()));
        const std::string root = argv[3];
        const auto load = [&](const std::string& package, NativeBackend::TranslucentSource& source) {
            const auto base = root + "/" + package.substr(6);
            source.header = ReadFile(base + ".uasset"); source.body = ReadFile(base + ".uexp"); return true;
        };
        const std::string donor = "/Game/Carrot4/Terrain/Area/Area001/Kakiwari/stg_Area001_BayWindow/MI_stg_Area001_BayWindow01";
        NativeBackend::TranslucentSource selected; load(donor, selected);
        const auto archive_bytes = ReadFile(argv[4]);
        const NativeBackend::ShaderArchive archive(archive_bytes);
        const auto old = NativeBackend::CompileCreatorOpaque(graph, "/Game/Mods/compare/M_Compare.M_Compare", donor, selected, load, archive);
        std::map<std::uint64_t, Bytes> donor_programs;
        for (const auto& shader : old.compiled.material.shaders)
            for (const auto& ref : old.compiled.source_references)
                if (ref.ResourceIndex == std::int32_t(shader.place) && ref.Frequency == 3 && ref.VertexFactoryHash == OwnOpaque::kLocalVertexFactory)
                    donor_programs[ref.TypeHash] = shader.payload;
        unsigned same = 0, total = 0;
        // the Material buffer layout of a material without parameters: two samplers, 16 bytes (own_material.py)
        for (const auto& program : OwnOpaque::Compile(graph, {}, {}, 0x1800ffu)) {
            ++total;
            const auto found = donor_programs.find(program.policy->type_hash);
            if (found == donor_programs.end()) { std::cout << program.policy->name << ": no 0.2.0 program\n"; continue; }
            const bool equal = found->second == program.compiled.payload;
            const auto donor_header = ShaderHeader(ReadPixelBindings(found->second));
            const auto own_header = ShaderHeader(program.bindings);
            const bool body = Bytes(found->second.begin() + donor_header.size(), found->second.end()) ==
                              Bytes(program.compiled.payload.begin() + own_header.size(), program.compiled.payload.end());
            same += equal;
            if (!equal) { WriteFile(program.policy->name + std::string(".donor-header"), donor_header);
                          WriteFile(program.policy->name + std::string(".own-header"), own_header); }
            std::cout << program.policy->name << ": payload " << (equal ? "identical" : "different")
                      << ", code and control " << (body ? "identical" : "DIFFERENT") << "\n";
        }
        std::cout << same << " of " << total << " payloads identical to 0.2.0's\n";
        return 0;
    }
    std::cerr << "usage: own_opaque_tool check <folder> <layout> | build <graph.json> <out> [layout]\n";
    return 2;
} catch (const std::exception& error) { std::cerr << "error: " << error.what() << "\n"; return 1; } }

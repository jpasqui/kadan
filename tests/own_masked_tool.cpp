// Stage 7 of tools/navigation/OWN_SHADER_MAPS.md: Kadan's own masked lit pixel programs, outside Unreal.
//   own_masked_tool check <folder of the game's mask pass payloads> <material layout hash>
//   own_masked_tool build <graph.json> <out folder>
// check: each mask pass's binding description, written for one material texture, against the game program's header
// (file names as dump_material_shaders.py writes them; the interpolants are the game program's for this comparison).
// build: the lit base pass programs (OwnOpaque, clipping on the mask) and the four mask pass programs for a masked
// creator graph, as <class>.payload, and uniforms.json (own_uniforms.py).
#include "../core/own_masked.h"
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
static std::string FileName(std::string name) {
    for (auto& c : name) { if (c == '<') c = '('; else if (c == '>') c = ')'; }
    return name;
}

int main(int argc, char** argv) { try {
    if (argc >= 4 && std::string(argv[1]) == "check") {
        const auto layout = std::uint32_t(std::stoul(argv[3]));
        unsigned same = 0;
        for (const auto& p : OwnMasked::Passes()) {
            const auto game = ReadFile(std::string(argv[2]) + "/" + FileName(p.name) + ".payload");
            auto bindings = OwnMasked::OneTextureBindings(p, layout);
            bindings.inputs = ReadPixelBindings(game).inputs;
            const auto ours = ShaderHeader(bindings);
            const bool equal = game.size() >= ours.size() && std::equal(ours.begin(), ours.end(), game.begin());
            same += equal;
            if (!equal) WriteFile(std::string(argv[2]) + "/" + FileName(p.name) + ".ours-header", ours);
            std::cout << p.name << ": header " << ours.size() << " bytes, " << (equal ? "identical" : "DIFFERENT") << "\n";
        }
        std::cout << same << " of " << OwnMasked::Passes().size() << " identical\n";
        return same == OwnMasked::Passes().size() ? 0 : 1;
    }
    if (argc >= 4 && std::string(argv[1]) == "build") {
        const auto bytes = ReadFile(argv[2]);
        const auto graph = Json::Parse(std::string(bytes.begin(), bytes.end()));
        const auto textures = OwnOpaque::GraphTextures(graph);
        const auto uniforms = OwnOpaque::Uniforms(graph, textures);
        const auto layout = OwnOpaque::MaterialLayoutHash(uniforms);
        for (const auto& program : OwnOpaque::Compile(graph, {}, textures, layout)) {
            WriteFile(std::string(argv[3]) + "/" + program.policy->name + ".payload", program.compiled.payload);
            std::cout << program.policy->name << ": " << program.compiled.code.size() << " bytes of code\n";
        }
        for (const auto& program : OwnMasked::Compile(graph, textures, layout)) {
            WriteFile(std::string(argv[3]) + "/" + FileName(program.pass->name) + ".payload", program.compiled.payload);
            std::cout << program.pass->name << ": " << program.compiled.code.size() << " bytes of code, payload "
                      << program.compiled.payload.size() << "\n";
        }
        const auto text = OwnOpaque::UniformFacts(uniforms);
        WriteFile(std::string(argv[3]) + "/uniforms.json", Bytes(text.begin(), text.end()));
        std::cout << "Material layout hash " << layout << ", " << textures.size() << " textures\n";
        return 0;
    }
    std::cerr << "usage: own_masked_tool check <folder> <layout> | build <graph.json> <out>\n";
    return 2;
} catch (const std::exception& error) { std::cerr << "error: " << error.what() << "\n"; return 1; } }

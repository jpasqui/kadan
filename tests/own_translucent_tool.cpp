// Stage 6 of tools/navigation/OWN_SHADER_MAPS.md: Kadan's own lit translucent pixel programs, outside Unreal.
//   own_translucent_tool check <folder of the game's translucent Surface pixel payloads, <class>.payload> <material layout hash>
//   own_translucent_tool build <graph.json> <out folder>
//   own_translucent_tool check-unlit <folder> <layout> | build-unlit <graph.json> <out folder>   (unlit translucent)
// check: each policy's binding description, written for one material texture, against the game program's header,
// byte for byte (the interpolants are the vertex shader's, so the game program's are used for this comparison).
// build: every policy's program for a creator graph, as <class>.payload, and uniforms.json (own_uniforms.py).
#include "../core/own_translucent.h"
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
        for (const auto& p : OwnTranslucent::Policies()) {
            const auto game = ReadFile(std::string(argv[2]) + "/" + p.name + ".payload");
            auto bindings = OwnTranslucent::OneTextureBindings(p, layout);
            bindings.inputs = ReadPixelBindings(game).inputs;
            const auto ours = ShaderHeader(bindings);
            const bool equal = game.size() >= ours.size() && std::equal(ours.begin(), ours.end(), game.begin());
            same += equal;
            if (!equal) WriteFile(std::string(argv[2]) + "/" + p.name + ".ours-header", ours);
            std::cout << p.name << ": header " << ours.size() << " bytes, " << (equal ? "identical" : "DIFFERENT") << "\n";
        }
        std::cout << same << " of " << OwnTranslucent::Policies().size() << " identical\n";
        return same == OwnTranslucent::Policies().size() ? 0 : 1;
    }
    if (argc >= 4 && std::string(argv[1]) == "build") {
        const auto bytes = ReadFile(argv[2]);
        const auto graph = Json::Parse(std::string(bytes.begin(), bytes.end()));
        const auto textures = OwnOpaque::GraphTextures(graph);
        const auto uniforms = OwnOpaque::Uniforms(graph, textures);
        const auto layout = OwnOpaque::MaterialLayoutHash(uniforms);
        for (const auto& program : OwnTranslucent::Compile(graph, textures, layout)) {
            WriteFile(std::string(argv[3]) + "/" + program.policy->name + ".payload", program.compiled.payload);
            std::cout << program.policy->name << ": " << program.compiled.code.size() << " bytes of code, "
                      << program.compiled.registers << " registers, payload " << program.compiled.payload.size() << "\n";
        }
        const auto text = OwnOpaque::UniformFacts(uniforms);
        WriteFile(std::string(argv[3]) + "/uniforms.json", Bytes(text.begin(), text.end()));
        std::cout << "Material layout hash " << layout << ", " << textures.size() << " textures, "
                  << uniforms.vectors.size() << " vector and " << uniforms.scalars.size() << " scalar parameters\n";
        return 0;
    }
    if (argc >= 4 && std::string(argv[1]) == "check-unlit") {
        const auto layout = std::uint32_t(std::stoul(argv[3]));
        unsigned same = 0;
        for (const auto& p : OwnTranslucent::UnlitPolicies()) {
            const auto game = ReadFile(std::string(argv[2]) + "/" + p.name + ".payload");
            auto bindings = OwnTranslucent::UnlitOneTextureBindings(layout);
            bindings.inputs = ReadPixelBindings(game).inputs;
            const auto ours = ShaderHeader(bindings);
            const bool equal = game.size() >= ours.size() && std::equal(ours.begin(), ours.end(), game.begin());
            same += equal;
            if (!equal) WriteFile(std::string(argv[2]) + "/" + p.name + ".ours-header", ours);
            std::cout << p.name << ": header " << ours.size() << " bytes, " << (equal ? "identical" : "DIFFERENT") << "\n";
        }
        return same == OwnTranslucent::UnlitPolicies().size() ? 0 : 1;
    }
    if (argc >= 4 && std::string(argv[1]) == "build-unlit") {
        const auto bytes = ReadFile(argv[2]);
        const auto graph = Json::Parse(std::string(bytes.begin(), bytes.end()));
        const auto textures = OwnOpaque::GraphTextures(graph);
        const auto uniforms = OwnOpaque::Uniforms(graph, textures);
        const auto layout = OwnOpaque::MaterialLayoutHash(uniforms);
        for (const auto& program : OwnTranslucent::CompileUnlit(graph, textures, layout)) {
            WriteFile(std::string(argv[3]) + "/" + program.policy->name + ".payload", program.compiled.payload);
            std::cout << program.policy->name << ": " << program.compiled.code.size() << " bytes of code, payload "
                      << program.compiled.payload.size() << "\n";
        }
        const auto text = OwnOpaque::UniformFacts(uniforms);
        WriteFile(std::string(argv[3]) + "/uniforms.json", Bytes(text.begin(), text.end()));
        std::cout << "Material layout hash " << layout << "\n";
        return 0;
    }
    std::cerr << "usage: own_translucent_tool check <folder> <layout> | build <graph.json> <out> | "
                 "check-unlit <folder> <layout> | build-unlit <graph.json> <out>\n";
    return 2;
} catch (const std::exception& error) { std::cerr << "error: " << error.what() << "\n"; return 1; } }

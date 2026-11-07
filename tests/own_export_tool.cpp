// Stage 7 of tools/navigation/OWN_SHADER_MAPS.md: a creator material's whole shader map of Kadan's own, as the export
// writes it (own_export.h), outside Unreal.
//   own_export_tool build <graph.json> <folder of vertex payloads, or - for the carried ones> <out folder> [instanced]
// Writes block.bin (the inline block for the cooked material), library.ushaderbytecode and build.json (map and id
// hashes, layout hash, version and program counts). The test mods of stages 5 to 7 were built from the same programs,
// so their build reports' map hashes check this end to end.
#include "../core/own_export.h"
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
static std::string HexText(const Bytes& b) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (auto c : b) { out += digits[c >> 4]; out += digits[c & 15]; }
    return out;
}

int main(int argc, char** argv) { try {
    if (argc >= 5 && std::string(argv[1]) == "build") {
        const auto bytes = ReadFile(argv[2]);
        const auto graph = Json::Parse(std::string(bytes.begin(), bytes.end()));
        std::map<std::string, Bytes> vertex;
        // "-": the programs the plugin carries (own_shader_payloads.h)
        if (std::string(argv[3]) == "-") vertex = OwnExport::ShippedVertexPrograms();
        else for (const auto& name : OwnExport::VertexProgramNames()) vertex[name] = ReadFile(std::string(argv[3]) + "/" + name + ".payload");
        OwnExport::Usage usage;
        usage.instanced = argc > 5 && std::string(argv[5]) == "instanced";
        const auto result = OwnExport::Build(graph, usage, vertex);
        const std::string out = argv[4];
        WriteFile(out + "/block.bin", result.map.block);
        WriteFile(out + "/library.ushaderbytecode", result.map.library);
        std::ostringstream report;
        report << "{\"map_hash\": \"" << HexText(result.map.mapHash) << "\", \"id_hash\": \"" << HexText(result.map.idHash)
               << "\", \"material_layout\": " << result.material_layout << ", \"versions\": " << result.versions
               << ", \"programs\": " << result.programs << ", \"block\": " << result.map.block.size()
               << ", \"library\": " << result.map.library.size() << "}\n";
        const auto text = report.str();
        WriteFile(out + "/build.json", Bytes(text.begin(), text.end()));
        std::cout << text;
        return 0;
    }
    std::cerr << "usage: own_export_tool build <graph.json> <vertex payload folder or -> <out> [instanced]\n";
    return 2;
} catch (const std::exception& error) { std::cerr << "error: " << error.what() << "\n"; return 1; } }

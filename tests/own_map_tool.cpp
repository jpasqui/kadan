// Kadan's own shader map writer (modding/mapkit/core/own_map_writer.h) outside Unreal, for
// recomp/graphics/nvasm/own_map_check.py, which compares its output with own_material.build_versions byte for byte.
//   own_map_tool build <description.json> <out folder>
//   own_map_tool uniforms <sets.json> <out.txt>
//   own_map_tool names <names.json> <out.txt>     (each name's name table entry, FString and hashes, as hex)
// build: a description is {"versions": [version, ...] or "unlit" | "lit" | "translucent-lit" | "translucent-unlit" |
// "masked" | "lit-instanced",
// "facts": {uniform facts} or "material": {"uniforms": <uniforms.json>, "indices": {name: index},
// "collections": [hex]}, "payloads": {program: payload path}}. Writes block.bin, library.ushaderbytecode and
// build.json (map hash, id hash, sizes).
// uniforms: sets.json is [uniform facts, ...]; writes one line per set, the SHA-1 of the set's root bytes and the
// repr of its arrays (what the map's id hash covers), or "error <why>".
#include "../core/own_map_writer.h"
#include <fstream>
#include <iostream>
#include <sstream>
using namespace P4MapKit::CustomMaterial;
using namespace P4MapKit::CustomMaterial::OwnMap;

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
static Json ReadJson(const std::string& path) {
    const auto b = ReadFile(path);
    return Json::Parse(std::string(b.begin(), b.end()), 512u * 1024u * 1024u, 50000000);
}

int main(int argc, char** argv) { try {
    const std::string command = argc > 1 ? argv[1] : "";
    if (command == "build" && argc == 4) {
        const auto j = ReadJson(argv[2]);
        UniformFacts facts;
        if (j.Has("facts")) facts = UniformFactsFromJson(j.At("facts"));
        else if (j.Has("material")) {
            const auto& m = j.At("material");
            std::map<std::string, std::int32_t> indices;
            if (m.Has("indices")) for (const auto& [name, index] : m.At("indices").object) indices[name] = std::int32_t(JsonInt(index));
            std::vector<Bytes> collections;
            if (m.Has("collections")) for (const auto& c : m.At("collections").array) collections.push_back(FromHex(c.Text()));
            facts = MaterialFacts(ReadJson(m.At("uniforms").Text()), indices, collections);
        }
        std::vector<Version> versions;
        const auto& v = j.At("versions");
        if (v.kind == Json::String) {
            const auto name = v.Text();
            if (name == "unlit") versions = UnlitVersions();
            else if (name == "lit") versions = LitVersions(facts);
            else if (name == "translucent-lit") versions = TranslucentLitVersions(facts);
            else if (name == "translucent-unlit") versions = TranslucentUnlitVersions(facts);
            else if (name == "masked") versions = MaskedVersions(facts);
            else if (name == "lit-instanced") {   // the lit versions, then the same for instanced static meshes
                versions = LitVersions(facts);
                const auto instanced = InstancedVersions(versions);
                versions.insert(versions.end(), instanced.begin(), instanced.end());
            }
            else throw std::runtime_error("unknown version set " + name);
        } else for (const auto& item : v.array) versions.push_back(VersionFromJson(item));
        std::map<std::string, Bytes> payloads;
        for (const auto& [name, path] : j.At("payloads").object) payloads[name] = ReadFile(path.Text());
        const auto out = BuildVersions(versions, payloads, facts);
        const std::string folder = argv[3];
        WriteFile(folder + "/block.bin", out.block);
        WriteFile(folder + "/library.ushaderbytecode", out.library);
        const auto report = "{\"map_hash\": \"" + Hex(out.mapHash) + "\", \"id_hash\": \"" + Hex(out.idHash) +
                            "\", \"block\": " + std::to_string(out.block.size()) + ", \"library\": " +
                            std::to_string(out.library.size()) + "}\n";
        WriteFile(folder + "/build.json", Bytes(report.begin(), report.end()));
        std::cout << "map " << Hex(out.mapHash) << " id " << Hex(out.idHash) << " block " << out.block.size()
                  << " library " << out.library.size() << "\n";
        return 0;
    }
    if (command == "uniforms" && argc == 4) {
        const auto sets = ReadJson(argv[2]);
        std::string text;
        for (const auto& s : sets.array) {
            try {
                const auto u = WriteUniformSet(UniformFactsFromJson(s));
                text += Hex(Sha1(Cat({u.tail, Text(ReprUniformNodes(u.nodes))}))) + "\n";
            } catch (const std::exception& e) { text += std::string("error ") + e.what() + "\n"; }
        }
        WriteFile(argv[3], Bytes(text.begin(), text.end()));
        std::cout << sets.array.size() << " sets written\n";
        return 0;
    }
    if (command == "names" && argc == 4) {   // [text, ...] -> the name table entry of each (FString and hashes), hex
        const auto names = ReadJson(argv[2]);
        std::string text;
        for (const auto& n : names.array) {
            try {
                auto entry = FString(n.Text());
                const auto [a, b] = NameHashes(n.Text());
                Put16(entry, a); Put16(entry, b);
                text += Hex(entry) + "\n";
            } catch (const std::exception& e) { text += std::string("error ") + e.what() + "\n"; }
        }
        WriteFile(argv[3], Bytes(text.begin(), text.end()));
        return 0;
    }
    std::cerr << "usage: own_map_tool build <description.json> <out folder> | uniforms <sets.json> <out.txt>\n";
    return 2;
} catch (const std::exception& e) { std::cerr << "own_map_tool: " << e.what() << "\n"; return 1; } }

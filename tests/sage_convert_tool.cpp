// core/sage_convert.h offline: converts Kadan's Switch 1 payloads to Switch 2 payloads.
//   sage_convert_tool <switch payload> <out payload>       one payload; prints the listing
//   sage_convert_tool --code <switch payload> <out code>   writes the code alone (for nvdisasm)
//   sage_convert_tool --library <switch library> [out dir]  converts every program; counts by stage and refusal; with a
//                                                          folder, writes each converted program's code as <n>.sass
// Build against the staged compiler (native_custom_backend_sources.stage_core) so "KadanMaxwell/..." resolves:
//   clang++ -std=c++17 -I core -I <staged> tests/sage_convert_tool.cpp <staged>/KadanMaxwell/*.cpp
#include "sage_convert.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <algorithm>
#include <map>

using namespace P4MapKit::CustomMaterial::OwnSage;

static Bytes Load(const char* path) {
    std::ifstream in(path, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
    try {
        if ((argc == 3 || argc == 4) && std::strcmp(argv[1], "--library") == 0) {
            const Bytes data = Load(argv[2]);
            const Archive a = ReadArchive(data);
            std::map<std::string, unsigned> counts;
            for (std::size_t s = 0; s < a.shaders.size(); ++s) {
                const Bytes p = a.Payload(s);
                const std::string stage = p.size() > 6 ? std::string{char(p[4]), char(p[5])} : "??";
                try {
                    const auto converted = ConvertSwitchPayloadToSage(p);
                    ++counts[stage + " converted"];
                    if (argc == 4) {   // the instructions alone: no header, no zero tail
                        Bytes code = ReadPayload(converted.payload).code;
                        code.erase(code.begin(), code.begin() + ProgramHeaderBytes);
                        while (code.size() >= 16 && std::all_of(code.end() - 16, code.end(), [](std::uint8_t b) { return b == 0; }))
                            code.resize(code.size() - 16);
                        std::ofstream o(std::string(argv[3]) + "/" + std::to_string(s) + ".sass", std::ios::binary);
                        o.write(reinterpret_cast<const char*>(code.data()), std::streamsize(code.size()));
                    }
                } catch (const std::exception& e) {
                    ++counts[stage + " refused: " + e.what()];
                }
            }
            for (const auto& c : counts) std::cout << c.second << "\t" << c.first << "\n";
            return 0;
        }
        const bool code = argc == 4 && std::strcmp(argv[1], "--code") == 0;
        if (argc != 3 && !code) { std::cerr << "sage_convert_tool [--code|--library] <switch payload> <out>\n"; return 2; }
        const auto out = ConvertSwitchPayloadToSage(Load(argv[code ? 2 : 1]));
        const auto parsed = ReadPayload(out.payload);
        const Bytes& bytes = code ? parsed.code : out.payload;
        std::ofstream o(argv[code ? 3 : 2], std::ios::binary);
        o.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        if (!code) std::cout << out.listing;
        std::cout << "payload " << out.payload.size() << " bytes, code " << parsed.code.size() << ", registers "
                  << out.registers << ", word3 " << parsed.bindings.word3 << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}

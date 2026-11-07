// own_switch_tool: the portable half of Kadan's GLSL_SWITCH format (own_switch_program.h) on SPIR-V files, offline.
//
//   own_switch_tool <file.spv> ...
//
// Each file is translated with the game's layouts, compiled by our Maxwell compiler and written as a payload with its
// own blocks bound in their slots (no resource tables: those come from UE's mapping in the plugin). Prints one line
// per file: stage, blocks, textures, samplers, varyings, payload bytes; or the error. Build against the staged
// compiler (native_custom_backend_sources.stage_core) so "KadanMaxwell/..." resolves.
#include "own_switch_program.h"
#include <fstream>
#include <iostream>

namespace Own = P4MapKit::CustomMaterial;

int main(int argc, char** argv) {
    int failed = 0;
    for (int i = 1; i < argc; ++i) {
        try {
            std::ifstream f(argv[i], std::ios::binary);
            if (!f) throw std::runtime_error("cannot read");
            std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            std::vector<std::uint32_t> words(bytes.size() / 4);
            std::memcpy(words.data(), bytes.data(), words.size() * 4);
            const auto p = Own::OwnSwitch::Translate(words);
            Own::OwnPayload::Description d;
            d.stage = p.stage;
            for (const auto& v : p.inputs) d.inputs.push_back({v.name, v.location});
            for (const auto& v : p.outputs) d.outputs.push_back({v.name, v.location});
            unsigned buffers = 0;
            for (const auto& b : p.iface.blocks) buffers = std::max(buffers, unsigned(b.slot + 1));
            d.uniform_buffers = buffers;
            d.texture_slots = int(p.iface.textures.size());
            d.storage_buffers = unsigned(p.iface.storage.size());
            d.sampler_slots = unsigned(std::max<int>(int(p.iface.samplers.size()), p.iface.default_sampler + 1));
            d.default_sampler = p.iface.default_sampler;
            d.inout_mask = p.stage == Own::OwnPayload::Stage::Geometry ? 0 : p.inout_mask;
            const auto c = Own::OwnPayload::Compile(p.assembly, d);
            std::cout << argv[i] << ": " << (p.stage == Own::OwnPayload::Stage::Vertex ? "vertex" : p.stage == Own::OwnPayload::Stage::Pixel ? "pixel" : "geometry")
                      << " blocks";
            for (const auto& b : p.iface.blocks) std::cout << " " << b.name << "@" << b.slot;
            std::cout << " textures " << p.iface.textures.size() << " samplers " << p.iface.samplers.size()
                      << " default " << p.iface.default_sampler << " in";
            for (const auto& v : p.inputs) std::cout << " " << v.name << "@" << v.location;
            std::cout << " out";
            for (const auto& v : p.outputs) std::cout << " " << v.name << "@" << v.location;
            std::cout << " mask " << p.inout_mask << " registers " << c.registers << " payload " << c.payload.size() << "\n";
        } catch (const std::exception& e) {
            ++failed;
            std::cout << argv[i] << ": error: " << e.what() << "\n";
        }
    }
    std::cout << (argc - 1 - failed) << " of " << (argc - 1) << " compiled\n";
    return failed ? 1 : 0;
}

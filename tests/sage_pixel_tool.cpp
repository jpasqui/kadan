// core/native_custom_backend_sage.h offline: compiles a pixel program's NV assembly for the Switch 2 against a donor
// 2.0.0 pixel payload and writes the new payload.
//   sage_pixel_tool <donor payload> <program.nvasm> <out payload>
// Build against the staged compiler (native_custom_backend_sources.stage_core) so "KadanMaxwell/..." resolves:
//   clang++ -std=c++17 -I core -I <staged> tests/sage_pixel_tool.cpp <staged>/KadanMaxwell/*.cpp
#include "native_custom_backend_sage.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

int main(int argc, char** argv) {
    if (argc != 4) { std::cerr << "sage_pixel_tool <donor payload> <program.nvasm> <out payload>\n"; return 2; }
    try {
        std::ifstream d(argv[1], std::ios::binary), a(argv[2], std::ios::binary);
        const P4MapKit::CustomMaterial::OwnSage::Bytes donor((std::istreambuf_iterator<char>(d)), std::istreambuf_iterator<char>());
        std::stringstream text;
        text << a.rdbuf();
        const auto out = P4MapKit::CustomMaterial::NativeBackend::CompileSagePixelAssembly(text.str(), donor);
        std::ofstream o(argv[3], std::ios::binary);
        o.write(reinterpret_cast<const char*>(out.payload.data()), std::streamsize(out.payload.size()));
        std::cout << out.listing << "payload " << out.payload.size() << " bytes, registers " << out.registers << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}

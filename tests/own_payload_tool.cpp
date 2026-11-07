// Stage 8 of tools/navigation/OWN_SHADER_MAPS.md: Kadan's own vertex and geometry payloads written in C++
// (core/own_payload.h), checked against the shipped ones.
//   own_payload_tool <folder of the shipped payloads> [<folder of NV assembly> ...]
// For each of Kadan's programs it compiles the NV assembly with our Maxwell compiler, writes the payload from the
// program's named description and compares it with <name>.payload byte for byte. The assembly is <name>.nvasm from the
// first assembly folder that has it (build_vertex.py / build_geometry.py output), else the text embedded in the
// shipped payload's control section (after offset 0x878).
#include "../core/own_payload.h"
#include <fstream>
#include <iostream>
#include <sstream>
using namespace P4MapKit::CustomMaterial;

static bool ReadFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream s;
    s << f.rdbuf();
    out = s.str();
    return true;
}

// The code and control sections of a payload, skipping its binding header.
struct Sections { std::size_t header_end = 0, code_at = 0, control_at = 0, tail_at = 0; Bytes code, control; };
static Sections Split(const Bytes& p) {
    std::size_t at = 0;
    const auto u32 = [&]() { auto v = Read32(p, at); at += 4; return v; };
    const auto skip = [&](std::size_t n) { if (n > p.size() - at) throw std::runtime_error("truncated payload"); at += n; };
    if (u32() != 0x474c534c) throw std::runtime_error("not a payload");
    skip(2);
    for (auto n = u32(); n; n--) skip(std::size_t(u32()) * 4);   // packed uniform buffers
    skip(std::size_t(u32()) * 4);                                 // packed global arrays
    for (int list = 0; list < 2; list++)
        for (auto n = u32(); n; n--) { skip(u32()); skip(4); }  // varyings
    skip(4);                                                      // resource bits
    for (int table = 0; table < 5; table++) skip(std::size_t(u32()) * 4);
    skip(2 + 5 + 12);
    Sections s;
    s.header_end = at;
    const auto code = u32();
    s.code_at = at;
    skip(code);
    s.code.assign(p.begin() + std::ptrdiff_t(s.code_at), p.begin() + std::ptrdiff_t(at));
    const auto control = u32();
    s.control_at = at;
    skip(control);
    s.control.assign(p.begin() + std::ptrdiff_t(s.control_at), p.begin() + std::ptrdiff_t(at));
    s.tail_at = at;
    return s;
}

static std::string Region(const Sections& s, std::size_t at) {
    if (at < s.header_end) return "header";
    if (at < s.code_at + s.code.size()) return "code";
    if (at < s.control_at + s.control.size()) return "control";
    return "tail";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "own_payload_tool <folder of the shipped payloads> [<folder of NV assembly> ...]\n";
        return 2;
    }
    unsigned same = 0, total = 0;
    for (const auto& name : OwnPayload::KadanProgramNames()) {
        total++;
        try {
            std::string text;
            if (!ReadFile(std::string(argv[1]) + "/" + name + ".payload", text)) throw std::runtime_error("no shipped payload");
            const Bytes shipped(text.begin(), text.end());
            const auto sections = Split(shipped);
            std::string assembly, source = "embedded";
            for (int i = 2; i < argc && source == "embedded"; i++)
                if (ReadFile(std::string(argv[i]) + "/" + name + ".nvasm", assembly)) source = argv[i];
            if (source == "embedded") {
                const auto size = Read32(sections.control, 0x18);
                assembly.assign(sections.control.begin() + 0x878, sections.control.begin() + 0x878 + size);
            } else {
                std::string plain;                                // the files were written with \n line ends
                for (char c : assembly) if (c != '\r') plain += c;
                assembly = plain;
            }
            const auto program = OwnPayload::Compile(assembly, OwnPayload::KadanProgram(name));
            const auto& ours = program.payload;
            std::size_t first = 0;
            while (first < ours.size() && first < shipped.size() && ours[first] == shipped[first]) first++;
            const bool equal = ours == shipped;
            same += equal;
            std::cout << name << ": " << (equal ? "identical" : "DIFFERENT") << ", " << ours.size() << " bytes (shipped "
                      << shipped.size() << "), code " << program.code.size() << ", registers " << program.registers
                      << ", scratch " << program.scratch << ", assembly " << source;
            if (!equal) {
                std::size_t differ = 0;
                for (std::size_t i = 0; i < std::min(ours.size(), shipped.size()); i++) differ += ours[i] != shipped[i];
                std::cout << "; first difference at 0x" << std::hex << first << std::dec << " (" << Region(sections, first)
                          << "), " << differ << " bytes differ in the common length";
                if (program.code != sections.code) std::cout << "; code differs";
            }
            std::cout << "\n";
        } catch (const std::exception& e) {
            std::cout << name << ": ERROR " << e.what() << "\n";
        }
    }
    std::cout << same << " of " << total << " byte identical\n";
    return same == total ? 0 : 1;
}

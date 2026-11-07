// Compiles NV_gpu_program5 assembly for the Switch 2 with compiler/ampere_write.cpp.
//   ampere_write_tool compile <program.nvasm> <out code> [<buf14 offset>=<c[0x10] offset> ...]
//     writes the code (program header, instructions, padding, tail) and prints the listing;
//   ampere_write_tool mod <game archive> <out folder> <library> @<list>
//     each list line <object path>=<.uexp>=<program.nvasm>: every base-pass pixel program of the instance's own map
//     is replaced by the compiled program, keeping the game program's bindings; texture handle words 328 and 1352 of
//     program.buffer[14] (Kadan's texture and sampler of the first parameter) go to the program's first texture and
//     first sampler slot. Writes the library and mod.json for Tsuchi, as the magenta proof (own_sage_tool magenta).
// Built with the compiler's sources and -std=c++20.
#include "../compiler/ampere_write.h"
#include "../compiler/nvasm_ir.h"
#include "../compiler/nvasm_lower.h"
#include "../compiler/nvasm_program.h"
#include "../core/custom_material_archive.h"
#include "../core/own_sage_payload.h"
#include "../core/custom_material_graph.h"
#include "../core/custom_material_bindings.h"
#include "../core/material_resource_boundary.h"
#include "../core/material_shader_map.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace P4MapKit::CustomMaterial::OwnSage;

static std::string ReadText(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot read " + path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
static Bytes ReadBytes(const std::string& path) { const auto s = ReadText(path); return Bytes(s.begin(), s.end()); }
static void WriteBytes(const std::string& path, const Bytes& b) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
}
static std::string HexHash(const Hash& h) {
    char s[41];
    for (int i = 0; i < 20; ++i) std::snprintf(s + 2 * i, 3, "%02x", h[i]);
    return std::string(s, 40);
}

static nvasm::AmpereResult Compile(const std::string& text, const nvasm::AmpereOptions& options) {
    nvasm::detail::Program parsed = nvasm::detail::parse(text);
    nvasm::Options o;
    o.glsl = false; o.spirv = false; o.maxwell = true; o.demote = false;
    nvasm::Result lowered;
    nvasm::ir::Function fn = nvasm::lower::lower(parsed, o, lowered);
    nvasm::AmpereResult r;
    nvasm::ir::write_ampere(fn, r, options);
    return r;
}

// The shader map id hash Tsuchi compares and the library map the package's resource hash names (see own_sage_tool).
struct MaterialMap { Hash id{}; std::size_t map = 0; };
static MaterialMap FindMaterialMap(const Bytes& uexp, const Archive& a) {
    MaterialMap out;
    bool found = false;
    for (std::size_t p = 0; p + 48 <= uexp.size(); ++p) {
        if (Get32(&uexp[p]) != 1 || Get32(&uexp[p + 4]) != 1) continue;
        const auto align = Get32(&uexp[p + 36]), flags = Get32(&uexp[p + 40]), frozen = Get32(&uexp[p + 44]);
        if (flags != 1 || !frozen || frozen > uexp.size() - p || (align != 0xffffffffu && (align & (align - 1)))) continue;
        if (found) throw std::runtime_error("more than one shader map envelope");
        std::memcpy(out.id.data(), &uexp[p + 16], 20);
        found = true;
    }
    if (!found) throw std::runtime_error("no shader map of its own");
    std::size_t matches = 0;
    for (std::size_t m = 0; m < a.maps.size(); ++m)
        if (std::search(uexp.begin(), uexp.end(), a.map_hashes[m].begin(), a.map_hashes[m].end()) != uexp.end()) out.map = m, ++matches;
    if (matches != 1) throw std::runtime_error("the package names " + std::to_string(matches) + " library maps");
    return out;
}

// The first row's slot of a resource table (head words, rows buffer << 24 | resource << 8 | slot, 0xffffffff).
static int FirstSlot(const std::vector<std::uint32_t>& t) {
    std::size_t head = t.size();
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (t[i] == 0xffffffffu) { head = i; break; }
        if (t[i]) { head = t[i]; break; }
    }
    return head < t.size() && t[head] != 0xffffffffu ? int(t[head] & 0xff) : -1;
}


// The instance's texture parameter names, in the uniform set's order (Kadan's ResolveMaterialBindings indexes them by
// resource index / 2), read from its .uasset/.uexp with Kadan's package and shader map readers.
static std::vector<std::string> TextureNames(const std::string& uexp) {
    const std::string uasset = uexp.substr(0, uexp.size() - 5) + ".uasset";
    const Bytes head = ReadBytes(uasset), body = ReadBytes(uexp);
    const auto header = P4MapKit::ReadPackageHeader(head, body.size());
    for (std::size_t i = 0; i < header.Exports.size(); ++i) {
        P4MapKit::FMaterialResourceBoundary boundary;
        P4MapKit::FReadError error;
        P4MapKit::FMaterialShaderMapEnvelope map;
        P4MapKit::FMaterialUniformSet uniforms;
        if (!P4MapKit::DiscoverMaterialResourceBoundary(header, body, i, boundary, error) ||
            !P4MapKit::ReadMaterialShaderMapEnvelope(body, boundary.ResourceStart, boundary.ExportEnd, map, error) ||
            !P4MapKit::ReadMaterialUniformSet(map.Frozen, map.ScriptNames, uniforms, error))
            continue;
        std::vector<std::string> names;
        for (const auto& t : uniforms.Textures) names.push_back(t.Parameter.Name);
        return names;
    }
    throw std::runtime_error("no material uniform set in " + uexp);
}

// Kadan's {TEX:name}/{SAMP:name} placeholders resolved through the donor program's own tables, as on Switch 1.
static std::string ResolveForDonor(const std::string& text, const Payload& p, const std::vector<std::string>& names) {
    if (text.find("{TEX:") == std::string::npos) return text;
    P4MapKit::CustomMaterial::ShaderBindings h;
    h.srv = p.bindings.srv;
    h.samplers = p.bindings.samplers;
    h.textures = p.bindings.textures;
    for (unsigned b = 0; b < 32; ++b) {
        try {
            const auto r = P4MapKit::CustomMaterial::ResolveMaterialBindings(h, b, names);
            bool complete = !r.textures.empty();
            for (const auto& t : r.textures) complete &= t.second.first >= 0 && t.second.second >= 0;
            if (complete) return P4MapKit::CustomMaterial::Resolve(text, r.textures);
        } catch (const std::exception&) {
        }
    }
    throw std::runtime_error("no uniform buffer of the program binds the material's textures");
}

static int Mod(int argc, char** argv) {
    const Bytes data = ReadBytes(argv[2]);
    const Archive a = ReadArchive(data);
    const std::string out = argv[3], library = argv[4];
    std::vector<std::string> lines;
    for (int k = 5; k < argc; ++k) {
        std::string arg = argv[k];
        if (arg[0] == '@') {
            std::istringstream list(ReadText(arg.substr(1)));
            for (std::string line; std::getline(list, line);) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty()) lines.push_back(line);
            }
        } else lines.push_back(arg);
    }
    std::vector<P4MapKit::CustomMaterial::NativeBackend::OwnedArchiveMap> maps;
    std::ostringstream rows;
    std::map<std::string, std::string> sources;
    for (const auto& line : lines) {
        const auto e1 = line.find('='), e2 = line.find('=', e1 + 1), e3 = line.find('=', e2 + 1);
        const std::string object = line.substr(0, e1), uexp = line.substr(e1 + 1, e2 - e1 - 1);
        const std::string program = line.substr(e2 + 1, e3 == std::string::npos ? std::string::npos : e3 - e2 - 1);
        const std::string mode = e3 == std::string::npos ? "scratch" : line.substr(e3 + 1);   // scratch, keepsize, keepsph
        if (!sources.count(program)) sources[program] = ReadText(program);
        MaterialMap material;
        try { material = FindMaterialMap(ReadBytes(uexp), a); }
        catch (const std::exception& e) { std::cout << object << ": skipped, " << e.what() << "\n"; continue; }
        std::vector<std::string> names;
        if (sources[program].find("{TEX:") != std::string::npos) names = TextureNames(uexp);
        const auto shaders = a.MapShaders(material.map);
        P4MapKit::CustomMaterial::NativeBackend::OwnedArchiveMap map;
        Bytes identities;
        std::ostringstream programs;
        unsigned registers = 0;
        for (std::size_t place = 0; place < shaders.size(); ++place) {
            if (a.shaders[shaders[place]].frequency != 3) continue;
            Payload p = ReadPayload(a.Payload(shaders[place]));
            if (p.bindings.name.find("/TBasePassPS") == std::string::npos) continue;
            const auto game = ReadNativeProgram(p.code, Stage::Pixel);
            if (ReadHeaderFacts(game.header).omap_target != 0xf) continue;   // render target 0 only, as the program
            nvasm::AmpereOptions options;
            const int tex = FirstSlot(p.bindings.textures), samp = FirstSlot(p.bindings.samplers);
            if (tex >= 0 && samp >= 0) options.handles = {{328u, 16u * unsigned(tex)}, {1352u, 16u * unsigned(samp)}};
            for (const auto* table : {&p.bindings.textures, &p.bindings.srv, &p.bindings.samplers}) {   // every row
                std::size_t head = table->size();
                for (std::size_t i = 0; i < table->size(); ++i) {
                    if ((*table)[i] == 0xffffffffu) { head = i; break; }
                    if ((*table)[i]) { head = (*table)[i]; break; }
                }
                const unsigned base = table == &p.bindings.samplers ? 1352 : 328;
                for (std::size_t i = head; i < table->size() && (*table)[i] != 0xffffffffu; ++i)
                    options.handles[base + 8 * ((*table)[i] & 0xff)] = 16 * ((*table)[i] & 0xff);
            }
            nvasm::AmpereResult r;
            try { r = Compile(ResolveForDonor(sources[program], p, names), options); }
            catch (const std::exception& e) { std::cout << "  place " << place << ": " << e.what() << "\n"; continue; }
            if (mode == "scratch" || mode == "nohash" || mode == "nosha" || mode == "noctlhash") {
                p.control = SageControl(p.control, std::uint32_t(r.code.size()), r.table_offset, r.registers, 0);
                p.code = r.code;
                if (mode == "nohash" || mode == "noctlhash")   // the two control-section hashes zeroed
                    for (unsigned w : {4u, 5u, 10u, 11u}) std::memset(&p.control[w * 4], 0, 4);
                if (mode == "nohash" || mode == "nosha") p.bindings.hash.fill(0);   // the payload's SHA-1 field zeroed
            } else {   // "keepsize": the game program's size, header, control and constant table; "keepsph": the same
                       // with the compiled program's header
                NativeProgram n = game;
                std::vector<Instruction> mine;
                for (std::size_t at = 128; at + 16 <= r.table_offset; at += 16) {
                    Instruction i;
                    std::memcpy(i.words.data(), &r.code[at], 16);
                    mine.push_back(i);
                    if (IsSelfBranch(i)) break;
                }
                if (mine.size() > n.instructions.size()) { std::cout << "  place " << place << ": longer than the game's\n"; continue; }
                while (mine.size() < n.instructions.size()) {
                    Instruction nop;
                    nop.words = {{0x00007918u, 0, 0, 0x000fc000u}};
                    mine.push_back(nop);
                }
                n.instructions = mine;
                if (mode == "keepsph") std::memcpy(n.header.data(), r.code.data(), 128);
                p.code = WriteNativeProgram(n);
                const std::uint32_t regs = Get32(&p.control[19 * 4]);
                if (r.registers + 2 > regs) p.control = SageControl(p.control, std::uint32_t(p.code.size()), Get32(&p.control[14 * 4]), r.registers, Get32(&p.control[20 * 4]));
            }
            registers = std::max(registers, r.registers);
            const Bytes payload = WritePayload(p);
            (void)ReadPayload(payload);
            const auto hash = P4MapKit::CustomMaterial::Sha1(payload);
            identities.insert(identities.end(), hash.begin(), hash.end());
            map.shaders.push_back({3, payload});
            programs << (programs.tellp() ? ", " : "") << "{\"place\": " << place << ", \"hash\": \""
                     << P4MapKit::CustomMaterial::Hex(hash) << "\"}";
        }
        if (map.shaders.empty()) { std::cout << object << ": no program replaced\n"; continue; }
        map.hash = P4MapKit::CustomMaterial::Sha1(identities);
        rows << (rows.tellp() ? ",\n" : "") << "      {\"material\": \"" << object << "\", \"game_material\": \""
             << object.substr(0, object.find('.')) << "\", \"map_id\": \"" << HexHash(material.id) << "\", \"map_shaders\": "
             << shaders.size() << ", \"archive_map\": \"" << P4MapKit::CustomMaterial::Hex(map.hash) << "\", \"shaders\": ["
             << programs.str() << "]}";
        std::cout << object << ": map " << material.map << ", " << map.shaders.size() << " programs compiled, registers "
                  << registers << "\n";
        if (std::none_of(maps.begin(), maps.end(), [&](const auto& m) { return m.hash == map.hash; })) maps.push_back(std::move(map));
    }
    const std::string archiveName = "ShaderArchive-" + library + "-" + ShaderFormat + ".ushaderbytecode";
    WriteBytes(out + "/" + archiveName, P4MapKit::CustomMaterial::NativeBackend::WriteOwnedShaderArchive(maps));
    std::ofstream json(out + "/mod.json");
    json << "{\n  \"id\": \"kadan.sage-compiled\",\n  \"name\": \"Switch 2 compiled shader test\",\n  \"version\": \"1.0.0\",\n"
         << "  \"framework\": 0,\n  \"shaders\": {\n    \"library\": \"" << library << "\",\n    \"archive\": \"" << archiveName
         << "\",\n    \"materials\": [\n" << rows.str() << "\n    ]\n  }\n}\n";
    return 0;
}

int main(int argc, char** argv) {
    try {
        const std::string command = argc > 1 ? argv[1] : "";
        if (command == "mod" && argc >= 6) return Mod(argc, argv);
        if (command != "compile" || argc < 4) { std::cerr << "usage: see tests/ampere_write_tool.cpp\n"; return 2; }
        nvasm::AmpereOptions options;
        for (int k = 4; k < argc; ++k) {
            const std::string m = argv[k];
            const auto eq = m.find('=');
            options.handles[uint32_t(std::stoul(m.substr(0, eq), nullptr, 0))] = uint32_t(std::stoul(m.substr(eq + 1), nullptr, 0));
        }
        const auto r = Compile(ReadText(argv[2]), options);
        WriteBytes(argv[3], r.code);
        std::cout << r.listing << "code " << r.code.size() << " bytes, table at " << r.table_offset << ", registers "
                  << r.registers << ", instructions " << r.instructions << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}

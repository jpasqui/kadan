// Checks core/own_sage_payload.h against the game's Switch 2 shader libraries (private inputs, never committed).
//   own_sage_tool roundtrip <archive>            every payload read, written back and compared; code read; counts
//   own_sage_tool maps <archive> <text>          the maps whose program names contain <text>, with their programs
//   own_sage_tool payload <archive> <shader> <out>  one payload, decompressed
//   own_sage_tool magenta <archive> <out> <library> <object path>=<.uexp> ...  the in-game proof's mod folder
//   own_sage_tool samples <archive> <out>         up to 3 instructions of each opcode, 16 bytes each, for nvdisasm
//   own_sage_tool reencode <archive> [every] [show]  game instructions read back and encoded again with
//                                                compiler/ampere_encode.h; counts per opcode ("show" lists examples
//                                                of instructions not taken)
//   own_sage_tool stream <archive> <out> [every]  instructions of every <every>th program for nvdisasm -b SM86, and
//                                                <out>.fields with what the reader decodes from each
//   own_sage_tool code <archive> <shader> <out>  one program's instructions, for nvdisasm
//   own_sage_tool stages <archive> [examples]    vertex and geometry programs: header words, attribute reads and
//                                                writes against the header's maps, program ends, vertex handles
#include "../core/own_sage_payload.h"
#include "../core/custom_material_archive.h"
#include "../compiler/ampere_encode.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
using namespace P4MapKit::CustomMaterial::OwnSage;

static Bytes ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot read " + path);
    return Bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
static void WriteFile(const std::string& path, const Bytes& b) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
}
static std::string HexHash(const Hash& h) {
    char s[41];
    for (int i = 0; i < 20; ++i) std::snprintf(s + 2 * i, 3, "%02x", h[i]);
    return std::string(s, 40);
}

static int Roundtrip(const std::string& path) {
    const Bytes data = ReadFile(path);
    const Archive a = ReadArchive(data);
    std::map<int, int> stages, versions, omaps;
    std::map<std::uint32_t, int> opcodes;
    std::size_t bad = 0, instructions = 0, headers = 0;
    for (std::size_t s = 0; s < a.shaders.size(); ++s) {
        try {
            const Bytes payload = a.Payload(s);
            const Payload p = ReadPayload(payload);
            if (WritePayload(p) != payload) throw std::runtime_error("written payload differs");
            if (int(a.shaders[s].frequency) != (p.bindings.stage == Stage::Vertex ? 0 : p.bindings.stage == Stage::Pixel ? 3
                                                 : p.bindings.stage == Stage::Geometry ? 4 : 5) && a.shaders[s].frequency < 6)
                throw std::runtime_error("stage letters and archive frequency differ");
            const auto stage = a.shaders[s].frequency >= 6 ? Stage::Compute : p.bindings.stage;
            const NativeProgram n = ReadNativeProgram(p.code, stage);
            if (WriteNativeProgram(n) != p.code) throw std::runtime_error("written code differs");
            stages[a.shaders[s].frequency]++;
            instructions += n.instructions.size();
            if (n.has_header) {
                ++headers;
                const auto f = ReadHeaderFacts(n.header);
                versions[int(f.version)]++;
                if (f.shader_type == 5) omaps[int(f.omap_target)]++;
            }
            for (const auto& i : n.instructions) opcodes[i.Opcode()]++;
        } catch (const std::exception& e) {
            if (++bad <= 10) std::cout << "shader " << s << ": " << e.what() << "\n";
        }
    }
    std::cout << path << "\n  maps " << a.maps.size() << ", shaders " << a.shaders.size() << ", failed " << bad
              << ", instructions " << instructions << ", program headers " << headers << "\n  frequencies:";
    for (auto& [k, v] : stages) std::cout << " " << k << ":" << v;
    std::cout << "\n  header versions:";
    for (auto& [k, v] : versions) std::cout << " " << k << ":" << v;
    std::cout << "\n  pixel output masks (most common):";
    std::multimap<int, int> byCount;
    for (auto& [k, v] : omaps) byCount.emplace(-v, k);
    int shown = 0;
    for (auto& [n, k] : byCount) { if (shown++ == 8) break; std::printf(" %#x:%d", k, -n); }
    std::cout << "\n  distinct opcodes " << opcodes.size() << "\n";
    return bad ? 1 : 0;
}

static int Maps(const std::string& path, const std::string& text) {
    const Bytes data = ReadFile(path);
    const Archive a = ReadArchive(data);
    std::map<std::uint32_t, std::string> names;   // shader index -> name, read on demand
    int found = 0;
    for (std::size_t m = 0; m < a.maps.size(); ++m) {
        const auto shaders = a.MapShaders(m);
        bool match = false;
        std::vector<std::string> rows;
        for (std::size_t place = 0; place < shaders.size(); ++place) {
            const auto s = shaders[place];
            auto it = names.find(s);
            if (it == names.end()) it = names.emplace(s, ReadPayload(a.Payload(s)).bindings.name).first;
            if (it->second.find(text) != std::string::npos) match = true;
            std::ostringstream row;
            row << "    " << place << " shader " << s << " freq " << int(a.shaders[s].frequency) << " " << HexHash(a.shader_hashes[s])
                << " " << it->second;
            rows.push_back(row.str());
        }
        if (!match) continue;
        ++found;
        std::cout << "map " << m << " " << HexHash(a.map_hashes[m]) << " shaders " << shaders.size() << "\n";
        for (auto& r : rows) std::cout << r << "\n";
    }
    std::cout << found << " maps\n";
    return 0;
}

static int OnePayload(const std::string& path, std::size_t s, const std::string& out) {
    const Bytes data = ReadFile(path);
    const Archive a = ReadArchive(data);
    WriteFile(out, a.Payload(s));
    return 0;
}

// Every <every>th program's instructions in one stream, and per instruction what the reader decodes, so that
// nvdisasm's text can be compared with it (compare_nvdisasm.py).
static int Stream(const std::string& path, const std::string& out, std::size_t every) {
    const Bytes data = ReadFile(path);
    const Archive a = ReadArchive(data);
    Bytes stream;
    std::ofstream fields(out + ".fields");
    for (std::size_t s = 0; s < a.shaders.size(); s += every) {
        const Payload p = ReadPayload(a.Payload(s));
        const auto stage = a.shaders[s].frequency >= 5 ? Stage::Compute : p.bindings.stage;
        const NativeProgram n = ReadNativeProgram(p.code, stage);
        for (const auto& i : n.instructions) {
            for (auto w : i.words) Put32(stream, w);
            char line[160];
            std::snprintf(line, sizeof line, "%zu %03x p%u%s d%u a%u b%u imm%08x c%u:%x st%u y%u wb%u rb%u wm%u\n", s, i.Opcode(),
                          i.Predicate(), i.PredicateNegated() ? "!" : "", i.Dst(), i.SrcA(), i.SrcB(), i.Immediate(),
                          i.ConstBank(), i.ConstOffset(), i.Stall(), i.Yield() ? 1 : 0, i.WriteBarrier(), i.ReadBarrier(), i.WaitMask());
            fields << line;
        }
    }
    WriteFile(out, stream);
    std::cout << stream.size() / 16 << " instructions\n";
    return 0;
}

static int Samples(const std::string& path, const std::string& out) {
    const Bytes data = ReadFile(path);
    const Archive a = ReadArchive(data);
    std::map<std::uint32_t, int> seen;
    Bytes stream;
    for (std::size_t s = 0; s < a.shaders.size(); ++s) {
        Payload p;
        try { p = ReadPayload(a.Payload(s)); } catch (const std::exception&) { continue; }
        const auto stage = a.shaders[s].frequency >= 5 ? Stage::Compute : p.bindings.stage;
        for (const auto& i : ReadNativeProgram(p.code, stage).instructions)
            if (seen[i.Opcode()]++ < 3) for (auto w : i.words) Put32(stream, w);
    }
    WriteFile(out, stream);
    std::cout << seen.size() << " opcodes, " << stream.size() / 16 << " samples\n";
    for (auto& [op, n] : seen) std::printf("%03x %d\n", op, n);
    return 0;
}

// ---- The in-game proof (docs/SWITCH2_SHADERS.md, step 4) ---------------------------------------------------------

// A cooked material instance with a shader map of its own holds, in its .uexp: bCooked 1, bValid 1, quality, feature
// level, the map's id hash (what Tsuchi compares), the frozen content's layout (alignment 0xffffffff on 4.27, flags 1)
// and size; and at its end the map's resource hash, which names the library's map.
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

static Instruction Words(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) { Instruction i; i.words = {{a, b, c, d}}; return i; }

// The pixel program made to write flat magenta: render target 0 gets (1, 0, 1, 1), every other output 0. The program
// header, the control section and the code's size stay; the old instructions become NOPs after the new EXIT, so the
// program's constant table stays where the control section places it. Encodings as nvdisasm reads the game's own:
// MOV R, imm (0x802, write mask 0xf), EXIT, BRA to itself, NOP.
static Payload Magenta(const Payload& in) {
    Payload p = in;
    NativeProgram n = ReadNativeProgram(p.code, Stage::Pixel);
    const auto facts = ReadHeaderFacts(n.header);
    if (facts.shader_type != 5 || facts.writes_depth || !(facts.omap_target & 0xf))
        throw std::runtime_error("not a colour pixel program");
    std::vector<Instruction> stub;
    const std::uint32_t one = 0x3f800000u, value[4] = {one, 0, one, one};
    unsigned reg = 0;
    for (unsigned rt = 0; rt < 8; ++rt)
        for (unsigned c = 0; c < 4; ++c)
            if ((facts.omap_target >> (rt * 4 + c)) & 1) {
                stub.push_back(Words(0x00007802u | (reg << 16), rt == 0 ? value[c] : 0u, 0x00000f00u, 0x000fe200u));
                ++reg;
            }
    stub.push_back(Words(0x0000794du, 0, 0x03800000u, 0x000fea00u));
    stub.push_back(Words(0x00007947u, 0xfffffff0u, 0x0383ffffu, 0x000fc000u));
    if (stub.size() > n.instructions.size()) throw std::runtime_error("program shorter than the stub");
    while (stub.size() < n.instructions.size()) stub.push_back(Words(0x00007918u, 0, 0, 0x000fc000u));
    n.instructions = stub;
    p.code = WriteNativeProgram(n);
    if (p.code.size() != in.code.size()) throw std::runtime_error("code size changed");
    return p;
}

// own_sage_tool magenta <archive> <out folder> <library> <object path>=<.uexp> ...
static int MagentaMod(int argc, char** argv) {
    const Bytes data = ReadFile(argv[2]);
    const Archive a = ReadArchive(data);
    const std::string out = argv[3], library = argv[4];
    std::vector<P4MapKit::CustomMaterial::NativeBackend::OwnedArchiveMap> maps;
    std::ostringstream rows;
    std::vector<std::string> targets;
    for (int k = 5; k < argc; ++k) {
        const std::string arg = argv[k];
        if (arg.size() > 1 && arg[0] == '@') {   // a file with one <object path>=<.uexp> per line
            std::ifstream list(arg.substr(1));
            for (std::string line; std::getline(list, line);)
                if (!line.empty() && line.back() == '\r') line.pop_back(), targets.push_back(line);
                else if (!line.empty()) targets.push_back(line);
        } else
            targets.push_back(arg);
    }
    for (const auto& arg : targets) {
        const auto eq = arg.find('=');
        const std::string object = arg.substr(0, eq), uexpPath = arg.substr(eq + 1);
        MaterialMap material;
        try { material = FindMaterialMap(ReadFile(uexpPath), a); }
        catch (const std::exception& e) { std::cout << object << ": skipped, " << e.what() << "\n"; continue; }
        const auto shaders = a.MapShaders(material.map);
        P4MapKit::CustomMaterial::NativeBackend::OwnedArchiveMap map;
        Bytes identities;
        std::ostringstream programs;
        for (std::size_t place = 0; place < shaders.size(); ++place) {
            if (a.shaders[shaders[place]].frequency != 3) continue;
            const Payload p = ReadPayload(a.Payload(shaders[place]));
            if (p.bindings.name.find("/TBasePassPS") == std::string::npos) continue;
            Payload changed;
            try { changed = Magenta(p); } catch (const std::exception&) { continue; }   // depth-writing programs stay
            const Bytes payload = WritePayload(changed);
            const auto hash = P4MapKit::CustomMaterial::Sha1(payload);
            identities.insert(identities.end(), hash.begin(), hash.end());
            map.shaders.push_back({3, payload});
            programs << (programs.tellp() ? ", " : "") << "{\"place\": " << place << ", \"hash\": \"" << P4MapKit::CustomMaterial::Hex(hash) << "\"}";
        }
        if (map.shaders.empty()) throw std::runtime_error(object + ": no base pass pixel programs");
        map.hash = P4MapKit::CustomMaterial::Sha1(identities);
        const std::string package = object.substr(0, object.find('.'));
        Hash id = material.id;
        rows << (rows.tellp() ? ",\n" : "") << "      {\"material\": \"" << object << "\", \"game_material\": \"" << package
             << "\", \"map_id\": \"" << HexHash(id) << "\", \"map_shaders\": " << shaders.size()
             << ", \"archive_map\": \"" << P4MapKit::CustomMaterial::Hex(map.hash) << "\", \"shaders\": [" << programs.str() << "]}";
        std::cout << object << ": library map " << material.map << ", " << shaders.size() << " programs, " << map.shaders.size()
                  << " base pass pixel programs made magenta\n";
        if (std::none_of(maps.begin(), maps.end(), [&](const auto& m) { return m.hash == map.hash; })) maps.push_back(std::move(map));
    }
    const std::string archiveName = "ShaderArchive-" + library + "-" + ShaderFormat + ".ushaderbytecode";
    WriteFile(out + "/" + archiveName, P4MapKit::CustomMaterial::NativeBackend::WriteOwnedShaderArchive(maps));
    std::ofstream json(out + "/mod.json");
    json << "{\n  \"id\": \"kadan.sage-magenta\",\n  \"name\": \"Switch 2 magenta shader proof\",\n  \"version\": \"1.0.0\",\n"
         << "  \"framework\": 0,\n  \"shaders\": {\n    \"library\": \"" << library << "\",\n    \"archive\": \"" << archiveName
         << "\",\n    \"materials\": [\n" << rows.str() << "\n    ]\n  }\n}\n";
    return 0;
}

// ---- compiler/ampere_encode.h against the game's instructions -----------------------------------------------------

namespace A = nvasm::ampere;

// The game instruction's operands read back, then encoded again with compiler/ampere_encode.h. Returns false when the
// instruction uses something the encoder does not take (the caller counts those apart).
static bool Reencode(const Instruction& g, A::Word& out) {
    const auto bits = [&](unsigned at, unsigned n) { return g.Bits(at, n); };
    const auto sx = [](std::uint64_t v, unsigned n) { return std::int64_t(v << (64 - n)) >> (64 - n); };
    A::Deps d;
    d.stall = bits(105, 4); d.yield = bits(109, 1); d.write_barrier = bits(110, 3); d.read_barrier = bits(113, 3);
    d.wait_mask = bits(116, 6); d.reuse = bits(122, 4);
    const unsigned pred = bits(12, 3), dst = bits(16, 8), op = bits(0, 9), form = bits(9, 3);
    const bool neg = bits(15, 1);
    const auto r0 = A::Src::R(bits(24, 8), bits(72, 1), bits(73, 1));
    const auto r2 = A::Src::R(bits(64, 8), bits(75, 1), bits(74, 1));
    const auto rA = A::Src::R(bits(24, 8)), rC = A::Src::R(bits(64, 8));   // integer sources: no float modifiers
    // The 32-63 slot by form: 1 register, 2/4 immediate, 3/5 constant, 6/7 uniform register.
    const auto slotAs = [&](bool mods) -> A::Src {
        const bool n = mods && bits(63, 1), a = mods && bits(62, 1);
        switch (form) {
        case 1: return A::Src::R(bits(32, 8), n, a);
        case 2: case 4: return A::Src::I(bits(32, 32));
        case 3: case 5:
            if (bits(91, 1)) throw std::runtime_error("bindless constant");
            return A::Src::C(bits(54, 5), bits(38, 16), n, a);
        case 6: case 7: return A::Src::U(bits(32, 6), n, a);
        default: throw std::runtime_error("form");
        }
    };
    const auto slot = [&]() { return slotAs(true); };
    const auto slotPlain = [&]() { return slotAs(false); };
    const bool slot1 = form == 1 || form == 4 || form == 5 || form == 6;   // the slot holds source b
    const bool slot2 = form == 2 || form == 3 || form == 7;                // the slot holds source c
    const A::Round rnd = A::Round(bits(78, 2));
    const bool sat = bits(77, 1), ftz = bits(80, 1);
    const auto off48 = [&]() { return sx(std::uint64_t(bits(34, 30)) | std::uint64_t(bits(64, 18)) << 30, 48) * 4; };
    const auto off24 = [&]() { return std::int32_t(sx(bits(40, 24), 24)); };
    const auto texture = [&](unsigned cbufOpcode, unsigned kind) -> bool {
        // kind: 0 TEX, 1 TLD, 2 TLD4, 3 TXQ
        const unsigned opc = bits(0, 12);
        A::TexHandle h;
        if (opc == cbufOpcode) h = A::TexHandle::C(bits(54, 5), bits(40, 14));
        else if (opc == (cbufOpcode | 0x400)) h = A::TexHandle::U(bits(40, 6), bits(46, 8));
        else h = A::TexHandle::Bindless();
        const A::TexRegs r{bits(16, 8), bits(64, 8), bits(24, 8), bits(32, 8)};
        A::TexOpts o;
        o.mask = bits(72, 4); o.nodep = bits(90, 1); o.split = bits(60, 1); o.fault = bits(81, 3); o.evict = bits(84, 3);
        o.lod = A::TexLod(bits(87, 3));
        const auto dim = A::TexDim(bits(61, 3));
        switch (kind) {
        case 0: o.aoffi = bits(76, 1); o.ndv = bits(77, 1); o.dc = bits(78, 1); out = A::Tex(r, h, dim, o, d, pred, neg); break;
        case 1: o.aoffi = bits(76, 1); o.ms = bits(78, 1); out = A::Tld(r, h, dim, o, d, pred, neg); break;
        case 2:
            if (bits(76, 2) == 3) return false;
            o.aoffi = bits(76, 2) == 1; o.ptp = bits(76, 2) == 2; o.dc = bits(78, 1); o.comp = bits(87, 2);
            out = A::Tld4(r, h, dim, o, d, pred, neg);
            break;
        default: out = A::Txq(r, h, A::TexQuery(bits(62, 2)), bits(72, 4), bits(90, 1), d, pred, neg); break;
        }
        return true;
    };
    try {
        // Opcodes taken whole (12 bits): control flow, special registers, textures, memory, the uniform datapath.
        switch (bits(0, 12)) {
        case 0x326:
            if (bits(32, 8) != A::RZ) return false;
            out = A::Ipa(dst, bits(64, 8) * 4, A::InterpFreq(bits(78, 2)), A::InterpLoc(bits(76, 2)), d, pred, neg);
            return true;
        case 0x94d: out = A::Exit(d, pred, neg, bits(87, 3), bits(90, 1)); return true;
        case 0x918: out = A::Nop(d); return true;
        case 0x95b: out = A::Kill(d, pred, neg); return true;
        case 0x947: out = A::Bra(off48(), d, pred, neg, bits(87, 3), bits(90, 1)); return true;
        case 0x945: out = A::BSsy(bits(16, 4), sx(bits(34, 30), 30) * 4, d, pred, neg); return true;
        case 0x941: out = A::BSync(bits(16, 4), d, pred, neg); return true;
        case 0x942: out = A::Break(bits(16, 4), bits(87, 3), bits(90, 1), d, pred, neg); return true;
        case 0x944: out = A::Call(off48(), bits(86, 1), d, pred, neg); return true;
        case 0x950: out = A::Ret(bits(24, 8), off48(), bits(85, 1), bits(86, 1), d, pred, neg); return true;
        case 0x948: out = A::WarpSync(bits(32, 32), d, pred, neg); return true;
        case 0x355: out = A::BMovFromBar(dst, bits(24, 4), bits(84, 1), d, pred, neg); return true;
        case 0x356: out = A::BMovToBar(bits(24, 4), bits(32, 8), bits(84, 1), d, pred, neg); return true;
        case 0x919: out = A::S2R(dst, A::SR(bits(72, 8)), d, pred, neg); return true;
        case 0x805: out = A::CS2R(dst, A::SR(bits(72, 8)), bits(80, 1), d, pred, neg); return true;
        case 0x91a: out = A::DepBar(bits(44, 3), bits(38, 6), bits(47, 1), bits(32, 6), d, pred, neg); return true;
        case 0x81c:
            out = A::PLop3(bits(81, 3), {bits(87, 3), bits(90, 1) != 0}, {bits(77, 3), bits(80, 1) != 0},
                           {bits(68, 3), bits(71, 1) != 0, bits(67, 1) != 0}, bits(64, 3) | bits(72, 5) << 3, bits(84, 3), bits(16, 8), d, pred, neg);
            return true;
        case 0x361: case 0xb60: case 0xf60: return texture(0xb60, 0);
        case 0x367: case 0xb66: case 0xf66: return texture(0xb66, 1);
        case 0x364: case 0xb63: case 0xf63: return texture(0xb63, 2);
        case 0x370: case 0xb6f: case 0xf6f: return texture(0xb6f, 3);
        case 0xab9: case 0xabb: {
            if (bits(73, 3) != 4 && bits(73, 3) != 5) return false;
            out = A::Uldc(bits(16, 6), bits(54, 5), bits(38, 16), bits(73, 3) == 5, bits(0, 12) == 0xabb ? bits(24, 6) : A::URZ,
                          d, pred, neg);
            return true;
        }
        case 0x882: out = A::UMov(bits(16, 6), A::Src::I(bits(32, 32)), d, pred, neg); return true;
        case 0xc82: out = A::UMov(bits(16, 6), A::Src::U(bits(32, 6)), d, pred, neg); return true;
        case 0x3c2: out = A::R2UR(bits(16, 6), bits(24, 8), d, pred, neg); return true;
        case 0x9c3: out = A::S2UR(bits(16, 6), A::SR(bits(72, 8)), d, pred, neg); return true;
        case 0xb82:
            out = A::Ldc(dst, bits(54, 5), bits(38, 16), bits(24, 8), A::MemType(bits(73, 3)), d, pred, neg, bits(78, 2));
            return true;
        case 0x983: out = A::Ldl(dst, bits(24, 8), off24(), A::MemType(bits(73, 3)), bits(84, 3), d, pred, neg); return true;
        case 0x387: out = A::Stl(bits(24, 8), off24(), bits(32, 8), A::MemType(bits(73, 3)), d, pred, neg); return true;
        case 0x984:
            out = A::Lds(dst, bits(24, 8), off24(), A::MemType(bits(73, 3)), A::Stride(bits(78, 2)), d, pred, neg);
            return true;
        case 0x388:
            out = A::Sts(bits(24, 8), off24(), bits(32, 8), A::MemType(bits(73, 3)), A::Stride(bits(78, 2)), d, pred, neg);
            return true;
        case 0x381: case 0x981:
            out = A::Ldg(dst, bits(0, 12) == 0x981 ? A::Src::U(bits(32, 6)) : A::Src::R(bits(24, 8)), off24(),
                         A::MemType(bits(73, 3)), A::MemOrder(bits(77, 4)), bits(72, 1), d, pred, neg, bits(24, 8), bits(90, 1));
            return true;
        case 0x386:
            out = A::Stg(bits(24, 8), off24(), bits(32, 8), A::MemType(bits(73, 3)), A::MemOrder(bits(77, 4)), bits(72, 1), d,
                         pred, neg);
            return true;
        case 0x38c:
            out = A::Atoms(dst, bits(24, 8), off24(), bits(32, 8), A::AtomOp(bits(87, 4)), A::AtomType(bits(73, 3)),
                           A::Stride(bits(78, 2)), d, pred, neg);
            return true;
        // Attribute memory and geometry output (the .PHYS forms with an offset register are not taken).
        case 0x321:
            if (bits(24, 8) != A::RZ || bits(77, 1)) return false;
            out = A::Ald(dst, bits(40, 10), bits(74, 2) + 1, bits(32, 8), bits(79, 1), bits(76, 1), d, pred, neg);
            return true;
        case 0x322:
            if (bits(24, 8) != A::RZ || bits(77, 1)) return false;
            out = A::Ast(bits(40, 10), bits(32, 8), bits(74, 2) + 1, bits(64, 8), bits(76, 1), d, pred, neg);
            return true;
        case 0x324: case 0x924:
            out = A::Out(A::OutType(bits(78, 2)), dst, bits(24, 8), form == 4 ? A::Src::I(bits(32, 32)) : A::Src::R(bits(32, 8)),
                         d, pred, neg);
            return true;
        case 0x923:
            if (bits(74, 2) || bits(77, 2)) return false;
            out = A::Isberd(dst, bits(24, 8), bits(40, 16), bits(79, 1), bits(76, 1), d, pred, neg);
            return true;
        case 0x806: out = A::Vote(A::VoteOp(bits(72, 2)), dst, bits(87, 3), bits(90, 1), bits(81, 3), d, pred, neg); return true;
        case 0x886: out = A::VoteU(A::VoteOp(bits(72, 2)), bits(16, 6), bits(87, 3), bits(90, 1), bits(81, 3), d, pred, neg); return true;
        case 0xb1d:
            if (bits(77, 3) > 1) return false;
            out = A::Bar(bits(54, 4), bits(42, 12), bits(77, 3) == 1, bits(80, 1), d, pred, neg);
            return true;
        case 0x34e: out = A::Lepc(dst, d, pred, neg); return true;
        case 0x357: out = A::BMovAtExitPC(bits(32, 8), d, pred, neg); return true;
        case 0x957:
            out = A::BMovAtExitPCImm(std::uint64_t(bits(32, 32)) | std::uint64_t(bits(64, 17)) << 32, d, pred, neg);
            return true;
        // Surfaces: the handle in a register, a constant bank or (SULD, SUST) a uniform register.
        case 0x998: case 0xb97: case 0xf97:
        case 0x99c: case 0xb9b: case 0xf9b:
        case 0x394: case 0x593:
        case 0x3a0: case 0x59f: {
            const unsigned opc = bits(0, 12);
            A::SurfHandle h = A::SurfHandle::R(bits(64, 8));
            if (opc == 0xb97 || opc == 0xb9b || opc == 0x593 || opc == 0x59f) h = A::SurfHandle::C(bits(54, 5), bits(40, 14));
            else if (opc == 0xf97 || opc == 0xf9b) h = A::SurfHandle::U(bits(40, 6), bits(46, 8));
            const auto dim = A::SurfDim(bits(61, 3));
            const auto clamp = A::SurfClamp(bits(59, 2));
            const auto order = A::MemOrder(bits(77, 4));
            if (opc == 0x998 || opc == 0xb97 || opc == 0xf97)
                out = A::SuLd(dst, bits(24, 8), h, dim, bits(72, 4), clamp, order, bits(84, 3), bits(81, 3), d, pred, neg);
            else if (opc == 0x99c || opc == 0xb9b || opc == 0xf9b)
                out = A::SuSt(bits(24, 8), bits(32, 8), h, dim, bits(72, 4), clamp, order, bits(84, 3), d, pred, neg);
            else if (opc == 0x394 || opc == 0x593)
                out = A::SuAtom(dst, bits(24, 8), bits(32, 8), h, dim, A::AtomOp(bits(87, 4)), A::AtomType(bits(73, 3)), order,
                                clamp, bits(84, 3), bits(81, 3), bits(72, 1), d, pred, neg);
            else
                out = A::SuRed(bits(24, 8), bits(32, 8), h, dim, A::AtomOp(bits(87, 3)), A::AtomType(bits(73, 3)), order, clamp,
                               bits(84, 3), bits(72, 1), d, pred, neg);
            return true;
        }
        default: break;
        }
        // ALU opcodes (9 bits, the form in bits 9-11).
        switch (op) {
        case 0x021:   // FADD a, b: b a register in the slot (form 1), else in src2's slot (2/3/7: immediate, constant, uniform)
            if (form != 1 && form != 2 && form != 3 && form != 7) return false;
            out = A::FAdd(dst, r0, slot(), sat, ftz, rnd, d, pred, neg);
            return true;
        case 0x020:   // FMUL a, b
            if (!slot1) return false;
            out = A::FMul(dst, r0, slot(), sat, ftz, rnd, d, pred, neg, bits(84, 3));
            return true;
        case 0x023:   // FFMA a, b, c: forms 1/4/5/6 b in the slot; 2/3/7 c in the slot and b in src2's field
            if (slot1) out = A::FFma(dst, r0, slot(), r2, sat, ftz, rnd, d, pred, neg);
            else if (slot2) out = A::FFma(dst, r0, r2, slot(), sat, ftz, rnd, d, pred, neg);
            else return false;
            return true;
        case 0x002:
            if (!slot1) return false;
            out = A::Mov(dst, slot(), d, pred, neg);
            return true;
        case 0x108:
            if (!slot1) return false;
            out = A::MuFu(A::MuFuOp(bits(74, 6)), dst, slot(), d, pred, neg);
            return true;
        case 0x009:
            if (!slot1 || bits(87, 3) != A::PT) return false;
            out = A::FMnMx(dst, r0, slot(), !bits(90, 1), ftz, d, pred, neg);
            return true;
        case 0x008:
            if (!slot1) return false;
            out = A::FSel(dst, r0, slot(), bits(87, 3), bits(90, 1), d, pred, neg);
            return true;
        case 0x00b:
            if (!slot1 || bits(84, 3) != A::PT) return false;
            out = A::FSetP(bits(81, 3), r0, slot(), A::Cmp(bits(76, 4)), A::PredOp(bits(74, 2)), bits(87, 3), bits(90, 1), ftz, d, pred, neg);
            return true;
        case 0x00a:
            if (!slot1) return false;
            out = A::FSet(dst, r0, slot(), A::Cmp(bits(76, 4)), A::PredOp(bits(74, 2)), bits(87, 3), bits(90, 1), ftz, d, pred, neg);
            return true;
        case 0x010: case 0x012: case 0x024: {   // IADD3, LOP3, IMAD: a, b, c like FFMA
            A::Src b, c;
            if (slot1) b = slot(), c = r2;
            else if (slot2) b = r2, c = slot();
            else return false;
            if (op == 0x010)
                out = A::IAdd3(dst, r0, b, c, d, pred, neg, bits(81, 3), bits(84, 3), bits(74, 1), bits(87, 3), bits(90, 1),
                               bits(77, 3), bits(80, 1));
            else if (op == 0x012) out = A::Lop3(dst, r0, b, c, bits(72, 8), d, pred, neg, bits(81, 3));
            else out = A::IMad(dst, r0, b, c, bits(73, 1), d, pred, neg, bits(74, 1), bits(87, 3), bits(90, 1));
            return true;
        }
        case 0x00c:
            if (!slot1) return false;
            out = A::ISetP(bits(81, 3), r0, slot(), A::ICmp(bits(76, 3)), bits(73, 1), A::PredOp(bits(74, 2)), bits(87, 3),
                           bits(90, 1), d, pred, neg);
            return true;
        case 0x007:
            if (!slot1) return false;
            out = A::Sel(dst, rA, slotPlain(), bits(87, 3), bits(90, 1), d, pred, neg);
            return true;
        case 0x019:
            if (!slot1) return false;
            out = A::Shf(dst, rA, slotPlain(), rC, bits(76, 1), A::ShfType(bits(73, 2)), bits(80, 1), bits(75, 1), d, pred, neg);
            return true;
        case 0x017:
            if (!slot1 || bits(87, 3) != A::PT) return false;
            out = A::IMnMx(dst, rA, slotPlain(), !bits(90, 1), bits(73, 1), d, pred, neg);
            return true;
        case 0x016:
            if (!slot1) return false;
            out = A::Prmt(dst, rA, slotPlain(), rC, A::PrmtMode(bits(72, 3)), d, pred, neg);
            return true;
        case 0x011:
            if (!slot1) return false;
            out = A::Lea(dst, rA, slotPlain(), bits(75, 5), bits(80, 1), bits(73, 1), rC, d, pred, neg, bits(81, 3), bits(72, 1));
            return true;
        case 0x004:
            if (form != 4) return false;
            out = A::R2P(rA, bits(32, 32), d, pred, neg);
            return true;
        case 0x003:
            if (form != 4 || bits(24, 8) != A::RZ) return false;
            out = A::P2R(dst, bits(32, 32), d, pred, neg);
            return true;
        case 0x105:
            if (!slot1) return false;
            out = A::F2I(dst, slot(), A::IntType(bits(75, 2) * 2 + bits(72, 1)), rnd, ftz, 8u << bits(84, 2), bits(77, 1), d, pred, neg);
            return true;
        case 0x106:
            if (!slot1) return false;
            out = A::I2F(dst, slot(), A::IntType(bits(84, 2) * 2 + bits(74, 1)), 8u << bits(75, 2), rnd, bits(60, 2), d, pred, neg);
            return true;
        case 0x045:
            if (!slot1) return false;
            out = A::I2FP(dst, slot(), bits(74, 1), rnd, d, pred, neg);
            return true;
        case 0x107:
            if (!slot1 || bits(75, 2) != bits(84, 2)) return false;
            out = A::FRnd(dst, slot(), rnd, ftz, 8u << bits(84, 2), d, pred, neg);
            return true;
        case 0x109:
            if (!slot1) return false;
            out = A::PopC(dst, slotPlain(), bits(63, 1), d, pred, neg);
            return true;
        case 0x100:
            if (!slot1 || bits(81, 3) != A::PT) return false;
            out = A::Flo(dst, slotPlain(), bits(73, 1), bits(74, 1), bits(63, 1), d, pred, neg);
            return true;
        case 0x013:
            if (!slot1) return false;
            out = A::IAbs(dst, slotPlain(), d, pred, neg);
            return true;
        case 0x03e:
            if (!slot1) return false;
            out = A::F2FP(dst, rA, slotPlain(), A::Round(bits(79, 2)), d, pred, neg);
            return true;
        case 0x038:
            if (!slot1) return false;
            out = A::I2I(dst, slotPlain(), A::IntType(bits(76, 2)), d, pred, neg);
            return true;
        case 0x104:
            if (!slot1) return false;
            out = A::F2F(dst, slot(), 8u << bits(75, 2), 8u << bits(84, 2), rnd, ftz, d, pred, neg);
            return true;
        // Uniform ALU: registers are uniform; an immediate is src1 (form 4) or src2 (form 2, src1 then in 64-69).
        case 0x092: case 0x090: case 0x099: {
            if (form != 1 && form != 2 && form != 4) return false;
            const auto ua = A::Src::U(bits(24, 6), bits(72, 1));
            const auto ub = form == 4 ? A::Src::I(bits(32, 32))
                          : form == 2 ? A::Src::U(bits(64, 6), bits(75, 1)) : A::Src::U(bits(32, 6), bits(63, 1));
            const auto uc = form == 2 ? A::Src::I(bits(32, 32)) : A::Src::U(bits(64, 6), bits(75, 1));
            if (op == 0x092) out = A::ULop3(bits(16, 6), ua, ub, uc, bits(72, 8), d, pred, neg, bits(81, 3));
            else if (op == 0x090)
                out = A::UIAdd3(bits(16, 6), ua, ub, uc, d, pred, neg, bits(81, 3), bits(84, 3), bits(74, 1), bits(87, 3),
                                bits(90, 1), bits(77, 3), bits(80, 1));
            else out = A::UShf(bits(16, 6), ua, ub, uc, bits(76, 1), A::ShfType(bits(73, 2)), bits(80, 1), bits(75, 1), d, pred, neg);
            return true;
        }
        case 0x030:   // HADD2 a, b: b a register in the slot (form 1), else in src2's slot (2/3/7), as FADD
            if (form != 1 && form != 2 && form != 3 && form != 7) return false;
            out = A::HAdd2(dst, r0, slot(), A::HSwz(bits(74, 2)), A::HSwz(bits(60, 2)), bits(78, 1), sat, ftz, d, pred, neg);
            return true;
        case 0x034:   // HSETP2, forms as HADD2
            if (form != 1 && form != 2 && form != 3 && form != 7) return false;
            out = A::HSetP2(bits(81, 3), r0, slot(), A::Cmp(bits(76, 4)), A::HSwz(bits(74, 2)), A::HSwz(bits(60, 2)),
                            A::PredOp(bits(69, 2)), bits(87, 3), bits(90, 1), ftz, bits(71, 1), bits(84, 3), d, pred, neg);
            return true;
        case 0x08c: case 0x087: {   // UISETP, USEL: b a uniform register (form 1) or an immediate (form 4)
            if (form != 1 && form != 4) return false;
            const auto ua = A::Src::U(bits(24, 6));
            const auto ub = form == 4 ? A::Src::I(bits(32, 32)) : A::Src::U(bits(32, 6));
            if (op == 0x087) out = A::USel(bits(16, 6), ua, ub, bits(87, 3), bits(90, 1), d, pred, neg);
            else {
                if (bits(72, 1) || bits(84, 3) != A::UPT) return false;   // .EX
                out = A::UISetP(bits(81, 3), ua, ub, A::ICmp(bits(76, 3)), bits(73, 1), A::PredOp(bits(74, 2)), bits(87, 3),
                                bits(90, 1), d, pred, neg);
            }
            return true;
        }
        default: return false;
        }
    } catch (const std::exception&) {
        return false;
    }
}

static int ReencodeAll(const std::string& path, std::size_t every, bool showNotTaken) {
    const Bytes data = ReadFile(path);
    const Archive a = ReadArchive(data);
    std::map<std::uint32_t, std::array<std::size_t, 3>> byOp;   // same, different, not taken
    std::map<std::pair<std::uint32_t, unsigned>, std::size_t> diffBits;
    for (std::size_t s = 0; s < a.shaders.size(); s += every) {
        Payload p;
        try { p = ReadPayload(a.Payload(s)); } catch (const std::exception&) { continue; }
        const auto stage = a.shaders[s].frequency >= 5 ? Stage::Compute : p.bindings.stage;
        for (const auto& g : ReadNativeProgram(p.code, stage).instructions) {
            // The ALU opcodes Reencode takes count by their 9 bits (all forms together), the others by all 12.
            static const std::set<std::uint32_t> alu = {0x002, 0x003, 0x004, 0x007, 0x008, 0x009, 0x00a, 0x00b, 0x00c, 0x010, 0x011,
                                                        0x012, 0x013, 0x016, 0x017, 0x019, 0x020, 0x021, 0x023, 0x024, 0x030, 0x034,
                                                        0x038, 0x03e, 0x045, 0x087, 0x08c, 0x090, 0x092, 0x099, 0x100, 0x104, 0x105,
                                                        0x106, 0x107, 0x108, 0x109};
            const auto key = alu.count(g.Bits(0, 9)) ? g.Bits(0, 9) : g.Opcode();
            A::Word w;
            if (!Reencode(g, w)) {
                if (byOp[key][2]++ < 2 && showNotTaken)
                    std::printf("  op %03x not taken: %08x %08x %08x %08x\n", key, g.words[0], g.words[1], g.words[2], g.words[3]);
                continue;
            }
            if (w.words == g.words) { byOp[key][0]++; continue; }
            if (byOp[key][1]++ < 2)   // the first differing instructions, game words then the encoder's
                std::printf("  op %03x differs: %08x %08x %08x %08x, encoded %08x %08x %08x %08x\n", key, g.words[0], g.words[1],
                            g.words[2], g.words[3], w.words[0], w.words[1], w.words[2], w.words[3]);
            for (unsigned b = 0; b < 128; ++b)
                if (((w.words[b / 32] ^ g.words[b / 32]) >> (b % 32)) & 1) diffBits[{key, b}]++;
        }
    }
    for (auto& [op, n] : byOp) std::printf("op %03x: %zu same, %zu different, %zu not taken\n", op, n[0], n[1], n[2]);
    for (auto& [k, n] : diffBits) std::printf("  op %03x bit %u differs %zu times\n", k.first, k.second, n);
    return 0;
}

// ---- Vertex and geometry programs: how they read inputs and write outputs, against their program headers ----------
//   own_sage_tool stages <archive> [examples]
// For vertex (frequency 0) and geometry (4) programs: the header's common words decoded (NVIDIA's SPH field names),
// the attribute addresses ALD reads and AST writes compared with the header's input and output maps (30-byte bitmaps of
// attribute words from byte 0x14 and 0x32, bit n = attribute address 4n, as the game's headers lay them out), how
// programs end, and where vertex handles come from. Pixel programs' common words are counted beside them.
static int Stages(const std::string& path, unsigned examples) {
    const Bytes data = ReadFile(path);
    const Archive a = ReadArchive(data);
    using Map = std::array<bool, 240>;
    const auto headerMap = [](const std::array<std::uint32_t, 32>& h, unsigned byte0) {
        Map m{};
        for (unsigned b = 0; b < 240; ++b) {
            const unsigned byte = byte0 + b / 8;
            m[b] = (h[byte / 4] >> (8 * (byte % 4) + b % 8)) & 1;
        }
        return m;
    };
    struct Count { std::map<std::string, std::size_t> n; void Add(const std::string& k) { n[k]++; } };
    std::map<unsigned, std::map<std::string, Count>> stats;   // frequency -> topic -> value -> programs
    std::map<unsigned, std::size_t> programs;
    std::map<unsigned, unsigned> shown;
    char buf[256];
    for (std::size_t s = 0; s < a.shaders.size(); ++s) {
        const unsigned f = a.shaders[s].frequency;
        if (f != 0 && f != 3 && f != 4) continue;
        Payload p;
        try { p = ReadPayload(a.Payload(s)); } catch (const std::exception&) { continue; }
        const NativeProgram n = ReadNativeProgram(p.code, p.bindings.stage);
        const auto& h = n.header;
        programs[f]++;
        auto& st = stats[f];
        const auto add = [&](const char* topic, const char* fmt, auto... v) { std::snprintf(buf, sizeof buf, fmt, v...); st[topic].Add(buf); };
        add("w0 type/version/shader_type", "%u/%u/%u", h[0] & 31, (h[0] >> 5) & 31, (h[0] >> 10) & 15);
        add("w0 mrt,kills,global_store,sass_version,reserved21-25,load_store,fp64,stream_out", "%u,%u,%u,%u,%#x,%u,%u,%#x",
            (h[0] >> 14) & 1, (h[0] >> 15) & 1, (h[0] >> 16) & 1, (h[0] >> 17) & 15, (h[0] >> 21) & 31, (h[0] >> 26) & 1,
            (h[0] >> 27) & 1, h[0] >> 28);
        add("w1 local_low,per_patch", "%#x,%u", h[1] & 0xffffff, h[1] >> 24);
        add("w2 local_high,threads_per_input_primitive", "%#x,%u", h[2] & 0xffffff, h[2] >> 24);
        add("w3 crs_size,output_topology,reserved", "%#x,%u,%#x", h[3] & 0xffffff, (h[3] >> 24) & 15, h[3] >> 28);
        add("w4 max_output_vertices,store_req_start,reserved,store_req_end", "%u,%u,%#x,%u", h[4] & 0xfff, (h[4] >> 12) & 0xff,
            (h[4] >> 20) & 15, h[4] >> 24);
        unsigned tailNonzero = 0;
        for (unsigned w = 0x50 / 4; w < 32; ++w) tailNonzero += h[w] != 0;
        add("words 0x50-0x7f nonzero", "%u", tailNonzero);
        if (f == 3) continue;
        // Attribute traffic in program order; the last writer of each register, for the vertex handles.
        Map reads{}, writes{};
        std::map<unsigned, std::string> writer;   // register -> what last wrote it
        std::ostringstream trace;
        unsigned alds = 0, asts = 0, ipas = 0, outs = 0, isberds = 0, exits = 0;
        bool anyOut = false;
        std::size_t lastExit = 0;
        for (std::size_t k = 0; k < n.instructions.size(); ++k) {
            const auto& i = n.instructions[k];
            const unsigned opc = i.Bits(0, 12), dst = i.Bits(16, 8);
            std::string wrote;
            if (opc == 0x321) {   // ALD
                ++alds;
                const unsigned addr = i.Bits(40, 10), comps = i.Bits(74, 2) + 1, vtx = i.Bits(32, 8);
                const bool out = i.Bits(79, 1), patch = i.Bits(76, 1);
                anyOut |= out;
                if (!out && !patch) for (unsigned c = 0; c < comps; ++c) if ((addr / 4 + c) < 240) reads[addr / 4 + c] = true;
                add("ALD address.comps (programs)", "a[%#x].%u%s%s", addr, comps, out ? " .O" : "", patch ? " .P" : "");
                add("ALD vertex handle from", "%s", vtx == 255 ? "RZ" : writer.count(vtx) ? writer[vtx].c_str() : "nothing earlier");
                if (trace.tellp() < 1500) trace << " ALD" << (comps > 1 ? "." + std::to_string(comps * 32) : "") << " R" << dst << ",a["
                                                << std::hex << "0x" << addr << std::dec << "]" << (vtx == 255 ? "" : ",R" + std::to_string(vtx)) << ";";
                wrote = "ALD";
            } else if (opc == 0x322) {   // AST
                ++asts;
                const unsigned addr = i.Bits(40, 10), comps = i.Bits(74, 2) + 1, vtx = i.Bits(64, 8);
                for (unsigned c = 0; c < comps; ++c) if ((addr / 4 + c) < 240) writes[addr / 4 + c] = true;
                add("AST address.comps (programs)", "a[%#x].%u", addr, comps);
                add("AST vertex handle from", "%s", vtx == 255 ? "RZ" : writer.count(vtx) ? writer[vtx].c_str() : "nothing earlier");
                if (trace.tellp() < 1500) trace << " AST" << (comps > 1 ? "." + std::to_string(comps * 32) : "") << " a[0x" << std::hex << addr
                                                << std::dec << "],R" << i.Bits(32, 8) << (vtx == 255 ? "" : ",R" + std::to_string(vtx)) << ";";
            } else if (opc == 0x326) { ++ipas; wrote = "IPA"; }
            else if (opc == 0x324 || opc == 0x924) {
                ++outs;
                static const char* type[] = {"FINAL", "EMIT", "CUT", "EMIT_THEN_CUT"};
                const unsigned handle = i.Bits(24, 8);
                add("OUT type", "%s", type[i.Bits(78, 2)]);
                add("OUT handle from", "%s", writer.count(handle) ? writer[handle].c_str() : "nothing earlier");
                if (trace.tellp() < 1500) trace << " OUT." << type[i.Bits(78, 2)] << " R" << dst << ",R" << handle << ";";
                wrote = "OUT";
            } else if (opc == 0x923) {
                ++isberds;
                const unsigned src = i.Bits(24, 8);
                add("ISBERD index from", "%s", writer.count(src) ? writer[src].c_str() : "nothing earlier");
                if (trace.tellp() < 1500) trace << " ISBERD R" << dst << ",[R" << src << "+0x" << std::hex << i.Bits(40, 16) << std::dec << "];";
                wrote = "ISBERD";
            } else if (opc == 0x94d) { ++exits; lastExit = k; }
            else if (opc == 0x919) { std::snprintf(buf, sizeof buf, "S2R SR %#x", i.Bits(72, 8)); wrote = buf; }
            else if (opc == 0x805) wrote = "CS2R";
            else if ((opc & 0x1ff) == 0x002) wrote = i.Bits(9, 3) == 4 ? "MOV imm" : "MOV";
            else if ((opc & 0x1ff) == 0x010 || (opc & 0x1ff) == 0x024 || (opc & 0x1ff) == 0x012) wrote = "integer ALU";
            if (dst != 255 && opc != 0x322 && opc != 0x94d && opc != 0x947 && opc != 0x918) writer[dst] = wrote.empty() ? "other" : wrote;
        }
        add("w0 bit 25 / reads a[0x2fc] / reads a[0x2f8] / any ALD .O", "%u/%u/%u/%u", (h[0] >> 25) & 1, unsigned(reads[0x2fc / 4]),
            unsigned(reads[0x2f8 / 4]), unsigned(anyOut));
        {
            bool tex = false, ldg = false, bra = false, ldc = false, s2r = false;
            for (const auto& i : n.instructions) {
                const unsigned o = i.Bits(0, 12);
                tex |= (o & 0x3f0) == 0x360 || (o & 0x1f0) == 0x160;
                ldg |= o == 0x381 || o == 0x981; bra |= o == 0x947; ldc |= o == 0xb82; s2r |= o == 0x919;
            }
            add("w0 bit 25 / texture / LDG / BRA / LDC / S2R", "%u/%u/%u/%u/%u/%u", (h[0] >> 25) & 1, tex, ldg, bra, ldc, s2r);
            add("w0 bit 25 / program name", "%u %s", (h[0] >> 25) & 1, p.bindings.name.find("Instanced") != std::string::npos ? "Instanced" :
                p.bindings.name.find("GPUSkin") != std::string::npos ? "GPUSkin" : p.bindings.name.find("FLocalVertexFactory") != std::string::npos ? "Local" : "other");
        }
        add("counts ALD/AST/IPA/OUT/ISBERD > 0", "%u/%u/%u/%u/%u", alds > 0, asts > 0, ipas > 0, outs > 0, isberds > 0);
        add("EXIT count", "%u", exits);
        // What precedes the last EXIT: the run of ASTs right before it, and the instruction before that run.
        std::size_t k = lastExit, run = 0;
        while (k > 0 && n.instructions[k - 1].Bits(0, 12) == 0x322) --k, ++run;
        add("ASTs right before the last EXIT", "%zu", run);
        if (k > 0) add("instruction before that (opcode)", "%03x", n.instructions[k - 1].Bits(0, 12));
        const Map hi = headerMap(h, 0x14), ho = headerMap(h, 0x32);
        std::string inDiff, outDiff;
        for (unsigned b = 0; b < 240; ++b) {
            if (reads[b] != hi[b]) { std::snprintf(buf, sizeof buf, " %s%#x", reads[b] ? "code:" : "map:", b * 4); inDiff += buf; }
            if (writes[b] != ho[b]) { std::snprintf(buf, sizeof buf, " %s%#x", writes[b] ? "code:" : "map:", b * 4); outDiff += buf; }
        }
        add("input map == ALD reads", "%s", inDiff.empty() ? "yes" : ("no:" + inDiff.substr(0, 120)).c_str());
        add("output map == AST writes", "%s", outDiff.empty() ? "yes" : ("no:" + outDiff.substr(0, 120)).c_str());
        if (shown[f]++ < examples) {
            std::printf("example f%u shader %zu %s\n  header", f, s, p.bindings.name.c_str());
            for (unsigned w = 0; w < 24; ++w) std::printf(" %08x", h[w]);
            std::printf("\n  imap/omap differ: in[%s] out[%s]\n  trace:%s\n", inDiff.c_str(), outDiff.c_str(), trace.str().c_str());
        }
    }
    for (auto& [f, topics] : stats) {
        std::printf("== frequency %u: %zu programs\n", f, programs[f]);
        for (auto& [topic, c] : topics) {
            std::multimap<std::size_t, std::string, std::greater<>> top;
            for (auto& [k, v] : c.n) top.emplace(v, k);
            std::printf("  %s (%zu values):", topic.c_str(), c.n.size());
            int i = 0;
            for (auto& [v, k] : top) { if (i++ == 40) break; std::printf(" [%s]x%zu", k.c_str(), v); }
            std::printf("\n");
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    try {
        const std::string command = argc > 1 ? argv[1] : "";
        if (command == "roundtrip" && argc == 3) return Roundtrip(argv[2]);
        if (command == "maps" && argc == 4) return Maps(argv[2], argv[3]);
        if (command == "payload" && argc == 5) return OnePayload(argv[2], std::stoul(argv[3]), argv[4]);
        if (command == "reencode" && argc >= 3)
            return ReencodeAll(argv[2], argc > 3 ? std::stoul(argv[3]) : 1, argc > 4 && std::string(argv[4]) == "show");
        if (command == "magenta" && argc >= 6) return MagentaMod(argc, argv);
        if (command == "control" && argc >= 3) {   // per program: facts from the code, then the control words
            const Bytes data = ReadFile(argv[2]);
            const Archive a = ReadArchive(data);
            const std::size_t every = argc > 3 ? std::stoul(argv[3]) : 1;
            for (std::size_t s = 0; s < a.shaders.size(); s += every) {
                Payload p;
                try { p = ReadPayload(a.Payload(s)); } catch (const std::exception&) { continue; }
                const bool compute = a.shaders[s].frequency >= 5;
                const NativeProgram n = ReadNativeProgram(p.code, compute ? Stage::Compute : p.bindings.stage);
                unsigned maxReg = 0, maxUReg = 0, locals = 0, bars = 0;
                for (const auto& i : n.instructions) {
                    const auto op = i.Opcode() & 0x1ff;
                    const bool uniform = (op >= 0x280 && op < 0x2c0) || op == 0xab9 % 0x200 || op == 0x1c3;
                    for (unsigned r : {i.Dst(), i.SrcA()})
                        if (r != 255) { if (uniform) maxUReg = std::max(maxUReg, r); else maxReg = std::max(maxReg, r); }
                    if (op == 0x187 || op == 0x183) ++locals;   // STL, LDL
                    if (op == 0x11d) ++bars;                    // BAR
                }
                // The constant table: the first word after the final branch that is neither a NOP nor zero.
                std::size_t table = 0, at = 0;
                while (at + 16 <= n.trailer.size() && Get32(&n.trailer[at]) == 0x00007918u) at += 16;
                for (std::size_t b = at; b + 4 <= n.trailer.size(); b += 4) if (Get32(&n.trailer[b])) { table = b; break; }
                const std::size_t instrBytes = n.instructions.size() * 16, header = n.has_header ? 128 : 0;
                std::printf("%zu f%u code %zu instr %zu nops_end %zu table %zu maxreg %u maxureg %u locals %u bars %u ctl %zu |",
                            s, a.shaders[s].frequency, p.code.size(), instrBytes, header + instrBytes + at,
                            table ? header + instrBytes + table : 0, maxReg, maxUReg, locals, bars, p.control.size());
                for (std::size_t w = 0; w + 4 <= p.control.size(); w += 4) std::printf(" %x", Get32(&p.control[w]));
                std::printf("\n");
            }
            return 0;
        }
        if (command == "hashes" && argc == 3) {   // distinct layout hashes the programs carry, with counts
            const Bytes data = ReadFile(argv[2]);
            const Archive a = ReadArchive(data);
            std::map<std::uint32_t, int> seen;
            for (std::size_t s = 0; s < a.shaders.size(); ++s) {
                try { for (auto h : ReadPayload(a.Payload(s)).bindings.layout_hashes) if (h) seen[h]++; } catch (const std::exception&) {}
            }
            for (auto& [h, n] : seen) std::printf("%u %d\n", h, n);
            return 0;
        }
        if (command == "samples" && argc == 4) return Samples(argv[2], argv[3]);
        if (command == "code" && argc == 5) {   // one program's instructions (no header, no trailer), for nvdisasm
            const Bytes data = ReadFile(argv[2]);
            const Archive a = ReadArchive(data);
            const std::size_t s = std::stoul(argv[3]);
            const Payload p = ReadPayload(a.Payload(s));
            Bytes out;
            for (const auto& i : ReadNativeProgram(p.code, a.shaders[s].frequency >= 5 ? Stage::Compute : p.bindings.stage).instructions)
                for (auto w : i.words) Put32(out, w);
            WriteFile(argv[4], out);
            return 0;
        }
        if (command == "stages" && argc >= 3) return Stages(argv[2], argc > 3 ? unsigned(std::stoul(argv[3])) : 2);
        if (command == "stream" && argc >= 4) return Stream(argv[2], argv[3], argc > 4 ? std::stoul(argv[4]) : 1);
        std::cerr << "usage: see the comment at the top of tests/own_sage_tool.cpp\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}

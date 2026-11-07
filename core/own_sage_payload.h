// Pikmin 4's Switch 2 shader format, SF_GLSL_SAGE (docs/SWITCH2_SHADERS.md), the portable half: the shader library
// (FSerializedShaderArchive version 2, as on Switch 1), each program's payload, and the native code inside it.
//
// A payload is the Switch 1 GLSL_SWITCH payload (own_payload.h) with its bindings reordered and trimmed: no packed
// uniform buffers, no packed globals and no named varyings, a 20-byte hash and the shader's name before the code, and
// an NVN 2 control section (magic 0x98761234) after it. The code is native GPU code for the console's Ampere-class
// GPU: a 32-word program header (graphics stages only) and then 128-bit instruction words in NVIDIA's Volta and later
// encoding, which nvdisasm -b SM86 reads. There is no NV assembly text.
//
// ReadPayload and WritePayload round-trip every program of the 2.0.0 and demo libraries byte for byte
// (tests/own_sage_tool.cpp). Field names marked "observed" are read from the values in the libraries, not from the
// game's loader; the doc says what each rests on.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace P4MapKit { namespace CustomMaterial { namespace OwnSage {

using Bytes = std::vector<std::uint8_t>;

constexpr std::uint32_t PayloadMarker = 0x474c534cu;   // "LSLG" in the file: 'GLSL' read as a little-endian word
constexpr std::uint32_t ControlMagic = 0x98761234u;    // first word of the NVN 2 control section
constexpr std::uint32_t ArchiveVersion = 2;
constexpr std::size_t ProgramHeaderBytes = 128;        // 32 words before the first instruction (graphics stages)
constexpr const char* ShaderFormat = "SF_GLSL_SAGE";   // ShaderArchive-<library>-SF_GLSL_SAGE.ushaderbytecode

// The two letters after the marker; UE's frequency in the archive entry says the same thing.
// Ray tracing programs keep other values ('S1' ...), which are kept as they are.
enum class Stage : std::uint16_t { Vertex = 0x5653, Pixel = 0x5053, Geometry = 0x4753, Compute = 0x4353 };
// UE 4.27 EShaderFrequency values used by the libraries' entries.
enum class Frequency : std::uint8_t { Vertex = 0, Pixel = 3, Geometry = 4, Compute = 5, RayGen = 6, RayMiss = 7, RayHitGroup = 8 };

inline std::uint32_t Get32(const std::uint8_t* p) { std::uint32_t v; std::memcpy(&v, p, 4); return v; }
inline std::uint64_t Get64(const std::uint8_t* p) { std::uint64_t v; std::memcpy(&v, p, 8); return v; }
inline void Put16(Bytes& out, std::uint16_t v) { out.push_back(std::uint8_t(v)); out.push_back(std::uint8_t(v >> 8)); }
inline void Put32(Bytes& out, std::uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(std::uint8_t(v >> (8 * i))); }
inline void PutWords(Bytes& out, const std::vector<std::uint32_t>& words) {
    Put32(out, std::uint32_t(words.size()));
    for (auto w : words) Put32(out, w);
}

class SageReader {
    const Bytes& data;
    std::size_t at = 0;
public:
    explicit SageReader(const Bytes& b, std::size_t start = 0) : data(b), at(start) {}
    void need(std::size_t n) const {
        if (at > data.size() || n > data.size() - at) throw std::runtime_error("sage payload: truncated");
    }
    std::uint8_t u8() { need(1); return data[at++]; }
    std::uint16_t u16() { need(2); std::uint16_t v = std::uint16_t(data[at] | data[at + 1] << 8); at += 2; return v; }
    std::uint32_t u32() { need(4); auto v = Get32(&data[at]); at += 4; return v; }
    std::int32_t i32() { return std::int32_t(u32()); }
    std::uint32_t count(std::uint32_t limit, const char* what) {
        auto n = u32();
        if (n > limit) throw std::runtime_error(std::string("sage payload: too many ") + what);
        return n;
    }
    std::vector<std::uint32_t> words(const char* what) {
        auto n = count(4096, what);
        need(std::size_t(n) * 4);
        std::vector<std::uint32_t> v(n);
        for (auto& w : v) w = u32();
        return v;
    }
    Bytes bytes(std::size_t n) { need(n); Bytes v(data.begin() + std::ptrdiff_t(at), data.begin() + std::ptrdiff_t(at + n)); at += n; return v; }
    std::size_t offset() const { return at; }
};

// What the program binds, as the engine's resource table describes it (own_payload.h's ResourceTable format: one
// head word per uniform buffer, then (buffer << 24 | resource index << 8 | slot) rows, ended by 0xffffffff).
struct Bindings {
    Stage stage = Stage::Pixel;
    std::uint32_t resource_bits = 0;   // the uniform buffers whose resources the tables bind
    std::vector<std::uint32_t> srv, samplers, uav, layout_hashes, textures;
    // Observed: a vertex attribute mask in vertex programs (1 = position only, 0xd83f, 0x1f ...), 0 in pixel programs.
    std::uint32_t attribute_mask = 0;
    // Observed: the uniform buffer slot of the loose parameters ($Globals) and its size in bytes; -1 and 0 for none
    // (the two agree on every program of both libraries).
    std::int32_t globals_buffer = -1;
    std::uint32_t globals_bytes = 0;
    std::uint32_t word3 = 0;           // the handle table's size in c[0x10]: HandleTableBytes
    std::array<std::uint8_t, 4> counts = {{0, 0, 0, 0}};  // observed: slots (texture, sampler, uniform buffer, storage)
    std::uint32_t word4 = 0;           // observed: 0, and 3 in the few compute programs
    std::uint8_t byte5 = 0;            // observed: always 0
    std::array<std::uint8_t, 20> hash{};  // observed: SHA-1; SHA-1 of nothing in programs without varyings
    std::string name;                  // "<material>/<vertex factory>/<shader type>/<permutation>:<usf path>"
};

// The 44 bytes after the control section. Words 4 and 5 repeat the control and code sizes; the last word is the size
// of the payload's optional data (just this word), as on Switch 1.
struct Tail {
    std::array<std::uint32_t, 11> words{};
    std::int32_t default_sampler() const { return std::int32_t(words[6]); }  // observed -1 everywhere
    std::uint32_t scratch_bytes() const { return words[7]; }                // observed 0 or 0x48000 with word 8 0x200
};

struct Payload {
    Bindings bindings;
    Bytes code;     // program header (graphics stages) and instructions, as the GPU reads them
    Bytes control;  // NVN 2 control section
    Tail tail;
};

inline Payload ReadPayload(const Bytes& payload) {
    if (payload.size() > (8u << 20)) throw std::runtime_error("sage payload: too large");
    SageReader r(payload);
    Payload p;
    auto& b = p.bindings;
    if (r.u32() != PayloadMarker) throw std::runtime_error("sage payload: no GLSL marker");
    const auto stage = r.u16();
    // 'VS', 'PS', 'GS', 'CS'; the ray tracing programs of the Global library carry 'S1', 'S2' and so on.
    if ((stage & 0xff) != 'S') throw std::runtime_error("sage payload: unknown stage");
    b.stage = Stage(stage);
    b.resource_bits = r.u32();
    b.srv = r.words("srv rows");
    b.samplers = r.words("sampler rows");
    b.uav = r.words("uav rows");
    b.layout_hashes = r.words("layout hashes");
    b.textures = r.words("texture rows");
    b.attribute_mask = r.u32();
    b.globals_buffer = r.i32();
    b.globals_bytes = r.u32();
    b.word3 = r.u32();
    for (auto& c : b.counts) c = r.u8();
    b.word4 = r.u32();
    b.byte5 = r.u8();
    for (auto& c : b.hash) c = r.u8();
    const auto length = r.i32();
    if (length >= 0) {
        auto name = r.bytes(std::size_t(length));
        if (!name.empty() && name.back() == 0) name.pop_back();
        b.name.assign(name.begin(), name.end());
    } else
        throw std::runtime_error("sage payload: UTF-16 shader names are not read");
    if (r.u32() != 0) throw std::runtime_error("sage payload: uniform buffer copy info is not read");
    p.code = r.bytes(r.count(4u << 20, "code bytes"));
    p.control = r.bytes(r.count(1u << 16, "control bytes"));
    if (p.control.size() < 16 || Get32(p.control.data()) != ControlMagic)
        throw std::runtime_error("sage payload: control section without the NVN 2 magic");
    for (auto& w : p.tail.words) w = r.u32();
    if (r.offset() != payload.size())  // one ray tracing hit group in the Global library
        throw std::runtime_error("sage payload: more than one code section is not read");
    if (p.tail.words[4] != p.control.size() || p.tail.words[5] != p.code.size() || p.tail.words[10] != 4)
        throw std::runtime_error("sage payload: tail does not repeat the section sizes");
    return p;
}

inline Bytes WritePayload(const Payload& p) {
    const auto& b = p.bindings;
    Bytes out;
    Put32(out, PayloadMarker);
    Put16(out, std::uint16_t(b.stage));
    Put32(out, b.resource_bits);
    for (const auto* table : {&b.srv, &b.samplers, &b.uav, &b.layout_hashes, &b.textures}) PutWords(out, *table);
    Put32(out, b.attribute_mask);
    Put32(out, std::uint32_t(b.globals_buffer));
    Put32(out, b.globals_bytes);
    Put32(out, b.word3);
    for (auto c : b.counts) out.push_back(c);
    Put32(out, b.word4);
    out.push_back(b.byte5);
    out.insert(out.end(), b.hash.begin(), b.hash.end());
    Put32(out, std::uint32_t(b.name.size() + 1));
    out.insert(out.end(), b.name.begin(), b.name.end());
    out.push_back(0);
    Put32(out, 0);
    Put32(out, std::uint32_t(p.code.size()));
    out.insert(out.end(), p.code.begin(), p.code.end());
    Put32(out, std::uint32_t(p.control.size()));
    out.insert(out.end(), p.control.begin(), p.control.end());
    auto tail = p.tail.words;
    tail[4] = std::uint32_t(p.control.size());
    tail[5] = std::uint32_t(p.code.size());
    for (auto w : tail) Put32(out, w);
    return out;
}

// ---- Control section for a program written from scratch ----------------------------------------------------------

// The NVN 2 control section of a new program: a game program's of the same stage and shape (`base`) with the fields
// fitted on 5,925 of the game's programs replaced (docs/SWITCH2_SHADERS.md, section 2): word 14 the constant table
// offset, words 16 and 17 the code size, word 19 the registers + 2, word 20 the local memory per thread. The two
// hashes (words 4-5, 10-11) are kept; their function is not known.
inline Bytes SageControl(const Bytes& base, std::uint32_t codeBytes, std::uint32_t tableOffset, std::uint32_t registers,
                         std::uint32_t localBytes) {
    if (base.size() < 96 || Get32(base.data()) != ControlMagic) throw std::runtime_error("sage control: no base control section");
    if (registers + 2 > 255 || tableOffset > codeBytes) throw std::runtime_error("sage control: sizes out of range");
    Bytes out = base;
    const auto put = [&](unsigned word, std::uint32_t v) { std::memcpy(&out[word * 4], &v, 4); };
    put(14, tableOffset);
    put(16, codeBytes);
    put(17, codeBytes);
    put(19, registers + 2);
    put(20, localBytes);
    return out;
}

// ---- Native code ------------------------------------------------------------------------------------------------

// One 128-bit instruction. Fields are NVIDIA's Volta-and-later layout (as Mesa NAK's sm70_encode.rs writes them):
// opcode in bits 0-11, guard predicate 12-15, the first registers at 16 and 24, scheduling control in 105-127.
struct Instruction {
    std::array<std::uint32_t, 4> words{};
    std::uint32_t Bits(unsigned offset, unsigned count) const {
        std::uint64_t v = words[offset / 32];
        if (offset / 32 + 1 < 4) v |= std::uint64_t(words[offset / 32 + 1]) << 32;
        return std::uint32_t((v >> (offset % 32)) & ((std::uint64_t(1) << count) - 1));
    }
    void SetBits(unsigned offset, unsigned count, std::uint32_t value) {
        for (unsigned i = 0; i < count; ++i) {
            const unsigned bit = offset + i, w = bit / 32, s = bit % 32;
            words[w] = (words[w] & ~(1u << s)) | (((value >> i) & 1u) << s);
        }
    }
    std::uint32_t Opcode() const { return Bits(0, 12); }
    std::uint32_t Predicate() const { return Bits(12, 3); }    // 7 = PT (always)
    bool PredicateNegated() const { return Bits(15, 1) != 0; }
    std::uint32_t Dst() const { return Bits(16, 8); }          // 255 = RZ
    std::uint32_t SrcA() const { return Bits(24, 8); }
    std::uint32_t SrcB() const { return Bits(32, 8); }         // register form
    std::uint32_t Immediate() const { return words[1]; }        // immediate form (bits 32-63)
    std::uint32_t ConstOffset() const { return Bits(38, 16); }  // constant form: c[bank][byte offset]
    std::uint32_t ConstBank() const { return Bits(54, 5); }
    std::uint32_t SrcC() const { return Bits(64, 8); }
    // Scheduling control.
    std::uint32_t Stall() const { return Bits(105, 4); }
    bool Yield() const { return Bits(109, 1) != 0; }
    std::uint32_t WriteBarrier() const { return Bits(110, 3); }  // 7 = none
    std::uint32_t ReadBarrier() const { return Bits(113, 3); }   // 7 = none
    std::uint32_t WaitMask() const { return Bits(116, 6); }
    std::uint32_t Reuse() const { return Bits(122, 4); }
};

// Opcodes (bits 0-11) of the game's programs that the doc's checks name; the full list read by nvdisasm is in
// docs/SWITCH2_SHADERS.md. Bit 9 selects the immediate form and bit 11 the constant form for most ALU opcodes
// (0x?2?/0x?a?/0x?c? below), as in NAK's ALU encoding.
namespace Op {
constexpr std::uint32_t Mov = 0x002, MovImm = 0x802, MovConst = 0xa02;
constexpr std::uint32_t FAdd = 0x221, FMul = 0x220, FFma = 0x223, MuFu = 0x308, Cs2r = 0x805;
constexpr std::uint32_t Ipa = 0x326, Exit = 0x94d, Bra = 0x947, S2r = 0x919, Kill = 0x95b;
}  // namespace Op

inline bool IsSelfBranch(const Instruction& i) {
    return i.Opcode() == Op::Bra && i.Predicate() == 7 && !i.PredicateNegated() && i.words[1] == 0xfffffff0u &&
           (i.words[2] & 0x3ffffu) == 0x3ffffu;
}

// A program as the code bytes hold it: the header of a graphics program, the instructions up to and including the
// unconditional branch to itself that ends every program, and what follows, kept as it is: NOP padding, the
// program's own constant table (which the code reads through a constant bank the control section places), zeros.
// Rewriting a program in place keeps the code's size so the constant table stays where the control section says.
struct NativeProgram {
    bool has_header = false;
    std::array<std::uint32_t, 32> header{};
    std::vector<Instruction> instructions;
    Bytes trailer;   // bytes after the final branch
};

// The program header's first words (NVIDIA's Shader Program Header; version 4 here, 32 words).
struct HeaderFacts {
    unsigned sph_type = 0, version = 0, shader_type = 0;   // shader_type: 1 vertex, 4 geometry, 5 pixel
    bool mrt = false, kills = false;
    std::uint32_t omap_target = 0;   // pixel: 4 bits per render target, the components written
    bool writes_depth = false, writes_sample_mask = false;
};

inline HeaderFacts ReadHeaderFacts(const std::array<std::uint32_t, 32>& h) {
    HeaderFacts f;
    f.sph_type = h[0] & 31;
    f.version = (h[0] >> 5) & 31;
    f.shader_type = (h[0] >> 10) & 15;
    f.mrt = (h[0] >> 14) & 1;
    f.kills = (h[0] >> 15) & 1;
    if (f.shader_type == 5) {
        f.omap_target = h[18];
        f.writes_sample_mask = h[19] & 1;
        f.writes_depth = (h[19] >> 1) & 1;
    }
    return f;
}

// Pixel outputs live in consecutive registers from R0: each enabled component of render target 0, then of target 1,
// and so on, then depth (as NVIDIA's pixel output convention and NAK's lowering place them).
inline std::vector<unsigned> PixelOutputRegisters(const HeaderFacts& f, unsigned target, unsigned& first) {
    unsigned reg = 0;
    std::vector<unsigned> regs;
    for (unsigned rt = 0; rt < 8; ++rt) {
        for (unsigned c = 0; c < 4; ++c)
            if ((f.omap_target >> (rt * 4 + c)) & 1) {
                if (rt == target) regs.push_back(reg);
                ++reg;
            }
    }
    first = regs.empty() ? 0 : regs.front();
    return regs;
}

inline NativeProgram ReadNativeProgram(const Bytes& code, Stage stage) {
    NativeProgram p;
    std::size_t at = 0;
    if (stage != Stage::Compute) {
        if (code.size() < ProgramHeaderBytes) throw std::runtime_error("sage code: no program header");
        for (unsigned i = 0; i < 32; ++i) p.header[i] = Get32(&code[i * 4]);
        p.has_header = true;
        at = ProgramHeaderBytes;
    }
    if ((code.size() - at) % 16) throw std::runtime_error("sage code: not whole 128-bit instructions");
    for (; at < code.size(); at += 16) {
        Instruction i;
        for (unsigned w = 0; w < 4; ++w) i.words[w] = Get32(&code[at + w * 4]);
        p.instructions.push_back(i);
        if (IsSelfBranch(i)) { at += 16; break; }
    }
    if (p.instructions.empty() || !IsSelfBranch(p.instructions.back()))
        throw std::runtime_error("sage code: no final branch to itself");
    p.trailer.assign(code.begin() + std::ptrdiff_t(at), code.end());
    return p;
}

inline Bytes WriteNativeProgram(const NativeProgram& p) {
    Bytes out;
    if (p.has_header) for (auto w : p.header) Put32(out, w);
    for (const auto& i : p.instructions) for (auto w : i.words) Put32(out, w);
    out.insert(out.end(), p.trailer.begin(), p.trailer.end());
    return out;
}

// ---- The handle table's size (payload word 3) --------------------------------------------------------------------

// The slots a resource table's rows name.
inline std::vector<unsigned> TableSlots(const std::vector<std::uint32_t>& t) {
    std::size_t head = t.size();
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (t[i] == 0xffffffffu) { head = i; break; }
        if (t[i]) { head = t[i]; break; }
    }
    std::vector<unsigned> out;
    for (std::size_t i = head; i < t.size() && t[i] != 0xffffffffu; ++i) out.push_back(t[i] & 0xff);
    return out;
}

// Payload word 3: the bytes of the program's handle table in c[0x10], 16 per resource slot up to the highest slot the
// resource tables name or the code reads (constant operands, LDC/ULDC, constant-bank texture handles), at least 32.
// Fitted on every 23rd program of both 2.0.0 libraries: 1,001/1,001 pixel, 390/390 vertex, 7/7 geometry programs
// (compute programs differ and are not written here).
inline std::uint32_t HandleTableBytes(const Bindings& b, const Bytes& code, bool graphics) {
    long top = -1;
    for (const auto* t : {&b.srv, &b.samplers, &b.uav, &b.textures})
        for (unsigned s : TableSlots(*t)) top = std::max(top, long(16 * s));
    for (std::size_t at = graphics ? ProgramHeaderBytes : 0; at + 16 <= code.size(); at += 16) {
        Instruction i;
        for (unsigned w = 0; w < 4; ++w) i.words[w] = Get32(&code[at + w * 4]);
        const std::uint32_t op = i.Opcode(), form = (op >> 9) & 7, base = op & 0x1ff;
        if (IsSelfBranch(i)) break;
        if (op == 0xb66 || op == 0xb60 || op == 0xb6f || op == 0xb63) {   // texture handle in c[bank][4 * index]
            if (i.Bits(54, 5) == 0x10) top = std::max(top, long(i.Bits(40, 14) * 4));
        } else if (((form == 3 || form == 5) && base < 0x100) || op == 0xb82 || op == 0xab9 || op == 0xabb) {
            if (i.ConstBank() == 0x10) top = std::max(top, long(i.ConstOffset()));
        }
    }
    return top < 0 ? 32u : std::max(32u, std::uint32_t((top / 16 + 1) * 16));
}

// ---- The library ------------------------------------------------------------------------------------------------

// A raw LZ4 block (lz4/doc/lz4_Block_format.md), as the archive compresses payloads.
inline Bytes Lz4Block(const std::uint8_t* in, std::size_t n, std::size_t expected) {
    Bytes out;
    out.reserve(expected);
    std::size_t at = 0;
    const auto length = [&](std::size_t base) {
        std::size_t len = base;
        if (base == 15) {
            std::uint8_t b;
            do {
                if (at >= n) throw std::runtime_error("lz4: truncated length");
                b = in[at++];
                len += b;
            } while (b == 255);
        }
        return len;
    };
    while (at < n) {
        const std::uint8_t token = in[at++];
        const std::size_t literals = length(token >> 4);
        if (literals > n - at || out.size() + literals > expected) throw std::runtime_error("lz4: literals overrun");
        out.insert(out.end(), in + at, in + at + literals);
        at += literals;
        if (at == n) break;
        if (n - at < 2) throw std::runtime_error("lz4: truncated offset");
        const std::size_t offset = std::size_t(in[at] | in[at + 1] << 8);
        at += 2;
        const std::size_t match = length(token & 15) + 4;
        if (!offset || offset > out.size() || out.size() + match > expected) throw std::runtime_error("lz4: bad match");
        const std::size_t from = out.size() - offset;
        for (std::size_t k = 0; k < match; ++k) out.push_back(out[from + k]);
    }
    if (out.size() != expected) throw std::runtime_error("lz4: wrong size");
    return out;
}

using Hash = std::array<std::uint8_t, 20>;
struct MapEntry { std::uint32_t first = 0, count = 0, first_preload = 0, preloads = 0; };
struct ShaderEntry { std::uint64_t offset = 0; std::uint32_t size = 0, uncompressed = 0; std::uint8_t frequency = 0; };

// FSerializedShaderArchive version 2: the hashes of the shader maps and of the shaders, each map's run of indices,
// each shader's place and size in the payload area, the preload ranges, then the payloads (LZ4 where size differs).
struct Archive {
    std::vector<Hash> map_hashes, shader_hashes;
    std::vector<MapEntry> maps;
    std::vector<ShaderEntry> shaders;
    std::vector<std::uint32_t> indices;
    const Bytes* data = nullptr;
    std::size_t payloads_at = 0;

    // Shader indices of map m, in the map's order.
    std::vector<std::uint32_t> MapShaders(std::size_t m) const {
        const auto& e = maps.at(m);
        return std::vector<std::uint32_t>(indices.begin() + e.first, indices.begin() + e.first + e.count);
    }
    Bytes Payload(std::size_t s) const {
        const auto& e = shaders.at(s);
        const std::uint8_t* at = data->data() + payloads_at + e.offset;
        if (e.size == e.uncompressed) return Bytes(at, at + e.size);
        return Lz4Block(at, e.size, e.uncompressed);
    }
};

// The archive keeps a pointer to `data`, which must outlive it.
inline Archive ReadArchive(const Bytes& data) {
    Archive a;
    a.data = &data;
    std::size_t at = 0;
    const auto u32 = [&]() {
        if (data.size() < at + 4) throw std::runtime_error("sage archive: truncated");
        auto v = Get32(&data[at]);
        at += 4;
        return v;
    };
    const auto array = [&](std::size_t stride) {
        const auto n = u32();
        if (n > (1u << 24) || (data.size() - at) / stride < n) throw std::runtime_error("sage archive: array overrun");
        const std::size_t start = at;
        at += std::size_t(n) * stride;
        return std::make_pair(start, n);
    };
    if (u32() != ArchiveVersion) throw std::runtime_error("sage archive: not version 2");
    const auto mh = array(20), sh = array(20), me = array(16), se = array(17), pe = array(16), si = array(4);
    (void)pe;
    a.payloads_at = at;
    for (std::uint32_t i = 0; i < mh.second; ++i) { Hash h; std::memcpy(h.data(), &data[mh.first + i * 20], 20); a.map_hashes.push_back(h); }
    for (std::uint32_t i = 0; i < sh.second; ++i) { Hash h; std::memcpy(h.data(), &data[sh.first + i * 20], 20); a.shader_hashes.push_back(h); }
    for (std::uint32_t i = 0; i < me.second; ++i) {
        const auto* p = &data[me.first + i * 16];
        a.maps.push_back({Get32(p), Get32(p + 4), Get32(p + 8), Get32(p + 12)});
    }
    for (std::uint32_t i = 0; i < se.second; ++i) {
        const auto* p = &data[se.first + std::size_t(i) * 17];
        a.shaders.push_back({Get64(p), Get32(p + 8), Get32(p + 12), p[16]});
    }
    for (std::uint32_t i = 0; i < si.second; ++i) a.indices.push_back(Get32(&data[si.first + i * 4]));
    if (a.map_hashes.size() != a.maps.size() || a.shader_hashes.size() != a.shaders.size())
        throw std::runtime_error("sage archive: hash and entry counts differ");
    for (const auto& m : a.maps)
        if (m.first > a.indices.size() || m.count > a.indices.size() - m.first) throw std::runtime_error("sage archive: map outside indices");
    for (auto i : a.indices) if (i >= a.shaders.size()) throw std::runtime_error("sage archive: index outside shaders");
    for (const auto& s : a.shaders)
        if (s.offset > data.size() - a.payloads_at || s.size > data.size() - a.payloads_at - s.offset)
            throw std::runtime_error("sage archive: payload outside the file");
    return a;
}

}}}  // namespace P4MapKit::CustomMaterial::OwnSage

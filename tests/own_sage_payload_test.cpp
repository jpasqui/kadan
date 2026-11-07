// core/own_sage_payload.h on programs made here (no game bytes): payload and code round trips, the program header's
// pixel outputs, instruction fields at the positions nvdisasm -b SM86 confirmed on the game's programs, LZ4 blocks
// and a library written by Kadan's archive writer.
#include "../core/own_sage_payload.h"
#include "../core/custom_material_archive.h"
#include <functional>
#include <iostream>
using namespace P4MapKit::CustomMaterial;
using namespace P4MapKit::CustomMaterial::OwnSage;

static unsigned checks = 0;
static void Check(bool ok, const char* what) {
    ++checks;
    if (!ok) throw std::runtime_error(std::string("own sage payload test failed: ") + what);
}
static void Refuses(const std::function<void()>& action, const char* what) {
    bool rejected = false;
    try { action(); } catch (const std::runtime_error&) { rejected = true; }
    Check(rejected, what);
}

static Instruction Encode(std::uint32_t opcode, std::uint32_t dst, std::uint32_t immediate = 0) {
    Instruction i;
    i.SetBits(0, 12, opcode);
    i.SetBits(12, 3, 7);          // PT
    i.SetBits(16, 8, dst);
    i.words[1] = immediate;
    i.SetBits(105, 4, 1);         // stall 1
    i.SetBits(110, 3, 7);         // no write barrier
    i.SetBits(113, 3, 7);         // no read barrier
    return i;
}

int main() {
    // A pixel program: header (SPH type 2, version 4, pixel, MRT), RT0 rgba; magenta into R0-R3; EXIT; BRA to itself.
    NativeProgram program;
    program.has_header = true;
    program.header[0] = 2u | (4u << 5) | (5u << 10) | (1u << 14);
    program.header[18] = 0xf;
    const float one = 1.0f;
    std::uint32_t oneBits;
    std::memcpy(&oneBits, &one, 4);
    for (std::uint32_t r : {0u, 2u, 3u}) program.instructions.push_back(Encode(Op::MovImm, r, oneBits));
    program.instructions.push_back(Encode(Op::MovImm, 1, 0));
    program.instructions.push_back(Encode(Op::Exit, 0));
    Instruction self = Encode(Op::Bra, 0, 0xfffffff0u);
    self.SetBits(64, 18, 0x3ffff);
    program.instructions.push_back(self);
    program.trailer = {0x18, 0x79, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xc0, 0x0f, 0};   // a NOP
    for (int i = 0; i < 16; ++i) program.trailer.push_back(std::uint8_t(i));               // a constant table
    program.trailer.resize(program.trailer.size() + 32, 0);

    const Bytes code = WriteNativeProgram(program);
    Check(code.size() == ProgramHeaderBytes + 6 * 16 + program.trailer.size(), "code size");
    const NativeProgram back = ReadNativeProgram(code, Stage::Pixel);
    Check(back.instructions.size() == 6 && back.trailer == program.trailer, "code split");
    Check(WriteNativeProgram(back) == code, "code round trip");
    Check(IsSelfBranch(back.instructions.back()) && !IsSelfBranch(back.instructions[4]), "final branch");
    const auto facts = ReadHeaderFacts(back.header);
    Check(facts.sph_type == 2 && facts.version == 4 && facts.shader_type == 5 && facts.mrt && facts.omap_target == 0xf,
          "header facts");
    unsigned first = 9;
    Check(PixelOutputRegisters(facts, 0, first) == std::vector<unsigned>({0, 1, 2, 3}) && first == 0, "RT0 registers");
    auto two = facts;
    two.omap_target = 0x9f;   // RT0 rgba, RT1 r and a
    Check(PixelOutputRegisters(two, 1, first) == std::vector<unsigned>({4, 5}) && first == 4, "RT1 registers");
    Refuses([&] { Bytes cut(code.begin(), code.begin() + std::ptrdiff_t(ProgramHeaderBytes + 4 * 16)); ReadNativeProgram(cut, Stage::Pixel); },
            "code without a final branch");

    // Fields: FFMA R5, R5, c[0x4][0x1bc], R2 guarded by !P1, as nvdisasm prints the game's constant form.
    Instruction ffma = Encode(0xa23, 5);
    ffma.SetBits(12, 3, 1);
    ffma.SetBits(15, 1, 1);
    ffma.SetBits(24, 8, 5);
    ffma.SetBits(38, 16, 0x1bc);
    ffma.SetBits(54, 5, 4);
    ffma.SetBits(64, 8, 2);
    ffma.SetBits(116, 6, 0x3f);
    Check(ffma.Opcode() == 0xa23 && ffma.Predicate() == 1 && ffma.PredicateNegated() && ffma.Dst() == 5 && ffma.SrcA() == 5,
          "ALU fields");
    Check(ffma.ConstBank() == 4 && ffma.ConstOffset() == 0x1bc && ffma.SrcC() == 2 && ffma.WaitMask() == 0x3f, "constant operand");
    Check(ffma.Stall() == 1 && ffma.WriteBarrier() == 7 && ffma.ReadBarrier() == 7, "scheduling fields");

    // A payload around it.
    Payload p;
    p.bindings.stage = Stage::Pixel;
    p.bindings.resource_bits = 0x5;
    p.bindings.srv = {3, 0, 0, 0x00000201u, 0xffffffffu};
    p.bindings.samplers = {3, 0, 0, 0x00000302u, 0xffffffffu};
    p.bindings.uav = {0, 0, 0, 0xffffffffu};
    p.bindings.layout_hashes = {0x12345678u, 0, 0x0d480502u};
    p.bindings.textures = {3, 0, 0, 0x00000105u, 0xffffffffu};
    p.bindings.globals_buffer = 1;
    p.bindings.globals_bytes = 32;
    p.bindings.word3 = 64;
    p.bindings.counts = {{1, 1, 3, 1}};
    for (std::size_t i = 0; i < 20; ++i) p.bindings.hash[i] = std::uint8_t(i * 7);
    p.bindings.name = "M_Test/FLocalVertexFactory/TBasePassPSFNoLightMapPolicy/0:/Engine/Private/BasePassPixelShader.usf";
    p.code = code;
    p.control.assign(224, 0);
    for (int i = 0; i < 4; ++i) p.control[i] = std::uint8_t(ControlMagic >> (8 * i));
    p.tail.words = {{0, 1, 0, 0, 0, 0, 0xffffffffu, 0, 0, 0, 4}};
    const Bytes payload = WritePayload(p);
    const Payload read = ReadPayload(payload);
    Check(read.bindings.name == p.bindings.name && read.bindings.textures == p.bindings.textures &&
          read.bindings.layout_hashes == p.bindings.layout_hashes && read.bindings.globals_bytes == 32 &&
          read.bindings.globals_buffer == 1 && read.bindings.hash == p.bindings.hash && read.code == code,
          "payload fields");
    Check(read.tail.words[4] == 224 && read.tail.words[5] == code.size() && read.tail.default_sampler() == -1, "tail sizes");
    Check(WritePayload(read) == payload, "payload round trip");
    Refuses([&] { Bytes bad = payload; bad[0] ^= 1; ReadPayload(bad); }, "marker");
    Refuses([&] { Bytes bad = payload; bad.push_back(0); ReadPayload(bad); }, "trailing bytes");
    Refuses([&] { Payload bad = p; bad.control[0] = 0; ReadPayload(WritePayload(bad)); }, "control magic");

    // LZ4: 4 literals, a match of 8 at offset 4, then a last literal.
    const std::uint8_t block[] = {0x44, 'a', 'b', 'c', 'd', 4, 0, 0x10, 'x'};
    const Bytes text = Lz4Block(block, sizeof block, 13);
    Check(std::string(text.begin(), text.end()) == "abcdabcdabcdx", "lz4");
    Refuses([&] { Lz4Block(block, sizeof block, 12); }, "lz4 size");

    // The library: two maps sharing the pixel program, written by Kadan's archive writer, read back here.
    const Bytes hashA(20, 1), hashB(20, 2);
    const auto archiveBytes = NativeBackend::WriteOwnedShaderArchive({{hashA, {{3, payload}}}, {hashB, {{3, payload}, {3, payload}}}});
    const Archive archive = ReadArchive(archiveBytes);
    Check(archive.maps.size() == 2 && archive.shaders.size() == 1 && archive.MapShaders(1) == std::vector<std::uint32_t>({0, 0}),
          "archive maps");
    Check(archive.Payload(0) == payload && archive.shaders[0].frequency == 3, "archive payload");
    Refuses([&] { Bytes bad = archiveBytes; bad[0] = 3; ReadArchive(bad); }, "archive version");

    // Word 3: the handle table up to the highest slot (tables: texture slot 5 -> 96 bytes), at least 32.
    Check(HandleTableBytes(p.bindings, code, true) == 96, "handle table from the tables");
    Bindings none;
    Check(HandleTableBytes(none, code, true) == 32, "handle table minimum");
    NativeProgram withConst = program;
    withConst.instructions.insert(withConst.instructions.begin(), Encode(0xa02, 7));
    withConst.instructions.front().SetBits(38, 16, 0x1a0);
    withConst.instructions.front().SetBits(54, 5, 0x10);
    Check(HandleTableBytes(none, WriteNativeProgram(withConst), true) == 0x1b0, "handle table from the code");

    std::cout << "own sage payload: " << checks << " checks passed\n";
    return 0;
}

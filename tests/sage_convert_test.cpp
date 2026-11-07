// core/sage_convert.h: Kadan's Switch 1 programs convert to Switch 2 programs (docs/SWITCH2_SHADERS.md, section 8).
// Built by tools/check.py against the staged compiler (its staged tests): every program of own_shaders/ and a pixel
// program with a texture, a storage buffer, a derivative, half floats and a View member that moved in 2.0.0.
#include "own_payload.h"
#include "sage_convert.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

using namespace P4MapKit::CustomMaterial;
namespace S = OwnSage;

static int failures = 0;
static void Expect(bool ok, const std::string& what) {
    if (!ok) { std::cerr << "FAIL " << what << "\n"; ++failures; }
}

static S::Bytes Load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return S::Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Every constant operand (bank, offset) and opcode of the program's instructions.
struct Code {
    std::vector<std::pair<unsigned, unsigned>> constants;
    std::vector<unsigned> opcodes;
};
static Code Read(const S::Payload& p) {
    Code c;
    const auto program = S::ReadNativeProgram(p.code, p.bindings.stage);
    for (const auto& i : program.instructions) {
        const unsigned op = i.Opcode();
        c.opcodes.push_back(op);
        if (((op >> 9) & 7) == 5 && (op & 0x1ff) < 0x100) c.constants.push_back({i.ConstBank(), i.ConstOffset()});
        if (op == 0xa11) c.constants.push_back({i.ConstBank(), i.ConstOffset()});   // LEA with a constant
    }
    return c;
}
template <class T, class V> static bool Has(const std::vector<T>& list, const V& v) {
    return std::find(list.begin(), list.end(), v) != list.end();
}

static const char* kPixel = R"(!!NVfp5.0
OPTION NV_internal;
OPTION NV_bindless_texture;
OPTION NV_shader_storage_buffer;
STORAGE sbo_buf0[] = { program.storage[0] };
CBUFFER buf0[] = { program.buffer[0] };
CBUFFER buf14[] = { program.buffer[14] };
ATTRIB fragment_attrib2[] = { fragment.attrib[2..2] };
TEMP R0, R1, R2, R3;
LONG TEMP D0, D1;
OUTPUT result_color0 = result.color;
MOV.F R0, fragment.attrib[2];
LDC.U64 D0.x, buf14[328];
LDC.U64 D1.x, buf14[1352];
OR.U64 D0.x, D0, D1;
TEX.F R1, R0, handle(D0.x), 2D;
LDB.F32X4 R2, sbo_buf0[16];
DDX.F R3.xy, R0;
UP2H.F R3.zw, R2.x;
MAD.F R1, R1, R2, R3;
LDC.F32 R0.x, buf0[3840];
MUL.F result_color0, R1, R0.x;
END
)";

int main() {
    // Kadan's own vertex and geometry programs.
    unsigned converted = 0;
    for (const auto& entry : std::filesystem::directory_iterator("own_shaders")) {
        if (entry.path().extension() != ".payload") continue;
        const std::string name = entry.path().stem().string();
        const auto in = Load(entry.path());
        try {
            const auto out = S::ConvertSwitchPayloadToSage(in);
            const auto p = S::ReadPayload(out.payload);
            Expect(S::WritePayload(p) == out.payload, name + ": payload round trip");
            const auto facts = S::ReadHeaderFacts(S::ReadNativeProgram(p.code, p.bindings.stage).header);
            const bool geometry = p.bindings.stage == S::Stage::Geometry;
            Expect(facts.version == 4 && facts.shader_type == (geometry ? 4u : 1u), name + ": program header");
            Expect(p.control.size() == 224 && S::Get32(&p.control[19 * 4]) == out.registers + 2, name + ": control section");
            Expect(p.bindings.word3 >= 32 && p.bindings.word3 % 16 == 0, name + ": handle table size");
            if (!geometry) Expect(p.bindings.hash == P4MapKit::Sage::VaryingHash({}), name + ": vertex programs hash no varyings");
            if (geometry) Expect(Has(Read(p).opcodes, 0x923u) && Has(Read(p).opcodes, 0x324u), name + ": ISBERD and OUT");
            ++converted;
        } catch (const std::exception& e) {
            // Instanced mesh programs keep loose parameters of two types (floats and integers).
            Expect(name.rfind("instanced_vs", 0) == 0 && std::string(e.what()).find("more than one type") != std::string::npos,
                   name + ": " + e.what());
        }
    }
    Expect(converted == 15, "own_shaders: 15 programs convert (" + std::to_string(converted) + ")");

    // A pixel program: texture slot 0, sampler slot 0, storage buffer 0 (SkyIrradiance, View resource 51).
    OwnPayload::Description d;
    d.stage = OwnPayload::Stage::Pixel;
    d.inputs = {{"in_texcoord0", 2}};
    d.outputs = {{"out_Target0", 0}};
    d.layout_hashes = {0x166705f7u, 0};
    d.textures = {{1, 0, 0}};
    d.samplers = {{1, 1, 0}};
    d.srvs = {{0, 51, 0}};
    d.texture_slots = 1;
    d.sampler_slots = 1;
    d.uniform_buffers = 2;
    d.storage_buffers = 1;
    d.inout_mask = 1;
    const auto program = OwnPayload::Compile(kPixel, d);
    const auto out = S::ConvertSwitchPayloadToSage(program.payload);
    const auto p = S::ReadPayload(out.payload);
    const auto rows = [](const std::vector<std::uint32_t>& t) { return S::TableSlots(t); };
    Expect(rows(p.bindings.textures) == std::vector<unsigned>{2}, "pixel: texture slot 0 is Switch 2 slot 2");
    Expect(rows(p.bindings.samplers) == std::vector<unsigned>{3}, "pixel: sampler slot 0 follows the textures");
    Expect(rows(p.bindings.srv) == std::vector<unsigned>{4}, "pixel: storage buffer 0 follows the samplers");
    Expect(p.bindings.layout_hashes[0] == 0x1f8305f6u, "pixel: View's 2.0.0 layout hash");
    Expect(p.bindings.hash == P4MapKit::Sage::VaryingHash({{"in_texcoord0", 2}}), "pixel: the hash of its input varyings");
    const auto code = Read(p);
    Expect(Has(code.constants, std::make_pair(0x10u, 0x40u)), "pixel: the storage buffer address at c[0x10][0x40]");
    Expect(Has(code.constants, std::make_pair(3u, 3824u)), "pixel: View member 3840 read at its 2.0.0 offset 3824");
    Expect(!Has(code.constants, std::make_pair(3u, 3840u)), "pixel: no read at View's 1.1.1 offset");
    for (unsigned op : {0xf89u, 0x822u, 0x381u, 0x230u, 0x945u, 0x361u})   // SHFL FSWZADD LDG HADD2 (BSSY) TEX.B
        if (op != 0x945u) Expect(Has(code.opcodes, op), "pixel: opcode " + std::to_string(op));
    Expect(p.bindings.word3 >= 0x50, "pixel: the handle table covers slot 4");

    if (failures) return 1;
    std::cout << "sage_convert_test: " << converted << " own programs and a pixel program convert\n";
    return 0;
}

// Kadan's Switch 1 programs as Switch 2 programs (docs/SWITCH2_SHADERS.md, section 8): the export converts the shader
// library Kadan writes for Switch 1 (GLSL_SWITCH) to SF_GLSL_SAGE for a game build of the switch2 family.
//
// Kadan's Switch 1 payloads keep the NV assembly they were compiled from in their control section (from 0x878). The
// same front end output therefore compiles again with the Ampere backend, and the bindings carry over:
// - resource tables keep their rows; Switch 1's texture slot t and sampler slot s become Switch 2 slots 2 + t and
//   2 + T + s (T: Switch 1 texture slots), one numbering as the game's 2.0.0 programs have; the handle words the code
//   reads, buf14[328 + 8t] and buf14[1352 + 8s], move to c[0x10][0x10 * slot];
// - uniform buffers keep their banks (3 + slot); a buffer whose layout changed between the game's 1.1.1 and 2.0.0
//   programs (View, ...: core/own_game_layouts_sage.h) gets its new layout hash and its moved members' offsets;
// - the payload's hash field is the SHA-1 of the input varyings (core/sage_varyings.h); vertex programs, which have
//   none, carry SHA-1 of nothing, as the game's do;
// - word 3 is the handle table size (OwnSage::HandleTableBytes), the control section the game's common one for the
//   stage with the fitted fields set and the two hashes 0 (run sage-compiled-09: they are not checked).
// Refused for now: compute programs, loose parameters of more than one type, storage images.
#pragma once
#include "own_sage_payload.h"
#include "own_game_layouts_sage.h"
#include "sage_varyings.h"
#include "KadanMaxwell/ampere_write.h"
#include "KadanMaxwell/nvasm_ir.h"
#include "KadanMaxwell/nvasm_lower.h"
#include "KadanMaxwell/nvasm_program.h"

namespace P4MapKit { namespace CustomMaterial { namespace OwnSage {

// A Switch 1 (GLSL_SWITCH) payload, the parts the conversion reads.
struct SwitchPayload {
    std::uint16_t stage = 0;
    std::uint32_t packed_buffers = 0;
    struct PackedArray { std::uint16_t bytes; std::uint8_t type_name, type_index; };
    std::vector<PackedArray> packed_globals;                       // loose parameters, one array per type
    std::vector<std::pair<std::uint8_t, std::uint32_t>> packed_slots;  // (array type index, uniform buffer slot)
    std::vector<Sage::Varying> inputs, outputs;
    std::uint32_t resource_bits = 0;
    std::vector<std::uint32_t> srv, samplers, uav, layout_hashes, textures;
    std::uint32_t inout_mask = 0;
    std::array<std::uint8_t, 5> counts{};
    std::int32_t default_sampler = -1;
    std::uint32_t scratch = 0;
    Bytes code, control;
};

inline SwitchPayload ReadSwitchPayload(const Bytes& p) {
    SageReader r(p);
    SwitchPayload s;
    if (r.u32() != PayloadMarker) throw std::runtime_error("switch payload: no GLSL marker");
    s.stage = r.u16();
    const auto buffers = r.count(32, "packed buffers");
    for (std::uint32_t b = 0; b < buffers; ++b) {
        const auto n = r.count(64, "packed arrays");
        if (n) ++s.packed_buffers;
        r.bytes(std::size_t(n) * 4);
    }
    const auto arrays = r.count(64, "packed globals");
    for (std::uint32_t k = 0; k < arrays; ++k) {
        SwitchPayload::PackedArray a;
        a.bytes = r.u16();
        a.type_name = r.u8();
        a.type_index = r.u8();
        s.packed_globals.push_back(a);
    }
    for (auto* list : {&s.inputs, &s.outputs}) {
        const auto n = r.count(32, "varyings");
        for (std::uint32_t k = 0; k < n; ++k) {
            const auto size = r.count(128, "varying name bytes");
            Bytes name = r.bytes(size);
            if (!name.empty() && name.back() == 0) name.pop_back();
            list->push_back({std::string(name.begin(), name.end()), int(r.i32())});
        }
    }
    s.resource_bits = r.u32();
    s.srv = r.words("srv rows");
    s.samplers = r.words("sampler rows");
    s.uav = r.words("uav rows");
    s.layout_hashes = r.words("layout hashes");
    s.textures = r.words("texture rows");
    s.inout_mask = r.u16();
    for (auto& c : s.counts) c = r.u8();
    r.u32();   // flatten
    const auto name = r.i32();
    if (name > 0) r.bytes(std::size_t(name));
    else if (name < 0) r.bytes(std::size_t(-name) * 2);
    const auto copies = r.count(64, "copy infos");
    r.bytes(std::size_t(copies) * 9);
    s.code = r.bytes(r.count(1u << 22, "code bytes"));
    s.control = r.bytes(r.count(1u << 20, "control bytes"));
    const auto slots = r.count(64, "packed global slots");
    for (std::uint32_t k = 0; k < slots; ++k) {
        const auto key = r.u8();
        s.packed_slots.push_back({key, r.u32()});
    }
    s.default_sampler = r.i32();
    r.u32();
    s.scratch = r.u32();
    return s;
}

// The assembly a Kadan payload keeps in its control section.
inline std::string EmbeddedAssembly(const Bytes& control) {
    constexpr std::size_t at = 0x878;
    if (control.size() <= at) throw std::runtime_error("switch payload: no assembly in the control section");
    std::size_t end = at;
    while (end < control.size() && control[end]) ++end;
    std::string text(control.begin() + std::ptrdiff_t(at), control.begin() + std::ptrdiff_t(end));
    if (text.compare(0, 4, "!!NV") != 0) throw std::runtime_error("switch payload: no assembly in the control section");
    return text;
}

// The control section of a new program: the game's common one for its stage (vertex, geometry and pixel programs of
// 2.0.0, 224 bytes; most common value of every word over 1,259 vertex, 265 geometry and 2,508 pixel programs), the
// two hashes 0. Geometry programs differ from vertex programs in words 21 (0x20000) and 22 (4), the same in all 265.
inline Bytes SageControlTemplate(Stage stage) {
    std::vector<std::uint32_t> w(56, 0);
    w[0] = ControlMagic; w[1] = 0xc9; w[2] = 0x14; w[3] = 0x000e0170;
    w[6] = 0x1ff2045f; w[7] = 0x00288000; w[12] = 0xe0; w[35] = 1; w[38] = 0xe0; w[41] = 0x01000000;
    w[47] = 0xe0; w[49] = 0xe0; w[50] = 0xe0; w[53] = 0xe0; w[54] = 0xe0;
    if (stage == Stage::Pixel) w[21] = 0x10000, w[24] = 0x10000;
    else if (stage == Stage::Geometry) w[21] = 0x20000, w[22] = 4;
    else if (stage != Stage::Vertex) throw std::runtime_error("sage control: compute programs are not written");
    Bytes out;
    for (auto v : w) Put32(out, v);
    return out;
}

// A resource table with each row's slot replaced.
template <class F> std::vector<std::uint32_t> RenumberSlots(const std::vector<std::uint32_t>& table, F slot) {
    std::vector<std::uint32_t> out = table;
    std::size_t head = out.size();
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (out[i] == 0xffffffffu) { head = i; break; }
        if (out[i]) { head = out[i]; break; }
    }
    for (std::size_t i = head; i < out.size() && out[i] != 0xffffffffu; ++i) {
        const unsigned n = slot(out[i]);
        if (n > 255) throw std::runtime_error("sage conversion: more than 256 resource slots");
        out[i] = (out[i] & ~0xffu) | n;
    }
    return out;
}

// The storage buffers an NV assembly program declares (STORAGE x[] = { program.storage[n] }).
inline std::vector<unsigned> DeclaredStorage(const std::string& assembly) {
    std::vector<unsigned> out;
    const std::string key = "program.storage[";
    for (std::size_t at = assembly.find(key); at != std::string::npos; at = assembly.find(key, at + 1))
        out.push_back(unsigned(std::stoul(assembly.substr(at + key.size(), 4))));
    return out;
}

struct SageConversion {
    Bytes payload;
    std::string listing;
    std::uint32_t registers = 0;
};

inline SageConversion ConvertSwitchPayloadToSage(const Bytes& switchPayload) {
    const SwitchPayload in = ReadSwitchPayload(switchPayload);
    const Stage stage = Stage(in.stage);
    if (stage != Stage::Vertex && stage != Stage::Pixel && stage != Stage::Geometry)
        throw std::runtime_error("sage conversion: only vertex, geometry and pixel programs are converted");
    if (in.packed_buffers)
        throw std::runtime_error("sage conversion: packed uniform buffers are not converted yet");
    // Loose parameters: Switch 1 packs them into one array per type, bound as a uniform buffer; Switch 2 keeps them in
    // one buffer (payload globals_buffer, globals_bytes). One array, of floats ('h'), carries over as that buffer.
    std::int32_t globalsBuffer = -1;
    std::uint32_t globalsBytes = 0;
    if (!in.packed_globals.empty() || !in.packed_slots.empty()) {
        if (in.packed_globals.size() != 1 || in.packed_slots.size() != 1 ||
            in.packed_slots[0].first != in.packed_globals[0].type_index || in.packed_globals[0].type_name != 'h' ||
            in.packed_slots[0].second > 13)
            throw std::runtime_error("sage conversion: loose parameters of more than one type are not converted yet");
        globalsBuffer = std::int32_t(in.packed_slots[0].second);
        globalsBytes = in.packed_globals[0].bytes;
    }
    if (!TableSlots(in.uav).empty()) throw std::runtime_error("sage conversion: storage images are not converted");
    const std::string assembly = EmbeddedAssembly(in.control);

    // Slots. Switch 1 numbers textures with buffer views (handle words buf14[328 + 8t]), samplers (buf14[1352 + 8s])
    // and storage buffers (addresses in c[0]) apart; Switch 2 has one numbering: textures from 2, then samplers, then
    // storage buffers. A storage buffer's row is in the srv table with its storage index as its slot; a buffer view's
    // row too, with its texture slot: a row names storage when the program declares that storage index and the slot
    // is a texture's or its handle word is never read.
    const auto storage = DeclaredStorage(assembly);
    const auto textureSlots = TableSlots(in.textures);
    const auto has = [](const std::vector<unsigned>& list, unsigned v) { return std::find(list.begin(), list.end(), v) != list.end(); };
    const auto handleRead = [&](unsigned t) { return assembly.find("buf14[" + std::to_string(328 + 8 * t) + "]") != std::string::npos; };
    const auto isStorage = [&](unsigned slot) { return has(storage, slot) && (has(textureSlots, slot) || !handleRead(slot)); };
    std::vector<unsigned> views;
    for (unsigned s : TableSlots(in.srv)) {
        if (!isStorage(s)) views.push_back(s);
        else if (handleRead(s) && !has(textureSlots, s))
            throw std::runtime_error("sage conversion: a resource slot is both a storage buffer and a buffer view");
    }
    unsigned textures = 0, samplerSlots = 0;
    for (unsigned s : textureSlots) textures = std::max(textures, s + 1);
    for (unsigned s : views) textures = std::max(textures, s + 1);
    const auto samplers = TableSlots(in.samplers);
    for (unsigned s : samplers) samplerSlots = std::max(samplerSlots, s + 1);
    const unsigned storageBase = 2 + textures + samplerSlots;
    nvasm::AmpereOptions options;
    for (unsigned s : textureSlots) options.handles[328 + 8 * s] = 0x10 * (2 + s);
    for (unsigned s : views) options.handles[328 + 8 * s] = 0x10 * (2 + s);
    for (unsigned s : samplers) options.handles[1352 + 8 * s] = 0x10 * (2 + textures + s);
    // Switch 1's default sampler (no table row) goes with texel fetches (TXF: buffer views, Load), which use no
    // sampler; Switch 2's fetch from the texture handle alone (TLD, as the game's programs do), so it reads as 0.
    if (in.default_sampler >= 0 && !has(samplers, unsigned(in.default_sampler)))
        options.handles[1352 + 8 * unsigned(in.default_sampler)] = nvasm::AmpereOptions::ZeroHandle;
    // Storage buffer n's address: c[0][table + 16n] (nvasm_maxwell.cpp's storage_table) to its slot's handle words.
    const std::uint32_t table = stage == Stage::Pixel ? 0x510 : stage == Stage::Geometry ? 0x910 : 0x110;
    for (unsigned n : storage) {
        options.storage[table + 16 * n] = 0x10 * (storageBase + n);
        options.storage[table + 16 * n + 4] = 0x10 * (storageBase + n) + 4;
    }

    // Layouts that moved.
    Payload out;
    auto& b = out.bindings;
    b.layout_hashes = in.layout_hashes;
    for (std::size_t slot = 0; slot < b.layout_hashes.size(); ++slot)
        for (const auto& moved : SageMovedLayouts())
            if (b.layout_hashes[slot] == moved.old_hash) {
                b.layout_hashes[slot] = moved.new_hash;
                auto& list = options.moved[std::uint32_t(3 + slot)];
                for (const auto& m : moved.members) list.push_back({m.old_start, m.old_end, m.new_start});
            }

    // Compile.
    nvasm::detail::Program parsed = nvasm::detail::parse(assembly);
    nvasm::Options o;
    o.glsl = false; o.spirv = false; o.maxwell = true; o.demote = false;
    nvasm::Result lowered;
    nvasm::ir::Function function = nvasm::lower::lower(parsed, o, lowered);
    nvasm::AmpereResult compiled;
    nvasm::ir::write_ampere(function, compiled, options);

    b.stage = stage;
    b.resource_bits = in.resource_bits;
    b.srv = RenumberSlots(in.srv, [&](std::uint32_t row) {
        const unsigned s = row & 0xff;
        return isStorage(s) ? storageBase + s : 2 + s;
    });
    b.textures = RenumberSlots(in.textures, [](std::uint32_t row) { return 2 + (row & 0xff); });
    b.samplers = RenumberSlots(in.samplers, [&](std::uint32_t row) { return 2 + textures + (row & 0xff); });
    b.uav = in.uav;
    b.attribute_mask = stage == Stage::Vertex ? in.inout_mask : 0;
    b.globals_buffer = globalsBuffer;
    b.globals_bytes = globalsBytes;
    b.counts = {{std::uint8_t(textures), std::uint8_t(samplerSlots), in.counts[2], in.counts[3]}};
    // Vertex programs have no input varyings (attributes are not varyings): SHA-1 of nothing, as the game's.
    b.hash = Sage::VaryingHash(stage == Stage::Vertex ? std::vector<Sage::Varying>{} : in.inputs);
    b.name = "Kadan";
    out.code = compiled.code;
    b.word3 = HandleTableBytes(b, out.code, true);
    out.control = SageControl(SageControlTemplate(stage), std::uint32_t(out.code.size()), compiled.table_offset,
                              compiled.registers, 0);
    out.tail.words = {{0, 1, 0, 0, 0, 0, 0xffffffffu, 0, 0, 0, 4}};
    SageConversion result;
    result.payload = WritePayload(out);
    (void)ReadPayload(result.payload);
    result.listing = compiled.listing;
    result.registers = compiled.registers;
    return result;
}

}}}  // namespace P4MapKit::CustomMaterial::OwnSage

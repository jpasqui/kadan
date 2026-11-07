// NVN payloads of Kadan's own vertex and geometry programs (stage 8 of tools/navigation/OWN_SHADER_MAPS.md), in the
// layout recomp/graphics/nvasm/own_shaders/own_payload.py writes: the binding header from a description, the code
// from our Maxwell compiler, a control section written from the program alone, the packed global slots and the
// trailing words. Nothing of a game program is copied. Pixel programs keep their writer (PixelPayload, CleanControl in
// custom_material_shader_set.h); Compile here admits them too, with that control section.
#pragma once
#include "custom_material_bindings.h"
#include "custom_material_shader_set.h"
#include "KadanMaxwell/nvasm.h"
#include "KadanMaxwell/nvasm_ir.h"
#include "KadanMaxwell/nvasm_lower.h"
#include "KadanMaxwell/nvasm_program.h"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace P4MapKit { namespace CustomMaterial { namespace OwnPayload {

// The header's stage tag ('SV', 'SG', 'SP') and the shader type in the code's program header.
enum class Stage : std::uint16_t { Vertex = 0x5653, Geometry = 0x4753, Pixel = 0x5053 };

inline unsigned ProgramHeaderType(Stage stage) {
    switch (stage) {
    case Stage::Vertex: return 1;
    case Stage::Geometry: return 4;
    case Stage::Pixel: return 5;
    }
    throw std::runtime_error("own payload: unknown stage");
}
inline const char* AssemblyPrefix(Stage stage) {
    switch (stage) {
    case Stage::Vertex: return "!!NVvp5.0";
    case Stage::Geometry: return "!!NVgp5.0";
    case Stage::Pixel: return "!!NVfp5.0";
    }
    throw std::runtime_error("own payload: unknown stage");
}

// A resource the program reads: resource index `resource` of the struct in uniform buffer `buffer`, in `slot`.
struct Resource { unsigned buffer = 0, resource = 0, slot = 0; };

// What a payload binds. The resource tables cover the buffers up to the last layout hash (and are empty when the
// program reads no resource); the counts are the texture slots (textures and buffer views), the sampler slots (with
// the default sampler), the uniform buffers and the storage buffers the program binds.
struct Description {
    Stage stage = Stage::Vertex;
    std::vector<Varying> inputs, outputs;           // a location here is the header's: the outputs are numbered 0..n-1
    std::vector<std::uint32_t> layout_hashes;       // per uniform buffer slot; 0 for a struct without one
    std::vector<Resource> textures, samplers, srvs, uavs;
    unsigned sampler_slots = 0, uniform_buffers = 0, storage_buffers = 0;
    int texture_slots = -1;                         // -1: the table rows (textures and buffer views)
    std::vector<PackedArray> global_arrays;         // packed global arrays (loose parameter buffers)
    std::vector<std::pair<std::uint8_t, std::int32_t>> packed_global_slots;  // (packed array index, buffer slot)
    std::int32_t default_sampler = -1;              // -1: none
    unsigned inout_mask = 1;                        // 0 in geometry programs
};

// The description as the shared binding record (ShaderHeader, ReadResourceTable and the pixel tools read it).
inline ShaderBindings Bindings(const Description& d) {
    ShaderBindings h;
    for (const auto* list : {&d.textures, &d.samplers, &d.srvs, &d.uavs})
        for (const auto& r : *list) {
            if (r.buffer > 31) throw std::runtime_error("own payload: resource buffer outside 0..31");
            h.resource_bits |= 1u << r.buffer;
        }
    const unsigned buffers = h.resource_bits ? unsigned(d.layout_hashes.size()) : 0;
    const auto table = [&](const std::vector<Resource>& list) {
        ResourceMap rows;
        for (const auto& r : list) rows[r.buffer].push_back({r.resource, r.slot});
        return BuildResourceTable(rows, buffers);
    };
    h.srv = table(d.srvs);
    h.samplers = table(d.samplers);
    h.uav = table(d.uavs);
    h.layout_hashes = d.layout_hashes;
    h.textures = table(d.textures);
    h.global_arrays = d.global_arrays;
    h.inputs = d.inputs;
    h.outputs = d.outputs;
    h.inout_mask = d.inout_mask;
    const unsigned textures = d.texture_slots >= 0 ? unsigned(d.texture_slots) : unsigned(d.textures.size() + d.srvs.size());
    if (textures > 255 || d.sampler_slots > 255 || d.uniform_buffers > 255 || d.storage_buffers > 255)
        throw std::runtime_error("own payload: binding count overflow");
    h.counts = {{std::uint8_t(textures), std::uint8_t(d.sampler_slots), std::uint8_t(d.uniform_buffers),
                 std::uint8_t(d.storage_buffers), 0}};
    h.packed_global_slots = d.packed_global_slots;
    h.default_sampler = d.default_sampler;
    return h;
}

// The binding header: the shared writer's layout with this stage's tag.
inline Bytes Header(const Description& d) {
    auto out = ShaderHeader(Bindings(d));
    const auto tag = unsigned(d.stage);
    out[4] = std::uint8_t(tag);
    out[5] = std::uint8_t(tag >> 8);
    return out;
}

constexpr std::size_t kControlHeader = 0x878;

// The words every one of the game's vertex programs has (1,470 sampled); geometry programs (18 sampled) add two.
inline const std::map<unsigned, std::uint32_t>& ControlFixed(Stage stage) {
    static const std::map<unsigned, std::uint32_t> vertex = {
        {0x000, 0x98761234}, {0x004, 1}, {0x008, 0x10}, {0x00c, 0x120}, {0x010, 0xb}, {0x014, kControlHeader},
        {0x02c, 0x20}, {0x034, 0x340}, {0x038, 0xffffff01}, {0x03c, 0xffffffff}, {0x040, 0xffffffff},
        {0x044, 0xffffffff}, {0x048, 0xffffffff}, {0x04c, 0xffffffff}, {0x050, 0xffffffff}, {0x054, 0xffffffff},
        {0x058, 0xff}, {0x708, 0x30}, {0x790, 0x20001f}, {0x7f4, 0xc}};
    static const std::map<unsigned, std::uint32_t> geometry = [] {
        auto m = vertex;
        m[0x714] = 2;
        m[0x828] = 4;
        return m;
    }();
    if (stage == Stage::Vertex) return vertex;
    if (stage == Stage::Geometry) return geometry;
    throw std::runtime_error("own payload: pixel control sections are CleanControl's");
}
// The words the compiler computes for the program (nvasm::maxwell_control's).
constexpr unsigned kControlProgram[] = {0x6f8, 0x6fc, 0x700, 0x704, 0x70c, 0x710};

// The computed words as a control-sized record, from a compiled program.
inline Bytes ComputedControl(const nvasm::Result& compiled) {
    Bytes c(kControlHeader, 0);
    const auto size = std::uint32_t(compiled.maxwell.size());
    Set32(c, 0x6f8, compiled.maxwell_program_size);
    Set32(c, 0x6fc, 0);                 // no immediate pool: constants are in the instructions
    Set32(c, 0x700, size);
    Set32(c, 0x704, size);
    Set32(c, 0x70c, compiled.maxwell_registers);
    Set32(c, 0x710, compiled.maxwell_scratch);
    if (compiled.stage == nvasm::Stage::Fragment) Set32(c, 0x714, 1);   // what CleanControl checks
    return c;
}

inline void CheckProgram(Stage stage, const std::string& assembly, const Bytes& code) {
    if (code.empty() || code.size() > (1u << 20) || code.size() % 256 || Read32(code, 0) != 0x12345678 ||
        ((Read32(code, 0x30) >> 10) & 15) != ProgramHeaderType(stage))
        throw std::runtime_error("own payload: compiled code shape refused for this stage");
    const std::string prefix = AssemblyPrefix(stage);
    if (assembly.size() > (1u << 18) || assembly.compare(0, prefix.size(), prefix) != 0 || assembly.size() < 4 ||
        assembly.compare(assembly.size() - 4, 4, "END\n") != 0)
        throw std::runtime_error(std::string("own payload: not a ") + prefix + " program");
    for (unsigned char c : assembly)
        if ((c < 32 && c != '\n' && c != '\t' && c != '\r') || c > 126)
            throw std::runtime_error("own payload: assembly must be plain text");
}

// The control section from the fixed table, the computed words, the program's own assembly text and a hash of its
// own code: nothing of the stock section it was compiled from.
inline Bytes Control(Stage stage, const Bytes& computed, const std::string& assembly, const Bytes& code) {
    if (stage == Stage::Pixel) return CleanControl(computed, assembly, code);
    CheckProgram(stage, assembly, code);
    if (computed.size() < kControlHeader || Read32(computed, 0x704) != code.size())
        throw std::runtime_error("own payload: compiler control/code mismatch");
    const auto total = (kControlHeader + assembly.size() + 1 + 7) & ~std::size_t(7);
    Bytes out(total, 0);
    for (const auto& p : ControlFixed(stage)) Set32(out, p.first, p.second);
    for (auto at : kControlProgram) Set32(out, at, Read32(computed, at));
    Set32(out, 0x18, std::uint32_t(assembly.size()));
    for (auto at : {0x20, 0x6f0, 0x7c8, 0x7e8}) Set32(out, at, std::uint32_t(total));
    auto own = ByteText("p4-custom-program");
    Append(own, code);
    const auto digest = Sha256(own);
    std::copy(digest.begin(), digest.begin() + 24, out.begin() + 0x7d0);
    std::copy(assembly.begin(), assembly.end(), out.begin() + kControlHeader);
    return out;
}

// The payload: header, code, control section, packed global slots, default sampler, the scratch words, 0, 4.
inline Bytes Payload(const Description& d, const Bytes& code, const Bytes& computed, const std::string& assembly) {
    if (d.stage == Stage::Pixel) return PixelPayload(Bindings(d), code, computed, assembly);
    auto out = Header(d);
    const auto control = Control(d.stage, computed, assembly, code);
    U32(out, std::uint32_t(code.size()));
    Append(out, code);
    U32(out, std::uint32_t(control.size()));
    Append(out, control);
    if (d.packed_global_slots.size() > 64) throw std::runtime_error("own payload: packed slot limit");
    std::set<unsigned> taken;
    U32(out, std::uint32_t(d.packed_global_slots.size()));
    for (const auto& p : d.packed_global_slots) {
        if (!taken.insert(p.first).second || p.second < 0 || p.second > 31)
            throw std::runtime_error("own payload: invalid packed global slot");
        out.push_back(p.first);
        U32(out, std::uint32_t(p.second));
    }
    U32(out, std::uint32_t(d.default_sampler));
    const auto scratch = Read32(control, 0x710);
    if (scratch > 0xffffff) throw std::runtime_error("own payload: scratch size overflow");
    U32(out, scratch << 8);
    U32(out, scratch);
    U32(out, 0);
    U32(out, 4);
    if (out.size() > (1u << 20)) throw std::runtime_error("own payload: payload limit");
    return out;
}

struct Program {
    Bytes code, control, payload;
    std::uint32_t program_size = 0, registers = 0, scratch = 0;
};

// Compiles NV assembly of any stage with our Maxwell compiler and writes its payload.
inline Program Compile(const std::string& assembly, const Description& d) {
    if (assembly.empty() || assembly.size() > (1u << 18)) throw std::runtime_error("own payload: assembly size limit");
    (void)Header(d);                    // the description is checked before compiling
    nvasm::Options options;
    options.glsl = false;
    options.spirv = false;
    options.maxwell = true;
    options.demote = false;
    // nvasm::translate's steps for Maxwell alone (the plugin compiles no GLSL or SPIR-V writer).
    nvasm::detail::Program parsed = nvasm::detail::parse(assembly);
    static const std::set<std::string> known = {"NV_internal", "NV_bindless_texture", "NV_gpu_program_fp64",
        "NV_shader_storage_buffer", "ARB_draw_buffers", "ARB_fragment_program_shadow", "NV_early_fragment_tests",
        "NV_texture_multisample", "ARB_shader_image_size", "NV_shader_atomic_float"};
    for (const auto& o : parsed.options) if (!known.count(o)) throw std::runtime_error("own payload: unsupported OPTION " + o);
    for (const auto& b : parsed.cbuffers)
        if (b.second < 0 || b.second > 14) throw std::runtime_error("own payload: program buffer index must be 0..14");
    nvasm::Result compiled;
    nvasm::ir::Function function = nvasm::lower::lower(parsed, options, compiled);
    nvasm::ir::write_maxwell(function, compiled);
    const auto expected = d.stage == Stage::Vertex ? nvasm::Stage::Vertex
                        : d.stage == Stage::Geometry ? nvasm::Stage::Geometry : nvasm::Stage::Fragment;
    if (compiled.stage != expected) throw std::runtime_error("own payload: assembly stage differs from the description");
    for (const auto& r : compiled.resources)
        if (r.kind == nvasm::ResourceKind::StorageImage || (r.kind == nvasm::ResourceKind::StorageBuffer && r.written))
            throw std::runtime_error("own payload: programs of Kadan's own write no storage");
    if (compiled.maxwell.size() > (1u << 20) || compiled.maxwell_program_size > compiled.maxwell.size() ||
        compiled.maxwell_registers > 255)
        throw std::runtime_error("own payload: compiled program exceeds payload limits");
    const auto computed = ComputedControl(compiled);
    Program out;
    out.code = std::move(compiled.maxwell);
    out.control = Control(d.stage, computed, assembly, out.code);
    out.payload = Payload(d, out.code, computed, assembly);
    out.program_size = compiled.maxwell_program_size;
    out.registers = compiled.maxwell_registers;
    out.scratch = compiled.maxwell_scratch;
    return out;
}

// Kadan's own programs (modding/mapkit/own_shaders): each one's description from named facts, as own_payload.py
// gives them. Layout hashes are the game's for each struct; 0 for a struct without one.
namespace Facts {
constexpr std::uint32_t kView = 375850487, kBasePass = 222823682, kLocalVF = 105252583;
constexpr std::uint32_t kTranslucentPass = 99356142, kShadowPass = 184025585, kInstancedVF = 104204025;
inline std::vector<Varying> Numbered(const std::vector<std::string>& names) {
    std::vector<Varying> out;
    for (std::size_t i = 0; i < names.size(); i++) out.push_back({names[i], std::int32_t(i)});
    return out;
}
inline const std::vector<std::string>& SharedOutputs() {
    static const std::vector<std::string> v = {"out_texcoord10_centroid", "out_texcoord11_centroid", "out_texcoord0"};
    return v;
}
inline std::vector<std::string> With(std::vector<std::string> a, const std::vector<std::string>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}
// The opaque base pass: View 0, Primitive 1, base pass 2, local vertex factory 3 (precomputed lighting 4).
inline const std::vector<Resource>& BasePassSamplers() {
    static const std::vector<Resource> v = {{0, 57, 1}, {0, 59, 2}, {2, 27, 0}};
    return v;
}

inline Description StaticMesh(bool lightmap) {
    Description d;
    d.outputs = Numbered(With(SharedOutputs(), lightmap ? std::vector<std::string>{"out_texcoord4", "out_texcoord7"}
                                                        : std::vector<std::string>{"out_texcoord7"}));
    d.layout_hashes = {kView, 0, kBasePass, kLocalVF};
    d.textures = {{0, 56, 4}, {0, 58, 3}, {2, 26, 2}};   // sky light LUT, aerial perspective volume, fog cube map
    d.samplers = BasePassSamplers();
    d.srvs = {{3, 0, 1}, {3, 3, 0}};                     // texture coordinate and packed tangent buffers
    d.default_sampler = 3;
    d.sampler_slots = 4;
    d.uniform_buffers = lightmap ? 5 : 4;
    return d;
}
// Translucent without vertex fog: View 0, Primitive 1, the vertex factory 2 (the lightmap's buffer 3); sampler 0.
inline Description Translucent(bool lightmap) {
    Description d;
    d.outputs = Numbered(lightmap ? With(SharedOutputs(), {"out_texcoord4"}) : SharedOutputs());
    d.layout_hashes = {kView, 0, kLocalVF};
    d.srvs = {{2, 0, 1}, {2, 3, 0}};
    d.default_sampler = 0;
    d.sampler_slots = 1;
    d.uniform_buffers = lightmap ? 4 : 3;
    return d;
}
// Translucent with vertex fog: the translucent pass buffer 0, View 1, Primitive 2, the vertex factory 3.
inline Description TranslucentFog() {
    Description d;
    d.outputs = Numbered(With(SharedOutputs(), {"out_texcoord7"}));
    d.layout_hashes = {kTranslucentPass, kView, 0, kLocalVF};
    d.textures = {{0, 26, 4}, {1, 56, 3}, {1, 58, 2}};
    d.samplers = {{0, 27, 0}, {1, 57, 1}, {1, 59, 2}};
    d.srvs = {{3, 0, 1}, {3, 3, 0}};
    d.default_sampler = 3;
    d.sampler_slots = 4;
    d.uniform_buffers = 4;
    return d;
}
// Depth and shadow depth: the vertex fetch buffers with sampler 0; the loose bias modes add a packed global array
// of 16 bytes (type h) in uniform buffer slot 4.
inline Description Depth(std::vector<std::uint32_t> hashes, const std::vector<std::string>& outputs, unsigned buffers,
                         bool loose) {
    Description d;
    d.outputs = Numbered(With(SharedOutputs(), outputs));
    const unsigned vf = unsigned(hashes.size() - 1);
    d.layout_hashes = std::move(hashes);
    d.srvs = {{vf, 0, 1}, {vf, 3, 0}};
    d.default_sampler = 0;
    d.sampler_slots = 1;
    d.uniform_buffers = buffers;
    if (loose) {
        d.global_arrays = {{16, std::uint8_t('h'), 0}};
        d.packed_global_slots = {{0, 4}};
    }
    return d;
}
// The one-pass point light geometry program: only the shadow depth pass buffer, no resources, no default sampler.
inline Description OnePassPointShadowGeometry() {
    Description d;
    d.stage = Stage::Geometry;
    d.inputs = {{"in_texcoord10_centroid", 0}, {"in_texcoord11_centroid", 1}, {"in_texcoord0", 2}, {"in_texcoord6", 4}};
    d.outputs = Numbered(With(SharedOutputs(), {"out_texcoord7", "out_hlslcc_layer_index"}));
    d.layout_hashes = {kShadowPass};
    d.uniform_buffers = 1;
    d.inout_mask = 0;
    return d;
}
// The instanced static mesh: the opaque buffers, two loose parameter buffers (instancing parameters, 112 bytes, type h;
// instance offset, 16 bytes, type u), then the instanced vertex factory, whose instance buffers take the texture
// slots before the fog textures.
inline Description Instanced(bool lightmap) {
    Description d;
    d.layout_hashes = {kView, 0, kBasePass, kLocalVF};
    if (lightmap) d.layout_hashes.push_back(0);
    const unsigned vf = unsigned(d.layout_hashes.size()) + 2;
    d.layout_hashes.insert(d.layout_hashes.end(), {0, 0, kInstancedVF});
    const unsigned count = lightmap ? 3 : 2;            // origins, transform rows, lightmap bias
    const unsigned fog = 3 + count;
    d.textures = {{0, 56, fog + 1}, {0, 58, fog}, {2, 26, 2}};
    d.samplers = BasePassSamplers();
    d.srvs = {{3, 0, 1}, {3, 3, 0}};
    for (unsigned r = 0; r < count; r++) d.srvs.push_back({vf, r, 3 + count - 1 - r});
    d.outputs = Numbered(With({"out_texcoord10_centroid", "out_texcoord11_centroid", "out_color1", "out_texcoord0"},
                              lightmap ? std::vector<std::string>{"out_texcoord4", "out_texcoord7"}
                                       : std::vector<std::string>{"out_texcoord7"}));
    d.global_arrays = {{112, std::uint8_t('h'), 0}, {16, std::uint8_t('u'), 4}};
    d.packed_global_slots = {{0, std::int32_t(vf - 1)}, {4, std::int32_t(vf - 2)}};
    d.default_sampler = 3;
    d.sampler_slots = 4;
    d.uniform_buffers = vf;                              // the vertex factory's buffer gives only resources
    return d;
}
}  // namespace Facts

inline const std::vector<std::string>& KadanProgramNames() {
    static const std::vector<std::string> names = {
        "static_mesh_vs_1uv", "static_mesh_vs_lightmap_1uv", "static_mesh_vs_lightmap_df_1uv",
        "instanced_vs_1uv", "instanced_vs_lightmap_1uv", "instanced_vs_lightmap_df_1uv",
        "translucent_vs_1uv", "translucent_vs_lightmap_1uv", "translucent_vs_lightmap_df_1uv",
        "translucent_fog_vs_1uv", "depth_only_vs", "shadow_vs", "shadow_loose_vs", "shadow_perspective_vs",
        "shadow_perspective_loose_vs", "shadow_for_gs_vs", "shadow_for_gs_onepass_vs", "onepass_point_shadow_gs"};
    return names;
}

inline Description KadanProgram(const std::string& name) {
    using namespace Facts;
    if (name == "static_mesh_vs_1uv") return StaticMesh(false);
    if (name == "static_mesh_vs_lightmap_1uv" || name == "static_mesh_vs_lightmap_df_1uv") return StaticMesh(true);
    if (name == "instanced_vs_1uv") return Instanced(false);
    if (name == "instanced_vs_lightmap_1uv" || name == "instanced_vs_lightmap_df_1uv") return Instanced(true);
    if (name == "translucent_vs_1uv") return Translucent(false);
    if (name == "translucent_vs_lightmap_1uv" || name == "translucent_vs_lightmap_df_1uv") return Translucent(true);
    if (name == "translucent_fog_vs_1uv") return TranslucentFog();
    const std::vector<std::uint32_t> shadow = {kView, 0, kShadowPass, kLocalVF}, plain = {kView, 0, kLocalVF};
    if (name == "depth_only_vs") return Depth(plain, {"out_texcoord6"}, 3, false);
    if (name == "shadow_vs") return Depth(shadow, {"out_texcoord6", "out_texcoord7"}, 4, false);
    if (name == "shadow_loose_vs") return Depth(shadow, {"out_texcoord6", "out_texcoord7"}, 5, true);
    if (name == "shadow_perspective_vs") return Depth(shadow, {"out_texcoord6", "out_texcoord8", "out_texcoord7"}, 4, false);
    if (name == "shadow_perspective_loose_vs")
        return Depth(shadow, {"out_texcoord6", "out_texcoord8", "out_texcoord7"}, 5, true);
    if (name == "shadow_for_gs_vs" || name == "shadow_for_gs_onepass_vs")
        return Depth(plain, {"out_texcoord7", "out_texcoord6", "out_texcoord8"}, 3, false);
    if (name == "onepass_point_shadow_gs") return OnePassPointShadowGeometry();
    throw std::runtime_error("own payload: no program of Kadan's named " + name);
}

} } }

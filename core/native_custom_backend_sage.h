// Kadan's own pixel programs for the Switch 2 (SF_GLSL_SAGE; docs/SWITCH2_SHADERS.md, section 7): the same NV
// assembly Kadan writes for Switch 1, compiled by the Ampere backend and wrapped in the donor game program's payload.
//
// The donor is the 2.0.0 game program the creator's material replaces (as on Switch 1, a pixel program of the game
// material's own map). Its bindings stay: the resource tables, layout hashes and slot counts. Kadan resolves texture
// parameters through those tables (ResolveMaterialBindings reads Sage tables, which have Switch 1's row format), so
// the assembly reads texture slot s at buf14[328 + 8s] and sampler slot s at buf14[1352 + 8s]; the backend moves
// both to Switch 2's handle bank, c[0x10][0x10 * s]. The program header, code and control section are new: the
// control section is the donor's with the fitted fields replaced (OwnSage::SageControl).
#pragma once
#include "own_sage_payload.h"
#include "KadanMaxwell/ampere_write.h"
#include "KadanMaxwell/nvasm_ir.h"
#include "KadanMaxwell/nvasm_lower.h"
#include "KadanMaxwell/nvasm_program.h"
#include <set>

namespace P4MapKit { namespace CustomMaterial { namespace NativeBackend {

struct SagePixelProgram {
    OwnSage::Bytes payload;
    std::uint32_t registers = 0, instructions = 0;
    std::string listing;
};

// The handle words a resource table's rows name, Switch 1 offset -> Switch 2 offset.
inline void AddSageHandles(const std::vector<std::uint32_t>& table, std::uint32_t base, nvasm::AmpereOptions& options) {
    std::size_t head = table.size();
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (table[i] == 0xffffffffu) { head = i; break; }
        if (table[i]) { head = table[i]; break; }
    }
    for (std::size_t i = head; i < table.size() && table[i] != 0xffffffffu; ++i) {
        const std::uint32_t slot = table[i] & 0xff;
        options.handles[base + 8 * slot] = 0x10 * slot;
    }
}

inline SagePixelProgram CompileSagePixelAssembly(const std::string& assembly, const OwnSage::Bytes& donorPayload) {
    if (assembly.empty() || assembly.size() > (1u << 18)) throw std::runtime_error("native-sage: pixel assembly size limit");
    OwnSage::Payload donor = OwnSage::ReadPayload(donorPayload);
    if (donor.bindings.stage != OwnSage::Stage::Pixel) throw std::runtime_error("native-sage: the donor is no pixel program");
    nvasm::detail::Program parsed = nvasm::detail::parse(assembly);
    if (parsed.stage != nvasm::Stage::Fragment) throw std::runtime_error("native-sage: only pixel programs are admitted");
    nvasm::Options options;
    options.glsl = false; options.spirv = false; options.maxwell = true; options.demote = false;
    nvasm::Result lowered;
    nvasm::ir::Function function = nvasm::lower::lower(parsed, options, lowered);
    for (const auto& r : lowered.resources)
        if (r.kind == nvasm::ResourceKind::StorageBuffer || r.kind == nvasm::ResourceKind::StorageImage)
            throw std::runtime_error("native-sage: storage buffers and images are not compiled for the Switch 2 yet");
    nvasm::AmpereOptions ampere;
    AddSageHandles(donor.bindings.textures, 328, ampere);
    AddSageHandles(donor.bindings.srv, 328, ampere);
    AddSageHandles(donor.bindings.samplers, 1352, ampere);
    nvasm::AmpereResult compiled;
    nvasm::ir::write_ampere(function, compiled, ampere);
    // The donor's outputs: the compiled program may write only render targets the donor writes.
    const auto donorCode = OwnSage::ReadNativeProgram(donor.code, OwnSage::Stage::Pixel);
    const auto mine = OwnSage::ReadNativeProgram(compiled.code, OwnSage::Stage::Pixel);
    const auto have = OwnSage::ReadHeaderFacts(donorCode.header), want = OwnSage::ReadHeaderFacts(mine.header);
    if ((want.omap_target & ~have.omap_target) || (want.writes_depth && !have.writes_depth))
        throw std::runtime_error("native-sage: the program writes outputs its donor does not");
    donor.control = OwnSage::SageControl(donor.control, std::uint32_t(compiled.code.size()), compiled.table_offset,
                                         compiled.registers, 0);
    donor.code = compiled.code;
    donor.bindings.word3 = OwnSage::HandleTableBytes(donor.bindings, donor.code, true);
    SagePixelProgram out;
    out.payload = OwnSage::WritePayload(donor);
    (void)OwnSage::ReadPayload(out.payload);
    out.registers = compiled.registers;
    out.instructions = compiled.instructions;
    out.listing = compiled.listing;
    return out;
}

}}}  // namespace P4MapKit::CustomMaterial::NativeBackend

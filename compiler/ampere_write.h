#pragma once
// Ampere backend for the Switch 2 (compiler/ampere_write.cpp, docs/SWITCH2_SHADERS.md).
#include "nvasm.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace nvasm {
struct AmpereOptions {
    // Texture handle words: program.buffer[14] byte offset (as Switch 1 programs read them, c[2][32 + offset]) ->
    // byte offset in Switch 2's handle bank c[0x10] (0x10 * the resource table slot). ZeroHandle reads as 0: a
    // default sampler that Switch 1 ORs into a buffer view's handle, where Switch 2's fetches use the texture alone.
    static constexpr uint32_t ZeroHandle = 0xffffffffu;
    std::map<uint32_t, uint32_t> handles;
    // Uniform buffer members that moved between the layouts the program was written for and the target's: per
    // constant bank, (old start, old end, new start); a new start of -1 refuses a read (the member is gone). Reads at
    // a dynamic index into a bank listed here are refused.
    struct Moved { uint32_t old_start, old_end; int32_t new_start; };
    std::map<uint32_t, std::vector<Moved>> moved;
    // Storage buffer addresses: Switch 1 keeps buffer n's 64-bit address at c[0][table + 16n] (the selector's
    // storage_table); Switch 2 at c[0x10][0x10 * slot]. Byte offset in bank 0 -> byte offset in c[0x10]. Any other read
    // of bank 0 (the driver's) is refused.
    std::map<uint32_t, uint32_t> storage;
};
struct AmpereResult {
    std::vector<uint8_t> code;      // program header, instructions, branch to itself, NOPs, zeroed 128-byte tail
    uint32_t table_offset = 0;      // where the tail starts: the control section's word 14
    uint32_t registers = 0;         // highest register + 1 (the control section's word 19 is this + 2)
    uint32_t instructions = 0;
    std::string listing;
};
namespace ir { struct Function; void write_ampere(const Function& f, AmpereResult& r, const AmpereOptions& o); }
}  // namespace nvasm

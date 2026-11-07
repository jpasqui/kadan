#pragma once
// NV_gpu_program5 assembly -> Vulkan GLSL.
//
// Every shader in the game's GLSL_SWITCH archives carries the NVIDIA assembly
// its Maxwell binary was compiled from. This translates that text into readable
// GLSL 4.60 for Vulkan. It has no dependencies; compiling the GLSL to SPIR-V is
// the caller's job (see nvasm_cli.cpp).
//
// Resources keep their NVN identities in the reflection: uniform buffers are
// program.buffer[N], storage buffers program.storage[N], and textures/images
// the constant-buffer words their bindless handles are loaded from, the same
// (bank, offset) pairs the Maxwell binary reads.
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nvasm {

enum class Stage : uint32_t { Vertex, TessControl, TessEval, Geometry, Fragment, Compute };

struct Options {
    // Last vertex-processing stage only: map GL clip depth [-w, w] to Vulkan's
    // [0, w], and flip clip-space y.
    bool convert_depth = false;
    bool y_negate = false;
    // Vertex attributes read as integers (bit N = vertex.attrib[N]). A bit may
    // be set in at most one mask.
    uint32_t signed_int_inputs = 0, unsigned_int_inputs = 0;
    // Fragment colour outputs written as integers (bit N = result.color[N]).
    uint32_t signed_int_outputs = 0, unsigned_int_outputs = 0;
    // First descriptor binding this stage may use.
    uint32_t first_binding = 0;
    // KIL as demote-to-helper (derivatives in the quad stay defined) instead
    // of terminating the invocation.
    bool demote = true;
    // Replace condition-code registers with the tested values where every
    // read has one reaching writer. Off keeps the literal flag emulation.
    bool fold_conditions = true;
    // Float arithmetic as the Maxwell binaries do it: MAD fused (fma), DPn as
    // a chain of fused multiply-adds, DIV as a multiply by the reciprocal, POW
    // as exp2(log2(a) * b), and every register precise so the driver cannot
    // reassociate or fuse on its own. Off gives plain operators.
    bool hardware_arithmetic = true;
    // Declare registers and outputs `precise` so the driver may not fuse or
    // reorder the remaining arithmetic. It matches the hardware far better (17
    // of 300 fragment programs differ instead of 85), but the Intel Arc driver
    // miscompiles carrot4/265 with it (see README.md).
    bool precise_registers = true;
    // Outputs to produce: readable GLSL and/or SPIR-V written directly (no glslang),
    // and/or Maxwell machine code for the Switch GPU (MAXWELL_BACKEND.md; implies
    // demote off: KIL ends the invocation there).
    bool glsl = true;
    bool spirv = false;
    bool maxwell = false;
};

enum class ResourceKind : uint32_t { UniformBuffer, StorageBuffer, SampledTexture, StorageImage };

struct HandleWord {
    uint32_t buffer, offset; // program.buffer[buffer] at byte offset; the 64-bit words are OR-ed
    bool operator==(const HandleWord& o) const { return buffer == o.buffer && offset == o.offset; } // C++17 for the RHI
    bool operator<(const HandleWord& o) const { return buffer != o.buffer ? buffer < o.buffer : offset < o.offset; }
};

struct Resource {
    ResourceKind kind{};
    uint32_t binding = 0;
    std::string name;       // GLSL identifier
    uint32_t index = 0;     // buffers: program.buffer / program.storage index
    uint32_t size = 0;      // uniform buffers: bytes declared
    bool dynamic = false;   // uniform buffers: indexed at run time
    std::vector<HandleWord> handle; // textures and images
    std::string target;     // assembly texture target: 2D, CUBE, BUFFER, SHADOW2D, ...
    char type = 'F';        // sampled/storage data type: F, S or U
    std::string format;     // storage images: GLSL format qualifier, empty when unformatted
    bool read = false, written = false;
};

struct Varying {
    uint32_t location;
    char type;              // F, S, U
    std::string interpolation; // "", "flat", "noperspective"
};

struct Result {
    Stage stage{};
    std::string glsl;
    std::vector<uint32_t> spirv; // SPIR-V 1.3 for Vulkan 1.1 when Options::spirv
    std::vector<Resource> resources; // in binding order
    std::vector<Varying> inputs, outputs;
    std::array<uint32_t, 3> workgroup{1, 1, 1};
    uint32_t shared_bytes = 0;
    uint32_t next_binding = 0;
    // Every constant-offset uniform load, with the type it reads the word as:
    // test harnesses fill integer words with small integers so that loops
    // bounded by uniforms stay short.
    struct ConstantWord {
        uint32_t buffer, offset;
        char type; // F, S, U
    };
    std::vector<ConstantWord> constant_words;
    // Options::maxwell: the NVN code allocation (a multiple of 256 bytes; graphics
    // programs start with the header and the shader program header), the size of
    // the program within it, and the registers it uses.
    std::vector<uint8_t> maxwell;
    uint32_t maxwell_program_size = 0, maxwell_registers = 0;
    uint32_t maxwell_scratch = 0;   // per-warp scratch: local memory for 32 lanes plus the reconvergence stack
    std::string maxwell_listing;
};

struct Error : std::runtime_error {
    int line;
    Error(int line_, const std::string& message)
        : std::runtime_error("line " + std::to_string(line_) + ": " + message), line(line_) {}
};

// Throws Error for text outside the supported subset; never guesses.
Result translate(std::string_view source, const Options& options = {});

const char* stage_name(Stage stage);

// An NVN control section for Result::maxwell, made from the stock program's own
// (`stock`, from the same archive entry): sizes, register count and immediate pool
// are replaced; everything else, including the assembly text, is kept.
std::vector<uint8_t> maxwell_control(const std::vector<uint8_t>& stock, const Result& compiled);

// Milliseconds this thread has spent in each phase of translate(), summed over
// calls: parse, lowering to IR, GLSL printing, SPIR-V writing. For benchmarking.
struct PhaseTimes { double parse = 0, analyze = 0, emit = 0, assemble = 0; };
PhaseTimes& phase_times();

} // namespace nvasm

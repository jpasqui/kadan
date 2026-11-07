#pragma once
// Internal to the Maxwell backend (MAXWELL_BACKEND.md): the machine IR that
// instruction selection (nvasm_maxwell.cpp) produces and register allocation,
// scheduling and encoding (maxwell_ra.cpp, maxwell_encode.cpp) consume.
//
// Instructions are Maxwell instructions with virtual registers. A value is a
// scalar 32-bit virtual register (vreg); an operand that must sit in consecutive
// hardware registers (texture coordinates, a 64-bit address) is a group of vregs
// allocated as one aligned block.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace nvasm::maxwell {

constexpr int32_t kRZ = -2;     // the zero register (R255)
constexpr int32_t kPT = -2;     // the always-true predicate (P7)
constexpr int kMaxRegisters = 253;

enum class Op : uint8_t {
    Label,      // pseudo: target = label id
    MOV, MOV32I, SEL,
    FADD, FADD32I, FMUL, FMUL32I, FFMA, FMNMX, FSET, FSETP, MUFU, RRO, F2F, F2I, I2F, I2I, FSWZADD,
    IADD, IADD32I, ISCADD, XMAD, IMNMX, ISET, ISETP, LOP, LOP32I, SHL, SHR, BFE, BFI, POPC, FLO, PSETP,
    LDC, LDG, STG, LDS, STS, LDL, STL, ATOM, ATOMS, BAR, MEMBAR, SULD, SUST, SUATOM,
    IPA, ALD, AST, S2R,
    VMAD, ISBERD, VSETP, OUT,   // geometry: vertex handles and emission
    TEX, TLD, TLD4, TXQ, TXD, PIXLD, SHFL,
    KIL, EXIT, BRA, SSY, SYNC, PBK, BRK, PCNT, CONT, NOP,
};

struct Opnd {
    enum K : uint8_t { None, R, Imm, Cb } k = None;
    int32_t v = -1;         // R: first vreg (or kRZ); Imm: the bits
    uint8_t n = 1;          // R: consecutive vregs (a group)
    uint8_t bank = 0;       // Cb
    uint32_t off = 0;       // Cb: byte offset
    bool neg = false, abs = false;
    static Opnd reg(int32_t v, uint8_t n = 1) { Opnd o; o.k = R; o.v = v; o.n = n; return o; }
    static Opnd imm(uint32_t bits) { Opnd o; o.k = Imm; o.v = int32_t(bits); return o; }
    static Opnd cb(uint8_t bank, uint32_t off) { Opnd o; o.k = Cb; o.bank = bank; o.off = off; return o; }
    static Opnd rz() { return reg(kRZ); }
};

// Comparison codes as Maxwell encodes them (FSET/FSETP/ISET/ISETP): the ordered
// ones and their unordered (…U: true when either operand is NaN) forms.
enum Cmp : uint8_t { F = 0, LT = 1, EQ = 2, LE = 3, GT = 4, NE = 5, GE = 6, NUM = 7, NAN_ = 8, LTU = 9, EQU = 10,
                     LEU = 11, GTU = 12, NEU = 13, GEU = 14, T = 15 };
enum class Mufu : uint8_t { COS = 0, SIN = 1, EX2 = 2, LG2 = 3, RCP = 4, RSQ = 5, SQRT = 8 };
enum class Rnd : uint8_t { RN = 0, RM = 1, RP = 2, RZ = 3 };   // nearest even, floor, ceil, trunc
enum class Lop : uint8_t { AND = 0, OR = 1, XOR = 2, PASS_B = 3 };
enum class BoolOp : uint8_t { AND = 0, OR = 1, XOR = 2 };

struct Inst {
    Op op;
    int32_t guard = kPT;    // predicate vreg (or kPT)
    bool guard_neg = false;
    int32_t d = -1;         // destination vreg (first of a group), or kRZ
    uint8_t dn = 1;         // destination group size
    int32_t pd = -1;        // destination predicate (SETP)
    Opnd a, b, c;
    int32_t pa = kPT;       // source predicate: SEL's selector, SETP's combine input, PSETP's second
    bool pa_neg = false;
    int32_t pb = kPT;       // PSETP's first
    bool pb_neg = false;
    int32_t target = -1;    // Label / branch target label; SYNC, BRK and CONT: where they resume
    // Opcode-specific modifiers.
    uint8_t cmp = 0;        // Cmp
    uint8_t sub = 0;        // Mufu, Rnd, Lop, BoolOp, texture function, conversion kind, ...
    uint32_t flags = 0;     // see the kFlag… bits below
    uint32_t aux = 0;       // attribute address, write mask, LOP3 table, bit-field spec, ...
    uint32_t aux2 = 0;
    std::vector<int32_t> uses;   // further registers read (EXIT: the fragment outputs)
    std::string note;       // for listings
};

// Inst::flags bits (meaning depends on the opcode).
enum : uint32_t {
    kSat = 1 << 0,          // saturate to [0, 1]
    kFtz = 1 << 1,
    kSigned = 1 << 2,       // integer source/destination is signed (conversions, compares, shifts, min/max)
    kSignedDst = 1 << 3,    // conversions: signed destination
    kBF = 1 << 4,           // SET: 1.0f for true instead of 0xFFFFFFFF
    kMax = 1 << 5,          // MNMX: select maximum (the predicate operand selects: PT = min)
    kInvA = 1 << 6, kInvB = 1 << 7,   // LOP operand inversion
    kCC = 1 << 8,           // write the condition code (64-bit address arithmetic)
    kX = 1 << 9,            // add with the carry in
    kIndexed = 1 << 10,     // LDC: Ra holds a byte offset added to the constant offset
    kPerspective = 1 << 11, // IPA: multiply by the interpolated 1/w in Rb
    kFlat = 1 << 12,        // IPA: constant (flat) interpolation
    kLod = 1 << 13, kBias = 1 << 14, kLodZero = 1 << 15, kDepthCompare = 1 << 16, kOffset = 1 << 17, kArray = 1 << 18,
    kHigh = 1 << 19,        // conversions from a half: the upper 16 bits
    kHalf = 1 << 20,        // conversions: 16-bit float source or destination; F2I: 16-bit integer result
    kMultisample = 1 << 21, // TLD: the sample index is in the meta registers
};

// XMAD modifiers (Inst::aux).
enum : uint32_t {
    kXmadHiA = 1 << 0,      // A's upper half
    kXmadHiB = 1 << 1,      // B's upper half
    kXmadPsl = 1 << 2,      // product shifted left by 16
    kXmadMrg = 1 << 3,      // result[31:16] = B[15:0]
    kXmadCbcc = 1 << 4,     // C + (B << 16)
};

struct Program {
    std::vector<Inst> code;
    int32_t vregs = 0, pregs = 0, labels = 0;
    std::vector<int32_t> fixed;     // vreg -> required hardware register, or -1
    std::vector<uint8_t> align;     // vreg -> alignment of the group it starts (1, 2 or 4)
    std::vector<int32_t> group;     // vreg -> first vreg of its group (itself when alone)
    int32_t new_vreg() { fixed.push_back(-1); align.push_back(1); group.push_back(vregs); return vregs++; }
    int32_t new_group(int n) {
        const int32_t first = vregs;
        for (int i = 0; i < n; ++i) { fixed.push_back(-1); align.push_back(1); group.push_back(first); ++vregs; }
        align[size_t(first)] = uint8_t(n >= 3 ? 4 : n);
        return first;
    }
    int32_t new_pred() { return pregs++; }
    int32_t new_label() { return labels++; }
};

// After allocation: hardware registers for every vreg and predicate, and the
// highest register used.
struct Allocation {
    std::vector<int32_t> reg;   // vreg -> R0..R252
    std::vector<int32_t> pred;  // pred -> P0..P6
    int32_t registers = 0;      // highest register + 1
};

void optimize(Program& p);                             // maxwell_opt.cpp: folding, coalescing, dead code
Allocation allocate(const Program& p);                 // maxwell_ra.cpp; throws Error when out of registers
struct Control { uint8_t stall = 1, yield = 1, wbar = 7, rbar = 7, wait = 0, reuse = 0; };
std::vector<Control> schedule(const Program& p, const Allocation& a);   // maxwell_ra.cpp
uint64_t encode(const Inst& i, const Allocation& a, int32_t pc, const std::vector<int32_t>& label_pc); // maxwell_encode.cpp
std::string listing(const Program& p, const Allocation* a);           // maxwell_encode.cpp

} // namespace nvasm::maxwell

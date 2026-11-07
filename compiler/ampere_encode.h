#pragma once
// Instruction encoder for the Switch 2's Ampere-class GPU (docs/SWITCH2_SHADERS.md, section 4): the start of Kadan's
// Ampere backend. Field positions and opcode forms are ported from Mesa NAK's src/nouveau/compiler/nak/sm70_encode.rs:
//
//   Copyright © 2022 Collabora, Ltd.
//   SPDX-License-Identifier: MIT
//
// Each encoder here is checked against NVIDIA's disassembler (nvdisasm -b SM86) in tests/ampere_encode_test.cpp and
// against the game's own instruction words in tests/own_sage_tool.cpp (reencode). Covered: float, half and integer
// ALU, moves, MUFU, compares and selects, shifts, predicate logic, conversions, attribute interpolation, attribute
// loads and stores and geometry output (ALD, AST, OUT, ISBERD), control flow (EXIT, BRA, BSSY/BSYNC/BREAK, CALL/RET,
// BAR, the exit address), special registers, votes, DEPBAR, textures and surfaces (bindless, constant-bank and uniform
// handles), the uniform datapath, and constant, local, shared and global memory. Where the game's compiler fills a
// field differently from NAK, the encoder follows the game and says so.
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace nvasm::ampere {

// One 128-bit instruction, words[0] holding bits 0-31.
struct Word {
    std::array<std::uint32_t, 4> words{};
    void Set(unsigned start, unsigned count, std::uint64_t value) {
        if (count < 64 && (value >> count)) throw std::runtime_error("ampere encode: field value too wide");
        for (unsigned i = 0; i < count; ++i) {
            const unsigned bit = start + i;
            words[bit / 32] = (words[bit / 32] & ~(1u << (bit % 32))) | (std::uint32_t((value >> i) & 1) << (bit % 32));
        }
    }
    void Bit(unsigned bit, bool on) { Set(bit, 1, on ? 1 : 0); }
    std::uint32_t Get(unsigned start, unsigned count) const {
        std::uint64_t v = 0;
        for (unsigned i = 0; i < count; ++i) v |= std::uint64_t((words[(start + i) / 32] >> ((start + i) % 32)) & 1) << i;
        return std::uint32_t(v);
    }
    bool operator==(const Word& o) const { return words == o.words; }
};

constexpr unsigned RZ = 255;   // zero register
constexpr unsigned PT = 7;     // always-true predicate

// Scheduling control (bits 105-125): stall cycles, yield, the scoreboards an instruction sets and waits on, reuse.
struct Deps {
    unsigned stall = 1;
    bool yield = false;
    unsigned write_barrier = 7, read_barrier = 7;   // 7: none
    unsigned wait_mask = 0;
    unsigned reuse = 0;
};

// A source: register, 32-bit immediate, constant c[bank][offset] or uniform register, with float modifiers.
struct Src {
    enum class Kind { Reg, Imm, Const, UReg } kind = Kind::Reg;
    unsigned reg = RZ;
    std::uint32_t imm = 0;
    unsigned bank = 0, offset = 0;
    bool neg = false, abs = false;
    static Src R(unsigned r, bool neg = false, bool abs = false) { Src s; s.reg = r; s.neg = neg; s.abs = abs; return s; }
    static Src Zero() { return R(RZ); }
    static Src I(std::uint32_t v) { Src s; s.kind = Kind::Imm; s.imm = v; return s; }
    static Src C(unsigned bank, unsigned offset, bool neg = false, bool abs = false) {
        Src s; s.kind = Kind::Const; s.bank = bank; s.offset = offset; s.neg = neg; s.abs = abs; return s;
    }
    static Src U(unsigned ur, bool neg = false, bool abs = false) { Src s; s.kind = Kind::UReg; s.reg = ur; s.neg = neg; s.abs = abs; return s; }
};

enum class Round { Nearest = 0, Down = 1, Up = 2, Zero = 3 };
enum class Cmp { Lt = 1, Eq = 2, Le = 3, Gt = 4, Ne = 5, Ge = 6, Num = 7, Nan = 8, Ltu = 9, Equ = 10, Leu = 11, Gtu = 12, Neu = 13, Geu = 14 };
enum class PredOp { And = 0, Or = 1, Xor = 2 };
enum class MuFuOp { Cos = 0, Sin = 1, Ex2 = 2, Lg2 = 3, Rcp = 4, Rsq = 5, Rcp64h = 6, Rsq64h = 7, Sqrt = 8, Tanh = 9 };
enum class InterpFreq { Pass = 0, Constant = 1, State = 2 };
enum class InterpLoc { Default = 0, Centroid = 1, Offset = 2 };

class Encoder {
public:
    Word w;

    void Guard(unsigned pred, bool negate) { w.Set(12, 3, pred); w.Bit(15, negate); }
    void Control(const Deps& d) {
        w.Set(105, 4, d.stall);
        w.Bit(109, d.yield);
        w.Set(110, 3, d.write_barrier);
        w.Set(113, 3, d.read_barrier);
        w.Set(116, 6, d.wait_mask);
        w.Set(122, 4, d.reuse);
    }

    // NAK encode_alu_base: dst 16-23, src0 24-31 (abs 73, neg 72); src1 in 32-63 as register (abs 62, neg 63), immediate,
    // constant or uniform register; src2 64-71 (abs 74, neg 75). An immediate or constant src2 takes the 32-63 slot and
    // src1 moves to 64-71. Form in bits 9-11: 1 reg, 2 imm src2, 3 const src2, 4 imm src1, 5 const src1, 6 ureg src1,
    // 7 ureg src2.
    void Alu(unsigned opcode, unsigned dst, const Src* s0, const Src* s1, const Src* s2) {
        w.Set(16, 8, dst);
        if (s0) {
            Need(s0->kind == Src::Kind::Reg, "src0 must be a register");
            w.Set(24, 8, s0->reg); w.Bit(73, s0->abs); w.Bit(72, s0->neg);
        }
        unsigned form;
        if (!s2 || s2->kind == Src::Kind::Reg) {
            if (s2) { w.Set(64, 8, s2->reg); w.Bit(74, s2->abs); w.Bit(75, s2->neg); }
            form = s1 ? Slot1(*s1) : 1;
        } else {
            const unsigned slot = Slot1(*s2);
            form = slot == 4 ? 2 : slot == 5 ? 3 : 7;
            if (s1) {
                Need(s1->kind == Src::Kind::Reg, "src1 must be a register when src2 is not");
                w.Set(64, 8, s1->reg); w.Bit(74, s1->abs); w.Bit(75, s1->neg);
            }
        }
        w.Set(0, 9, opcode);
        w.Set(9, 3, form);
    }

    // NAK encode_ualu_base: the uniform datapath's form of Alu. Registers are uniform (6 bits: udst 16-21, src0 24-29,
    // src1 32-37, src2 64-69) and bit 91 is always set. Sources are Src::U or immediates: form 1 registers, 4 immediate
    // src1, 2 immediate src2 (src1 then moves to 64-69).
    void UAlu(unsigned opcode, unsigned udst, const Src* s0, const Src* s1, const Src* s2) {
        w.Set(16, 6, udst);
        w.Bit(91, true);
        if (s0) {
            Need(s0->kind == Src::Kind::UReg, "uniform src0 must be a uniform register");
            w.Set(24, 6, s0->reg); w.Bit(73, s0->abs); w.Bit(72, s0->neg);
        }
        const auto third = [&](const Src& s) {
            Need(s.kind == Src::Kind::UReg, "uniform src2 must be a uniform register");
            w.Set(64, 6, s.reg); w.Bit(74, s.abs); w.Bit(75, s.neg);
        };
        unsigned form = 1;
        if (!s2 || s2->kind == Src::Kind::UReg) {
            if (s2) third(*s2);
            if (s1 && s1->kind == Src::Kind::UReg) { w.Set(32, 6, s1->reg); w.Bit(62, s1->abs); w.Bit(63, s1->neg); }
            else if (s1 && s1->kind == Src::Kind::Imm) { w.Set(32, 32, s1->imm); form = 4; }
            else Need(!s1, "uniform src1 must be a uniform register or an immediate");
        } else {
            Need(s2->kind == Src::Kind::Imm, "uniform src2 must be a uniform register or an immediate");
            w.Set(32, 32, s2->imm);
            if (s1) third(*s1);
            form = 2;
        }
        w.Set(0, 9, opcode);
        w.Set(9, 3, form);
    }

    static void Need(bool ok, const char* what) { if (!ok) throw std::runtime_error(std::string("ampere encode: ") + what); }

private:
    // Puts a source in bits 32-63 and returns its form when it is src1.
    unsigned Slot1(const Src& s) {
        switch (s.kind) {
        case Src::Kind::Reg: w.Set(32, 8, s.reg); w.Bit(62, s.abs); w.Bit(63, s.neg); return 1;
        case Src::Kind::Imm: w.Set(32, 32, s.imm); return 4;
        case Src::Kind::Const:
            Need(s.offset < 0x10000 && s.bank < 32, "constant out of range");
            w.Set(38, 16, s.offset); w.Set(54, 5, s.bank); w.Bit(91, false); w.Bit(62, s.abs); w.Bit(63, s.neg);
            return 5;
        case Src::Kind::UReg: w.Set(32, 6, s.reg); w.Bit(62, s.abs); w.Bit(63, s.neg); w.Bit(91, true); return 6;
        }
        return 1;
    }
};

// ---- Instructions -------------------------------------------------------------------------------------------------

inline Word Finish(Encoder& e, const Deps& d, unsigned pred, bool negate) { e.Guard(pred, negate); e.Control(d); return e.w; }

inline Word FAdd(unsigned dst, Src a, Src b, bool sat = false, bool ftz = true, Round rnd = Round::Nearest,
                 const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    // An immediate, constant or uniform-register b takes the src2 slot (forms 2, 3, 7, as the game writes them); its
    // unused src1 field stays 0 as the game's compiler leaves it (NAK writes RZ there; nvdisasm reads both the same).
    Encoder e;
    if (b.kind == Src::Kind::Reg) e.Alu(0x021, dst, &a, &b, nullptr);
    else e.Alu(0x021, dst, &a, nullptr, &b);
    e.w.Bit(77, sat); e.w.Set(78, 2, unsigned(rnd)); e.w.Bit(80, ftz);
    return Finish(e, d, pred, negate);
}

// FMUL. NAK writes RZ in the unused third source field; the game's compiler leaves it 0 (nvdisasm reads both the
// same), so this follows the game. `scale` is the post-multiply field (bits 84-86): 4 for none, as NAK writes it;
// the game also uses others (FMUL.D2 and so on).
inline Word FMul(unsigned dst, Src a, Src b, bool sat = false, bool ftz = true, Round rnd = Round::Nearest,
                 const Deps& d = {}, unsigned pred = PT, bool negate = false, unsigned scale = 4) {
    Encoder e;
    e.Alu(0x020, dst, &a, &b, nullptr);
    e.w.Bit(77, sat); e.w.Set(78, 2, unsigned(rnd)); e.w.Bit(80, ftz);
    e.w.Set(84, 3, scale);
    return Finish(e, d, pred, negate);
}

inline Word FFma(unsigned dst, Src a, Src b, Src c, bool sat = false, bool ftz = true, Round rnd = Round::Nearest,
                 const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.Alu(0x023, dst, &a, &b, &c);
    e.w.Bit(77, sat); e.w.Set(78, 2, unsigned(rnd)); e.w.Bit(80, ftz);
    return Finish(e, d, pred, negate);
}

inline Word Mov(unsigned dst, Src s, const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.Alu(0x002, dst, nullptr, &s, nullptr);
    e.w.Set(72, 4, 0xf);   // all quad lanes
    return Finish(e, d, pred, negate);
}

inline Word MuFu(MuFuOp op, unsigned dst, Src s, const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.Alu(0x108, dst, nullptr, &s, nullptr);
    e.w.Set(74, 6, unsigned(op));
    return Finish(e, d, pred, negate);
}

// FMNMX: the predicate picks min (true) or max (false), as `PT` / `!PT` in nvdisasm.
inline Word FMnMx(unsigned dst, Src a, Src b, bool min, bool ftz = true, const Deps& d = {}, unsigned pred = PT,
                  bool negate = false) {
    Encoder e;
    e.Alu(0x009, dst, &a, &b, nullptr);
    e.w.Set(87, 3, PT); e.w.Bit(90, !min);
    e.w.Bit(80, ftz);
    return Finish(e, d, pred, negate);
}

// FSEL dst = cond ? a : b.
inline Word FSel(unsigned dst, Src a, Src b, unsigned cond, bool condNot, const Deps& d = {}, unsigned pred = PT,
                 bool negate = false) {
    Encoder e;
    e.Alu(0x008, dst, &a, &b, nullptr);
    e.w.Set(87, 3, cond); e.w.Bit(90, condNot);
    return Finish(e, d, pred, negate);
}

// FSETP p = (a cmp b) op accum.
inline Word FSetP(unsigned pdst, Src a, Src b, Cmp cmp, PredOp op = PredOp::And, unsigned accum = PT,
                  bool accumNot = false, bool ftz = true, const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.Alu(0x00b, RZ, &a, &b, nullptr);
    e.w.Set(16, 8, 0);
    e.w.Set(81, 3, pdst); e.w.Set(84, 3, PT);
    e.w.Set(87, 3, accum); e.w.Bit(90, accumNot);
    e.w.Set(74, 2, unsigned(op)); e.w.Set(76, 4, unsigned(cmp)); e.w.Bit(80, ftz);
    return Finish(e, d, pred, negate);
}

// IPA dst, a[addr]: interpolate attribute byte address `addr`.
inline Word Ipa(unsigned dst, unsigned addr, InterpFreq freq = InterpFreq::Pass, InterpLoc loc = InterpLoc::Default,
                const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    if (addr % 4 || addr >= 1024) throw std::runtime_error("ampere encode: attribute address");
    Encoder e;
    e.w.Set(0, 12, 0x326);
    e.w.Set(16, 8, dst);
    e.w.Set(32, 8, RZ);
    e.w.Set(64, 8, addr >> 2);
    e.w.Set(76, 2, unsigned(loc));
    e.w.Set(78, 2, unsigned(freq));
    e.w.Set(81, 3, PT);
    return Finish(e, d, pred, negate);
}

// EXIT; `cond` is its own predicate beside the guard (bits 87-90, "EXIT P2").
inline Word Exit(const Deps& d = {}, unsigned pred = PT, bool negate = false, unsigned cond = PT, bool condNot = false) {
    Encoder e;
    e.w.Set(0, 12, 0x94d);
    e.w.Set(87, 3, cond); e.w.Bit(90, condNot);
    return Finish(e, d, pred, negate);
}

// BRA to an offset in bytes from the instruction after this one. `cond` is the branch's own predicate (bits 87-90,
// "BRA P3, target"); the game's compiler uses it beside the guard.
inline Word Bra(std::int64_t offset, const Deps& d = {}, unsigned pred = PT, bool negate = false, unsigned cond = PT,
                bool condNot = false) {
    if (offset % 4) throw std::runtime_error("ampere encode: branch offset");
    Encoder e;
    e.w.Set(0, 12, 0x947);
    e.w.Set(34, 48, std::uint64_t(offset / 4) & ((std::uint64_t(1) << 48) - 1));
    e.w.Set(87, 3, cond); e.w.Bit(90, condNot);
    return Finish(e, d, pred, negate);
}

inline Word Kill(const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.w.Set(0, 12, 0x95b);
    e.w.Set(87, 3, PT);
    return Finish(e, d, pred, negate);
}

// ---- Integer ALU --------------------------------------------------------------------------------------------------

enum class ICmp { False = 0, Lt = 1, Eq = 2, Le = 3, Gt = 4, Ne = 5, Ge = 6, True = 7 };

// IADD3 dst = a + b + c (neg on a or b subtracts). By default no carry in or out: carry-in predicates !PT, carry-out
// PT. `pdst`/`pdst2` take the carry-outs ("IADD3 R0, P0, ..."); `x` is IADD3.X, adding carry-in predicate `cin`
// (bits 87-90) and `cin2` (77-80), as the game writes 64-bit adds. Only b may be an immediate, constant or uniform
// register (the game has no IADD3 with c in the 32-63 slot, and nvdisasm refuses one).
inline Word IAdd3(unsigned dst, Src a, Src b, Src c, const Deps& d = {}, unsigned pred = PT, bool negate = false,
                  unsigned pdst = PT, unsigned pdst2 = PT, bool x = false, unsigned cin = PT, bool cinNot = true,
                  unsigned cin2 = PT, bool cin2Not = true) {
    if (c.kind != Src::Kind::Reg) throw std::runtime_error("ampere encode: IADD3 takes c in a register");
    Encoder e;
    e.Alu(0x010, dst, &a, &b, &c);
    e.w.Set(87, 3, cin); e.w.Bit(90, cinNot);
    e.w.Set(77, 3, cin2); e.w.Bit(80, cin2Not);
    e.w.Set(81, 3, pdst); e.w.Set(84, 3, pdst2);
    e.w.Bit(74, x);
    return Finish(e, d, pred, negate);
}

// LOP3.LUT dst = lut(a, b, c); `pdst` also gets whether the result is nonzero (PT: no predicate output).
inline Word Lop3(unsigned dst, Src a, Src b, Src c, unsigned lut, const Deps& d = {}, unsigned pred = PT,
                 bool negate = false, unsigned pdst = PT) {
    Encoder e;
    e.Alu(0x012, dst, &a, &b, &c);
    e.w.Set(87, 3, PT); e.w.Bit(90, true);
    e.w.Set(72, 8, lut);
    e.w.Bit(80, false);
    e.w.Set(81, 3, pdst);
    return Finish(e, d, pred, negate);
}

// IMAD dst = a * b + c (32-bit, low half). `x` is IMAD.X, adding carry-in predicate `cin` (bits 87-90), the high half of
// the game's 64-bit adds.
inline Word IMad(unsigned dst, Src a, Src b, Src c, bool isSigned, const Deps& d = {}, unsigned pred = PT,
                 bool negate = false, bool x = false, unsigned cin = PT, bool cinNot = true) {
    Encoder e;
    e.Alu(0x024, dst, &a, &b, &c);
    e.w.Set(81, 3, PT);
    e.w.Set(87, 3, cin); e.w.Bit(90, cinNot);   // carry-in !PT by default, as the game's compiler writes it (NAK leaves 0)
    e.w.Bit(73, isSigned); e.w.Bit(74, x);
    return Finish(e, d, pred, negate);
}

// ISETP p = (a cmp b) op accum, 32-bit, no extended compare.
inline Word ISetP(unsigned pdst, Src a, Src b, ICmp cmp, bool isSigned, PredOp op = PredOp::And, unsigned accum = PT,
                  bool accumNot = false, const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.Alu(0x00c, RZ, &a, &b, nullptr);
    e.w.Set(16, 8, 0);
    e.w.Set(68, 3, PT); e.w.Bit(71, false);
    e.w.Set(87, 3, accum); e.w.Bit(90, accumNot);
    e.w.Bit(72, false);
    e.w.Bit(73, isSigned);
    e.w.Set(74, 2, unsigned(op)); e.w.Set(76, 3, unsigned(cmp));
    e.w.Set(81, 3, pdst); e.w.Set(84, 3, PT);
    return Finish(e, d, pred, negate);
}

inline Word Nop(const Deps& d = {}) {
    Encoder e;
    e.w.Set(0, 12, 0x918);
    return Finish(e, d, PT, false);
}

// ---- More ALU: compares to registers, selects, shifts, min/max, predicate logic, bit counts, permutes -------------

constexpr unsigned URZ = 63;   // uniform zero register
constexpr unsigned UPT = 7;    // uniform always-true predicate

// FSET.BF dst = (a cmp b) op accum ? 1.0 : 0.0. The unused third source field stays 0 as the game's compiler leaves it.
inline Word FSet(unsigned dst, Src a, Src b, Cmp cmp, PredOp op = PredOp::And, unsigned accum = PT, bool accumNot = false,
                 bool ftz = true, const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.Alu(0x00a, dst, &a, &b, nullptr);
    e.w.Set(87, 3, accum); e.w.Bit(90, accumNot);
    e.w.Set(74, 2, unsigned(op)); e.w.Set(76, 4, unsigned(cmp)); e.w.Bit(80, ftz);
    return Finish(e, d, pred, negate);
}

// SEL dst = cond ? a : b (integer).
inline Word Sel(unsigned dst, Src a, Src b, unsigned cond, bool condNot, const Deps& d = {}, unsigned pred = PT,
                bool negate = false) {
    Encoder e;
    e.Alu(0x007, dst, &a, &b, nullptr);
    e.w.Set(87, 3, cond); e.w.Bit(90, condNot);
    return Finish(e, d, pred, negate);
}

// Shift data types (bits 73-74), as SHF prints them.
enum class ShfType { S64 = 0, U64 = 1, S32 = 2, U32 = 3 };

// SHF dst = funnel shift of the pair hi:lo by `shift`; `right` for .R, `high` for .HI (the high word of the result),
// `wrap` for .W (shift taken modulo the width).
inline Word Shf(unsigned dst, Src lo, Src shift, Src hi, bool right, ShfType type = ShfType::U32, bool high = false,
                bool wrap = false, const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.Alu(0x019, dst, &lo, &shift, &hi);
    e.w.Set(73, 2, unsigned(type)); e.w.Bit(75, wrap); e.w.Bit(76, right); e.w.Bit(80, high);
    return Finish(e, d, pred, negate);
}

// IMNMX dst = min (PT) or max (!PT) of a and b.
inline Word IMnMx(unsigned dst, Src a, Src b, bool min, bool isSigned, const Deps& d = {}, unsigned pred = PT,
                  bool negate = false) {
    Encoder e;
    e.Alu(0x017, dst, &a, &b, nullptr);
    e.w.Set(87, 3, PT); e.w.Bit(90, !min);
    e.w.Bit(73, isSigned);
    return Finish(e, d, pred, negate);
}

// A predicate source: P0-P6 or PT, optionally negated; `uniform` for UP0-UP6/UPT (PLOP3's c only, bit 67).
struct PSrc {
    unsigned p = PT;
    bool neg = false;
    bool uniform = false;
};

// PLOP3.LUT pdst = lut(a, b, c), pdst2 = lut2(a, b, c). Only c may be a uniform predicate, as the game uses it to
// copy UP0 into a predicate.
inline Word PLop3(unsigned pdst, PSrc a, PSrc b, PSrc c, unsigned lut, unsigned pdst2 = PT, unsigned lut2 = 0,
                  const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.w.Set(0, 12, 0x81c);
    e.w.Set(87, 3, a.p); e.w.Bit(90, a.neg);
    e.w.Set(77, 3, b.p); e.w.Bit(80, b.neg);
    e.w.Set(68, 3, c.p); e.w.Bit(71, c.neg); e.w.Bit(67, c.uniform);
    if (a.uniform || b.uniform) throw std::runtime_error("ampere encode: PLOP3 takes a uniform predicate in c only");
    e.w.Set(16, 8, lut2);
    e.w.Set(64, 3, lut & 7); e.w.Set(72, 5, lut >> 3);
    e.w.Set(81, 3, pdst); e.w.Set(84, 3, pdst2);
    return Finish(e, d, pred, negate);
}

// POPC dst = number of set bits (of ~s with `invert`).
inline Word PopC(unsigned dst, Src s, bool invert = false, const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.Alu(0x109, dst, nullptr, &s, nullptr);
    e.w.Bit(63, invert);
    return Finish(e, d, pred, negate);
}

// FLO dst = position of the highest set bit (sign bit for signed), or its shift amount with `shiftAmount` (.SH).
inline Word Flo(unsigned dst, Src s, bool isSigned = false, bool shiftAmount = false, bool invert = false,
                const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.Alu(0x100, dst, nullptr, &s, nullptr);
    e.w.Set(81, 3, PT);
    e.w.Bit(74, shiftAmount); e.w.Bit(73, isSigned); e.w.Bit(63, invert);
    return Finish(e, d, pred, negate);
}

// IABS dst = |s|.
inline Word IAbs(unsigned dst, Src s, const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.Alu(0x013, dst, nullptr, &s, nullptr);
    return Finish(e, d, pred, negate);
}

enum class PrmtMode { Index = 0, F4E = 1, B4E = 2, Rc8 = 3, Ecl = 4, Ecr = 5, Rc16 = 6 };

// PRMT dst = bytes of the pair b:a chosen by `sel`.
inline Word Prmt(unsigned dst, Src a, Src sel, Src b, PrmtMode mode = PrmtMode::Index, const Deps& d = {},
                 unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.Alu(0x016, dst, &a, &sel, &b);
    e.w.Set(72, 3, unsigned(mode));
    return Finish(e, d, pred, negate);
}

// LEA dst = (a << shift) + b; with `high`, (hi:a >> (32 - shift)) + b (LEA.HI), `sx32` sign-extending a as the high
// word. The game writes the unused carry-in predicate as !PT (bits 87-90) and RZ in the unused third source.
inline Word Lea(unsigned dst, Src a, Src b, unsigned shift, bool high = false, bool sx32 = false, Src hi = Src::Zero(),
                const Deps& d = {}, unsigned pred = PT, bool negate = false, unsigned pdst = PT, bool negA = false) {
    if (shift >= 32) throw std::runtime_error("ampere encode: LEA shift");
    Encoder e;
    e.Alu(0x011, dst, &a, &b, &hi);
    e.w.Bit(72, negA); e.w.Bit(73, sx32); e.w.Bit(74, false);
    e.w.Set(75, 5, shift); e.w.Bit(80, high);
    e.w.Set(81, 3, pdst);
    e.w.Set(87, 3, PT); e.w.Bit(90, true);
    return Finish(e, d, pred, negate);
}

// R2P: predicates P0-P6 from the bits of a under `mask`. P2R dst = predicates under `mask` as bits (RZ base).
inline Word R2P(Src a, std::uint32_t mask, const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    const Src m = Src::I(mask);
    e.Alu(0x004, 0, &a, &m, nullptr);
    return Finish(e, d, pred, negate);
}
inline Word P2R(unsigned dst, std::uint32_t mask, const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    const Src z = Src::Zero(), m = Src::I(mask);
    e.Alu(0x003, dst, &z, &m, nullptr);
    return Finish(e, d, pred, negate);
}

// ---- Conversions --------------------------------------------------------------------------------------------------

// Integer types: size 8 << (value / 2), signed when odd.
enum class IntType { U8 = 0, S8 = 1, U16 = 2, S16 = 3, U32 = 4, S32 = 5, U64 = 6, S64 = 7 };
inline unsigned SizeLog2(unsigned bits) { return bits == 8 ? 0 : bits == 16 ? 1 : bits == 32 ? 2 : 3; }
inline unsigned IntBits(IntType t) { return 8u << (unsigned(t) / 2); }
inline bool IntSigned(IntType t) { return unsigned(t) & 1; }

// F2I dst = float s (srcBits wide) to an integer, rounding `rnd`. The game's compiler always sets .NTZ (bit 77; NAK
// leaves it 0), so `ntz` defaults on.
inline Word F2I(unsigned dst, Src s, IntType to, Round rnd = Round::Zero, bool ftz = false, unsigned srcBits = 32,
                bool ntz = true, const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.Alu(srcBits <= 32 && IntBits(to) <= 32 ? 0x105 : 0x111, dst, nullptr, &s, nullptr);
    e.w.Bit(72, IntSigned(to)); e.w.Set(75, 2, SizeLog2(IntBits(to))); e.w.Bit(77, ntz);
    e.w.Set(78, 2, unsigned(rnd)); e.w.Bit(80, ftz); e.w.Set(84, 2, SizeLog2(srcBits));
    return Finish(e, d, pred, negate);
}

// I2F dst = integer s to a float dstBits wide. `byteSel` (bits 60-61) picks the byte or half of an 8- or 16-bit source
// (R2.B3, R2.H1).
inline Word I2F(unsigned dst, Src s, IntType from, unsigned dstBits = 32, Round rnd = Round::Nearest, unsigned byteSel = 0,
                const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.Alu(IntBits(from) <= 32 && dstBits <= 32 ? 0x106 : 0x112, dst, nullptr, &s, nullptr);
    e.w.Set(60, 2, byteSel);
    e.w.Bit(74, IntSigned(from)); e.w.Set(75, 2, SizeLog2(dstBits));
    e.w.Set(78, 2, unsigned(rnd)); e.w.Set(84, 2, SizeLog2(IntBits(from)));
    return Finish(e, d, pred, negate);
}

// I2FP.F32.S32/U32: the SM86 ALU-pipe form of a 32-bit integer to float conversion (opcode 0x045), as the game uses it
// for every 32-bit I2F. Fields as I2F's (read from the game's instructions; NAK has no I2FP).
inline Word I2FP(unsigned dst, Src s, bool isSigned, Round rnd = Round::Nearest, const Deps& d = {}, unsigned pred = PT,
                 bool negate = false) {
    Encoder e;
    e.Alu(0x045, dst, nullptr, &s, nullptr);
    e.w.Bit(74, isSigned); e.w.Set(75, 2, 2);
    e.w.Set(78, 2, unsigned(rnd)); e.w.Set(84, 2, 2);
    return Finish(e, d, pred, negate);
}

// F2F dst = float s (srcBits wide) as a float dstBits wide.
inline Word F2F(unsigned dst, Src s, unsigned dstBits, unsigned srcBits, Round rnd = Round::Nearest, bool ftz = false,
                const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.Alu(srcBits <= 32 && dstBits <= 32 ? 0x104 : 0x110, dst, nullptr, &s, nullptr);
    e.w.Set(75, 2, SizeLog2(dstBits)); e.w.Set(78, 2, unsigned(rnd)); e.w.Bit(80, ftz); e.w.Set(84, 2, SizeLog2(srcBits));
    return Finish(e, d, pred, negate);
}

// FRND dst = s rounded to an integral value (FRND.FLOOR is Round::Down).
inline Word FRnd(unsigned dst, Src s, Round rnd, bool ftz = false, unsigned bits = 32, const Deps& d = {},
                 unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.Alu(bits <= 32 ? 0x107 : 0x113, dst, nullptr, &s, nullptr);
    e.w.Set(75, 2, SizeLog2(bits)); e.w.Set(78, 2, unsigned(rnd)); e.w.Bit(80, ftz); e.w.Set(84, 2, SizeLog2(bits));
    return Finish(e, d, pred, negate);
}

// F2FP.PACK_AB dst = half(a) << 16 | half(b). The unused third source is RZ, as NAK writes it.
inline Word F2FP(unsigned dst, Src a, Src b, Round rnd = Round::Nearest, const Deps& d = {}, unsigned pred = PT,
                 bool negate = false) {
    Encoder e;
    const Src z = Src::Zero();
    e.Alu(0x03e, dst, &a, &b, &z);
    e.w.Set(79, 2, unsigned(rnd));
    return Finish(e, d, pred, negate);
}

// I2I.<to>.S32.SAT dst = s saturated to an 8- or 16-bit type. Bits 76-77 hold the type (read with nvdisasm; NAK has no
// I2I).
inline Word I2I(unsigned dst, Src s, IntType to, const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    if (IntBits(to) > 16) throw std::runtime_error("ampere encode: I2I goes to 8 or 16 bits");
    Encoder e;
    e.Alu(0x038, dst, nullptr, &s, nullptr);
    e.w.Set(76, 2, unsigned(to));
    return Finish(e, d, pred, negate);
}

// SHFL.<mode> pdst, dst, src, lane, c with an immediate lane and clamp/segment mask (NAK's 0xf89 form; the game's
// derivatives use SHFL.BFLY PT, R, R, 0x1, 0x1c03).
enum class ShflMode { Idx = 0, Up = 1, Down = 2, Bfly = 3 };
inline Word Shfl(unsigned dst, unsigned src, unsigned lane, unsigned c, ShflMode mode, unsigned pdst = PT,
                 const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.w.Set(0, 12, 0xf89);
    e.w.Set(16, 8, dst); e.w.Set(24, 8, src);
    e.w.Set(40, 13, c & 0x1f1f); e.w.Set(53, 5, lane & 0x1f); e.w.Set(58, 2, unsigned(mode));
    e.w.Set(81, 3, pdst);
    return Finish(e, d, pred, negate);
}

// FSWZADD dst, a, b, ops: per quad lane a + b, a - b, b - a or b; `ops` holds lane 0's operation in bits 6-7 down to
// lane 3's in bits 0-1 (0 add, 1 a - b, 2 b - a, 3 b), as NAK encodes it; nvdisasm prints the signs per lane.
inline Word FSwzAdd(unsigned dst, unsigned a, unsigned b, unsigned ops, bool ftz = false, Round rnd = Round::Nearest,
                    const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.w.Set(0, 12, 0x822);
    e.w.Set(16, 8, dst); e.w.Set(24, 8, a); e.w.Set(32, 8, ops & 0xff); e.w.Set(64, 8, b);
    e.w.Bit(77, false); e.w.Set(78, 2, unsigned(rnd)); e.w.Bit(80, ftz);
    return Finish(e, d, pred, negate);
}

// ---- Control flow and special registers ---------------------------------------------------------------------------

// BSSY Bn, target: the convergence barrier `bar` (B0-B15) for the reconvergence point `offset` bytes after the next
// instruction.
inline Word BSsy(unsigned bar, std::int64_t offset, const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    if (offset % 4 || bar > 15) throw std::runtime_error("ampere encode: BSSY");
    Encoder e;
    e.w.Set(0, 12, 0x945);
    e.w.Set(16, 4, bar);
    e.w.Set(34, 30, std::uint64_t(offset / 4) & ((std::uint64_t(1) << 30) - 1));
    e.w.Set(87, 3, PT);
    return Finish(e, d, pred, negate);
}

inline Word BSync(unsigned bar, const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    if (bar > 15) throw std::runtime_error("ampere encode: BSYNC");
    Encoder e;
    e.w.Set(0, 12, 0x941);
    e.w.Set(16, 4, bar);
    e.w.Set(87, 3, PT);
    return Finish(e, d, pred, negate);
}

// BREAK [cond,] Bn: the threads leave barrier `bar`.
inline Word Break(unsigned bar, unsigned cond = PT, bool condNot = false, const Deps& d = {}, unsigned pred = PT,
                  bool negate = false) {
    if (bar > 15) throw std::runtime_error("ampere encode: BREAK");
    Encoder e;
    e.w.Set(0, 12, 0x942);
    e.w.Set(16, 4, bar);
    e.w.Set(87, 3, cond); e.w.Bit(90, condNot);
    return Finish(e, d, pred, negate);
}

// BMOV.32 dst, Bn (barrier to register) and BMOV.32 Bn, src (register to barrier), as NAK; `clear` is .CLEAR.
inline Word BMovFromBar(unsigned dst, unsigned bar, bool clear = false, const Deps& d = {}, unsigned pred = PT,
                        bool negate = false) {
    Encoder e;
    e.w.Set(0, 12, 0x355);
    e.w.Set(16, 8, dst); e.w.Set(24, 4, bar); e.w.Bit(84, clear);
    return Finish(e, d, pred, negate);
}
inline Word BMovToBar(unsigned bar, unsigned src, bool clear = false, const Deps& d = {}, unsigned pred = PT,
                      bool negate = false) {
    Encoder e;
    e.w.Set(0, 12, 0x356);
    e.w.Set(24, 4, bar); e.w.Set(32, 8, src); e.w.Bit(84, clear);
    return Finish(e, d, pred, negate);
}

inline Word WarpSync(std::uint32_t mask = 0xffffffffu, const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    const Src m = Src::I(mask);
    e.Alu(0x148, 0, nullptr, &m, nullptr);
    e.w.Set(87, 3, PT);
    return Finish(e, d, pred, negate);
}

// CALL.REL.NOINC to an offset in bytes from the next instruction, as the game calls its subroutines (bit 86: .NOINC).
inline Word Call(std::int64_t offset, bool noInc = true, const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    if (offset % 4) throw std::runtime_error("ampere encode: call offset");
    Encoder e;
    e.w.Set(0, 12, 0x944);
    e.w.Set(34, 48, std::uint64_t(offset / 4) & ((std::uint64_t(1) << 48) - 1));
    e.w.Bit(86, noInc);
    e.w.Set(87, 3, PT);
    return Finish(e, d, pred, negate);
}

// RET.REL.NODEC Rn: return to the address in register pair `reg`; the offset field is the one the game's RET carries
// (the distance back to its function's start), bit 85 .ABS, bit 86 .NODEC.
inline Word Ret(unsigned reg, std::int64_t offset = 0, bool absolute = false, bool noDec = true, const Deps& d = {},
                unsigned pred = PT, bool negate = false) {
    if (offset % 4) throw std::runtime_error("ampere encode: return offset");
    Encoder e;
    e.w.Set(0, 12, 0x950);
    e.w.Set(24, 8, reg);
    e.w.Set(34, 48, std::uint64_t(offset / 4) & ((std::uint64_t(1) << 48) - 1));
    e.w.Bit(85, absolute); e.w.Bit(86, noDec);
    e.w.Set(87, 3, PT);
    return Finish(e, d, pred, negate);
}

// Special registers by index (bits 72-79).
enum class SR : unsigned {
    LaneId = 0x00, InvocationId = 0x11, YDirection = 0x12, ThreadKill = 0x13, InvocationInfo = 0x1d, TidX = 0x21,
    TidY = 0x22, TidZ = 0x23, CtaIdX = 0x25, CtaIdY = 0x26, CtaIdZ = 0x27, ClockLo = 0x50, Zero = 0xff
};

inline Word S2R(unsigned dst, SR sr, const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.w.Set(0, 12, 0x919);
    e.w.Set(16, 8, dst); e.w.Set(72, 8, unsigned(sr));
    return Finish(e, d, pred, negate);
}

// CS2R dst, sr: a fixed-latency read; the game zeroes register pairs with CS2R Rn, SRZ (64-bit).
inline Word CS2R(unsigned dst, SR sr = SR::Zero, bool b64 = true, const Deps& d = {}, unsigned pred = PT,
                 bool negate = false) {
    Encoder e;
    e.w.Set(0, 12, 0x805);
    e.w.Set(16, 8, dst); e.w.Set(72, 8, unsigned(sr)); e.w.Bit(80, b64);
    return Finish(e, d, pred, negate);
}

// DEPBAR.LE SBn, count[, {others}]: wait until scoreboard `sb` counts at most `count` outstanding operations
// (bits 44-46 sb, 38-43 count, 47 .LE, 32-37 further scoreboards to wait on).
inline Word DepBar(unsigned sb, unsigned count, bool le = true, unsigned others = 0, const Deps& d = {},
                   unsigned pred = PT, bool negate = false) {
    if (sb > 7 || count > 63 || others > 63) throw std::runtime_error("ampere encode: DEPBAR");
    Encoder e;
    e.w.Set(0, 12, 0x91a);
    e.w.Set(32, 6, others); e.w.Set(38, 6, count); e.w.Set(44, 3, sb); e.w.Bit(47, le);
    return Finish(e, d, pred, negate);
}

// ---- Textures -----------------------------------------------------------------------------------------------------

enum class TexDim { D1 = 0, D2 = 1, D3 = 2, Cube = 3, Array1D = 4, Array2D = 5, ArrayCube = 7 };
enum class TexLod { Auto = 0, Zero = 1, Bias = 2, Lod = 3, Clamp = 4, BiasClamp = 5 };   // -, .LZ, .LB, .LL, .LC, .LB.LC
enum class TexQuery { Dimension = 0, TextureType = 1, SamplerPos = 2 };

// Where a texture instruction finds its handle: in the register of source b (bindless, bit 59 .B), in c[bank][4 *
// index] (NAK set_tex_cb_ref: index 40-53, bank 54-58), or in a uniform register (40-45) with an index (46-53), the
// form the game uses for combined texture and sampler handles (TEX.SCR ... UR4, 0x0).
struct TexHandle {
    enum class Kind { Reg, CBuf, UReg } kind = Kind::Reg;
    unsigned bank = 0, index = 0, ureg = 0;
    static TexHandle Bindless() { return {}; }
    static TexHandle C(unsigned bank, unsigned index) { TexHandle h; h.kind = Kind::CBuf; h.bank = bank; h.index = index; return h; }
    static TexHandle U(unsigned ureg, unsigned index = 0) { TexHandle h; h.kind = Kind::UReg; h.ureg = ureg; h.index = index; return h; }
};

// A texture instruction's registers and options. Roles as read from the game: dst0 bits 16-23, coordinates (a) 24-31,
// b 32-39 (the handle for a bindless handle, then extras such as lod or bias), dst1 64-71. nvdisasm prints dst1
// first. `split` is bit 60, the .SCR form the game uses except for cube lookups with an explicit lod.
struct TexRegs {
    unsigned dst0 = RZ, dst1 = RZ, a = RZ, b = RZ;
};
struct TexOpts {
    unsigned mask = 0xf;          // channel mask, bits 72-75
    TexLod lod = TexLod::Auto;    // bits 87-89 (TEX, TLD)
    bool dc = false;              // depth compare, bit 78 (TEX, TLD4)
    bool aoffi = false;           // immediate offsets, bit 76
    bool ndv = false;             // .NDV, bit 77 (TEX)
    bool ms = false;              // multisample, bit 78 (TLD)
    bool nodep = false;           // bit 90
    bool split = true;            // .SCR, bit 60
    unsigned comp = 0;            // TLD4 component, bits 87-88 (.R .G .B .A)
    bool ptp = false;             // TLD4 per-pixel offsets (.PTP, bits 76-77 = 2)
    unsigned fault = PT;          // predicate written on a sparse fault, bits 81-83
    unsigned evict = 1;           // eviction priority, bits 84-86 (1 normal, as the game writes)
};

inline Encoder TexBase(unsigned cbufOpcode, const TexRegs& r, const TexHandle& h) {
    Encoder e;
    switch (h.kind) {
    case TexHandle::Kind::Reg: e.w.Set(0, 12, cbufOpcode - 0x7ff); e.w.Bit(59, true); break;
    case TexHandle::Kind::CBuf:
        Encoder::Need(h.index < 0x4000 && h.bank < 32, "texture constant out of range");
        e.w.Set(0, 12, cbufOpcode); e.w.Set(40, 14, h.index); e.w.Set(54, 5, h.bank);
        break;
    case TexHandle::Kind::UReg:
        Encoder::Need(h.ureg < 64 && h.index < 256, "texture uniform handle out of range");
        e.w.Set(0, 12, cbufOpcode | 0x400); e.w.Set(40, 6, h.ureg); e.w.Set(46, 8, h.index); e.w.Bit(91, true);
        break;
    }
    e.w.Set(16, 8, r.dst0); e.w.Set(24, 8, r.a); e.w.Set(32, 8, r.b); e.w.Set(64, 8, r.dst1);
    return e;
}

inline Word Tex(const TexRegs& r, const TexHandle& h, TexDim dim, const TexOpts& o = {}, const Deps& d = {},
                unsigned pred = PT, bool negate = false) {
    Encoder e = TexBase(0xb60, r, h);
    e.w.Bit(60, o.split); e.w.Set(61, 3, unsigned(dim));
    e.w.Set(72, 4, o.mask); e.w.Bit(76, o.aoffi); e.w.Bit(77, o.ndv); e.w.Bit(78, o.dc);
    e.w.Set(81, 3, o.fault); e.w.Set(84, 3, o.evict); e.w.Set(87, 3, unsigned(o.lod)); e.w.Bit(90, o.nodep);
    return Finish(e, d, pred, negate);
}

// TLD: texel fetch; the lod mode is .LZ or .LL.
inline Word Tld(const TexRegs& r, const TexHandle& h, TexDim dim, const TexOpts& o = {}, const Deps& d = {},
                unsigned pred = PT, bool negate = false) {
    Encoder e = TexBase(0xb66, r, h);
    e.w.Bit(60, o.split); e.w.Set(61, 3, unsigned(dim));
    e.w.Set(72, 4, o.mask); e.w.Bit(76, o.aoffi); e.w.Bit(78, o.ms);
    e.w.Set(81, 3, o.fault); e.w.Set(84, 3, o.evict); e.w.Set(87, 3, unsigned(o.lod)); e.w.Bit(90, o.nodep);
    return Finish(e, d, pred, negate);
}

// TLD4: gather component `comp`.
inline Word Tld4(const TexRegs& r, const TexHandle& h, TexDim dim, const TexOpts& o = {}, const Deps& d = {},
                 unsigned pred = PT, bool negate = false) {
    Encoder e = TexBase(0xb63, r, h);
    e.w.Bit(60, o.split); e.w.Set(61, 3, unsigned(dim));
    e.w.Set(72, 4, o.mask); e.w.Set(76, 2, o.ptp ? 2 : o.aoffi ? 1 : 0); e.w.Bit(78, o.dc);
    e.w.Set(81, 3, o.fault); e.w.Set(84, 3, o.evict); e.w.Set(87, 2, o.comp); e.w.Bit(90, o.nodep);
    return Finish(e, d, pred, negate);
}

// TXQ: texture header or sampler query (bits 62-63). The game's TXQ leaves the fault and eviction fields 0.
inline Word Txq(const TexRegs& r, const TexHandle& h, TexQuery q, unsigned mask = 0xf, bool nodep = false,
                const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e = TexBase(0xb6f, r, h);
    e.w.Set(62, 2, unsigned(q)); e.w.Set(72, 4, mask); e.w.Bit(90, nodep);
    return Finish(e, d, pred, negate);
}

// ---- Uniform datapath ---------------------------------------------------------------------------------------------

// ULDC[.64] udst, c[bank][offset]; with `index` (a uniform register other than URZ), c[bank][index + offset]
// (opcode 0xabb, bit 91, as the game writes it).
inline Word Uldc(unsigned udst, unsigned bank, unsigned offset, bool b64 = false, unsigned index = URZ,
                 const Deps& d = {}, unsigned pred = UPT, bool negate = false) {
    if (offset >= 0x10000 || bank >= 32) throw std::runtime_error("ampere encode: ULDC constant out of range");
    Encoder e;
    e.w.Set(0, 12, index == URZ ? 0xab9 : 0xabb);
    e.w.Set(16, 6, udst);
    if (index != URZ) { e.w.Set(24, 6, index); e.w.Bit(91, true); }
    e.w.Set(38, 16, offset); e.w.Set(54, 5, bank);
    e.w.Set(73, 3, b64 ? 5 : 4);
    return Finish(e, d, pred, negate);
}

// ULOP3.LUT udst = lut(a, b, c); b may be an immediate.
inline Word ULop3(unsigned udst, Src a, Src b, Src c, unsigned lut, const Deps& d = {}, unsigned pred = UPT,
                  bool negate = false, unsigned updst = UPT) {
    Encoder e;
    e.UAlu(0x092, udst, &a, &b, &c);
    e.w.Set(87, 3, UPT); e.w.Bit(90, true);
    e.w.Set(72, 8, lut); e.w.Bit(80, false); e.w.Set(81, 3, updst);
    return Finish(e, d, pred, negate);
}

// UIADD3 udst = a + b + c, with uniform-predicate carries as IADD3's (no carries by default).
inline Word UIAdd3(unsigned udst, Src a, Src b, Src c, const Deps& d = {}, unsigned pred = UPT, bool negate = false,
                   unsigned updst = UPT, unsigned updst2 = UPT, bool x = false, unsigned cin = UPT, bool cinNot = true,
                   unsigned cin2 = UPT, bool cin2Not = true) {
    Encoder e;
    e.UAlu(0x090, udst, &a, &b, &c);
    e.w.Set(87, 3, cin); e.w.Bit(90, cinNot);
    e.w.Set(77, 3, cin2); e.w.Bit(80, cin2Not);
    e.w.Set(81, 3, updst); e.w.Set(84, 3, updst2);
    e.w.Bit(74, x);
    return Finish(e, d, pred, negate);
}

// USHF: SHF on uniform registers.
inline Word UShf(unsigned udst, Src lo, Src shift, Src hi, bool right, ShfType type = ShfType::U32, bool high = false,
                 bool wrap = false, const Deps& d = {}, unsigned pred = UPT, bool negate = false) {
    Encoder e;
    e.UAlu(0x099, udst, &lo, &shift, &hi);
    e.w.Set(73, 2, unsigned(type)); e.w.Bit(75, wrap); e.w.Bit(76, right); e.w.Bit(80, high);
    return Finish(e, d, pred, negate);
}

// UMOV udst, uniform register or immediate. Encoded like a non-uniform ALU op (NAK): opcode 0x082 with form 6 for a
// register (bit 91 set) or 4 for an immediate.
inline Word UMov(unsigned udst, Src s, const Deps& d = {}, unsigned pred = UPT, bool negate = false) {
    Encoder e;
    e.w.Set(16, 6, udst);
    if (s.kind == Src::Kind::UReg) { e.w.Set(32, 6, s.reg); e.w.Bit(91, true); e.w.Set(0, 12, 0xc82); }
    else if (s.kind == Src::Kind::Imm) { e.w.Set(32, 32, s.imm); e.w.Set(0, 12, 0x882); }
    else throw std::runtime_error("ampere encode: UMOV takes a uniform register or an immediate");
    return Finish(e, d, pred, negate);
}

// R2UR udst, src: a register (the same in every thread) into a uniform register.
inline Word R2UR(unsigned udst, unsigned src, const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.w.Set(0, 12, 0x3c2);
    e.w.Set(16, 6, udst); e.w.Set(24, 8, src); e.w.Set(81, 3, PT);
    return Finish(e, d, pred, negate);
}

inline Word S2UR(unsigned udst, SR sr, const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.w.Set(0, 12, 0x9c3);
    e.w.Set(16, 6, udst); e.w.Set(72, 8, unsigned(sr));
    return Finish(e, d, pred, negate);
}

// ---- Memory -------------------------------------------------------------------------------------------------------

enum class MemType { U8 = 0, S8 = 1, U16 = 2, S16 = 3, B32 = 4, B64 = 5, B128 = 6 };   // bits 73-75
enum class MemOrder { Weak = 0, Constant = 4, StrongCta = 5, StrongGpu = 7, StrongSys = 0xa };   // bits 77-80 (SM80+)
enum class AtomOp { Add = 0, Min = 1, Max = 2, Inc = 3, Dec = 4, And = 5, Or = 6, Xor = 7, Exch = 8 };
enum class AtomType { U32 = 0, S32 = 1, U64 = 2, F32 = 3, F16x2 = 4, S64 = 5, F64 = 6 };
enum class Stride { X1 = 0, X4 = 1, X8 = 2, X16 = 3 };   // shared-memory address scale, bits 78-79

inline void MemOffset(Encoder& e, std::int32_t offset) {
    if (offset < -(1 << 23) || offset >= (1 << 23)) throw std::runtime_error("ampere encode: memory offset");
    e.w.Set(40, 24, std::uint32_t(offset) & 0xffffff);
}

// LDC dst, c[bank][index + offset] with a register index (RZ for none).
inline Word Ldc(unsigned dst, unsigned bank, unsigned offset, unsigned index = RZ, MemType type = MemType::B32,
                const Deps& d = {}, unsigned pred = PT, bool negate = false, unsigned mode = 0) {
    if (offset >= 0x10000 || bank >= 32) throw std::runtime_error("ampere encode: LDC constant out of range");
    Encoder e;
    e.w.Set(0, 12, 0xb82);
    e.w.Set(16, 8, dst); e.w.Set(24, 8, index);
    e.w.Set(38, 16, offset); e.w.Set(54, 5, bank);
    e.w.Set(73, 3, unsigned(type)); e.w.Set(78, 2, mode);
    return Finish(e, d, pred, negate);
}

// LDL dst, [addr + offset]. The game writes 0x983 with bit 91 clear and the uniform field 0 (NAK sets bit 91 and URZ);
// `evict` 3 is .LU, the game's reload of a spilled value.
inline Word Ldl(unsigned dst, unsigned addr, std::int32_t offset, MemType type = MemType::B32, unsigned evict = 1,
                const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.w.Set(0, 12, 0x983);
    e.w.Set(16, 8, dst); e.w.Set(24, 8, addr); MemOffset(e, offset);
    e.w.Set(73, 3, unsigned(type)); e.w.Set(84, 3, evict);
    return Finish(e, d, pred, negate);
}

// STL [addr + offset], data (the game's opcode 0x387, without the uniform address).
inline Word Stl(unsigned addr, std::int32_t offset, unsigned data, MemType type = MemType::B32, const Deps& d = {},
                unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.w.Set(0, 12, 0x387);
    e.w.Set(24, 8, addr); e.w.Set(32, 8, data); MemOffset(e, offset);
    e.w.Set(73, 3, unsigned(type)); e.w.Set(84, 3, 1);
    return Finish(e, d, pred, negate);
}

// LDS dst, [addr.Xn + offset] (0x984 with bit 91 clear, as the game writes it).
inline Word Lds(unsigned dst, unsigned addr, std::int32_t offset, MemType type = MemType::B32, Stride stride = Stride::X1,
                const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.w.Set(0, 12, 0x984);
    e.w.Set(16, 8, dst); e.w.Set(24, 8, addr); MemOffset(e, offset);
    e.w.Set(73, 3, unsigned(type)); e.w.Set(78, 2, unsigned(stride));
    return Finish(e, d, pred, negate);
}

// STS [addr.Xn + offset], data (the game's 0x388).
inline Word Sts(unsigned addr, std::int32_t offset, unsigned data, MemType type = MemType::B32, Stride stride = Stride::X1,
                const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.w.Set(0, 12, 0x388);
    e.w.Set(24, 8, addr); e.w.Set(32, 8, data); MemOffset(e, offset);
    e.w.Set(73, 3, unsigned(type)); e.w.Set(78, 2, unsigned(stride));
    return Finish(e, d, pred, negate);
}

// LDG.E dst, [addr + offset]: addr a register (0x381) or a uniform register (0x981). The uniform form also adds
// register `reg` (bit 90: 64-bit; clear prints R.U32, as in [R34.U32+UR4]); with RZ there the game still sets bit 90.
inline Word Ldg(unsigned dst, Src addr, std::int32_t offset, MemType type = MemType::B32,
                MemOrder order = MemOrder::Constant, bool e64 = true, const Deps& d = {}, unsigned pred = PT,
                bool negate = false, unsigned reg = RZ, bool reg64 = true) {
    Encoder e;
    if (addr.kind == Src::Kind::UReg) {
        e.w.Set(0, 12, 0x981);
        e.w.Set(24, 8, reg); e.w.Set(32, 6, addr.reg);
        e.w.Bit(72, e64); e.w.Bit(90, reg64); e.w.Bit(91, true);
    } else {
        Encoder::Need(addr.kind == Src::Kind::Reg, "LDG address");
        e.w.Set(0, 12, 0x381);
        e.w.Set(24, 8, addr.reg); e.w.Bit(72, e64);
    }
    e.w.Set(16, 8, dst); MemOffset(e, offset);
    e.w.Set(73, 3, unsigned(type)); e.w.Set(77, 4, unsigned(order)); e.w.Set(81, 3, PT); e.w.Set(84, 3, 1);
    return Finish(e, d, pred, negate);
}

// STG.E [addr + offset], data (register address, the game's 0x386).
inline Word Stg(unsigned addr, std::int32_t offset, unsigned data, MemType type = MemType::B32,
                MemOrder order = MemOrder::StrongCta, bool e64 = true, const Deps& d = {}, unsigned pred = PT,
                bool negate = false) {
    Encoder e;
    e.w.Set(0, 12, 0x386);
    e.w.Set(24, 8, addr); e.w.Set(32, 8, data); MemOffset(e, offset);
    e.w.Bit(72, e64); e.w.Set(73, 3, unsigned(type)); e.w.Set(77, 4, unsigned(order)); e.w.Set(84, 3, 1);
    return Finish(e, d, pred, negate);
}

// ATOMS.<op> dst, [addr.Xn + offset], data (shared memory, the game's 0x38c).
inline Word Atoms(unsigned dst, unsigned addr, std::int32_t offset, unsigned data, AtomOp op,
                  AtomType type = AtomType::U32, Stride stride = Stride::X1, const Deps& d = {}, unsigned pred = PT,
                  bool negate = false) {
    Encoder e;
    e.w.Set(0, 12, 0x38c);
    e.w.Set(16, 8, dst); e.w.Set(24, 8, addr); e.w.Set(32, 8, data); MemOffset(e, offset);
    e.w.Set(73, 3, unsigned(type)); e.w.Set(78, 2, unsigned(stride)); e.w.Set(87, 4, unsigned(op));
    return Finish(e, d, pred, negate);
}

// ---- Attribute memory and geometry output -------------------------------------------------------------------------
// Field positions read with nvdisasm from the game's instructions and single-bit changes to them.

inline void AttrAccess(Encoder& e, unsigned addr, unsigned comps, bool patch) {
    Encoder::Need(addr % 4 == 0 && addr < 1024, "attribute address");
    Encoder::Need(comps >= 1 && comps <= 4, "attribute components");
    e.w.Set(24, 8, RZ);   // no offset register (a[Rn+addr] is the .PHYS form, not taken)
    e.w.Set(40, 10, addr);
    e.w.Set(74, 2, comps - 1);
    e.w.Bit(76, patch);
}

// ALD[.64/.96/.128] dst, a[addr][, vtx]: `comps` consecutive attribute words into dst.. . `vtx` is the vertex handle a
// geometry program gets from ISBERD (RZ in vertex programs, nvdisasm then leaves it out). `output` (.O) reads the
// program's own outputs, `patch` (.P) a patch attribute (with vtx RZ).
inline Word Ald(unsigned dst, unsigned addr, unsigned comps = 1, unsigned vtx = RZ, bool output = false, bool patch = false,
                const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.w.Set(0, 12, 0x321);
    e.w.Set(16, 8, dst); e.w.Set(32, 8, vtx);
    AttrAccess(e, addr, comps, patch);
    e.w.Bit(79, output);
    return Finish(e, d, pred, negate);
}

// AST[.64/.96/.128] a[addr], data[, vtx]: `comps` registers from data to the outputs. A vertex program stores with vtx
// RZ; a geometry program passes the handle OUT returned (AST a[0x70], R80, R0).
inline Word Ast(unsigned addr, unsigned data, unsigned comps = 1, unsigned vtx = RZ, bool patch = false, const Deps& d = {},
                unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.w.Set(0, 12, 0x322);
    e.w.Set(32, 8, data); e.w.Set(64, 8, vtx);
    AttrAccess(e, addr, comps, patch);
    return Finish(e, d, pred, negate);
}

enum class OutType { Final = 0, Emit = 1, Cut = 2, EmitThenCut = 3 };   // bits 78-79

// OUT.EMIT dst, handle, stream (and .CUT, .EMIT_THEN_CUT): emits or ends the geometry program's current primitive and
// returns the next vertex's handle in dst. OUT.FINAL handle (dst RZ) closes the output. The stream is a register
// (RZ for stream 0, as the game writes it) or an immediate.
inline Word Out(OutType type, unsigned dst, unsigned handle, Src stream = Src::Zero(), const Deps& d = {}, unsigned pred = PT,
                bool negate = false) {
    Encoder::Need(stream.kind == Src::Kind::Reg || stream.kind == Src::Kind::Imm, "OUT stream");
    Encoder e;
    const Src h = Src::R(handle);
    e.Alu(0x124, dst, &h, &stream, nullptr);
    e.w.Set(78, 2, unsigned(type));
    return Finish(e, d, pred, negate);
}

// ISBERD dst, [addr + offset]: reads the internal stage buffer, which is how a geometry program turns an input vertex
// index (in addr) into the vertex handle ALD takes. `output` (.O) and `patch` (.PATCH) pick the other buffers.
inline Word Isberd(unsigned dst, unsigned addr, unsigned offset = 0, bool output = false, bool patch = false,
                   const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder::Need(offset < 0x10000, "ISBERD offset");
    Encoder e;
    e.w.Set(0, 12, 0x923);
    e.w.Set(16, 8, dst); e.w.Set(24, 8, addr); e.w.Set(40, 16, offset);
    e.w.Bit(76, patch); e.w.Bit(79, output);
    return Finish(e, d, pred, negate);
}

// ---- Half-precision ALU -------------------------------------------------------------------------------------------

// Which halves of a register an fp16 instruction reads (.H0_H0 ...; H1H0 prints nothing, F32 reads one fp32 value).
enum class HSwz { H1H0 = 0, F32 = 1, H0H0 = 2, H1H1 = 3 };

// HADD2[.F32] dst = a + b on half pairs; `f32` (bit 78) writes one fp32 result, as the game converts halves with
// HADD2.F32 R, -RZ, R.H0_H0. Swizzles: a bits 74-75, b bits 60-61 (a register or constant b). An immediate b holds
// two halves (bits 48-63 the first nvdisasm prints, 32-47 the second). b other than a register takes src2's slot,
// as FADD's.
inline Word HAdd2(unsigned dst, Src a, Src b, HSwz aSwz = HSwz::H1H0, HSwz bSwz = HSwz::H1H0, bool f32 = false,
                  bool sat = false, bool ftz = false, const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    if (b.kind == Src::Kind::Reg) e.Alu(0x030, dst, &a, &b, nullptr);
    else e.Alu(0x030, dst, &a, nullptr, &b);
    e.w.Set(74, 2, unsigned(aSwz));
    if (b.kind != Src::Kind::Imm) e.w.Set(60, 2, unsigned(bSwz));
    e.w.Bit(77, sat); e.w.Bit(78, f32); e.w.Bit(80, ftz);
    return Finish(e, d, pred, negate);
}

// HSETP2 p = (a cmp b) op accum on half pairs, pdst2 the second half's result (PT: none). `hAnd` (.H_AND) ands the
// two halves' compares into pdst. Operand forms and swizzles as HADD2's; op in bits 69-70, compare 76-79.
inline Word HSetP2(unsigned pdst, Src a, Src b, Cmp cmp, HSwz aSwz = HSwz::H1H0, HSwz bSwz = HSwz::H1H0,
                   PredOp op = PredOp::And, unsigned accum = PT, bool accumNot = false, bool ftz = false, bool hAnd = false,
                   unsigned pdst2 = PT, const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    if (b.kind == Src::Kind::Reg) e.Alu(0x034, 0, &a, &b, nullptr);
    else e.Alu(0x034, 0, &a, nullptr, &b);
    e.w.Set(74, 2, unsigned(aSwz));
    if (b.kind != Src::Kind::Imm) e.w.Set(60, 2, unsigned(bSwz));
    e.w.Set(69, 2, unsigned(op)); e.w.Bit(71, hAnd);
    e.w.Set(76, 4, unsigned(cmp)); e.w.Bit(80, ftz);
    e.w.Set(81, 3, pdst); e.w.Set(84, 3, pdst2);
    e.w.Set(87, 3, accum); e.w.Bit(90, accumNot);
    return Finish(e, d, pred, negate);
}

// ---- More uniform datapath ----------------------------------------------------------------------------------------

// UISETP up = (a cmp b) op accum on uniform registers (b a uniform register or an immediate), 32-bit, fields as ISETP's.
inline Word UISetP(unsigned updst, Src a, Src b, ICmp cmp, bool isSigned, PredOp op = PredOp::And, unsigned accum = UPT,
                   bool accumNot = false, const Deps& d = {}, unsigned pred = UPT, bool negate = false) {
    Encoder e;
    e.UAlu(0x08c, 0, &a, &b, nullptr);
    e.w.Set(68, 3, UPT); e.w.Bit(71, false);
    e.w.Bit(72, false); e.w.Bit(73, isSigned);
    e.w.Set(74, 2, unsigned(op)); e.w.Set(76, 3, unsigned(cmp));
    e.w.Set(81, 3, updst); e.w.Set(84, 3, UPT);
    e.w.Set(87, 3, accum); e.w.Bit(90, accumNot);
    return Finish(e, d, pred, negate);
}

// USEL udst = ucond ? a : b (b a uniform register or an immediate).
inline Word USel(unsigned udst, Src a, Src b, unsigned ucond, bool condNot = false, const Deps& d = {}, unsigned pred = UPT,
                 bool negate = false) {
    Encoder e;
    e.UAlu(0x087, udst, &a, &b, nullptr);
    e.w.Set(87, 3, ucond); e.w.Bit(90, condNot);
    return Finish(e, d, pred, negate);
}

enum class VoteOp { All = 0, Any = 1, Eq = 2 };   // bits 72-73

// VOTE.<op> dst, pdst, src: dst gets the ballot of predicate src across the warp, pdst the vote.
inline Word Vote(VoteOp op, unsigned dst, unsigned src = PT, bool srcNot = false, unsigned pdst = PT, const Deps& d = {},
                 unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.w.Set(0, 12, 0x806);
    e.w.Set(16, 8, dst); e.w.Set(72, 2, unsigned(op));
    e.w.Set(81, 3, pdst); e.w.Set(87, 3, src); e.w.Bit(90, srcNot);
    return Finish(e, d, pred, negate);
}

// VOTEU.<op> udst, updst, src: the uniform form; udst URZ prints as "VOTEU.ANY UP0, P1".
inline Word VoteU(VoteOp op, unsigned udst, unsigned src = PT, bool srcNot = false, unsigned updst = UPT, const Deps& d = {},
                  unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.w.Set(0, 12, 0x886);
    e.w.Set(16, 6, udst); e.w.Set(72, 2, unsigned(op));
    e.w.Set(81, 3, updst); e.w.Set(87, 3, src); e.w.Bit(90, srcNot);
    return Finish(e, d, pred, negate);
}

// ---- Barriers and the exit address --------------------------------------------------------------------------------

// BAR.SYNC id[, threads] (or BAR.ARV with `arrive`): barrier `id` (bits 54-57) for `threads` threads (bits 42-53, 0 all
// the block's threads; BAR.ARV always prints it). `defer` is .DEFER_BLOCKING (bit 80).
inline Word Bar(unsigned id = 0, unsigned threads = 0, bool arrive = false, bool defer = false, const Deps& d = {},
                unsigned pred = PT, bool negate = false) {
    Encoder::Need(id < 16 && threads < 0x1000, "BAR");
    Encoder e;
    e.w.Set(0, 12, 0xb1d);
    e.w.Set(42, 12, threads); e.w.Set(54, 4, id);
    e.w.Set(77, 3, arrive ? 1 : 0); e.w.Bit(80, defer);
    return Finish(e, d, pred, negate);
}

// LEPC dst: the program counter into the pair dst. The game's geometry programs start LEPC R0; MOV R3, <offset>;
// IADD3/IADD3.X; BMOV.64 ATEXIT_PC, R0, where <offset> is the epilogue's offset from the program start (LEPC is at
// offset 0), so LEPC yields its own address.
inline Word Lepc(unsigned dst, const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.w.Set(0, 12, 0x34e);
    e.w.Set(16, 8, dst);
    return Finish(e, d, pred, negate);
}

// BMOV.64 ATEXIT_PC, src: the register pair src (0x357), or a 49-bit immediate (0x957: bits 32-63, then 64-80; an
// address, so a multiple of 4: nvdisasm ignores bits 32-33).
inline Word BMovAtExitPC(unsigned src, const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e;
    e.w.Set(0, 12, 0x357);
    e.w.Set(32, 8, src);
    return Finish(e, d, pred, negate);
}
inline Word BMovAtExitPCImm(std::uint64_t value, const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder::Need(value < (std::uint64_t(1) << 49) && value % 4 == 0, "ATEXIT_PC immediate");
    Encoder e;
    e.w.Set(0, 12, 0x957);
    e.w.Set(32, 32, value & 0xffffffffu); e.w.Set(64, 17, value >> 32);
    return Finish(e, d, pred, negate);
}

// ---- Surfaces -----------------------------------------------------------------------------------------------------

enum class SurfDim { D1 = 0, Buffer1D = 1, Array1D = 2, D2 = 3, Array2D = 4, D3 = 5 };   // bits 61-63
enum class SurfClamp { Ign = 0, None = 1, Trap = 2 };                                       // bits 59-60

// Where a surface instruction finds its handle: a register (bits 64-71), c[bank][index] (index 40-53, bank 54-58), or a
// uniform register (40-45) with an index (46-53, bit 91). nvdisasm prints the index as written.
struct SurfHandle {
    enum class Kind { Reg, CBuf, UReg } kind = Kind::Reg;
    unsigned reg = RZ, bank = 0, index = 0, ureg = 0;
    static SurfHandle R(unsigned reg) { SurfHandle h; h.reg = reg; return h; }
    static SurfHandle C(unsigned bank, unsigned index) { SurfHandle h; h.kind = Kind::CBuf; h.bank = bank; h.index = index; return h; }
    static SurfHandle U(unsigned ureg, unsigned index = 0) { SurfHandle h; h.kind = Kind::UReg; h.ureg = ureg; h.index = index; return h; }
};

// The parts every surface instruction shares: the opcode for the handle's kind (register, constant; the uniform form is
// the constant one | 0x400), the address registers (24-31), dimension, clamp, memory order (77-80) and eviction
// priority (84-86).
inline Encoder SurfBase(unsigned regOpcode, unsigned cbufOpcode, const SurfHandle& h, unsigned addr, SurfDim dim,
                        SurfClamp clamp, MemOrder order, unsigned evict) {
    Encoder e;
    switch (h.kind) {
    case SurfHandle::Kind::Reg: e.w.Set(0, 12, regOpcode); e.w.Set(64, 8, h.reg); break;
    case SurfHandle::Kind::CBuf:
        Encoder::Need(h.index < 0x4000 && h.bank < 32, "surface constant out of range");
        e.w.Set(0, 12, cbufOpcode); e.w.Set(40, 14, h.index); e.w.Set(54, 5, h.bank);
        break;
    case SurfHandle::Kind::UReg:
        Encoder::Need(h.ureg < 64 && h.index < 256, "surface uniform handle out of range");
        e.w.Set(0, 12, cbufOpcode | 0x400); e.w.Set(40, 6, h.ureg); e.w.Set(46, 8, h.index); e.w.Bit(91, true);
        break;
    }
    e.w.Set(24, 8, addr);
    e.w.Set(59, 2, unsigned(clamp)); e.w.Set(61, 3, unsigned(dim));
    e.w.Set(77, 4, unsigned(order)); e.w.Set(84, 3, evict);
    return e;
}

// SULD.P dst, [addr], handle: formatted load of the channels in `mask` (bits 72-75: 0x1 .R, 0x3 .RG, 0xf all, which
// prints nothing). `fault` takes the sparse-fault predicate (bits 81-83).
inline Word SuLd(unsigned dst, unsigned addr, const SurfHandle& h, SurfDim dim, unsigned mask = 0xf,
                 SurfClamp clamp = SurfClamp::Ign, MemOrder order = MemOrder::Weak, unsigned evict = 1, unsigned fault = PT,
                 const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder e = SurfBase(0x998, 0xb97, h, addr, dim, clamp, order, evict);
    e.w.Set(16, 8, dst); e.w.Set(72, 4, mask); e.w.Set(81, 3, fault);
    return Finish(e, d, pred, negate);
}

// SUST.P [addr], data, handle: formatted store of the channels in `mask`.
inline Word SuSt(unsigned addr, unsigned data, const SurfHandle& h, SurfDim dim, unsigned mask = 0xf,
                 SurfClamp clamp = SurfClamp::Ign, MemOrder order = MemOrder::Weak, unsigned evict = 1, const Deps& d = {},
                 unsigned pred = PT, bool negate = false) {
    Encoder e = SurfBase(0x99c, 0xb9b, h, addr, dim, clamp, order, evict);
    e.w.Set(32, 8, data); e.w.Set(72, 4, mask);
    return Finish(e, d, pred, negate);
}

// SUATOM.D.<op> dst, [addr], data, handle: atomic on a surface; type in bits 73-75 (AtomType), op 87-90. `ba` is .BA
// (bit 72). The handle is a register or a constant (nvdisasm refuses the uniform form 0x793).
inline Word SuAtom(unsigned dst, unsigned addr, unsigned data, const SurfHandle& h, SurfDim dim, AtomOp op,
                   AtomType type = AtomType::U32, MemOrder order = MemOrder::StrongGpu, SurfClamp clamp = SurfClamp::Ign,
                   unsigned evict = 1, unsigned fault = PT, bool ba = false, const Deps& d = {}, unsigned pred = PT,
                   bool negate = false) {
    Encoder::Need(h.kind != SurfHandle::Kind::UReg, "SUATOM takes no uniform handle");
    Encoder e = SurfBase(0x394, 0x593, h, addr, dim, clamp, order, evict);
    e.w.Set(16, 8, dst); e.w.Set(32, 8, data);
    e.w.Bit(72, ba); e.w.Set(73, 3, unsigned(type)); e.w.Set(81, 3, fault); e.w.Set(87, 4, unsigned(op));
    return Finish(e, d, pred, negate);
}

// SURED.D.<op> [addr], data, handle: a surface reduction (an atomic without a result); op in bits 87-89. Register or
// constant handle, as SUATOM.
inline Word SuRed(unsigned addr, unsigned data, const SurfHandle& h, SurfDim dim, AtomOp op, AtomType type = AtomType::U32,
                  MemOrder order = MemOrder::StrongGpu, SurfClamp clamp = SurfClamp::Ign, unsigned evict = 1, bool ba = false,
                  const Deps& d = {}, unsigned pred = PT, bool negate = false) {
    Encoder::Need(unsigned(op) < 8 && h.kind != SurfHandle::Kind::UReg, "SURED operation or handle");
    Encoder e = SurfBase(0x3a0, 0x59f, h, addr, dim, clamp, order, evict);
    e.w.Set(32, 8, data);
    e.w.Bit(72, ba); e.w.Set(73, 3, unsigned(type)); e.w.Set(87, 3, unsigned(op));
    return Finish(e, d, pred, negate);
}

}  // namespace nvasm::ampere

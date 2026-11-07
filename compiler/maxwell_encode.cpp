// Maxwell backend: instruction encoding (MAXWELL_BACKEND.md).
//
// Field positions follow the Maxwell SM 5.3 encoding as the NVN-compiled corpus
// uses it (each form below was checked against real words; see the reference
// notes in MAXWELL_BACKEND.md). An operand B picks the register, constant-buffer
// or 20-bit immediate form of the instruction.
#include "maxwell_mir.h"
#include "nvasm.h"

#include <cstdio>
#include <stdexcept>

namespace nvasm::maxwell {

namespace {

uint64_t field(uint64_t v, int lo, int width) {
    const uint64_t mask = width == 64 ? ~0ull : ((1ull << width) - 1);
    return (v & mask) << lo;
}

struct Enc {
    const Allocation& a;
    uint64_t w = 0;
    void set(uint64_t v, int lo, int width) { w |= field(v, lo, width); }
    uint32_t reg(int32_t v) const {
        if (v == kRZ || v < 0) return 255;
        const int32_t r = a.reg.at(size_t(v));
        if (r < 0) throw std::logic_error("maxwell: unallocated register");
        return uint32_t(r);
    }
    uint32_t reg(const Opnd& o) const { return o.k == Opnd::R ? reg(o.v) : 255; }
    uint32_t pred(int32_t p) const {
        if (p == kPT || p < 0) return 7;
        return uint32_t(a.pred.at(size_t(p)));
    }
    void guard(const Inst& i) {
        set(pred(i.guard), 16, 3);
        set(i.guard_neg ? 1 : 0, 19, 1);
    }
    // Operand B at bits 20.. in its register, constant-buffer or imm20 form. `top` holds
    // the three opcode variants (bits 63..48): register, constant buffer, immediate.
    void operand_b(const Opnd& b, uint16_t reg_op, uint16_t cb_op, uint16_t imm_op, bool float_imm) {
        switch (b.k) {
        case Opnd::R:
        case Opnd::None:
            w |= uint64_t(reg_op) << 48;
            set(b.k == Opnd::R ? reg(b.v) : 255, 20, 8);
            break;
        case Opnd::Cb:
            if (b.off % 4 || b.off > 0xfffc || b.bank > 17) throw Error(0, "maxwell: constant buffer operand out of range");
            w |= uint64_t(cb_op) << 48;
            set(b.off / 4, 20, 14);
            set(b.bank, 34, 5);
            break;
        case Opnd::Imm: {
            const uint32_t v = uint32_t(b.v);
            uint32_t f19, sign;
            if (float_imm) {
                if (v & 0xfff) throw Error(0, "maxwell: float immediate needs its low 12 bits zero");
                f19 = (v >> 12) & 0x7ffff;
                sign = v >> 31;
            } else {
                const int32_t s = int32_t(v);
                if (s < -(1 << 19) || s >= (1 << 19)) throw Error(0, "maxwell: integer immediate out of 20 bits");
                f19 = uint32_t(s) & 0x7ffff;
                sign = (uint32_t(s) >> 19) & 1;
            }
            w |= uint64_t(imm_op) << 48;
            set(f19, 20, 19);
            set(sign, 56, 1);
            break;
        }
        }
    }
};

bool fits_imm20_float(uint32_t v) { return (v & 0xfff) == 0; }

} // namespace

uint64_t encode(const Inst& i, const Allocation& al, int32_t pc, const std::vector<int32_t>& label_pc) {
    Enc e{al};
    auto rd = [&]() { e.set(e.reg(i.d), 0, 8); };
    auto ra = [&]() { e.set(e.reg(i.a), 8, 8); };
    auto rc = [&]() { e.set(e.reg(i.c), 39, 8); };
    auto branch = [&](uint64_t base) {
        e.w = base;
        int32_t target = label_pc.at(size_t(i.target));
        if (target % 32 == 8) target -= 8;   // a bundle's first slot is encoded as the bundle
        const int32_t offset = target - (pc + 8);
        e.set(uint64_t(uint32_t(offset)), 20, 24);
    };
    const bool ftz = true;   // as NVIDIA's compiler: denormals flushed (FMZ/FTZ = 1)
    switch (i.op) {
    case Op::MOV:
        e.operand_b(i.b, 0x5c98, 0x4c98, 0x3898, false);
        rd();
        e.set(0xf, 39, 4);
        break;
    case Op::MOV32I:
        e.w = uint64_t(0x0100) << 48;
        rd();
        e.set(uint32_t(i.a.v), 20, 32);
        e.set(0xf, 12, 4);
        break;
    case Op::SEL:
        e.operand_b(i.b, 0x5ca0, 0x4ca0, 0x38a0, false);
        rd();
        ra();
        e.set(e.pred(i.pa), 39, 3);
        e.set(i.pa_neg, 42, 1);
        break;
    case Op::FADD: {
        const bool imm = i.b.k == Opnd::Imm;
        if (imm && !fits_imm20_float(uint32_t(i.b.v))) {   // FADD32I
            e.w = uint64_t(0x0800) << 48;
            rd();
            ra();
            e.set(uint32_t(i.b.v), 20, 32);
            e.set(i.b.neg, 53, 1);
            e.set(i.a.abs, 54, 1);
            e.set(ftz, 55, 1);
            e.set(i.a.neg, 56, 1);
            e.set(i.b.abs, 57, 1);
            if (i.flags & kSat) throw Error(0, "maxwell: FADD32I has no saturate");
            break;
        }
        e.operand_b(i.b, 0x5c58, 0x4c58, 0x3858, true);
        rd();
        ra();
        e.set(0, 39, 2);   // RN
        e.set(ftz, 44, 1);
        e.set(i.b.neg, 45, 1);
        e.set(i.a.abs, 46, 1);
        e.set(i.a.neg, 48, 1);
        e.set(i.b.abs, 49, 1);
        e.set((i.flags & kSat) != 0, 50, 1);
        break;
    }
    case Op::FMUL: {
        const bool negate = i.a.neg != i.b.neg;
        if (i.a.abs || i.b.abs) throw Error(0, "maxwell: FMUL has no absolute value");
        if (i.b.k == Opnd::Imm && !fits_imm20_float(uint32_t(i.b.v))) {   // FMUL32I
            e.w = uint64_t(0x1e00) << 48;
            rd();
            ra();
            e.set(uint32_t(i.b.v) ^ (negate ? 0x80000000u : 0), 20, 32);
            e.set(ftz, 53, 2);
            e.set((i.flags & kSat) != 0, 55, 1);
            break;
        }
        e.operand_b(i.b, 0x5c68, 0x4c68, 0x3868, true);
        rd();
        ra();
        e.set(0, 39, 2);
        e.set(ftz, 44, 2);
        e.set(negate, 48, 1);
        e.set((i.flags & kSat) != 0, 50, 1);
        break;
    }
    case Op::FFMA: {
        const bool negate = i.a.neg != i.b.neg;
        if (i.a.abs || i.b.abs || i.c.abs) throw Error(0, "maxwell: FFMA has no absolute value");
        if (i.c.k == Opnd::Cb) {   // rc form: B = Rc, C = constant buffer
            if (i.b.k != Opnd::R) throw Error(0, "maxwell: FFMA with two constant operands");
            e.w = uint64_t(0x5180) << 48;
            e.set(i.c.off / 4, 20, 14);
            e.set(i.c.bank, 34, 5);
            e.set(e.reg(i.b), 39, 8);
        } else {
            e.operand_b(i.b, 0x5980, 0x4980, 0x3280, true);
            rc();
        }
        rd();
        ra();
        e.set(negate, 48, 1);
        e.set(i.c.neg, 49, 1);
        e.set((i.flags & kSat) != 0, 50, 1);
        e.set(0, 51, 2);
        e.set(ftz, 53, 2);
        break;
    }
    case Op::FMNMX:
        e.operand_b(i.b, 0x5c60, 0x4c60, 0x3860, true);
        rd();
        ra();
        e.set(7, 39, 3);                               // PT: minimum; !PT: maximum
        e.set((i.flags & kMax) != 0, 42, 1);
        e.set(ftz, 44, 1);
        e.set(i.b.neg, 45, 1);
        e.set(i.a.abs, 46, 1);
        e.set(i.a.neg, 48, 1);
        e.set(i.b.abs, 49, 1);
        break;
    case Op::FSET:
        e.operand_b(i.b, 0x5800, 0x4800, 0x3000, true);
        rd();
        ra();
        e.set(7, 39, 3);   // AND PT
        e.set(i.a.neg, 43, 1);
        e.set(i.b.abs, 44, 1);
        e.set(0, 45, 2);
        e.set(i.cmp, 48, 4);
        e.set((i.flags & kBF) != 0, 52, 1);
        e.set(i.b.neg, 53, 1);
        e.set(i.a.abs, 54, 1);
        e.set(ftz, 55, 1);
        break;
    case Op::FSETP:
        e.operand_b(i.b, 0x5bb0, 0x4bb0, 0x36b0, true);
        e.set(7, 0, 3);
        e.set(e.pred(i.pd), 3, 3);
        e.set(i.b.neg, 6, 1);
        e.set(i.a.abs, 7, 1);
        ra();
        e.set(e.pred(i.pa), 39, 3);
        e.set(i.pa_neg, 42, 1);
        e.set(i.a.neg, 43, 1);
        e.set(i.b.abs, 44, 1);
        e.set(0, 45, 2);
        e.set(ftz, 47, 1);
        e.set(i.cmp, 48, 4);
        break;
    case Op::MUFU:
        e.w = uint64_t(0x5080) << 48;
        rd();
        ra();
        e.set(i.sub, 20, 4);
        e.set(i.a.abs, 46, 1);
        e.set(i.a.neg, 48, 1);
        e.set((i.flags & kSat) != 0, 50, 1);
        break;
    case Op::RRO:
        e.operand_b(i.b, 0x5c90, 0x4c90, 0x3890, true);
        rd();
        e.set(i.sub, 39, 1);   // 0 SINCOS, 1 EX2
        e.set(i.b.neg, 45, 1);
        e.set(i.b.abs, 49, 1);
        break;
    case Op::F2F:
        e.operand_b(i.b, 0x5ca8, 0x4ca8, 0x38a8, true);
        rd();
        if (i.flags & kHalf) {             // F16 -> F32, one half of the source
            e.set(2, 8, 2);
            e.set(1, 10, 2);
            e.set((i.flags & kHigh) != 0, 41, 1);
        } else if (i.flags & kSignedDst) { // F32 -> F16 (packHalf2x16), rounding to nearest even
            e.set(1, 8, 2);
            e.set(2, 10, 2);
            e.set(i.sub, 39, 2);
        } else {                           // F32 -> F32 rounded to an integral value
            e.set(2, 8, 2);
            e.set(2, 10, 2);
            static const uint8_t round[] = {8, 9, 10, 11};   // Rnd RN, RM, RP, RZ
            e.set(round[i.sub & 3], 39, 4);
        }
        e.set(ftz, 44, 1);
        e.set(i.b.neg, 45, 1);
        e.set(i.b.abs, 49, 1);
        e.set((i.flags & kSat) != 0, 50, 1);
        break;
    case Op::F2I:
        e.operand_b(i.b, 0x5cb0, 0x4cb0, 0x38b0, true);
        rd();
        e.set((i.flags & kHalf) ? 1 : 2, 8, 2);    // I16 or I32
        e.set(2, 10, 2);   // F32
        e.set((i.flags & kSignedDst) != 0, 12, 1);
        e.set(i.sub == uint8_t(Rnd::RZ) ? 3 : i.sub == uint8_t(Rnd::RM) ? 1 : i.sub == uint8_t(Rnd::RP) ? 2 : 0, 39, 2);
        e.set(ftz, 44, 1);
        e.set(i.b.abs, 45, 1);
        e.set(i.b.neg, 49, 1);
        break;
    case Op::I2F:
        e.operand_b(i.b, 0x5cb8, 0x4cb8, 0x38b8, false);
        rd();
        e.set(2, 8, 2);    // F32
        e.set(2, 10, 2);   // 32-bit integer
        e.set((i.flags & kSigned) != 0, 13, 1);
        e.set(i.sub & 3, 39, 2);
        break;
    case Op::IADD:
        e.operand_b(i.b, 0x5c10, 0x4c10, 0x3810, false);
        rd();
        ra();
        e.set((i.flags & kX) != 0, 43, 1);
        e.set((i.flags & kCC) != 0, 47, 1);
        e.set(i.b.neg, 48, 1);
        e.set(i.a.neg, 49, 1);
        break;
    case Op::ISCADD:
        e.operand_b(i.b, 0x5c18, 0x4c18, 0x3818, false);
        rd();
        ra();
        e.set(i.aux, 39, 5);
        e.set((i.flags & kCC) != 0, 47, 1);
        e.set(i.b.neg, 48, 1);
        e.set(i.a.neg, 49, 1);
        break;
    case Op::LDG:
    case Op::STG:
        if (i.a.k != Opnd::R || i.a.n != 2) throw Error(0, "maxwell: global memory needs a 64-bit address");
        e.w = uint64_t(i.op == Op::LDG ? 0xeed0 : 0xeed8) << 48;
        e.set(i.op == Op::LDG ? e.reg(i.d) : e.reg(i.b), 0, 8);
        ra();
        e.set(1, 45, 1);   // .E: the address is Ra:Ra+1
        e.set(4, 48, 3);   // 32 bits
        break;
    case Op::PSETP:     // Pd = (Pb op Pa) AND PT
        e.w = uint64_t(0x5090) << 48;
        e.set(7, 0, 3);
        e.set(e.pred(i.pd), 3, 3);
        e.set(e.pred(i.pb), 12, 3);
        e.set(i.pb_neg, 15, 1);
        e.set(i.sub, 24, 2);
        e.set(e.pred(i.pa), 29, 3);
        e.set(i.pa_neg, 32, 1);
        e.set(7, 39, 3);   // C = PT
        e.set(0, 45, 2);   // AND
        break;
    case Op::S2R:
        e.w = uint64_t(0xf0c8) << 48;
        rd();
        e.set(i.aux, 20, 8);
        break;
    case Op::LDS:
    case Op::STS:
        e.w = uint64_t(i.op == Op::LDS ? 0xef48 : 0xef58) << 48;
        e.set(i.op == Op::LDS ? e.reg(i.d) : e.reg(i.b), 0, 8);
        ra();
        e.set(i.aux, 20, 24);
        if (i.op == Op::LDS) e.set(1, 44, 1);   // as NVIDIA's compiler emits it
        e.set(4, 48, 3);
        break;
    case Op::BAR: e.w = 0xF0A81B8000070000ull; break;   // BAR.SYNC 0
    case Op::MEMBAR:
        e.w = uint64_t(0xef98) << 48;
        e.set(i.aux, 8, 2);   // 0 CTA, 1 GL
        break;
    case Op::ATOMS:     // shared memory: Rd = old, [Ra + offset], Rb
        e.w = uint64_t(0xec00) << 48;
        rd();
        ra();
        e.set(e.reg(i.b), 20, 8);
        e.set((i.flags & kSigned) ? 1 : 0, 28, 2);
        e.set(i.aux / 4, 30, 22);
        e.set(i.sub, 52, 4);
        break;
    case Op::ATOM:      // global memory, 64-bit address Ra:Ra+1
        e.w = uint64_t(0xed00) << 48;
        rd();
        ra();
        e.set(e.reg(i.b), 20, 8);
        e.set(1, 48, 1);   // .E
        e.set((i.flags & kSigned) ? 1 : 0, 49, 3);
        e.set(i.sub, 52, 4);
        break;
    case Op::SULD:      // bindless: coordinates Ra.., handle Rc
    case Op::SUST:
    case Op::SUATOM: {
        if (i.c.k != Opnd::R || i.a.k != Opnd::R) throw Error(0, "maxwell: image operands must be registers");
        if (i.op == Op::SUATOM) {
            e.w = uint64_t(0xea00) << 48;
            rd();
            e.set(e.reg(i.b), 20, 8);
            e.set(i.sub, 29, 4);
            e.set((i.flags & kSigned) ? 1 : 0, 51, 3);
            e.set(1, 54, 1);
        } else if (i.op == Op::SULD) {
            e.w = uint64_t(0xeb00) << 48;
            rd();
            if (i.flags & kHalf) e.set(1, 52, 1), e.set(i.aux, 20, 3);   // .D: raw words of size aux
            else e.set(0xf, 20, 4);                                      // .P RGBA, converted from the format
        } else {
            e.w = uint64_t(0xeb20) << 48;
            e.set(e.reg(i.b), 0, 8);   // the data
            e.set(0xf, 20, 4);
        }
        ra();
        e.set(i.aux2, 33, 3);   // image type
        e.set(e.reg(i.c), 39, 8);
        break;
    }
    case Op::LDL:
    case Op::STL:
        e.w = uint64_t(i.op == Op::LDL ? 0xef40 : 0xef50) << 48;
        e.set(i.op == Op::LDL ? e.reg(i.d) : e.reg(i.b), 0, 8);
        ra();
        e.set(i.aux, 20, 24);
        e.set(4, 48, 3);   // 32 bits
        break;
    case Op::SHFL:      // SHFL.BFLY (or .IDX with kIndexed) Rd, Ra, lane, 0x1c03: within the quad
        e.w = uint64_t(0xef10) << 48;
        rd();
        ra();
        e.set(i.aux, 20, 5);
        e.set(1, 28, 1);
        e.set(1, 29, 1);
        e.set((i.flags & kIndexed) ? 0 : 3, 30, 2);
        e.set(0x1c03, 34, 13);
        e.set(7, 48, 3);
        break;
    case Op::FSWZADD:   // per quad lane: A - B or B - A, chosen by the swizzle
        e.w = uint64_t(0x50f8) << 48;
        rd();
        ra();
        e.set(e.reg(i.b), 20, 8);
        e.set(i.aux, 28, 8);
        break;
    case Op::PIXLD:     // PIXLD.MY_INDEX: the sample index
        e.w = uint64_t(0xefe8) << 48;
        rd();
        e.set(255, 8, 8);
        e.set(5, 31, 3);
        e.set(7, 45, 3);
        break;
    case Op::IADD32I:
        e.w = uint64_t(0x1c00) << 48;
        rd();
        ra();
        e.set(uint32_t(i.b.v), 20, 32);
        e.set((i.flags & kCC) != 0, 52, 1);
        e.set((i.flags & kX) != 0, 53, 1);
        break;
    case Op::XMAD:
        if (i.b.k != Opnd::R) throw Error(0, "maxwell: XMAD takes a register B");
        e.w = uint64_t(0x5b00) << 48;
        rd();
        ra();
        e.set(e.reg(i.b), 20, 8);
        rc();
        e.set((i.aux & kXmadHiB) != 0, 35, 1);
        e.set((i.aux & kXmadPsl) != 0, 36, 1);
        e.set((i.aux & kXmadMrg) != 0, 37, 1);
        e.set((i.aux & kXmadCbcc) ? 4 : 0, 50, 3);
        e.set((i.aux & kXmadHiA) != 0, 53, 1);
        break;
    case Op::IMNMX:
        e.operand_b(i.b, 0x5c20, 0x4c20, 0x3820, false);
        rd();
        ra();
        e.set(7, 39, 3);
        e.set((i.flags & kMax) != 0, 42, 1);
        e.set((i.flags & kSigned) != 0, 48, 1);
        break;
    case Op::ISET:
        e.operand_b(i.b, 0x5b50, 0x4b50, 0x3650, false);
        rd();
        ra();
        e.set(7, 39, 3);
        e.set((i.flags & kBF) != 0, 44, 1);
        e.set(0, 45, 2);
        e.set((i.flags & kSigned) != 0, 48, 1);
        if (i.cmp > 7) throw Error(0, "maxwell: integer comparison code");
        e.set(i.cmp, 49, 3);
        break;
    case Op::ISETP:
        e.operand_b(i.b, 0x5b60, 0x4b60, 0x3660, false);
        e.set(7, 0, 3);
        e.set(e.pred(i.pd), 3, 3);
        ra();
        e.set(e.pred(i.pa), 39, 3);
        e.set(i.pa_neg, 42, 1);
        e.set(0, 45, 2);
        e.set((i.flags & kSigned) != 0, 48, 1);
        if (i.cmp > 7) throw Error(0, "maxwell: integer comparison code");
        e.set(i.cmp, 49, 3);
        break;
    case Op::LOP:
        e.operand_b(i.b, 0x5c40, 0x4c40, 0x3840, false);
        rd();
        ra();
        e.set((i.flags & kInvA) != 0, 39, 1);
        e.set((i.flags & kInvB) != 0, 40, 1);
        e.set(i.sub, 41, 2);
        e.set(7, 48, 3);   // no predicate result
        break;
    case Op::LOP32I:
        e.w = uint64_t(0x0400) << 48;
        rd();
        ra();
        e.set(uint32_t(i.b.v), 20, 32);
        e.set(i.sub, 53, 2);
        e.set((i.flags & kInvA) != 0, 55, 1);
        break;
    // Shifts wrap their amount (& 31), as NVIDIA's compiler emits NV_gpu_program5's
    // SHL/SHR: every register-amount shift in the game's binaries sets .W.
    case Op::SHL:
        e.operand_b(i.b, 0x5c48, 0x4c48, 0x3848, false);
        rd();
        ra();
        e.set(1, 39, 1);
        break;
    case Op::SHR:
        e.operand_b(i.b, 0x5c28, 0x4c28, 0x3828, false);
        rd();
        ra();
        e.set(1, 39, 1);
        e.set((i.flags & kSigned) != 0, 48, 1);
        break;
    case Op::LDC: {
        if (i.b.k != Opnd::Cb) throw Error(0, "maxwell: LDC needs a constant buffer operand");
        if (i.b.off > 0x7fff) throw Error(0, "maxwell: LDC offset beyond 16 bits");
        e.w = uint64_t(0xef90) << 48;
        rd();
        e.set(i.a.k == Opnd::R ? e.reg(i.a) : 255, 8, 8);
        e.set(i.b.off, 20, 16);
        e.set(i.b.bank, 36, 5);
        e.set(4, 48, 3);   // B32
        break;
    }
    case Op::IPA:
        e.w = uint64_t(0xe000) << 48;
        rd();
        e.set(255, 8, 8);
        e.set(i.b.k == Opnd::R ? e.reg(i.b) : 255, 20, 8);
        e.set(i.aux, 28, 10);
        e.set(255, 39, 8);
        e.set(7, 47, 3);
        e.set((i.flags & kFlat) ? 2 : (i.flags & kPerspective) ? 1 : 0, 54, 2);
        break;
    case Op::ALD:   // C: the vertex handle (geometry inputs), else RZ
        e.w = uint64_t(0xefd8) << 48;
        rd();
        e.set(255, 8, 8);
        e.set(i.aux, 20, 10);
        e.set(e.reg(i.c), 39, 8);
        break;
    case Op::AST:   // C: the output vertex handle (geometry), else RZ
        e.w = uint64_t(0xeff0) << 48;
        e.set(e.reg(i.a), 0, 8);
        e.set(255, 8, 8);
        e.set(i.aux, 20, 10);
        e.set(e.reg(i.c), 39, 8);
        break;
    // The geometry prologue, word for word as NVIDIA's compiler writes it:
    // VMAD d = a.b0 * a.h1 + c (invocation times vertices per primitive, plus the
    // vertex), ISBERD d = the attribute buffer handle of vertex a, VSETP P = a.h1 <= imm
    // (the vertex is past the primitive's vertex count).
    case Op::VMAD:
        e.w = 0x5f04000020000000ull;
        rd();
        ra();
        e.set(e.reg(i.a), 20, 8);
        e.set(e.reg(i.c), 39, 8);
        break;
    case Op::ISBERD:
        e.w = 0xefd0000000000000ull;
        rd();
        ra();
        break;
    case Op::VSETP:
        e.w = 0x50f01ba000000000ull;
        e.set(7, 0, 3);
        e.set(e.pred(i.pd), 3, 3);
        ra();
        e.set(i.aux & 0xffff, 20, 16);
        break;
    case Op::OUT:   // aux: 1 emit, 2 cut; A: the output vertex handle; D: the next one
        e.w = 0xfbe0000000000000ull;
        rd();
        ra();
        e.set(255, 20, 8);   // stream 0
        e.set(i.aux & 3, 39, 2);
        break;
    case Op::TEX:
    case Op::TLD:
    case Op::TLD4:
    case Op::TXQ:
    case Op::TXD: {
        if (i.a.k != Opnd::R || (i.op != Op::TXQ && i.b.k != Opnd::R)) throw Error(0, "maxwell: texture operands must be registers");
        const bool dc = (i.flags & kDepthCompare) != 0, offset = (i.flags & kOffset) != 0;
        if (i.op == Op::TEX) {        // TEX.B
            e.w = uint64_t(0xde80) << 48;
            e.set(7, 51, 3);
            e.set(dc, 50, 1);
            e.set((i.flags & kLodZero) ? 1 : (i.flags & kBias) ? 2 : (i.flags & kLod) ? 3 : 0, 37, 3);
            e.set(offset, 36, 1);
        } else if (i.op == Op::TLD) { // TLD.B
            e.w = uint64_t(0xdd00) << 48;
            e.set((i.flags & kLod) ? 1 : 0, 55, 1);
            e.set(7, 51, 3);
            e.set((i.flags & kMultisample) != 0, 50, 1);
            e.set(offset, 35, 1);
        } else if (i.op == Op::TLD4) {   // TLD4.B
            e.w = uint64_t(0xdec0) << 48;
            e.set(7, 51, 3);
            e.set(dc, 50, 1);
            e.set(i.aux2, 38, 2);
            e.set(offset ? 1 : 0, 36, 2);
        } else if (i.op == Op::TXD) { // TXD.B: Ra = handle, coordinates [, layer]; Rb = gradients
            e.w = uint64_t(0xde78) << 48;
            e.set(7, 51, 3);
        } else if (i.b.k == Opnd::Cb) { // TXQ, bound: the handle word at c[2][B], Ra = LOD
            if (i.b.bank != 2 || i.b.off % 4 || i.b.off / 4 >= 0x2000) throw Error(0, "maxwell: bound TXQ handle out of range");
            e.w = uint64_t(0xdf48) << 48;
            e.set(1, 22, 3);          // DIMENSION
            e.set(uint32_t(i.b.off / 4), 36, 13);
        } else {                      // TXQ.B: Ra = handle, Ra+1 = LOD
            e.w = uint64_t(0xdf50) << 48;
            e.set(1, 22, 3);          // DIMENSION
        }
        rd();
        ra();
        if (i.op != Op::TXQ) {
            e.set(e.reg(i.b), 20, 8);
            e.set(i.sub, 28, 3);
        }
        e.set(i.aux, 31, 4);
        break;
    }
    case Op::KIL: e.w = 0xE33000000007000Full; break;
    case Op::EXIT: e.w = 0xE30000000007000Full; break;
    case Op::SYNC: e.w = 0xF0F800000007000Full; break;
    case Op::BRK: e.w = 0xE34000000007000Full; break;
    case Op::CONT: e.w = 0xE35000000007000Full; break;
    case Op::NOP: e.w = 0x50B0000000070F00ull; break;
    case Op::BRA: branch(0xE24000000000000Full); break;
    case Op::SSY: branch(uint64_t(0xe290) << 48); break;
    case Op::PBK: branch(uint64_t(0xe2a0) << 48); break;
    case Op::PCNT: branch(uint64_t(0xe2b0) << 48); break;
    default:
        throw Error(0, "maxwell: no encoding for this instruction yet");
    }
    // The guard predicate (SSY, PBK and PCNT have none).
    if (i.op != Op::SSY && i.op != Op::PBK && i.op != Op::PCNT) {
        e.w &= ~(0xfull << 16);
        e.guard(i);
    } else if (i.guard != kPT) {
        throw Error(0, "maxwell: SSY, PBK and PCNT cannot be predicated");
    }
    return e.w;
}

std::string listing(const Program& p, const Allocation* a) {
    static const char* names[] = {"label", "MOV", "MOV32I", "SEL", "FADD", "FADD32I", "FMUL", "FMUL32I", "FFMA", "FMNMX",
                                  "FSET", "FSETP", "MUFU", "RRO", "F2F", "F2I", "I2F", "I2I", "FSWZADD", "IADD", "IADD32I",
                                  "ISCADD", "XMAD", "IMNMX", "ISET", "ISETP", "LOP", "LOP32I", "SHL", "SHR", "BFE", "BFI",
                                  "POPC", "FLO", "PSETP", "LDC", "LDG", "STG", "LDS", "STS", "LDL", "STL", "ATOM", "ATOMS", "BAR", "MEMBAR", "SULD", "SUST", "SUATOM", "IPA", "ALD",
                                  "AST", "S2R", "VMAD", "ISBERD", "VSETP", "OUT", "TEX", "TLD", "TLD4", "TXQ", "TXD", "PIXLD", "SHFL", "KIL", "EXIT", "BRA", "SSY", "SYNC", "PBK",
                                  "BRK", "PCNT", "CONT", "NOP"};
    std::string out;
    auto reg = [&](int32_t v) {
        if (v == kRZ) return std::string("RZ");
        if (a) return "R" + std::to_string(a->reg.at(size_t(v)));
        return "v" + std::to_string(v);
    };
    auto opnd = [&](const Opnd& o) -> std::string {
        std::string s = o.neg ? "-" : "";
        if (o.abs) s += "|";
        if (o.k == Opnd::R) s += reg(o.v) + (o.n > 1 ? "x" + std::to_string(o.n) : "");
        else if (o.k == Opnd::Imm) { char b[16]; std::snprintf(b, sizeof b, "0x%x", uint32_t(o.v)); s += b; }
        else if (o.k == Opnd::Cb) { char b[32]; std::snprintf(b, sizeof b, "c[%u][0x%x]", o.bank, o.off); s += b; }
        else return "";
        if (o.abs) s += "|";
        return s;
    };
    for (const Inst& i : p.code) {
        if (i.op == Op::Label) { out += "L" + std::to_string(i.target) + ":\n"; continue; }
        std::string line = "    ";
        if (i.guard != kPT) line += std::string("@") + (i.guard_neg ? "!" : "") + "p" + std::to_string(i.guard) + " ";
        line += names[size_t(i.op)];
        if (i.d >= 0) line += " " + reg(i.d) + (i.dn > 1 ? "x" + std::to_string(i.dn) : "");
        if (i.pd >= 0) line += " p" + std::to_string(i.pd);
        for (const Opnd* o : {&i.a, &i.b, &i.c}) {
            const std::string s = opnd(*o);
            if (!s.empty()) line += ", " + s;
        }
        if (i.target >= 0) line += " -> L" + std::to_string(i.target);
        out += line + "\n";
    }
    return out;
}

} // namespace nvasm::maxwell

// Ampere backend for the Switch 2 (docs/SWITCH2_SHADERS.md, "Compiling for SF_GLSL_SAGE").
//
// Instruction selection and register allocation are the Maxwell backend's (nvasm_maxwell.cpp, maxwell_opt.cpp,
// maxwell_ra.cpp): the machine IR already places uniform buffer n in constant bank 3 + n, as the game's 2.0.0 programs
// do, and builds texture handles in registers for bindless texture instructions. This file translates each allocated
// instruction into Ampere instructions (compiler/ampere_encode.h), schedules them conservatively (per-instruction stall
// counts from Mesa NAK's SM80 latencies, scoreboards for variable-latency results), resolves branches and lays out the
// code: the 128-byte program header (version 4), the instructions, a branch to itself, NOP padding and a zeroed
// 128-byte tail where the game's programs keep their constant table.
//
// What differs from Switch 1 and is translated here:
// - texture handles: Switch 1 reads each handle word from c[2][32 + offset]; on Switch 2 the word of resource slot s
//   is at c[0x10][0x10 * s]. Options::ampere_handles maps one to the other;
// - IPA.MULTIPLY does not exist: perspective inputs are IPA.PASS then FMUL.RZ by 1/w, as the game's programs do it;
// - Maxwell's 64-bit words with a scheduling word every three instructions become 128-bit words with their own
//   scheduling bits;
// - SSY/SYNC reconvergence becomes BSSY/BSYNC (convergence barriers); a loop's PBK the same, with BRK a branch to the
//   loop's exit that first leaves (BREAK) every barrier opened inside the loop, CONT likewise to the continue point;
// - storage buffer addresses move from c[0] to c[0x10] (Options::storage), and loads are LDG.E.CONSTANT;
// - geometry programs: the input vertex handles come from SR_INVOCATION_INFO's bytes (PRMT, IMAD, ISBERD) as the game's
//   do, a vertex past the primitive's count reads handle 0x80000000, and every EXIT first closes the output with
//   OUT.FINAL on the current output handle (the selector names it in EXIT's aux2; write_ampere makes it a use so it
//   stays allocated).
// Anything else the selector can emit and this file does not translate yet is refused by name.
#include "ampere_encode.h"
#include "ampere_write.h"
#include "maxwell_mir.h"
#include "nvasm_ir.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <sstream>

namespace nvasm::ir {

maxwell::Program select_maxwell(const Function& f, bool& memory, uint32_t& local_bytes);   // nvasm_maxwell.cpp

namespace {

namespace A = nvasm::ampere;
using maxwell::Allocation;
using maxwell::Inst;
using maxwell::Opnd;
using MOp = maxwell::Op;

// Conservative latencies (NAK sm80_instr_latencies.rs): a register written by a fixed-latency ALU or FMA
// instruction is readable 6 cycles after issue; a predicate 14 cycles after.
constexpr unsigned kRegLatency = 6, kPredLatency = 14;

struct AInst {
    A::Word w;
    std::vector<int> reads, writes;           // hardware registers (RZ left out)
    std::vector<int> pred_reads, pred_writes; // P0..P6
    bool variable = false;    // result through a scoreboard (MUFU, conversions, S2R, IPA, LDC, textures, memory)
    bool block_end = false;   // branch, EXIT, KILL, convergence barriers: everything settles first
    int branch_label = -1;    // BRA / BSSY target label, patched after layout
    unsigned guard = A::PT;
    bool guard_neg = false;
    bool bssy = false;
    unsigned barrier = 0;     // BSSY / BSYNC barrier register
    std::string text;
};

[[noreturn]] void refuse(const std::string& what) { throw Error(0, "ampere: " + what); }

class Translator {
public:
    Translator(const maxwell::Program& p, const Allocation& a, const AmpereOptions& o, bool geometry)
        : p_(p), a_(a), o_(o), geometry_(geometry) {}

    std::vector<AInst> out;
    std::vector<int> label_at;   // label -> index in out
    unsigned registers() const { return std::max(unsigned(std::max(a_.registers, 1)), scratch_ ? scratch_ + 1 : 0u); }

    void run() {
        label_at.assign(size_t(p_.labels), -1);
        for (const Inst& i : p_.code) one(i);
    }

private:
    const maxwell::Program& p_;
    const Allocation& a_;
    const AmpereOptions& o_;
    const bool geometry_;
    std::set<int32_t> past_count_;   // geometry: predicates "this vertex is past the primitive's count" (VSETP)
    unsigned scratch_ = 0;
    unsigned scratch(unsigned k = 0) {   // registers above the allocation, for translations that need temporaries
        const unsigned r = unsigned(std::max(a_.registers, 1)) + k;
        if (r > 252) refuse("no register left for a temporary");
        scratch_ = std::max(scratch_, r);
        return r;
    }
    // One fixed-latency ALU instruction of a multi-instruction translation.
    void alu(const Inst& i, A::Word w, std::vector<int> reads, int write, const char* text) {
        AInst ai;
        ai.w = w;
        ai.reads = std::move(reads);
        if (write >= 0 && write != 255) ai.writes.push_back(write);
        ai.guard = pred(i.guard);
        ai.guard_neg = i.guard_neg;
        if (i.guard != maxwell::kPT) ai.pred_reads.push_back(int(ai.guard));
        ai.text = text;
        out.push_back(ai);
    }
    std::vector<unsigned> barriers_;   // SSY nesting: the convergence barrier of each open region
    std::map<int, unsigned> sync_barrier_;   // label -> barrier BSSY opened for it
    std::map<int, size_t> loop_depth_;       // a loop's exit label -> barriers_ size inside its PBK region
    std::vector<size_t> loops_;              // open loops: barriers_ size inside each PBK region
    unsigned carry_ = A::PT;                 // the predicate holding the last .CC carry
    // A predicate above the allocated ones, for carries and compares a translation needs.
    unsigned scratch_pred() const {
        int top = -1;
        for (int v : a_.pred) top = std::max(top, v);
        if (top >= 6) refuse("no predicate left for a temporary");
        return 6;
    }
    // Leave the barriers opened after the first `keep` (inside the loop), before a branch out of them.
    void leave_barriers(const Inst& i, size_t keep) {
        for (size_t k = barriers_.size(); k-- > keep;) {
            AInst b;
            b.w = A::Break(barriers_[k], A::PT, false, {}, pred(i.guard), i.guard_neg);
            if (i.guard != maxwell::kPT) b.pred_reads.push_back(int(pred(i.guard)));
            b.block_end = true;
            b.text = "BREAK";
            out.push_back(b);
        }
    }

    int reg(int32_t v) const {
        if (v == maxwell::kRZ) return 255;
        if (v < 0 || size_t(v) >= a_.reg.size() || a_.reg[size_t(v)] < 0) refuse("unallocated register");
        return a_.reg[size_t(v)];
    }
    unsigned pred(int32_t v) const {
        if (v == maxwell::kPT) return A::PT;
        if (v < 0 || size_t(v) >= a_.pred.size()) refuse("unallocated predicate");
        return unsigned(a_.pred[size_t(v)]);
    }
    // An operand as an encoder source; constant handle words are moved to Switch 2's handle bank.
    A::Src src(const Opnd& o, AInst& ai) const {
        switch (o.k) {
        case Opnd::R: {
            const int r = reg(o.v);
            for (int k = 0; k < o.n; ++k)
                if (r != 255) ai.reads.push_back(r + k);
            return A::Src::R(unsigned(r), o.neg, o.abs);
        }
        case Opnd::Imm: return A::Src::I(uint32_t(o.v));
        case Opnd::Cb: {
            uint32_t bank = o.bank, off = o.off;
            if (bank == 2) {   // Switch 1's texture handle words (c[2][32 + program.buffer[14] offset])
                const auto it = o_.handles.find(off >= 32 ? off - 32 : ~0u);
                if (it == o_.handles.end()) refuse("a constant in bank 2 that is no mapped texture handle word");
                if (it->second == AmpereOptions::ZeroHandle) return A::Src::I(0);
                bank = 0x10, off = it->second;
            } else if (bank == 0) {   // the driver's bank: only the storage buffer addresses move
                const auto it = o_.storage.find(off);
                if (it == o_.storage.end()) refuse("a constant in bank 0 that is no mapped storage buffer address");
                bank = 0x10, off = it->second;
            } else if (o_.moved.count(bank)) {
                off = moved(bank, off);
            }
            return A::Src::C(bank, off, o.neg, o.abs);
        }
        default: refuse("an empty operand");
        }
    }
    // A float source: a negated immediate becomes the immediate with its sign flipped (Ampere immediates carry no
    // modifier), as the Maxwell encoder does for FADD32I/FMUL32I.
    A::Src fsrc(const Opnd& o, AInst& ai) const {
        A::Src s = src(o, ai);
        if (s.kind == A::Src::Kind::Imm) {
            if (o.abs) s.imm &= 0x7fffffffu;
            if (o.neg) s.imm ^= 0x80000000u;
        }
        return s;
    }
    // A constant offset in a bank whose struct members moved, at the member's new place.
    uint32_t moved(uint32_t bank, uint32_t off) const {
        for (const auto& m : o_.moved.at(bank))
            if (off >= m.old_start && off < m.old_end) {
                if (m.new_start < 0) refuse("a read of a uniform buffer member the target's layout lacks");
                return uint32_t(m.new_start) + (off - m.old_start);
            }
        return off;
    }
    AInst& add(const Inst& i, A::Word w, const char* text) {
        AInst ai;
        ai.w = w;
        ai.guard = pred(i.guard);
        ai.guard_neg = i.guard_neg;
        if (i.guard != maxwell::kPT) ai.pred_reads.push_back(int(pred(i.guard)));
        ai.text = text;
        out.push_back(ai);
        return out.back();
    }
    void dst(AInst& ai, int32_t d, int n = 1) {
        const int r = reg(d);
        if (r != 255)
            for (int k = 0; k < n; ++k) ai.writes.push_back(r + k);
    }
    // Sources are collected while encoding, so each instruction is built in a scratch AInst then moved.
    void push(const Inst& i, AInst ai, int32_t d, int dn, bool variable, const char* text) {
        ai.guard = pred(i.guard);
        ai.guard_neg = i.guard_neg;
        if (i.guard != maxwell::kPT) ai.pred_reads.push_back(int(pred(i.guard)));
        if (d >= 0 || d == maxwell::kRZ) dst(ai, d, dn);
        ai.variable = variable;
        ai.text = text;
        out.push_back(std::move(ai));
    }

    static unsigned lop3_table(uint8_t op, bool invA, bool invB) {
        const unsigned a = invA ? 0x0f : 0xf0, b = invB ? 0x33 : 0xcc;
        switch (maxwell::Lop(op)) {
        case maxwell::Lop::AND: return a & b;
        case maxwell::Lop::OR: return a | b;
        case maxwell::Lop::XOR: return (a ^ b) & 0xff;
        case maxwell::Lop::PASS_B: return b;
        }
        return 0;
    }

    void one(const Inst& i) {
        const unsigned g = pred(i.guard);
        const bool gn = i.guard_neg;
        const bool ftz = (i.flags & maxwell::kFtz) != 0, sat = (i.flags & maxwell::kSat) != 0;
        AInst ai;
        switch (i.op) {
        case MOp::Label:
            label_at[size_t(i.target)] = int(out.size());
            if (sync_barrier_.count(i.target)) {   // the reconvergence point of a BSSY region
                AInst s;
                s.w = Bsync(sync_barrier_[i.target]);
                s.block_end = true;
                s.text = "BSYNC";
                out.push_back(s);
                barriers_.pop_back();
                if (loop_depth_.count(i.target)) loops_.pop_back();
            }
            return;
        case MOp::MOV:
            ai.w = A::Mov(0, src(i.b, ai), {}, g, gn);
            ai.w.Set(16, 8, unsigned(reg(i.d)));
            push(i, ai, i.d, 1, false, "MOV");
            return;
        case MOp::MOV32I:
            if (geometry_ && past_count_.count(i.guard) && !gn && i.a.v == 0) {   // the handle of a vertex past the count
                ai.w = A::Mov(unsigned(reg(i.d)), A::Src::I(0x80000000u), {}, g, gn);
                push(i, ai, i.d, 1, false, "MOV (no vertex)");
                return;
            }
            ai.w = A::Mov(unsigned(reg(i.d)), A::Src::I(uint32_t(i.a.v)), {}, g, gn);
            push(i, ai, i.d, 1, false, "MOV");
            return;
        case MOp::FADD: case MOp::FADD32I: {
            const A::Src a = fsrc(i.a, ai), b = fsrc(i.b, ai);
            ai.w = A::FAdd(unsigned(reg(i.d)), a, b, sat, ftz, A::Round::Nearest, {}, g, gn);
            push(i, ai, i.d, 1, false, "FADD");
            return;
        }
        case MOp::FMUL: case MOp::FMUL32I: {
            const A::Src a = fsrc(i.a, ai), b = fsrc(i.b, ai);
            ai.w = A::FMul(unsigned(reg(i.d)), a, b, sat, ftz, A::Round::Nearest, {}, g, gn);
            push(i, ai, i.d, 1, false, "FMUL");
            return;
        }
        case MOp::FFMA: {
            const A::Src a = fsrc(i.a, ai), b = fsrc(i.b, ai), c = fsrc(i.c, ai);
            if (c.kind != A::Src::Kind::Reg && b.kind != A::Src::Kind::Reg) refuse("FFMA with two non-register sources");
            ai.w = A::FFma(unsigned(reg(i.d)), a, b, c, sat, ftz, A::Round::Nearest, {}, g, gn);
            push(i, ai, i.d, 1, false, "FFMA");
            return;
        }
        case MOp::FMNMX: {
            const A::Src a = fsrc(i.a, ai), b = fsrc(i.b, ai);
            ai.w = A::FMnMx(unsigned(reg(i.d)), a, b, (i.flags & maxwell::kMax) == 0, ftz, {}, g, gn);
            push(i, ai, i.d, 1, false, "FMNMX");
            return;
        }
        case MOp::FSETP: {
            const A::Src a = fsrc(i.a, ai), b = fsrc(i.b, ai);
            if (i.cmp == maxwell::T || i.cmp == maxwell::F) refuse("FSETP with a constant comparison");
            ai.w = A::FSetP(pred(i.pd), a, b, A::Cmp(i.cmp), A::PredOp(i.sub), pred(i.pa), i.pa_neg, ftz, {}, g, gn);
            if (i.pa != maxwell::kPT) ai.pred_reads.push_back(int(pred(i.pa)));
            ai.pred_writes.push_back(int(pred(i.pd)));
            push(i, ai, -1, 0, false, "FSETP");
            return;
        }
        case MOp::ISETP: {
            const A::Src a = src(i.a, ai), b = src(i.b, ai);
            if (i.cmp == maxwell::T || i.cmp > maxwell::GE) refuse("ISETP comparison");
            ai.w = A::ISetP(pred(i.pd), a, b, A::ICmp(i.cmp), (i.flags & maxwell::kSigned) != 0, A::PredOp(i.sub),
                            pred(i.pa), i.pa_neg, {}, g, gn);
            if (i.pa != maxwell::kPT) ai.pred_reads.push_back(int(pred(i.pa)));
            ai.pred_writes.push_back(int(pred(i.pd)));
            push(i, ai, -1, 0, false, "ISETP");
            return;
        }
        case MOp::SEL: {   // d = pa ? a : b, bit-exact (SEL, not FSEL)
            const A::Src a = src(i.a, ai), b = src(i.b, ai);
            ai.w = Sel(unsigned(reg(i.d)), a, b, pred(i.pa), i.pa_neg, g, gn);
            if (i.pa != maxwell::kPT) ai.pred_reads.push_back(int(pred(i.pa)));
            push(i, ai, i.d, 1, false, "SEL");
            return;
        }
        case MOp::MUFU: {
            if (i.sub == 4 /*RCP*/ || i.sub == 5 /*RSQ*/ || i.sub <= 3 || i.sub == 8) {
                const A::Src s = src(i.a, ai);
                ai.w = A::MuFu(A::MuFuOp(i.sub), unsigned(reg(i.d)), s, {}, g, gn);
                push(i, ai, i.d, 1, true, "MUFU");
                return;
            }
            refuse("MUFU function");
        }
        case MOp::RRO: {   // Maxwell's range reduction before MUFU.SIN/COS/EX2: Ampere's MUFU takes the value itself
            const A::Src s = src(i.b, ai);
            if (s.neg || s.abs)   // the modifier applied exactly: x * 1.0 (no FTZ, so denormals pass)
                ai.w = A::FMul(unsigned(reg(i.d)), s, A::Src::I(0x3f800000u), false, false, A::Round::Nearest, {}, g, gn);
            else
                ai.w = A::Mov(unsigned(reg(i.d)), s, {}, g, gn);
            push(i, ai, i.d, 1, false, "MOV (RRO)");
            return;
        }
        case MOp::LOP: case MOp::LOP32I: {
            const A::Src a = src(i.a, ai), b = src(i.b, ai);
            ai.w = A::Lop3(unsigned(reg(i.d)), a, b, A::Src::Zero(), lop3_table(i.sub, (i.flags & maxwell::kInvA) != 0,
                                                                         (i.flags & maxwell::kInvB) != 0), {}, g, gn);
            push(i, ai, i.d, 1, false, "LOP3");
            return;
        }
        case MOp::IADD: case MOp::IADD32I: {   // IADD3 d, a, b, RZ; .CC writes the carry, .X adds it
            const A::Src a = src(i.a, ai), b = src(i.b, ai);
            const bool cc = (i.flags & maxwell::kCC) != 0, x = (i.flags & maxwell::kX) != 0;
            if (x && carry_ == A::PT) refuse("IADD.X without a carry");
            const unsigned cin = x ? carry_ : A::PT, out_pred = cc ? scratch_pred() : A::PT;
            if (x) ai.pred_reads.push_back(int(cin));
            if (cc) ai.pred_writes.push_back(int(out_pred));
            ai.w = A::IAdd3(unsigned(reg(i.d)), a, b, A::Src::Zero(), {}, g, gn, out_pred, A::PT, x, cin, !x);
            carry_ = cc ? out_pred : A::PT;
            push(i, ai, i.d, 1, false, x ? "IADD3.X" : "IADD3");
            return;
        }
        case MOp::ISCADD: {   // d = (a << aux) + b: LEA, the carry to a predicate with .CC
            if (i.flags & maxwell::kX) refuse("ISCADD.X");
            if (i.a.k != Opnd::R) refuse("ISCADD with a non-register A");
            const A::Src a = src(i.a, ai), b = src(i.b, ai);
            const bool cc = (i.flags & maxwell::kCC) != 0;
            const unsigned out_pred = cc ? scratch_pred() : A::PT;
            if (cc) ai.pred_writes.push_back(int(out_pred));
            if (i.a.neg) refuse("ISCADD with a negated A");
            ai.w = A::Lea(unsigned(reg(i.d)), A::Src::R(a.reg), b, i.aux & 31, false, false, A::Src::Zero(), {}, g, gn, out_pred);
            carry_ = cc ? out_pred : A::PT;
            push(i, ai, i.d, 1, false, "LEA");
            return;
        }
        case MOp::ISET: {   // ISETP into a spare predicate, then SEL 1.0 (BF) or all ones, or 0
            const A::Src a = src(i.a, ai), b = src(i.b, ai);
            if (i.cmp == maxwell::T || i.cmp > maxwell::GE) refuse("ISET comparison");
            const unsigned p = scratch_pred();
            AInst c = ai;
            c.w = A::ISetP(p, a, b, A::ICmp(i.cmp), (i.flags & maxwell::kSigned) != 0, A::PredOp::And, A::PT, false, {}, g, gn);
            c.pred_writes.push_back(int(p));
            push(i, c, -1, 0, false, "ISETP (ISET)");
            AInst s;
            const uint32_t yes = (i.flags & maxwell::kBF) ? 0x3f800000u : 0xffffffffu;
            s.w = Sel(unsigned(reg(i.d)), A::Src::Zero(), A::Src::I(yes), p, true, g, gn);
            s.pred_reads.push_back(int(p));
            push(i, s, i.d, 1, false, "SEL (ISET)");
            return;
        }
        case MOp::SHFL: {   // within the quad: SHFL.BFLY (or .IDX) d, a, lane, 0x1c03, a scoreboard as the game's
            if (i.a.k != Opnd::R) refuse("SHFL of a non-register");
            const int a = reg(i.a.v);
            ai.reads.push_back(a);
            ai.w = A::Shfl(unsigned(reg(i.d)), unsigned(a), i.aux & 31, 0x1c03,
                           (i.flags & maxwell::kIndexed) ? A::ShflMode::Idx : A::ShflMode::Bfly, A::PT, {}, g, gn);
            push(i, ai, i.d, 1, true, "SHFL");
            return;
        }
        case MOp::FSWZADD: {   // Maxwell's mask has lane L in bits 2L; Ampere's in bits 6 - 2L
            if (i.a.k != Opnd::R || i.b.k != Opnd::R || i.a.neg || i.b.neg || i.a.abs || i.b.abs) refuse("FSWZADD operands");
            const int a = reg(i.a.v), b = reg(i.b.v);
            ai.reads.push_back(a);
            ai.reads.push_back(b);
            unsigned ops = 0;
            for (unsigned lane = 0; lane < 4; ++lane) ops |= ((i.aux >> (2 * lane)) & 3u) << (6 - 2 * lane);
            ai.w = A::FSwzAdd(unsigned(reg(i.d)), unsigned(a), unsigned(b), ops, ftz, A::Round::Nearest, {}, g, gn);
            push(i, ai, i.d, 1, false, "FSWZADD");
            return;
        }
        case MOp::LDG: {   // a 32-bit load from a storage buffer: LDG.E.CONSTANT (the game's form)
            if (i.a.k != Opnd::R || i.a.n != 2) refuse("LDG without a 64-bit register address");
            const A::Src addr = src(i.a, ai);
            ai.w = A::Ldg(unsigned(reg(i.d)), addr, 0, A::MemType::B32, A::MemOrder::Constant, true, {}, g, gn);
            push(i, ai, i.d, 1, true, "LDG.E.CONSTANT");
            return;
        }
        case MOp::IPA: {
            const unsigned d = unsigned(reg(i.d));
            const bool flat = (i.flags & maxwell::kFlat) != 0, persp = (i.flags & maxwell::kPerspective) != 0;
            // Maxwell's IPA.MULTIPLY reads 1/w as it writes, so allocation may give both the same register; the two
            // instructions here then interpolate into a scratch register above the allocated ones.
            const bool clash = persp && i.b.k == Opnd::R && reg(i.b.v) == int(d);
            const unsigned into = clash ? scratch() : d;
            ai.w = A::Ipa(into, i.aux, flat ? A::InterpFreq::Constant : A::InterpFreq::Pass, A::InterpLoc::Default, {}, g, gn);
            ai.writes.push_back(int(into));
            ai.guard = g; ai.guard_neg = gn;
            if (i.guard != maxwell::kPT) ai.pred_reads.push_back(int(g));
            ai.variable = true; ai.text = "IPA";
            out.push_back(ai);
            if (persp) {   // IPA.MULTIPLY: times the interpolated 1/w, as the game does it (FMUL.FTZ.RZ)
                if (i.b.k != Opnd::R) refuse("perspective IPA without 1/w");
                AInst m;
                const A::Src w = src(i.b, m);
                m.reads.push_back(int(into));
                m.w = A::FMul(d, A::Src::R(into), w, false, true, A::Round::Zero, {}, g, gn);
                push(i, m, i.d, 1, false, "FMUL.RZ");
            }
            return;
        }
        case MOp::TEX: {
            if (i.a.k != Opnd::R || i.b.k != Opnd::R) refuse("texture operands must be registers");
            const int coords = reg(i.a.v), meta = reg(i.b.v);
            for (int k = 0; k < i.a.n; ++k) ai.reads.push_back(coords + k);
            for (int k = 0; k < i.b.n; ++k) ai.reads.push_back(meta + k);
            ai.w = TexB(unsigned(reg(i.d)), unsigned(coords), unsigned(meta), i.sub, i.aux, i.flags, g, gn);
            push(i, ai, i.d, i.dn, true, "TEX.B");
            return;
        }
        case MOp::F2F: {   // float to float with rounding: FRND; halves as the game converts them
            if (i.flags & maxwell::kHalf) {   // a half of the source to fp32: HADD2.F32 d, -RZ, b.H0_H0 (or .H1_H1)
                if (i.b.k != Opnd::R || i.b.neg || i.b.abs || (i.flags & maxwell::kSat)) refuse("half-float source form");
                const A::Src b = src(i.b, ai);
                ai.w = A::HAdd2(unsigned(reg(i.d)), A::Src::R(A::RZ, true), b, A::HSwz::H1H0,
                                (i.flags & maxwell::kHigh) ? A::HSwz::H1H1 : A::HSwz::H0H0, true, false, ftz, {}, g, gn);
                push(i, ai, i.d, 1, false, "HADD2.F32");
                return;
            }
            if (i.flags & maxwell::kSignedDst) {   // fp32 to a half in the low bits: F2F.F16.F32
                if (i.flags & maxwell::kSat) refuse("saturated half-float conversion");
                const A::Src b = fsrc(i.b, ai);
                ai.w = A::F2F(unsigned(reg(i.d)), b, 16, 32, A::Round(i.sub & 3), ftz, {}, g, gn);
                push(i, ai, i.d, 1, true, "F2F.F16.F32");
                return;
            }
            if (i.flags & maxwell::kHigh) refuse("half-float conversions");
            const A::Src b = fsrc(i.b, ai);
            ai.w = Frnd(unsigned(reg(i.d)), b, i.sub & 3, ftz, g, gn);
            push(i, ai, i.d, 1, true, "FRND");
            return;
        }
        case MOp::ALD: {   // vertex attribute load: aux the attribute address, c the vertex handle (RZ for vertex)
            const int vtx = i.c.k == Opnd::R ? reg(i.c.v) : 255;
            if (vtx != 255) ai.reads.push_back(vtx);
            ai.w = A::Ald(unsigned(reg(i.d)), i.aux, 1, unsigned(vtx), false, false, {}, g, gn);
            push(i, ai, i.d, 1, true, "ALD");
            return;
        }
        case MOp::AST: {   // vertex attribute store: a the value, aux the address, c the output vertex handle
            if (i.a.k != Opnd::R) refuse("AST of a non-register");
            const int data = reg(i.a.v), vtx = i.c.k == Opnd::R ? reg(i.c.v) : 255;
            ai.reads.push_back(data);
            if (vtx != 255) ai.reads.push_back(vtx);
            ai.w = A::Ast(i.aux, unsigned(data), 1, unsigned(vtx), false, {}, g, gn);
            push(i, ai, -1, 0, false, "AST");
            ai.variable = false;
            out.back().variable = true;   // reads its sources late: later writers wait on its read scoreboard
            return;
        }
        case MOp::S2R:
            ai.w = A::S2R(unsigned(reg(i.d)), A::SR(i.aux), {}, g, gn);
            push(i, ai, i.d, 1, true, "S2R");
            return;
        case MOp::LDC: {
            if (i.b.k != Opnd::Cb) refuse("LDC without a constant operand");
            const int index = i.a.k == Opnd::R ? reg(i.a.v) : 255;
            if (index != 255) ai.reads.push_back(index);
            if (i.b.bank == 2) refuse("LDC of a texture handle word");
            uint32_t off = i.b.off;
            if (o_.moved.count(i.b.bank)) {
                if (index != 255) refuse("a dynamic index into a uniform buffer whose members moved");
                off = moved(i.b.bank, off);
            }
            ai.w = A::Ldc(unsigned(reg(i.d)), i.b.bank, off, unsigned(index), A::MemType::B32, {}, g, gn);
            push(i, ai, i.d, 1, true, "LDC");
            return;
        }
        case MOp::TLD: {   // texel fetch, bindless: a the coordinates, b the handle (then LOD, offset, sample)
            if (i.a.k != Opnd::R || i.b.k != Opnd::R) refuse("texture operands must be registers");
            if (i.flags & (maxwell::kOffset | maxwell::kMultisample)) refuse("TLD with offsets or samples");
            const int coords = reg(i.a.v), meta = reg(i.b.v);
            for (int k = 0; k < i.a.n; ++k) ai.reads.push_back(coords + k);
            for (int k = 0; k < i.b.n; ++k) ai.reads.push_back(meta + k);
            static const A::TexDim dims[8] = {A::TexDim::D1, A::TexDim::Array1D, A::TexDim::D2, A::TexDim::Array2D,
                                              A::TexDim::D3, A::TexDim::D1, A::TexDim::Cube, A::TexDim::ArrayCube};
            if (i.sub > 7 || i.sub == 5) refuse("texture type");
            const unsigned d = unsigned(reg(i.d));
            A::TexRegs regs;
            regs.dst0 = d;
            regs.dst1 = i.dn > 2 ? d + 2 : A::RZ;
            regs.a = unsigned(coords);
            regs.b = unsigned(meta);
            A::TexOpts opts;
            opts.mask = i.aux & 15;
            opts.lod = (i.flags & maxwell::kLod) ? A::TexLod::Lod : A::TexLod::Zero;
            ai.w = A::Tld(regs, A::TexHandle::Bindless(), dims[i.sub], opts, {}, g, gn);
            push(i, ai, i.d, i.dn, true, "TLD.B");
            return;
        }
        case MOp::SHL: case MOp::SHR: {   // Maxwell's .W shifts (amount modulo 32) as funnel shifts, as the game writes them
            const A::Src a = src(i.a, ai), b = src(i.b, ai);
            const bool right = i.op == MOp::SHR, sign = (i.flags & maxwell::kSigned) != 0;
            if (right)   // SHF.R.{U32,S32}.HI d, RZ, b, a
                ai.w = A::Shf(unsigned(reg(i.d)), A::Src::Zero(), b, a, true, sign ? A::ShfType::S32 : A::ShfType::U32, true,
                              true, {}, g, gn);
            else         // SHF.L.U32 d, a, b, RZ
                ai.w = A::Shf(unsigned(reg(i.d)), a, b, A::Src::Zero(), false, A::ShfType::U32, false, true, {}, g, gn);
            push(i, ai, i.d, 1, false, right ? "SHF.R" : "SHF.L");
            return;
        }
        case MOp::FSET: {   // FSET.BF: 1.0 or 0.0 (the only form the game uses)
            const A::Src a = fsrc(i.a, ai), b = fsrc(i.b, ai);
            if (i.flags & maxwell::kBF) {
                ai.w = A::FSet(unsigned(reg(i.d)), a, b, A::Cmp(i.cmp), A::PredOp::And, A::PT, false, ftz, {}, g, gn);
                push(i, ai, i.d, 1, false, "FSET.BF");
                return;
            }
            // The integer mask (all ones or 0): FSET.BF gives 0x3f800000 or 0; >> 29 gives 1 or 0; negated, -1 or 0.
            const unsigned s0 = scratch(0);
            alu(i, A::FSet(s0, a, b, A::Cmp(i.cmp), A::PredOp::And, A::PT, false, ftz, {}, g, gn), ai.reads, int(s0), "FSET.BF");
            alu(i, A::Shf(s0, A::Src::Zero(), A::Src::I(29), A::Src::R(s0), true, A::ShfType::U32, true, false, {}, g, gn),
                {int(s0)}, int(s0), "FSET: >> 29");
            alu(i, A::IAdd3(unsigned(reg(i.d)), A::Src::Zero(), A::Src::R(s0, true), A::Src::Zero(), {}, g, gn), {int(s0)},
                reg(i.d), "FSET: mask");
            return;
        }
        case MOp::IMNMX: {
            const A::Src a = src(i.a, ai), b = src(i.b, ai);
            ai.w = A::IMnMx(unsigned(reg(i.d)), a, b, (i.flags & maxwell::kMax) == 0, (i.flags & maxwell::kSigned) != 0, {}, g, gn);
            push(i, ai, i.d, 1, false, "IMNMX");
            return;
        }
        case MOp::XMAD: {
            // Maxwell's 16-bit multiply-add, exactly: d = (A16 * B16) [<< 16 if PSL] + C [+ (B << 16) if CBCC], then
            // [MRG] d = (d & 0xffff) | (B << 16); A16/B16 the low or high (HiA/HiB) unsigned halves. Ampere has no XMAD,
            // so this is masks, shifts, IMAD and IADD3 through two registers above the allocation.
            if (i.a.k != Opnd::R || i.b.k != Opnd::R) refuse("XMAD with a non-register A or B");
            const int ra = reg(i.a.v), rb = reg(i.b.v), d = reg(i.d);
            const A::Src c = i.c.k == Opnd::None ? A::Src::Zero() : src(i.c, ai);
            const int rc = c.kind == A::Src::Kind::Reg ? int(c.reg) : -1;
            const unsigned s0 = scratch(0), s1 = scratch(1);
            const A::Src A = A::Src::R(unsigned(ra)), B = A::Src::R(unsigned(rb)), S0 = A::Src::R(s0), S1 = A::Src::R(s1);
            if (i.aux & maxwell::kXmadHiA)
                alu(i, A::Shf(s0, A::Src::Zero(), A::Src::I(16), A, true, A::ShfType::U32, true, false, {}, g, gn), {ra}, int(s0), "XMAD: A.hi");
            else
                alu(i, A::Lop3(s0, A, A::Src::I(0xffff), A::Src::Zero(), 0xc0, {}, g, gn), {ra}, int(s0), "XMAD: A.lo");
            if (i.aux & maxwell::kXmadHiB)
                alu(i, A::Shf(s1, A::Src::Zero(), A::Src::I(16), B, true, A::ShfType::U32, true, false, {}, g, gn), {rb}, int(s1), "XMAD: B.hi");
            else
                alu(i, A::Lop3(s1, B, A::Src::I(0xffff), A::Src::Zero(), 0xc0, {}, g, gn), {rb}, int(s1), "XMAD: B.lo");
            alu(i, A::IMad(s0, S0, S1, A::Src::Zero(), false, {}, g, gn), {int(s0), int(s1)}, int(s0), "XMAD: product");
            if (i.aux & maxwell::kXmadPsl)
                alu(i, A::Shf(s0, S0, A::Src::I(16), A::Src::Zero(), false, A::ShfType::U32, false, false, {}, g, gn), {int(s0)}, int(s0), "XMAD: PSL");
            std::vector<int> creads{int(s0)};
            if (rc >= 0 && rc != 255) creads.push_back(rc);
            if (i.aux & maxwell::kXmadCbcc) {
                alu(i, A::Shf(s1, B, A::Src::I(16), A::Src::Zero(), false, A::ShfType::U32, false, false, {}, g, gn), {rb}, int(s1), "XMAD: B << 16");
                creads.push_back(int(s1));
                alu(i, A::IAdd3(s0, S0, S1, c, {}, g, gn), creads, int(s0), "XMAD: + C + (B << 16)");
            } else {
                alu(i, A::IAdd3(s0, S0, A::Src::Zero(), c, {}, g, gn), creads, int(s0), "XMAD: + C");
            }
            if (i.aux & maxwell::kXmadMrg) {
                alu(i, A::Shf(s1, B, A::Src::I(16), A::Src::Zero(), false, A::ShfType::U32, false, false, {}, g, gn), {rb}, int(s1), "XMAD: B << 16");
                // (s0 & 0xffff) | s1: LOP3 with a = s0, b = 0xffff, c = s1, table (A & B) | C
                alu(i, A::Lop3(unsigned(d), S0, A::Src::I(0xffff), S1, 0xea, {}, g, gn), {int(s0), int(s1)}, d, "XMAD: MRG");
            } else {
                alu(i, A::Mov(unsigned(d), S0, {}, g, gn), {int(s0)}, d, "XMAD: result");
            }
            return;
        }
        case MOp::F2I: {   // float to integer: 32-bit (or 16-bit with kHalf), signed or not, the MIR's rounding
            const A::Src b = fsrc(i.b, ai);
            const bool half = (i.flags & maxwell::kHalf) != 0, sign = (i.flags & maxwell::kSignedDst) != 0;
            const A::IntType to = half ? (sign ? A::IntType::S16 : A::IntType::U16) : (sign ? A::IntType::S32 : A::IntType::U32);
            ai.w = A::F2I(unsigned(reg(i.d)), b, to, A::Round(i.sub & 3), ftz, 32, true, {}, g, gn);
            push(i, ai, i.d, 1, true, "F2I");
            return;
        }
        case MOp::I2F: {   // 32-bit integer to float: I2FP, fixed latency (the game's I2FP never sets a scoreboard)
            const A::Src b = src(i.b, ai);
            ai.w = A::I2FP(unsigned(reg(i.d)), b, (i.flags & maxwell::kSigned) != 0, A::Round(i.sub & 3), {}, g, gn);
            push(i, ai, i.d, 1, false, "I2FP");
            return;
        }
        case MOp::KIL:
            ai.w = A::Kill({}, g, gn);
            ai.block_end = true;
            push(i, ai, -1, 0, false, "KILL");
            return;
        case MOp::VMAD: {   // geometry: invocation * vertices per primitive + c, from SR_INVOCATION_INFO's bytes 0 and 2
            if (i.a.k != Opnd::R) refuse("VMAD of a non-register");
            const int info = reg(i.a.v);
            const A::Src c = i.c.k == Opnd::None ? A::Src::Zero() : src(i.c, ai);
            const unsigned s0 = scratch(0), s1 = scratch(1);
            alu(i, A::Prmt(s0, A::Src::R(unsigned(info)), A::Src::I(0x7772), A::Src::Zero()), {info}, int(s0), "PRMT (vertices)");
            alu(i, A::Prmt(s1, A::Src::R(unsigned(info)), A::Src::I(0x7770), A::Src::Zero()), {info}, int(s1), "PRMT (invocation)");
            std::vector<int> reads{int(s0), int(s1)};
            if (c.kind == A::Src::Kind::Reg && c.reg != A::RZ) reads.push_back(int(c.reg));
            alu(i, A::IMad(unsigned(reg(i.d)), A::Src::R(s0), A::Src::R(s1), c, false, {}, g, gn), reads, reg(i.d), "IMAD (vertex)");
            return;
        }
        case MOp::ISBERD: {   // the input vertex handle at an index
            if (i.a.k != Opnd::R) refuse("ISBERD of a non-register");
            const int a = reg(i.a.v);
            ai.reads.push_back(a);
            ai.w = A::Isberd(unsigned(reg(i.d)), unsigned(a), 0, false, false, {}, g, gn);
            push(i, ai, i.d, 1, true, "ISBERD");
            return;
        }
        case MOp::VSETP: {   // geometry: vertex aux is past the primitive's count (vertices <= aux)
            if (i.a.k != Opnd::R) refuse("VSETP of a non-register");
            const int info = reg(i.a.v);
            const unsigned s0 = scratch(0);
            alu(i, A::Prmt(s0, A::Src::R(unsigned(info)), A::Src::I(0x7772), A::Src::Zero()), {info}, int(s0), "PRMT (vertices)");
            AInst c;
            c.reads.push_back(int(s0));
            c.w = A::ISetP(pred(i.pd), A::Src::R(s0), A::Src::I(i.aux & 0xffff), A::ICmp(maxwell::LE), false, A::PredOp::And,
                           A::PT, false, {}, g, gn);
            c.pred_writes.push_back(int(pred(i.pd)));
            push(i, c, -1, 0, false, "ISETP.LE.U32 (past the count)");
            past_count_.insert(i.pd);
            return;
        }
        case MOp::OUT: {   // OUT.EMIT / OUT.CUT d, handle, RZ: d the next output handle
            if (i.a.k != Opnd::R) refuse("OUT of a non-register handle");
            const int h = reg(i.a.v);
            ai.reads.push_back(h);
            const unsigned how = i.aux & 3;
            if (!how) refuse("OUT without EMIT or CUT");
            ai.w = A::Out(how == 1 ? A::OutType::Emit : how == 2 ? A::OutType::Cut : A::OutType::EmitThenCut,
                          unsigned(reg(i.d)), unsigned(h), A::Src::Zero(), {}, g, gn);
            push(i, ai, i.d, 1, true, how == 1 ? "OUT.EMIT" : how == 2 ? "OUT.CUT" : "OUT.EMIT_THEN_CUT");
            return;
        }
        case MOp::EXIT:
            if (geometry_) {   // OUT.FINAL on the current output handle (the EXIT's one use) first
                if (i.uses.size() != 1) refuse("a geometry EXIT without its output handle");
                AInst f;
                const int h = reg(i.uses[0]);
                if (h != 255) f.reads.push_back(h);
                f.w = A::Out(A::OutType::Final, A::RZ, unsigned(h), A::Src::Zero(), {}, g, gn);
                f.guard = g; f.guard_neg = gn;
                if (i.guard != maxwell::kPT) f.pred_reads.push_back(int(g));
                f.variable = true;
                f.text = "OUT.FINAL";
                out.push_back(f);
            }
            ai.w = A::Exit({}, g, gn);
            for (int32_t u : i.uses) ai.reads.push_back(reg(u));
            ai.block_end = true;
            push(i, ai, -1, 0, false, "EXIT");
            return;
        case MOp::BRA:
            ai.w = A::Bra(0, {}, g, gn);
            ai.branch_label = i.target;
            ai.block_end = true;
            push(i, ai, -1, 0, false, "BRA");
            return;
        case MOp::SSY: {   // BSSY Bn, label: the region reconverges at the label (BSYNC there)
            const unsigned b = unsigned(barriers_.size());
            if (b >= 16) refuse("more than 16 nested reconvergence regions");
            barriers_.push_back(b);
            sync_barrier_[i.target] = b;
            ai.w = Bssy(b);
            ai.bssy = true;
            ai.barrier = b;
            ai.branch_label = i.target;
            ai.block_end = true;
            push(i, ai, -1, 0, false, "BSSY");
            return;
        }
        case MOp::SYNC:    // the end of a path inside the region: go to the reconvergence point
            ai.w = A::Bra(0, {}, g, gn);
            ai.branch_label = i.target;
            ai.block_end = true;
            push(i, ai, -1, 0, false, "BRA (SYNC)");
            return;
        case MOp::PBK: {   // a loop: BSSY Bn, exit; the loop reconverges at its exit (BSYNC there)
            const unsigned b = unsigned(barriers_.size());
            if (b >= 16) refuse("more than 16 nested reconvergence regions");
            barriers_.push_back(b);
            sync_barrier_[i.target] = b;
            loop_depth_[i.target] = barriers_.size();
            loops_.push_back(barriers_.size());
            ai.w = Bssy(b);
            ai.bssy = true;
            ai.barrier = b;
            ai.branch_label = i.target;
            ai.block_end = true;
            push(i, ai, -1, 0, false, "BSSY (PBK)");
            return;
        }
        case MOp::PCNT:    // continue points need no barrier: the threads stay in the loop
            return;
        case MOp::BRK: case MOp::CONT: {   // out of the regions opened inside the loop, then to its exit / continue point
            if (loops_.empty()) refuse(i.op == MOp::BRK ? "BRK outside a loop" : "CONT outside a loop");
            leave_barriers(i, loops_.back());
            ai.w = A::Bra(0, {}, g, gn);
            ai.branch_label = i.target;
            ai.block_end = true;
            push(i, ai, -1, 0, false, i.op == MOp::BRK ? "BRA (BRK)" : "BRA (CONT)");
            return;
        }
        case MOp::NOP:
            return;
        default: break;
        }
        static const char* names[] = {"Label", "MOV", "MOV32I", "SEL", "FADD", "FADD32I", "FMUL", "FMUL32I", "FFMA",
            "FMNMX", "FSET", "FSETP", "MUFU", "RRO", "F2F", "F2I", "I2F", "I2I", "FSWZADD", "IADD", "IADD32I",
            "ISCADD", "XMAD", "IMNMX", "ISET", "ISETP", "LOP", "LOP32I", "SHL", "SHR", "BFE", "BFI", "POPC", "FLO",
            "PSETP", "LDC", "LDG", "STG", "LDS", "STS", "LDL", "STL", "ATOM", "ATOMS", "BAR", "MEMBAR", "SULD",
            "SUST", "SUATOM", "IPA", "ALD", "AST", "S2R", "VMAD", "ISBERD", "VSETP", "OUT", "TEX", "TLD", "TLD4",
            "TXQ", "TXD", "PIXLD", "SHFL", "KIL", "EXIT", "BRA", "SSY", "SYNC", "PBK", "BRK", "PCNT", "CONT", "NOP"};
        const size_t k = size_t(i.op);
        refuse(std::string("no Ampere translation for ") + (k < sizeof names / sizeof names[0] ? names[k] : "an instruction") + " yet");
    }

    // Encodings this file needs that compiler/ampere_encode.h does not have yet; each was checked with
    // nvdisasm -b SM86 (tests/ampere_write_test.cpp lists the expected text).
    static A::Word Sel(unsigned d, A::Src a, A::Src b, unsigned p, bool pn, unsigned g, bool gn) {
        A::Encoder e;
        e.Alu(0x007, d, &a, &b, nullptr);
        e.w.Set(87, 3, p); e.w.Bit(90, pn);
        return A::Finish(e, {}, g, gn);
    }
    // TEX.B, the non-split form: Rd..Rd+n-1 (mask), coordinates from Ra, handle (then LOD/bias, offset, reference)
    // from Rb. Maxwell texture types to Ampere dimensions; LOD modes as NAK's set_tex_lod_mode.
    static A::Word TexB(unsigned d, unsigned a, unsigned b, uint8_t type, uint32_t mask, uint32_t flags, unsigned g,
                        bool gn) {
        static const unsigned dim[8] = {0 /*1D*/, 4 /*ARRAY1D*/, 1 /*2D*/, 5 /*ARRAY2D*/, 2 /*3D*/, 0, 3 /*CUBE*/, 7};
        if (type == 5 || type > 7) refuse("texture type");
        A::Encoder e;
        e.w.Set(0, 12, 0x361);
        e.w.Bit(59, true);   // .B
        e.w.Set(16, 8, d);
        // The split (.SCR) form the game uses for every 2D lookup: dst0 gets the first two components, dst1 the
        // next two; dst1 = dst0 + 2 keeps them consecutive, as the Maxwell destination group expects.
        const unsigned n = (mask & 1) + ((mask >> 1) & 1) + ((mask >> 2) & 1) + ((mask >> 3) & 1);
        e.w.Set(64, 8, n > 2 ? d + 2 : 255);
        e.w.Set(24, 8, a);
        e.w.Set(32, 8, b);
        e.w.Set(81, 3, A::PT);
        e.w.Bit(60, true);
        e.w.Set(61, 3, dim[type]);
        e.w.Set(72, 4, mask & 15);
        e.w.Bit(76, (flags & maxwell::kOffset) != 0);
        e.w.Bit(78, (flags & maxwell::kDepthCompare) != 0);
        const unsigned lod = (flags & maxwell::kLodZero) ? 1 : (flags & maxwell::kBias) ? 2 : (flags & maxwell::kLod) ? 3 : 0;
        e.w.Set(87, 3, lod);
        e.w.Set(84, 3, 1);   // eviction priority normal, as 11,226 of the game's 11,240 texture instructions (0 = .EF)
        return A::Finish(e, {}, g, gn);
    }
    // FRND: opcode 0x107, source and destination size 32-bit (log2 of bytes: 2), rounding in bits 78-79 (NAK; the
    // game's FRND.FLOOR words match).
    static A::Word Frnd(unsigned d, A::Src s, unsigned rnd, bool ftz, unsigned g, bool gn) {
        A::Encoder e;
        e.Alu(0x107, d, nullptr, &s, nullptr);
        e.w.Set(84, 2, 2);
        e.w.Set(75, 2, 2);
        e.w.Set(78, 2, rnd);
        e.w.Bit(80, ftz);
        return A::Finish(e, {}, g, gn);
    }
    static A::Word Bssy(unsigned b) {
        A::Encoder e;
        e.w.Set(0, 12, 0x945);
        e.w.Set(16, 4, b);
        e.w.Set(87, 3, A::PT);
        return A::Finish(e, {}, A::PT, false);
    }
    static A::Word Bsync(unsigned b) {
        A::Encoder e;
        e.w.Set(0, 12, 0x941);
        e.w.Set(16, 4, b);
        e.w.Set(87, 3, A::PT);
        return A::Finish(e, {}, A::PT, false);
    }
};

// Conservative scheduling. Fixed-latency results: the next reader issues at least kRegLatency (kPredLatency for
// predicates) cycles after the writer, by the writer's and following instructions' stall counts. Variable-latency
// results: a write scoreboard that readers and later writers wait on; their sources: a read scoreboard that later
// writers of those registers wait on. Around branches, labels, EXIT and KILL everything settles: all fixed results
// ready and all scoreboards waited on.
void schedule(std::vector<AInst>& code, const std::vector<int>& label_at) {
    std::set<int> starts;
    for (int l : label_at) if (l >= 0) starts.insert(l);
    std::map<int, unsigned> ready, pready;            // register -> cycle it can be read
    std::array<std::set<int>, 6> wpend, rpend, ppend; // scoreboard -> registers (predicates in ppend)
    std::array<bool, 6> busy{};
    unsigned next = 0;
    unsigned issue = 0;   // the cycle the previous instruction issued
    auto all_ready = [&]() {
        unsigned last = 0;
        for (auto& rc : ready) last = std::max(last, rc.second);
        for (auto& rc : pready) last = std::max(last, rc.second);
        return last;
    };
    for (size_t k = 0; k < code.size(); ++k) {
        AInst& in = code[k];
        A::Deps d;
        const bool boundary = in.block_end || starts.count(int(k)) || (k > 0 && code[k - 1].block_end);
        // Waits on scoreboards.
        for (unsigned s = 0; s < 6; ++s) {
            if (!busy[s]) continue;
            bool hit = boundary;
            for (int r : in.reads) hit |= wpend[s].count(r) != 0;
            for (int r : in.writes) hit |= wpend[s].count(r) != 0 || rpend[s].count(r) != 0;
            for (int r : in.pred_reads) hit |= ppend[s].count(r) != 0;
            if (hit) { d.wait_mask |= 1u << s; busy[s] = false; wpend[s].clear(); rpend[s].clear(); ppend[s].clear(); }
        }
        // Fixed latencies: the cycle this instruction can issue, set by the previous instruction's stall.
        auto at = [](const std::map<int, unsigned>& m, int r) { auto it = m.find(r); return it == m.end() ? 0u : it->second; };
        unsigned need = k ? issue + 1 : 0;
        for (int r : in.reads) need = std::max(need, at(ready, r));
        for (int r : in.writes) need = std::max(need, at(ready, r));
        for (int r : in.pred_reads) need = std::max(need, at(pready, r));
        if (boundary) need = std::max(need, all_ready());
        if (k > 0) {
            const unsigned stall = std::min(15u, std::max(1u, need - issue));
            code[k - 1].w.Set(105, 4, stall);
            code[k - 1].w.Bit(109, stall <= 2);
            if (need - issue > 15) refuse("a dependency longer than one stall count");   // latencies here are at most 14
            issue += stall;
        }
        const unsigned cycle = issue;
        // Scoreboards for this instruction's variable-latency result and late-read sources.
        auto take = [&]() {
            for (unsigned t = 0; t < 6; ++t) {
                const unsigned s = (next + t) % 6;
                if (!busy[s]) { next = (s + 1) % 6; busy[s] = true; return s; }
            }
            const unsigned s = next;   // all in use: wait for the oldest choice first
            d.wait_mask |= 1u << s;
            wpend[s].clear(); rpend[s].clear(); ppend[s].clear();
            next = (s + 1) % 6;
            return s;
        };
        if (in.variable) {
            if (!in.writes.empty() || !in.pred_writes.empty()) {   // a store (AST) writes nothing: no write scoreboard
                const unsigned ws = take();
                d.write_barrier = ws;
                for (int r : in.writes) wpend[ws].insert(r);
                for (int r : in.pred_writes) ppend[ws].insert(r);
            }
            if (!in.reads.empty()) {
                const unsigned rs = take();
                d.read_barrier = rs;
                for (int r : in.reads) rpend[rs].insert(r);
            }
            for (int r : in.writes) ready.erase(r);
        } else {
            for (int r : in.writes) ready[r] = cycle + kRegLatency;
            for (int r : in.pred_writes) pready[r] = cycle + kPredLatency;
        }
        in.w.Set(110, 3, d.write_barrier);
        in.w.Set(113, 3, d.read_barrier);
        in.w.Set(116, 6, d.wait_mask);
        in.w.Set(122, 4, 0);
        in.w.Set(105, 4, 1);
        in.w.Bit(109, true);
    }
    // The last instruction (EXIT) gets the longest stall so nothing after it matters.
    if (!code.empty()) { code.back().w.Set(105, 4, 15); code.back().w.Bit(109, false); }
}

std::string listing(const std::vector<AInst>& code) {
    std::ostringstream s;
    for (size_t k = 0; k < code.size(); ++k) {
        const auto& w = code[k].w;
        char line[160];
        std::snprintf(line, sizeof line, "%04zx %08x %08x %08x %08x  st%u wb%u rb%u wm%02x  %s\n", k * 16, w.words[0],
                      w.words[1], w.words[2], w.words[3], w.Get(105, 4), w.Get(110, 3), w.Get(113, 3), w.Get(116, 6),
                      code[k].text.c_str());
        s << line;
    }
    return s.str();
}

// Program header, version 4 (32 words), from the Maxwell header the Switch 1 backend writes: the same input and
// output maps; type, version and SASS version as the game's 2.0.0 programs have them.
std::array<uint32_t, 32> header(const Function& f, uint32_t local_bytes) {
    std::array<uint32_t, 32> w{};
    uint8_t* b = reinterpret_cast<uint8_t*>(w.data());
    w[1] = local_bytes & 0xffffff;
    if (f.stage == Stage::Vertex) {
        // As 2.0.0's vertex programs (docs/SWITCH2_VERTEX_NOTES.md): word 0 0x000c0481, the input and output maps at
        // the bytes the Maxwell header uses (inputs from 0x14, outputs from 0x32, one bit per attribute word).
        w[0] = 0x000c0481u;
        if (f.builtin_used[size_t(Builtin::InstanceId)]) b[0x2b] |= 0x40;
        if (f.builtin_used[size_t(Builtin::VertexId)]) b[0x2b] |= 0x80;
        for (const auto& in : f.inputs) {
            if (in.location < 0 || in.location >= 32) refuse("input location out of range");
            b[0x18 + in.location / 2] |= uint8_t(0xF << (4 * (in.location % 2)));
        }
        if (f.position) b[0x35] |= 0xF0;
        if (f.layer) b[0x35] |= 0x02;
        for (const auto& o : f.outputs) {
            if (o.location < 0 || o.location >= 32) refuse("output location out of range");
            b[0x36 + o.location / 2] |= uint8_t(0xF << (4 * (o.location % 2)));
        }
        return w;
    }
    if (f.stage == Stage::Geometry) {
        // As 2.0.0's geometry programs: word 0 0x100c1081 (version 4, stream 0 out), threads per input primitive,
        // output topology and vertex count, the maps as for vertex programs.
        w[0] = 0x100c1081u;
        w[2] = uint32_t(std::max(f.invocations, 1)) << 24;
        const uint32_t topology = f.prim_out == "POINTS" ? 1 : f.prim_out == "LINE_STRIP" ? 6 : f.prim_out == "TRIANGLE_STRIP" ? 7 : 0;
        if (!topology) refuse("geometry output primitive " + f.prim_out);
        w[3] |= topology << 24;
        w[4] = uint32_t(f.vertices_out) & 0xfff;
        for (const Node& n : f.nodes)
            if (n.op == Op::Builtin && Builtin(n.sub) == Builtin::GsPosition) b[0x17] |= 0xF0;
        for (const auto& in : f.inputs) {
            if (in.location < 0 || in.location >= 32) refuse("input location out of range");
            b[0x18 + in.location / 2] |= uint8_t(0xF << (4 * (in.location % 2)));
        }
        if (f.position) b[0x35] |= 0xF0;
        if (f.layer) b[0x35] |= 0x02;
        for (const auto& o : f.outputs) {
            if (o.location < 0 || o.location >= 32) refuse("output location out of range");
            b[0x36 + o.location / 2] |= uint8_t(0xF << (4 * (o.location % 2)));
        }
        return w;
    }
    if (f.stage != Stage::Fragment) refuse("program headers for compute programs are not written yet");
    w[0] = 0x000c5482u | (f.kills ? 1u << 15 : 0u) | (local_bytes ? 1u << 26 : 0u);   // as 2.0.0's pixel programs
    b[0x17] = 0x80;   // position w
    if (f.builtin_used[size_t(Builtin::FragCoord)]) b[0x17] |= 0x70;
    for (const auto& in : f.inputs) {
        if (in.location < 0 || in.location >= 32) refuse("input location out of range");
        const uint8_t mode = in.interpolation == "flat" ? 1 : in.interpolation == "noperspective" ? 3 : 2;
        b[0x18 + in.location] = uint8_t(mode | mode << 2 | mode << 4 | mode << 6);
    }
    for (const auto& c : f.colors) {
        if (c.location < 0 || c.location >= 8) refuse("colour output out of range");
        w[18] |= 0xFu << (4 * c.location);
    }
    if (f.frag_depth) w[19] |= 2;
    return w;
}

} // namespace

void write_ampere(const Function& f, AmpereResult& r, const AmpereOptions& o) {
    bool memory = false;
    uint32_t local_bytes = 0;
    maxwell::Program p = select_maxwell(f, memory, local_bytes);
    if (local_bytes) refuse("local memory");
    const bool geometry = f.stage == Stage::Geometry;
    if (geometry)   // the selector names the output handle current at EXIT (aux2 = vreg + 1); EXIT reads it here
        for (Inst& i : p.code)
            if (i.op == MOp::EXIT) i.uses = {i.aux2 ? int32_t(i.aux2 - 1) : maxwell::kRZ};
    maxwell::optimize(p);
    const Allocation a = maxwell::allocate(p);
    p.code.erase(std::remove_if(p.code.begin(), p.code.end(), [&](const Inst& i) {
                     return i.op == MOp::MOV && i.guard == maxwell::kPT && i.b.k == Opnd::R && !i.b.neg && !i.b.abs &&
                            i.d >= 0 && a.reg[size_t(i.d)] == (i.b.v == maxwell::kRZ ? 255 : a.reg[size_t(i.b.v)]);
                 }),
                 p.code.end());
    Translator t(p, a, o, geometry);
    t.run();
    auto& code = t.out;
    // The branch to itself that ends every program.
    AInst self;
    self.w = A::Bra(-16);
    self.text = "BRA (end)";
    code.push_back(self);
    schedule(code, t.label_at);
    // Branch targets.
    for (size_t k = 0; k < code.size(); ++k) {
        AInst& in = code[k];
        if (in.branch_label < 0) continue;
        const int target = t.label_at.at(size_t(in.branch_label));
        if (target < 0) refuse("a branch to a label that was never placed");
        const std::int64_t offset = (std::int64_t(target) - std::int64_t(k) - 1) * 16;
        if (in.bssy) in.w.Set(34, 48, std::uint64_t(offset / 4) & ((std::uint64_t(1) << 48) - 1));
        else in.w.Set(34, 48, std::uint64_t(offset / 4) & ((std::uint64_t(1) << 48) - 1));
    }
    // Layout: header, instructions, NOPs to a multiple of 128 bytes, a zeroed 128-byte tail, all a multiple of 256.
    const auto sph = header(f, local_bytes);
    std::vector<uint8_t> bytes(128);
    std::memcpy(bytes.data(), sph.data(), 128);
    for (const auto& in : code) {
        const size_t at = bytes.size();
        bytes.resize(at + 16);
        std::memcpy(bytes.data() + at, in.w.words.data(), 16);
    }
    while (bytes.size() % 128) {
        const A::Word nop = A::Nop();
        const size_t at = bytes.size();
        bytes.resize(at + 16);
        std::memcpy(bytes.data() + at, nop.words.data(), 16);
    }
    const size_t table = bytes.size();
    bytes.resize(table + 128, 0);
    while (bytes.size() % 256) {   // more NOPs before the tail keep the tail last
        bytes.insert(bytes.begin() + std::ptrdiff_t(table), 16, 0);
        const A::Word nop = A::Nop();
        std::memcpy(bytes.data() + table, nop.words.data(), 16);
    }
    r.code = std::move(bytes);
    r.table_offset = uint32_t(r.code.size() - 128);
    r.registers = t.registers();
    r.instructions = uint32_t(code.size());
    r.listing = listing(code);
}

} // namespace nvasm::ir

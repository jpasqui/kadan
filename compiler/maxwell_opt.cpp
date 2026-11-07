// Maxwell backend: machine IR clean-up before register allocation (MAXWELL_BACKEND.md).
//
// Instruction selection is simple on purpose: every constant and uniform word is
// moved into a register, negation is a sign-bit flip, and every comparison makes a
// mask. These passes fold that back into the forms the hardware has:
//   - operand folding: a constant becomes an immediate, a uniform word a
//     constant-buffer operand, a sign flip or absolute value an operand modifier;
//     commutative instructions swap to put the folded operand where it can go;
//   - predicates straight from comparisons (FSET/ISET then ISETP.NE → xSETP);
//   - copy coalescing: a value computed only to be moved is computed in place;
//   - dead code: instructions whose results are never read.
#include "maxwell_mir.h"

#include <algorithm>
#include <functional>
#include <cstdlib>
#include <map>
#include <string>

namespace nvasm::maxwell {

namespace {

bool side_effects(Op op) {
    switch (op) {
    case Op::Label: case Op::STG: case Op::STS: case Op::STL: case Op::AST: case Op::SUST: case Op::ATOM: case Op::ATOMS:
    case Op::SUATOM: case Op::BAR: case Op::MEMBAR: case Op::KIL: case Op::EXIT: case Op::BRA: case Op::SSY: case Op::SYNC:
    case Op::PBK: case Op::BRK: case Op::PCNT: case Op::CONT: case Op::NOP: case Op::OUT:
        return true;
    default:
        return false;
    }
}

struct Uses {
    std::vector<int32_t> defs, uses;        // per vreg: number of definitions and reads
    std::vector<int32_t> pdefs, puses;
    std::vector<int32_t> def_at;            // vreg -> the defining instruction when there is exactly one
};

void count(const Program& p, Uses& u) {
    u.defs.assign(size_t(p.vregs), 0);
    u.uses.assign(size_t(p.vregs), 0);
    u.def_at.assign(size_t(p.vregs), -1);
    u.pdefs.assign(size_t(p.pregs), 0);
    u.puses.assign(size_t(p.pregs), 0);
    for (size_t k = 0; k < p.code.size(); ++k) {
        const Inst& i = p.code[k];
        for (const Opnd* o : {&i.a, &i.b, &i.c})
            if (o->k == Opnd::R && o->v >= 0)
                for (int j = 0; j < o->n; ++j) ++u.uses[size_t(o->v + j)];
        for (int32_t v : i.uses) ++u.uses[size_t(v)];
        if (i.d >= 0)
            for (int j = 0; j < i.dn; ++j) {
                ++u.defs[size_t(i.d + j)];
                u.def_at[size_t(i.d + j)] = int32_t(k);
                if (i.guard != kPT) ++u.uses[size_t(i.d + j)];
            }
        for (int32_t q : {i.guard, i.pa, i.pb})
            if (q >= 0) ++u.puses[size_t(q)];
        if (i.pd >= 0) ++u.pdefs[size_t(i.pd)];
    }
}

// Can operand slot `which` (0 = a, 1 = b, 2 = c) of `i` take this kind of operand?
bool takes_imm(const Inst& i, int which) {
    if (which != 1) return false;
    switch (i.op) {
    case Op::FADD: case Op::FMUL: case Op::FFMA: case Op::FMNMX: case Op::FSET: case Op::FSETP: case Op::IADD:
    case Op::ISCADD: case Op::IMNMX: case Op::ISET: case Op::ISETP: case Op::LOP: case Op::SHL: case Op::SHR: case Op::SEL:
    case Op::MOV:
        return true;
    default:
        return false;
    }
}
bool takes_cb(const Inst& i, int which) {
    if (i.op == Op::FFMA) return which == 1 || which == 2;   // cr / rc forms
    if (which != 1) return false;
    switch (i.op) {
    case Op::FADD: case Op::FMUL: case Op::FMNMX: case Op::FSET: case Op::FSETP: case Op::IADD: case Op::ISCADD:
    case Op::IMNMX: case Op::ISET: case Op::ISETP: case Op::LOP: case Op::SHL: case Op::SHR: case Op::SEL: case Op::MOV:
    case Op::I2F: case Op::F2I: case Op::F2F: case Op::RRO:
        return true;
    default:
        return false;
    }
}
bool float_operands(Op op) {
    return op == Op::FADD || op == Op::FMUL || op == Op::FFMA || op == Op::FMNMX || op == Op::FSET || op == Op::FSETP;
}
// Which float operand slots accept negate / absolute-value modifiers.
bool takes_neg(const Inst& i, int which) {
    switch (i.op) {
    case Op::FADD: case Op::FMNMX: case Op::FSET: case Op::FSETP: return which <= 1;
    case Op::FMUL: return which <= 1;             // folded into the product's sign
    case Op::FFMA: return true;                   // a or b: the product's sign; c
    case Op::MUFU: return which == 0;
    case Op::F2F: case Op::RRO: return which == 1;
    default: return false;
    }
}
bool takes_abs(const Inst& i, int which) {
    switch (i.op) {
    case Op::FADD: case Op::FMNMX: case Op::FSET: case Op::FSETP: return which <= 1;
    case Op::MUFU: return which == 0;
    case Op::F2F: case Op::RRO: return which == 1;
    default: return false;
    }
}
bool representable(const Inst& i, uint32_t bits) {
    if (float_operands(i.op)) {
        if ((bits & 0xfff) == 0) return true;
        if (i.op == Op::FADD && (i.flags & kSat)) return false;   // FADD32I has no saturate
        return i.op == Op::FADD || i.op == Op::FMUL;   // their 32-bit immediate forms
    }
    if (i.op == Op::MOV) return true;                // MOV32I
    const int32_t s = int32_t(bits);
    if (i.op == Op::SHL || i.op == Op::SHR) return bits < 32;
    return s >= -(1 << 19) && s < (1 << 19);
}
bool commutes(const Inst& i) {
    switch (i.op) {
    case Op::FADD: case Op::FMUL: case Op::FMNMX: case Op::IADD: case Op::IMNMX: return true;
    case Op::LOP: return (i.flags & (kInvA | kInvB)) == 0 && (i.sub == uint8_t(Lop::AND) || i.sub == uint8_t(Lop::OR) || i.sub == uint8_t(Lop::XOR));
    case Op::FSET: case Op::FSETP: case Op::ISET: case Op::ISETP: return true;   // with the comparison mirrored
    default: return false;
    }
}
uint8_t mirrored(uint8_t cmp) {
    switch (cmp & 7) {   // LT <-> GT, LE <-> GE; the unordered bit (8) is kept
    case LT: return uint8_t((cmp & 8) | GT);
    case GT: return uint8_t((cmp & 8) | LT);
    case LE: return uint8_t((cmp & 8) | GE);
    case GE: return uint8_t((cmp & 8) | LE);
    default: return cmp;
    }
}

Opnd& slot(Inst& i, int which) { return which == 0 ? i.a : which == 1 ? i.b : i.c; }

// NVASM_MAXWELL_PASSES (debugging) names the passes to run by letter: f operand
// folding (n modifiers, k constants, b uniform words, s swapping), p predicates,
// c coalescing, d dead code, u unread definitions, t texture results, m sinking.
// Default: all of them.
bool pass_on(char c) {
    static const std::string passes = [] {
        char* v = nullptr;
        size_t length = 0;
        std::string text = "fpcdunkbstm";
        if (_dupenv_s(&v, &length, "NVASM_MAXWELL_PASSES") == 0 && v) text = v;
        std::free(v);
        return text;
    }();
    return passes.find(c) != std::string::npos;
}

// One round of operand folding. Returns whether anything changed.

bool fold_operands(Program& p) {
    Uses u;
    count(p, u);
    bool changed = false;
    auto source = [&](const Opnd& o) -> const Inst* {   // the single, unguarded definition of a lone register
        if (o.k != Opnd::R || o.v < 0 || o.n != 1) return nullptr;
        if (u.defs[size_t(o.v)] != 1 || p.group[size_t(o.v)] != o.v || p.fixed[size_t(o.v)] >= 0) return nullptr;
        const int32_t at = u.def_at[size_t(o.v)];
        if (at < 0 || p.code[size_t(at)].guard != kPT) return nullptr;
        return &p.code[size_t(at)];
    };
    // A modifier reads the flipped register where the flip was used, so that register
    // must still hold the same value there: same block, not written in between.
    auto unchanged_since = [&](int32_t v, size_t from, size_t to) {
        if (v < 0) return true;
        for (size_t m = from + 1; m < to; ++m) {
            const Inst& x = p.code[m];
            if (x.op == Op::Label || x.op == Op::BRA || x.op == Op::SYNC || x.op == Op::BRK || x.op == Op::CONT) return false;
            if (x.d >= 0 && v >= x.d && v < x.d + x.dn) return false;
        }
        return true;
    };
    for (size_t k = 0; k < p.code.size(); ++k) {
        Inst& i = p.code[k];
        if (i.op == Op::Label) continue;
        // Sign flips and absolute values become operand modifiers.
        for (int w = 0; w < 3; ++w) {
            Opnd& o = slot(i, w);
            const Inst* d = source(o);
            if (!d || d->op != Op::LOP32I || d->a.k != Opnd::R) continue;
            const size_t at = size_t(d - p.code.data());
            if (at >= k || !unchanged_since(d->a.v, at, k)) continue;
            const uint32_t imm = uint32_t(d->b.v);
            const bool is_neg = d->sub == uint8_t(Lop::XOR) && imm == 0x80000000u;
            const bool is_abs = d->sub == uint8_t(Lop::AND) && imm == 0x7fffffffu;
            if (!pass_on('n')) continue;
            if (is_neg && takes_neg(i, w) && !o.abs) {
                const Opnd inner = d->a;
                o.v = inner.v;
                o.neg = !o.neg;
                changed = true;
            } else if (is_abs && takes_abs(i, w)) {
                o.v = d->a.v;
                o.abs = true;   // an outer negation stays: -|x|
                changed = true;
            }
        }
        // Constants and uniform words into operand B (or FFMA's C), swapping if that helps.
        auto foldable = [&](const Opnd& o, int w) -> int {   // 1 immediate, 2 constant buffer, 0 no
            const Inst* d = source(o);
            if (!d || o.abs || (o.neg && !float_operands(i.op))) return 0;
            if (d->op == Op::MOV32I && takes_imm(i, w) && pass_on('k')) {
                uint32_t bits = uint32_t(d->a.v);
                if (o.neg) bits ^= 0x80000000u;
                if (representable(i, bits)) return 1;
            }
            if (d->op == Op::MOV && d->b.k == Opnd::Cb && takes_cb(i, w) && !o.neg && pass_on('b')) return 2;
            return 0;
        };
        auto apply = [&](Opnd& o, int kind) {
            const Inst* d = source(o);
            if (kind == 1) {
                uint32_t bits = uint32_t(d->a.v);
                if (o.neg) bits ^= 0x80000000u;
                o = Opnd::imm(bits);
            } else {
                o = d->b;
            }
            changed = true;
        };
        if (i.op == Op::MUFU || i.op == Op::LDC || i.op == Op::LDG || i.op == Op::IPA || i.op == Op::XMAD) continue;
        const bool b_register = i.b.k == Opnd::R;
        int kb = b_register ? foldable(i.b, 1) : 0;
        if (kb) {
            apply(i.b, kb);
        } else if (i.op == Op::FFMA && i.c.k == Opnd::R && i.b.k == Opnd::R && foldable(i.c, 2) == 2) {
            apply(i.c, 2);
        } else if (commutes(i) && i.a.k == Opnd::R && b_register && pass_on('s')) {
            const int ka = foldable(i.a, 1);
            if (ka) {
                std::swap(i.a, i.b);
                if (i.op == Op::FSET || i.op == Op::FSETP || i.op == Op::ISET || i.op == Op::ISETP) i.cmp = mirrored(i.cmp);
                apply(i.b, ka);
            }
        }
        // A MOV of a folded constant is MOV32I.
        if (i.op == Op::MOV && i.b.k == Opnd::Imm) {
            i.op = Op::MOV32I;
            i.a = i.b;
            i.b = {};
        }
        // FMUL/FFMA: a negated A moves to B (the product's sign is one flag).
        if ((i.op == Op::FMUL || i.op == Op::FFMA) && i.a.neg) {
            i.a.neg = false;
            if (i.b.k == Opnd::Imm) i.b.v = int32_t(uint32_t(i.b.v) ^ 0x80000000u);
            else i.b.neg = !i.b.neg;
        }
    }
    return changed;
}

// FSET/ISET mask followed by ISETP.NE mask, RZ: the comparison writes the predicate itself.
bool fold_predicates(Program& p) {
    Uses u;
    count(p, u);
    bool changed = false;
    for (Inst& i : p.code) {
        if (i.op != Op::ISETP || i.cmp != NE || i.b.k != Opnd::R || i.b.v != kRZ || i.a.k != Opnd::R || i.a.v < 0) continue;
        if (i.pa != kPT || u.defs[size_t(i.a.v)] != 1) continue;
        const int32_t at = u.def_at[size_t(i.a.v)];
        if (at < 0) continue;
        const Inst& d = p.code[size_t(at)];
        if ((d.op != Op::FSET && d.op != Op::ISET) || d.guard != kPT || (d.flags & kBF)) continue;
        // The comparison's operands must still hold the same values here: only when
        // nothing between them writes those registers.
        const size_t here = size_t(&i - p.code.data());
        bool clean = true;
        for (size_t k = size_t(at) + 1; k < here && clean; ++k) {
            const Inst& m = p.code[k];
            if (m.op == Op::Label || m.op == Op::BRA || m.op == Op::SYNC || m.op == Op::BRK || m.op == Op::CONT) clean = false;
            if (m.d >= 0)
                for (int j = 0; j < m.dn; ++j)
                    for (const Opnd* o : {&d.a, &d.b})
                        if (o->k == Opnd::R && o->v == m.d + j) clean = false;
        }
        if (!clean) continue;
        const int32_t pd = i.pd;
        const int32_t guard = i.guard;
        const bool guard_neg = i.guard_neg;
        i = d;
        i.op = d.op == Op::FSET ? Op::FSETP : Op::ISETP;
        i.d = -1;
        i.pd = pd;
        i.guard = guard;
        i.guard_neg = guard_neg;
        changed = true;
    }
    return changed;
}

// MOV d, s where s was computed just for this: compute it into d instead.
bool coalesce(Program& p) {
    Uses u;
    count(p, u);
    bool changed = false;
    for (size_t k = 0; k < p.code.size(); ++k) {
        Inst& mv = p.code[k];
        if (mv.op != Op::MOV || mv.guard != kPT || mv.b.k != Opnd::R || mv.b.v < 0 || mv.b.neg || mv.b.abs) continue;
        const int32_t s = mv.b.v, d = mv.d;
        if (d < 0 || u.uses[size_t(s)] != 1 || u.defs[size_t(s)] != 1) continue;
        if (p.group[size_t(s)] != s || p.group[size_t(d)] != d || p.fixed[size_t(s)] >= 0 || p.fixed[size_t(d)] >= 0) continue;
        const int32_t at = u.def_at[size_t(s)];
        if (at < 0 || size_t(at) >= k) continue;
        Inst& def = p.code[size_t(at)];
        if (def.guard != kPT || def.dn != 1 || side_effects(def.op)) continue;
        // d must be neither read nor written between the definition and the move.
        bool clean = true;
        for (size_t m = size_t(at); m < k && clean; ++m) {
            const Inst& x = p.code[m];
            if (m > size_t(at) && (x.op == Op::Label || x.op == Op::BRA || x.op == Op::SYNC || x.op == Op::BRK ||
                                   x.op == Op::CONT || x.op == Op::SSY || x.op == Op::PBK || x.op == Op::PCNT)) clean = false;
            for (const Opnd* o : {&x.a, &x.b, &x.c})
                if (o->k == Opnd::R && o->v >= 0 && d >= o->v && d < o->v + o->n) clean = false;
            for (int32_t v : x.uses) if (v == d) clean = false;
            if (m > size_t(at) && x.d >= 0 && d >= x.d && d < x.d + x.dn) clean = false;
        }
        if (!clean) continue;
        def.d = d;
        mv.op = Op::NOP;   // removed by dead-code elimination below
        mv.d = -1;
        mv.b = {};
        changed = true;
    }
    return changed;
}

bool branches(Op op) { return op == Op::BRA || op == Op::SYNC || op == Op::BRK || op == Op::CONT || op == Op::EXIT; }

// Definitions no path reads before the next definition: liveness over the control
// flow graph, then a backward walk through each block. Removes unguarded,
// side-effect-free instructions that define only dead registers and predicates.
bool eliminate_unread(Program& p) {
    const size_t n = p.code.size();
    if (!n) return false;
    const size_t words = (size_t(p.vregs + p.pregs) + 63) / 64;
    const auto pbit = [&](int32_t q) { return size_t(p.vregs) + size_t(q); };
    // Blocks.
    std::vector<size_t> start{0};
    std::vector<int32_t> label_block(size_t(p.labels), -1);
    for (size_t k = 0; k < n; ++k) {
        if (k && (p.code[k].op == Op::Label || branches(p.code[k - 1].op)) && start.back() != k) start.push_back(k);
    }
    const size_t blocks = start.size();
    std::vector<size_t> end(blocks);
    for (size_t b = 0; b < blocks; ++b) end[b] = b + 1 < blocks ? start[b + 1] : n;
    for (size_t b = 0; b < blocks; ++b)
        for (size_t k = start[b]; k < end[b]; ++k)
            if (p.code[k].op == Op::Label) label_block[size_t(p.code[k].target)] = int32_t(b);
    std::vector<std::vector<size_t>> succ(blocks);
    for (size_t b = 0; b < blocks; ++b) {
        const Inst& last = p.code[end[b] - 1];
        bool falls = true;
        if (branches(last.op)) {
            if (last.op != Op::EXIT && last.target >= 0 && label_block[size_t(last.target)] >= 0)
                succ[b].push_back(size_t(label_block[size_t(last.target)]));
            falls = last.guard != kPT;
        }
        if (falls && b + 1 < blocks) succ[b].push_back(b + 1);
    }
    using Set = std::vector<uint64_t>;
    auto set = [](Set& s, size_t v) { s[v / 64] |= 1ull << (v % 64); };
    auto clear = [](Set& s, size_t v) { s[v / 64] &= ~(1ull << (v % 64)); };
    auto has = [](const Set& s, size_t v) { return (s[v / 64] >> (v % 64)) & 1; };
    // Transfer through one instruction, backwards: kill unguarded definitions, add reads.
    auto transfer = [&](const Inst& i, Set& live) {
        if (i.guard == kPT) {
            if (i.d >= 0) for (int j = 0; j < i.dn; ++j) clear(live, size_t(i.d + j));
            if (i.pd >= 0) clear(live, pbit(i.pd));
        }
        for (const Opnd* o : {&i.a, &i.b, &i.c})
            if (o->k == Opnd::R && o->v >= 0) for (int j = 0; j < o->n; ++j) set(live, size_t(o->v + j));
        for (int32_t v : i.uses) set(live, size_t(v));
        for (int32_t q : {i.guard, i.pa, i.pb}) if (q >= 0) set(live, pbit(q));
        if (i.guard != kPT) {   // a predicated write keeps the old value where it does not happen
            if (i.d >= 0) for (int j = 0; j < i.dn; ++j) set(live, size_t(i.d + j));
            if (i.pd >= 0) set(live, pbit(i.pd));
        }
    };
    std::vector<Set> in(blocks, Set(words, 0)), out(blocks, Set(words, 0));
    for (bool changed = true; changed;) {
        changed = false;
        for (size_t b = blocks; b-- > 0;) {
            Set o(words, 0);
            for (size_t s2 : succ[b]) for (size_t w = 0; w < words; ++w) o[w] |= in[s2][w];
            Set live = o;
            for (size_t k = end[b]; k-- > start[b];) transfer(p.code[k], live);
            if (live != in[b] || o != out[b]) { in[b] = std::move(live); out[b] = std::move(o); changed = true; }
        }
    }
    std::vector<bool> dead(n, false);
    bool any = false;
    for (size_t b = 0; b < blocks; ++b) {
        Set live = out[b];
        for (size_t k = end[b]; k-- > start[b];) {
            const Inst& i = p.code[k];
            if (!side_effects(i.op) && i.guard == kPT && (i.d >= 0 || i.pd >= 0)) {
                bool used = false;
                if (i.d >= 0) for (int j = 0; j < i.dn; ++j) used |= has(live, size_t(i.d + j));
                if (i.pd >= 0) used |= has(live, pbit(i.pd));
                if (!used) { dead[k] = any = true; continue; }
            }
            transfer(i, live);
        }
    }
    if (!any) return false;
    std::vector<Inst> kept;
    kept.reserve(n);
    for (size_t k = 0; k < n; ++k) if (!dead[k]) kept.push_back(std::move(p.code[k]));
    p.code = std::move(kept);
    return true;
}

// Instructions without side effects whose results nothing reads, until none are left.
bool eliminate_dead(Program& p) {
    bool changed = false;
    for (bool again = true; again;) {
        again = false;
        Uses u;
        count(p, u);
        std::vector<Inst> kept;
        kept.reserve(p.code.size());
        for (const Inst& i : p.code) {
            bool dead = false;
            if (i.op == Op::NOP && i.a.k == Opnd::None) dead = true;
            else if (!side_effects(i.op)) {
                bool any = false;
                if (i.d >= 0)
                    for (int j = 0; j < i.dn; ++j) any |= u.uses[size_t(i.d + j)] > (i.guard != kPT ? 1 : 0);
                if (i.pd >= 0) any |= u.puses[size_t(i.pd)] > (i.guard != kPT ? 1 : 0);
                dead = !any;
            }
            if (dead) {
                again = changed = true;
                continue;
            }
            kept.push_back(i);
        }
        p.code = std::move(kept);
    }
    return changed;
}

// A texture instruction writes only the components its mask names, packed from the
// first destination register: drop the components nothing reads, so the result
// group shrinks (TLD4 always returns four).
bool shrink_texture_results(Program& p) {
    Uses u;
    count(p, u);
    bool changed = false;
    std::map<int32_t, int32_t> rename;   // old vreg -> new
    for (Inst& i : p.code) {
        if ((i.op != Op::TEX && i.op != Op::TLD && i.op != Op::TXQ) || i.d < 0 || i.dn < 2) continue;
        // Component c of the mask sits at the register counting the set bits below it.
        std::vector<int> comps;
        for (int c = 0; c < 4; ++c)
            if (i.aux >> c & 1) comps.push_back(c);
        if (int(comps.size()) != i.dn) continue;
        uint32_t mask = 0;
        std::vector<int32_t> kept;
        for (size_t k = 0; k < comps.size(); ++k)
            if (u.uses[size_t(i.d + int32_t(k))] > 0) mask |= 1u << comps[k], kept.push_back(i.d + int32_t(k));
        if (mask == i.aux || kept.empty()) continue;
        const int32_t g = kept.size() == 1 ? p.new_vreg() : p.new_group(int(kept.size()));
        for (size_t k = 0; k < kept.size(); ++k) rename[kept[k]] = g + int32_t(k);
        i.d = g;
        i.dn = uint8_t(kept.size());
        i.aux = mask;
        changed = true;
    }
    if (!changed) return false;
    for (Inst& i : p.code) {
        for (Opnd* o : {&i.a, &i.b, &i.c})
            if (o->k == Opnd::R && o->v >= 0 && o->n == 1) {
                auto it = rename.find(o->v);
                if (it != rename.end()) o->v = it->second;
            }
        for (int32_t& v : i.uses) {
            auto it = rename.find(v);
            if (it != rename.end()) v = it->second;
        }
    }
    return true;
}

} // namespace

// Fixed-latency instructions whose one result has one reader later in the same
// block move to just before that reader, so the value holds a register for less
// time. Only where the sources stay live until then anyway, so no other value
// lives longer. Loads and other variable-latency instructions stay where they
// are, early, so their latency stays hidden.
bool sink(Program& p) {
    auto movable = [](const Inst& i) {
        switch (i.op) {
        case Op::MOV: case Op::MOV32I: case Op::SEL: case Op::FADD: case Op::FADD32I: case Op::FMUL: case Op::FMUL32I:
        case Op::FFMA: case Op::FMNMX: case Op::FSET: case Op::RRO: case Op::IADD: case Op::IADD32I: case Op::ISCADD:
        case Op::XMAD: case Op::IMNMX: case Op::ISET: case Op::LOP: case Op::LOP32I: case Op::SHL: case Op::SHR:
        case Op::BFE: case Op::BFI:
            return i.guard == kPT && i.pd < 0 && i.pa < 0 && i.pb < 0 && i.d >= 0 && i.dn == 1;
        default:
            return false;
        }
    };
    Uses u;
    count(p, u);
    const size_t n = p.code.size();
    std::vector<int32_t> block(n);
    std::vector<int32_t> def_at(size_t(p.vregs), -1), reader(size_t(p.vregs), -1), last_read(size_t(p.vregs), -1);
    int32_t b = 0;
    for (size_t k = 0; k < n; ++k) {
        const Inst& i = p.code[k];
        if (i.op == Op::Label) ++b;
        block[k] = b;
        if (branches(i.op) || i.op == Op::SSY || i.op == Op::PBK || i.op == Op::PCNT || i.op == Op::KIL ||
            i.op == Op::BAR || i.op == Op::MEMBAR)
            ++b;
        auto read = [&](int32_t v) {
            if (v < 0) return;
            if (reader[size_t(v)] < 0) reader[size_t(v)] = int32_t(k);
            last_read[size_t(v)] = int32_t(k);
        };
        for (const Opnd* o : {&i.a, &i.b, &i.c})
            if (o->k == Opnd::R && o->v >= 0)
                for (int j = 0; j < o->n; ++j) read(o->v + j);
        for (int32_t v : i.uses) read(v);
        if (i.d >= 0)
            for (int j = 0; j < i.dn; ++j) def_at[size_t(i.d + j)] = int32_t(k);
    }
    std::vector<std::vector<int32_t>> attach(n);
    std::vector<bool> moved(n, false);
    bool any = false;
    for (size_t k = 0; k < n; ++k) {
        const Inst& i = p.code[k];
        if (!movable(i) || u.defs[size_t(i.d)] != 1 || u.uses[size_t(i.d)] != 1 || p.group[size_t(i.d)] != i.d) continue;
        if (size_t(i.d) + 1 < size_t(p.vregs) && p.group[size_t(i.d) + 1] == i.d) continue;   // leads a group
        const int32_t r = reader[size_t(i.d)];
        if (r <= int32_t(k) + 1 || block[size_t(r)] != block[k]) continue;
        bool ok = true;   // the sources keep their values up to the reader
        for (const Opnd* o : {&i.a, &i.b, &i.c})
            if (o->k == Opnd::R && o->v >= 0)
                for (int j = 0; j < o->n; ++j)
                    ok &= u.defs[size_t(o->v + j)] <= 1 && def_at[size_t(o->v + j)] < int32_t(k) &&
                          last_read[size_t(o->v + j)] >= r;
        if (!ok) continue;
        attach[size_t(r)].push_back(int32_t(k));
        moved[k] = any = true;
    }
    if (!any) return false;
    std::vector<Inst> out;
    out.reserve(n);
    std::function<void(size_t)> place = [&](size_t k) {
        for (int32_t j : attach[k]) place(size_t(j));
        out.push_back(std::move(p.code[k]));
    };
    for (size_t k = 0; k < n; ++k)
        if (!moved[k]) place(k);
    p.code = std::move(out);
    return true;
}

void optimize(Program& p) {
    auto on = [](char c) { return pass_on(c); };
    for (int round = 0; round < 8; ++round) {
        bool changed = false;
        if (on('f')) changed |= fold_operands(p);
        if (on('p')) changed |= fold_predicates(p);
        if (on('d')) changed |= eliminate_dead(p);
        if (on('c')) changed |= coalesce(p);
        if (on('d')) changed |= eliminate_dead(p);
        if (on('u')) changed |= eliminate_unread(p);
        if (on('t')) changed |= shrink_texture_results(p);
        if (!changed) break;
    }
    if (on('m')) sink(p);
}

} // namespace nvasm::maxwell

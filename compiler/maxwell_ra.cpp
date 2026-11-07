// Maxwell backend: register allocation and scheduling words (MAXWELL_BACKEND.md).
//
// Allocation: liveness over basic blocks, an interference graph, then greedy
// coloring. A predicated definition keeps the old value where the predicate is
// false, so it is also a use. Groups (consecutive operands) take one aligned
// block; vregs with a fixed register (fragment outputs at EXIT) keep that
// register. An instruction reads its sources before it writes, so a value may take
// the register of one that dies at its definition; copy partners prefer each
// other's registers, which turns the copy into a no-op the writer drops. Values
// merged at the end of an if/else are written in both arms but never live at the
// same time as the arm's temporaries they copy, so they share those registers.
//
// Scheduling: variable-latency instructions (the ones NVIDIA's own words give a
// write barrier: MUFU, IPA, texture, conversions, loads, S2R) set a write barrier
// that readers wait on, and a read barrier when they have register sources that a
// later write must not overtake. Fixed-latency results are ready 6 cycles after
// issue, predicates 13. Every label and control transfer drains: the next
// instruction waits on every pending barrier, 15 cycles after the transfer.
#include "maxwell_mir.h"
#include "nvasm.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <bit>
#include <map>
#include <set>

namespace nvasm::maxwell {

namespace {

bool is_branch(Op op) {
    return op == Op::BRA || op == Op::SYNC || op == Op::BRK || op == Op::CONT || op == Op::EXIT;
}
bool is_control(Op op) {
    return is_branch(op) || op == Op::SSY || op == Op::PBK || op == Op::PCNT || op == Op::KIL || op == Op::BAR || op == Op::MEMBAR;
}

// Registers an instruction reads and writes (vregs, groups expanded).
struct Access {
    std::vector<int32_t> use, def, puse, pdef;
};
void add_opnd(std::vector<int32_t>& out, const Opnd& o) {
    if (o.k == Opnd::R && o.v >= 0)
        for (int i = 0; i < o.n; ++i) out.push_back(o.v + i);
}
Access access(const Inst& i) {
    Access r;
    add_opnd(r.use, i.a);
    add_opnd(r.use, i.b);
    add_opnd(r.use, i.c);
    r.use.insert(r.use.end(), i.uses.begin(), i.uses.end());
    if (i.d >= 0)
        for (int k = 0; k < i.dn; ++k) {
            r.def.push_back(i.d + k);
            // A predicated write, or a texture/load that may leave components untouched,
            // keeps the old value: the register is live into the instruction as well.
            if (i.guard != kPT) r.use.push_back(i.d + k);
        }
    if (i.guard >= 0) r.puse.push_back(i.guard);
    if (i.pa >= 0) r.puse.push_back(i.pa);
    if (i.pb >= 0) r.puse.push_back(i.pb);
    if (i.pd >= 0) {
        r.pdef.push_back(i.pd);
        if (i.guard != kPT) r.puse.push_back(i.pd);
    }
    return r;
}

struct Cfg {
    std::vector<size_t> start, end;       // instruction ranges [start, end)
    std::vector<std::vector<size_t>> succ;
};
Cfg build_cfg(const Program& p) {
    Cfg g;
    const auto& code = p.code;
    std::vector<size_t> label_block(size_t(p.labels), SIZE_MAX);
    std::vector<bool> leader(code.size() + 1, false);
    leader[0] = true;
    for (size_t i = 0; i < code.size(); ++i) {
        if (code[i].op == Op::Label) leader[i] = true;
        if (is_branch(code[i].op)) leader[i + 1] = true;
    }
    for (size_t i = 0; i < code.size(); ++i)
        if (leader[i]) {
            if (!g.start.empty()) g.end.push_back(i);
            g.start.push_back(i);
            if (code[i].op == Op::Label) label_block[size_t(code[i].target)] = g.start.size() - 1;
        }
    g.end.push_back(code.size());
    g.succ.resize(g.start.size());
    for (size_t b = 0; b < g.start.size(); ++b) {
        if (g.start[b] == g.end[b]) {
            if (b + 1 < g.start.size()) g.succ[b].push_back(b + 1);
            continue;
        }
        const Inst& last = code[g.end[b] - 1];
        bool falls = true;
        if (is_branch(last.op)) {
            if (last.op != Op::EXIT) {
                const size_t t = label_block.at(size_t(last.target));
                if (t == SIZE_MAX) throw Error(0, "branch to an undefined label");
                g.succ[b].push_back(t);
            }
            falls = last.guard != kPT;
        }
        if (falls && b + 1 < g.start.size()) g.succ[b].push_back(b + 1);
    }
    return g;
}

// Liveness per basic block, then an interference graph over nodes (a group of
// consecutive vregs is one node) for n values of one class. A definition
// interferes with everything live after it except itself (and, for a plain copy,
// its source: both hold the same value). Sources are read before any result is
// written, so a result (a group too, as in NVIDIA's TEX R0, R0) may take the
// registers of sources that die there.
struct Graph {
    std::vector<int32_t> node;                  // vreg -> node
    std::vector<int32_t> leader, size, align;   // per node
    std::vector<std::vector<int32_t>> adj;      // per node, sorted
    std::vector<int32_t> first_def;             // per node: first instruction writing it
    std::vector<bool> used;                     // per node: read or written anywhere
    // The most values live after one instruction (NVASM_MAXWELL_PRESSURE prints it).
    int32_t peak = 0, peak_at = -1;
    std::vector<int32_t> peak_live;
};

bool report_pressure() {
    static const bool on = [] {
        char* v = nullptr;
        size_t length = 0;
        const bool set = _dupenv_s(&v, &length, "NVASM_MAXWELL_PRESSURE") == 0 && v;
        std::free(v);
        return set;
    }();
    return on;
}

Graph interference(const Program& p, const Cfg& g, bool preds) {
    const size_t n = size_t(preds ? p.pregs : p.vregs);
    Graph gr;
    gr.node.assign(n, -1);
    for (size_t v = 0; v < n; ++v) {
        const int32_t lead = preds ? int32_t(v) : p.group[v];
        if (lead != int32_t(v)) {
            gr.node[v] = gr.node[size_t(lead)];
            ++gr.size[size_t(gr.node[v])];
            continue;
        }
        gr.node[v] = int32_t(gr.leader.size());
        gr.leader.push_back(int32_t(v));
        gr.size.push_back(1);
        gr.align.push_back(preds ? 1 : std::max(1, int32_t(p.align[v])));
    }
    const size_t nodes = gr.leader.size();
    gr.adj.resize(nodes);
    gr.first_def.assign(nodes, INT32_MAX);
    gr.used.assign(nodes, false);

    const size_t words = (n + 63) / 64;
    const size_t blocks = g.start.size();
    std::vector<Access> acc(p.code.size());
    for (size_t i = 0; i < p.code.size(); ++i) acc[i] = access(p.code[i]);
    auto uses = [&](size_t i) -> const std::vector<int32_t>& { return preds ? acc[i].puse : acc[i].use; };
    auto defs = [&](size_t i) -> const std::vector<int32_t>& { return preds ? acc[i].pdef : acc[i].def; };
    using Set = std::vector<uint64_t>;
    std::vector<Set> gen(blocks, Set(words)), kill = gen, in = gen, out = gen;
    auto set = [](Set& s, size_t v) { s[v / 64] |= 1ull << (v % 64); };
    auto clear = [](Set& s, size_t v) { s[v / 64] &= ~(1ull << (v % 64)); };
    for (size_t b = 0; b < blocks; ++b)
        for (size_t i = g.start[b]; i < g.end[b]; ++i) {
            for (int32_t v : uses(i))
                if (!((kill[b][size_t(v) / 64] >> (size_t(v) % 64)) & 1)) set(gen[b], size_t(v));
            for (int32_t v : defs(i)) set(kill[b], size_t(v));   // a predicated write is also a use (access)
        }
    for (bool changed = true; changed;) {
        changed = false;
        for (size_t b = blocks; b-- > 0;) {
            Set o(words, 0);
            for (size_t s : g.succ[b])
                for (size_t w = 0; w < words; ++w) o[w] |= in[s][w];
            Set ni(words);
            for (size_t w = 0; w < words; ++w) ni[w] = gen[b][w] | (o[w] & ~kill[b][w]);
            if (ni != in[b] || o != out[b]) {
                in[b] = std::move(ni);
                out[b] = std::move(o);
                changed = true;
            }
        }
    }

    std::vector<int32_t> live_list;
    for (size_t b = 0; b < blocks; ++b) {
        Set live = out[b];
        for (size_t i = g.end[b]; i-- > g.start[b];) {
            const Inst& ins = p.code[i];
            const auto& d = defs(i);
            const auto& u = uses(i);
            for (int32_t v : u) gr.used[size_t(gr.node[size_t(v)])] = true;
            if (!preds && report_pressure()) {
                int32_t count = 0;
                for (uint64_t w : live) count += std::popcount(w);
                if (count > gr.peak) {
                    gr.peak = count;
                    gr.peak_at = int32_t(i);
                    gr.peak_live.clear();
                    for (size_t w = 0; w < words; ++w)
                        for (uint64_t bits = live[w]; bits; bits &= bits - 1)
                            gr.peak_live.push_back(int32_t(w * 64 + size_t(std::countr_zero(bits))));
                }
            }
            if (!d.empty()) {
                int32_t copy_src = -1;
                if (!preds && ins.op == Op::MOV && ins.guard == kPT && ins.b.k == Opnd::R && ins.b.v >= 0 && ins.b.n == 1 &&
                    !ins.b.neg && !ins.b.abs && ins.dn == 1)
                    copy_src = gr.node[size_t(ins.b.v)];
                // Only a whole value is the same as the copy: a member of a group
                // shares its node with members that may still be live.
                if (copy_src >= 0 && gr.size[size_t(copy_src)] != 1) copy_src = -1;
                live_list.clear();
                for (size_t w = 0; w < words; ++w)
                    for (uint64_t bits = live[w]; bits; bits &= bits - 1)
                        live_list.push_back(int32_t(w * 64 + size_t(std::countr_zero(bits))));
                int32_t last = -1;
                for (int32_t v : d) {
                    const int32_t dn = gr.node[size_t(v)];
                    if (dn == last) continue;   // the rest of a group
                    last = dn;
                    gr.used[size_t(dn)] = true;
                    gr.first_def[size_t(dn)] = std::min(gr.first_def[size_t(dn)], int32_t(i));
                    for (int32_t l : live_list) {
                        const int32_t ln = gr.node[size_t(l)];
                        if (ln != dn && ln != copy_src) gr.adj[size_t(dn)].push_back(ln);
                    }
                }
                for (int32_t v : d) clear(live, size_t(v));
            }
            for (int32_t v : u) set(live, size_t(v));
        }
    }
    // Symmetric, without duplicates.
    std::vector<size_t> own(nodes);
    for (size_t a = 0; a < nodes; ++a) own[a] = gr.adj[a].size();
    for (size_t a = 0; a < nodes; ++a)
        for (size_t j = 0; j < own[a]; ++j) gr.adj[size_t(gr.adj[a][j])].push_back(int32_t(a));
    for (auto& l : gr.adj) {
        std::sort(l.begin(), l.end());
        l.erase(std::unique(l.begin(), l.end()), l.end());
    }
    return gr;
}

} // namespace

// Greedy coloring in order of first definition (for code in which every use is
// dominated by a definition this is close to optimal), fixed registers first.
// A node takes the register of a copy partner when it is free, which makes the
// copy a no-op the writer drops.
Allocation allocate(const Program& p) {
    const Cfg g = build_cfg(p);
    Allocation a;
    a.reg.assign(size_t(p.vregs), -1);
    a.pred.assign(size_t(p.pregs), -1);

    const Graph gr = interference(p, g, false);
    const size_t nodes = gr.leader.size();
    std::vector<int32_t> base(nodes, -1);
    // Copy partners, as (node, offset of the vreg in its node) pairs.
    std::vector<std::vector<std::pair<int32_t, int32_t>>> partners(nodes);
    for (const Inst& i : p.code)
        if (i.op == Op::MOV && i.guard == kPT && i.b.k == Opnd::R && i.b.v >= 0 && i.b.n == 1 && !i.b.neg && !i.b.abs &&
            i.d >= 0 && i.dn == 1) {
            const int32_t dn = gr.node[size_t(i.d)], sn = gr.node[size_t(i.b.v)];
            if (dn == sn) continue;
            partners[size_t(dn)].push_back({sn, i.b.v - gr.leader[size_t(sn)] - (i.d - gr.leader[size_t(dn)])});
            partners[size_t(sn)].push_back({dn, i.d - gr.leader[size_t(dn)] - (i.b.v - gr.leader[size_t(sn)])});
        }

    std::vector<int32_t> order;
    for (size_t k = 0; k < nodes; ++k) {
        if (!gr.used[k]) continue;
        const int32_t fixed = p.fixed[size_t(gr.leader[k])];
        if (fixed >= 0) {
            base[k] = fixed;
            continue;
        }
        order.push_back(int32_t(k));
    }
    for (size_t k = 0; k < nodes; ++k)   // fixed nodes must not interfere with each other on a register
        if (base[k] >= 0)
            for (int32_t o : gr.adj[k])
                if (base[size_t(o)] >= 0 && base[size_t(o)] < base[k] + gr.size[k] && base[k] < base[size_t(o)] + gr.size[size_t(o)])
                    throw Error(0, "fixed register R" + std::to_string(base[k]) + " is still in use");
    std::stable_sort(order.begin(), order.end(), [&](int32_t x, int32_t y) { return gr.first_def[size_t(x)] < gr.first_def[size_t(y)]; });

    std::vector<uint8_t> taken(kMaxRegisters + 8);
    for (int32_t k : order) {
        std::fill(taken.begin(), taken.end(), uint8_t(0));
        for (int32_t o : gr.adj[size_t(k)])
            if (base[size_t(o)] >= 0)
                for (int32_t r = 0; r < gr.size[size_t(o)]; ++r) taken[size_t(base[size_t(o)] + r)] = 1;
        const int32_t size = gr.size[size_t(k)], align = gr.align[size_t(k)];
        auto fits = [&](int32_t r) {
            if (r < 0 || r % align != 0 || r + size > kMaxRegisters) return false;
            for (int32_t j = 0; j < size; ++j)
                if (taken[size_t(r + j)]) return false;
            return true;
        };
        int32_t chosen = -1;
        for (auto [o, offset] : partners[size_t(k)])
            if (chosen < 0 && base[size_t(o)] >= 0 && fits(base[size_t(o)] + offset)) chosen = base[size_t(o)] + offset;
        for (int32_t r = 0; chosen < 0 && r + size <= kMaxRegisters; r += align)
            if (fits(r)) chosen = r;
        if (chosen < 0) throw Error(0, "the program needs more than " + std::to_string(kMaxRegisters) + " registers");
        base[size_t(k)] = chosen;
    }
    for (int32_t v = 0; v < p.vregs; ++v) {
        const int32_t k = gr.node[size_t(v)];
        if (base[size_t(k)] < 0) continue;
        a.reg[size_t(v)] = base[size_t(k)] + (v - gr.leader[size_t(k)]);
        a.registers = std::max(a.registers, a.reg[size_t(v)] + 1);
    }
    if (report_pressure()) {
        std::fprintf(stderr, "pressure: %d values live after instruction %d; %d registers\n", gr.peak, gr.peak_at, a.registers);
        for (int32_t v : gr.peak_live)
            std::fprintf(stderr, "  v%d R%d first written at %d\n", v, a.reg[size_t(v)], gr.first_def[size_t(gr.node[size_t(v)])]);
    }

    // Predicates: P0..P6, the same way.
    const Graph pg = interference(p, g, true);
    std::vector<int32_t> porder;
    for (size_t k = 0; k < pg.leader.size(); ++k)
        if (pg.used[k]) porder.push_back(int32_t(k));
    std::stable_sort(porder.begin(), porder.end(), [&](int32_t x, int32_t y) { return pg.first_def[size_t(x)] < pg.first_def[size_t(y)]; });
    std::vector<int32_t> pbase(pg.leader.size(), -1);
    for (int32_t k : porder) {
        std::array<bool, 7> busy{};
        for (int32_t o : pg.adj[size_t(k)])
            if (pbase[size_t(o)] >= 0) busy[size_t(pbase[size_t(o)])] = true;
        int32_t r = 0;
        while (r < 7 && busy[size_t(r)]) ++r;
        if (r == 7) throw Error(0, "the program needs more than 7 predicates");
        pbase[size_t(k)] = r;
        a.pred[size_t(pg.leader[size_t(k)])] = r;
    }
    return a;
}

namespace {

bool variable_latency(Op op) {
    switch (op) {
    case Op::MUFU: case Op::IPA: case Op::TEX: case Op::TLD: case Op::TLD4: case Op::TXQ: case Op::TXD: case Op::F2F: case Op::F2I:
    case Op::I2F: case Op::I2I: case Op::LDC: case Op::LDG: case Op::LDS: case Op::LDL: case Op::S2R: case Op::ALD:
    case Op::ISBERD: case Op::OUT:
    case Op::POPC: case Op::FLO: case Op::PIXLD: case Op::SHFL: case Op::ATOM: case Op::ATOMS: case Op::SULD: case Op::SUATOM:
        return true;
    default:
        return false;
    }
}
// Instructions that may read their register sources after they issue.
bool late_reads(Op op) {
    return variable_latency(op) || op == Op::STG || op == Op::STS || op == Op::STL || op == Op::AST || op == Op::SUST;
}

} // namespace

std::vector<Control> schedule(const Program& p, const Allocation& a) {
    std::vector<Control> out(p.code.size());
    std::array<int64_t, 256> ready{};        // hardware register -> cycle its value is available
    std::array<int8_t, 256> wbar;            // register -> barrier guarding its pending write
    std::array<int8_t, 256> rbar;            // register -> barrier guarding a pending read of it
    std::array<int64_t, 8> pready{};
    wbar.fill(-1);
    rbar.fill(-1);
    std::array<bool, 6> in_use{};
    int64_t cycle = 0;
    int64_t prev_issue = -1;
    size_t prev = SIZE_MAX;
    auto regs_of = [&](const Opnd& o, std::vector<int32_t>& r) {
        if (o.k == Opnd::R && o.v >= 0)
            for (int k = 0; k < o.n; ++k) r.push_back(a.reg[size_t(o.v + k)]);
    };
    auto release = [&](int b) {
        in_use[size_t(b)] = false;
        for (auto& w : wbar) if (w == b) w = -1;
        for (auto& r : rbar) if (r == b) r = -1;
    };
    bool drain = true;
    for (size_t i = 0; i < p.code.size(); ++i) {
        const Inst& in = p.code[i];
        Control& c = out[i];
        if (in.op == Op::Label) { drain = true; continue; }
        std::vector<int32_t> srcs, dsts;
        regs_of(in.a, srcs);
        regs_of(in.b, srcs);
        regs_of(in.c, srcs);
        if (in.d >= 0)
            for (int k = 0; k < in.dn; ++k) dsts.push_back(a.reg[size_t(in.d + k)]);
        uint8_t wait = 0;
        int64_t need = cycle;
        if (drain) {
            for (int b = 0; b < 6; ++b)
                if (in_use[size_t(b)]) wait |= uint8_t(1 << b);
            need = std::max(need, prev_issue + 15);
            ready.fill(0);
            pready.fill(0);
        }
        for (int32_t r : srcs) {
            need = std::max(need, ready[size_t(r)]);
            if (wbar[size_t(r)] >= 0) wait |= uint8_t(1 << wbar[size_t(r)]);
        }
        if (in.guard >= 0) need = std::max(need, pready[size_t(a.pred[size_t(in.guard)])]);
        if (in.pa >= 0) need = std::max(need, pready[size_t(a.pred[size_t(in.pa)])]);
        if (in.pb >= 0) need = std::max(need, pready[size_t(a.pred[size_t(in.pb)])]);
        for (int32_t r : dsts) {
            if (wbar[size_t(r)] >= 0) wait |= uint8_t(1 << wbar[size_t(r)]);
            if (rbar[size_t(r)] >= 0) wait |= uint8_t(1 << rbar[size_t(r)]);
            need = std::max(need, ready[size_t(r)]);
        }
        if (in.op == Op::EXIT || is_control(in.op))
            for (int b = 0; b < 6; ++b)
                if (in_use[size_t(b)]) wait |= uint8_t(1 << b);
        for (int b = 0; b < 6; ++b)
            if (wait >> b & 1) release(b);
        c.wait = wait;
        if (prev != SIZE_MAX) {
            const int64_t stall = std::clamp<int64_t>(need - prev_issue, 1, 15);
            out[prev].stall = uint8_t(std::max<int64_t>(out[prev].stall, stall));
            cycle = prev_issue + out[prev].stall;
        }
        const int64_t issue = cycle;
        auto take = [&]() -> int {
            for (int b = 0; b < 6; ++b)
                if (!in_use[size_t(b)]) { in_use[size_t(b)] = true; return b; }
            // All six in flight: wait for barrier 0 first.
            c.wait |= 1;
            release(0);
            in_use[0] = true;
            return 0;
        };
        if (variable_latency(in.op) && !dsts.empty()) {
            const int b = take();
            c.wbar = uint8_t(b);
            for (int32_t r : dsts) wbar[size_t(r)] = int8_t(b), ready[size_t(r)] = issue + 1;
        } else {
            for (int32_t r : dsts) ready[size_t(r)] = issue + 6;
        }
        if (late_reads(in.op) && !srcs.empty()) {
            const int b = take();
            c.rbar = uint8_t(b);
            for (int32_t r : srcs) rbar[size_t(r)] = int8_t(b);
        }
        if (in.pd >= 0) pready[size_t(a.pred[size_t(in.pd)])] = issue + 13;
        c.stall = is_control(in.op) ? 15 : 1;
        drain = is_branch(in.op) || in.op == Op::SSY || in.op == Op::PBK || in.op == Op::PCNT;
        prev_issue = issue;
        prev = i;
    }
    return out;
}

} // namespace nvasm::maxwell

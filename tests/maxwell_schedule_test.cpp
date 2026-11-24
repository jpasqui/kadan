// compiler/maxwell_ra.cpp schedule(): every read of a variable-latency result waits on the barrier that guards it.
// Built by tools/check.py against the staged compiler (its staged tests). The check walks the scheduled program on its
// own: per register, the barrier its pending write sets; a wait on a barrier ends every write pending on it; a read or
// overwrite of a register with a write still pending is a hazard. An instruction's write and read barriers differ.
#include "KadanMaxwell/maxwell_mir.h"
#include <iostream>
#include <string>
#include <vector>

using namespace nvasm::maxwell;

static int failures = 0;
static void Expect(bool ok, const std::string& what) {
    if (!ok) { std::cerr << "FAIL " << what << "\n"; ++failures; }
}

static Inst Make(Op op, int32_t d, int32_t a) {
    Inst in;
    in.op = op;
    in.d = d;
    if (a >= 0) in.a = Opnd::reg(a);
    return in;
}

// Every vreg is its own hardware register.
static Allocation Identity(const Program& p) {
    Allocation a;
    for (int32_t v = 0; v < p.vregs; ++v) a.reg.push_back(v);
    a.registers = p.vregs;
    return a;
}

static void Registers(const Opnd& o, const Allocation& a, std::vector<int32_t>& out) {
    if (o.k == Opnd::R && o.v >= 0)
        for (int k = 0; k < o.n; ++k) out.push_back(a.reg[size_t(o.v + k)]);
}

static void Check(const std::string& name, const Program& p) {
    const Allocation a = Identity(p);
    const std::vector<Control> c = schedule(p, a);
    Expect(c.size() == p.code.size(), name + ": one control per instruction");
    std::vector<int> pending(256, -1);     // register -> barrier of its pending write
    for (size_t i = 0; i < p.code.size() && i < c.size(); ++i) {
        const Inst& in = p.code[i];
        const std::string at = name + ", instruction " + std::to_string(i);
        for (int b = 0; b < 6; ++b)
            if (c[i].wait >> b & 1)
                for (int& w : pending) if (w == b) w = -1;
        std::vector<int32_t> srcs, dsts;
        Registers(in.a, a, srcs);
        Registers(in.b, a, srcs);
        Registers(in.c, a, srcs);
        if (in.d >= 0)
            for (int k = 0; k < in.dn; ++k) dsts.push_back(a.reg[size_t(in.d + k)]);
        for (int32_t r : srcs)
            Expect(pending[size_t(r)] < 0, at + " reads R" + std::to_string(r) + " before its write lands");
        for (int32_t r : dsts)
            Expect(pending[size_t(r)] < 0, at + " overwrites R" + std::to_string(r) + " before its write lands");
        if (c[i].wbar != 7 && c[i].rbar != 7)
            Expect(c[i].wbar != c[i].rbar, at + " sets one barrier for its write and its read");
        for (int32_t r : dsts) pending[size_t(r)] = c[i].wbar != 7 ? int(c[i].wbar) : -1;
        Expect(c[i].wbar == 7 || !dsts.empty(), at + " sets a write barrier without a destination");
    }
}

// `count` texture fetches in a row, each from its own coordinate register into its own result, then one add per
// result reading it, then EXIT. Each fetch takes two barriers: one for its result, one for its coordinates.
static Program Fetches(int count) {
    Program p;
    std::vector<int32_t> coords, results;
    for (int k = 0; k < count; ++k) coords.push_back(p.new_vreg());
    for (int k = 0; k < count; ++k) {
        results.push_back(p.new_vreg());
        p.code.push_back(Make(Op::TEX, results.back(), coords[size_t(k)]));
    }
    for (int k = 0; k < count; ++k) {
        Inst add = Make(Op::FADD, p.new_vreg(), results[size_t(k)]);
        add.b = Opnd::reg(results[size_t(k)]);
        p.code.push_back(add);
    }
    p.code.push_back(Make(Op::EXIT, -1, -1));
    return p;
}

int main() {
    // Free barriers: a fetch's result and coordinates take the two lowest, and the read waits on the result's.
    {
        const Program p = Fetches(1);
        Check("one fetch", p);
        const std::vector<Control> c = schedule(p, Identity(p));
        Expect(c[0].wbar == 0 && c[0].rbar == 1, "one fetch: barriers 0 and 1");
        Expect((c[1].wait & 1) != 0, "one fetch: its read waits on barrier 0");
    }
    Check("two fetches", Fetches(2));
    Check("three fetches (all six barriers)", Fetches(3));
    // More two-barrier instructions in a row than barriers: each must reuse a barrier other than the one it just took.
    for (int count = 4; count <= 8; ++count)
        Check(std::to_string(count) + " fetches", Fetches(count));
    // Interleaved with single-barrier interpolant reads (no register sources).
    {
        Program p;
        std::vector<int32_t> results;
        for (int k = 0; k < 6; ++k) {
            const int32_t coord = p.new_vreg();
            results.push_back(p.new_vreg());
            p.code.push_back(Make(Op::TEX, results.back(), coord));
            results.push_back(p.new_vreg());
            p.code.push_back(Make(Op::IPA, results.back(), -1));
        }
        for (int32_t r : results) p.code.push_back(Make(Op::FADD, p.new_vreg(), r));
        p.code.push_back(Make(Op::EXIT, -1, -1));
        Check("fetches and interpolants", p);
    }
    if (failures) {
        std::cerr << failures << " failures\n";
        return 1;
    }
    std::cout << "maxwell schedule: all checks passed\n";
    return 0;
}

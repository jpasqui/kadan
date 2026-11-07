// Maxwell backend: instruction selection from the IR (MAXWELL_BACKEND.md).
//
// Every IR value becomes scalar vregs, one per component, memoized per statement
// like the SPIR-V writer's values. Program registers, locals and outputs are not
// storage but names: each component is a slot pointing at the vreg that holds its
// current value, so an assignment emits nothing. Where control flow joins, a merge
// vreg takes each path's value (if/else), or a carried vreg holds the value around
// a loop, written before every break, continue and back edge. Booleans are masks
// (0 or 0xFFFFFFFF) in registers; a predicate is made only where a select, branch,
// break or kill needs one.
#include "maxwell_mir.h"
#include "nvasm_ir.h"

#include <bit>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>

namespace nvasm::ir {

namespace {

using namespace nvasm::maxwell;
using MOp = maxwell::Op;
using MInst = maxwell::Inst;

constexpr uint32_t kGeneric = 0x80;   // attribute address of generic attribute 0 (16 bytes each)
constexpr uint32_t kPosition = 0x70;
constexpr uint32_t kLayer = 0x64;       // render target array index
constexpr uint32_t kFrontFace = 0x3fc, kInstanceId = 0x2f8, kVertexId = 0x2fc;

// Bank 0 offset of storage buffer N's 64-bit address (its size follows): 16 bytes per
// buffer from a per-stage base, as the NVN driver lays the table out.
uint32_t storage_table(Stage s) {
    switch (s) {
    case Stage::Fragment: return 0x510;
    case Stage::Geometry: return 0x910;
    case Stage::Compute: return 0x310;
    default: return 0x110;
    }
}

[[noreturn]] void refuse(const std::string& what);

class Selector {
public:
    explicit Selector(const Function& f) : f_(f) {}
    Program run();
    bool uses_memory() const { return memory_; }
    uint32_t local_bytes() const { return local_bytes_; }

private:
    const Function& f_;
    Program p_;

    struct Val {
        Kind k = Kind::F;
        int n = 1;
        std::array<int32_t, 4> r{kRZ, kRZ, kRZ, kRZ};
    };

    // ---- emission
    MInst& emit(MOp op) {
        MInst i;
        i.op = op;
        p_.code.push_back(i);
        return p_.code.back();
    }
    int32_t vreg() { return p_.new_vreg(); }
    int32_t pred() { return p_.new_pred(); }
    int32_t mov32i(uint32_t bits) {
        if (bits == 0) return kRZ;
        MInst& i = emit(MOp::MOV32I);
        i.d = vreg();
        i.a = Opnd::imm(bits);
        return i.d;
    }
    void mov(int32_t d, int32_t s) {
        if (d == s) return;
        MInst& i = emit(MOp::MOV);
        i.d = d;
        i.b = Opnd::reg(s);
    }
    int32_t copy(int32_t s) {
        const int32_t d = vreg();
        MInst& i = emit(MOp::MOV);
        i.d = d;
        i.b = Opnd::reg(s);
        return d;
    }
    // A predicate true where the mask `v` is nonzero.
    int32_t truth(int32_t v) {
        MInst& i = emit(MOp::ISETP);
        i.pd = pred();
        i.cmp = NE;
        i.a = Opnd::reg(v);
        i.b = Opnd::rz();
        return i.pd;
    }
    int32_t alu(MOp op, Opnd a, Opnd b, Opnd c = {}, uint32_t flags = 0, uint8_t sub = 0) {
        MInst& i = emit(op);
        i.d = vreg();
        i.a = a;
        i.b = b;
        i.c = c;
        i.flags = flags;
        i.sub = sub;
        return i.d;
    }
    static Opnd R(int32_t v) { return Opnd::reg(v); }
    static Opnd neg(int32_t v) { Opnd o = Opnd::reg(v); o.neg = true; return o; }

    // ---- values
    std::vector<Val> memo_;
    std::vector<uint32_t> stamp_;
    uint32_t generation_ = 1;
    Val value(int id);
    Val value_w(int id, int n) {   // widened to n components
        Val v = value(id);
        if (v.n == 1 && n > 1) {
            for (int i = 1; i < n; ++i) v.r[size_t(i)] = v.r[0];
            v.n = n;
        }
        return v;
    }
    Val call(const Node& n);
    Val texture(const Node& n);
    int32_t compare(MOp, Kind k, uint8_t cmp, int32_t a, int32_t b);
    int32_t convert(int32_t v, Kind from, Kind to);
    int32_t imul(int32_t a, int32_t b);
    int32_t imul_const(int32_t a, uint32_t c);
    bool const_lane(int node, int lane, uint32_t& bits) const {
        const Node& n = f_.nodes.at(size_t(node));
        if (n.op != Op::Const) return false;
        bits = n.bits[size_t(n.type.n == 1 ? 0 : lane)];
        return true;
    }
    int32_t divide(int32_t a, int32_t b, const Node* divisor, int lane, bool is_signed, bool modulo);
    Opnd storage_address(int buffer, int32_t word_index);
    // Invariant special registers (compute thread and group IDs) are read once and
    // reused wherever that read dominates: nested bodies drop what they added.
    std::map<uint32_t, int32_t> specials_;
    std::map<int, Val> builtins_;   // compute IDs, the same way
    int32_t special(uint32_t sr, bool invariant = false) {   // S2R
        if (invariant) {
            auto it = specials_.find(sr);
            if (it != specials_.end()) return it->second;
        }
        MInst& m = emit(MOp::S2R);
        m.d = vreg();
        m.aux = sr;
        if (invariant) specials_[sr] = m.d;
        return m.d;
    }
    int32_t image_handle(const Image& img);
    Opnd image_coords(const Image& img, int coord_node, uint8_t& type);
    Val atomic(const Node& n);
    bool memory_ = false;   // global loads or stores: the SPH says so
    // Register arrays indexed at run time live in local memory (16 bytes per element).
    std::vector<int32_t> local_base_;   // IR register -> byte offset, or -1 in registers
    // Compute programs keep such arrays in registers instead (the port gives compute no
    // local memory, and NVIDIA's kernels do the same): an element is chosen by comparing
    // the index with each one.
    std::vector<bool> select_array_;
    int array_size(int var) const { return std::max(1, f_.regs[size_t(var)].array); }
    int32_t index_is(int32_t index, int element) {   // predicate: index == element
        const int32_t constant = mov32i(uint32_t(element));
        MInst& t = emit(MOp::ISETP);
        t.pd = pred();
        t.cmp = EQ;
        t.a = R(index);
        t.b = R(constant);
        return t.pd;
    }
    uint32_t local_bytes_ = 0;
    int32_t local_address(int var, int index_node, int component, uint32_t& offset);

    // ---- storage: renamed values
    enum SlotKind { SlotReg = 0, SlotLocal = 1, SlotOutput = 2, SlotPosition = 3, SlotDepth = 4, SlotLayer = 5, SlotOutVertex = 6 };
    // Geometry: the attribute handle of each input vertex read, and (slot SlotOutVertex)
    // the handle of the output vertex being written, which each OUT advances.
    std::map<int, int32_t> vertex_handle_;
    void geometry_prologue();
    void emit_vertex(uint32_t how);
    std::map<uint64_t, int32_t> slot_index_;
    std::vector<int32_t> cur_;   // slot -> the vreg holding its value (kRZ: zero)
    int32_t slot(int kind, int var, int element, int comp) {
        const uint64_t key = uint64_t(kind) << 56 | uint64_t(uint32_t(var)) << 24 | uint64_t(uint32_t(element)) << 4 | uint64_t(comp);
        auto [it, fresh] = slot_index_.try_emplace(key, int32_t(cur_.size()));
        if (fresh) cur_.push_back(kRZ);
        return it->second;
    }
    std::set<int> output_vars_;   // colour location, or 1000 + generic location
    bool position_written_ = false, depth_written_ = false;
    int32_t rcp_w_ = -1;
    struct LoopLabels {
        int32_t exit, cont;
        std::vector<std::pair<int32_t, int32_t>> carried;   // slot, the vreg holding it around the loop
    };
    std::vector<LoopLabels> loops_;
    std::set<int32_t> carried_;   // every carried vreg of the loops being selected
    int element_of(int index_node) {
        if (index_node < 0) return 0;
        const Node& ix = f_.nodes.at(size_t(index_node));
        if (ix.op != Op::Const) refuse("a register array indexed at run time");
        return int32_t(ix.bits[0]);
    }
    std::vector<int32_t> target_slots(const Stmt& s);        // the slots an assignment writes
    void slots_written(const std::vector<Stmt>& list, std::set<int32_t>& out);
    void slots_written(const Stmt& s, std::set<int32_t>& out);
    // Vertex outputs are stored after the last top-level statement that writes them,
    // so they do not hold registers to the end of the program.
    std::map<int32_t, uint32_t> output_address_;   // slot -> attribute address
    std::set<int32_t> stored_;
    void store_output(int32_t sl) {
        MInst& st = emit(MOp::AST);
        st.a = R(cur_[size_t(sl)]);
        st.aux = output_address_.at(sl);
        stored_.insert(sl);
    }
    void flush(const LoopLabels& l) {   // the carried vregs take the current values
        for (auto [sl, v] : l.carried)
            if (cur_[size_t(sl)] != v) mov(v, cur_[size_t(sl)]);
    }
    int32_t rcp_w();

    void stmts(const std::vector<Stmt>& list);
    void stmt(const Stmt& s);
    void assign(const Stmt& s);
    void finish();
};

[[noreturn]] void refuse(const std::string& what) { throw Error(0, "maxwell: " + what + " is not supported yet"); }

// ---------------------------------------------------------------- values

int32_t Selector::rcp_w() {
    if (rcp_w_ < 0) throw std::logic_error("1/w requested after entry");
    return rcp_w_;
}

int32_t Selector::compare(MOp op, Kind k, uint8_t cmp, int32_t a, int32_t b) {
    MInst& i = emit(op);
    i.d = vreg();
    i.cmp = cmp;
    i.a = R(a);
    i.b = R(b);
    if (k == Kind::S) i.flags |= kSigned;
    return i.d;
}

int32_t Selector::convert(int32_t v, Kind from, Kind to) {
    if (from == to) return v;
    if (from == Kind::B) {   // mask -> 1 / 1.0
        return alu(MOp::LOP32I, R(v), Opnd::imm(to == Kind::F ? 0x3f800000u : 1u), {}, 0, uint8_t(Lop::AND));
    }
    if (from == Kind::F) {   // truncating, saturating, NaN -> 0 (Maxwell F2I)
        return alu(MOp::F2I, {}, R(v), {}, to == Kind::S ? kSignedDst : 0, uint8_t(Rnd::RZ));
    }
    if (to == Kind::F) return alu(MOp::I2F, {}, R(v), {}, from == Kind::S ? kSigned : 0, uint8_t(Rnd::RN));
    return v;   // S <-> U: the bits
}

// The low 32 bits of a * b from three 16-bit XMADs, as NVIDIA's compiler emits it:
//   t0 = a.lo * b.lo;  t1 = a.lo * b.hi, merged with b.lo in its upper half (MRG);
//   d  = ((a.hi * t1.hi) << 16) + t0 + (t1 << 16)   (PSL, CBCC)
int32_t Selector::imul(int32_t a, int32_t b) {
    auto xmad = [&](int32_t x, int32_t y, Opnd c, uint32_t aux) {
        MInst& m = emit(MOp::XMAD);
        m.d = vreg();
        m.a = R(x);
        m.b = R(y);
        m.c = c;
        m.aux = aux;
        return m.d;
    };
    const int32_t t0 = xmad(a, b, Opnd::rz(), 0);
    const int32_t t1 = xmad(a, b, Opnd::rz(), kXmadHiB | kXmadMrg);
    return xmad(a, t1, R(t0), kXmadHiA | kXmadHiB | kXmadPsl | kXmadCbcc);
}

// a * c for a constant c: a shift for a power of two; below 2^16 two XMADs,
// a.lo * c + ((a.hi * c) << 16).
int32_t Selector::imul_const(int32_t a, uint32_t c) {
    if (c == 0) return kRZ;
    if (c == 1) return a;
    if ((c & (c - 1)) == 0) return alu(MOp::SHL, R(a), Opnd::imm(uint32_t(std::countr_zero(c))));
    const int32_t b = mov32i(c);
    if (c >= 0x10000) return imul(a, b);
    const int32_t t = alu(MOp::XMAD, R(a), R(b), Opnd::rz());
    MInst& m = emit(MOp::XMAD);
    m.d = vreg();
    m.a = R(a);
    m.b = R(b);
    m.c = R(t);
    m.aux = kXmadHiA | kXmadPsl;
    return m.d;
}

// Integer division and remainder (SPIR-V's semantics: truncating quotient, a signed
// remainder that takes the divisor's sign). A constant power of two is a shift or a
// mask; anything else is an exact restoring division, 32 predicated steps.
int32_t Selector::divide(int32_t a, int32_t b, const Node* divisor, int lane, bool is_signed, bool modulo) {
    if (divisor && divisor->op == Op::Const) {
        const uint32_t d = divisor->bits[size_t(divisor->type.n == 1 ? 0 : lane)];
        if (d && (d & (d - 1)) == 0) {
            int k = 0;
            while ((1u << k) != d) ++k;
            if (modulo) return k ? alu(MOp::LOP32I, R(a), Opnd::imm(d - 1), {}, 0, uint8_t(Lop::AND)) : kRZ;
            if (!k) return a;
            if (!is_signed) return alu(MOp::SHR, R(a), Opnd::imm(uint32_t(k)));
            // Toward zero: add d - 1 to negative dividends first.
            const int32_t sign = alu(MOp::SHR, R(a), Opnd::imm(31), {}, kSigned);
            const int32_t bias = alu(MOp::SHR, R(sign), Opnd::imm(uint32_t(32 - k)));
            const int32_t sum = alu(MOp::IADD, R(a), R(bias));
            return alu(MOp::SHR, R(sum), Opnd::imm(uint32_t(k)), {}, kSigned);
        }
    }
    int32_t n = a, dv = b;
    int32_t neg_n = -1, neg_d = -1;
    if (is_signed) {   // magnitudes; the signs are applied afterwards
        auto magnitude = [&](int32_t v, int32_t& negative) {
            MInst& t = emit(MOp::ISETP);
            t.pd = pred();
            t.cmp = LT;
            t.flags |= kSigned;
            t.a = R(v);
            t.b = Opnd::rz();
            negative = t.pd;
            const int32_t m = copy(v);
            MInst& ng = emit(MOp::IADD);
            ng.guard = negative;
            ng.d = m;
            ng.a = Opnd::rz();
            ng.b = neg(v);
            return m;
        };
        n = magnitude(a, neg_n);
        dv = magnitude(b, neg_d);
    }
    // Restoring division: r = r << 1 | next bit; if r >= d { r -= d; q |= bit }.
    const int32_t q = copy(kRZ), r = copy(kRZ);
    for (int i = 31; i >= 0; --i) {
        const int32_t bit = alu(MOp::SHR, R(n), Opnd::imm(uint32_t(i)));
        const int32_t low = alu(MOp::LOP32I, R(bit), Opnd::imm(1), {}, 0, uint8_t(Lop::AND));
        const int32_t shifted = alu(MOp::SHL, R(r), Opnd::imm(1));
        { MInst& o = emit(MOp::LOP); o.d = r; o.a = R(shifted); o.b = R(low); o.sub = uint8_t(Lop::OR); }
        MInst& ge = emit(MOp::ISETP);
        ge.pd = pred();
        ge.cmp = GE;
        ge.a = R(r);
        ge.b = R(dv);
        { MInst& s = emit(MOp::IADD); s.guard = ge.pd; s.d = r; s.a = R(r); s.b = neg(dv); }
        { MInst& o = emit(MOp::LOP32I); o.guard = ge.pd; o.d = q; o.a = R(q); o.b = Opnd::imm(1u << i); o.sub = uint8_t(Lop::OR); }
    }
    if (!is_signed) return modulo ? r : q;
    if (!modulo) {   // negative when exactly one operand is
        MInst& x = emit(MOp::PSETP);
        x.pd = pred();
        x.pb = neg_n;
        x.pa = neg_d;
        x.sub = uint8_t(BoolOp::XOR);
        MInst& ng = emit(MOp::IADD);
        ng.guard = x.pd;
        ng.d = q;
        ng.a = Opnd::rz();
        ng.b = neg(q);
        return q;
    }
    // Remainder with the dividend's sign, then moved to the divisor's sign.
    { MInst& ng = emit(MOp::IADD); ng.guard = neg_n; ng.d = r; ng.a = Opnd::rz(); ng.b = neg(r); }
    MInst& nz = emit(MOp::ISETP);
    nz.pd = pred();
    nz.cmp = NE;
    nz.a = R(r);
    nz.b = Opnd::rz();
    MInst& differ = emit(MOp::PSETP);
    differ.pd = pred();
    differ.pb = neg_n;
    differ.pa = neg_d;
    differ.sub = uint8_t(BoolOp::XOR);
    MInst& both = emit(MOp::PSETP);
    both.pd = pred();
    both.pb = differ.pd;
    both.pa = nz.pd;
    both.sub = uint8_t(BoolOp::AND);
    { MInst& ad = emit(MOp::IADD); ad.guard = both.pd; ad.d = r; ad.a = R(r); ad.b = R(b); }
    return r;
}

// The 64-bit address of word `word_index` of storage buffer `buffer`, as NVIDIA's
// compiler forms it: ISCADD.CC lo = (index << 2) + c[0][table]; IADD.X hi = c[0][table + 4].
Opnd Selector::storage_address(int buffer, int32_t word_index) {
    memory_ = true;
    const uint32_t table = storage_table(f_.stage) + 16 * uint32_t(buffer);
    const int32_t g = p_.new_group(2);
    MInst& lo = emit(MOp::ISCADD);
    lo.d = g;
    lo.a = R(word_index);
    lo.b = Opnd::cb(0, table);
    lo.aux = 2;
    lo.flags |= kCC;
    MInst& hi = emit(MOp::IADD);
    hi.d = g + 1;
    hi.a = Opnd::rz();
    hi.b = Opnd::cb(0, table + 4);
    hi.flags |= kX;
    return Opnd::reg(g, 2);
}

// Local memory address of component `component` of element `index_node` (an int node)
// of array `var`: a register holding the dynamic part (or RZ) and a constant offset.
int32_t Selector::local_address(int var, int index_node, int component, uint32_t& offset) {
    offset = uint32_t(local_base_[size_t(var)]) + 4 * uint32_t(component);
    if (index_node < 0) return kRZ;
    const Node& ix = f_.nodes.at(size_t(index_node));
    if (ix.op == Op::Const) {
        offset += 16 * ix.bits[0];
        return kRZ;
    }
    return alu(MOp::SHL, R(value(index_node).r[0]), Opnd::imm(4));
}

int32_t Selector::image_handle(const Image& img) {
    const int32_t handle = vreg();
    bool first = true;
    for (const auto& w : img.handle) {
        if (w.buffer != 14) refuse("an image handle outside program.buffer[14]");
        MInst& m = emit(first ? MOp::MOV : MOp::LOP);
        m.d = handle;
        if (!first) m.a = R(handle), m.sub = uint8_t(Lop::OR);
        m.b = Opnd::cb(2, w.offset + 32);
        first = false;
    }
    if (first) throw Error(0, "maxwell: image without a handle");
    return handle;
}

// Image coordinates in consecutive registers: x [, y [, z or layer]], and the type.
Opnd Selector::image_coords(const Image& img, int coord_node, uint8_t& type) {
    static const std::map<std::string, std::pair<uint8_t, int>> types = {
        {"1D", {0, 1}}, {"BUFFER", {1, 1}}, {"ARRAY1D", {2, 2}}, {"2D", {3, 2}}, {"ARRAY2D", {4, 3}}, {"3D", {5, 3}}};
    auto it = types.find(img.target);
    if (it == types.end()) refuse("image target " + img.target);
    type = it->second.first;
    const Val c = value(coord_node);
    const int n = it->second.second;
    const int32_t g = p_.new_group(n);
    for (int i = 0; i < n; ++i) mov(g + i, c.n == 1 ? c.r[0] : c.r[size_t(i)]);
    return Opnd::reg(g, uint8_t(n));
}

Selector::Val Selector::atomic(const Node& n) {
    static const uint8_t ops[] = {0 /*Add*/, 1 /*Min*/, 2 /*Max*/, 5 /*And*/, 6 /*Or*/, 7 /*Xor*/, 8 /*Exchange*/};
    Val out;
    out.k = n.type.k;
    out.n = 1;
    const uint8_t op = ops[n.sub];
    const uint32_t sign = n.type.k == Kind::S ? kSigned : 0;
    const int32_t v = value(n.b).r[0];
    if (n.var == 0) {   // shared memory word
        const int32_t address = alu(MOp::SHL, R(value(n.a).r[0]), Opnd::imm(2));
        MInst& m = emit(MOp::ATOMS);
        m.d = vreg();
        m.a = R(address);
        m.b = R(v);
        m.sub = op;
        m.flags |= sign;
        out.r[0] = m.d;
    } else if (n.var == 1) {   // storage buffer word
        const Opnd address = storage_address(n.imm, value(n.a).r[0]);
        MInst& m = emit(MOp::ATOM);
        m.d = vreg();
        m.a = address;
        m.b = R(v);
        m.sub = op;
        m.flags |= sign;
        out.r[0] = m.d;
    } else {   // image texel
        const Image& img = f_.images.at(size_t(n.imm));
        uint8_t type = 0;
        const Opnd coords = image_coords(img, n.args.at(0), type);
        if (n.args.size() > 1) refuse("multisample image atomics");
        const int32_t handle = image_handle(img);
        MInst& m = emit(MOp::SUATOM);
        m.d = vreg();
        m.a = coords;
        m.b = R(v);
        m.c = R(handle);
        m.sub = op;
        m.aux2 = type;
        m.flags |= sign;
        out.r[0] = m.d;
    }
    memory_ = true;
    return out;
}

std::vector<int32_t> Selector::target_slots(const Stmt& s) {
    std::vector<int32_t> out;
    if (s.target == Target::Reg && select_array_[size_t(s.var)] && s.index >= 0 &&
        f_.nodes.at(size_t(s.index)).op != Op::Const) {   // any element may change
        for (int e = 0; e < array_size(s.var); ++e)
            for (uint8_t l : s.lanes) out.push_back(slot(SlotReg, s.var, e, l));
        return out;
    }
    for (uint8_t l : s.lanes) {
        switch (s.target) {
        case Target::Reg: {
            const int element = element_of(s.index);
            const int size = f_.regs[size_t(s.var)].array > 0 ? f_.regs[size_t(s.var)].array : 1;
            if (element < 0 || element >= size) throw Error(0, "maxwell: register array index out of range");
            out.push_back(slot(SlotReg, s.var, element, l));
            break;
        }
        case Target::Local: out.push_back(slot(SlotLocal, s.var, 0, l)); break;
        case Target::Output: out.push_back(slot(SlotOutput, s.var, 0, l)); break;
        case Target::Position: out.push_back(slot(SlotPosition, 0, 0, l)); break;
        case Target::FragDepth: out.push_back(slot(SlotDepth, 0, 0, 0)); break;
        case Target::Layer: if (l == 0) out.push_back(slot(SlotLayer, 0, 0, 0)); break;
        }
    }
    return out;
}

void Selector::slots_written(const std::vector<Stmt>& list, std::set<int32_t>& out) {
    for (const Stmt& s : list) slots_written(s, out);
}

void Selector::slots_written(const Stmt& s, std::set<int32_t>& out) {
    if (s.k == Stmt::Emit || s.k == Stmt::EndPrimitive) out.insert(slot(SlotOutVertex, 0, 0, 0));
    if (s.k == Stmt::Assign && !(s.target == Target::Reg && local_base_[size_t(s.var)] >= 0))
        for (int32_t sl : target_slots(s)) out.insert(sl);
    if (s.k == Stmt::DeclLocal) {
        const int n = f_.nodes.at(size_t(s.value)).type.n;
        for (int c = 0; c < n; ++c) out.insert(slot(SlotLocal, s.var, 0, c));
    }
    slots_written(s.body, out);
    slots_written(s.other, out);
}

Selector::Val Selector::value(int id) {
    if (memo_.size() < f_.nodes.size()) memo_.resize(f_.nodes.size()), stamp_.resize(f_.nodes.size());
    if (stamp_[size_t(id)] == generation_) return memo_[size_t(id)];
    const Node& n = f_.nodes.at(size_t(id));
    Val out;
    out.k = n.type.k;
    out.n = n.type.n;
    const int w = n.type.n;
    auto each = [&](auto fn) { for (int i = 0; i < w; ++i) out.r[size_t(i)] = fn(i); };
    switch (n.op) {
    case Op::Const:
        each([&](int i) { return mov32i(n.bits[size_t(i)]); });
        break;
    case Op::Reg: {
        if (local_base_[size_t(n.var)] >= 0) {
            memory_ = true;
            int32_t address = -1;
            each([&](int i) {
                uint32_t offset = 0;
                const int32_t a = local_address(n.var, n.a, i, offset);
                if (address < 0 || a == kRZ) address = a;   // the dynamic part is shared by the components
                MInst& ld = emit(MOp::LDL);
                ld.d = vreg();
                ld.a = R(address);
                ld.aux = offset;
                return ld.d;
            });
            break;
        }
        if (select_array_[size_t(n.var)] && n.a >= 0 && f_.nodes.at(size_t(n.a)).op != Op::Const) {
            const int32_t index = value(n.a).r[0];
            std::array<int32_t, 4> v{};
            for (int i = 0; i < w; ++i) v[size_t(i)] = cur_[size_t(slot(SlotReg, n.var, 0, i))];
            for (int e = 1; e < array_size(n.var); ++e) {   // one predicate at a time
                const int32_t is = index_is(index, e);
                for (int i = 0; i < w; ++i) {
                    MInst& sel = emit(MOp::SEL);
                    sel.d = vreg();
                    sel.a = R(cur_[size_t(slot(SlotReg, n.var, e, i))]);
                    sel.b = R(v[size_t(i)]);
                    sel.pa = is;
                    v[size_t(i)] = sel.d;
                }
            }
            each([&](int i) { return v[size_t(i)]; });
            break;
        }
        const int element = element_of(n.a);
        each([&](int i) { return cur_[size_t(slot(SlotReg, n.var, element, i))]; });
        break;
    }
    case Op::Local:
        each([&](int i) { return cur_[size_t(slot(SlotLocal, n.var, 0, i))]; });
        break;
    case Op::Input: {
        if (f_.stage == Stage::Fragment) {
            bool flat = false, linear = false;
            for (const auto& in : f_.inputs)
                if (in.location == n.var) flat = in.interpolation == "flat", linear = in.interpolation == "noperspective";
            each([&](int i) {
                MInst& ipa = emit(MOp::IPA);
                ipa.d = vreg();
                ipa.aux = kGeneric + 16 * uint32_t(n.var) + 4 * uint32_t(i);
                if (flat) ipa.flags |= kFlat;
                else if (!linear) ipa.flags |= kPerspective, ipa.b = R(rcp_w());
                return ipa.d;
            });
        } else if (f_.stage == Stage::Vertex || f_.stage == Stage::Geometry) {
            const int32_t handle = f_.stage == Stage::Geometry ? vertex_handle_.at(int(n.imm)) : kRZ;
            each([&](int i) {
                MInst& ald = emit(MOp::ALD);
                ald.d = vreg();
                ald.aux = kGeneric + 16 * uint32_t(n.var) + 4 * uint32_t(i);
                ald.c = R(handle);
                return ald.d;
            });
        } else {
            refuse("inputs of this stage");
        }
        break;
    }
    case Op::Builtin: {
        if (Builtin(n.sub) == Builtin::FragCoord && f_.stage == Stage::Fragment) {
            each([&](int i) {
                MInst& ipa = emit(MOp::IPA);
                ipa.d = vreg();
                ipa.aux = kPosition + 4 * uint32_t(i);
                return ipa.d;
            });
            break;
        }
        if (Builtin(n.sub) == Builtin::Facing && f_.stage == Stage::Fragment) {
            MInst& ipa = emit(MOp::IPA);   // 0xFFFFFFFF front, 0 back
            ipa.d = vreg();
            ipa.aux = kFrontFace;
            ipa.flags |= kFlat;
            const int32_t front = truth(ipa.d);
            const int32_t one = mov32i(0x3f800000u), minus_one = mov32i(0xbf800000u);
            MInst& s = emit(MOp::SEL);
            s.d = vreg();
            s.a = R(one);
            s.b = R(minus_one);
            s.pa = front;
            const int32_t d = s.d;
            out.r = {d, kRZ, kRZ, mov32i(0x3f800000u)};
            break;
        }
        if ((Builtin(n.sub) == Builtin::VertexId || Builtin(n.sub) == Builtin::InstanceId) && f_.stage == Stage::Vertex) {
            MInst& ald = emit(MOp::ALD);
            ald.d = vreg();
            ald.aux = Builtin(n.sub) == Builtin::VertexId ? kVertexId : kInstanceId;
            out.r = {ald.d, kRZ, kRZ, kRZ};
            break;
        }
        if (f_.stage == Stage::Compute) {
            if (auto it = builtins_.find(n.sub); it != builtins_.end()) {
                out = it->second;
                break;
            }
            const Builtin b = Builtin(n.sub);
            auto tid = [&](int c) { return special(33 + uint32_t(c), true); };     // SR_TID.X/Y/Z
            auto ctaid = [&](int c) { return special(37 + uint32_t(c), true); };   // SR_CTAID.X/Y/Z
            if (b == Builtin::LocalId || b == Builtin::GroupId) {
                for (int c = 0; c < 3; ++c) out.r[size_t(c)] = b == Builtin::LocalId ? tid(c) : ctaid(c);
                out.r[3] = kRZ;
                builtins_[n.sub] = out;
                break;
            }
            if (b == Builtin::GlobalId) {   // group id * group size + local id
                for (int c = 0; c < 3; ++c) {
                    const uint32_t size = f_.group[size_t(c)];
                    const int32_t g = ctaid(c), l = tid(c);
                    if (size && (size & (size - 1)) == 0) {
                        int k = 0;
                        while ((1u << k) != size) ++k;
                        MInst& m = emit(MOp::ISCADD);
                        m.d = vreg();
                        m.a = R(g);
                        m.b = R(l);
                        m.aux = uint32_t(k);
                        out.r[size_t(c)] = m.d;
                    } else {
                        out.r[size_t(c)] = alu(MOp::IADD, R(imul_const(g, size)), R(l));
                    }
                }
                out.r[3] = kRZ;
                builtins_[n.sub] = out;
                break;
            }
            if (b == Builtin::LocalIndex) {   // z * (sx * sy) + y * sx + x
                const int32_t z = imul_const(tid(2), f_.group[0] * f_.group[1]);
                const int32_t y = imul_const(tid(1), f_.group[0]);
                out.r = {alu(MOp::IADD, R(alu(MOp::IADD, R(z), R(y))), R(tid(0))), kRZ, kRZ, kRZ};
                builtins_[n.sub] = out;
                break;
            }
        }
        if (Builtin(n.sub) == Builtin::SampleId && f_.stage == Stage::Fragment) {
            MInst& px = emit(MOp::PIXLD);   // PIXLD.MY_INDEX
            px.d = vreg();
            out.r = {px.d, kRZ, kRZ, kRZ};
            break;
        }
        if (Builtin(n.sub) == Builtin::GsPosition && f_.stage == Stage::Geometry) {
            const int32_t handle = vertex_handle_.at(int(n.imm));
            each([&](int i) {
                MInst& ald = emit(MOp::ALD);
                ald.d = vreg();
                ald.aux = kPosition + 4 * uint32_t(i);
                ald.c = R(handle);
                return ald.d;
            });
            break;
        }
        if (Builtin(n.sub) == Builtin::OutPosition) {   // the position written so far
            each([&](int i) { return cur_[size_t(slot(SlotPosition, 0, 0, i))]; });
            break;
        }
        refuse("builtin input " + std::to_string(n.sub));
    }
    case Op::Output:
        each([&](int i) { return cur_[size_t(slot(SlotOutput, n.var, 0, i))]; });
        break;
    case Op::Uniform: {
        const uint8_t bank = uint8_t(3 + n.var);
        const Node& ix = f_.nodes.at(size_t(n.a));
        if (ix.op == Op::Const) {
            const uint32_t base = ix.bits[0] * 16;
            each([&](int i) {
                MInst& m = emit(MOp::MOV);
                m.d = vreg();
                m.b = Opnd::cb(bank, base + 4 * uint32_t(i));
                return m.d;
            });
        } else {
            const int32_t element = value(n.a).r[0];
            const int32_t bytes = alu(MOp::SHL, R(element), Opnd::imm(4));
            each([&](int i) {
                MInst& m = emit(MOp::LDC);
                m.d = vreg();
                m.a = R(bytes);
                m.b = Opnd::cb(bank, 4 * uint32_t(i));
                m.flags |= kIndexed;
                return m.d;
            });
        }
        break;
    }
    case Op::UniformWord: {
        const int32_t bytes = value(n.a).r[0];
        MInst& m = emit(MOp::LDC);
        m.d = vreg();
        m.a = R(bytes);
        m.b = Opnd::cb(uint8_t(3 + n.var), 0);
        m.flags |= kIndexed;
        out.r[0] = m.d;
        break;
    }
    case Op::Storage: {
        const Opnd address = storage_address(n.var, value(n.a).r[0]);
        MInst& m = emit(MOp::LDG);
        m.d = vreg();
        m.a = address;
        out.r[0] = m.d;
        break;
    }
    case Op::Shared: {
        const int32_t address = alu(MOp::SHL, R(value(n.a).r[0]), Opnd::imm(2));
        MInst& m = emit(MOp::LDS);
        m.d = vreg();
        m.a = R(address);
        out.r[0] = m.d;
        break;
    }
    case Op::Swizzle: {
        const Val a = value(n.a);
        each([&](int i) { return a.r[size_t(n.sw[size_t(i)])]; });
        break;
    }
    case Op::Construct: {
        int k = 0;
        for (int a : n.args) {
            const Val v = value(a);
            for (int i = 0; i < v.n && k < 4; ++i) out.r[size_t(k++)] = v.r[size_t(i)];
        }
        if (n.args.size() == 1 && k == 1 && w > 1)   // a scalar splat
            for (int i = 1; i < w; ++i) out.r[size_t(i)] = out.r[0];
        break;
    }
    case Op::Bitcast: {
        const Val a = value(n.a);
        out.r = a.r;
        break;
    }
    case Op::Convert: {
        const Val a = value_w(n.a, w);
        each([&](int i) { return convert(a.r[size_t(i)], a.k, n.type.k); });
        break;
    }
    case Op::Neg: {
        const Val a = value(n.a);
        if (n.type.k == Kind::F)   // exact sign flip, also for zero and NaN
            each([&](int i) { return alu(MOp::LOP32I, R(a.r[size_t(i)]), Opnd::imm(0x80000000u), {}, 0, uint8_t(Lop::XOR)); });
        else
            each([&](int i) { return alu(MOp::IADD, Opnd::rz(), neg(a.r[size_t(i)])); });
        break;
    }
    case Op::Abs: {
        const Val a = value(n.a);
        if (n.type.k == Kind::F)
            each([&](int i) { return alu(MOp::LOP32I, R(a.r[size_t(i)]), Opnd::imm(0x7fffffffu), {}, 0, uint8_t(Lop::AND)); });
        else if (n.type.k == Kind::S) {
            each([&](int i) {   // max(a, -a)
                const int32_t m = alu(MOp::IADD, Opnd::rz(), neg(a.r[size_t(i)]));
                return alu(MOp::IMNMX, R(a.r[size_t(i)]), R(m), {}, kSigned | kMax);
            });
        } else {
            out.r = a.r;
        }
        break;
    }
    case Op::BitNot:
    case Op::LNot: {
        const Val a = value(n.a);
        each([&](int i) { return alu(MOp::LOP32I, R(a.r[size_t(i)]), Opnd::imm(0xffffffffu), {}, 0, uint8_t(Lop::XOR)); });
        break;
    }
    case Op::Add: case Op::Sub: case Op::Mul: case Op::Div: case Op::Mod:
    case Op::Shl: case Op::Shr: case Op::And: case Op::Or: case Op::Xor: {
        const Val a = value_w(n.a, w), b = value_w(n.b, w);
        const Kind k = a.k;
        each([&](int i) -> int32_t {
            const int32_t x = a.r[size_t(i)], y = b.r[size_t(i)];
            if (k == Kind::F) {
                switch (n.op) {
                case Op::Add: return alu(MOp::FADD, R(x), R(y));
                case Op::Sub: return alu(MOp::FADD, R(x), neg(y));
                case Op::Mul: return alu(MOp::FMUL, R(x), R(y));
                case Op::Div: {   // x * rcp(y); 1/y alone is the reciprocal
                    const int32_t r = alu(MOp::MUFU, R(y), {}, {}, 0, uint8_t(Mufu::RCP));
                    const Node& xn = f_.nodes.at(size_t(n.a));
                    if (xn.op == Op::Const && xn.bits[size_t(xn.type.n == 1 ? 0 : i)] == 0x3f800000u) return r;
                    return alu(MOp::FMUL, R(x), R(r));
                }
                default: refuse("this float operation");
                }
            }
            switch (n.op) {
            case Op::Add: return alu(MOp::IADD, R(x), R(y));
            case Op::Sub: return alu(MOp::IADD, R(x), neg(y));
            case Op::Mul: {
                uint32_t c = 0;
                if (const_lane(n.b, i, c)) return imul_const(x, c);
                if (const_lane(n.a, i, c)) return imul_const(y, c);
                return imul(x, y);
            }
            case Op::Shl: return alu(MOp::SHL, R(x), R(y));
            case Op::Shr: return alu(MOp::SHR, R(x), R(y), {}, k == Kind::S ? kSigned : 0);
            case Op::And: return alu(MOp::LOP, R(x), R(y), {}, 0, uint8_t(Lop::AND));
            case Op::Or: return alu(MOp::LOP, R(x), R(y), {}, 0, uint8_t(Lop::OR));
            case Op::Xor: return alu(MOp::LOP, R(x), R(y), {}, 0, uint8_t(Lop::XOR));
            case Op::Div: case Op::Mod: {
                const Node& dn = f_.nodes.at(size_t(n.b));
                return divide(x, y, &dn, i, k == Kind::S, n.op == Op::Mod);
            }
            default: refuse("this integer operation");
            }
        });
        break;
    }
    case Op::Eq: case Op::Ne: case Op::Lt: case Op::Le: case Op::Gt: case Op::Ge: {
        const Val a = value(n.a);
        const Val b = value_w(n.b, a.n);
        const Val aw = value_w(n.a, b.n);
        uint8_t cmp = 0;
        if (aw.k == Kind::F) {   // as the SPIR-V writer: ordered, except not-equal (true for NaN)
            cmp = n.op == Op::Eq ? EQ : n.op == Op::Ne ? NEU : n.op == Op::Lt ? LT : n.op == Op::Le ? LE : n.op == Op::Gt ? GT : GE;
        } else {
            cmp = n.op == Op::Eq ? EQ : n.op == Op::Ne ? NE : n.op == Op::Lt ? LT : n.op == Op::Le ? LE : n.op == Op::Gt ? GT : GE;
        }
        each([&](int i) {
            return compare(aw.k == Kind::F ? MOp::FSET : MOp::ISET, aw.k, cmp, aw.r[size_t(i)], b.r[size_t(i)]);
        });
        break;
    }
    case Op::LAnd: case Op::LOr: {
        const Val a = value_w(n.a, w), b = value_w(n.b, w);
        each([&](int i) {
            return alu(MOp::LOP, R(a.r[size_t(i)]), R(b.r[size_t(i)]), {}, 0, uint8_t(n.op == Op::LAnd ? Lop::AND : Lop::OR));
        });
        break;
    }
    case Op::Select: {
        const Val c = value(n.a);
        const Val a = value_w(n.b, w), b = value_w(n.c, w);
        const int32_t p = truth(c.r[0]);
        each([&](int i) {
            MInst& s = emit(MOp::SEL);
            s.d = vreg();
            s.a = R(a.r[size_t(i)]);
            s.b = R(b.r[size_t(i)]);
            s.pa = p;
            return s.d;
        });
        break;
    }
    case Op::Call: out = call(n); break;
    case Op::Texture: out = texture(n); break;
    case Op::Atomic: out = atomic(n); break;
    }
    stamp_[size_t(id)] = generation_;
    return memo_[size_t(id)] = out;
}

Selector::Val Selector::call(const Node& n) {
    Val out;
    out.k = n.type.k;
    out.n = n.type.n;
    const int w = n.type.n;
    auto arg = [&](size_t i) { return value_w(n.args.at(i), w); };
    auto each = [&](auto fn) { for (int i = 0; i < w; ++i) out.r[size_t(i)] = fn(i); };
    auto mufu = [&](Mufu fn, int32_t v, bool abs = false) {
        MInst& m = emit(MOp::MUFU);
        m.d = vreg();
        m.a = R(v);
        m.a.abs = abs;
        m.sub = uint8_t(fn);
        return m.d;
    };
    auto rro = [&](int32_t v, bool exp) {   // range reduction ahead of MUFU.EX2 / SIN / COS
        MInst& m = emit(MOp::RRO);
        m.d = vreg();
        m.b = R(v);
        m.sub = exp ? 1 : 0;
        return m.d;
    };
    auto f2f = [&](int32_t v, Rnd r) {
        MInst& m = emit(MOp::F2F);
        m.d = vreg();
        m.b = R(v);
        m.sub = uint8_t(r);
        m.flags |= kLodZero;   // round to an integral value (F2F.F32.F32 with rounding)
        return m.d;
    };
    const Kind k0 = n.args.empty() ? Kind::F : f_.nodes.at(size_t(n.args[0])).type.k;
    switch (Fn(n.sub)) {
    case Fn::Fma: {
        const Val a = arg(0), b = arg(1), c = arg(2);
        each([&](int i) { return alu(MOp::FFMA, R(a.r[size_t(i)]), R(b.r[size_t(i)]), R(c.r[size_t(i)])); });
        break;
    }
    case Fn::Min: case Fn::Max: {
        const Val a = arg(0), b = arg(1);
        const uint32_t mx = Fn(n.sub) == Fn::Max ? kMax : 0;
        each([&](int i) {
            return alu(k0 == Kind::F ? MOp::FMNMX : MOp::IMNMX, R(a.r[size_t(i)]), R(b.r[size_t(i)]), {},
                       mx | (k0 == Kind::S ? kSigned : 0));
        });
        break;
    }
    case Fn::Clamp: {
        const Val a = arg(0), lo = arg(1), hi = arg(2);
        const Node& lon = f_.nodes.at(size_t(n.args[1]));
        const Node& hin = f_.nodes.at(size_t(n.args[2]));
        const bool saturate = lon.op == Op::Const && hin.op == Op::Const && lon.bits[0] == 0 && hin.bits[0] == 0x3f800000u;
        each([&](int i) {
            if (saturate)   // .SAT: NaN becomes 0, as NV_gpu_program4 specifies
                return alu(MOp::FADD, R(a.r[size_t(i)]), Opnd::rz(), {}, kSat);
            const int32_t m = alu(MOp::FMNMX, R(a.r[size_t(i)]), R(lo.r[size_t(i)]), {}, kMax);
            return alu(MOp::FMNMX, R(m), R(hi.r[size_t(i)]));
        });
        break;
    }
    case Fn::Floor: { const Val a = arg(0); each([&](int i) { return f2f(a.r[size_t(i)], Rnd::RM); }); break; }
    case Fn::Ceil: { const Val a = arg(0); each([&](int i) { return f2f(a.r[size_t(i)], Rnd::RP); }); break; }
    case Fn::Trunc: { const Val a = arg(0); each([&](int i) { return f2f(a.r[size_t(i)], Rnd::RZ); }); break; }
    case Fn::RoundEven: { const Val a = arg(0); each([&](int i) { return f2f(a.r[size_t(i)], Rnd::RN); }); break; }
    case Fn::Fract: {
        const Val a = arg(0);
        each([&](int i) { return alu(MOp::FADD, R(a.r[size_t(i)]), neg(f2f(a.r[size_t(i)], Rnd::RM))); });
        break;
    }
    case Fn::Exp2: { const Val a = arg(0); each([&](int i) { return mufu(Mufu::EX2, rro(a.r[size_t(i)], true)); }); break; }
    case Fn::Log2: { const Val a = arg(0); each([&](int i) { return mufu(Mufu::LG2, a.r[size_t(i)]); }); break; }
    case Fn::Sin: { const Val a = arg(0); each([&](int i) { return mufu(Mufu::SIN, rro(a.r[size_t(i)], false)); }); break; }
    case Fn::Cos: { const Val a = arg(0); each([&](int i) { return mufu(Mufu::COS, rro(a.r[size_t(i)], false)); }); break; }
    case Fn::InverseSqrt: { const Val a = arg(0); each([&](int i) { return mufu(Mufu::RSQ, a.r[size_t(i)]); }); break; }
    case Fn::Pow: {   // exp2(log2(|a|) * b)
        const Val a = arg(0), b = arg(1);
        each([&](int i) {
            const int32_t l = mufu(Mufu::LG2, a.r[size_t(i)], true);
            return mufu(Mufu::EX2, rro(alu(MOp::FMUL, R(l), R(b.r[size_t(i)])), true));
        });
        break;
    }
    case Fn::F2I: case Fn::F2U: {
        const Val a = arg(0);
        each([&](int i) { return convert(a.r[size_t(i)], Kind::F, Fn(n.sub) == Fn::F2I ? Kind::S : Kind::U); });
        break;
    }
    case Fn::Dp2: case Fn::Dp3: case Fn::Dp4: {
        const int len = Fn(n.sub) == Fn::Dp2 ? 2 : Fn(n.sub) == Fn::Dp3 ? 3 : 4;
        const Val a = value_w(n.args.at(0), len), b = value_w(n.args.at(1), len);
        int32_t acc = alu(MOp::FMUL, R(a.r[0]), R(b.r[0]));
        for (int c = 1; c < len; ++c) acc = alu(MOp::FFMA, R(a.r[size_t(c)]), R(b.r[size_t(c)]), R(acc));
        each([&](int) { return acc; });
        break;
    }
    case Fn::ImageSize: {   // bound TXQ on the image's handle word, which is also a texture handle
        const Image& img = f_.images.at(size_t(n.var));
        const std::string& t = img.target;
        const int kc = t == "1D" || t == "BUFFER" ? 1 : t == "3D" || t == "ARRAY2D" || t == "ARRAYCUBE" ? 3 : 2;
        if (img.handle.size() != 1 || img.handle[0].buffer != 14) refuse("an image size query on a combined handle");
        MInst& q = emit(MOp::TXQ);
        q.a = Opnd::rz();   // level 0
        q.b = Opnd::cb(2, img.handle[0].offset + 32);
        q.d = p_.new_group(kc);
        q.dn = uint8_t(kc);
        q.aux = (1u << kc) - 1;
        const int32_t d = q.d;
        for (int i = 0; i < 4; ++i) out.r[size_t(i)] = i < kc ? d + i : kRZ;
        break;
    }
    case Fn::ImageLoad: {
        const Image& img = f_.images.at(size_t(n.var));
        uint8_t type = 0;
        const Opnd coords = image_coords(img, n.args.at(0), type);
        if (n.args.size() > 1) refuse("multisample image loads");
        const int32_t handle = image_handle(img);
        // Raw 32-bit integer formats read their words (SULD.D); the others convert (SULD.P RGBA).
        int words = 0;
        if (img.format == "r32ui" || img.format == "r32i") words = 1;
        else if (img.format == "rg32ui" || img.format == "rg32i") words = 2;
        else if (img.format == "rgba32ui" || img.format == "rgba32i") words = 4;
        MInst& m = emit(MOp::SULD);
        const int got = words ? words : 4;
        m.d = p_.new_group(got);
        m.dn = uint8_t(got);
        m.a = coords;
        m.c = R(handle);
        m.aux2 = type;
        if (words) m.flags |= kHalf, m.aux = words == 1 ? 4 : words == 2 ? 5 : 6;
        const int32_t d = m.d;   // m moves once more instructions are emitted
        for (int i = 0; i < 4; ++i)
            out.r[size_t(i)] = i < got ? d + i : i == 3 ? mov32i(n.type.k == Kind::F ? 0x3f800000u : 1u) : kRZ;
        break;
    }
    case Fn::DFdx: case Fn::DFdy: {   // the quad partner's value, then FSWZADD: right - left, bottom - top
        if (f_.stage != Stage::Fragment) refuse("derivatives outside fragment programs");
        const bool x = Fn(n.sub) == Fn::DFdx;
        const Val a = arg(0);
        each([&](int i) {
            MInst& sh = emit(MOp::SHFL);   // SHFL.BFLY lane ^ 1 (x) or ^ 2 (y), clamp 0x1c03
            sh.d = vreg();
            sh.a = R(a.r[size_t(i)]);
            sh.aux = x ? 1 : 2;
            MInst& sw = emit(MOp::FSWZADD);
            sw.d = vreg();
            sw.a = R(sh.d);
            sw.b = R(a.r[size_t(i)]);
            sw.aux = x ? 0x99 : 0xa5;
            return sw.d;
        });
        break;
    }
    case Fn::IsNan: {
        const Val a = arg(0);
        each([&](int i) { return compare(MOp::FSET, Kind::F, NAN_, a.r[size_t(i)], a.r[size_t(i)]); });
        break;
    }
    case Fn::Any: {
        const Val a = value(n.args.at(0));
        int32_t acc = a.r[0];
        for (int c = 1; c < a.n; ++c) acc = alu(MOp::LOP, R(acc), R(a.r[size_t(c)]), {}, 0, uint8_t(Lop::OR));
        each([&](int) { return acc; });
        break;
    }
    case Fn::UnpackHalf2x16: {   // the two halves of a word, as floats
        const Val a = value(n.args.at(0));
        for (int i = 0; i < 2; ++i) {
            MInst& m = emit(MOp::F2F);
            m.d = vreg();
            m.b = R(a.r[0]);
            m.flags |= kHalf | (i ? kHigh : 0);
            out.r[size_t(i)] = m.d;
        }
        for (int i = 2; i < w; ++i) out.r[size_t(i)] = kRZ;
        break;
    }
    case Fn::PackHalf2x16: {
        const Val a = value_w(n.args.at(0), 2);
        int32_t h[2];
        for (int i = 0; i < 2; ++i) {
            MInst& m = emit(MOp::F2F);
            m.d = vreg();
            m.b = R(a.r[size_t(i)]);
            m.flags |= kSignedDst;   // F2F.F16.F32: to a half in the low bits
            m.sub = uint8_t(Rnd::RN);
            h[i] = m.d;
        }
        const int32_t hi = alu(MOp::SHL, R(h[1]), Opnd::imm(16));
        const int32_t r = alu(MOp::LOP, R(h[0]), R(hi), {}, 0, uint8_t(Lop::OR));
        each([&](int) { return r; });
        break;
    }
    default:
        refuse("function " + std::to_string(n.sub));
    }
    return out;
}

// Texture instructions, bindless as the game's binaries are: the 32-bit handle is
// the OR of its bank-2 words and goes first in the meta registers (Rb...), then the
// LOD or bias, the packed offset, the sample index or the depth reference.
// Coordinates go in Ra... with an array layer first, as an integer.
Selector::Val Selector::texture(const Node& n) {
    const TexOp& tx = f_.tex_ops.at(size_t(n.tex));
    const Texture& res = f_.textures.at(size_t(tx.resource));
    const std::string& t = res.target;
    Val out;
    out.k = n.type.k;
    out.n = n.type.n;
    struct Shape { uint8_t type; int coords; bool array, shadow, ms; };
    static const std::map<std::string, Shape> shapes = {
        {"1D", {0, 1, false, false, false}}, {"ARRAY1D", {1, 2, true, false, false}}, {"2D", {2, 2, false, false, false}},
        {"RECT", {2, 2, false, false, false}}, {"ARRAY2D", {3, 3, true, false, false}}, {"3D", {4, 3, false, false, false}},
        {"CUBE", {6, 3, false, false, false}}, {"ARRAYCUBE", {7, 4, true, false, false}},
        {"SHADOW1D", {0, 1, false, true, false}}, {"SHADOWARRAY1D", {1, 2, true, true, false}},
        {"SHADOW2D", {2, 2, false, true, false}}, {"SHADOWRECT", {2, 2, false, true, false}},
        {"SHADOWARRAY2D", {3, 3, true, true, false}}, {"SHADOWCUBE", {6, 3, false, true, false}},
        {"SHADOWARRAYCUBE", {7, 4, true, true, false}}, {"BUFFER", {0, 1, false, false, false}},
        {"2DMS", {2, 2, false, false, true}}, {"ARRAY2DMS", {3, 3, true, false, true}}};
    auto shape_it = shapes.find(t);
    if (shape_it == shapes.end()) refuse("texture target " + t);
    const Shape sh = shape_it->second;

    // The handle.
    const int32_t handle = vreg();
    {
        if (res.handle.empty()) throw Error(0, "maxwell: texture without a handle");
        bool first = true;
        for (const auto& w : res.handle) {
            if (w.buffer != 14) refuse("a texture handle outside program.buffer[14]");
            MInst& m = emit(first ? MOp::MOV : MOp::LOP);
            m.d = handle;
            if (!first) m.a = R(handle), m.sub = uint8_t(Lop::OR);
            m.b = Opnd::cb(2, w.offset + 32);
            first = false;
        }
    }
    auto group = [&](const std::vector<int32_t>& parts) {   // parts copied into consecutive registers
        const int32_t g = p_.new_group(int(parts.size()));
        for (size_t i = 0; i < parts.size(); ++i) mov(g + int32_t(i), parts[i]);
        return Opnd::reg(g, uint8_t(parts.size()));
    };
    auto layer_int = [&](int32_t v, bool from_float) {
        if (!from_float) return v;
        MInst& m = emit(MOp::F2I);   // F2I.U16, rounding to nearest, as NVIDIA's compiler does
        m.d = vreg();
        m.b = R(v);
        m.sub = uint8_t(Rnd::RN);
        m.flags |= kHalf;
        return m.d;
    };
    auto offsets = [&](int bits) {   // the constant texel offset packed at `bits` per component
        uint32_t packed = 0;
        const int dims = sh.array ? sh.coords - 1 : sh.coords;
        for (int i = 0; i < dims && i < 3; ++i) packed |= (uint32_t(tx.offset[size_t(i)]) & ((1u << (bits == 8 ? 6 : 4)) - 1)) << (bits * i);
        return mov32i(packed);
    };
    // Coordinates: GLSL order (x, y, z, layer) from the IR, hardware order (layer, x, y, z).
    auto coords = [&](bool floats) {
        const Val c = value(tx.coord);
        std::vector<int32_t> parts;
        const int spatial = sh.array ? sh.coords - 1 : sh.coords;
        if (sh.array) parts.push_back(layer_int(c.r[size_t(spatial)], floats));
        for (int i = 0; i < spatial; ++i) parts.push_back(c.r[size_t(i)]);
        return group(parts);
    };
    auto dest = [&](MInst& m, int components) {
        m.d = p_.new_group(components);
        m.dn = uint8_t(components);
        m.aux = (1u << components) - 1;   // R, RG, RGB or RGBA
        for (int i = 0; i < components; ++i) out.r[size_t(i)] = m.d + i;
    };
    auto is_zero = [&](int node) {
        const Node& z = f_.nodes.at(size_t(node));
        return z.op == Op::Const && z.bits[0] == 0;
    };

    switch (tx.func) {
    case TexOp::Size: {
        std::vector<int32_t> ra{handle, tx.lod >= 0 ? value(tx.lod).r[0] : kRZ};
        MInst m;
        m.op = MOp::TXQ;
        const Opnd a = group(ra);
        MInst& q = emit(MOp::TXQ);
        q.a = a;
        const int kc = t == "BUFFER" || t == "1D" ? 1 : (t == "3D" || sh.array) && t != "ARRAYCUBE" ? 3 : t == "ARRAYCUBE" ? 3 : 2;
        dest(q, kc);
        for (int i = kc; i < out.n; ++i) out.r[size_t(i)] = kRZ;
        return out;
    }
    case TexOp::Fetch:
    case TexOp::FetchMs: {
        const Opnd a = coords(false);
        std::vector<int32_t> meta{handle};
        uint32_t flags = kLodZero;
        if (t != "BUFFER" && tx.func == TexOp::Fetch && tx.lod >= 0 && !is_zero(tx.lod)) {
            meta.push_back(value(tx.lod).r[0]);
            flags = kLod;
        }
        if (tx.func == TexOp::Fetch && tx.has_offset && t != "BUFFER") meta.push_back(offsets(4)), flags |= kOffset;
        if (tx.func == TexOp::FetchMs) meta.push_back(value(tx.lod).r[0]), flags |= kMultisample;
        const Opnd b = group(meta);
        MInst& m = emit(MOp::TLD);
        m.a = a;
        m.b = b;
        m.sub = sh.type;
        m.flags = flags;
        dest(m, out.n);
        return out;
    }
    case TexOp::Gather: {
        const Opnd a = coords(true);
        std::vector<int32_t> meta{handle};
        uint32_t flags = 0;
        if (tx.has_offset) meta.push_back(offsets(8)), flags |= kOffset;
        if (tx.ref >= 0) meta.push_back(value(tx.ref).r[0]), flags |= kDepthCompare;
        const Opnd b = group(meta);
        MInst& m = emit(MOp::TLD4);
        m.a = a;
        m.b = b;
        m.sub = sh.type;
        m.flags = flags;
        m.aux2 = uint32_t(tx.gather_component & 3);
        dest(m, 4);
        return out;
    }
    case TexOp::SampleGrad: {
        // Explicit gradients, as NVIDIA's compiler (and nouveau) do it for every
        // target: once per lane l of the quad, all four lanes take lane l's
        // coordinates and gradients, offset so that lane l samples its own
        // coordinate and the quad's implicit derivatives are the gradients; lane l
        // keeps that sample.
        if (sh.shadow || tx.ref >= 0 || tx.has_offset) refuse("shadow or offset texture gradients");
        const Val c = value(tx.coord), gx = value(tx.ddx), gy = value(tx.ddy);
        const int spatial = sh.array ? sh.coords - 1 : sh.coords;
        // NVASM_MAXWELL_TXD_LOOP (a test switch) sends 1D and 2D through the quad loop
        // too, where the round trip's mipmapped 2D textures can check it.
        static const bool loop_all = [] {
            char* v = nullptr;
            size_t length = 0;
            const bool set = _dupenv_s(&v, &length, "NVASM_MAXWELL_TXD_LOOP") == 0 && v;
            std::free(v);
            return set;
        }();
        if (sh.type <= 3 && !loop_all) {   // 1D and 2D (arrays too): TXD.B, gradients interleaved x, y per axis
            std::vector<int32_t> parts{handle};
            for (int k = 0; k < spatial; ++k) parts.push_back(c.r[size_t(k)]);
            if (sh.array) parts.push_back(layer_int(c.r[size_t(spatial)], true));
            std::vector<int32_t> grads;
            for (int k = 0; k < spatial; ++k) grads.push_back(gx.r[size_t(k)]), grads.push_back(gy.r[size_t(k)]);
            const Opnd a = group(parts);
            const Opnd b = group(grads);
            MInst& m = emit(MOp::TXD);
            m.a = a;
            m.b = b;
            m.sub = sh.type;
            dest(m, out.n);
            return out;
        }
        if (f_.stage != Stage::Fragment) refuse("3D or cube texture gradients outside fragment programs");
        const int32_t lane = alu(MOp::LOP32I, R(special(0, true)), Opnd::imm(3), {}, 0, uint8_t(Lop::AND));   // SR_LANEID & 3
        auto lane_is = [&](uint32_t mask, uint8_t cmp, uint32_t v) {
            const int32_t bits = mask == 3 ? lane : alu(MOp::LOP32I, R(lane), Opnd::imm(mask), {}, 0, uint8_t(Lop::AND));
            MInst& t = emit(MOp::ISETP);
            t.pd = pred();
            t.cmp = cmp;
            t.a = R(bits);
            t.b = Opnd::imm(v);
            return t.pd;
        };
        const int32_t right = lane_is(1, NE, 0), below = lane_is(2, NE, 0);
        auto from_lane = [&](int32_t v, int l) {
            MInst& s = emit(MOp::SHFL);
            s.d = vreg();
            s.a = R(v);
            s.aux = uint32_t(l);
            s.flags |= kIndexed;
            return s.d;
        };
        // Lane l's value plus the gradient in the lanes on the other side of it.
        auto step = [&](int32_t base, int32_t gradient, int32_t side, bool l_on_side) {
            MInst& s = emit(MOp::SEL);
            s.d = vreg();
            s.a = l_on_side ? Opnd::rz() : R(gradient);
            s.b = l_on_side ? R(gradient) : Opnd::rz();
            s.pa = side;
            const int32_t amount = s.d;
            return alu(MOp::FADD, R(base), l_on_side ? neg(amount) : R(amount));
        };
        const int components = out.n;
        std::array<int32_t, 4> result{kRZ, kRZ, kRZ, kRZ};
        for (int l = 0; l < 4; ++l) {
            std::vector<int32_t> parts;
            if (sh.array) parts.push_back(layer_int(from_lane(c.r[size_t(spatial)], l), true));
            for (int k = 0; k < spatial; ++k) {
                int32_t v = from_lane(c.r[size_t(k)], l);
                v = step(v, from_lane(gx.r[size_t(k)], l), right, (l & 1) != 0);
                v = step(v, from_lane(gy.r[size_t(k)], l), below, (l & 2) != 0);
                parts.push_back(v);
            }
            const Opnd a = group(parts);
            const Opnd b = group({handle});
            MInst& m = emit(MOp::TEX);
            m.a = a;
            m.b = b;
            m.sub = sh.type;
            dest(m, components);
            const int32_t mine = lane_is(3, EQ, uint32_t(l));
            for (int i = 0; i < components; ++i) {
                MInst& s = emit(MOp::SEL);
                s.d = vreg();
                s.a = R(out.r[size_t(i)]);
                s.b = R(result[size_t(i)]);
                s.pa = mine;
                result[size_t(i)] = s.d;
            }
        }
        for (int i = 0; i < components; ++i) out.r[size_t(i)] = result[size_t(i)];
        return out;
    }
    default: break;
    }
    const Opnd a = coords(true);
    std::vector<int32_t> meta{handle};
    uint32_t flags = 0;
    if (tx.func == TexOp::SampleBias) meta.push_back(value(tx.lod).r[0]), flags |= kBias;
    else if (tx.func == TexOp::SampleLod) {
        if (is_zero(tx.lod)) flags |= kLodZero;
        else meta.push_back(value(tx.lod).r[0]), flags |= kLod;
    } else if (f_.stage != Stage::Fragment) {
        flags |= kLodZero;   // no derivatives outside fragment programs
    }
    if (tx.has_offset) meta.push_back(offsets(4)), flags |= kOffset;
    if (tx.ref >= 0 || sh.shadow) {
        if (tx.ref < 0) refuse("a shadow lookup without a reference");
        meta.push_back(value(tx.ref).r[0]);
        flags |= kDepthCompare;
    }
    const Opnd b = group(meta);
    MInst& m = emit(MOp::TEX);
    m.a = a;
    m.b = b;
    m.sub = sh.type;
    m.flags = flags;
    dest(m, (flags & kDepthCompare) ? 1 : out.n);
    if (flags & kDepthCompare)
        for (int i = 1; i < out.n; ++i) out.r[size_t(i)] = out.r[0];
    return out;
}

// ---------------------------------------------------------------- statements

void Selector::assign(const Stmt& s) {
    const Val v0 = value(s.value);
    std::vector<int32_t> dst;
    std::array<int32_t, 4>* out = nullptr;
    if (s.target == Target::Reg && local_base_[size_t(s.var)] >= 0) {
        memory_ = true;
        int32_t address = -1;
        for (size_t i = 0; i < s.lanes.size(); ++i) {
            uint32_t offset = 0;
            const int32_t a = local_address(s.var, s.index, s.lanes[i], offset);
            if (address < 0 || a == kRZ) address = a;
            MInst& st = emit(MOp::STL);
            st.a = R(address);
            st.b = R(v0.n == 1 ? v0.r[0] : v0.r[i]);
            st.aux = offset;
        }
        return;
    }
    (void)dst;
    (void)out;
    if (s.target == Target::Reg && select_array_[size_t(s.var)] && s.index >= 0 &&
        f_.nodes.at(size_t(s.index)).op != Op::Const) {
        const int32_t index = value(s.index).r[0];
        for (int e = 0; e < array_size(s.var); ++e) {
            const int32_t is = index_is(index, e);
            for (size_t i = 0; i < s.lanes.size(); ++i) {
                const int32_t sl = slot(SlotReg, s.var, e, s.lanes[i]);
                MInst& sel = emit(MOp::SEL);
                sel.d = vreg();
                sel.a = R(v0.n == 1 ? v0.r[0] : v0.r[i]);
                sel.b = R(cur_[size_t(sl)]);
                sel.pa = is;
                cur_[size_t(sl)] = sel.d;
            }
        }
        return;
    }
    if (s.target == Target::Output) output_vars_.insert(s.var);
    if (s.target == Target::Position) position_written_ = true;
    if (s.target == Target::FragDepth) depth_written_ = true;
    // Lane i names value component i (a scalar is replicated). A carried vreg changes
    // at the loop's flushes, so a slot never names another slot's carried vreg: copy.
    const std::vector<int32_t> slots = target_slots(s);
    for (size_t i = 0; i < slots.size(); ++i) {
        int32_t v = v0.n == 1 ? v0.r[0] : v0.r[i];
        if (carried_.count(v)) v = copy(v);
        cur_[size_t(slots[i])] = v;
    }
}

void Selector::stmts(const std::vector<Stmt>& list) {   // a nested body
    auto specials = specials_;
    auto builtins = builtins_;
    for (const Stmt& s : list) {
        ++generation_;   // values do not cross statements
        stmt(s);
    }
    specials_ = std::move(specials);
    builtins_ = std::move(builtins);
}

void Selector::stmt(const Stmt& s) {
    auto one_liner = [&](Stmt::K k) {
        return s.other.empty() && s.body.size() == 1 && s.body[0].k == k;
    };
    switch (s.k) {
    case Stmt::Assign: assign(s); break;
    case Stmt::DeclLocal: {
        const Val v = value(s.value);
        for (int i = 0; i < v.n; ++i) {
            int32_t x = v.r[size_t(i)];
            if (carried_.count(x)) x = copy(x);
            cur_[size_t(slot(SlotLocal, s.var, 0, i))] = x;
        }
        break;
    }
    case Stmt::If: {
        const int32_t p = truth(value(s.cond).r[0]);
        // A conditional break, continue or kill is one predicated instruction.
        if (one_liner(Stmt::Break) || one_liner(Stmt::Continue) || one_liner(Stmt::Kill)) {
            const Stmt::K k = s.body[0].k;
            if (k != Stmt::Kill && loops_.empty()) throw Error(0, "maxwell: break outside a loop");
            if (k != Stmt::Kill) flush(loops_.back());   // harmless for threads that stay: the values are current
            MInst& i = emit(k == Stmt::Break ? MOp::BRK : k == Stmt::Continue ? MOp::CONT : MOp::KIL);
            i.guard = p;
            if (k == Stmt::Break) i.target = loops_.back().exit;
            if (k == Stmt::Continue) i.target = loops_.back().cont;
            break;
        }
        const int32_t end = p_.new_label(), other = s.other.empty() ? -1 : p_.new_label();
        const size_t before = p_.code.size();
        const std::vector<int32_t> snapshot = cur_;
        MInst& ssy = emit(MOp::SSY);
        ssy.target = end;
        {
            MInst& skip = emit(s.other.empty() ? MOp::SYNC : MOp::BRA);   // threads with a false condition
            skip.guard = p;
            skip.guard_neg = true;
            skip.target = s.other.empty() ? end : other;
        }
        stmts(s.body);
        const std::vector<int32_t> after_then = cur_;
        const size_t then_end = p_.code.size();
        emit(MOp::SYNC).target = end;
        std::vector<int32_t> after_else;
        size_t else_end = SIZE_MAX;
        if (!s.other.empty()) {
            emit(MOp::Label).target = other;
            cur_ = snapshot;
            cur_.resize(after_then.size(), kRZ);
            stmts(s.other);
            after_else = cur_;
            else_end = p_.code.size();
            emit(MOp::SYNC).target = end;
        }
        emit(MOp::Label).target = end;
        // Merges: a slot the two paths leave in different vregs gets a fresh one, written
        // at the end of each path (before the SSY for the path that skips the body).
        const size_t slots = std::max({snapshot.size(), after_then.size(), after_else.size()});
        cur_.resize(slots, kRZ);
        auto at = [](const std::vector<int32_t>& v, size_t i) { return i < v.size() ? v[i] : kRZ; };
        std::vector<MInst> at_before, at_then, at_else;
        auto move = [](int32_t d, int32_t v) { MInst m; m.op = MOp::MOV; m.d = d; m.b = Opnd::reg(v); return m; };
        for (size_t sl = 0; sl < slots; ++sl) {
            const int32_t a = at(after_then, sl), b = s.other.empty() ? at(snapshot, sl) : at(after_else, sl);
            if (a == b) { cur_[sl] = a; continue; }
            const int32_t j = vreg();
            cur_[sl] = j;
            at_then.push_back(move(j, a));
            (s.other.empty() ? at_before : at_else).push_back(move(j, b));
        }
        if (else_end != SIZE_MAX) p_.code.insert(p_.code.begin() + std::ptrdiff_t(else_end), at_else.begin(), at_else.end());
        p_.code.insert(p_.code.begin() + std::ptrdiff_t(then_end), at_then.begin(), at_then.end());
        p_.code.insert(p_.code.begin() + std::ptrdiff_t(before), at_before.begin(), at_before.end());
        break;
    }
    case Stmt::Loop: {
        const int32_t head = p_.new_label(), exit = p_.new_label(), cont = p_.new_label();
        int32_t counter = -1;
        if (s.count >= 0) {
            counter = vreg();
            mov(counter, value(s.count).r[0]);
        }
        // The slots the body writes are carried around the loop in their own vregs.
        std::set<int32_t> written;
        slots_written(s.body, written);
        LoopLabels labels{exit, cont, {}};
        for (int32_t sl : written) {
            const int32_t v = vreg();
            mov(v, cur_[size_t(sl)]);
            labels.carried.push_back({sl, v});
            carried_.insert(v);
        }
        for (auto [sl, v] : labels.carried) cur_[size_t(sl)] = v;
        // PCNT only for a loop that continues: each iteration pushes it and CONT pops it.
        std::function<bool(const std::vector<Stmt>&)> continues = [&](const std::vector<Stmt>& list) {
            for (const Stmt& c : list) {
                if (c.k == Stmt::Continue) return true;
                if (c.k != Stmt::Loop && (continues(c.body) || continues(c.other))) return true;
            }
            return false;
        };
        const bool has_continue = continues(s.body);
        MInst& pbk = emit(MOp::PBK);
        pbk.target = exit;
        emit(MOp::Label).target = head;
        if (has_continue) {
            MInst& pcnt = emit(MOp::PCNT);
            pcnt.target = cont;
        }
        ++generation_;
        if (counter >= 0) {   // leave when the count is used up
            MInst& t = emit(MOp::ISETP);
            t.pd = pred();
            t.cmp = LE;
            t.flags |= kSigned;
            t.a = R(counter);
            t.b = Opnd::rz();
            MInst& brk = emit(MOp::BRK);
            brk.guard = t.pd;
            brk.target = exit;
        }
        loops_.push_back(labels);
        stmts(s.body);
        flush(loops_.back());
        loops_.pop_back();
        if (has_continue) {
            MInst& c = emit(MOp::CONT);   // the end of the body: every thread continues
            c.target = cont;
        }
        emit(MOp::Label).target = cont;
        ++generation_;
        if (counter >= 0) {
            MInst& dec = emit(MOp::IADD32I);
            dec.d = counter;
            dec.a = R(counter);
            dec.b = Opnd::imm(0xffffffffu);
        }
        MInst& back = emit(MOp::BRA);
        back.target = head;
        emit(MOp::Label).target = exit;
        for (auto [sl, v] : labels.carried) cur_[size_t(sl)] = v;
        break;
    }
    case Stmt::Break: {
        if (loops_.empty()) throw Error(0, "maxwell: break outside a loop");
        flush(loops_.back());
        MInst& b = emit(MOp::BRK);
        b.target = loops_.back().exit;
        break;
    }
    case Stmt::Continue: {
        if (loops_.empty()) throw Error(0, "maxwell: continue outside a loop");
        flush(loops_.back());
        MInst& c = emit(MOp::CONT);
        c.target = loops_.back().cont;
        break;
    }
    case Stmt::Kill: emit(MOp::KIL); break;
    case Stmt::Emit: emit_vertex(1); break;
    case Stmt::EndPrimitive: emit_vertex(2); break;
    case Stmt::Demote: refuse("demote (translate with demote off)");
    case Stmt::StoreStorage: {
        const int32_t v = value(s.value).r[0];
        const Opnd address = storage_address(s.var, value(s.a).r[0]);
        MInst& st = emit(MOp::STG);
        st.a = address;
        st.b = R(v);
        break;
    }
    case Stmt::StoreShared: {
        const int32_t v = value(s.value).r[0];
        const int32_t address = alu(MOp::SHL, R(value(s.a).r[0]), Opnd::imm(2));
        MInst& st = emit(MOp::STS);
        st.a = R(address);
        st.b = R(v);
        break;
    }
    case Stmt::ImageStore: {
        const Image& img = f_.images.at(size_t(s.var));
        if (s.index >= 0) refuse("multisample image stores");
        uint8_t type = 0;
        const Opnd coords = image_coords(img, s.a, type);
        const Val v = value_w(s.value, 4);
        std::vector<int32_t> parts(v.r.begin(), v.r.end());
        const int32_t g = p_.new_group(4);
        for (int i = 0; i < 4; ++i) mov(g + i, parts[size_t(i)]);
        const int32_t handle = image_handle(img);
        MInst& st = emit(MOp::SUST);
        st.a = coords;
        st.b = Opnd::reg(g, 4);
        st.c = R(handle);
        st.aux2 = type;
        break;
    }
    case Stmt::Barrier: emit(MOp::BAR); break;
    case Stmt::GroupMemoryBarrier: emit(MOp::MEMBAR).aux = 0; break;
    case Stmt::MemoryBarrier: emit(MOp::MEMBAR).aux = 1; break;
    case Stmt::Block: stmts(s.body); break;
    case Stmt::Expr: value(s.value); break;
    default: refuse("statement kind " + std::to_string(int(s.k)));
    }
}

// Handles for the input vertices the program reads, as NVIDIA's compiler makes
// them: vertex i of this invocation's primitive is at invocation * vertices + i;
// a vertex past the primitive's count reads vertex handle 0.
void Selector::geometry_prologue() {
    std::set<int> vertices;
    for (const Node& n : f_.nodes) {
        if (n.op == Op::Input || (n.op == Op::Builtin && Builtin(n.sub) == Builtin::GsPosition)) {
            if (n.imm < 0) refuse("geometry inputs of a vertex chosen at run time");
            vertices.insert(int(n.imm));
        }
    }
    const int32_t info = special(29);   // SR_INVOCATION_INFO: vertices per primitive << 16 | invocation
    for (int v : vertices) {
        const int32_t index = v ? mov32i(uint32_t(v)) : kRZ;
        MInst& mad = emit(MOp::VMAD);
        mad.d = vreg();
        mad.a = R(info);
        mad.c = R(index);
        const int32_t offset = mad.d;
        MInst& isberd = emit(MOp::ISBERD);
        isberd.d = vreg();
        isberd.a = R(offset);
        const int32_t handle = isberd.d;
        MInst& past = emit(MOp::VSETP);
        past.pd = pred();
        past.a = R(info);
        past.aux = uint32_t(v);
        const int32_t p = past.pd;
        MInst& zero = emit(MOp::MOV32I);
        zero.d = handle;
        zero.a = Opnd::imm(0);
        zero.guard = p;
        vertex_handle_[v] = handle;
    }
}

// EMIT / ENDPRIM: store every output to the current output vertex, then OUT.
void Selector::emit_vertex(uint32_t how) {
    const int32_t sl = slot(SlotOutVertex, 0, 0, 0);
    const int32_t handle = cur_[size_t(sl)];
    auto store = [&](uint32_t address, int32_t v) {
        MInst& s = emit(MOp::AST);
        s.a = R(v);
        s.aux = address;
        s.c = R(handle);
    };
    if (how & 1) {
        if (f_.layer) store(kLayer, cur_[size_t(slot(SlotLayer, 0, 0, 0))]);
        if (f_.position)
            for (int c = 0; c < 4; ++c) store(kPosition + 4 * uint32_t(c), cur_[size_t(slot(SlotPosition, 0, 0, c))]);
        for (const auto& o : f_.outputs)
            for (int c = 0; c < 4; ++c)
                store(kGeneric + 16 * uint32_t(o.location) + 4 * uint32_t(c), cur_[size_t(slot(SlotOutput, 1000 + o.location, 0, c))]);
    }
    MInst& out = emit(MOp::OUT);
    out.d = vreg();
    out.a = R(handle);
    out.aux = how;
    cur_[size_t(sl)] = out.d;
}

void Selector::finish() {
    if (f_.stage == Stage::Fragment) {
        // Enabled colour targets in order, four registers each from R0 (a target
        // without output takes none), then the sample mask slot, then depth.
        int32_t next = 0;
        std::vector<std::pair<int32_t, int32_t>> moves;
        for (int loc : output_vars_) {
            if (loc >= 1000) throw Error(0, "maxwell: generic output from a fragment program");
            for (int c = 0; c < 4; ++c) moves.push_back({next + c, cur_[size_t(slot(SlotOutput, loc, 0, c))]});
            next += 4;
        }
        if (depth_written_) moves.push_back({next + 1, cur_[size_t(slot(SlotDepth, 0, 0, 0))]});
        std::vector<int32_t> fixed;
        for (auto [hw, v] : moves) {
            const int32_t d = vreg();
            p_.fixed[size_t(d)] = hw;
            mov(d, v);
            fixed.push_back(d);
        }
        emit(MOp::EXIT).uses = fixed;   // EXIT hands the colour registers over
        return;
    }
    if (f_.stage == Stage::Vertex) {
        auto store = [&](uint32_t address, int32_t v) {
            MInst& s = emit(MOp::AST);
            s.a = R(v);
            s.aux = address;
        };
        if (f_.layer && !stored_.count(slot(SlotLayer, 0, 0, 0))) store(kLayer, cur_[size_t(slot(SlotLayer, 0, 0, 0))]);
        if (position_written_)
            for (int c = 0; c < 4; ++c)
                if (!stored_.count(slot(SlotPosition, 0, 0, c)))
                    store(kPosition + 4 * uint32_t(c), cur_[size_t(slot(SlotPosition, 0, 0, c))]);
        for (int loc : output_vars_)
            for (int c = 0; c < 4; ++c)
                if (!stored_.count(slot(SlotOutput, loc, 0, c)))
                    store(kGeneric + 16 * uint32_t(loc - 1000) + 4 * uint32_t(c), cur_[size_t(slot(SlotOutput, loc, 0, c))]);
        emit(MOp::EXIT);
        return;
    }
    if (f_.stage == Stage::Geometry) {
        // The output handle current at the end, for backends that close the output at EXIT (Ampere's OUT.FINAL):
        // aux2 = vreg + 1, not a use, so Maxwell's allocation and code are unchanged.
        const int32_t handle = cur_[size_t(slot(SlotOutVertex, 0, 0, 0))];
        emit(MOp::EXIT).aux2 = handle >= 0 ? uint32_t(handle) + 1 : 0;
        return;
    }
    if (f_.stage == Stage::Compute) {
        emit(MOp::EXIT);
        return;
    }
    refuse("this stage");
}

Program Selector::run() {
    if (f_.stage != Stage::Fragment && f_.stage != Stage::Vertex && f_.stage != Stage::Compute && f_.stage != Stage::Geometry)
        refuse("tessellation programs");
    // Arrays indexed at run time anywhere go to local memory, whole.
    local_base_.assign(f_.regs.size(), -1);
    std::vector<bool> dynamic(f_.regs.size(), false);
    auto is_dynamic = [&](int index) { return index >= 0 && f_.nodes.at(size_t(index)).op != Op::Const; };
    for (const Node& n : f_.nodes)
        if (n.op == Op::Reg && is_dynamic(n.a)) dynamic[size_t(n.var)] = true;
    std::function<void(const std::vector<Stmt>&)> scan = [&](const std::vector<Stmt>& list) {
        for (const Stmt& s : list) {
            if (s.k == Stmt::Assign && s.target == Target::Reg && is_dynamic(s.index)) dynamic[size_t(s.var)] = true;
            scan(s.body);
            scan(s.other);
        }
    };
    scan(f_.body);
    select_array_.assign(f_.regs.size(), false);
    for (size_t r = 0; r < f_.regs.size(); ++r)
        if (dynamic[r] && f_.stage == Stage::Compute) {
            select_array_[r] = true;
        } else if (dynamic[r]) {
            local_base_[r] = int32_t(local_bytes_);
            local_bytes_ += 16 * uint32_t(std::max(1, f_.regs[r].array));
        }
    // Registers start at zero, as in the other backends: every slot starts as RZ.
    // Fragment: 1/w for perspective-correct inputs, from the interpolated position w.
    if (f_.stage == Stage::Fragment) {
        bool perspective = false;
        for (const auto& in : f_.inputs) perspective |= in.interpolation.empty();
        if (perspective) {
            MInst& w = emit(MOp::IPA);
            w.d = vreg();
            w.aux = kPosition + 12;
            const int32_t rw = w.d;
            MInst& r = emit(MOp::MUFU);
            r.d = vreg();
            r.a = R(rw);
            r.sub = uint8_t(Mufu::RCP);
            rcp_w_ = r.d;
        }
    }
    if (f_.stage == Stage::Geometry) geometry_prologue();
    std::vector<std::vector<int32_t>> store_after(f_.body.size());
    if (f_.stage == Stage::Vertex) {
        for (int c = 0; c < 4; ++c) output_address_[slot(SlotPosition, 0, 0, c)] = kPosition + 4 * uint32_t(c);
        if (f_.layer) output_address_[slot(SlotLayer, 0, 0, 0)] = kLayer;
        for (const auto& o : f_.outputs)
            for (int c = 0; c < 4; ++c)
                output_address_[slot(SlotOutput, 1000 + o.location, 0, c)] = kGeneric + 16 * uint32_t(o.location) + 4 * uint32_t(c);
        std::map<int32_t, size_t> last;   // output slot -> the last top-level statement writing it
        for (size_t k = 0; k < f_.body.size(); ++k) {
            std::set<int32_t> written;
            slots_written(f_.body[k], written);
            for (int32_t sl : written)
                if (output_address_.count(sl)) last[sl] = k;
        }
        for (auto [sl, k] : last) store_after[k].push_back(sl);
    }
    for (size_t k = 0; k < f_.body.size(); ++k) {
        ++generation_;
        stmt(f_.body[k]);
        for (int32_t sl : store_after[k]) store_output(sl);
    }
    finish();
    return std::move(p_);
}

} // namespace

maxwell::Program select_maxwell(const Function& f, bool& memory, uint32_t& local_bytes) {
    Selector s(f);
    maxwell::Program p = s.run();
    memory = s.uses_memory();
    local_bytes = s.local_bytes();
    return p;
}

} // namespace nvasm::ir

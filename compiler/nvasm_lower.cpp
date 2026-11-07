// Lowering: parsed NV_gpu_program5 program -> IR. Every semantic decision of the
// translator lives here; nvasm_glsl.cpp and nvasm_spirv.cpp only express it.
//
// Semantics follow the Khronos NV_gpu_program4/5, NV_parameter_buffer_object2,
// NV_compute_program5, NV_bindless_texture and EXT_shader_image_load_store
// specifications, with the float arithmetic of the Maxwell binaries (see
// README.md). Temporaries are untyped 32-bit registers held as vec4; integer
// operations bit-cast, so no value is ever converted by accident.
#include "nvasm_ir.h"
#include "nvasm_lower.h"
#include "nvasm_program.h"

#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <set>

namespace nvasm::lower {

using namespace detail;
using ir::Kind;
using ir::Node;
using ir::Op;
using ir::Stmt;
using ir::Type;

namespace {

Kind kind_of(char t) { return t == 'F' ? Kind::F : t == 'S' ? Kind::S : t == 'U' ? Kind::U : Kind::B; }

struct Ex {
    int node = -1;
    Kind k = Kind::F;
    int n = 1;
    int truthy = -1; // bool node true iff the (scalar) value is nonzero / 1.0
};

struct TargetInfo {
    int coords;  // coordinate components (including layer)
    bool shadow; // depth comparison
    bool array;
};

const TargetInfo* target_info(const std::string& t) {
    static const std::map<std::string, TargetInfo> m = {
        {"1D", {1, false, false}},        {"2D", {2, false, false}},           {"3D", {3, false, false}},
        {"CUBE", {3, false, false}},      {"ARRAY1D", {2, false, true}},       {"ARRAY2D", {3, false, true}},
        {"ARRAYCUBE", {4, false, true}},  {"SHADOW1D", {1, true, false}},      {"SHADOW2D", {2, true, false}},
        {"SHADOWCUBE", {3, true, false}}, {"SHADOWARRAY1D", {2, true, true}},  {"SHADOWARRAY2D", {3, true, true}},
        {"SHADOWARRAYCUBE", {4, true, true}}, {"BUFFER", {1, false, false}},   {"2DMS", {2, false, false}},
        {"ARRAY2DMS", {3, false, true}},
    };
    auto it = m.find(t);
    return it == m.end() ? nullptr : &it->second;
}

struct Storage {
    char t;    // F, S, U
    int count; // components
};

std::optional<Storage> storage_mod(const Instr& in) {
    for (const auto& m : in.mods) {
        if (m.size() < 3) continue;
        char t = m[0];
        if (t != 'F' && t != 'S' && t != 'U') continue;
        std::string rest = m.substr(1);
        if (rest == "32") return Storage{t, 1};
        if (rest == "32X2") return Storage{t, 2};
        if (rest == "32X4") return Storage{t, 4};
    }
    return std::nullopt;
}

std::string image_format(const Storage& s) {
    std::string base = s.count == 1 ? "r32" : s.count == 2 ? "rg32" : "rgba32";
    return base + (s.t == 'F' ? "f" : s.t == 'S' ? "i" : "ui");
}

bool writes_cc(const Instr& in) { return in.has("CC") || in.has("CC0") || in.has("CC1"); }

// (register, component) pairs a node's condition tests read.
std::vector<int> cc_reads(const detail::Node& n) {
    std::vector<int> r;
    auto add = [&](const std::string& test, const std::string& suffix, const std::vector<int>& lanes) {
        int reg = (test.back() == '1') ? 1 : 0;
        if (test == "TR" || test == "FL") return;
        if (suffix.empty()) {
            for (int l : lanes) r.push_back(reg * 4 + l);
        } else if (suffix.size() == 1) {
            r.push_back(reg * 4 + comp_index(suffix[0]));
        } else {
            for (int l : lanes) r.push_back(reg * 4 + comp_index(suffix[l]));
        }
    };
    const Instr& in = n.ins;
    if (n.kind == detail::Node::If) {
        add(in.ops.at(0).name, in.ops[0].suffix, {0, 1, 2, 3});
    } else if (n.kind == detail::Node::Ins) {
        if ((in.op == "BRK" || in.op == "CONT" || in.op == "KIL") && !in.ops.empty() && in.ops[0].kind == Operand::CCTest)
            add(in.ops[0].name, in.ops[0].suffix, {0, 1, 2, 3});
        if (!in.ops.empty() && !in.ops[0].pred.empty()) {
            std::vector<int> lanes;
            if (in.ops[0].suffix.empty()) lanes = {0, 1, 2, 3};
            else
                for (char c : in.ops[0].suffix) lanes.push_back(comp_index(c));
            add(in.ops[0].pred, in.ops[0].pred_suffix, lanes);
        }
    }
    std::sort(r.begin(), r.end());
    r.erase(std::unique(r.begin(), r.end()), r.end());
    return r;
}

std::vector<int> lanes_of(const Operand& d) {
    if (d.suffix.empty()) return {0, 1, 2, 3};
    std::vector<int> l;
    for (char c : d.suffix) l.push_back(comp_index(c));
    return l;
}

int swz(const Operand& o, int lane) {
    if (o.suffix.empty()) return lane;
    if (o.suffix.size() == 1) return comp_index(o.suffix[0]);
    if (o.suffix.size() == 4) return comp_index(o.suffix[lane]);
    return -1;
}

class Lowerer {
public:
    Lowerer(Program& p, const Options& o) : p_(p), opt_(o) {}
    ir::Function run(Result& r);

private:
    Program& p_; // run() moves the instructions into the block tree
    const Options& opt_;
    ir::Function f_;
    std::vector<std::vector<Stmt>*> sinks_;

    std::vector<int> reg_index_; // Temp::id -> f_.regs index (-1: LONG)
    std::vector<char> read_;     // by Temp::id
    const Temp* temp(const Operand& o) const {
        return o.temp < 0 ? nullptr : &p_.temp_info[size_t(o.temp)];
    }
    TempKind temp_kind(const Operand& o) const { return temp(o)->kind; }
    int cc_local_[2] = {-1, -1};
    int predicates_ = 0, loops_ = 0;

    struct UniformUse { uint32_t bytes = 0; bool dynamic = false; };
    std::map<int, UniformUse> ubos_;
    std::map<int, std::pair<bool, bool>> sbos_;
    std::map<std::pair<uint32_t, uint32_t>, char> words_;
    std::map<const Instr*, std::vector<HandleWord>> handles_;
    std::map<int, char> in_attr_, out_attr_, out_color_;

    // Condition-code folding (see README.md): every flag read with exactly one
    // reaching writer in scope reads that writer's value instead.
    std::set<int> cc_dead_;
    struct FoldPlan {
        std::vector<std::string> tests;
        bool immediate = false;
    };
    std::map<const Instr*, FoldPlan> fold_plans_;
    struct Pending {
        Ex value;         // k == B: a bool local holding `test`
        std::string test; // B only
    };
    std::map<int, Pending> pending_;

    [[noreturn]] void fail(const Instr& in, const std::string& m) const { throw Error(in.line, in.op + ": " + m); }
    std::vector<Stmt>& sink() { return *sinks_.back(); }
    void push(Stmt s) { sink().push_back(std::move(s)); }

    // ---- node construction
    int node(Node n) { return f_.add(std::move(n)); }
    int konst(Kind k, uint32_t bits) {
        Node n{Op::Const, {k, 1}};
        n.bits[0] = bits;
        return node(n);
    }
    int kf(float v) {
        uint32_t b;
        std::memcpy(&b, &v, 4);
        return konst(Kind::F, b);
    }
    int ks(int32_t v) { return konst(Kind::S, uint32_t(v)); }
    int ku(uint32_t v) { return konst(Kind::U, v); }
    int kb(bool v) { return konst(Kind::B, v ? 1u : 0u); }
    const Node& at(int i) const { return f_.nodes[size_t(i)]; }
    bool is_const(int i) const { return at(i).op == Op::Const; }
    bool is_const_bool(int i, bool v) const { return is_const(i) && at(i).type.k == Kind::B && at(i).bits[0] == uint32_t(v); }

    Ex ex(int nid) const { return {nid, at(nid).type.k, at(nid).type.n}; }
    int un(Op op, int a, Type t) {
        Node n{op, t};
        n.a = a;
        return node(n);
    }
    int bin(Op op, int a, int b, Type t) {
        Node n{op, t};
        n.a = a;
        n.b = b;
        return node(n);
    }
    int call(ir::Fn fn, std::vector<int> args, Type t) {
        Node n{Op::Call, t};
        n.sub = uint8_t(fn);
        n.args = std::move(args);
        return node(n);
    }
    int swizzle(int a, const std::vector<int>& comps) {
        const Node& src = at(a);
        const int n = int(comps.size());
        if (n == src.type.n) {
            bool identity = true;
            for (int i = 0; i < n; ++i) identity &= comps[i] == i;
            if (identity) return a;
        }
        if (src.op == Op::Swizzle) { // compose
            std::vector<int> c;
            for (int i : comps) c.push_back(src.sw[i]);
            return swizzle(src.a, c);
        }
        if (src.op == Op::Const) {
            Node k{Op::Const, {src.type.k, uint8_t(n)}};
            for (int i = 0; i < n; ++i) k.bits[i] = src.bits[comps[i]];
            return node(k);
        }
        Node s{Op::Swizzle, {src.type.k, uint8_t(n)}};
        s.a = a;
        for (int i = 0; i < n; ++i) s.sw[i] = uint8_t(comps[i]);
        return node(s);
    }
    int splat(int a, int n) {
        const Node& s = at(a);
        if (s.type.n == n) return a;
        if (s.type.n != 1) throw std::logic_error("splat of a vector");
        if (s.op == Op::Const) {
            Node k{Op::Const, {s.type.k, uint8_t(n)}};
            for (int i = 0; i < n; ++i) k.bits[i] = s.bits[0];
            return node(k);
        }
        Node c{Op::Construct, {s.type.k, uint8_t(n)}};
        c.args = {a};
        return node(c);
    }
    int construct(const std::vector<int>& parts, Kind k) {
        int total = 0;
        bool all_const = true;
        for (int p : parts) total += at(p).type.n, all_const &= is_const(p);
        if (parts.size() == 1 && total == at(parts[0]).type.n) return parts[0];
        if (all_const) {
            Node c{Op::Const, {k, uint8_t(total)}};
            int i = 0;
            for (int p : parts)
                for (int j = 0; j < at(p).type.n; ++j) c.bits[i++] = at(p).bits[j];
            return node(c);
        }
        Node c{Op::Construct, {k, uint8_t(total)}};
        c.args = parts;
        return node(c);
    }
    // Reinterpret bits. Folds x -> y -> x chains and integer literals.
    int bitcast(int a, Kind to) {
        const Node& s = at(a);
        if (s.type.k == to) return a;
        if (s.op == Op::Bitcast && at(s.a).type.k == to) return s.a;
        if (s.op == Op::Const && (to != Kind::F || std::all_of(s.bits.begin(), s.bits.begin() + s.type.n, [](uint32_t b) { return b == 0; })) &&
            s.type.k != Kind::F) {
            Node k = s;
            k.type.k = to;
            return node(k);
        }
        return un(Op::Bitcast, a, {to, s.type.n});
    }
    Ex convert(Ex e, Kind to) {
        if (e.k == to) return e;
        return {bitcast(e.node, to), to, e.n, e.truthy};
    }
    int neg(int a) {
        const Node& s = at(a);
        if (s.op == Op::Neg) return s.a;
        if (s.op == Op::Const) {
            Node k = s;
            for (int i = 0; i < s.type.n; ++i)
                k.bits[i] = s.type.k == Kind::F ? s.bits[i] ^ 0x80000000u : uint32_t(0u - s.bits[i]);
            return node(k);
        }
        return un(Op::Neg, a, s.type);
    }
    // a + b and a - b, with a negated operand folded into the other operator:
    // a + -b = a - b and -a + b = b - a are exact in IEEE and wrapping arithmetic.
    int add_sub(int a, bool subtract, int b, Type t) {
        if (at(b).op == Op::Neg) return bin(subtract ? Op::Add : Op::Sub, a, at(b).a, t);
        if (!subtract && at(a).op == Op::Neg) return bin(Op::Sub, b, at(a).a, t);
        return bin(subtract ? Op::Sub : Op::Add, a, b, t);
    }
    int lnot(int a) {
        if (at(a).op == Op::LNot) return at(a).a;
        if (at(a).op == Op::Const) return kb(!at(a).bits[0]);
        return un(Op::LNot, a, {Kind::B, 1});
    }

    // ---- analysis
    void scan_reads(const Operand& o, bool dest) {
        if (o.kind == Operand::Handle && o.inner) scan_reads(*o.inner, false);
        if (o.kind != Operand::Var) return;
        if (o.index_temp >= 0) read_[size_t(o.index_temp)] = 1;
        if (!dest && o.temp >= 0) read_[size_t(o.temp)] = 1;
    }
    // Handle tracking state, per LONG register lane (Temp::id * 4 + lane):
    // kAbsent never touched, kUnknown not a known handle, else an index into handle_sets_.
    static constexpr int kAbsent = -2, kUnknown = -1;
    std::vector<std::vector<HandleWord>> handle_sets_;
    int intern_handles(std::vector<HandleWord>&& w) {
        for (size_t i = 0; i < handle_sets_.size(); ++i)
            if (handle_sets_[i] == w) return int(i);
        handle_sets_.push_back(std::move(w));
        return int(handle_sets_.size()) - 1;
    }
    void resolve_handles(const std::vector<detail::Node>& nodes, std::vector<int>& st);
    void plan_cc(const std::vector<detail::Node>& root);

    // ---- operands
    int reg(int temp_id, const std::string& name) {
        if (temp_id < 0 || reg_index_[size_t(temp_id)] < 0) throw Error(0, name + " is not a register");
        return reg_index_[size_t(temp_id)];
    }
    int index_expr(const Operand& o) {
        if (o.index_reg.empty()) return ks(o.index);
        int r = swizzle(reg_node(o.index_temp, o.index_reg), {o.index_comp});
        int i = bitcast(r, Kind::S);
        if (o.index > 0) i = bin(Op::Add, i, ks(o.index), {Kind::S, 1});
        if (o.index < 0) i = bin(Op::Sub, i, ks(-o.index), {Kind::S, 1});
        return i;
    }
    int reg_node(int temp_id, const std::string& name) {
        Node n{Op::Reg, {Kind::F, 4}};
        n.var = reg(temp_id, name);
        return node(n);
    }
    int builtin(ir::Builtin b, Type t, int imm = -1) {
        Node n{Op::Builtin, t};
        n.sub = uint8_t(b);
        n.imm = imm;
        f_.builtin_used[size_t(b)] = true;
        return node(n);
    }
    Ex base(const Instr& in, const Operand& o);
    Ex fetch(const Instr& in, const Operand& o, char t, const std::vector<int>& comps);
    Ex load(const Instr& in, const Operand& o, char t, const std::vector<int>& lanes) {
        std::vector<int> comps;
        for (int l : lanes) {
            int c = swz(o, l);
            if (c < 0) fail(in, "operand swizzle ." + o.suffix + " is not a vector swizzle");
            comps.push_back(c);
        }
        return fetch(in, o, t, comps);
    }
    // A value whose lanes all read one component comes back as a scalar.
    Ex load_bc(const Instr& in, const Operand& o, char t, const std::vector<int>& lanes) {
        std::vector<int> comps;
        for (int l : lanes) comps.push_back(swz(o, l));
        if (comps.size() > 1 && comps[0] >= 0 && std::all_of(comps.begin(), comps.end(), [&](int c) { return c == comps[0]; }))
            return fetch(in, o, t, {comps[0]});
        return load(in, o, t, lanes);
    }
    Ex scalar(const Instr& in, const Operand& o, char t) {
        int c = o.suffix.empty() ? 0 : comp_index(o.suffix[0]);
        if (o.suffix.size() > 1 && !std::all_of(o.suffix.begin(), o.suffix.end(), [&](char ch) { return comp_index(ch) == c; }))
            fail(in, "scalar operand needs one component");
        return fetch(in, o, t, {c});
    }
    uint32_t literal(const std::string& text, char t, int line);

    // ---- destinations
    void store(const Instr& in, const Operand& d, Ex value, const std::vector<int>& lanes, bool side_effect = false);
    int cc_test(const std::string& test, const std::string& suffix, int line);
    int pending_test(const Pending& p, const std::string& test);
    int cc_local(int reg) {
        if (cc_local_[reg] < 0) {
            f_.locals.push_back({"cc" + std::to_string(reg), {Kind::S, 4}});
            cc_local_[reg] = int(f_.locals.size()) - 1;
            f_.cc_used[reg] = true;
        }
        return cc_local_[reg];
    }
    int local(const std::string& name, Type t) {
        f_.locals.push_back({name, t});
        return int(f_.locals.size()) - 1;
    }
    int local_node(int l) {
        Node n{Op::Local, f_.locals[size_t(l)].type};
        n.var = l;
        return node(n);
    }

    // NVN clip space to Vulkan's, when requested: flip y, map depth [-w, w] to [0, w].
    void position_fixups() {
        if (!f_.position) return;
        auto pos = [&](int c) { return swizzle(builtin(ir::Builtin::OutPosition, {Kind::F, 4}), {c}); };
        auto set = [&](int lane, int v) {
            Stmt s{Stmt::Assign};
            s.target = ir::Target::Position;
            s.lanes = {uint8_t(lane)};
            s.value = v;
            push(s);
        };
        const Type f1{Kind::F, 1};
        if (opt_.y_negate) set(1, neg(pos(1)));
        if (opt_.convert_depth) set(2, bin(Op::Mul, bin(Op::Add, pos(2), pos(3), f1), kf(0.5f), f1));
    }

    // ---- instructions
    void lower_nodes(const std::vector<detail::Node>& nodes);
    void lower(const Instr& in);
    void lower_texture(const Instr& in);
    void lower_memory(const Instr& in);
    void lower_image(const Instr& in);
    char data_type(const Instr& in, char fallback = 'F') const {
        for (const auto& m : in.mods) {
            if (m == "F" || m == "F32" || m == "F16") return 'F';
            if (m == "S" || m == "S32") return 'S';
            if (m == "U" || m == "U32") return 'U';
        }
        return fallback;
    }
    int texture(const Instr& in, const std::string& target, char type) {
        const auto& h = handles_.at(&in);
        for (size_t i = 0; i < f_.textures.size(); ++i)
            if (f_.textures[i].handle == h && f_.textures[i].target == target && f_.textures[i].type == type) return int(i);
        ir::Texture t;
        t.name = "tex" + std::to_string(f_.textures.size());
        t.target = target;
        t.type = type;
        t.handle = h;
        f_.textures.push_back(t);
        return int(f_.textures.size()) - 1;
    }
    int image(const Instr& in, const std::string& target, char type, const std::string& format, bool read, bool write) {
        const auto& h = handles_.at(&in);
        for (size_t i = 0; i < f_.images.size(); ++i) {
            auto& r = f_.images[i];
            if (r.handle == h && r.target == target && r.type == type && r.format == format) {
                r.read |= read;
                r.written |= write;
                return int(i);
            }
        }
        ir::Image r;
        r.name = "img" + std::to_string(f_.images.size());
        r.target = target;
        r.type = type;
        r.format = format;
        r.read = read;
        r.written = write;
        r.handle = h;
        f_.images.push_back(r);
        return int(f_.images.size()) - 1;
    }
};

// ---------------------------------------------------------------- analysis

void Lowerer::plan_cc(const std::vector<detail::Node>& root) {
    std::map<int, bool> ok;
    std::map<const Instr*, FoldPlan> plans;
    using State = std::map<int, const Instr*>; // nullptr: unknown / several writers
    std::function<void(const std::vector<detail::Node>&, std::set<int>&)> written = [&](const std::vector<detail::Node>& nodes,
                                                                                        std::set<int>& out) {
        for (const auto& n : nodes) {
            if (n.kind == detail::Node::Ins && writes_cc(n.ins) && !n.ins.ops.empty()) {
                int r = n.ins.has("CC1") ? 1 : 0;
                for (int l : lanes_of(n.ins.ops[0])) out.insert(r * 4 + l);
            }
            written(n.body, out);
            written(n.other, out);
        }
    };
    std::function<void(const std::vector<detail::Node>&, State&)> walk = [&](const std::vector<detail::Node>& nodes, State& st) {
        for (size_t i = 0; i < nodes.size(); ++i) {
            const auto& n = nodes[i];
            std::vector<int> reads = cc_reads(n);
            std::string test;
            if (n.kind == detail::Node::If || (n.kind == detail::Node::Ins && !n.ins.ops.empty() && n.ins.ops[0].kind == Operand::CCTest))
                test = n.ins.ops[0].name;
            else if (n.kind == detail::Node::Ins && !n.ins.ops.empty())
                test = n.ins.ops[0].pred;
            if (!test.empty() && (test.back() == '0' || test.back() == '1')) test.pop_back();
            for (int c : reads) {
                if (!ok.count(c)) ok[c] = true;
                auto it = st.find(c);
                const Instr* w = it == st.end() ? nullptr : it->second;
                if (!w || reads.size() != 1) {
                    ok[c] = false;
                    continue;
                }
                plans[w].tests.push_back(test);
                plans[w].immediate = i > 0 && nodes[i - 1].kind == detail::Node::Ins && &nodes[i - 1].ins == w;
            }
            if (n.kind == detail::Node::If) {
                State a = st, b = st;
                walk(n.body, a);
                walk(n.other, b);
                std::set<int> keys;
                for (auto& [k, v] : a) keys.insert(k);
                for (auto& [k, v] : b) keys.insert(k);
                for (int k : keys) {
                    const Instr* before = st.count(k) ? st[k] : nullptr;
                    if ((a.count(k) ? a[k] : nullptr) != before || (b.count(k) ? b[k] : nullptr) != before) st[k] = nullptr;
                }
            } else if (n.kind == detail::Node::Loop) {
                std::set<int> w;
                written(n.body, w);
                State inner = st;
                for (int c : w) inner[c] = nullptr;
                walk(n.body, inner);
                for (int c : w) st[c] = nullptr;
            } else if (writes_cc(n.ins) && !n.ins.ops.empty()) {
                const Operand& d = n.ins.ops[0];
                int r = n.ins.has("CC1") ? 1 : 0;
                bool single = d.suffix.size() == 1 && d.pred.empty();
                for (int l : lanes_of(d)) st[r * 4 + l] = single ? &n.ins : nullptr;
            }
        }
    };
    State st;
    walk(root, st);
    std::set<int> all;
    written(root, all);
    for (int c : all)
        if (!ok.count(c)) ok[c] = true;
    for (auto& [c, good] : ok)
        if (good) cc_dead_.insert(c);
    for (auto& [w, plan] : plans) {
        int r = w->has("CC1") ? 1 : 0;
        if (!cc_dead_.count(r * 4 + comp_index(w->ops[0].suffix[0]))) continue;
        if (plan.tests.size() != 1) plan.immediate = false;
        fold_plans_[w] = plan;
    }
}

void Lowerer::resolve_handles(const std::vector<detail::Node>& nodes, std::vector<int>& st) {
    for (const auto& n : nodes) {
        if (n.kind == detail::Node::If) {
            auto a = st, b = st;
            resolve_handles(n.body, a);
            resolve_handles(n.other, b);
            for (size_t k = 0; k < st.size(); ++k) st[k] = a[k] == b[k] ? a[k] : kUnknown;
            continue;
        }
        if (n.kind == detail::Node::Loop) {
            for (int iter = 0; iter < 8; ++iter) {
                auto s = st;
                resolve_handles(n.body, s);
                bool changed = false;
                for (size_t k = 0; k < st.size(); ++k) {
                    if (s[k] == kAbsent || s[k] == st[k]) continue;
                    const int v = st[k] == kAbsent ? s[k] : kUnknown;
                    changed |= v != st[k];
                    st[k] = v;
                }
                if (!changed) break;
            }
            continue;
        }
        const Instr& in = n.ins;
        auto long_reg = [&](const Operand& o) {
            if (o.kind != Operand::Var || o.indexed) fail(in, "64-bit operand must be a LONG register");
            if (o.temp < 0 || temp_kind(o) != TempKind::Long) fail(in, o.name + " is not a LONG register");
        };
        auto key = [&](const Operand& o, int comp) { return size_t(o.temp) * 4 + size_t(comp); };
        // Reading a lane that was never written makes it unknown (not absent) from here on.
        auto read = [&](size_t k) -> const std::vector<HandleWord>* {
            if (st[k] == kAbsent) st[k] = kUnknown;
            return st[k] >= 0 ? &handle_sets_[size_t(st[k])] : nullptr;
        };
        if ((in.op == "LDC" && in.has("U64")) || (in.op == "OR" && in.has("U64"))) {
            const Operand& d = in.ops.at(0);
            long_reg(d);
            for (int lane : lanes_of(d)) {
                int v = kUnknown;
                if (in.op == "LDC") {
                    const Operand& src = in.ops.at(1);
                    auto cb = p_.cbuffers.find(src.name);
                    if (cb == p_.cbuffers.end() || !src.index_reg.empty()) fail(in, "handle load must use a constant CBUFFER offset");
                    if (lane == 0) v = intern_handles({{uint32_t(cb->second), uint32_t(src.index)}});
                    else v = intern_handles({}); // U64 loads fill .x only
                } else {
                    const Operand &a = in.ops.at(1), &b = in.ops.at(2);
                    long_reg(a);
                    long_reg(b);
                    int ca = swz(a, lane), cb = swz(b, lane);
                    if (ca < 0 || cb < 0) fail(in, "bad handle swizzle");
                    const auto* va = read(key(a, ca));
                    const auto* vb = read(key(b, cb));
                    if (va && vb) {
                        std::vector<HandleWord> w = *va;
                        w.insert(w.end(), vb->begin(), vb->end());
                        std::sort(w.begin(), w.end());
                        w.erase(std::unique(w.begin(), w.end()), w.end());
                        v = intern_handles(std::move(w));
                    }
                }
                st[key(d, lane)] = v;
            }
            continue;
        }
        for (const Operand& o : in.ops) {
            if (o.kind == Operand::Handle) {
                const Operand& r = *o.inner;
                long_reg(r);
                if (r.suffix.size() > 1) fail(in, "handle must be one component");
                const auto* v = read(key(r, r.suffix.empty() ? 0 : comp_index(r.suffix[0])));
                if (!v || v->empty()) fail(in, "texture handle is not a known constant-buffer word combination");
                handles_[&in] = *v;
            } else if (o.kind == Operand::Var) {
                if (o.temp >= 0 && temp_kind(o) == TempKind::Long) fail(in, "64-bit register used outside handle loads");
            }
        }
    }
}

// ---------------------------------------------------------------- operands

uint32_t Lowerer::literal(const std::string& text, char t, int line) {
    const bool hex = text.find_first_of("xX") != std::string::npos;
    if (t == 'F' && hex) {
        uint32_t bits = uint32_t(std::strtoull(text.c_str() + (text[0] == '-'), nullptr, 16));
        return text[0] == '-' ? bits ^ 0x80000000u : bits; // the raw encoding
    }
    if (t == 'F') {
        float v = float(std::strtod(text.c_str(), nullptr));
        uint32_t b;
        std::memcpy(&b, &v, 4);
        return b;
    }
    if (!hex && text.find_first_of(".eE") != std::string::npos) throw Error(line, "fractional constant used as integer: " + text);
    long long v = std::strtoll(text.c_str(), nullptr, 0);
    if (v < -2147483648LL || v > 4294967295LL) throw Error(line, "integer constant out of range: " + text);
    return uint32_t(v);
}

Ex Lowerer::base(const Instr& in, const Operand& o) {
    if (o.kind == Operand::Var) {
        if (const Temp* t = temp(o)) {
            if (t->kind == TempKind::Long) fail(in, "64-bit register as a value");
            Node n{Op::Reg, {Kind::F, 4}};
            n.var = reg(o.temp, o.name);
            if (t->array > 0) {
                if (!o.indexed) fail(in, "array " + o.name + " used without index");
                n.a = index_expr(o);
            } else if (o.indexed) {
                fail(in, o.name + " is not an array");
            }
            return ex(node(n));
        }
        auto a = p_.aliases.find(o.name);
        if (a != p_.aliases.end()) {
            Operand b = o;
            b.kind = Operand::Bind;
            b.bind.path = a->second.path;
            b.bind.index = a->second.first < 0 ? -1 : a->second.first + (o.indexed ? o.index : 0);
            if (o.indexed && !o.index_reg.empty()) fail(in, "relative binding index");
            return base(in, b);
        }
        fail(in, "unknown variable " + o.name);
    }
    if (o.kind != Operand::Bind) fail(in, "operand is not a value");
    const Binding& b = o.bind;
    auto need_index = [&]() {
        if (b.index < 0) fail(in, b.path + " needs an index");
        return b.index;
    };
    using B = ir::Builtin;
    if (b.path == "fragment.position") return ex(builtin(B::FragCoord, {Kind::F, 4}));
    if (b.path == "fragment.attrib" || (b.path == "vertex.attrib" && p_.stage == Stage::Vertex)) {
        int i = need_index();
        char t = 'F';
        if (p_.stage == Stage::Vertex) {
            if (opt_.signed_int_inputs >> i & 1) t = 'S';
            if (opt_.unsigned_int_inputs >> i & 1) t = 'U';
        }
        in_attr_[i] = t;
        Node n{Op::Input, {kind_of(t), 4}};
        n.var = i;
        return ex(node(n));
    }
    if (b.path == "vertex[].attrib") {
        int i = need_index();
        in_attr_[i] = 'F';
        Node n{Op::Input, {Kind::F, 4}};
        n.var = i;
        n.imm = b.vertex;
        return ex(node(n));
    }
    if (b.path == "vertex[].position") return ex(builtin(B::GsPosition, {Kind::F, 4}, b.vertex));
    if (b.path == "vertex.position" && p_.stage == Stage::Geometry) fail(in, "vertex.position without vertex index");
    if (b.path == "fragment.facing") return ex(builtin(B::Facing, {Kind::F, 4}));
    if (b.path == "fragment.sampleid") return ex(builtin(B::SampleId, {Kind::S, 4}));
    if (b.path == "vertex.id") return ex(builtin(B::VertexId, {Kind::S, 4}));
    if (b.path == "vertex.instance") return ex(builtin(B::InstanceId, {Kind::S, 4}));
    if (b.path == "invocation.globalid") return ex(builtin(B::GlobalId, {Kind::U, 4}));
    if (b.path == "invocation.localid") return ex(builtin(B::LocalId, {Kind::U, 4}));
    if (b.path == "invocation.groupid") return ex(builtin(B::GroupId, {Kind::U, 4}));
    if (b.path == "invocation.localindex") return ex(builtin(B::LocalIndex, {Kind::U, 4}));
    fail(in, "unsupported source binding " + b.path);
}

Ex Lowerer::fetch(const Instr& in, const Operand& o, char t, const std::vector<int>& comps) {
    const int n = int(comps.size());
    Ex e;
    if (o.kind == Operand::Const || o.kind == Operand::Offset) {
        Node k{Op::Const, {kind_of(t), uint8_t(n)}};
        for (int i = 0; i < n; ++i) {
            if (comps[i] >= int(o.consts.size())) fail(in, "constant component out of range");
            k.bits[i] = literal(o.consts[comps[i]], t, in.line);
        }
        e = ex(node(k));
    } else {
        Ex b = base(in, o);
        e = ex(swizzle(b.node, comps));
        e = convert(e, kind_of(t));
    }
    if (o.abs && t != 'U') {
        Node a{Op::Abs, at(e.node).type};
        a.a = e.node;
        e.node = node(a);
    }
    if (o.neg) e.node = neg(e.node);
    return e;
}

// ---------------------------------------------------------------- destinations

int Lowerer::pending_test(const Pending& p, const std::string& test) {
    const Type b1{Kind::B, 1};
    if (p.value.k == Kind::B) {
        if (test != p.test) throw std::logic_error("predicate local read with another test");
        return p.value.node;
    }
    const int v = p.value.node;
    if (p.value.k == Kind::F && p.value.truthy >= 0) { // value is 1.0 or 0.0
        if (test == "NE" || test == "GT") return p.value.truthy;
        if (test == "EQ" || test == "LE") return lnot(p.value.truthy);
        return kb(test == "GE" || test == "LEG");
    }
    if (p.value.k == Kind::F) {
        const int zero = kf(0.0f);
        if (test == "EQ") return bin(Op::Eq, v, zero, b1);
        if (test == "NE") return lnot(bin(Op::Eq, v, zero, b1)); // true for NaN
        if (test == "GE") return bin(Op::Ge, v, zero, b1);
        if (test == "LE") return bin(Op::Le, v, zero, b1);
        if (test == "GT") return bin(Op::Gt, v, zero, b1);
        if (test == "LT") return bin(Op::Lt, v, zero, b1);
        int nan = call(ir::Fn::IsNan, {v}, b1);
        return test == "NAN" ? nan : lnot(nan);
    }
    if (is_const(v)) {
        int32_t sv = int32_t(at(v).bits[0]);
        bool r = test == "EQ" ? sv == 0 : test == "NE" ? sv != 0 : test == "GE" ? sv >= 0 : test == "LE" ? sv <= 0
               : test == "GT" ? sv > 0 : test == "LT" ? sv < 0 : test == "LEG";
        return kb(r);
    }
    const int zero = p.value.k == Kind::U ? ku(0) : ks(0);
    if (test == "NE") return p.value.truthy >= 0 ? p.value.truthy : bin(Op::Ne, v, zero, b1);
    if (test == "EQ") return p.value.truthy >= 0 ? lnot(p.value.truthy) : bin(Op::Eq, v, zero, b1);
    if (test == "NAN") return kb(false);
    if (test == "LEG") return kb(true);
    int sv = p.value.k == Kind::S ? v : bitcast(v, Kind::S);
    Op op = test == "GE" ? Op::Ge : test == "LE" ? Op::Le : test == "GT" ? Op::Gt : Op::Lt;
    return bin(op, sv, ks(0), b1);
}

int Lowerer::cc_test(const std::string& test_in, const std::string& suffix, int line) {
    std::string test = test_in;
    int r = 0;
    if (test.back() == '0' || test.back() == '1') {
        r = test.back() - '0';
        test.pop_back();
    }
    if (test == "TR") return kb(true);
    if (test == "FL") return kb(false);
    static const std::map<std::string, ir::Fn> fns = {{"EQ", ir::Fn::CcEq}, {"NE", ir::Fn::CcNe}, {"LT", ir::Fn::CcLt},
                                                     {"LE", ir::Fn::CcLe}, {"GT", ir::Fn::CcGt}, {"GE", ir::Fn::CcGe},
                                                     {"NAN", ir::Fn::CcNan}, {"LEG", ir::Fn::CcLeg}};
    auto fn = fns.find(test);
    if (fn == fns.end()) throw Error(line, "condition test " + test + " needs carry/overflow flags");
    std::set<int> comps;
    if (suffix.empty()) comps = {0, 1, 2, 3};
    else
        for (char c : suffix) comps.insert(comp_index(c));
    int result = -1;
    for (int c : comps) {
        int t;
        auto it = pending_.find(r * 4 + c);
        if (it != pending_.end()) {
            t = pending_test(it->second, test);
        } else {
            if (cc_dead_.count(r * 4 + c)) throw Error(line, "internal: folded condition code read without its value");
            t = call(fn->second, {swizzle(local_node(cc_local(r)), {c})}, {Kind::B, 1});
        }
        result = result < 0 ? t : bin(Op::LOr, result, t, {Kind::B, 1});
    }
    return result;
}

void Lowerer::store(const Instr& in, const Operand& d, Ex value, const std::vector<int>& lanes, bool side_effect) {
    bool cc = writes_cc(in);
    const int ccreg = in.has("CC1") ? 1 : 0;
    const FoldPlan* plan = nullptr;
    if (cc && std::all_of(lanes.begin(), lanes.end(), [&](int l) { return cc_dead_.count(ccreg * 4 + l) != 0; })) {
        cc = false; // no reader observes these flags directly
        auto it = fold_plans_.find(&in);
        if (it != fold_plans_.end()) plan = &it->second;
    }
    const int cc_key = ccreg * 4 + lanes[0];
    // Flags consumed later by one test become a bool local; by several tests, a typed copy.
    auto capture = [&](const Ex& v) {
        std::string name = "p" + std::to_string(predicates_++);
        bool same = std::all_of(plan->tests.begin(), plan->tests.end(), [&](const std::string& t) { return t == plan->tests[0]; });
        Stmt s{Stmt::DeclLocal};
        s.target = ir::Target::Local;
        if (same) {
            s.var = local(name, {Kind::B, 1});
            s.value = pending_test({v, ""}, plan->tests[0]);
            push(s);
            pending_[cc_key] = {{local_node(s.var), Kind::B, 1}, plan->tests[0]};
        } else {
            s.var = local(name, {v.k, 1});
            s.value = v.node;
            push(s);
            pending_[cc_key] = {{local_node(s.var), v.k, 1}, ""};
        }
    };
    const int n = int(lanes.size());
    if (value.n != n) value.node = splat(value.node, n), value.n = n;
    auto clamp = [&](float lo) {
        if (value.k != Kind::F) fail(in, ".SAT on integer result");
        value.node = call(ir::Fn::Clamp, {value.node, kf(lo), kf(1.0f)}, {Kind::F, uint8_t(n)});
        value.truthy = -1;
    };
    if (in.has("SAT")) clamp(0.0f);
    if (in.has("SSAT")) clamp(-1.0f);

    // Destination and its storage type.
    ir::Target target = ir::Target::Reg;
    int var = -1, index = -1;
    Kind store_k = Kind::F;
    bool dead = false;
    int scalar_lane = -1;
    if (d.kind == Operand::Var) {
        if (const Temp* t = temp(d)) {
            if (t->kind == TempKind::Long) fail(in, "64-bit destination outside handle loads");
            var = reg(d.temp, d.name);
            if (t->array > 0) {
                if (!d.indexed) fail(in, "array destination without index");
                index = index_expr(d);
            } else {
                dead = !read_[size_t(d.temp)];
            }
        } else {
            auto a = p_.aliases.find(d.name);
            if (a == p_.aliases.end()) fail(in, "unknown destination " + d.name);
            Operand b = d;
            b.kind = Operand::Bind;
            b.bind.path = a->second.path;
            b.bind.index = a->second.first < 0 ? -1 : a->second.first + (d.indexed ? d.index : 0);
            store(in, b, value, lanes, side_effect);
            return;
        }
    } else if (d.kind == Operand::Bind) {
        const std::string& path = d.bind.path;
        if (path == "result.color") {
            int i = d.bind.index < 0 ? 0 : d.bind.index;
            char t = 'F';
            if (opt_.signed_int_outputs >> i & 1) t = 'S';
            if (opt_.unsigned_int_outputs >> i & 1) t = 'U';
            out_color_[i] = t;
            target = ir::Target::Output;
            var = i;
            store_k = kind_of(t);
        } else if (path == "result.attrib") {
            if (d.bind.index < 0) fail(in, "result.attrib needs an index");
            out_attr_[d.bind.index] = 'F';
            target = ir::Target::Output;
            var = 1000 + d.bind.index; // generic outputs: location + 1000
        } else if (path == "result.position") {
            f_.position = true;
            target = ir::Target::Position;
        } else if (path == "result.depth") {
            f_.frag_depth = true;
            target = ir::Target::FragDepth;
            scalar_lane = 2;
        } else if (path == "result.layer") {
            f_.layer = true;
            target = ir::Target::Layer;
            scalar_lane = 0;
            store_k = Kind::S;
        } else {
            fail(in, "unsupported destination binding " + path);
        }
    } else {
        fail(in, "destination is not a variable");
    }
    if (scalar_lane >= 0 && (n != 1 || lanes[0] != scalar_lane))
        fail(in, std::string(target == ir::Target::FragDepth ? "gl_FragDepth" : "gl_Layer") + " takes exactly one component");

    // Per-lane write conditions from a condition-code mask.
    std::vector<int> cond(n, -1);
    if (!d.pred.empty()) {
        for (int i = 0; i < n; ++i) {
            int c = d.pred_suffix.empty() ? lanes[i] : d.pred_suffix.size() == 1 ? comp_index(d.pred_suffix[0])
                                                                                 : comp_index(d.pred_suffix[lanes[i]]);
            cond[i] = cc_test(d.pred, std::string(1, "xyzw"[c]), in.line);
        }
    }
    auto same_cond = [&](int a, int b) {
        if (a == b) return true;
        if (a < 0 || b < 0) return false;
        // cc tests on the same component print identically; compare structurally one level
        const Node &x = at(a), &y = at(b);
        return x.op == Op::Call && y.op == Op::Call && x.sub == y.sub && x.args.size() == 1 && y.args.size() == 1 &&
               at(x.args[0]).op == Op::Swizzle && at(y.args[0]).op == Op::Swizzle && at(x.args[0]).sw[0] == at(y.args[0]).sw[0] &&
               at(at(x.args[0]).a).var == at(at(y.args[0]).a).var;
    };
    bool uniform_cond = std::all_of(cond.begin(), cond.end(), [&](int c) { return same_cond(c, cond[0]); });

    auto assign = [&](const std::vector<uint8_t>& ls, int v) {
        Stmt s{Stmt::Assign};
        s.target = target;
        s.var = var;
        s.index = index;
        s.lanes = ls;
        s.value = v;
        return s;
    };
    std::vector<uint8_t> all_lanes(lanes.begin(), lanes.end());

    if (plan && dead) {
        if (plan->immediate) pending_[cc_key] = {value, ""};
        else capture(value);
        return;
    }
    if (dead && !cc) {
        if (side_effect) {
            Stmt s{Stmt::Expr};
            s.value = value.node;
            push(s);
        }
        return;
    }
    auto read_back = [&](int lane) {
        Node n2{target == ir::Target::Reg ? Op::Reg : Op::Output, {target == ir::Target::Reg ? Kind::F : store_k, 4}};
        n2.var = var;
        n2.a = index;
        return swizzle(node(n2), {lane});
    };

    if (!cc && uniform_cond) {
        Stmt s = assign(all_lanes, convert(value, store_k).node);
        if (cond[0] >= 0 && !is_const_bool(cond[0], true)) {
            if (is_const_bool(cond[0], false)) {
                if (side_effect) {
                    Stmt e{Stmt::Expr};
                    e.value = value.node;
                    push(e);
                }
                return;
            }
            Stmt w{Stmt::If};
            w.cond = cond[0];
            w.body.push_back(s);
            w.a = 1; // print on one line
            push(w);
        } else {
            push(s);
        }
        if (plan) {
            if (target != ir::Target::Reg && target != ir::Target::Output) fail(in, "condition read back from a builtin");
            Ex back = convert(ex(read_back(lanes[0])), value.k);
            if (plan->immediate) pending_[cc_key] = {back, ""};
            else capture(back);
        }
        return;
    }
    // Materialize once, then store and update condition codes lane by lane.
    Stmt block{Stmt::Block};
    sinks_.push_back(&block.body);
    Stmt decl{Stmt::DeclLocal};
    decl.target = ir::Target::Local;
    decl.var = local("v", {value.k, uint8_t(n)});
    decl.value = value.node;
    push(decl);
    const int v = local_node(decl.var);
    for (int i = 0; i < n; ++i) {
        const int lane = n == 1 ? v : swizzle(v, {i});
        std::vector<Stmt> stmts;
        if (!dead) stmts.push_back(assign({uint8_t(lanes[i])}, convert(ex(lane), store_k).node));
        if (cc) {
            ir::Fn fn = value.k == Kind::F ? ir::Fn::CcSetF : value.k == Kind::S ? ir::Fn::CcSetI : ir::Fn::CcSetU;
            Stmt c{Stmt::Assign};
            c.target = ir::Target::Local;
            c.var = cc_local(ccreg);
            c.lanes = {uint8_t(lanes[i])};
            c.value = call(fn, {lane}, {Kind::S, 1});
            stmts.push_back(c);
        }
        if (cond[i] >= 0 && !is_const_bool(cond[i], true)) {
            if (is_const_bool(cond[i], false)) continue;
            Stmt w{Stmt::If};
            w.cond = cond[i];
            w.body = std::move(stmts);
            w.a = 2; // one line, braced
            push(w);
        } else {
            for (auto& s : stmts) push(s);
        }
    }
    sinks_.pop_back();
    push(block);
}

// ---------------------------------------------------------------- instructions

void Lowerer::lower_nodes(const std::vector<detail::Node>& nodes) {
    for (const auto& n : nodes) {
        if (n.kind == detail::Node::If) {
            const Instr& in = n.ins;
            if (in.ops.size() != 1 || in.ops[0].kind != Operand::CCTest) fail(in, "IF needs a condition test");
            int cond = cc_test(in.ops[0].name, in.ops[0].suffix, in.line);
            auto saved = pending_; // writers inside a branch are out of scope after it
            Stmt s{Stmt::If};
            if (n.body.empty() && !n.other.empty()) {
                s.cond = lnot(cond); // only an else branch: invert the test
                sinks_.push_back(&s.body);
                lower_nodes(n.other);
                sinks_.pop_back();
            } else {
                s.cond = cond;
                sinks_.push_back(&s.body);
                lower_nodes(n.body);
                sinks_.pop_back();
                pending_ = saved;
                sinks_.push_back(&s.other);
                lower_nodes(n.other);
                sinks_.pop_back();
            }
            pending_ = saved;
            push(std::move(s));
        } else if (n.kind == detail::Node::Loop) {
            const Instr& in = n.ins;
            Stmt s{Stmt::Loop};
            if (!in.ops.empty()) {
                char t = data_type(in, 'F');
                Ex c = scalar(in, in.ops[0], t);
                int count = t == 'F' ? un(Op::Convert, call(ir::Fn::Floor, {c.node}, {Kind::F, 1}), {Kind::S, 1})
                          : t == 'U' ? bitcast(c.node, Kind::S)
                                     : c.node;
                s.count = count;
                s.var = local("loop" + std::to_string(loops_++), {Kind::S, 1});
            }
            auto saved = pending_;
            sinks_.push_back(&s.body);
            lower_nodes(n.body);
            sinks_.pop_back();
            pending_ = saved;
            push(std::move(s));
        } else {
            lower(n.ins);
        }
    }
}

void Lowerer::lower(const Instr& in) {
    const std::string& op = in.op;
    auto need = [&](size_t n) {
        if (in.ops.size() != n) fail(in, "expected " + std::to_string(n) + " operands");
    };
    if ((op == "LDC" && in.has("U64")) || (op == "OR" && in.has("U64"))) return; // handles: resolved statically

    auto conditional = [&](int cond, Stmt::K k) {
        if (is_const_bool(cond, false)) return;
        Stmt s{k};
        if (is_const_bool(cond, true)) {
            push(s);
            return;
        }
        Stmt w{Stmt::If};
        w.cond = cond;
        w.body.push_back(s);
        w.a = 1;
        push(w);
    };
    if (op == "BRK" || op == "CONT") {
        int cond = in.ops.empty() ? kb(true) : cc_test(in.ops[0].name, in.ops[0].suffix, in.line);
        conditional(cond, op == "BRK" ? Stmt::Break : Stmt::Continue);
        return;
    }
    if (op == "KIL") {
        if (p_.stage != Stage::Fragment) fail(in, "KIL outside a fragment program");
        f_.kills = true;
        need(1);
        int cond;
        if (in.ops[0].kind == Operand::CCTest) {
            cond = cc_test(in.ops[0].name, in.ops[0].suffix, in.line);
        } else {
            char t = data_type(in, 'F');
            Ex v = load(in, in.ops[0], t, {0, 1, 2, 3});
            int zero = splat(t == 'F' ? kf(0.0f) : t == 'S' ? ks(0) : ku(0), 4);
            cond = call(ir::Fn::Any, {bin(Op::Lt, v.node, zero, {Kind::B, 4})}, {Kind::B, 1});
        }
        conditional(cond, opt_.demote ? Stmt::Demote : Stmt::Kill);
        return;
    }
    if (op == "EMIT" || op == "ENDPRIM") {
        if (p_.stage != Stage::Geometry) fail(in, op + " outside a geometry program");
        if (op == "EMIT") position_fixups();
        push(Stmt{op == "EMIT" ? Stmt::Emit : Stmt::EndPrimitive});
        return;
    }
    if (op == "BAR") {
        push(Stmt{Stmt::Barrier});
        return;
    }
    if (op == "MEMBAR") {
        push(Stmt{in.has("CTA") ? Stmt::GroupMemoryBarrier : Stmt::MemoryBarrier});
        return;
    }
    if (op == "TEX" || op == "TXB" || op == "TXL" || op == "TXF" || op == "TXFMS" || op == "TXG" || op == "TXD" || op == "TXQ" ||
        op == "TXP") {
        lower_texture(in);
        return;
    }
    if (op == "LDC" || op == "LDB" || op == "STB" || op == "LDS" || op == "STS" || op == "ATOMS" || op == "ATOMB") {
        lower_memory(in);
        return;
    }
    if (op == "LOADIM" || op == "STOREIM" || op == "ATOMIM" || op == "IMQ") {
        lower_image(in);
        return;
    }

    if (in.ops.empty()) fail(in, "missing destination");
    const Operand& d = in.ops[0];
    std::vector<int> lanes = lanes_of(d);
    if (!d.suffix.empty())
        for (size_t i = 1; i < lanes.size(); ++i)
            if (lanes[i] <= lanes[i - 1]) fail(in, "write mask must be in xyzw order");
    const int n = int(lanes.size());
    auto T = [](char t, int k) { return Type{kind_of(t), uint8_t(k)}; };

    static const std::map<std::string, Op> binary = {{"ADD", Op::Add}, {"SUB", Op::Sub}, {"MUL", Op::Mul},
                                                     {"AND", Op::And}, {"OR", Op::Or},   {"XOR", Op::Xor}};
    if (binary.count(op)) {
        need(3);
        const bool bitwise = op == "AND" || op == "OR" || op == "XOR";
        char t = data_type(in, bitwise ? 'S' : 'F');
        if (bitwise && t == 'F') fail(in, "bitwise op on floats");
        if (in.has("HI") || in.has("S24") || in.has("U24")) fail(in, "MUL.HI/S24/U24 not supported yet");
        if (writes_cc(in) && t != 'F' && (op == "ADD" || op == "SUB")) fail(in, "integer add with condition codes needs carry/overflow flags");
        Ex a = load_bc(in, in.ops[1], t, lanes), b = load_bc(in, in.ops[2], t, lanes);
        int rn = std::max(a.n, b.n);
        Op o = binary.at(op);
        int r = (o == Op::Add || o == Op::Sub) ? add_sub(a.node, o == Op::Sub, b.node, T(t, rn)) : bin(o, a.node, b.node, T(t, rn));
        store(in, d, ex(r), lanes);
        return;
    }
    if (op == "MAD") {
        need(4);
        char t = data_type(in);
        if (writes_cc(in) && t != 'F') fail(in, "integer MAD with condition codes");
        Ex a = load_bc(in, in.ops[1], t, lanes), b = load_bc(in, in.ops[2], t, lanes), c = load_bc(in, in.ops[3], t, lanes);
        int rn = std::max({a.n, b.n, c.n});
        if (t == 'F' && opt_.hardware_arithmetic) {
            // fma() takes three operands of one size: broadcasts become swizzles.
            if (a.n != rn) a = load(in, in.ops[1], t, lanes);
            if (b.n != rn) b = load(in, in.ops[2], t, lanes);
            if (c.n != rn) c = load(in, in.ops[3], t, lanes);
            store(in, d, ex(call(ir::Fn::Fma, {a.node, b.node, c.node}, T(t, rn))), lanes);
            return;
        }
        int m = bin(Op::Mul, a.node, b.node, T(t, std::max(a.n, b.n)));
        store(in, d, ex(add_sub(m, false, c.node, T(t, rn))), lanes);
        return;
    }
    if (op == "MOV") {
        need(2);
        store(in, d, load(in, in.ops[1], data_type(in), lanes), lanes);
        return;
    }
    if (op == "MIN" || op == "MAX") {
        need(3);
        char t = data_type(in);
        Ex a = load(in, in.ops[1], t, lanes), b = load_bc(in, in.ops[2], t, lanes);
        store(in, d, ex(call(op == "MIN" ? ir::Fn::Min : ir::Fn::Max, {a.node, b.node}, T(t, n))), lanes);
        return;
    }
    if (op == "ABS" || op == "NOT") {
        need(2);
        char t = data_type(in, op == "NOT" ? 'S' : 'F');
        Ex a = load(in, in.ops[1], t, lanes);
        if (op == "NOT") store(in, d, ex(un(Op::BitNot, a.node, T(t, n))), lanes);
        else store(in, d, t == 'U' ? a : ex(un(Op::Abs, a.node, T(t, n))), lanes);
        return;
    }
    if (op == "DIV" || op == "MOD" || op == "SHL" || op == "SHR") {
        need(3);
        char t = data_type(in, op == "DIV" ? 'F' : 'S');
        if (op != "DIV" && t == 'F') fail(in, "integer op on floats");
        Ex a = (op == "DIV" || op == "MOD") ? load_bc(in, in.ops[1], t, lanes) : load(in, in.ops[1], t, lanes);
        Ex b = scalar(in, in.ops[2], t);
        int r;
        if (op == "DIV" && t == 'F' && opt_.hardware_arithmetic)
            r = bin(Op::Mul, a.node, bin(Op::Div, kf(1.0f), b.node, T('F', 1)), T(t, a.n)); // as the binaries: x * rcp(y)
        else
            r = bin(op == "DIV" ? Op::Div : op == "MOD" ? Op::Mod : op == "SHL" ? Op::Shl : Op::Shr, a.node, b.node, T(t, a.n));
        store(in, d, ex(r), lanes);
        return;
    }
    static const std::map<std::string, Op> compare = {{"SEQ", Op::Eq}, {"SNE", Op::Ne}, {"SLT", Op::Lt},
                                                      {"SLE", Op::Le}, {"SGT", Op::Gt}, {"SGE", Op::Ge}};
    if (compare.count(op)) {
        need(3);
        char t = data_type(in);
        Ex a = load(in, in.ops[1], t, lanes), b = load(in, in.ops[2], t, lanes);
        int c = bin(compare.at(op), a.node, b.node, {Kind::B, uint8_t(n)});
        // TRUE is 1.0, -1, or all ones by data type (NV_gpu_program4 SEQ).
        int r;
        if (t == 'F') r = un(Op::Convert, c, T('F', n));
        else if (t == 'S') r = neg(un(Op::Convert, c, T('S', n)));
        else if (n == 1) {
            Node s{Op::Select, T('U', 1)};
            s.a = c;
            s.b = ku(0xFFFFFFFFu);
            s.c = ku(0);
            r = node(s);
        } else {
            r = bin(Op::Mul, un(Op::Convert, c, T('U', n)), ku(0xFFFFFFFFu), T('U', n));
        }
        Ex e = ex(r);
        if (n == 1) e.truthy = c;
        store(in, d, e, lanes);
        return;
    }
    if (op == "FLR" || op == "CEIL" || op == "TRUNC" || op == "ROUND") {
        need(2);
        char t = data_type(in);
        Ex a = load(in, in.ops[1], 'F', lanes);
        ir::Fn fn = op == "FLR" ? ir::Fn::Floor : op == "CEIL" ? ir::Fn::Ceil : op == "TRUNC" ? ir::Fn::Trunc : ir::Fn::RoundEven;
        Ex e = ex(call(fn, {a.node}, T('F', n)));
        if (t != 'F') {
            e = ex(call(t == 'S' ? ir::Fn::F2I : ir::Fn::F2U, {e.node}, T(t, n)));
            // Nonzero exactly when (NaN converts to zero and fails every comparison):
            if (n == 1) {
                const Type b1{Kind::B, 1};
                if (op == "TRUNC")
                    e.truthy = t == 'U' ? bin(Op::Ge, a.node, kf(1.0f), b1) : bin(Op::Ge, un(Op::Abs, a.node, T('F', 1)), kf(1.0f), b1);
                if (op == "FLR" && t == 'U') e.truthy = bin(Op::Ge, a.node, kf(1.0f), b1);
                if (op == "CEIL" && t == 'U') e.truthy = bin(Op::Gt, a.node, kf(0.0f), b1);
            }
        }
        store(in, d, e, lanes);
        return;
    }
    if (op == "FRC" || op == "DDX" || op == "DDY") {
        need(2);
        if (data_type(in) != 'F') fail(in, "float-only instruction");
        if (op != "FRC" && p_.stage != Stage::Fragment) fail(in, "derivatives outside a fragment program");
        Ex a = load(in, in.ops[1], 'F', lanes);
        store(in, d, ex(call(op == "FRC" ? ir::Fn::Fract : op == "DDX" ? ir::Fn::DFdx : ir::Fn::DFdy, {a.node}, T('F', n))), lanes);
        return;
    }
    if (op == "I2F") {
        need(2);
        char t = data_type(in, 'S');
        if (t == 'F') fail(in, "I2F needs S or U");
        Ex a = load(in, in.ops[1], t, lanes);
        store(in, d, ex(un(Op::Convert, a.node, T('F', n))), lanes);
        return;
    }
    static const std::map<std::string, ir::Fn> unary_scalar = {
        {"RSQ", ir::Fn::InverseSqrt}, {"EX2", ir::Fn::Exp2}, {"LG2", ir::Fn::Log2}, {"SIN", ir::Fn::Sin}, {"COS", ir::Fn::Cos}};
    if (op == "RCP" || unary_scalar.count(op)) {
        need(2);
        if (data_type(in) != 'F') fail(in, "float-only instruction");
        Ex a = scalar(in, in.ops[1], 'F');
        int r = op == "RCP" ? bin(Op::Div, kf(1.0f), a.node, T('F', 1)) : call(unary_scalar.at(op), {a.node}, T('F', 1));
        store(in, d, ex(splat(r, n)), lanes);
        return;
    }
    if (op == "POW") {
        need(3);
        Ex a = scalar(in, in.ops[1], 'F'), b = scalar(in, in.ops[2], 'F');
        int r;
        if (opt_.hardware_arithmetic) {
            // The Maxwell binaries compute POW as ex2(lg2(|a|) * b): the base's sign is dropped.
            int base = at(a.node).op == Op::Abs ? a.node : un(Op::Abs, a.node, T('F', 1));
            r = call(ir::Fn::Exp2, {bin(Op::Mul, call(ir::Fn::Log2, {base}, T('F', 1)), b.node, T('F', 1))}, T('F', 1));
        } else {
            r = call(ir::Fn::Pow, {a.node, b.node}, T('F', 1));
        }
        store(in, d, ex(splat(r, n)), lanes);
        return;
    }
    if (op == "DP2" || op == "DP3" || op == "DP4") {
        need(3);
        if (data_type(in) != 'F') fail(in, "float-only instruction");
        int k = op[2] - '0';
        std::vector<int> l;
        for (int i = 0; i < k; ++i) l.push_back(i);
        Ex a = load(in, in.ops[1], 'F', l), b = load(in, in.ops[2], 'F', l);
        ir::Fn fn = k == 2 ? ir::Fn::Dp2 : k == 3 ? ir::Fn::Dp3 : ir::Fn::Dp4;
        store(in, d, ex(splat(call(fn, {a.node, b.node}, T('F', 1)), n)), lanes);
        return;
    }
    if (op == "UP2H") {
        need(2);
        char t = data_type(in);
        Ex a = scalar(in, in.ops[1], t);
        int bits = bitcast(a.node, Kind::U);
        std::vector<int> sw;
        for (int l : lanes) sw.push_back(l & 1);
        store(in, d, ex(swizzle(call(ir::Fn::UnpackHalf2x16, {bits}, T('F', 2)), sw)), lanes);
        return;
    }
    if (op == "PK2H") {
        need(2);
        char t = data_type(in);
        Ex a = load(in, in.ops[1], 'F', {0, 1});
        Ex r = ex(call(ir::Fn::PackHalf2x16, {a.node}, T('U', 1)));
        store(in, d, ex(splat(convert(r, kind_of(t)).node, n)), lanes);
        return;
    }
    fail(in, "unsupported instruction");
}

// ---------------------------------------------------------------- textures

void Lowerer::lower_texture(const Instr& in) {
    const std::string& op = in.op;
    size_t hi = 0;
    for (; hi < in.ops.size(); ++hi)
        if (in.ops[hi].kind == Operand::Handle) break;
    if (hi == in.ops.size()) fail(in, "texture access without a bindless handle");
    if (hi + 1 >= in.ops.size() || in.ops[hi + 1].kind != Operand::Target) fail(in, "missing texture target");
    const std::string target = in.ops[hi + 1].name;
    const TargetInfo* ti = target_info(target);
    if (!ti) fail(in, "unsupported texture target " + target);
    const Operand* offset = nullptr;
    if (hi + 2 < in.ops.size()) {
        if (in.ops[hi + 2].kind != Operand::Offset || hi + 3 != in.ops.size()) fail(in, "unexpected operand after target");
        offset = &in.ops[hi + 2];
    }
    const Operand& d = in.ops[0];
    std::vector<int> lanes = lanes_of(d);
    const int n = int(lanes.size());
    const int nsrc = int(hi) - 1;
    const char t = data_type(in);
    const bool fragment = p_.stage == Stage::Fragment;
    auto first = [](int k) {
        std::vector<int> l;
        for (int i = 0; i < k; ++i) l.push_back(i);
        return l;
    };
    auto coord_vec = [&](const Operand& o, int k, char type) { return load(in, o, type, first(k)).node; };
    auto comp_of = [&](const Operand& o, int lane, char type) { return fetch(in, o, type, {swz(o, lane)}).node; };

    ir::TexOp tx;
    if (offset) {
        tx.has_offset = true;
        Ex e = load(in, *offset, 'S', first(3));
        for (int i = 0; i < 3; ++i) tx.offset[i] = int32_t(at(e.node).bits[i]);
    }
    const bool cube = target.find("CUBE") != std::string::npos;
    Type rt{kind_of(t), 4};

    if (op == "TXQ") {
        if (nsrc != 1) fail(in, "TXQ takes one operand");
        tx.func = ir::TexOp::Size;
        tx.resource = texture(in, target, 'F');
        if (target != "BUFFER" && target != "2DMS" && target != "ARRAY2DMS") tx.lod = comp_of(in.ops[1], 0, 'S');
        Node q{Op::Texture, {Kind::S, 4}}; // size padded to four components with zeros
        f_.tex_ops.push_back(tx);
        q.tex = int(f_.tex_ops.size()) - 1;
        store(in, d, ex(swizzle(node(q), lanes)), lanes);
        return;
    }
    if (op == "TXF" || op == "TXFMS") {
        if (nsrc != 1) fail(in, op + " takes one coordinate operand");
        if (ti->shadow || cube) fail(in, "texel fetch from " + target);
        if ((op == "TXFMS") != (target == "2DMS" || target == "ARRAY2DMS")) fail(in, "TXFMS needs a multisample target");
        tx.resource = texture(in, target, t);
        const Operand& c = in.ops[1];
        if (target == "BUFFER") {
            if (offset) fail(in, "offset on buffer fetch");
            tx.func = ir::TexOp::Fetch;
            tx.coord = comp_of(c, 0, 'S');
        } else {
            tx.func = op == "TXFMS" ? ir::TexOp::FetchMs : ir::TexOp::Fetch;
            tx.coord = coord_vec(c, ti->coords, 'S');
            tx.lod = comp_of(c, 3, 'S'); // lod, or sample index for TXFMS
            if (offset && op == "TXFMS") fail(in, "offset on multisample fetch");
        }
    } else if (op == "TXG") {
        if (nsrc != 1) fail(in, "TXG takes one coordinate operand");
        tx.func = ir::TexOp::Gather;
        tx.resource = texture(in, target, t);
        const Operand& c = in.ops[1];
        tx.coord = coord_vec(c, ti->coords, 'F');
        if (ti->shadow) {
            if (target == "SHADOWARRAYCUBE") fail(in, "gather from SHADOWARRAYCUBE");
            tx.ref = comp_of(c, ti->coords, 'F');
        } else {
            tx.gather_component = in.ops[hi].handle_comp < 0 ? 0 : in.ops[hi].handle_comp;
        }
    } else {
        // TEX, TXB, TXL, TXD: filtered sampling.
        if (target == "BUFFER" || target == "2DMS" || target == "ARRAY2DMS") fail(in, "filtered sampling of " + target);
        tx.resource = texture(in, target, t);
        const Operand& c = in.ops[1];
        const int k = ti->coords;
        if (ti->shadow) {
            if (target == "SHADOW1D") {
                tx.coord = comp_of(c, 0, 'F');
                tx.ref = comp_of(c, 2, 'F');
            } else if (target == "SHADOWARRAYCUBE") {
                tx.coord = coord_vec(c, 4, 'F');
                if (nsrc < 2) fail(in, "SHADOWARRAYCUBE needs the reference operand");
                tx.ref = comp_of(in.ops[2], 0, 'F');
            } else {
                Ex all = load(in, c, 'F', first(k + 1));
                tx.coord = swizzle(all.node, first(k));
                tx.ref = swizzle(all.node, {k});
            }
            rt = {Kind::F, 1};
        } else {
            tx.coord = coord_vec(c, k, 'F');
        }
        auto extra_scalar = [&]() {
            bool full = k + (ti->shadow ? 1 : 0) >= 4;
            if (full) {
                if (nsrc < 2) fail(in, "missing LOD/bias operand");
                return comp_of(in.ops[2], 0, 'F');
            }
            return comp_of(c, 3, 'F');
        };
        if (offset && cube) fail(in, "offset on cube map");
        if (op == "TEX" || op == "TXB") {
            if (fragment) {
                if (op == "TXB" && ti->shadow && target == "SHADOWARRAYCUBE") fail(in, "bias on cube-array shadow");
                tx.func = op == "TXB" ? ir::TexOp::SampleBias : ir::TexOp::Sample;
                if (op == "TXB") tx.lod = extra_scalar();
            } else if (ti->shadow && cube) {
                // No derivatives outside fragment programs: the base level.
                tx.func = ir::TexOp::SampleGrad;
                tx.ddx = tx.ddy = splat(kf(0.0f), 3);
            } else {
                tx.func = ir::TexOp::SampleLod;
                tx.lod = kf(0.0f);
            }
        } else if (op == "TXL") {
            if (ti->shadow && cube) fail(in, "TXL with a cube shadow target");
            tx.func = ir::TexOp::SampleLod;
            tx.lod = extra_scalar();
        } else if (op == "TXD") {
            if (nsrc != 3) fail(in, "TXD takes coordinates and two derivative operands");
            int dims = ti->array ? k - 1 : k;
            if (target == "SHADOW1D" || target == "SHADOWARRAY1D") dims = 1;
            int g = cube ? 3 : dims;
            tx.func = ir::TexOp::SampleGrad;
            tx.ddx = coord_vec(in.ops[2], g, 'F');
            tx.ddy = coord_vec(in.ops[3], g, 'F');
        } else {
            fail(in, "unsupported texture instruction");
        }
    }
    f_.tex_ops.push_back(tx);
    Node q{Op::Texture, rt};
    q.tex = int(f_.tex_ops.size()) - 1;
    const int r = node(q);
    if (rt.n == 1) store(in, d, ex(splat(r, n)), lanes);
    else store(in, d, ex(swizzle(r, lanes)), lanes);
}

// ---------------------------------------------------------------- memory

void Lowerer::lower_memory(const Instr& in) {
    const std::string& op = in.op;
    auto st = storage_mod(in);
    if (!st) fail(in, "unsupported storage modifier");
    const bool load_op = op == "LDC" || op == "LDB" || op == "LDS";
    const bool atomic = op == "ATOMS" || op == "ATOMB";
    const Operand& mem = in.ops.back();
    if (mem.kind != Operand::Var || !mem.indexed) fail(in, "memory operand must be an indexed buffer");

    enum { Uniform, StorageBuf, SharedMem } space;
    int buffer = -1;
    if (op == "LDC") {
        auto cb = p_.cbuffers.find(mem.name);
        if (cb == p_.cbuffers.end()) fail(in, mem.name + " is not a CBUFFER");
        auto& u = ubos_[cb->second];
        if (mem.index_reg.empty()) {
            u.bytes = std::max<uint32_t>(u.bytes, uint32_t(mem.index + 4 * st->count));
            for (int k = 0; k < st->count; ++k) {
                char& w = words_[{uint32_t(cb->second), uint32_t(mem.index + 4 * k)}];
                if (!w || w == 'F') w = st->t; // integer use wins: it is what bounds loops
            }
        } else {
            u.dynamic = true;
        }
        space = Uniform;
        buffer = cb->second;
    } else if (op == "LDB" || op == "STB" || op == "ATOMB") {
        auto sb = p_.storage.find(mem.name);
        if (sb == p_.storage.end()) fail(in, mem.name + " is not a STORAGE buffer");
        auto& u = sbos_[sb->second];
        (load_op ? u.first : u.second) = true;
        if (atomic) u.first = u.second = true;
        space = StorageBuf;
        buffer = sb->second;
    } else {
        if (mem.name != p_.shared_name) fail(in, mem.name + " is not shared memory");
        space = SharedMem;
    }
    if (mem.index_reg.empty() && mem.index % 4) fail(in, "misaligned memory access");

    // A dynamic byte offset lives in a scoped local `o`.
    Stmt block{Stmt::Block};
    int off = -1;
    const bool dynamic = !mem.index_reg.empty();
    if (dynamic) {
        sinks_.push_back(&block.body);
        Stmt decl{Stmt::DeclLocal};
        decl.target = ir::Target::Local;
        decl.var = local("o", {Kind::S, 1});
        decl.value = index_expr(mem);
        push(decl);
        off = local_node(decl.var);
    }
    const Type i1{Kind::S, 1};
    auto byte_offset = [&](int k) { // int node
        if (!dynamic) return ks(mem.index + 4 * k);
        return k ? bin(Op::Add, off, ks(4 * k), i1) : off;
    };
    auto word_index = [&](int k) {
        if (!dynamic) return ks((mem.index + 4 * k) / 4);
        return bin(Op::Shr, byte_offset(k), ks(2), i1);
    };
    auto uniform_element = [&](int element_node) {
        Node u{Op::Uniform, {Kind::F, 4}};
        u.var = buffer;
        u.a = element_node;
        return node(u);
    };
    auto word = [&](int k, char t) -> int { // one 32-bit word, typed
        if (space == Uniform) {
            if (!dynamic) {
                const int byte = mem.index + 4 * k;
                return bitcast(swizzle(uniform_element(ks(byte / 16)), {(byte / 4) % 4}), kind_of(t));
            }
            Node w{Op::UniformWord, {Kind::F, 1}};
            w.var = buffer;
            w.a = byte_offset(k);
            return bitcast(node(w), kind_of(t));
        }
        Node w{space == StorageBuf ? Op::Storage : Op::Shared, {Kind::U, 1}};
        w.var = buffer;
        w.a = word_index(k);
        return bitcast(node(w), kind_of(t));
    };

    if (load_op) {
        const Operand& d = in.ops[0];
        std::vector<int> lanes = lanes_of(d);
        std::vector<int> sw;
        bool one_element = space == Uniform && std::all_of(lanes.begin(), lanes.end(), [&](int l) {
            return l < st->count && (dynamic ? st->count == 4 : (mem.index + 4 * l) / 16 == mem.index / 16);
        });
        if (one_element) {
            // Words of one vec4 element read as a single swizzle.
            int element = dynamic ? bin(Op::Shr, off, ks(4), i1) : ks(mem.index / 16);
            for (int l : lanes) sw.push_back(dynamic ? l : ((mem.index + 4 * l) / 4) % 4);
            store(in, d, ex(bitcast(swizzle(uniform_element(element), sw), kind_of(st->t))), lanes);
        } else {
            std::vector<int> parts;
            for (int l : lanes)
                parts.push_back(l < st->count ? word(l, st->t) : st->t == 'F' ? kf(0.0f) : st->t == 'S' ? ks(0) : ku(0));
            store(in, d, ex(construct(parts, kind_of(st->t))), lanes);
        }
    } else if (!atomic) {
        const Operand& v = in.ops.at(0);
        for (int k = 0; k < st->count; ++k) {
            Stmt s{space == StorageBuf ? Stmt::StoreStorage : Stmt::StoreShared};
            s.var = buffer;
            s.a = word_index(k);
            s.value = bitcast(load(in, v, st->t, {k}).node, Kind::U);
            push(s);
        }
    } else {
        if (st->count != 1 || st->t == 'F') fail(in, "atomic on non-32-bit-integer data");
        static const std::map<std::string, ir::AtomicOp> fn = {{"ADD", ir::AtomicOp::Add}, {"MIN", ir::AtomicOp::Min},
                                                              {"MAX", ir::AtomicOp::Max}, {"AND", ir::AtomicOp::And},
                                                              {"OR", ir::AtomicOp::Or},   {"XOR", ir::AtomicOp::Xor},
                                                              {"EXCH", ir::AtomicOp::Exchange}};
        std::optional<ir::AtomicOp> f;
        for (auto& m : in.mods)
            if (fn.count(m)) f = fn.at(m);
        if (!f) fail(in, "unsupported atomic operation");
        if (in.ops.size() != 3) fail(in, "atomic takes result, data and address");
        Ex data = load(in, in.ops[1], st->t, {0});
        if (st->t == 'S') {
            // Shared/storage words are uint; signed min/max need their own view.
            if (*f == ir::AtomicOp::Min || *f == ir::AtomicOp::Max) fail(in, "signed atomic min/max on uint storage");
            data = convert(data, Kind::U);
        }
        Node a{Op::Atomic, {Kind::U, 1}};
        a.sub = uint8_t(*f);
        a.var = space == StorageBuf ? 1 : 0;
        a.imm = buffer;
        a.a = word_index(0);
        a.b = data.node;
        const Operand& d = in.ops[0];
        std::vector<int> lanes = lanes_of(d);
        store(in, d, ex(splat(bitcast(node(a), kind_of(st->t)), int(lanes.size()))), lanes, /*side_effect=*/true);
    }
    if (dynamic) {
        sinks_.pop_back();
        push(std::move(block));
    }
}

// ---------------------------------------------------------------- images

void Lowerer::lower_image(const Instr& in) {
    const std::string& op = in.op;
    size_t hi = 0;
    for (; hi < in.ops.size(); ++hi)
        if (in.ops[hi].kind == Operand::Handle) break;
    if (hi == in.ops.size() || in.ops.back().kind != Operand::Target) fail(in, "image access needs a handle and target");
    const std::string target = in.ops.back().name;
    if (op != "STOREIM" && hi + 2 != in.ops.size()) fail(in, "unexpected operand after image target");
    static const std::map<std::string, int> dims = {{"1D", 1},      {"2D", 2},      {"3D", 3},        {"CUBE", 3},
                                                    {"BUFFER", 1},  {"ARRAY1D", 2}, {"ARRAY2D", 3},   {"ARRAYCUBE", 3},
                                                    {"2DMS", 2},    {"ARRAY2DMS", 3}};
    auto dt = dims.find(target);
    if (dt == dims.end()) fail(in, "unsupported image target " + target);
    const int k = dt->second;
    const bool ms = target == "2DMS" || target == "ARRAY2DMS";
    auto coords = [&](const Operand& o) {
        std::vector<int> l;
        for (int i = 0; i < k; ++i) l.push_back(i);
        std::vector<int> args{load(in, o, 'S', l).node};
        if (ms) args.push_back(fetch(in, o, 'S', {swz(o, 3)}).node);
        return args;
    };
    if (op == "IMQ") {
        if (hi != 1) fail(in, "IMQ takes a destination and handle");
        const int img = image(in, target, 'F', "", false, false);
        const Operand& d = in.ops[0];
        std::vector<int> lanes = lanes_of(d);
        Node q{Op::Call, {Kind::S, 4}}; // size padded to four components with zeros
        q.sub = uint8_t(ir::Fn::ImageSize);
        q.var = img;
        store(in, d, ex(swizzle(node(q), lanes)), lanes);
        return;
    }
    if (op == "STOREIM") {
        if (hi != 0 || in.ops.size() != 4) fail(in, "STOREIM takes handle, data, coordinates, target");
        if (storage_mod(in)) fail(in, "unformatted STOREIM not supported yet");
        const char t = data_type(in);
        Stmt s{Stmt::ImageStore};
        s.var = image(in, target, t, "", false, true);
        auto c = coords(in.ops[2]);
        s.a = c[0];
        s.index = ms ? c[1] : -1;
        s.value = load(in, in.ops[1], t, {0, 1, 2, 3}).node;
        push(s);
        return;
    }
    auto st = storage_mod(in);
    if (!st) fail(in, "image load/atomic needs a storage modifier");
    const Operand& d = in.ops[0];
    std::vector<int> lanes = lanes_of(d);
    const int n = int(lanes.size());
    const Kind sk = kind_of(st->t);
    if (op == "LOADIM") {
        if (hi != 2) fail(in, "LOADIM takes result, coordinates, handle, target");
        Node q{Op::Call, {sk, 4}};
        q.sub = uint8_t(ir::Fn::ImageLoad);
        q.var = image(in, target, st->t, image_format(*st), true, false);
        q.args = coords(in.ops[1]);
        const int r = node(q);
        // Components past the storage size read as (0, 0, 1).
        const int zero = st->t == 'F' ? kf(0.0f) : st->t == 'S' ? ks(0) : ku(0);
        const int one = st->t == 'F' ? kf(1.0f) : st->t == 'S' ? ks(1) : ku(1);
        int full = r;
        if (st->count == 2) full = construct({swizzle(r, {0, 1}), zero, one}, sk);
        else if (st->count == 1) full = construct({swizzle(r, {0}), zero, zero, one}, sk);
        store(in, d, ex(swizzle(full, lanes)), lanes);
        return;
    }
    if (op == "ATOMIM") {
        if (hi != 3) fail(in, "ATOMIM takes result, data, coordinates, handle, target");
        if (st->count != 1 || st->t == 'F') fail(in, "image atomic on non-32-bit-integer data");
        static const std::map<std::string, ir::AtomicOp> fn = {{"ADD", ir::AtomicOp::Add}, {"MIN", ir::AtomicOp::Min},
                                                              {"MAX", ir::AtomicOp::Max}, {"AND", ir::AtomicOp::And},
                                                              {"OR", ir::AtomicOp::Or},   {"XOR", ir::AtomicOp::Xor},
                                                              {"EXCH", ir::AtomicOp::Exchange}};
        std::optional<ir::AtomicOp> f;
        for (auto& m : in.mods)
            if (fn.count(m)) f = fn.at(m);
        if (!f) fail(in, "unsupported image atomic");
        Node a{Op::Atomic, {sk, 1}};
        a.sub = uint8_t(*f);
        a.var = 2;
        a.imm = image(in, target, st->t, image_format(*st), true, true);
        a.args = coords(in.ops[2]);
        a.b = load(in, in.ops[1], st->t, {0}).node;
        store(in, d, ex(splat(node(a), n)), lanes, /*side_effect=*/true);
        return;
    }
    fail(in, "unsupported image instruction");
}

// ---------------------------------------------------------------- driver

ir::Function Lowerer::run(Result& r) {
    f_.stage = p_.stage;
    f_.group = p_.group;
    f_.shared_bytes = p_.shared_name.empty() ? 0 : std::max<uint32_t>(4, p_.shared_bytes);
    f_.prim_in = p_.prim_in;
    f_.prim_out = p_.prim_out;
    f_.vertices_out = p_.vertices_out;
    f_.invocations = p_.invocations;
    f_.precise = opt_.precise_registers;
    f_.demote = opt_.demote;
    f_.first_binding = opt_.first_binding;
    f_.early_fragment_tests = p_.stage == Stage::Fragment && p_.options.count("NV_early_fragment_tests");
    r.stage = p_.stage;
    r.workgroup = p_.group;
    r.shared_bytes = p_.shared_bytes;

    // Registers in natural order (R2 before R10).
    std::vector<std::string> names;
    for (const auto& [name, t] : p_.temps)
        if (t.kind != TempKind::Long) names.push_back(name);
    auto split = [](const std::string& s) {
        size_t i = s.find_last_not_of("0123456789") + 1;
        return std::make_pair(s.substr(0, i), i < s.size() ? std::stoll(s.substr(i)) : -1LL);
    };
    std::sort(names.begin(), names.end(), [&](const std::string& a, const std::string& b) { return split(a) < split(b); });
    read_.assign(p_.temp_list.size(), 0);
    reg_index_.assign(p_.temp_list.size(), -1);
    for (const auto& in : p_.code)
        for (size_t i = 0; i < in.ops.size(); ++i) {
            bool is_dest = i == 0 && in.op != "STB" && in.op != "STS" && in.op != "STOREIM" && in.op != "IF" && in.op != "KIL" &&
                           in.op != "BRK" && in.op != "CONT" && in.op != "REP";
            scan_reads(in.ops[i], is_dest);
        }
    for (const auto& name : names) {
        const Temp& t = p_.temps.at(name);
        reg_index_[size_t(t.id)] = int(f_.regs.size());
        f_.regs.push_back({name, t.array, read_[size_t(t.id)] != 0});
    }

    auto tree = build_tree(std::move(p_.code));
    std::vector<int> hs(p_.temp_list.size() * 4, kAbsent);
    resolve_handles(tree, hs);
    if (opt_.fold_conditions) plan_cc(tree);
    sinks_.push_back(&f_.body);
    lower_nodes(tree);
    // Outputs start as zero, so components the program never writes are defined
    // (a colour target's .zw behind a two-channel format, for example). Eden's
    // translation of the Maxwell binary gives 0 there; the hardware leaves whatever
    // the register held.
    {
        std::vector<Stmt> init;
        auto zero = [&](int var, Kind k) {
            Stmt s{Stmt::Assign};
            s.target = ir::Target::Output;
            s.var = var;
            s.lanes = {0, 1, 2, 3};
            s.value = node(Node{Op::Const, {k, 4}});
            init.push_back(std::move(s));
        };
        for (auto& [loc, t] : out_color_) zero(loc, kind_of(t));
        for (auto& [loc, t] : out_attr_) zero(1000 + loc, kind_of(t));
        f_.body.insert(f_.body.begin(), std::make_move_iterator(init.begin()), std::make_move_iterator(init.end()));
    }
    if (p_.stage != Stage::Geometry) position_fixups();

    // Reflection and bindings, shared by both backends.
    uint32_t binding = opt_.first_binding;
    for (auto& [index, u] : ubos_) {
        uint32_t bytes = u.dynamic ? 65536u : ((u.bytes + 15u) & ~15u);
        f_.ubos.push_back({uint32_t(index), bytes, binding, u.dynamic});
        Resource res;
        res.kind = ResourceKind::UniformBuffer;
        res.binding = binding++;
        res.name = "cb" + std::to_string(index);
        res.index = uint32_t(index);
        res.size = bytes;
        res.dynamic = u.dynamic;
        res.read = true;
        r.resources.push_back(res);
    }
    for (auto& [index, rw] : sbos_) {
        f_.sbos.push_back({uint32_t(index), binding, rw.first, rw.second});
        Resource res;
        res.kind = ResourceKind::StorageBuffer;
        res.binding = binding++;
        res.name = "sb" + std::to_string(index);
        res.index = uint32_t(index);
        res.read = rw.first;
        res.written = rw.second;
        r.resources.push_back(res);
    }
    for (auto& t : f_.textures) {
        t.binding = binding++;
        Resource res;
        res.kind = ResourceKind::SampledTexture;
        res.binding = t.binding;
        res.name = t.name;
        res.handle = t.handle;
        res.target = t.target;
        res.type = t.type;
        res.read = true;
        r.resources.push_back(res);
    }
    for (auto& t : f_.images) {
        t.binding = binding++;
        Resource res;
        res.kind = ResourceKind::StorageImage;
        res.binding = t.binding;
        res.name = t.name;
        res.handle = t.handle;
        res.target = t.target;
        res.type = t.type;
        res.format = t.format;
        res.read = t.read;
        res.written = t.written;
        r.resources.push_back(res);
    }
    r.next_binding = binding;
    for (auto& [loc, t] : in_attr_) {
        std::string interp;
        if (p_.stage == Stage::Fragment) {
            auto it = p_.interpolation.find(loc);
            if (it != p_.interpolation.end()) interp = it->second;
        }
        f_.inputs.push_back({loc, t, interp});
        r.inputs.push_back({uint32_t(loc), t, interp});
    }
    for (auto& [loc, t] : out_attr_) {
        f_.outputs.push_back({loc, t, ""});
        r.outputs.push_back({uint32_t(loc), t, ""});
    }
    for (auto& [loc, t] : out_color_) {
        f_.colors.push_back({loc, t, ""});
        r.outputs.push_back({uint32_t(loc), t, ""});
    }
    for (auto& [key, type] : words_) r.constant_words.push_back({key.first, key.second, type});
    if (p_.stage == Stage::Geometry) {
        static const std::set<std::string> in_prims = {"POINTS", "LINES", "LINES_ADJACENCY", "TRIANGLES", "TRIANGLES_ADJACENCY"};
        static const std::set<std::string> out_prims = {"POINTS", "LINE_STRIP", "TRIANGLE_STRIP"};
        if (!in_prims.count(p_.prim_in) || !out_prims.count(p_.prim_out)) throw Error(0, "unsupported geometry primitive types");
    }
    return std::move(f_);
}

} // namespace

ir::Function lower(detail::Program& p, const Options& options, Result& result) {
    Lowerer l(p, options);
    return l.run(result);
}

} // namespace nvasm::lower

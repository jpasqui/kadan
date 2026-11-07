// Maxwell backend: the NVN code allocation and control section (MAXWELL_BACKEND.md).
//
// Graphics code: the magic word 0x12345678, zeros to 0x30, the shader program
// header (SPH, 0x50 bytes), then 32-byte bundles from 0x80: a scheduling word
// and three instructions. The program ends EXIT, a branch to itself and NOP
// padding, and the allocation is a multiple of 256 bytes. Compute code has no
// header. The control section is the stock program's with the sizes and the
// register count replaced.
#include "maxwell_mir.h"
#include "nvasm_ir.h"

#include <algorithm>
#include <cstring>

namespace nvasm::ir {

maxwell::Program select_maxwell(const Function& f, bool& memory, uint32_t& local_bytes);   // nvasm_maxwell.cpp
constexpr uint32_t kStackBytes = 0x600;   // the reconvergence stack NVIDIA's compiler reserves

namespace {

using namespace nvasm::maxwell;

constexpr uint64_t kSelfBranchSlot0 = 0xE2400FFFFF07000Full;   // offset -16: the bundle
constexpr uint64_t kSelfBranch = 0xE2400FFFFF87000Full;        // offset -8
constexpr uint64_t kNop = 0x50B0000000070F00ull;

uint64_t control_bits(const Control& c) {
    return uint64_t(c.stall & 15) | uint64_t(c.yield & 1) << 4 | uint64_t(c.wbar & 7) << 5 | uint64_t(c.rbar & 7) << 8 |
           uint64_t(c.wait & 63) << 11 | uint64_t(c.reuse & 15) << 17;
}

// The shader program header for a vertex or fragment program.
std::array<uint32_t, 20> sph_for(const Function& f, bool memory, uint32_t local_bytes, bool stack) {
    std::array<uint32_t, 20> w{};
    uint8_t* b = reinterpret_cast<uint8_t*>(w.data());
    w[1] = local_bytes & 0xffffff;       // local memory per invocation
    w[3] = stack ? kStackBytes : 0;      // call/return (reconvergence) stack
    if (f.stage == Stage::Fragment) {
        w[0] = 0x00065462u | (f.kills ? 1u << 15 : 0u) | (memory ? 1u << 26 : 0u);   // PS, version 3, MRT, SASS version 3
        b[0x17] = 0x80;                                    // position w: the perspective divisor
        if (f.builtin_used[size_t(Builtin::FragCoord)]) b[0x17] |= 0x70;
        for (const auto& in : f.inputs) {
            if (in.location < 0 || in.location >= 32) throw Error(0, "maxwell: input location out of range");
            const uint8_t mode = in.interpolation == "flat" ? 1 : in.interpolation == "noperspective" ? 3 : 2;
            b[0x18 + in.location] = uint8_t(mode | mode << 2 | mode << 4 | mode << 6);
        }
        int target = 0;
        for (const auto& c : f.colors) {
            if (c.location < 0 || c.location >= 8) throw Error(0, "maxwell: colour output out of range");
            w[0x48 / 4] |= 0xFu << (4 * c.location);
            ++target;
        }
        (void)target;
        if (f.frag_depth) w[0x4c / 4] |= 2;
        return w;
    }
    if (f.stage == Stage::Vertex) {
        w[0] = 0x02060461u | (memory ? 1u << 26 : 0u);   // VS, version 3, SASS version 3 (bit 25 as NVIDIA's compiler sets it)
        if (f.builtin_used[size_t(Builtin::InstanceId)]) b[0x2b] |= 0x40;
        if (f.builtin_used[size_t(Builtin::VertexId)]) b[0x2b] |= 0x80;
        for (const auto& in : f.inputs) {
            if (in.location < 0 || in.location >= 32) throw Error(0, "maxwell: input location out of range");
            b[0x18 + in.location / 2] |= uint8_t(0xF << (4 * (in.location % 2)));
        }
        if (f.position) b[0x35] |= 0xF0;
        if (f.layer) b[0x35] |= 0x02;   // render target array index
        for (const auto& o : f.outputs) {
            if (o.location < 0 || o.location >= 32) throw Error(0, "maxwell: output location out of range");
            b[0x36 + o.location / 2] |= uint8_t(0xF << (4 * (o.location % 2)));
        }
        return w;
    }
    if (f.stage == Stage::Geometry) {
        w[0] = 0x10061061u | (memory ? 1u << 26 : 0u);   // GS, version 3, SASS version 3, stream 0 out
        w[2] = uint32_t(std::max(f.invocations, 1)) << 24;   // threads per input primitive
        const uint32_t topology = f.prim_out == "POINTS" ? 1 : f.prim_out == "LINE_STRIP" ? 6 : f.prim_out == "TRIANGLE_STRIP" ? 7 : 0;
        if (!topology) throw Error(0, "maxwell: geometry output primitive " + f.prim_out);
        w[3] |= topology << 24;
        w[4] = uint32_t(f.vertices_out) & 0xfff;
        for (const Node& n : f.nodes)
            if (n.op == ir::Op::Builtin && ir::Builtin(n.sub) == ir::Builtin::GsPosition) b[0x17] |= 0xF0;   // input position
        for (const auto& in : f.inputs) {
            if (in.location < 0 || in.location >= 32) throw Error(0, "maxwell: input location out of range");
            b[0x18 + in.location / 2] |= uint8_t(0xF << (4 * (in.location % 2)));
        }
        if (f.position) b[0x35] |= 0xF0;
        if (f.layer) b[0x35] |= 0x02;
        for (const auto& o : f.outputs) {
            if (o.location < 0 || o.location >= 32) throw Error(0, "maxwell: output location out of range");
            b[0x36 + o.location / 2] |= uint8_t(0xF << (4 * (o.location % 2)));
        }
        return w;
    }
    throw Error(0, "maxwell: no header for this stage yet");
}

} // namespace

void write_maxwell(const Function& f, Result& r) {
    bool memory = false;
    uint32_t local_bytes = 0;
    Program p = select_maxwell(f, memory, local_bytes);
    // Selection emits every value before its readers (loop-carried values are set
    // before the loop): a read ahead of any write is a selector bug.
    {
        std::vector<bool> reg(size_t(p.vregs), false), pred(size_t(p.pregs), false);
        auto read = [&](const Opnd& o) {
            if (o.k == Opnd::R && o.v >= 0)
                for (int j = 0; j < o.n; ++j)
                    if (!reg[size_t(o.v + j)]) throw Error(0, "maxwell: selection reads a register before writing it");
        };
        auto read_p = [&](int32_t v) {
            if (v >= 0 && !pred[size_t(v)]) throw Error(0, "maxwell: selection reads a predicate before writing it");
        };
        for (const Inst& i : p.code) {
            read(i.a);
            read(i.b);
            read(i.c);
            for (int32_t u : i.uses) read(Opnd::reg(u));
            read_p(i.guard);
            read_p(i.pa);
            read_p(i.pb);
            if (i.d >= 0)
                for (int j = 0; j < i.dn; ++j) reg[size_t(i.d + j)] = true;
            if (i.pd >= 0) pred[size_t(i.pd)] = true;
        }
    }
    bool stack = false;
    for (const Inst& i : p.code) stack |= i.op == maxwell::Op::SSY || i.op == maxwell::Op::PBK || i.op == maxwell::Op::PCNT;
    optimize(p);
    const Allocation a = allocate(p);
    // Moves that allocation made into no-ops.
    p.code.erase(std::remove_if(p.code.begin(), p.code.end(), [&](const Inst& i) {
                     return i.op == maxwell::Op::MOV && i.guard == kPT && i.b.k == Opnd::R && !i.b.neg && !i.b.abs &&
                            i.d >= 0 && a.reg[size_t(i.d)] == (i.b.v == kRZ ? 255 : a.reg[size_t(i.b.v)]);
                 }),
                 p.code.end());
    const std::vector<Control> scheduled = schedule(p, a);

    // Instructions without the labels, and where each label lands.
    std::vector<const Inst*> code;
    std::vector<Control> control;
    std::vector<size_t> label_index(size_t(p.labels), SIZE_MAX);
    for (size_t k = 0; k < p.code.size(); ++k) {
        if (p.code[k].op == maxwell::Op::Label) {
            label_index[size_t(p.code[k].target)] = code.size();
            continue;
        }
        code.push_back(&p.code[k]);
        control.push_back(scheduled[k]);
    }
    auto pc = [](size_t k) { return int32_t((k / 3) * 32 + 8 + (k % 3) * 8); };
    std::vector<int32_t> label_pc(size_t(p.labels), -1);
    for (size_t l = 0; l < label_index.size(); ++l)
        if (label_index[l] != SIZE_MAX) {
            if (label_index[l] >= code.size()) throw Error(0, "maxwell: a label after the last instruction");
            label_pc[l] = pc(label_index[l]);
        }

    const size_t slots = code.size() + 1;   // and the branch to itself
    const size_t bundles = (slots + 2) / 3;
    std::vector<uint64_t> words;
    for (size_t g = 0; g < bundles; ++g) {
        uint64_t ctl = 0;
        std::array<uint64_t, 3> ins{};
        for (size_t s = 0; s < 3; ++s) {
            const size_t k = g * 3 + s;
            Control c;
            if (k < code.size()) {
                c = control[k];
                ins[s] = encode(*code[k], a, pc(k), label_pc);
            } else if (k == code.size()) {
                c.stall = 15;
                ins[s] = s == 0 ? kSelfBranchSlot0 : kSelfBranch;
            } else {
                c.stall = 0;
                c.yield = 0;
                ins[s] = kNop;
            }
            ctl |= control_bits(c) << (21 * s);
        }
        words.push_back(ctl);
        words.insert(words.end(), ins.begin(), ins.end());
    }

    const bool graphics = f.stage != Stage::Compute;
    std::vector<uint8_t> out(graphics ? 0x80 : 0, 0);
    if (graphics) {
        const uint32_t magic = 0x12345678;
        std::memcpy(out.data(), &magic, 4);
        const auto sph = sph_for(f, memory, local_bytes, stack);
        std::memcpy(out.data() + 0x30, sph.data(), sizeof sph);
    }
    const size_t program_end = out.size() + words.size() * 8;
    out.resize((program_end + 255) / 256 * 256, 0);
    std::memcpy(out.data() + (graphics ? 0x80 : 0), words.data(), words.size() * 8);
    r.maxwell = std::move(out);
    r.maxwell_program_size = uint32_t(program_end - (graphics ? 0x30 : 0));
    r.maxwell_registers = uint32_t(std::max(a.registers, 1));
    r.maxwell_listing = listing(p, &a);
    r.maxwell_scratch = local_bytes * 32 + (stack ? kStackBytes : 0);
}

} // namespace nvasm::ir

namespace nvasm {

std::vector<uint8_t> maxwell_control(const std::vector<uint8_t>& stock, const Result& compiled) {
    if (stock.size() < 0x878) throw Error(0, "maxwell: the stock control section is too small");
    if (compiled.maxwell.empty()) throw Error(0, "maxwell: nothing compiled");
    std::vector<uint8_t> c = stock;
    auto put = [&](size_t offset, uint32_t v) { std::memcpy(c.data() + offset, &v, 4); };
    const uint32_t code_size = uint32_t(compiled.maxwell.size());
    put(0x6f8, compiled.maxwell_program_size);
    put(0x6fc, 0);            // no immediate pool: constants are in the instructions
    put(0x700, code_size);
    put(0x704, code_size);
    put(0x70c, compiled.maxwell_registers);
    put(0x710, compiled.maxwell_scratch);
    return c;
}

} // namespace nvasm

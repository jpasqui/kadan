#pragma once
// The translator's intermediate representation: typed expression trees in an
// arena plus structured statements. Lowering (nvasm.cpp) decides every semantic
// question once; the backends only print it (nvasm_glsl.cpp) or encode it
// (nvasm_spirv.cpp).
#include "nvasm.h"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

namespace nvasm::ir {

enum class Kind : uint8_t { F, S, U, B }; // float, int, uint, bool
struct Type {
    Kind k = Kind::F;
    uint8_t n = 1; // components
    bool operator==(const Type& o) const { return k == o.k && n == o.n; }
};

enum class Op : uint8_t {
    Const,       // bits[0..n)
    Reg,         // var: register; a: array index (int) or -1. Always vec4 float.
    Local,       // var: local variable
    Input,       // var: location; imm = GS vertex index or -1
    Builtin,     // sub: Builtin
    Output,      // var: as Target::Output (reading back a written output)
    Uniform,     // var: uniform buffer; a: element index (int) -> vec4 float
    UniformWord, // var: uniform buffer; a: byte offset (int) -> float (dynamic single word)
    Storage,     // var: storage buffer; a: word index (int) -> uint
    Shared,      // a: word index (int) -> uint
    Swizzle,     // a; sw[0..n)
    Construct,   // args -> vector
    Bitcast,     // a -> type
    Convert,     // a -> type (numeric conversion)
    Neg, Abs, BitNot, LNot,
    Add, Sub, Mul, Div, Mod, Shl, Shr, And, Or, Xor,
    Eq, Ne, Lt, Le, Gt, Ge, // component-wise comparisons -> bool / bvecN (operand kind decides F/S/U)
    LAnd, LOr,
    Select,      // a ? b : c (scalar bool a)
    Call,        // sub: Fn; args
    Texture,     // tex: index into Function::tex_ops
    Atomic,      // sub: AtomicOp; var: target space (0 shared, 1 storage buffer, 2 image); args
};

enum class Builtin : uint8_t {
    FragCoord, Facing /*vec4(±1,0,0,1)*/, SampleId /*ivec4*/, VertexId /*ivec4*/, InstanceId /*ivec4, minus base*/,
    GlobalId, LocalId, GroupId, LocalIndex, /*uvec4*/ GsPosition /*vec4, imm = vertex*/,
    OutPosition /*the written clip position, vec4*/,
};

enum class Fn : uint8_t {
    Fma, Min, Max, Clamp, Floor, Ceil, Trunc, RoundEven, Fract, Exp2, Log2, Sin, Cos, InverseSqrt, DFdx, DFdy,
    PackHalf2x16, UnpackHalf2x16, IsNan, Any, F2I, F2U, Dp2, Dp3, Dp4, CcSetF, CcSetI, CcSetU,
    CcEq, CcNe, CcLt, CcLe, CcGt, CcGe, CcNan, CcLeg, ImageSize, ImageLoad, Pow,
};

enum class AtomicOp : uint8_t { Add, Min, Max, And, Or, Xor, Exchange };

// Operand list of a call or constructor, stored inline (at most eight).
class Args {
public:
    Args() = default;
    Args(std::initializer_list<int32_t> v) { assign(v.begin(), v.end()); }
    Args(const std::vector<int32_t>& v) { assign(v.begin(), v.end()); }
    template <class It> void assign(It first, It last) {
        n_ = 0;
        for (; first != last; ++first) push_back(*first);
    }
    void push_back(int32_t v) {
        if (n_ == v_.size()) throw std::logic_error("more than eight IR operands");
        v_[n_++] = v;
    }
    size_t size() const { return n_; }
    bool empty() const { return n_ == 0; }
    int32_t operator[](size_t i) const { return v_[i]; }
    int32_t at(size_t i) const {
        if (i >= n_) throw std::out_of_range("IR operand index");
        return v_[i];
    }
    const int32_t* begin() const { return v_.data(); }
    const int32_t* end() const { return v_.data() + n_; }

private:
    std::array<int32_t, 8> v_{};
    uint32_t n_ = 0;
};

struct Node {
    Op op;
    Type type;
    uint8_t sub = 0;
    int32_t a = -1, b = -1, c = -1;
    int32_t var = -1;
    int32_t imm = -1;
    std::array<uint8_t, 4> sw{0, 1, 2, 3};
    std::array<uint32_t, 4> bits{};
    int32_t tex = -1;
    Args args;
};

// A texture access. Coordinates and operands are node ids; -1 when absent.
struct TexOp {
    enum Func : uint8_t { Sample, SampleBias, SampleLod, SampleGrad, Fetch, FetchMs, Gather, Size } func = Sample;
    int32_t resource = -1;       // index into Function::textures
    int32_t coord = -1;          // float/int vector sized for the target
    int32_t lod = -1;            // lod, bias, sample index, size lod
    int32_t ref = -1;            // depth reference
    int32_t ddx = -1, ddy = -1;
    std::array<int32_t, 3> offset{};
    bool has_offset = false;
    int gather_component = 0;
};

// Output: var is the colour location, or 1000 + location for a generic output.
enum class Target : uint8_t { Reg, Local, Output, Position, FragDepth, Layer };

struct Stmt {
    enum K : uint8_t {
        Assign,     // target[lanes] = value
        DeclLocal,  // Local var = value (declares it here)
        If,         // cond ? body : other
        Loop,       // count (int node) or -1 for forever; var = counter local for counted loops
        Break, Continue, Kill, Demote, Emit, EndPrimitive, Barrier, GroupMemoryBarrier, MemoryBarrier,
        StoreStorage, StoreShared, // var: buffer (storage); a: word index; value: uint
        ImageStore,                // var: image; a: coords; index: sample (multisample) or -1; value: data
        Expr,                      // evaluate `value` for its effect (atomics)
        Block,                     // scoped body
    } k;
    Target target = Target::Reg;
    int32_t var = -1;       // register/local/output/cc register
    int32_t index = -1;     // register array index node, or -1
    std::vector<uint8_t> lanes;
    int32_t value = -1, cond = -1, count = -1, a = -1;
    std::vector<Stmt> body, other;
    // If: `a` 1 prints a one-statement body on the same line, 2 the same braced.
};

struct Texture { // a declared sampler
    std::string name, target;  // assembly target (2D, CUBE, SHADOWCUBE, BUFFER, ...)
    char type = 'F';
    std::vector<HandleWord> handle;
    uint32_t binding = 0;
};
struct Image {
    std::string name, target, format; // format: GLSL qualifier or empty
    char type = 'F';
    bool read = false, written = false;
    std::vector<HandleWord> handle;
    uint32_t binding = 0;
};
struct UniformBuffer { uint32_t index, bytes, binding; bool dynamic; };
struct StorageBuffer { uint32_t index, binding; bool read, written; };
struct Register { std::string name; int array = 0; bool read = false; };
struct Local { std::string name; Type type; };
struct Varying { int location; char type; std::string interpolation; };

struct Function {
    Stage stage{};
    std::vector<Node> nodes;
    std::vector<TexOp> tex_ops;
    std::vector<Stmt> body;
    std::vector<Register> regs;       // declared registers (natural order)
    std::vector<Local> locals;
    std::vector<Texture> textures;
    std::vector<Image> images;
    std::vector<UniformBuffer> ubos;
    std::vector<StorageBuffer> sbos;
    std::vector<Varying> inputs, outputs, colors; // stage inputs, generic outputs, colour outputs
    bool cc_used[2] = {false, false};
    bool builtin_used[11] = {};
    bool frag_depth = false, position = false, layer = false, kills = false;
    bool precise = false, early_fragment_tests = false;
    bool demote = true;
    std::array<uint32_t, 3> group{1, 1, 1};
    uint32_t shared_bytes = 0;
    std::string prim_in, prim_out;
    int vertices_out = 0, invocations = 0;
    uint32_t first_binding = 0;
    std::vector<int> gs_positions; // vertex indices read through gl_in[]

    int add(Node n) {
        nodes.push_back(std::move(n));
        return int(nodes.size()) - 1;
    }
    const Node& at(int i) const { return nodes.at(size_t(i)); }
};

std::string print_glsl(const Function& f);          // nvasm_glsl.cpp
std::vector<uint32_t> write_spirv(const Function& f); // nvasm_spirv.cpp
void write_maxwell(const Function& f, Result& r);       // nvasm_maxwell.cpp

} // namespace nvasm::ir

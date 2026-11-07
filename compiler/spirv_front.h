#pragma once
// SPIR-V to NV_gpu_program5 assembly: the front end of our own compiler for the shaders UE 4.26 writes (stage 8.4
// of tools/navigation/OWN_SHADER_MAPS.md). UE's Vulkan shader format compiles each shader's HLSL with DXC to SPIR-V;
// this turns that SPIR-V into the NV assembly our Maxwell compiler (nvasm_parse, nvasm_lower, maxwell_*) takes, in the
// form the game's own programs have: constant buffers bufN, textures as bindless handles read from buffer 14 (the
// texture handle at 328 + 8 * slot ORed with the sampler handle at 1352 + 8 * slot).
//
// DXC's output is one function with structured control flow (selection and loop merges), SSA values with Phi, uniform
// blocks read through access chains at their members' declared offsets, and combined image and sampler reads. Each
// SSA value gets a temporary of its own and the compiler's register allocator packs them. Booleans are floats 1.0 and
// 0.0. Anything outside what is handled throws with the instruction's name, never a partial program.
#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace spirv_front {

struct Error : std::runtime_error { explicit Error(const std::string& s) : std::runtime_error("spirv front end: " + s) {} };

enum class Stage { Vertex, Fragment, Geometry };

struct Member { std::string name; std::uint32_t offset = 0, size = 0; };   // size: bytes the member's layout spans
struct Block { std::string name; std::uint32_t set = 0, binding = 0; int slot = -1; std::vector<Member> members;
               std::uint32_t size = 0;             // bytes the declared members span
};
struct Resource { std::string name; std::uint32_t set = 0, binding = 0; int slot = -1; std::string target; };
struct IoVar { std::string name; int location = -1; std::string builtin; unsigned components = 4; bool flat = false, centroid = false, integer = false, noperspective = false;
               unsigned count = 1;                  // array elements, each at the next location
};
struct Interface {
    Stage stage = Stage::Fragment;
    std::vector<Block> blocks;          // uniform blocks, in slot order
    std::vector<Resource> textures, samplers;
    std::vector<Resource> storage;      // structured buffers (read only), storage slots sbo_bufN
    std::vector<IoVar> inputs, outputs;
    int default_sampler = -1;           // the sampler slot reads without a sampler take; -1: none
    std::set<int> read_inputs;          // a pixel program's input locations it reads
};

struct Options {
    // The constant buffer slot of a uniform block, by its name (default: the blocks in order of their binding, from 0).
    std::map<std::string, int> block_slots;
    // The game's byte offset of a member, when its renderer lays the block out differently from stock UE (by block and
    // member name); none keeps the declared offset.
    std::function<std::optional<std::uint32_t>(const std::string& block, const std::string& member, std::uint32_t offset)> remap;
    // Writes to gl_ClipDistance are dropped instead of refused. The game's renderer uses no clip distances (none of its
    // programs writes one); Kadan's compile target turns the global clip plane off, so this is for measurements of
    // shaders compiled with it on.
    bool drop_clip_distance = false;
    // Temporaries share registers where their lives do not overlap (off: one register each, for checking).
    bool reuse_registers = true;
    // A vertex program reads its ATTRIBUTEn input from vertex attribute n, as the game's vertex declarations bind them
    // (off: the SPIR-V location, which DXC gives in declaration order).
    bool vertex_attribute_semantics = false;
    // The loose parameters ($Globals) split as the game's compiler packs them: integers in "$Globals.u" in the loose
    // slot, floats in "$Globals" in the next, each member at the start of a 16-byte row (off: one block, DXC's layout).
    bool split_loose_integers = false;
    // Texture and sampler slots in the order the code first reads each, as the game's compiler numbers them (off:
    // binding order).
    bool resources_by_first_use = false;
    // OpFMod truncates as HLSL's fmod does (DXC writes fmod as OpFMod; the game's programs truncate).
    bool fmod_truncates = false;
};

struct Result { std::string assembly; Interface iface; };

namespace detail {

enum Op : std::uint16_t {
    OpUndef = 1, OpSource = 3, OpSourceExtension = 4, OpName = 5, OpMemberName = 6, OpString = 7, OpLine = 8,
    OpExtension = 10, OpExtInstImport = 11, OpExtInst = 12, OpMemoryModel = 14, OpEntryPoint = 15,
    OpExecutionMode = 16, OpCapability = 17, OpTypeVoid = 19, OpTypeBool = 20, OpTypeInt = 21, OpTypeFloat = 22,
    OpTypeVector = 23, OpTypeMatrix = 24, OpTypeImage = 25, OpTypeSampler = 26, OpTypeSampledImage = 27,
    OpTypeArray = 28, OpTypeRuntimeArray = 29, OpTypeStruct = 30, OpTypePointer = 32, OpTypeFunction = 33,
    OpConstantTrue = 41, OpConstantFalse = 42, OpConstant = 43, OpConstantComposite = 44, OpConstantNull = 46,
    OpFunction = 54, OpFunctionParameter = 55, OpFunctionEnd = 56, OpVariable = 59, OpLoad = 61, OpStore = 62,
    OpAccessChain = 65, OpInBoundsAccessChain = 66, OpDecorate = 71, OpMemberDecorate = 72, OpVectorShuffle = 79,
    OpCompositeConstruct = 80, OpCompositeExtract = 81, OpCompositeInsert = 82, OpCopyObject = 83, OpTranspose = 84,
    OpSampledImage = 86, OpImageSampleImplicitLod = 87, OpImageSampleExplicitLod = 88,
    OpImageSampleDrefImplicitLod = 89, OpImageSampleDrefExplicitLod = 90, OpImageFetch = 95, OpImageQuerySizeLod = 103,
    OpConvertFToU = 109, OpConvertFToS = 110, OpConvertSToF = 111, OpConvertUToF = 112, OpBitcast = 124,
    OpSNegate = 126, OpFNegate = 127, OpIAdd = 128, OpFAdd = 129, OpISub = 130, OpFSub = 131, OpIMul = 132,
    OpFMul = 133, OpUDiv = 134, OpSDiv = 135, OpFDiv = 136, OpUMod = 137, OpSRem = 138, OpSMod = 139, OpFRem = 140,
    OpFMod = 141, OpVectorTimesScalar = 142, OpMatrixTimesScalar = 143, OpVectorTimesMatrix = 144,
    OpMatrixTimesVector = 145, OpMatrixTimesMatrix = 146, OpDot = 148, OpAny = 154, OpAll = 155, OpIsNan = 156,
    OpIsInf = 157, OpLogicalEqual = 164, OpLogicalNotEqual = 165, OpLogicalOr = 166, OpLogicalAnd = 167,
    OpLogicalNot = 168, OpSelect = 169, OpIEqual = 170, OpINotEqual = 171, OpUGreaterThan = 172,
    OpSGreaterThan = 173, OpUGreaterThanEqual = 174, OpSGreaterThanEqual = 175, OpULessThan = 176,
    OpSLessThan = 177, OpULessThanEqual = 178, OpSLessThanEqual = 179, OpFOrdEqual = 180, OpFUnordEqual = 181,
    OpFOrdNotEqual = 182, OpFUnordNotEqual = 183, OpFOrdLessThan = 184, OpFUnordLessThan = 185,
    OpFOrdGreaterThan = 186, OpFUnordGreaterThan = 187, OpFOrdLessThanEqual = 188, OpFUnordLessThanEqual = 189,
    OpFOrdGreaterThanEqual = 190, OpFUnordGreaterThanEqual = 191, OpShiftRightLogical = 194,
    OpShiftRightArithmetic = 195, OpShiftLeftLogical = 196, OpBitwiseOr = 197, OpBitwiseXor = 198,
    OpBitwiseAnd = 199, OpNot = 200, OpDPdx = 207, OpDPdy = 208, OpFwidth = 209, OpEmitVertex = 218,
    OpEndPrimitive = 219, OpPhi = 245, OpLoopMerge = 246, OpSelectionMerge = 247, OpLabel = 248, OpBranch = 249,
    OpBranchConditional = 250, OpSwitch = 251, OpKill = 252, OpReturn = 253, OpReturnValue = 254,
    OpUnreachable = 255, OpNoLine = 317, OpModuleProcessed = 330, OpDecorateString = 5632,
    OpMemberDecorateString = 5633,
};

// Decorations and built-ins used.
constexpr std::uint32_t DecBufferBlock = 3;
constexpr std::uint32_t DecBlock = 2, DecColMajor = 5, DecArrayStride = 6, DecMatrixStride = 7, DecRowMajor = 4,
    DecBuiltIn = 11, DecNoPerspective = 13, DecFlat = 14, DecCentroid = 16, DecLocation = 30, DecBinding = 33, DecDescriptorSet = 34,
    DecOffset = 35;
constexpr std::uint32_t BuiltInClipDistance = 3;
constexpr std::uint32_t BuiltInPosition = 0, BuiltInFragCoord = 15, BuiltInFrontFacing = 17, BuiltInFragDepth = 22,
    BuiltInVertexIndex = 42, BuiltInInstanceIndex = 43, BuiltInLayer = 9;
constexpr std::uint32_t StorageStorageBuffer = 12;
constexpr std::uint32_t StorageUniformConstant = 0, StorageInput = 1, StorageUniform = 2, StorageOutput = 3,
    StorageFunction = 7, StoragePrivate = 6;

struct Inst { std::uint16_t op = 0; std::vector<std::uint32_t> w; };   // w: the operands (after the opcode word)

enum class K { Void, Bool, Int, Float, Vector, Matrix, Image, Sampler, SampledImage, Array, Struct, Pointer, Function };
struct Type {
    K k = K::Void;
    unsigned width = 32; bool sign = false;     // Int
    std::uint32_t elem = 0; unsigned count = 0; // Vector (component type, n), Matrix (column type, columns), Array
    std::vector<std::uint32_t> members;         // Struct
    std::uint32_t storage = 0;                  // Pointer
    unsigned dim = 0; bool depth = false, arrayed = false, ms = false; // Image
};

// A value as the program holds it: the temporary (or immediate) and its component count. Matrices hold one
// temporary per column.
struct Val {
    std::string reg;                   // "V12", or an immediate "{1, 0, 0, 0}"
    unsigned n = 1;
    std::vector<std::string> cols;     // matrix columns
    bool imm = false;
};

inline std::string Fmt(float f) {
    if (std::isnan(f)) return "NaN";
    if (std::isinf(f)) return f > 0 ? "1e+38" : "-1e+38";
    char b[48]; std::snprintf(b, sizeof b, "%.9g", double(f));
    std::string s = b;
    return s;
}
inline const char* Mask(unsigned n) { static const char* m[] = {"", ".x", ".xy", ".xyz", ""}; return m[n > 4 ? 4 : n]; }
inline std::string KindName(char k) { return std::string(1, k); }
inline std::string Comp(unsigned i) { return std::string(1, "xyzw"[i]); }

class Translator {
public:
    Translator(const std::vector<std::uint32_t>& words, const Options& options) : options_(options) { Parse(words); }

    Result Run() {
        Collect();
        std::ostringstream body;
        out_ = &body;
        EmitFunction();
        Result r;
        r.iface = iface_;
        r.iface.read_inputs = attribs_;
        r.iface.read_inputs.insert(flatAttribs_.begin(), flatAttribs_.end());
        const std::string allocated = Allocate(body.str());
        r.assembly = Header() + allocated + "END\n";
        return r;
    }

private:
    const Options& options_;
    std::vector<Inst> insts_;
    std::map<std::uint32_t, Type> types_;
    std::map<std::uint32_t, std::uint32_t> typeOf_;             // result id -> type id
    std::map<std::uint32_t, std::string> names_;
    std::map<std::uint32_t, std::map<std::uint32_t, std::string>> memberNames_;
    std::map<std::uint32_t, std::map<std::uint32_t, std::vector<std::uint32_t>>> decor_;           // id -> dec -> args
    std::map<std::uint32_t, std::map<std::uint32_t, std::map<std::uint32_t, std::vector<std::uint32_t>>>> memberDecor_;
    std::map<std::uint32_t, std::vector<std::uint32_t>> constants_;  // id -> words (scalars) or constituents
    std::map<std::uint32_t, bool> constantIsComposite_;
    std::map<std::uint32_t, std::uint32_t> varStorage_;             // variable id -> storage class
    std::uint32_t glslStd_ = 0;
    Stage stage_ = Stage::Fragment;
    std::uint32_t entry_ = 0;
    // The function's blocks: label -> instruction range, and the order they appear.
    struct BlockRange { std::size_t first = 0, last = 0; };    // [first, last] includes the terminator
    std::map<std::uint32_t, BlockRange> blocks_;
    std::vector<std::uint32_t> blockOrder_;
    std::map<std::uint32_t, std::size_t> defAt_;                // result id -> instruction index

    Interface iface_;
    std::map<std::uint32_t, int> blockSlot_;                    // uniform variable -> buffer slot
    std::map<std::uint32_t, int> storageSlot_;                  // structured buffer variable -> storage slot
    std::set<int> usedStorage_;
    std::map<std::uint32_t, std::size_t> blockIndex_;           // uniform variable -> iface_.blocks index
    std::map<std::uint32_t, int> textureSlot_, samplerSlot_;    // variable -> slot
    std::map<std::uint32_t, int> inputLoc_, outputLoc_;         // variable -> location
    std::map<std::uint32_t, std::string> inputBuiltin_, outputBuiltin_;

    std::map<std::uint32_t, Val> vals_;
    std::map<std::uint32_t, Val> locals_;
    std::set<std::uint32_t> droppedClip_;
    struct IoChain { std::uint32_t base = 0; std::vector<std::uint32_t> idx; };
    std::map<std::uint32_t, IoChain> ioChains_;
    // Pointers into a function variable: the variable, and the indices (an element or column, then a component).
    struct LocalChain { std::uint32_t base = 0; std::vector<std::uint32_t> idx; };
    std::map<std::uint32_t, LocalChain> localChains_;
    // A geometry program: its primitive types, output vertex count and invocations (execution modes), and the values
    // of inputs that are arrays per vertex of arrays (each vertex's element, a value of several columns).
    std::string gsIn_ = "TRIANGLES", gsOut_ = "TRIANGLE_STRIP";
    unsigned gsVertices_ = 0, gsInvocations_ = 1;
    std::map<std::uint32_t, std::vector<Val>> gsNested_;                 // pointer -> input or output variable and indices                       // pointers into gl_ClipDistance                       // Function/Private variables held in temporaries
    std::set<std::string> temps_;
    unsigned scratch_ = 0;
    std::ostream* out_ = nullptr;
    bool usesHandles_ = false;
    std::set<int> usedBuffers_;
    std::set<int> attribs_, flatAttribs_, noPerspectiveLocs_;
    int layerLocation_ = -1;                                    // the layer's varying (hlslcc_layer_index), -1: none
    std::set<int> colorOutputs_;
    bool usesDepthOut_ = false;

    // ----- parsing -----
    void Parse(const std::vector<std::uint32_t>& words) {
        if (words.size() < 5 || words[0] != 0x07230203) throw Error("not SPIR-V");
        for (std::size_t at = 5; at < words.size();) {
            const std::uint32_t head = words[at];
            const std::uint16_t count = std::uint16_t(head >> 16), op = std::uint16_t(head & 0xffff);
            if (!count || at + count > words.size()) throw Error("truncated instruction");
            Inst in; in.op = op; in.w.assign(words.begin() + at + 1, words.begin() + at + count);
            insts_.push_back(std::move(in));
            at += count;
        }
    }
    static std::string Str(const std::vector<std::uint32_t>& w, std::size_t from) {
        std::string s;
        for (std::size_t i = from; i < w.size(); ++i)
            for (int b = 0; b < 4; ++b) { char c = char((w[i] >> (8 * b)) & 0xff); if (!c) return s; s += c; }
        return s;
    }

    void Collect() {
        bool inFunction = false;
        std::uint32_t current = 0;
        for (std::size_t i = 0; i < insts_.size(); ++i) {
            const auto& in = insts_[i];
            const auto& w = in.w;
            switch (in.op) {
            case OpName: names_[w[0]] = Str(w, 1); break;
            case OpMemberName: memberNames_[w[0]][w[1]] = Str(w, 2); break;
            case OpExtInstImport: if (Str(w, 1) == "GLSL.std.450") glslStd_ = w[0]; break;
            case OpEntryPoint:
                entry_ = w[1];
                stage_ = w[0] == 4 ? Stage::Fragment : w[0] == 0 ? Stage::Vertex : w[0] == 3 ? Stage::Geometry
                       : throw Error("unsupported execution model " + std::to_string(w[0]));
                break;
            case OpExecutionMode:
                switch (w[1]) {
                case 0: gsInvocations_ = w[2]; break;                   // Invocations
                case 19: gsIn_ = "POINTS"; break;
                case 20: gsIn_ = "LINES"; break;
                case 21: gsIn_ = "LINES_ADJACENCY"; break;
                case 22: gsIn_ = "TRIANGLES"; break;
                case 23: gsIn_ = "TRIANGLES_ADJACENCY"; break;
                case 26: gsVertices_ = w[2]; break;                     // OutputVertices
                case 27: gsOut_ = "POINTS"; break;
                case 28: gsOut_ = "LINE_STRIP"; break;
                case 29: gsOut_ = "TRIANGLE_STRIP"; break;
                default: break;
                }
                break;
            case OpDecorate: decor_[w[0]][w[1]] = std::vector<std::uint32_t>(w.begin() + 2, w.end()); break;
            case OpMemberDecorate: memberDecor_[w[0]][w[1]][w[2]] = std::vector<std::uint32_t>(w.begin() + 3, w.end()); break;
            case OpTypeVoid: types_[w[0]] = {}; break;
            case OpTypeBool: { Type t; t.k = K::Bool; types_[w[0]] = t; break; }
            case OpTypeInt: { Type t; t.k = K::Int; t.width = w[1]; t.sign = w[2] != 0; types_[w[0]] = t; break; }
            case OpTypeFloat: { Type t; t.k = K::Float; t.width = w[1]; types_[w[0]] = t; break; }
            case OpTypeVector: { Type t; t.k = K::Vector; t.elem = w[1]; t.count = w[2]; types_[w[0]] = t; break; }
            case OpTypeMatrix: { Type t; t.k = K::Matrix; t.elem = w[1]; t.count = w[2]; types_[w[0]] = t; break; }
            case OpTypeImage: { Type t; t.k = K::Image; t.elem = w[1]; t.dim = w[2]; t.depth = w[3] == 1; t.arrayed = w[4] != 0; t.ms = w[5] != 0; types_[w[0]] = t; break; }
            case OpTypeSampler: { Type t; t.k = K::Sampler; types_[w[0]] = t; break; }
            case OpTypeSampledImage: { Type t; t.k = K::SampledImage; t.elem = w[1]; types_[w[0]] = t; break; }
            case OpTypeArray: { Type t; t.k = K::Array; t.elem = w[1]; t.count = constants_.count(w[2]) ? constants_[w[2]][0] : 0; types_[w[0]] = t; break; }
            case OpTypeRuntimeArray: { Type t; t.k = K::Array; t.elem = w[1]; types_[w[0]] = t; break; }
            case OpTypeStruct: { Type t; t.k = K::Struct; t.members.assign(w.begin() + 1, w.end()); types_[w[0]] = t; break; }
            case OpTypePointer: { Type t; t.k = K::Pointer; t.storage = w[1]; t.elem = w[2]; types_[w[0]] = t; break; }
            case OpTypeFunction: { Type t; t.k = K::Function; types_[w[0]] = t; break; }
            case OpConstantTrue: typeOf_[w[1]] = w[0]; constants_[w[1]] = {1}; break;
            case OpConstantFalse: typeOf_[w[1]] = w[0]; constants_[w[1]] = {0}; break;
            case OpConstant: typeOf_[w[1]] = w[0]; constants_[w[1]] = std::vector<std::uint32_t>(w.begin() + 2, w.end()); break;
            case OpConstantComposite: typeOf_[w[1]] = w[0]; constants_[w[1]] = std::vector<std::uint32_t>(w.begin() + 2, w.end()); constantIsComposite_[w[1]] = true; break;
            case OpConstantNull: typeOf_[w[1]] = w[0]; constants_[w[1]] = {}; constantIsComposite_[w[1]] = types_[w[0]].k != K::Bool && types_[w[0]].k != K::Int && types_[w[0]].k != K::Float; break;
            case OpUndef: typeOf_[w[1]] = w[0]; if (!inFunction) constants_[w[1]] = {}; break;
            case OpVariable: typeOf_[w[1]] = w[0]; varStorage_[w[1]] = w[2]; defAt_[w[1]] = i; break;
            case OpFunction: inFunction = true; if (w[1] != entry_) throw Error("more than one function"); break;
            case OpFunctionEnd: inFunction = false; break;
            case OpLabel:
                current = w[0]; blocks_[current].first = i; blockOrder_.push_back(current); break;
            case OpBranch: case OpBranchConditional: case OpSwitch: case OpReturn: case OpReturnValue:
            case OpKill: case OpUnreachable:
                if (inFunction) blocks_[current].last = i;
                break;
            default:
                if (inFunction && w.size() >= 2 && HasResult(in.op)) { typeOf_[w[1]] = w[0]; defAt_[w[1]] = i; }
                break;
            }
        }
        if (!entry_) throw Error("no entry point");
        iface_.stage = stage_;
        // Uniform blocks: in binding order, unless a slot is named.
        std::vector<std::uint32_t> blockVars, storageVars;
        for (const auto& [id, storage] : varStorage_) {
            const auto& ptr = types_[typeOf_[id]];
            if ((storage == StorageUniform || storage == StorageStorageBuffer) && types_[ptr.elem].k == K::Struct) {
                if (storage == StorageStorageBuffer || Has(ptr.elem, DecBufferBlock)) storageVars.push_back(id);
                else blockVars.push_back(id);
            }
        }
        std::sort(storageVars.begin(), storageVars.end(), [&](std::uint32_t a, std::uint32_t b) { return Dec(a, DecBinding) < Dec(b, DecBinding); });
        for (auto id : storageVars) {
            storageSlot_[id] = int(iface_.storage.size());
            iface_.storage.push_back({BlockName(id), Dec(id, DecDescriptorSet), Dec(id, DecBinding), storageSlot_[id], "storage"});
        }
        // Loose parameters ($Globals) after the struct buffers: the game's programs have View in slot 0.
        std::sort(blockVars.begin(), blockVars.end(), [&](std::uint32_t a, std::uint32_t b) {
            const bool ga = BlockName(a) == "$Globals", gb = BlockName(b) == "$Globals";
            if (ga != gb) return gb;
            return Dec(a, DecBinding) < Dec(b, DecBinding);
        });
        std::set<int> taken;
        for (auto id : blockVars) { auto it = options_.block_slots.find(BlockName(id)); if (it != options_.block_slots.end()) taken.insert(it->second); }
        int next = 0;
        for (auto id : blockVars) {
            Block b; b.name = BlockName(id); b.set = Dec(id, DecDescriptorSet); b.binding = Dec(id, DecBinding);
            auto it = options_.block_slots.find(b.name);
            if (it != options_.block_slots.end()) b.slot = it->second;
            else { while (taken.count(next) || next == 14) ++next; b.slot = next++; }
            const auto structType = types_[typeOf_[id]].elem;
            b.size = LayoutSize(structType, 0);
            for (std::uint32_t m = 0; m < types_[structType].members.size(); ++m)
                b.members.push_back({memberNames_[structType][m], MemberDec(structType, m, DecOffset), MemberSize(structType, m)});
            blockSlot_[id] = b.slot; blockIndex_[id] = iface_.blocks.size();
            iface_.blocks.push_back(b);
        }
        if (options_.split_loose_integers)
            for (auto id : blockVars)
                if (BlockName(id) == "$Globals") SplitLoose(id);
        // Textures and samplers: slots in binding order.
        std::vector<std::uint32_t> texVars, sampVars;
        for (const auto& [id, storage] : varStorage_) {
            if (storage != StorageUniformConstant) continue;
            const auto k = types_[types_[typeOf_[id]].elem].k;
            if (k == K::Image) texVars.push_back(id);
            else if (k == K::Sampler) sampVars.push_back(id);
            else if (k == K::SampledImage) throw Error("combined image samplers in the module are not expected from DXC");
        }
        auto byBinding = [&](std::uint32_t a, std::uint32_t b) { return Dec(a, DecBinding) < Dec(b, DecBinding); };
        std::sort(texVars.begin(), texVars.end(), byBinding);
        std::sort(sampVars.begin(), sampVars.end(), byBinding);
        if (options_.resources_by_first_use) {
            // In the order the code first loads each (unused ones last, in binding order), as HLSLcc numbers them.
            std::map<std::uint32_t, std::size_t> firstUse;
            for (std::size_t i = 0; i < insts_.size(); ++i)
                if (insts_[i].op == OpLoad && insts_[i].w.size() >= 3) firstUse.emplace(insts_[i].w[2], i);
            auto byUse = [&](std::uint32_t a, std::uint32_t b) {
                const auto ia = firstUse.count(a) ? firstUse[a] : SIZE_MAX, ib = firstUse.count(b) ? firstUse[b] : SIZE_MAX;
                return ia != ib ? ia < ib : Dec(a, DecBinding) < Dec(b, DecBinding);
            };
            std::stable_sort(texVars.begin(), texVars.end(), byUse);
            std::stable_sort(sampVars.begin(), sampVars.end(), byUse);
            // Loose ones (not a struct's member, "<Struct>_<member>") come first: textures in the reverse of their
            // declaration, samplers in it (the game's GPU sprite program: its curve, attributes, velocity and position
            // textures and its particle indices in slots 1 to 5, their samplers from 0 in declaration order).
            auto loose = [&](std::uint32_t id) { return names_[id].find('_') == std::string::npos; };
            auto front = [&](std::vector<std::uint32_t>& vars, bool reverse) {
                std::vector<std::uint32_t> first, rest;
                for (auto id : vars) (loose(id) ? first : rest).push_back(id);
                std::sort(first.begin(), first.end(), byBinding);
                if (reverse) std::reverse(first.begin(), first.end());
                first.insert(first.end(), rest.begin(), rest.end());
                vars = first;
            };
            front(texVars, true);
            front(sampVars, false);
        }
        for (auto id : texVars) { textureSlot_[id] = int(iface_.textures.size()); iface_.textures.push_back({names_[id], Dec(id, DecDescriptorSet), Dec(id, DecBinding), textureSlot_[id], Target(types_[typeOf_[id]].elem)}); }
        for (auto id : sampVars) { samplerSlot_[id] = int(iface_.samplers.size()); iface_.samplers.push_back({names_[id], Dec(id, DecDescriptorSet), Dec(id, DecBinding), samplerSlot_[id], "sampler"}); }
        // Inputs and outputs.
        for (const auto& [id, storage] : varStorage_) {
            if (storage != StorageInput && storage != StorageOutput) continue;
            IoVar v; v.name = names_[id];
            const Type* pointeePtr = &types_[types_[typeOf_[id]].elem];
            // A geometry program's input is an array per vertex: its varying is the vertex's element.
            if (stage_ == Stage::Geometry && storage == StorageInput && pointeePtr->k == K::Array) pointeePtr = &types_[pointeePtr->elem];
            if (pointeePtr->k == K::Array && pointeePtr->count) { v.count = pointeePtr->count; pointeePtr = &types_[pointeePtr->elem]; }
            const auto& pointee = *pointeePtr;
            v.components = pointee.k == K::Vector ? pointee.count : 1;
            const auto& scalar = pointee.k == K::Vector ? types_[pointee.elem] : pointee;
            v.integer = scalar.k == K::Int;
            v.flat = Has(id, DecFlat); v.centroid = Has(id, DecCentroid); v.noperspective = Has(id, DecNoPerspective);
            if (storage == StorageInput && v.noperspective && !Has(id, DecBuiltIn)) noPerspectiveLocs_.insert(int(Dec(id, DecLocation)));
            if (Has(id, DecBuiltIn)) v.builtin = std::to_string(Dec(id, DecBuiltIn));
            else v.location = int(Dec(id, DecLocation));
            // A vertex program's ATTRIBUTEn input is vertex attribute n (DXC numbers inputs in declaration order).
            int attribute = -1;
            if (options_.vertex_attribute_semantics && stage_ == Stage::Vertex && storage == StorageInput && v.builtin.empty() &&
                std::sscanf(v.name.c_str(), "in.var.ATTRIBUTE%d", &attribute) == 1 && attribute >= 0)
                v.location = attribute;
            if (storage == StorageInput) { iface_.inputs.push_back(v); if (v.builtin.empty()) inputLoc_[id] = v.location; else inputBuiltin_[id] = v.builtin; }
            else { iface_.outputs.push_back(v); if (v.builtin.empty()) outputLoc_[id] = v.location; else outputBuiltin_[id] = v.builtin; }
        }
        // The layer (SV_RenderTargetArrayIndex), as Nintendo's compiler (HLSLCC) passes it: a geometry program writes it
        // to the layer and to a flat varying hlslcc_layer_index after its others; a pixel program reads that varying.
        auto layerVarying = [&](std::vector<IoVar>& list, const char* name) {
            int next = 0;
            bool layer = false;
            for (const auto& v : list) {
                if (v.builtin.empty()) next = std::max(next, v.location + int(v.count));
                else if (std::stoul(v.builtin) == BuiltInLayer) layer = true;
            }
            if (!layer) return -1;
            IoVar l; l.name = name; l.location = next; l.components = 1; l.flat = true; l.integer = true;
            list.push_back(l);
            return next;
        };
        if (stage_ == Stage::Fragment) layerLocation_ = layerVarying(iface_.inputs, "in.var.HLSLCC_LAYER_INDEX");
        if (stage_ == Stage::Geometry) layerLocation_ = layerVarying(iface_.outputs, "out.var.HLSLCC_LAYER_INDEX");
        {
        }
    }
    static bool HasResult(std::uint16_t op) {
        switch (op) {
        case OpStore: case OpSelectionMerge: case OpLoopMerge: case OpBranch: case OpBranchConditional: case OpSwitch:
        case OpReturn: case OpReturnValue: case OpKill: case OpUnreachable: case OpEmitVertex: case OpEndPrimitive:
        case OpLine: case OpNoLine: case OpLabel: case OpFunctionEnd: case OpDecorate: case OpMemberDecorate:
        case OpDecorateString: case OpMemberDecorateString:
            return false;
        default: return true;
        }
    }
    std::uint32_t Dec(std::uint32_t id, std::uint32_t d) const {
        auto a = decor_.find(id); if (a == decor_.end()) return 0;
        auto b = a->second.find(d); return b == a->second.end() || b->second.empty() ? 0 : b->second[0];
    }
    bool Has(std::uint32_t id, std::uint32_t d) const { auto a = decor_.find(id); return a != decor_.end() && a->second.count(d); }
    // The bytes a value of type t spans in a block: matrices by their member's MatrixStride, arrays by ArrayStride
    // (the last element unpadded), structs to the end of their last member.
    std::uint32_t LayoutSize(std::uint32_t t, std::uint32_t matrixStride) const {
        const auto it = types_.find(t);
        if (it == types_.end()) return 0;
        const Type& ty = it->second;
        switch (ty.k) {
        case K::Bool: return 4;
        case K::Int: case K::Float: return ty.width / 8;
        case K::Vector: return ty.count * LayoutSize(ty.elem, 0);
        case K::Matrix: return ty.count ? (ty.count - 1) * (matrixStride ? matrixStride : 16) + LayoutSize(ty.elem, 0) : 0;
        case K::Array: {
            if (!ty.count) return 0;
            const std::uint32_t stride = Dec(t, DecArrayStride);
            return (ty.count - 1) * stride + LayoutSize(ty.elem, matrixStride);
        }
        case K::Struct: {
            std::uint32_t end = 0;
            for (std::uint32_t m = 0; m < ty.members.size(); ++m) end = std::max(end, MemberDec(t, m, DecOffset) + MemberSize(t, m));
            return end;
        }
        default: return 0;
        }
    }
    std::uint32_t MemberSize(std::uint32_t s, std::uint32_t m) const {
        const auto it = types_.find(s);
        if (it == types_.end() || m >= it->second.members.size()) return 0;
        return LayoutSize(it->second.members[m], MemberDec(s, m, DecMatrixStride));
    }
    std::uint32_t MemberDec(std::uint32_t s, std::uint32_t m, std::uint32_t d) const {
        auto a = memberDecor_.find(s); if (a == memberDecor_.end()) return 0;
        auto b = a->second.find(m); if (b == a->second.end()) return 0;
        auto c = b->second.find(d); return c == b->second.end() || c->second.empty() ? 0 : c->second[0];
    }
    bool MemberHas(std::uint32_t s, std::uint32_t m, std::uint32_t d) const {
        auto a = memberDecor_.find(s); if (a == memberDecor_.end()) return false;
        auto b = a->second.find(m); return b != a->second.end() && b->second.count(d);
    }
    // The loose parameters as the game's compiler packs them: each member at the start of a 16-byte row of its own,
    // in declaration order, the integer ones in a block of their own ("$Globals.u") in the loose slot and the others
    // ("$Globals") in the next slot (every loose parameter of the game's programs starts a row). The game's shadow
    // shader puts its C4ShadowDepthBias before the vertex factory's floats; DXC declares that parameter after them.
    std::map<std::uint32_t, std::pair<int, std::uint32_t>> loosePlace_;   // $Globals member -> (slot, offset)
    std::uint32_t looseVar_ = 0;
    void SplitLoose(std::uint32_t var) {
        const std::size_t at = blockIndex_[var];
        const auto structType = types_[typeOf_[var]].elem;
        const auto& members = types_[structType].members;
        std::vector<std::uint32_t> ints, floats;
        for (std::uint32_t m = 0; m < members.size(); ++m) (ScalarKind(members[m]) == 'F' ? floats : ints).push_back(m);
        auto byOffset = [&](std::uint32_t a, std::uint32_t b) { return MemberDec(structType, a, DecOffset) < MemberDec(structType, b, DecOffset); };
        std::sort(ints.begin(), ints.end(), byOffset);
        std::sort(floats.begin(), floats.end(), byOffset);
        // TC4ShadowDepthVS<..., 1>: the game reads the bias from float row 0 and the instancing parameters from
        // row 1 onward. Keeping DXC's order instead reads the bias at byte 112 and shifts all seven factory rows.
        // Other factories have only the bias float row, and the point-light mode does not read the bias at all.
        std::stable_partition(floats.begin(), floats.end(), [&](std::uint32_t m) {
            return memberNames_[structType][m] == "C4ShadowDepthBias";
        });
        auto pack = [&](const std::vector<std::uint32_t>& list, Block& b) {
            std::uint32_t offset = 0;
            b.members.clear();
            for (auto m : list) {
                const std::uint32_t size = MemberSize(structType, m);
                offset = (offset + 15) / 16 * 16;
                loosePlace_[m] = {b.slot, offset};
                b.members.push_back({memberNames_[structType][m], offset, size});
                offset += size;
            }
            b.size = offset;
        };
        looseVar_ = var;
        if (ints.empty()) { pack(floats, iface_.blocks[at]); return; }
        const int slot = iface_.blocks[at].slot;
        int next = slot + 1;
        while (next == 14) ++next;
        Block u = iface_.blocks[at];
        u.name = "$Globals.u";
        pack(ints, u);
        if (floats.empty()) { iface_.blocks[at] = u; return; }
        iface_.blocks[at].slot = next;
        pack(floats, iface_.blocks[at]);
        iface_.blocks.insert(iface_.blocks.begin() + std::ptrdiff_t(at), u);
        for (auto& [other, index] : blockIndex_) if (index >= at) ++index;
    }

    std::string BlockName(std::uint32_t var) {
        // UE names the variable after the block ("View"); the type is "type.View".
        auto n = names_[var];
        if (!n.empty()) return n;
        auto t = names_[types_[typeOf_[var]].elem];
        return t.rfind("type.", 0) == 0 ? t.substr(5) : t;
    }
    std::string Target(std::uint32_t imageType) {
        const auto& t = types_[imageType];
        // Dim: 0 1D, 1 2D, 2 3D, 3 Cube, 5 Buffer.
        std::string base = t.dim == 0 ? "1D" : t.dim == 1 ? "2D" : t.dim == 2 ? "3D" : t.dim == 3 ? "CUBE" : t.dim == 5 ? "BUFFER" : "";
        if (base.empty() || t.ms) throw Error("unsupported image dimension");
        if (t.arrayed) base = (t.dim == 3 ? std::string("ARRAYCUBE") : "ARRAY" + base);
        return base;
    }

    // The register an input or output chain names (fragment.attrib[n], result.attrib[n], fragment.position) and the
    // vector component it selects (-1: the whole vector).
    std::pair<std::string, int> IoChainTarget(const IoChain& c, bool input) {
        const Type& pointee = types_[types_[typeOf_[c.base]].elem];
        std::size_t at = 0;
        int element = 0;
        if (pointee.k == K::Array) element = int(c.idx.at(at++));
        const int component = at < c.idx.size() ? int(c.idx[at++]) : -1;
        if (at != c.idx.size()) throw Error("a nested index into an input or output");
        std::string reg;
        if (input && stage_ == Stage::Geometry && inputLoc_.count(c.base)) {
            // vertex[v].attrib[location + element]: the first index is the vertex; an input that is an array per
            // vertex takes the next one as its element.
            if (pointee.k != K::Array || c.idx.empty()) throw Error("a geometry input that is not an array per vertex");
            const auto& perVertex = types_[pointee.elem];
            std::size_t g = 1;
            int inner = 0;
            if (perVertex.k == K::Array) { if (g >= c.idx.size()) throw Error("a whole array element of a geometry input"); inner = int(c.idx[g++]); }
            const int comp = g < c.idx.size() ? int(c.idx[g++]) : -1;
            if (g != c.idx.size()) throw Error("a nested index into a geometry input");
            return {"vertex[" + std::to_string(c.idx[0]) + "].attrib[" + std::to_string(inputLoc_[c.base] + inner) + "]", comp};
        }
        if (input && inputLoc_.count(c.base)) {
            const int loc = inputLoc_[c.base] + element;
            (Has(c.base, DecFlat) ? flatAttribs_ : attribs_).insert(loc);
            reg = std::string(stage_ == Stage::Vertex ? "vertex" : "fragment") + ".attrib[" + std::to_string(loc) + "]";
        } else if (input && inputBuiltin_.count(c.base) && std::stoul(inputBuiltin_[c.base]) == BuiltInFragCoord) {
            reg = "fragment.position";
        } else if (!input && outputLoc_.count(c.base) && stage_ != Stage::Fragment) {
            reg = "result.attrib[" + std::to_string(outputLoc_[c.base] + element) + "]";
        } else if (!input && outputBuiltin_.count(c.base) && std::stoul(outputBuiltin_[c.base]) == BuiltInPosition) {
            reg = "result.position";
        } else if (!input && outputLoc_.count(c.base)) {
            colorOutputs_.insert(outputLoc_[c.base]);
            reg = "result_color" + std::to_string(outputLoc_[c.base]);
        } else throw Error("an access chain into an input or output this front end does not read or write");
        return {reg, component};
    }

    // ----- types and values -----
    const Type& TypeOfId(std::uint32_t id) { return types_[typeOf_.at(id)]; }
    // A value held as one register per column (matrices) or element (arrays of scalars and vectors).
    bool Columned(const Type& t) {
        if (t.k == K::Matrix) return true;
        if (t.k != K::Array || !t.count) return false;
        const auto k = types_[t.elem].k;
        return k == K::Vector || k == K::Float || k == K::Int || k == K::Bool;
    }
    unsigned Components(std::uint32_t typeId) {
        const auto& t = types_[typeId];
        if (t.k == K::Vector) return t.count;
        if (t.k == K::Bool || t.k == K::Int || t.k == K::Float) return 1;
        if (t.k == K::Matrix) return types_[t.elem].count;
        if (t.k == K::Array && t.count) return Components(t.elem);
        throw Error("a value of a type without components");
    }
    // The scalar kind of a value: 'F' float (bools are floats too), 'S' signed, 'U' unsigned.
    char ScalarKind(std::uint32_t typeId) {
        const auto* t = &types_[typeId];
        while (t->k == K::Array) t = &types_[t->elem];
        if (t->k == K::Vector || t->k == K::Matrix) t = &types_[t->elem];
        if (t->k == K::Matrix) t = &types_[types_[t->elem].elem];
        if (t->k == K::Int) return t->sign ? 'S' : 'U';
        return 'F';
    }
    std::string New(unsigned n = 4) { (void)n; std::string r = "V" + std::to_string(++scratch_); temps_.insert(r); return r; }
    void Emit(const std::string& line) { *out_ << line << ";\n"; }

    Val Constant(std::uint32_t id) {
        const auto typeId = typeOf_.at(id);
        const auto& t = types_[typeId];
        auto scalar = [&](std::uint32_t sid, std::uint32_t stype) -> std::string {
            const auto& st = types_[stype];
            const auto it = constants_.find(sid);
            std::uint32_t bits = it == constants_.end() || it->second.empty() ? 0 : it->second[0];
            if (st.k == K::Float) { float f; std::memcpy(&f, &bits, 4); return Fmt(f); }
            if (st.k == K::Bool) return bits ? "1" : "0";
            if (st.sign) return std::to_string(std::int32_t(bits));
            return std::to_string(bits);
        };
        Val v; v.imm = true;
        if (t.k == K::Bool || t.k == K::Int || t.k == K::Float) {
            v.n = 1; v.reg = "{" + scalar(id, typeId) + ", 0, 0, 0}";
            return v;
        }
        if (t.k == K::Vector) {
            v.n = t.count;
            std::string s = "{";
            const auto& parts = constants_[id];
            for (unsigned i = 0; i < 4; ++i) {
                if (i) s += ", ";
                if (i < t.count) s += parts.empty() ? (types_[t.elem].k == K::Float ? "0" : "0") : scalar(parts[i], t.elem);
                else s += "0";
            }
            v.reg = s + "}";
            return v;
        }
        if (t.k == K::Array && Columned(t)) {
            // An array of scalars or vectors: one immediate per element (all zero for a null constant).
            v.n = Components(typeId);
            const auto& parts = constants_[id];
            for (unsigned c = 0; c < t.count; ++c) v.cols.push_back(parts.empty() ? std::string("{0, 0, 0, 0}") : Constant(parts[c]).reg);
            v.reg = v.cols[0];
            return v;
        }
        if (t.k == K::Matrix) {
            v.n = types_[t.elem].count;
            const auto& parts = constants_[id];
            for (unsigned c = 0; c < t.count; ++c) {
                if (parts.empty()) { v.cols.push_back("{0, 0, 0, 0}"); continue; }
                v.cols.push_back(Constant(parts[c]).reg);
            }
            v.reg = v.cols[0];
            return v;
        }
        throw Error("a constant of an unsupported type (kind " + std::to_string(int(t.k)) + ")");
    }
    Val Get(std::uint32_t id) {
        auto it = vals_.find(id);
        if (it != vals_.end()) {
            if (typeOf_.count(id) && types_[typeOf_[id]].k == K::Matrix && it->second.cols.empty())
                throw Error("a matrix value %" + std::to_string(id) + " that is not held as columns");
            return it->second;
        }
        if (constants_.count(id)) return Constant(id);
        throw Error("value %" + std::to_string(id) + " used before it is defined");
    }
    // An operand: the value's register with a swizzle. Immediates and registers share the syntax; a scalar used in a
    // vector operation is replicated (".x").
    std::string Src(const Val& v, unsigned want, bool negate = false) {
        std::string s = (negate ? "-" : "") + v.reg;
        if (v.n == 1 && want > 1) return s + ".x";
        if (v.n == 1) return s + ".x";
        return s;
    }
    std::string SrcComp(const Val& v, unsigned c) { return v.reg + "." + Comp(v.n == 1 ? 0 : c); }
    Val Def(std::uint32_t id, unsigned n) { Val v; v.reg = New(); v.n = n; vals_[id] = v; return v; }
    std::string Dst(const Val& v) { return v.reg + Mask(v.n); }

    // Float to bool and back: comparisons write 1.0 / 0.0.
    void Compare(const char* op, char kind, std::uint32_t id, const Val& a, const Val& b) {
        auto n = std::max(a.n, b.n);
        auto d = Def(id, n);
        if (kind == 'F') { Emit(std::string(op) + ".F " + Dst(d) + ", " + Src(a, n) + ", " + Src(b, n)); return; }
        // An integer comparison writes ~0 or 0: as a float bool, 1.0 or 0.0.
        Emit(std::string(op) + "." + kind + " " + Dst(d) + ", " + Src(a, n) + ", " + Src(b, n));
        Emit("I2F.S " + Dst(d) + ", -" + d.reg);
    }
    void SetCondition(const Val& c) {
        // The condition code from a float bool, lane by lane.
        auto t = New();
        Emit("TRUNC.U.CC " + t + Mask(c.n) + ", " + Src(c, c.n));
    }

    // ----- uniform block reads -----
    struct Address { int slot = 0; std::uint32_t offset = 0; std::string dyn; std::uint32_t type = 0; std::uint32_t block = 0; std::string member; std::uint32_t structType = 0, memberIndex = 0; bool storage = false; };
    std::map<std::uint32_t, Address> pointers_;

    Address Chain(std::uint32_t base, const std::vector<std::uint32_t>& idx) {
        Address a;
        if (pointers_.count(base)) a = pointers_[base];
        else if (storageSlot_.count(base)) {
            a.slot = storageSlot_[base]; a.type = types_[typeOf_[base]].elem; a.block = base; a.storage = true;
            a.member = "-";                 // structured buffers keep UE's layout (no remap)
        }
        else {
            if (!blockSlot_.count(base)) throw Error("an access chain into a variable that is not a uniform block");
            a.slot = blockSlot_[base]; a.type = types_[typeOf_[base]].elem; a.block = base;
        }
        for (auto ix : idx) {
            const auto& t = types_[a.type];
            if (t.k == K::Struct) {
                const auto m = constants_.at(ix)[0];
                std::uint32_t off = MemberDec(a.type, m, DecOffset);
                if (a.structType == 0 && a.member.empty()) {
                    // A member of the block itself: the game's layout may place it elsewhere.
                    const auto& blockName = iface_.blocks[blockIndex_[a.block]].name;
                    const auto memberName = memberNames_[a.type][m];
                    if (options_.remap) if (auto moved = options_.remap(blockName, memberName, off)) off = *moved;
                    if (a.block == looseVar_ && loosePlace_.count(m)) { a.slot = loosePlace_[m].first; off = loosePlace_[m].second; }
                    a.member = memberName;
                }
                a.structType = a.type; a.memberIndex = m;
                a.offset += off; a.type = t.members[m];
            } else if (t.k == K::Array) {
                const std::uint32_t stride = a.structType ? MemberDec(a.structType, a.memberIndex, DecArrayStride) : 0;
                std::uint32_t arrayStride = Dec(TypeIdOfArray(a), DecArrayStride);
                const std::uint32_t s = arrayStride ? arrayStride : stride ? stride : 16;
                if (constants_.count(ix)) a.offset += constants_[ix][0] * s;
                else {
                    const auto v = Get(ix);
                    const auto r = New();
                    Emit("MUL.S " + r + ".x, " + Src(v, 1) + ", {" + std::to_string(s) + ", 0, 0, 0}.x");
                    if (!a.dyn.empty()) Emit("ADD.S " + r + ".x, " + r + ".x, " + a.dyn);
                    a.dyn = r + ".x";
                }
                a.type = t.elem;
            } else if (t.k == K::Vector || t.k == K::Matrix) {
                // A component of a vector (4 bytes apart) or a column of a matrix (16 bytes apart).
                const std::uint32_t s = t.k == K::Vector ? 4 : 16;
                if (constants_.count(ix)) a.offset += constants_[ix][0] * s;
                else {
                    const auto v = Get(ix);
                    const auto r = New();
                    Emit("MUL.S " + r + ".x, " + Src(v, 1) + ", {" + std::to_string(s) + ", 0, 0, 0}.x");
                    if (!a.dyn.empty()) Emit("ADD.S " + r + ".x, " + r + ".x, " + a.dyn);
                    a.dyn = r + ".x";
                }
                a.type = t.elem;
            } else throw Error("an access chain step into an unsupported type");
        }
        return a;
    }
    std::uint32_t TypeIdOfArray(const Address& a) { return a.type; }

    std::string Location(const Address& a, std::uint32_t extra = 0) {
        std::string at = (a.storage ? "sbo_buf" : "buf") + std::to_string(a.slot) + "[";
        if (!a.dyn.empty()) at += a.dyn + " + ";
        at += std::to_string(a.offset + extra) + "]";
        (a.storage ? usedStorage_ : usedBuffers_).insert(a.slot);
        return at;
    }
    Val LoadUniform(std::uint32_t id, const Address& a) {
        const auto& t = types_[a.type];
        const char kind = ScalarKind(a.type);
        const std::string k = kind == 'F' ? "F32" : kind == 'S' ? "S32" : "U32";
        if (t.k == K::Matrix) {
            const bool rowMajor = a.structType && MemberHas(a.structType, a.memberIndex, DecRowMajor);
            if (rowMajor) throw Error("a row-major matrix in a uniform block");
            const unsigned rows = types_[t.elem].count;
            Val v; v.n = rows;
            for (unsigned c = 0; c < t.count; ++c) {
                auto r = New();
                Emit((a.storage ? "LDB." : "LDC.") + k + (rows == 1 ? "" : rows == 2 ? "X2" : "X4") + " " + r + Mask(rows) + ", " + Location(a, 16 * c));
                v.cols.push_back(r);
            }
            v.reg = v.cols[0];
            vals_[id] = v;
            return v;
        }
        if (t.k == K::Array || t.k == K::Struct) throw Error("a whole array or struct loaded from a uniform block");
        const unsigned n = Components(a.type);
        auto d = Def(id, n);
        Emit((a.storage ? "LDB." : "LDC.") + k + (n == 1 ? "" : n == 2 ? "X2" : "X4") + " " + Dst(d) + ", " + Location(a));
        return d;
    }

    // ----- texture handles -----
    std::map<std::uint32_t, std::pair<std::uint32_t, std::uint32_t>> sampledImages_;   // result -> (image load, sampler load)
    std::map<std::uint32_t, std::uint32_t> loadedFrom_;                                  // loaded value -> variable
    std::string Handle(std::uint32_t imageVar, std::uint32_t samplerVar) {
        usesHandles_ = true;
        const int t = textureSlot_.at(imageVar);
        Emit("LDC.U64 D0.x, buf14[" + std::to_string(328 + 8 * t) + "]");
        // A read without a sampler of its own (texel fetches, size queries) takes the default sampler, the slot after
        // the program's samplers, as the game's programs do.
        const int s = samplerVar ? samplerSlot_.at(samplerVar) : int(iface_.samplers.size());
        if (!samplerVar) iface_.default_sampler = s;
        Emit("LDC.U64 D1.x, buf14[" + std::to_string(1352 + 8 * s) + "]");
        Emit("OR.U64 D0.x, D0, D1");
        return "handle(D0.x)";
    }
    void Sample(const Inst& in) {
        const auto& w = in.w;
        const auto resultType = w[0], id = w[1], si = w[2];
        const auto coord = Get(w[3]);
        std::uint32_t imageVar = 0, samplerVar = 0;
        if (sampledImages_.count(si)) { imageVar = loadedFrom_.at(sampledImages_[si].first); samplerVar = loadedFrom_.at(sampledImages_[si].second); }
        else imageVar = loadedFrom_.at(si);
        const auto& imgType = types_[types_[typeOf_[imageVar]].elem];
        const std::string target = Target(types_[typeOf_[imageVar]].elem);
        std::uint32_t operandsAt = 4;
        bool dref = in.op == OpImageSampleDrefImplicitLod || in.op == OpImageSampleDrefExplicitLod;
        Val depthRef;
        if (dref) { depthRef = Get(w[4]); operandsAt = 5; }
        std::uint32_t mask = w.size() > operandsAt ? w[operandsAt] : 0;
        std::size_t next = operandsAt + 1;
        std::optional<Val> bias, lod, gradX, gradY;
        std::uint32_t lodId = 0;
        if (mask & 0x1) bias = Get(w[next++]);
        if (mask & 0x2) { lodId = w[next]; lod = Get(w[next++]); }
        if (mask & 0x4) { gradX = Get(w[next++]); gradY = Get(w[next++]); }
        if (mask & ~0x7u) throw Error("image operands other than bias, lod and gradients");
        // A cube shadow compare at level 0 (HLSL's SampleCmpLevelZero) is a plain TEX, as the game's programs have it:
        // the assembly has no TXL for a cube shadow target (its coordinate, reference and level fill five components).
        if (dref && lod && target == "CUBE" && lod->imm && lod->reg == "{0, 0, 0, 0}") lod.reset();
        const unsigned coordN = coord.n;
        // The coordinate register: the coordinate, then the depth reference and the bias or lod, as the
        // instructions read them (the last component carries the bias or level).
        auto c = New();
        Emit("MOV.F " + c + Mask(coordN) + ", " + Src(coord, coordN));
        unsigned slot = coordN;
        if (dref) { if (slot > 3) throw Error("a depth reference with a four-component coordinate"); Emit("MOV.F " + c + "." + Comp(slot) + ", " + Src(depthRef, 1)); ++slot; }
        std::string op = "TEX";
        // The bias or level goes in the coordinate's last component, unless the coordinates and the reference fill all
        // four: then it is an operand of its own.
        const bool full = slot >= 4;
        std::string extra;
        // A comparison sample of a cube shadow map at level 0 in a pixel program (UE's SampleCmpLevelZero, as the
        // translucent lighting injection's point light shadows): TEX, as the game's programs sample it (the map has
        // one level; the back end takes no level on a cube shadow target).
        const auto level0 = constants_.find(lodId);
        if (dref && lod && target == "CUBE" && stage_ == Stage::Fragment && level0 != constants_.end() &&
            (level0->second.empty() || (level0->second[0] & 0x7fffffffu) == 0)) lod.reset();
        if (bias || lod) {
            const auto& level = bias ? *bias : *lod;
            op = bias ? "TXB" : "TXL";
            if (full) { auto e = New(); Emit("MOV.F " + e + ".x, " + Src(level, 1)); extra = ", " + e + ".x"; }
            else Emit("MOV.F " + c + ".w, " + Src(level, 1));
        }
        if (in.op == OpImageSampleExplicitLod && !lod && !gradX) throw Error("an explicit-lod sample without a level");
        const std::string handle = Handle(imageVar, samplerVar);
        const unsigned n = Components(resultType);
        auto d = Def(id, n);
        const char kind = ScalarKind(imgType.elem);
        const std::string type = kind == 'F' ? "F" : kind == 'S' ? "S" : "U";
        std::string shadow = dref ? "SHADOW" + target : target;
        if (gradX) {
            auto gx = New(), gy = New();
            Emit("MOV.F " + gx + Mask(gradX->n) + ", " + Src(*gradX, gradX->n));
            Emit("MOV.F " + gy + Mask(gradY->n) + ", " + Src(*gradY, gradY->n));
            Emit("TXD." + type + " " + Dst(d) + ", " + c + ", " + gx + ", " + gy + ", " + handle + ", " + shadow);
            return;
        }
        Emit(op + "." + type + " " + Dst(d) + ", " + c + extra + ", " + handle + ", " + shadow);
    }
    void Fetch(const Inst& in) {
        const auto& w = in.w;
        const auto resultType = w[0], id = w[1], image = w[2];
        const auto coord = Get(w[3]);
        std::optional<Val> lod;
        if (w.size() > 4) {
            if (w[4] & ~0x2u) throw Error("image fetch operands other than lod");
            if (w[4] & 0x2) lod = Get(w[5]);
        }
        const auto imageVar = loadedFrom_.at(image);
        const auto& imgType = types_[types_[typeOf_[imageVar]].elem];
        auto c = New();
        Emit("MOV.S " + c + Mask(coord.n) + ", " + Src(coord, coord.n));
        Emit("MOV.S " + c + ".w, " + (lod ? Src(*lod, 1) : std::string("{0, 0, 0, 0}.x")));
        const std::string handle = Handle(imageVar, 0);
        auto d = Def(id, Components(resultType));
        const char kind = ScalarKind(imgType.elem);
        Emit(std::string("TXF.") + (kind == 'F' ? "F" : kind == 'S' ? "S" : "U") + " " + Dst(d) + ", " + c + ", " + handle + ", " + Target(types_[typeOf_[imageVar]].elem));
    }

    // ----- the function -----
    void EmitFunction() {
        if (blockOrder_.empty()) throw Error("no blocks");
        EmitRegion(blockOrder_.front(), 0, 0, 0, 0);
    }
    // Phi copies: as control goes from `from` to `to`, each Phi of `to` takes its value for that edge.
    void PhiCopies(std::uint32_t from, std::uint32_t to) {
        const auto& range = blocks_.at(to);
        for (std::size_t i = range.first + 1; i <= range.last; ++i) {
            const auto& in = insts_[i];
            if (in.op != OpPhi) break;
            const auto id = in.w[1];
            const auto& phiType = types_[in.w[0]];
            if (!vals_.count(id)) {
                Val v; v.n = Components(in.w[0]);
                if (Columned(phiType)) { for (unsigned c = 0; c < phiType.count; ++c) v.cols.push_back(New()); v.reg = v.cols[0]; }
                else v.reg = New();
                vals_[id] = v;
            }
            const auto d = vals_[id];
            for (std::size_t p = 2; p + 1 < in.w.size(); p += 2) {
                if (in.w[p + 1] != from) continue;
                const auto src = Get(in.w[p]);
                if (Columned(phiType)) {
                    if (src.cols.size() != d.cols.size()) throw Error("a matrix or array Phi whose value is not held as columns");
                    for (std::size_t c = 0; c < d.cols.size(); ++c) Emit("MOV.F " + d.cols[c] + Mask(d.n) + ", " + src.cols[c]);
                    continue;
                }
                Emit("MOV." + KindName(ScalarKind(in.w[0])) + " " + Dst(d) + ", " + Src(src, d.n));
            }
        }
    }
    struct Loop { std::uint32_t header = 0, merge = 0, cont = 0; };
    std::vector<Loop> loops_;
    // The continue block of the innermost loop, then its copies into the header: what runs before the next iteration.
    void RunContinue() {
        const auto& loop = loops_.back();
        if (loop.cont != loop.header) {
            const auto& range = blocks_.at(loop.cont);
            for (std::size_t i = range.first + 1; i < range.last; ++i) Instruction(insts_[i]);
            const auto& term = insts_[range.last];
            if (term.op != OpBranch || term.w[0] != loop.header) throw Error("a continue block that does not branch back to its header");
        }
        PhiCopies(loop.cont, loop.header);
    }
    // The innermost construct a selection can break out of: a switch (its merge block), or a loop (merge 0: a loop's
    // breaks are BRK). DXC wraps a function's early returns, and each GetPerInstanceCustomData fetch, in a switch with
    // only a default whose selections branch to the switch's merge. `live` is the flag of "no break taken", made where
    // the code after a selection needs it; each break's marker becomes its clear.
    struct Breakable { std::uint32_t merge = 0; unsigned id = 0; std::string live; };
    std::vector<Breakable> breakables_;
    unsigned switches_ = 0;
    std::vector<std::uint32_t> Successors(std::uint32_t label) {
        const auto& term = insts_[blocks_.at(label).last];
        if (term.op == OpBranch) return {term.w[0]};
        if (term.op == OpBranchConditional) return {term.w[1], term.w[2]};
        std::vector<std::uint32_t> s;
        if (term.op == OpSwitch) { s.push_back(term.w[1]); for (std::size_t p = 2; p + 1 < term.w.size(); p += 2) s.push_back(term.w[p + 1]); }
        return s;
    }
    // Where control from `label` goes before it leaves the selection whose merge is `merge`: into that merge, or out of
    // the innermost switch (a break). The region's end, a loop's break or continue and a return are neither.
    struct Reach { bool merge = false, brk = false; };
    Reach Reaches(std::uint32_t label, std::uint32_t merge, std::uint32_t stop, std::uint32_t breakTo, std::uint32_t continueTo) {
        Reach r;
        const auto sw = breakables_.empty() ? 0 : breakables_.back().merge;
        std::set<std::uint32_t> seen;
        std::vector<std::uint32_t> work{label};
        while (!work.empty()) {
            const auto l = work.back(); work.pop_back();
            if (!seen.insert(l).second) continue;
            if (l == merge) { r.merge = true; continue; }
            if (sw && l == sw) { r.brk = true; continue; }
            if (l == stop || (breakTo && l == breakTo) || (continueTo && l == continueTo)) continue;
            for (const auto s : Successors(l)) work.push_back(s);
        }
        return r;
    }
    // Emits the blocks from `label` on, entered from `from`, until `stop` (the enclosing construct's merge block).
    // A branch to the innermost loop's merge block breaks out of it; one to its continue block runs that block and
    // goes on with the next iteration. A branch to the innermost switch's merge from a selection inside it breaks out
    // of the switch.
    void EmitRegion(std::uint32_t label, std::uint32_t stop, std::uint32_t breakTo, std::uint32_t continueTo, std::uint32_t from) {
        while (label) {
            if (from && !(continueTo && label == continueTo)) PhiCopies(from, label);
            if (label == stop) return;
            if (!breakables_.empty() && breakables_.back().merge == label) { Emit("@BRK" + std::to_string(breakables_.back().id)); return; }
            if (breakTo && label == breakTo) { Emit("BRK"); return; }
            if (continueTo && label == continueTo) {
                PhiCopies(from, label);
                RunContinue();
                Emit("CONT");
                return;
            }
            const auto& range = blocks_.at(label);
            std::uint32_t merge = 0, loopMerge = 0, loopContinue = 0;
            for (std::size_t i = range.first + 1; i < range.last; ++i) {
                if (insts_[i].op == OpLoopMerge) { loopMerge = insts_[i].w[0]; loopContinue = insts_[i].w[1]; }
                if (insts_[i].op == OpSelectionMerge) merge = insts_[i].w[0];
            }
            if (loopMerge) Emit("REP");
            for (std::size_t i = range.first + 1; i < range.last; ++i) {
                const auto& in = insts_[i];
                if (in.op == OpSelectionMerge || in.op == OpLoopMerge) continue;
                Instruction(in);
            }
            const auto& term = insts_[range.last];
            if (loopMerge) {
                // The loop: this header's code above is the start of each iteration. The body runs until it reaches
                // the continue block; that block and the copies into the header end the iteration.
                loops_.push_back({label, loopMerge, loopContinue});
                breakables_.push_back({});
                if (term.op == OpBranch) {
                    EmitRegion(term.w[0], loopContinue, loopMerge, loopContinue, label);
                } else if (term.op == OpBranchConditional) {
                    SetCondition(Get(term.w[0]));
                    Emit("IF NE.x");
                    EmitRegion(term.w[1], loopContinue, loopMerge, loopContinue, label);
                    Emit("ELSE");
                    EmitRegion(term.w[2], loopContinue, loopMerge, loopContinue, label);
                    Emit("ENDIF");
                } else throw Error("a loop header that does not branch into its body");
                RunContinue();
                breakables_.pop_back();
                loops_.pop_back();
                Emit("ENDREP");
                from = 0;       // the copies into the merge block were made where the loop broke out
                label = loopMerge;
                continue;
            }
            switch (term.op) {
            case OpBranch:
                from = label; label = term.w[0];
                break;
            case OpBranchConditional: {
                if (!merge) throw Error("a conditional branch without a selection merge");
                const auto a = Reaches(term.w[1], merge, stop, breakTo, continueTo), b = Reaches(term.w[2], merge, stop, breakTo, continueTo);
                SetCondition(Get(term.w[0]));
                Emit("IF NE.x");
                if (!a.brk && !b.brk) {
                    EmitRegion(term.w[1], merge, breakTo, continueTo, label);
                    Emit("ELSE");
                    EmitRegion(term.w[2], merge, breakTo, continueTo, label);
                    Emit("ENDIF");
                    from = 0; label = merge;
                    break;
                }
                // An arm can break out of the switch: the code after the merge runs only where control reached the
                // merge. Where one arm reaches it, that arm goes on through it (the other never comes back); where
                // both do, the rest runs while no break was taken.
                EmitRegion(term.w[1], a.merge && !b.merge ? stop : merge, breakTo, continueTo, label);
                Emit("ELSE");
                EmitRegion(term.w[2], b.merge && !a.merge ? stop : merge, breakTo, continueTo, label);
                Emit("ENDIF");
                if (a.merge && b.merge) {
                    auto& sw = breakables_.back();
                    if (sw.live.empty()) sw.live = New();
                    Val live; live.reg = sw.live;
                    SetCondition(live);
                    Emit("IF NE.x");
                    EmitRegion(merge, stop, breakTo, continueTo, 0);
                    Emit("ENDIF");
                }
                return;
            }
            case OpSwitch: {
                if (!merge) throw Error("a switch without a selection merge");
                // The switch's code is held until its end, when it is known whether a break needs the flag.
                breakables_.push_back({merge, ++switches_, ""});
                auto* outer = out_;
                std::ostringstream body;
                out_ = &body;
                const auto sel = Get(term.w[0]);
                const auto def = term.w[1];
                std::size_t opened = 0;
                for (std::size_t p = 2; p + 1 < term.w.size(); p += 2) {
                    auto c = New();
                    Emit("SEQ.S " + c + ".x, " + Src(sel, 1) + ", {" + std::to_string(std::int32_t(term.w[p])) + ", 0, 0, 0}.x");
                    Emit("MOV.U.CC RC.x, " + c + ".x");
                    Emit("IF NE.x");
                    EmitRegion(term.w[p + 1], merge, breakTo, continueTo, label);
                    Emit("ELSE");
                    ++opened;
                }
                EmitRegion(def, merge, breakTo, continueTo, label);
                for (std::size_t i = 0; i < opened; ++i) Emit("ENDIF");
                out_ = outer;
                const auto sw = breakables_.back();
                breakables_.pop_back();
                std::string text = body.str();
                const std::string marker = "@BRK" + std::to_string(sw.id) + ";\n";
                const std::string clear = sw.live.empty() ? "" : "MOV.F " + sw.live + ".x, {0, 0, 0, 0}.x;\n";
                for (auto at = text.find(marker); at != std::string::npos; at = text.find(marker, at + clear.size()))
                    text.replace(at, marker.size(), clear);
                if (!sw.live.empty()) Emit("MOV.F " + sw.live + ".x, {1, 0, 0, 0}.x");
                *out_ << text;
                from = 0; label = merge;
                break;
            }
            case OpReturn: return;
            case OpKill: Emit("KIL {-1, -1, -1, -1}"); return;
            case OpUnreachable: return;
            default: throw Error("unsupported terminator");
            }
        }
    }

    // ----- function variables of several columns -----
    void StoreColumns(const Val& local, const Val& v, char kind) {
        if (v.cols.size() != local.cols.size()) throw Error("a matrix or array stored whole from a value not held as columns");
        const std::string mov = "MOV." + KindName(kind) + " ";
        for (std::size_t c = 0; c < local.cols.size(); ++c) Emit(mov + local.cols[c] + Mask(local.n) + ", " + v.cols[c]);
    }
    // Runs `body(column)` for the column a chain's first index names: directly for a constant index, else once per
    // column under a test of the index.
    template <class F> void ForColumn(const Val& local, std::uint32_t index, F body) {
        if (constants_.count(index)) { body(local.cols.at(constants_[index][0])); return; }
        const auto ix = Get(index);
        for (std::size_t c = 0; c < local.cols.size(); ++c) {
            auto t = New();
            Emit("SEQ.S " + t + ".x, " + Src(ix, 1) + ", {" + std::to_string(c) + ", 0, 0, 0}.x");
            Emit("MOV.U.CC RC.x, " + t + ".x");
            Emit("IF NE.x");
            body(local.cols[c]);
            Emit("ENDIF");
        }
    }
    unsigned ChainComponent(const LocalChain& c, std::size_t at) {
        if (at >= c.idx.size()) return 4;
        if (at + 1 != c.idx.size()) throw Error("a nested index into a function variable");
        if (!constants_.count(c.idx[at])) throw Error("a dynamic component index into a function variable");
        return constants_[c.idx[at]][0];
    }
    void LoadLocal(std::uint32_t id, std::uint32_t type, const LocalChain& c) {
        const auto& l = locals_.at(c.base);
        const std::string mov = "MOV." + KindName(ScalarKind(type)) + " ";
        if (l.cols.empty()) {
            const unsigned comp = ChainComponent(c, 0);
            if (comp == 4) throw Error("an empty access chain into a function variable");
            auto d = Def(id, 1);
            Emit(mov + d.reg + ".x, " + l.reg + "." + Comp(comp));
            return;
        }
        const unsigned comp = ChainComponent(c, 1);
        auto d = Def(id, comp == 4 ? l.n : 1);
        ForColumn(l, c.idx.at(0), [&](const std::string& col) {
            Emit(mov + (comp == 4 ? Dst(d) + ", " + col : d.reg + ".x, " + col + "." + Comp(comp)));
        });
    }
    void StoreLocal(const LocalChain& c, const Val& v) {
        const auto& l = locals_.at(c.base);
        if (l.cols.empty()) {
            const unsigned comp = ChainComponent(c, 0);
            if (comp == 4) throw Error("an empty access chain into a function variable");
            Emit("MOV.F " + l.reg + "." + Comp(comp) + ", " + Src(v, 1));
            return;
        }
        const unsigned comp = ChainComponent(c, 1);
        ForColumn(l, c.idx.at(0), [&](const std::string& col) {
            Emit(comp == 4 ? "MOV.F " + col + Mask(l.n) + ", " + Src(v, l.n) : "MOV.F " + col + "." + Comp(comp) + ", " + Src(v, 1));
        });
    }

    // ----- instructions -----
    void Instruction(const Inst& in) {
        const auto& w = in.w;
        switch (in.op) {
        case OpPhi: return;          // filled by the predecessors' copies
        case OpUndef: {
            const auto& t = types_[w[0]];
            Val v; v.imm = true;
            if (Columned(t)) { v.n = Components(w[0]); for (unsigned c = 0; c < t.count; ++c) v.cols.push_back("{0, 0, 0, 0}"); v.reg = v.cols[0]; }
            else { v.n = Components(w[0]); v.reg = "{0, 0, 0, 0}"; }
            vals_[w[1]] = v;
            return;
        }
        case OpLine: case OpNoLine: return;
        case OpEmitVertex: Emit("EMIT"); return;
        case OpTranspose: {
            // Column j of the result holds component j of each of the matrix's columns.
            const auto m = Get(w[2]);
            const auto& rt = types_[w[0]];
            Val v; v.n = types_[rt.elem].count;
            for (unsigned j = 0; j < rt.count; ++j) {
                auto r = New();
                for (unsigned i = 0; i < m.cols.size(); ++i) Emit("MOV.F " + r + "." + Comp(i) + ", " + m.cols[i] + "." + Comp(j));
                v.cols.push_back(r);
            }
            v.reg = v.cols[0]; vals_[w[1]] = v;
            return;
        }
        case OpEndPrimitive: Emit("ENDPRIM"); return;
        case OpVariable: {
            const auto storage = w[2];
            if (storage != StorageFunction && storage != StoragePrivate) throw Error("a variable in a function of another storage class");
            const auto pointeeId = types_[w[0]].elem;
            const auto& pointee = types_[pointeeId];
            if (Columned(pointee)) {
                // A matrix or an array of scalars or vectors: one temporary per column or element.
                Val v; v.n = Components(pointeeId);
                for (unsigned c = 0; c < pointee.count; ++c) v.cols.push_back(New());
                v.reg = v.cols[0];
                if (w.size() > 3) StoreColumns(v, Get(w[3]), ScalarKind(pointeeId));
                locals_[w[1]] = v;
                return;
            }
            if (pointee.k != K::Bool && pointee.k != K::Int && pointee.k != K::Float && pointee.k != K::Vector)
                throw Error("a function variable that is not a scalar, vector, matrix or array of them");
            Val v; v.reg = New(); v.n = Components(pointeeId);
            if (w.size() > 3) { const auto init = Get(w[3]); Emit("MOV.F " + Dst(v) + ", " + Src(init, v.n)); }
            locals_[w[1]] = v;
            return;
        }
        case OpAccessChain: case OpInBoundsAccessChain: {
            const auto base = w[2];
            std::vector<std::uint32_t> idx(w.begin() + 3, w.end());
            if (outputBuiltin_.count(base) && std::stoul(outputBuiltin_[base]) == BuiltInClipDistance) {
                if (!options_.drop_clip_distance) throw Error("a clip distance (the game's renderer uses none)");
                droppedClip_.insert(w[1]);
                return;
            }
            if (inputLoc_.count(base) || inputBuiltin_.count(base) || outputLoc_.count(base) || outputBuiltin_.count(base)) {
                IoChain c; c.base = base;
                for (auto ix : idx) {
                    if (!constants_.count(ix)) throw Error("a dynamic index into an input or output");
                    c.idx.push_back(constants_[ix][0]);
                }
                ioChains_[w[1]] = c;
                return;
            }
            if (locals_.count(base) || localChains_.count(base)) {
                LocalChain c;
                if (localChains_.count(base)) c = localChains_[base];
                else c.base = base;
                c.idx.insert(c.idx.end(), idx.begin(), idx.end());
                localChains_[w[1]] = c;
                return;
            }
            pointers_[w[1]] = Chain(base, idx);
            return;
        }
        case OpLoad: {
            const auto id = w[1], ptr = w[2];
            if (pointers_.count(ptr)) { LoadUniform(id, pointers_[ptr]); return; }
            if (localChains_.count(ptr)) { LoadLocal(id, w[0], localChains_[ptr]); return; }
            if (locals_.count(ptr) && !locals_[ptr].cols.empty()) {
                // A whole matrix or array: its columns copied, as the variable may change after.
                const auto& l = locals_[ptr];
                Val v; v.n = l.n;
                const std::string mov = "MOV." + KindName(ScalarKind(w[0])) + " ";
                for (const auto& col : l.cols) { auto r = New(); Emit(mov + r + Mask(v.n) + ", " + col); v.cols.push_back(r); }
                v.reg = v.cols[0]; vals_[id] = v;
                return;
            }
            if (locals_.count(ptr)) { vals_[id] = locals_[ptr]; auto copy = Def(id, locals_[ptr].n); Emit("MOV.F " + Dst(copy) + ", " + Src(locals_[ptr], copy.n)); return; }
            if (textureSlot_.count(ptr) || samplerSlot_.count(ptr)) { loadedFrom_[id] = ptr; return; }
            if (ioChains_.count(ptr)) {
                const auto [reg, component] = IoChainTarget(ioChains_[ptr], true);
                const char kind = ScalarKind(w[0]);
                if (component < 0) {
                    auto d = Def(id, Components(w[0])); Emit(std::string("MOV.") + KindName(kind) + " " + Dst(d) + ", " + reg);
                    if (reg == "fragment.position") Emit("RCP.F " + d.reg + ".w, fragment.position.w");   // as above
                }
                else if (reg == "fragment.position" && component == 3) { auto d = Def(id, 1); Emit("RCP.F " + d.reg + ".x, fragment.position.w"); }
                else { auto d = Def(id, 1); Emit(std::string("MOV.") + KindName(kind) + " " + d.reg + ".x, " + reg + "." + Comp(unsigned(component))); }
                return;
            }
            if (stage_ == Stage::Geometry && inputLoc_.count(ptr)) {
                // An input of a geometry program: one value per vertex. A scalar or vector input is a value of one
                // column per vertex; an array input, one value of several columns per vertex (gsNested_).
                const auto& t = types_[w[0]];
                if (t.k != K::Array) throw Error("a geometry input that is not an array per vertex");
                const auto& e = types_[t.elem];
                const int loc = inputLoc_[ptr];
                const char kind = ScalarKind(w[0]);
                auto attr = [&](unsigned vertex, int at, unsigned n) {
                    auto r = New();
                    Emit(std::string("MOV.") + KindName(kind) + " " + r + Mask(n) + ", vertex[" + std::to_string(vertex) + "].attrib[" + std::to_string(at) + "]");
                    return r;
                };
                if (e.k == K::Array) {
                    std::vector<Val> perVertex;
                    for (unsigned v = 0; v < t.count; ++v) {
                        Val x; x.n = Components(t.elem);
                        for (unsigned j = 0; j < e.count; ++j) x.cols.push_back(attr(v, loc + int(j), x.n));
                        x.reg = x.cols[0];
                        perVertex.push_back(x);
                    }
                    gsNested_[id] = perVertex;
                    Val v; v.n = perVertex[0].n; v.cols = {perVertex[0].reg}; v.reg = v.cols[0]; vals_[id] = v;
                    return;
                }
                Val v; v.n = Components(w[0]);
                for (unsigned i = 0; i < t.count; ++i) v.cols.push_back(attr(i, loc, v.n));
                v.reg = v.cols[0]; vals_[id] = v;
                return;
            }
            if (inputLoc_.count(ptr) && types_[w[0]].k == K::Array) {
                const int loc = inputLoc_[ptr];
                const auto& t = types_[w[0]];
                Val v; v.n = Components(w[0]);
                const char kind = ScalarKind(w[0]);
                for (unsigned e = 0; e < t.count; ++e) {
                    const auto attr = std::string(stage_ == Stage::Vertex ? "vertex" : "fragment") + ".attrib[" + std::to_string(loc + int(e)) + "]";
                    (Has(ptr, DecFlat) ? flatAttribs_ : attribs_).insert(loc + int(e));
                    auto r = New();
                    Emit(std::string("MOV.") + KindName(kind) + " " + r + Mask(v.n) + ", " + attr);
                    v.cols.push_back(r);
                }
                v.reg = v.cols[0]; vals_[id] = v;
                return;
            }
            if (inputLoc_.count(ptr)) {
                const int loc = inputLoc_[ptr];
                const auto n = Components(w[0]);
                const auto attr = std::string(stage_ == Stage::Vertex ? "vertex" : "fragment") + ".attrib[" + std::to_string(loc) + "]";
                (Has(ptr, DecFlat) ? flatAttribs_ : attribs_).insert(loc);
                auto d = Def(id, n);
                const char kind = ScalarKind(w[0]);
                Emit(std::string("MOV.") + KindName(kind) + " " + Dst(d) + ", " + attr);
                return;
            }
            if (inputBuiltin_.count(ptr)) {
                const auto b = std::stoul(inputBuiltin_[ptr]);
                if (b == BuiltInFragCoord) {
                    // SV_Position.w: DXC (as UE calls it, -fvk-use-dx-position-w) reads it as 1 / FragCoord.w; the
                    // game's programs use the fragment's w as it is. Its reciprocal here gives that back.
                    auto d = Def(id, 4);
                    Emit("MOV.F " + Dst(d) + ", fragment.position");
                    Emit("RCP.F " + d.reg + ".w, fragment.position.w");
                    return;
                }
                if (b == BuiltInFrontFacing) {
                    auto d = Def(id, 1);
                    Emit("SGT.S " + d.reg + ".x, fragment.facing, {0, 0, 0, 0}.x");
                    Emit("I2F.S " + d.reg + ".x, -" + d.reg + ".x");
                    return;
                }
                if (b == BuiltInVertexIndex) { auto d = Def(id, 1); Emit("MOV.S " + d.reg + ".x, vertex.id"); return; }
                if (b == BuiltInInstanceIndex) { auto d = Def(id, 1); Emit("MOV.S " + d.reg + ".x, vertex.instance"); return; }
                if (b == BuiltInLayer && stage_ == Stage::Fragment && layerLocation_ >= 0) {
                    flatAttribs_.insert(layerLocation_);
                    auto d = Def(id, 1);
                    Emit("MOV.U " + d.reg + ".x, fragment.attrib[" + std::to_string(layerLocation_) + "].x");
                    return;
                }
                throw Error("an unsupported input built-in " + inputBuiltin_[ptr]);
            }
            throw Error("a load from an unknown pointer");
        }
        case OpStore: {
            const auto ptr = w[0];
            if (droppedClip_.count(ptr)) return;
            if (outputBuiltin_.count(ptr) && std::stoul(outputBuiltin_[ptr]) == BuiltInClipDistance) {
                if (!options_.drop_clip_distance) throw Error("a clip distance (the game's renderer uses none)");
                return;
            }
            const auto v = Get(w[1]);
            if (ioChains_.count(ptr)) {
                const auto [reg, component] = IoChainTarget(ioChains_[ptr], false);
                if (component < 0) Emit("MOV.F " + reg + Mask(v.n) + ", " + Src(v, v.n));
                else Emit("MOV.F " + reg + "." + Comp(unsigned(component)) + ", " + Src(v, 1));
                return;
            }
            if (localChains_.count(ptr)) { StoreLocal(localChains_[ptr], v); return; }
            if (locals_.count(ptr) && !locals_[ptr].cols.empty()) { StoreColumns(locals_[ptr], v, ScalarKind(types_[typeOf_[ptr]].elem)); return; }
            if (locals_.count(ptr)) { Emit("MOV.F " + Dst(locals_[ptr]) + ", " + Src(v, locals_[ptr].n)); return; }
            if (outputLoc_.count(ptr) && types_[types_[typeOf_[ptr]].elem].k == K::Array) {
                const int loc = outputLoc_[ptr];
                if (stage_ == Stage::Fragment) throw Error("an array of colour outputs");
                for (std::size_t e = 0; e < v.cols.size(); ++e)
                    Emit("MOV.F result.attrib[" + std::to_string(loc + int(e)) + "]" + Mask(v.n) + ", " + v.cols[e]);
                return;
            }
            if (outputLoc_.count(ptr)) {
                const int loc = outputLoc_[ptr];
                if (stage_ == Stage::Fragment) {
                    colorOutputs_.insert(loc);
                    Emit("MOV.F result_color" + std::to_string(loc) + Mask(v.n) + ", " + Src(v, v.n));
                } else {
                    Emit("MOV.F result.attrib[" + std::to_string(loc) + "]" + Mask(v.n) + ", " + Src(v, v.n));
                }
                return;
            }
            if (outputBuiltin_.count(ptr)) {
                const auto b = std::stoul(outputBuiltin_[ptr]);
                if (b == BuiltInFragDepth) { usesDepthOut_ = true; Emit("MOV.F result.depth.z, " + Src(v, 1)); return; }
                if (b == BuiltInPosition) { Emit("MOV.F result.position, " + Src(v, 4)); return; }
                if (b == BuiltInLayer && stage_ == Stage::Geometry) {
                    Emit("MOV.S result.layer.x, " + Src(v, 1));
                    Emit("MOV.U result.attrib[" + std::to_string(layerLocation_) + "].x, " + Src(v, 1));
                    return;
                }
                throw Error("an unsupported output built-in");
            }
            throw Error("a store to an unknown pointer");
        }
        case OpCompositeExtract: {
            if (gsNested_.count(w[2])) {
                if (w.size() != 4) throw Error("a nested extract from a geometry input");
                vals_[w[1]] = gsNested_[w[2]].at(w[3]);
                return;
            }
            const auto src = Get(w[2]);
            const auto& srcType = TypeOfId(w[2]);
            if (Columned(srcType)) {
                const auto c = w[3];
                if (w.size() == 4) { Val v; v.reg = src.cols.at(c); v.n = src.n; vals_[w[1]] = v; return; }
                auto d = Def(w[1], 1);
                Emit("MOV.F " + d.reg + ".x, " + src.cols.at(c) + "." + Comp(w[4]));
                return;
            }
            if (w.size() != 4) throw Error("a nested composite extract");
            if (srcType.k != K::Vector) throw Error("a composite extract from a non-vector");
            auto d = Def(w[1], 1);
            const char kind = ScalarKind(w[0]);
            Emit(std::string("MOV.") + KindName(kind) + " " + d.reg + ".x, " + SrcComp(src, w[3]));
            return;
        }
        case OpCompositeInsert: {
            if (Columned(types_[w[0]])) {
                // A column (or one element of a column) into a matrix or array: the other columns are the composite's.
                const auto obj = Get(w[2]);
                const auto m = Get(w[3]);
                Val v = m; v.n = Components(w[0]);
                if (w.size() == 5) {
                    auto r = New(); Emit("MOV.F " + r + Mask(v.n) + ", " + Src(obj, v.n)); v.cols.at(w[4]) = r;
                } else if (w.size() == 6) {
                    auto r = New(); Emit("MOV.F " + r + Mask(v.n) + ", " + v.cols.at(w[4]));
                    Emit("MOV.F " + r + "." + Comp(w[5]) + ", " + Src(obj, 1)); v.cols.at(w[4]) = r;
                } else throw Error("a nested matrix insert");
                v.reg = v.cols[0]; vals_[w[1]] = v; return;
            }
            const auto obj = Get(w[2]);
            const auto comp = Get(w[3]);
            if (w.size() != 5) throw Error("a nested composite insert");
            auto d = Def(w[1], comp.n);
            Emit("MOV.F " + Dst(d) + ", " + Src(comp, d.n));
            Emit("MOV.F " + d.reg + "." + Comp(w[4]) + ", " + Src(obj, 1));
            return;
        }
        case OpCompositeConstruct: {
            const auto& t = types_[w[0]];
            if (Columned(t)) {
                Val v; v.n = Components(w[0]);
                for (std::size_t i = 2; i < w.size(); ++i) {
                    const auto col = Get(w[i]);
                    auto r = New(); Emit("MOV.F " + r + Mask(v.n) + ", " + Src(col, v.n)); v.cols.push_back(r);
                }
                v.reg = v.cols[0]; vals_[w[1]] = v; return;
            }
            if (t.k != K::Vector) throw Error("a composite construct of a non-vector");
            auto d = Def(w[1], t.count);
            unsigned at = 0;
            const char kind = ScalarKind(w[0]);
            const std::string mov = std::string("MOV.") + KindName(kind) + " ";
            for (std::size_t i = 2; i < w.size(); ++i) {
                const auto part = Get(w[i]);
                for (unsigned c = 0; c < part.n; ++c, ++at) Emit(mov + d.reg + "." + Comp(at) + ", " + SrcComp(part, c));
            }
            return;
        }
        case OpVectorShuffle: {
            const auto a = Get(w[2]), b = Get(w[3]);
            const unsigned n = unsigned(w.size() - 4);
            auto d = Def(w[1], n);
            const char kind = ScalarKind(w[0]);
            const std::string mov = std::string("MOV.") + KindName(kind) + " ";
            for (unsigned i = 0; i < n; ++i) {
                const auto sel = w[4 + i];
                if (sel == 0xffffffff) continue;
                Emit(mov + d.reg + "." + Comp(i) + ", " + (sel < a.n ? SrcComp(a, sel) : SrcComp(b, sel - a.n)));
            }
            return;
        }
        case OpCopyObject: case OpBitcast: {
            const auto a = Get(w[2]);
            if (!a.cols.empty()) { vals_[w[1]] = a; return; }
            auto d = Def(w[1], a.n);
            Emit("MOV.U " + Dst(d) + ", " + Src(a, d.n));
            return;
        }
        case OpSampledImage: sampledImages_[w[1]] = {w[2], w[3]}; return;
        case OpImageSampleImplicitLod: case OpImageSampleExplicitLod:
        case OpImageSampleDrefImplicitLod: case OpImageSampleDrefExplicitLod: Sample(in); return;
        case OpImageFetch: Fetch(in); return;
        case OpImageQuerySizeLod: {
            const auto imageVar = loadedFrom_.at(w[2]);
            const auto lod = Get(w[3]);
            auto c = New();
            Emit("MOV.S " + c + ".x, " + Src(lod, 1));
            const std::string handle = Handle(imageVar, 0);
            auto d = Def(w[1], Components(w[0]));
            Emit("TXQ.S " + Dst(d) + ", " + c + ".x, " + handle + ", " + Target(types_[typeOf_[imageVar]].elem));
            return;
        }
        case OpExtInst: ExtInst(in); return;
        default: Arithmetic(in); return;
        }
    }

    void Binary(const std::string& op, std::uint32_t id, std::uint32_t type, const Val& a, const Val& b, bool negateB = false) {
        const unsigned n = Components(type);
        auto d = Def(id, n);
        Emit(op + " " + Dst(d) + ", " + Src(a, n) + ", " + Src(b, n, negateB));
    }
    void PerComponent(const std::string& op, std::uint32_t id, std::uint32_t type, const Val& a, const Val* b = nullptr) {
        const unsigned n = Components(type);
        auto d = Def(id, n);
        for (unsigned c = 0; c < n; ++c)
            Emit(op + " " + d.reg + "." + Comp(c) + ", " + SrcComp(a, c) + (b ? ", " + SrcComp(*b, c) : std::string()));
    }

    void Arithmetic(const Inst& in) {
        const auto& w = in.w;
        const auto type = w[0], id = w[1];
        auto A = [&]() { return Get(w[2]); };
        auto B = [&]() { return Get(w[3]); };
        const char kind = ScalarKind(type);
        const std::string ik = kind == 'S' ? "S" : "U";
        switch (in.op) {
        case OpFAdd: Binary("ADD.F", id, type, A(), B()); return;
        case OpFSub: Binary("ADD.F", id, type, A(), B(), true); return;
        case OpFMul: Binary("MUL.F", id, type, A(), B()); return;
        case OpFDiv: { const auto a = A(), b = B(); PerComponent("DIV.F", id, type, a, &b); return; }
        case OpFNegate: { const auto a = A(); auto d = Def(id, Components(type)); Emit("MOV.F " + Dst(d) + ", " + Src(a, d.n, true)); return; }
        case OpFMod: case OpFRem: {
            const auto a = A(), b = B();
            const unsigned n = Components(type);
            auto q = New();
            for (unsigned c = 0; c < n; ++c) Emit("DIV.F " + q + "." + Comp(c) + ", " + SrcComp(a, c) + ", " + SrcComp(b, c));
            // HLSL's fmod truncates (the result has the dividend's sign), which DXC writes as OpFMod (floored).
            const bool floored = in.op == OpFMod && !options_.fmod_truncates;
            Emit((floored ? "FLR.F " : "TRUNC.F ") + q + Mask(n) + ", " + q);
            auto d = Def(id, n);
            Emit("MUL.F " + q + Mask(n) + ", " + q + ", " + Src(b, n));
            Emit("ADD.F " + Dst(d) + ", " + Src(a, n) + ", -" + q);
            return;
        }
        case OpIAdd: Binary("ADD." + ik, id, type, A(), B()); return;
        case OpISub: Binary("SUB." + ik, id, type, A(), B()); return;
        case OpIMul: Binary("MUL." + ik, id, type, A(), B()); return;
        case OpSNegate: { const auto a = A(); auto d = Def(id, Components(type)); Emit("MOV.S " + Dst(d) + ", " + Src(a, d.n, true)); return; }
        case OpUDiv: { const auto a = A(), b = B(); PerComponent("DIV.U", id, type, a, &b); return; }
        case OpSDiv: { const auto a = A(), b = B(); PerComponent("DIV.S", id, type, a, &b); return; }
        case OpUMod: { const auto a = A(), b = B(); PerComponent("MOD.U", id, type, a, &b); return; }
        case OpShiftRightLogical: Binary("SHR.U", id, type, A(), B()); return;
        case OpShiftRightArithmetic: Binary("SHR.S", id, type, A(), B()); return;
        case OpShiftLeftLogical: Binary("SHL.U", id, type, A(), B()); return;
        case OpBitwiseOr: Binary("OR.U", id, type, A(), B()); return;
        case OpBitwiseXor: Binary("XOR.U", id, type, A(), B()); return;
        case OpBitwiseAnd: Binary("AND.U", id, type, A(), B()); return;
        case OpNot: { const auto a = A(); auto d = Def(id, Components(type)); Emit("NOT.U " + Dst(d) + ", " + Src(a, d.n)); return; }
        case OpConvertFToS: { const auto a = A(); auto d = Def(id, Components(type)); Emit("TRUNC.S " + Dst(d) + ", " + Src(a, d.n)); return; }
        case OpConvertFToU: { const auto a = A(); auto d = Def(id, Components(type)); Emit("TRUNC.U " + Dst(d) + ", " + Src(a, d.n)); return; }
        case OpConvertSToF: { const auto a = A(); auto d = Def(id, Components(type)); Emit("I2F.S " + Dst(d) + ", " + Src(a, d.n)); return; }
        case OpConvertUToF: { const auto a = A(); auto d = Def(id, Components(type)); Emit("I2F.U " + Dst(d) + ", " + Src(a, d.n)); return; }
        case OpVectorTimesScalar: { const auto v = A(), s = B(); auto d = Def(id, Components(type)); Emit("MUL.F " + Dst(d) + ", " + Src(v, d.n) + ", " + s.reg + ".x"); return; }
        case OpMatrixTimesScalar: {
            const auto m = A(), s = B();
            Val v; v.n = m.n;
            for (const auto& col : m.cols) { auto r = New(); Emit("MUL.F " + r + Mask(v.n) + ", " + col + ", " + s.reg + ".x"); v.cols.push_back(r); }
            v.reg = v.cols[0]; vals_[id] = v; return;
        }
        case OpMatrixTimesVector: {
            const auto m = A(), v = B();
            auto d = Def(id, Components(type));
            for (std::size_t c = 0; c < m.cols.size(); ++c) {
                if (!c) Emit("MUL.F " + Dst(d) + ", " + m.cols[c] + ", " + SrcComp(v, unsigned(c)));
                else Emit("MAD.F " + Dst(d) + ", " + m.cols[c] + ", " + SrcComp(v, unsigned(c)) + ", " + d.reg);
            }
            return;
        }
        case OpVectorTimesMatrix: {
            const auto v = A(), m = B();
            auto d = Def(id, Components(type));
            const char* dp = v.n == 4 ? "DP4.F " : v.n == 3 ? "DP3.F " : "DP2.F ";
            for (std::size_t c = 0; c < m.cols.size(); ++c) Emit(dp + d.reg + "." + Comp(unsigned(c)) + ", " + v.reg + ", " + m.cols[c]);
            return;
        }
        case OpMatrixTimesMatrix: {
            const auto a = A(), b = B();
            Val v; v.n = a.n;
            for (const auto& bc : b.cols) {
                auto r = New();
                for (std::size_t c = 0; c < a.cols.size(); ++c) {
                    if (!c) Emit("MUL.F " + r + Mask(v.n) + ", " + a.cols[c] + ", " + bc + "." + Comp(unsigned(c)));
                    else Emit("MAD.F " + r + Mask(v.n) + ", " + a.cols[c] + ", " + bc + "." + Comp(unsigned(c)) + ", " + r);
                }
                v.cols.push_back(r);
            }
            v.reg = v.cols[0]; vals_[id] = v; return;
        }
        case OpDot: {
            const auto a = A(), b = B();
            auto d = Def(id, 1);
            Emit(std::string(a.n == 4 ? "DP4.F " : a.n == 3 ? "DP3.F " : "DP2.F ") + d.reg + ".x, " + a.reg + ", " + b.reg);
            return;
        }
        case OpFOrdEqual: case OpFUnordEqual: Compare("SEQ", 'F', id, A(), B()); return;
        case OpFOrdNotEqual: case OpFUnordNotEqual: Compare("SNE", 'F', id, A(), B()); return;
        case OpFOrdLessThan: case OpFUnordLessThan: Compare("SLT", 'F', id, A(), B()); return;
        case OpFOrdGreaterThan: case OpFUnordGreaterThan: Compare("SGT", 'F', id, A(), B()); return;
        case OpFOrdLessThanEqual: case OpFUnordLessThanEqual: Compare("SLE", 'F', id, A(), B()); return;
        case OpFOrdGreaterThanEqual: case OpFUnordGreaterThanEqual: Compare("SGE", 'F', id, A(), B()); return;
        case OpIEqual: Compare("SEQ", ScalarKind(typeOf_.at(w[2])), id, A(), B()); return;
        case OpINotEqual: Compare("SNE", ScalarKind(typeOf_.at(w[2])), id, A(), B()); return;
        case OpUGreaterThan: Compare("SGT", 'U', id, A(), B()); return;
        case OpSGreaterThan: Compare("SGT", 'S', id, A(), B()); return;
        case OpUGreaterThanEqual: Compare("SGE", 'U', id, A(), B()); return;
        case OpSGreaterThanEqual: Compare("SGE", 'S', id, A(), B()); return;
        case OpULessThan: Compare("SLT", 'U', id, A(), B()); return;
        case OpSLessThan: Compare("SLT", 'S', id, A(), B()); return;
        case OpULessThanEqual: Compare("SLE", 'U', id, A(), B()); return;
        case OpSLessThanEqual: Compare("SLE", 'S', id, A(), B()); return;
        case OpLogicalEqual: Compare("SEQ", 'F', id, A(), B()); return;
        case OpLogicalNotEqual: Compare("SNE", 'F', id, A(), B()); return;
        case OpLogicalAnd: Binary("MUL.F", id, type, A(), B()); return;
        case OpLogicalOr: Binary("MAX.F", id, type, A(), B()); return;
        case OpLogicalNot: { const auto a = A(); auto d = Def(id, Components(type)); Emit("SEQ.F " + Dst(d) + ", " + Src(a, d.n) + ", {0, 0, 0, 0}"); return; }
        case OpAny: case OpAll: {
            const auto a = A();
            auto d = Def(id, 1);
            Emit(std::string("MOV.F ") + d.reg + ".x, " + SrcComp(a, 0));
            for (unsigned c = 1; c < a.n; ++c) Emit(std::string(in.op == OpAny ? "MAX.F " : "MUL.F ") + d.reg + ".x, " + d.reg + ".x, " + SrcComp(a, c));
            return;
        }
        case OpIsNan: { const auto a = A(); auto d = Def(id, a.n); Emit("SNE.F " + Dst(d) + ", " + Src(a, a.n) + ", " + Src(a, a.n)); return; }
        case OpSelect: {
            // c ? t : f, lane by lane, through IF (the form the compiler takes for condition codes).
            const auto c = Get(w[2]), t = Get(w[3]), f = Get(w[4]);
            const unsigned n = Components(type);
            auto d = Def(id, n);
            const std::string mov = "MOV." + KindName(ScalarKind(type)) + " ";
            for (unsigned lane = 0; lane < n; ++lane) {
                auto cc = New();
                Emit("TRUNC.U.CC " + cc + ".x, " + SrcComp(c, c.n == 1 ? 0 : lane));
                Emit("IF NE.x");
                Emit(mov + d.reg + "." + Comp(lane) + ", " + SrcComp(t, lane));
                Emit("ELSE");
                Emit(mov + d.reg + "." + Comp(lane) + ", " + SrcComp(f, lane));
                Emit("ENDIF");
            }
            return;
        }
        case OpDPdx: { const auto a = A(); auto d = Def(id, Components(type)); Emit("DDX.F " + Dst(d) + ", " + Src(a, d.n)); return; }
        case OpDPdy: { const auto a = A(); auto d = Def(id, Components(type)); Emit("DDY.F " + Dst(d) + ", " + Src(a, d.n)); return; }
        case OpFwidth: {
            const auto a = A(); auto d = Def(id, Components(type)); auto t = New();
            Emit("DDX.F " + t + Mask(d.n) + ", " + Src(a, d.n));
            Emit("DDY.F " + Dst(d) + ", " + Src(a, d.n));
            Emit("ADD.F " + Dst(d) + ", |" + d.reg + "|, |" + t + "|");
            return;
        }
        default: break;
        }
        throw Error("unsupported instruction " + std::to_string(in.op));
    }

    // atan2(y, x) of one lane into dst: atan on [0, 1] by a degree-11 odd polynomial of min / max, then the
    // octant: pi/2 - a when |y| > |x|, pi - a when x < 0, negated when y < 0.
    void Atan2Lane(const std::string& dst, const std::string& y, const std::string& x) {
        auto t = New(), u = New(), m = New();
        Emit("MOV.F " + t + ".x, |" + y + "|");
        Emit("MOV.F " + t + ".y, |" + x + "|");
        Emit("MAX.F " + t + ".z, " + t + ".x, " + t + ".y");
        Emit("MIN.F " + t + ".w, " + t + ".x, " + t + ".y");
        Emit("DIV.F " + u + ".x, " + t + ".w, " + t + ".z");                       // a = min / max (0 / 0: NaN below)
        Emit("SEQ.F " + m + ".w, " + t + ".z, {0, 0, 0, 0}.x");
        Emit("MAD.F " + u + ".x, " + m + ".w, -" + u + ".x, " + u + ".x");          // max == 0: a = 0
        Emit("MUL.F " + u + ".y, " + u + ".x, " + u + ".x");                       // a^2
        Emit("MAD.F " + u + ".z, " + u + ".y, {-0.0117212, 0, 0, 0}.x, {0.05265332, 0, 0, 0}.x");
        Emit("MAD.F " + u + ".z, " + u + ".y, " + u + ".z, {-0.11643287, 0, 0, 0}.x");
        Emit("MAD.F " + u + ".z, " + u + ".y, " + u + ".z, {0.19354346, 0, 0, 0}.x");
        Emit("MAD.F " + u + ".z, " + u + ".y, " + u + ".z, {-0.33262347, 0, 0, 0}.x");
        Emit("MAD.F " + u + ".z, " + u + ".y, " + u + ".z, {0.99997723, 0, 0, 0}.x");
        Emit("MUL.F " + u + ".z, " + u + ".z, " + u + ".x");                       // p = atan(a)
        Emit("SGT.F " + m + ".x, " + t + ".x, " + t + ".y");                       // |y| > |x|
        Emit("ADD.F " + u + ".w, {1.57079637, 0, 0, 0}.x, -" + u + ".z");
        Emit("ADD.F " + u + ".w, " + u + ".w, -" + u + ".z");
        Emit("MAD.F " + u + ".z, " + m + ".x, " + u + ".w, " + u + ".z");           // p = |y| > |x| ? pi/2 - p : p
        Emit("SLT.F " + m + ".y, " + x + ", {0, 0, 0, 0}.x");                      // x < 0
        Emit("ADD.F " + u + ".w, {3.14159274, 0, 0, 0}.x, -" + u + ".z");
        Emit("ADD.F " + u + ".w, " + u + ".w, -" + u + ".z");
        Emit("MAD.F " + u + ".z, " + m + ".y, " + u + ".w, " + u + ".z");           // p = x < 0 ? pi - p : p
        Emit("SLT.F " + m + ".z, " + y + ", {0, 0, 0, 0}.x");                      // y < 0
        Emit("MAD.F " + m + ".z, " + m + ".z, {-2, 0, 0, 0}.x, {1, 0, 0, 0}.x");
        Emit("MUL.F " + dst + ", " + u + ".z, " + m + ".z");
    }

    void ExtInst(const Inst& in) {
        const auto& w = in.w;
        if (w[2] != glslStd_) throw Error("an extended instruction set other than GLSL.std.450");
        const auto type = w[0], id = w[1];
        // NMin, NMax, NClamp (NaN-aware forms an optimizer writes) are the plain float forms.
        const auto which = w[3] == 79 ? 37u : w[3] == 80 ? 40u : w[3] == 81 ? 43u : w[3];
        auto arg = [&](unsigned i) { return Get(w[4 + i]); };
        const unsigned n = Components(type);
        const char kind = ScalarKind(type);
        auto unary = [&](const std::string& op) { const auto a = arg(0); auto d = Def(id, n); Emit(op + " " + Dst(d) + ", " + Src(a, n)); };
        auto scalarEach = [&](const std::string& op) { const auto a = arg(0); PerComponent(op, id, type, a); };
        switch (which) {
        case 1: unary("ROUND.F"); return;                           // Round
        case 2: unary("ROUND.F"); return;                           // RoundEven
        case 3: unary("TRUNC.F"); return;                           // Trunc
        case 4: { const auto a = arg(0); auto d = Def(id, n); Emit((kind == 'F' ? "MOV.F " : "ABS.S ") + Dst(d) + ", " + (kind == 'F' ? "|" + a.reg + (a.n == 1 ? ".x" : "") + "|" : Src(a, n))); return; }  // FAbs, SAbs
        case 5: { const auto a = arg(0); auto d = Def(id, n); Emit("ABS.S " + Dst(d) + ", " + Src(a, n)); return; }
        case 6: {                                                   // FSign
            const auto a = arg(0); auto d = Def(id, n); auto t = New();
            Emit("SGT.F " + Dst(d) + ", " + Src(a, n) + ", {0, 0, 0, 0}");
            Emit("SLT.F " + t + Mask(n) + ", " + Src(a, n) + ", {0, 0, 0, 0}");
            Emit("ADD.F " + Dst(d) + ", " + d.reg + ", -" + t);
            return;
        }
        case 8: unary("FLR.F"); return;                             // Floor
        case 9: unary("CEIL.F"); return;                            // Ceil
        case 10: unary("FRC.F"); return;                            // Fract
        case 13: scalarEach("SIN.F"); return;                       // Sin
        case 14: scalarEach("COS.F"); return;                       // Cos
        case 15: {                                                  // Tan = sin / cos
            const auto a = arg(0); auto d = Def(id, n); auto c = New();
            for (unsigned k = 0; k < n; ++k) {
                Emit("SIN.F " + d.reg + "." + Comp(k) + ", " + SrcComp(a, k));
                Emit("COS.F " + c + "." + Comp(k) + ", " + SrcComp(a, k));
                Emit("DIV.F " + d.reg + "." + Comp(k) + ", " + d.reg + "." + Comp(k) + ", " + c + "." + Comp(k));
            }
            return;
        }
        case 17: {
            // Acos as NVIDIA's compiler writes it in the game's programs (Abramowitz and Stegun's polynomial, Cg's
            // acos): s = (((-0.0187293 |x| + 0.074261) |x| - 0.2121144) |x| + 1.5707288) sqrt(1 - |x|), pi - s for x < 0.
            const auto a = arg(0); auto d = Def(id, n);
            for (unsigned k = 0; k < n; ++k) {
                auto r = New();
                Emit("MOV.F " + r + ".w, " + SrcComp(a, k));
                Emit("MAD.F " + r + ".x, |" + r + ".w|, {-0.0187292993, 0, 0, 0}.x, {0.0742610022, 0, 0, 0}.x");
                Emit("MAD.F " + r + ".x, |" + r + ".w|, " + r + ".x, {-0.212114394, 0, 0, 0}.x");
                Emit("ADD.F " + r + ".y, -|" + r + ".w|, {1, 0, 0, 0}.x");
                Emit("MAD.F " + r + ".x, |" + r + ".w|, " + r + ".x, {1.57072878, 0, 0, 0}.x");
                Emit("SLT.F " + r + ".z, " + r + ".w, {0, 0, 0, 0}.x");
                Emit("RSQ.F " + r + ".y, " + r + ".y");
                Emit("DIV.F " + r + ".y, " + r + ".x, " + r + ".y");                    // s
                Emit("MUL.F " + r + ".x, " + r + ".z, " + r + ".y");                    // x < 0 ? s : 0
                Emit("MAD.F " + r + ".y, -" + r + ".x, {2, 0, 0, 0}.x, " + r + ".y");  // x < 0 ? -s : s
                Emit("MAD.F " + d.reg + "." + Comp(k) + ", " + r + ".z, {3.14159274, 0, 0, 0}.x, " + r + ".y");
            }
            return;
        }
        case 16: {                                                  // Asin = atan2(x, sqrt(1 - x x))
            const auto a = arg(0); auto d = Def(id, n);
            for (unsigned k = 0; k < n; ++k) {
                auto r = New();
                Emit("MAD.F " + r + ".x, -" + SrcComp(a, k) + ", " + SrcComp(a, k) + ", {1, 0, 0, 0}.x");
                Emit("MAX.F " + r + ".x, " + r + ".x, {0, 0, 0, 0}.x");
                Emit("RSQ.F " + r + ".y, " + r + ".x");
                Emit("RCP.F " + r + ".y, " + r + ".y");
                Emit("MOV.F " + r + ".z, " + SrcComp(a, k));
                Atan2Lane(d.reg + "." + Comp(k), r + ".z", r + ".y");
            }
            return;
        }
        case 18: {                                                  // Atan = atan2(x, 1)
            const auto a = arg(0); auto d = Def(id, n);
            for (unsigned k = 0; k < n; ++k) {
                auto r = New();
                Emit("MOV.F " + r + ".x, " + SrcComp(a, k));
                Atan2Lane(d.reg + "." + Comp(k), r + ".x", "{1, 0, 0, 0}.x");
            }
            return;
        }
        case 25: {                                                  // Atan2(y, x)
            const auto y = arg(0), x = arg(1); auto d = Def(id, n);
            for (unsigned k = 0; k < n; ++k) {
                auto r = New();
                Emit("MOV.F " + r + ".x, " + SrcComp(y, k));
                Emit("MOV.F " + r + ".y, " + SrcComp(x, k));
                Atan2Lane(d.reg + "." + Comp(k), r + ".x", r + ".y");
            }
            return;
        }
        case 26: { const auto a = arg(0), b = arg(1); PerComponent("POW.F", id, type, a, &b); return; }   // Pow
        case 27: {                                                  // Exp
            const auto a = arg(0); auto t = New(); Emit("MUL.F " + t + Mask(n) + ", " + Src(a, n) + ", {1.44269502, 0, 0, 0}.x");
            Val tv; tv.reg = t; tv.n = n; PerComponent("EX2.F", id, type, tv); return;
        }
        case 28: {                                                  // Log
            const auto a = arg(0); Val tv; tv.reg = New(); tv.n = n;
            for (unsigned c = 0; c < n; ++c) Emit("LG2.F " + tv.reg + "." + Comp(c) + ", " + SrcComp(a, c));
            auto d = Def(id, n); Emit("MUL.F " + Dst(d) + ", " + tv.reg + ", {0.693147182, 0, 0, 0}.x"); return;
        }
        case 29: scalarEach("EX2.F"); return;                       // Exp2
        case 30: scalarEach("LG2.F"); return;                       // Log2
        case 31: {                                                  // Sqrt: 1 / (1 / sqrt x), 0 stays 0
            const auto a = arg(0); Val tv; tv.reg = New(); tv.n = n;
            for (unsigned c = 0; c < n; ++c) Emit("RSQ.F " + tv.reg + "." + Comp(c) + ", " + SrcComp(a, c));
            PerComponent("RCP.F", id, type, tv); return;
        }
        case 32: scalarEach("RSQ.F"); return;                       // InverseSqrt
        case 37: { const auto a = arg(0), b = arg(1); Binary("MIN.F", id, type, a, b); return; }
        case 38: { const auto a = arg(0), b = arg(1); Binary("MIN.U", id, type, a, b); return; }
        case 39: { const auto a = arg(0), b = arg(1); Binary("MIN.S", id, type, a, b); return; }
        case 40: { const auto a = arg(0), b = arg(1); Binary("MAX.F", id, type, a, b); return; }
        case 41: { const auto a = arg(0), b = arg(1); Binary("MAX.U", id, type, a, b); return; }
        case 42: { const auto a = arg(0), b = arg(1); Binary("MAX.S", id, type, a, b); return; }
        case 43: case 44: case 45: {                                // FClamp, UClamp, SClamp
            const auto x = arg(0), lo = arg(1), hi = arg(2);
            const std::string k = which == 43 ? "F" : which == 44 ? "U" : "S";
            auto d = Def(id, n);
            Emit("MAX." + k + " " + Dst(d) + ", " + Src(x, n) + ", " + Src(lo, n));
            Emit("MIN." + k + " " + Dst(d) + ", " + d.reg + ", " + Src(hi, n));
            return;
        }
        case 46: {                                                  // FMix: a + (b - a) t
            const auto a = arg(0), b = arg(1), t = arg(2);
            auto diff = New();
            Emit("ADD.F " + diff + Mask(n) + ", " + Src(b, n) + ", " + Src(a, n, true));
            auto d = Def(id, n);
            Emit("MAD.F " + Dst(d) + ", " + diff + ", " + Src(t, n) + ", " + Src(a, n));
            return;
        }
        case 48: { const auto edge = arg(0), x = arg(1); auto d = Def(id, n); Emit("SGE.F " + Dst(d) + ", " + Src(x, n) + ", " + Src(edge, n)); return; }
        case 49: {                                                  // SmoothStep
            const auto e0 = arg(0), e1 = arg(1), x = arg(2);
            auto range = New(), t = New();
            Emit("ADD.F " + range + Mask(n) + ", " + Src(e1, n) + ", " + Src(e0, n, true));
            Emit("ADD.F " + t + Mask(n) + ", " + Src(x, n) + ", " + Src(e0, n, true));
            for (unsigned c = 0; c < n; ++c) Emit("DIV.F " + t + "." + Comp(c) + ", " + t + "." + Comp(c) + ", " + range + "." + Comp(c));
            Emit("MOV.F.SAT " + t + Mask(n) + ", " + t);
            auto d = Def(id, n);
            Emit("MAD.F " + range + Mask(n) + ", " + t + ", {-2, 0, 0, 0}.x, {3, 0, 0, 0}.x");
            Emit("MUL.F " + t + Mask(n) + ", " + t + ", " + t);
            Emit("MUL.F " + Dst(d) + ", " + t + ", " + range);
            return;
        }
        case 50: { const auto a = arg(0), b = arg(1), c = arg(2); auto d = Def(id, n); Emit("MAD.F " + Dst(d) + ", " + Src(a, n) + ", " + Src(b, n) + ", " + Src(c, n)); return; }
        case 62: { const auto a = arg(0); auto d = Def(id, 2); Emit("UP2H.F " + d.reg + ".xy, " + Src(a, 1)); return; }   // UnpackHalf2x16
        case 58: { const auto a = arg(0); auto d = Def(id, 1); Emit("PK2H.U " + d.reg + ".x, " + a.reg + ".xyxy"); return; }   // PackHalf2x16
        case 66: {                                                  // Length
            const auto a = arg(0); auto d = Def(id, 1);
            Emit(std::string(a.n == 4 ? "DP4.F " : a.n == 3 ? "DP3.F " : a.n == 2 ? "DP2.F " : "MUL.F ") + d.reg + ".x, " + Src(a, a.n) + ", " + Src(a, a.n));
            Emit("RSQ.F " + d.reg + ".x, " + d.reg + ".x");
            Emit("RCP.F " + d.reg + ".x, " + d.reg + ".x");
            return;
        }
        case 67: {                                                  // Distance
            const auto a = arg(0), b = arg(1); auto t = New(); auto d = Def(id, 1);
            Emit("ADD.F " + t + Mask(a.n) + ", " + Src(a, a.n) + ", " + Src(b, a.n, true));
            Emit(std::string(a.n == 4 ? "DP4.F " : a.n == 3 ? "DP3.F " : a.n == 2 ? "DP2.F " : "MUL.F ") + d.reg + ".x, " + t + ", " + t);
            Emit("RSQ.F " + d.reg + ".x, " + d.reg + ".x");
            Emit("RCP.F " + d.reg + ".x, " + d.reg + ".x");
            return;
        }
        case 33: {                                                  // Determinant of a 2x2, 3x3 or 4x4 matrix
            const auto m = arg(0); auto d = Def(id, 1);
            const auto& c = m.cols;
            if (c.size() == 2) {
                Emit("MUL.F " + d.reg + ".x, " + c[0] + ".x, " + c[1] + ".y");
                Emit("MAD.F " + d.reg + ".x, -" + c[0] + ".y, " + c[1] + ".x, " + d.reg + ".x");
            } else if (c.size() == 3) {
                // dot(c0, cross(c1, c2))
                auto x = New();
                Emit("MUL.F " + x + ".xyz, " + c[1] + ".zxyw, " + c[2] + ".yzxw");
                Emit("MAD.F " + x + ".xyz, " + c[1] + ".yzxw, " + c[2] + ".zxyw, -" + x);
                Emit("DP3.F " + d.reg + ".x, " + c[0] + ", " + x);
            } else if (c.size() == 4) {
                // By the 2x2 minors of the first two columns (s) and of the last two (u):
                // s01 u23 - s02 u13 + s03 u12 + s12 u03 - s13 u02 + s23 u01.
                const int pairs[6][2] = {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}};
                auto minor = [&](const std::string& a, const std::string& b, int i, int j) {
                    auto r = New();
                    Emit("MUL.F " + r + ".x, " + a + "." + Comp(i) + ", " + b + "." + Comp(j));
                    Emit("MAD.F " + r + ".x, -" + a + "." + Comp(j) + ", " + b + "." + Comp(i) + ", " + r + ".x");
                    return r;
                };
                std::string s[6], u[6];
                for (int k = 0; k < 6; ++k) { s[k] = minor(c[0], c[1], pairs[k][0], pairs[k][1]); u[k] = minor(c[2], c[3], pairs[k][0], pairs[k][1]); }
                const int other[6] = {5, 4, 3, 2, 1, 0};
                const bool negative[6] = {false, true, false, false, true, false};
                Emit("MUL.F " + d.reg + ".x, " + s[0] + ".x, " + u[other[0]] + ".x");
                for (int k = 1; k < 6; ++k)
                    Emit("MAD.F " + d.reg + ".x, " + (negative[k] ? "-" : "") + s[k] + ".x, " + u[other[k]] + ".x, " + d.reg + ".x");
            } else throw Error("a determinant of a matrix that is not square 2, 3 or 4");
            return;
        }
        case 68: {                                                  // Cross
            const auto a = arg(0), b = arg(1); auto d = Def(id, 3);
            Emit("MUL.F " + d.reg + ".xyz, " + a.reg + ".zxyw, " + b.reg + ".yzxw");
            Emit("MAD.F " + d.reg + ".xyz, " + a.reg + ".yzxw, " + b.reg + ".zxyw, -" + d.reg);
            return;
        }
        case 69: {                                                  // Normalize
            const auto a = arg(0); auto t = New(); auto d = Def(id, n);
            Emit(std::string(n == 4 ? "DP4.F " : n == 3 ? "DP3.F " : n == 2 ? "DP2.F " : "MUL.F ") + t + ".x, " + Src(a, n) + ", " + Src(a, n));
            Emit("RSQ.F " + t + ".x, " + t + ".x");
            Emit("MUL.F " + Dst(d) + ", " + Src(a, n) + ", " + t + ".x");
            return;
        }
        case 71: {                                                  // Reflect: I - 2 dot(N, I) N
            const auto i = arg(0), nn = arg(1); auto t = New(); auto d = Def(id, n);
            Emit(std::string(n == 4 ? "DP4.F " : n == 3 ? "DP3.F " : "DP2.F ") + t + ".x, " + Src(nn, n) + ", " + Src(i, n));
            Emit("MUL.F " + t + ".x, " + t + ".x, {-2, 0, 0, 0}.x");
            Emit("MAD.F " + Dst(d) + ", " + Src(nn, n) + ", " + t + ".x, " + Src(i, n));
            return;
        }
        default: break;
        }
        throw Error("unsupported GLSL.std.450 instruction " + std::to_string(which));
    }

    // ----- the program's declarations -----
    // Register reuse by liveness over the program's control flow (IF/ELSE/ENDIF, REP/ENDREP, BRK, CONT), per
    // component: a temporary's component is live from a write to the reads that write can reach. Two temporaries
    // interfere when one is written while a component of the other is live; interfering temporaries get different
    // registers (R0, R1, ...). A read of a component no write of that temporary ever writes is not a read.
    std::string Allocate(const std::string& body) {
        std::vector<std::string> lines;
        { std::istringstream in(body); std::string l; while (std::getline(in, l)) lines.push_back(l); }
        const std::size_t n = lines.size();
        // Each line's temporaries: (temporary, component mask) written, and read.
        struct Access { unsigned v; unsigned mask; };
        std::vector<std::vector<Access>> writes(n), reads(n);
        std::map<unsigned, unsigned> written;           // temporary -> components it is ever written
        unsigned temps = 0;
        auto maskOf = [](const std::string& l, std::size_t at) {
            if (at >= l.size() || l[at] != '.') return 15u;
            unsigned m = 0;
            for (std::size_t k = at + 1; k < l.size(); ++k) {
                const char c = l[k];
                if (c == 'x') m |= 1; else if (c == 'y') m |= 2; else if (c == 'z') m |= 4; else if (c == 'w') m |= 8;
                else break;
            }
            return m ? m : 15u;
        };
        for (std::size_t i = 0; i < n; ++i) {
            const auto& l = lines[i];
            const auto space = l.find(' ');
            const std::string op = l.substr(0, space == std::string::npos ? l.size() : space);
            const bool control = op == "IF" || op == "ELSE" || op == "ENDIF" || op == "REP" || op.rfind("REP.", 0) == 0 ||
                                 op == "ENDREP" || op == "BRK" || op == "CONT" || op.rfind("KIL", 0) == 0 || op == "RET";
            const std::size_t operands = space == std::string::npos ? l.size() : space + 1;
            const std::size_t firstComma = l.find(',', operands);
            for (std::size_t k = operands; k + 1 < l.size(); ++k) {
                if (l[k] != 'V' || !std::isdigit(static_cast<unsigned char>(l[k + 1]))) continue;
                if (k && (std::isalnum(static_cast<unsigned char>(l[k - 1])) || l[k - 1] == '_')) continue;
                std::size_t j = k + 1; unsigned v = 0;
                while (j < l.size() && std::isdigit(static_cast<unsigned char>(l[j]))) v = v * 10 + unsigned(l[j++] - '0');
                temps = std::max(temps, v + 1);
                const bool isDest = !control && k == operands && (firstComma == std::string::npos || k < firstComma);
                const unsigned m = maskOf(l, j);
                if (isDest) { writes[i].push_back({v, m}); written[v] |= m; }
                else reads[i].push_back({v, m});
                k = j - 1;
            }
        }
        // Control flow: successors of each line.
        std::vector<std::vector<std::size_t>> next(n);
        std::vector<std::size_t> ifs, elses, reps;
        std::vector<std::size_t> matchElse(n, SIZE_MAX), matchEnd(n, SIZE_MAX), loopEnd(n, SIZE_MAX), loopStart(n, SIZE_MAX);
        {
            std::vector<std::size_t> stack, loops;
            for (std::size_t i = 0; i < n; ++i) {
                const auto& l = lines[i];
                if (l.rfind("IF ", 0) == 0) stack.push_back(i);
                else if (l.rfind("ELSE", 0) == 0) matchElse[stack.back()] = i;
                else if (l.rfind("ENDIF", 0) == 0) { const auto o = stack.back(); stack.pop_back(); matchEnd[o] = i; if (matchElse[o] != SIZE_MAX) matchEnd[matchElse[o]] = i; }
                else if (l.rfind("REP", 0) == 0) loops.push_back(i);
                else if (l.rfind("ENDREP", 0) == 0) { const auto o = loops.back(); loops.pop_back(); loopEnd[o] = i; loopStart[i] = o; }
            }
        }
        {
            std::vector<std::size_t> loops;
            for (std::size_t i = 0; i < n; ++i) {
                const auto& l = lines[i];
                auto add = [&](std::size_t t) { if (t < n) next[i].push_back(t); };
                if (l.rfind("IF ", 0) == 0) { add(i + 1); add(matchElse[i] != SIZE_MAX ? matchElse[i] + 1 : matchEnd[i]); }
                else if (l.rfind("ELSE", 0) == 0) add(matchEnd[i]);
                else if (l.rfind("REP", 0) == 0) { loops.push_back(i); add(i + 1); add(loopEnd[i] + 1); }
                else if (l.rfind("ENDREP", 0) == 0) { add(loopStart[i] + 1); add(i + 1); loops.pop_back(); }
                else if (l.rfind("BRK", 0) == 0) add(loopEnd[loops.back()] + 1);
                else if (l.rfind("CONT", 0) == 0) add(loopEnd[loops.back()]);
                else if (l.rfind("RET", 0) == 0) {}
                else add(i + 1);
            }
        }
        // Liveness, a bit per (temporary, component), to a fixed point.
        const std::size_t bits = std::size_t(temps) * 4, words = (bits + 63) / 64;
        std::vector<std::vector<std::uint64_t>> liveIn(n, std::vector<std::uint64_t>(words, 0)), liveOut(n, std::vector<std::uint64_t>(words, 0));
        auto setBit = [](std::vector<std::uint64_t>& b, std::size_t k) { b[k / 64] |= std::uint64_t(1) << (k % 64); };
        auto clearBit = [](std::vector<std::uint64_t>& b, std::size_t k) { b[k / 64] &= ~(std::uint64_t(1) << (k % 64)); };
        for (bool changed = true; changed;) {
            changed = false;
            for (std::size_t i = n; i-- > 0;) {
                std::vector<std::uint64_t> out(words, 0);
                for (auto s : next[i]) for (std::size_t w = 0; w < words; ++w) out[w] |= liveIn[s][w];
                std::vector<std::uint64_t> in = out;
                for (const auto& a : writes[i]) for (unsigned c = 0; c < 4; ++c) if (a.mask >> c & 1) clearBit(in, a.v * 4 + c);
                for (const auto& a : reads[i]) {
                    const unsigned m = a.mask & written[a.v];
                    for (unsigned c = 0; c < 4; ++c) if (m >> c & 1) setBit(in, a.v * 4 + c);
                }
                if (in != liveIn[i] || out != liveOut[i]) { liveIn[i] = std::move(in); liveOut[i] = std::move(out); changed = true; }
            }
        }
        // Interference: a temporary written while another's component is live (after the line, or read by it).
        std::vector<std::set<unsigned>> conflicts(temps);
        std::vector<std::size_t> firstWrite(temps, SIZE_MAX);
        for (std::size_t i = 0; i < n; ++i) {
            if (writes[i].empty()) continue;
            std::vector<std::uint64_t> live = liveOut[i];
            for (const auto& a : reads[i]) { const unsigned m = a.mask & written[a.v]; for (unsigned c = 0; c < 4; ++c) if (m >> c & 1) setBit(live, a.v * 4 + c); }
            for (const auto& d : writes[i]) {
                firstWrite[d.v] = std::min(firstWrite[d.v], i);
                for (std::size_t w = 0; w < words; ++w) {
                    const std::uint64_t word = live[w];
                    if (!word) continue;
                    for (unsigned bit = 0; bit < 64; ++bit) {
                        if (!(word >> bit & 1)) continue;
                        const unsigned other = unsigned((w * 64 + bit) / 4);
                        if (other != d.v) { conflicts[d.v].insert(other); conflicts[other].insert(d.v); }
                    }
                }
            }
        }
        // Registers: temporaries in order of first write, each the lowest register no interfering one has.
        std::vector<unsigned> order;
        for (unsigned v = 0; v < temps; ++v) if (firstWrite[v] != SIZE_MAX || written.count(v)) order.push_back(v);
        std::stable_sort(order.begin(), order.end(), [&](unsigned a, unsigned b) { return firstWrite[a] < firstWrite[b]; });
        std::map<unsigned, unsigned> reg;
        unsigned count = 0;
        for (unsigned v : order) {
            if (!options_.reuse_registers) { reg[v] = count++; continue; }
            std::set<unsigned> taken;
            for (unsigned o : conflicts[v]) { auto it = reg.find(o); if (it != reg.end()) taken.insert(it->second); }
            unsigned r = 0;
            while (taken.count(r)) ++r;
            reg[v] = r;
            count = std::max(count, r + 1);
        }
        registers_ = count;
        std::string out;
        for (const auto& l : lines) {
            std::string r;
            for (std::size_t i = 0; i < l.size();) {
                if (l[i] == 'V' && i + 1 < l.size() && std::isdigit(static_cast<unsigned char>(l[i + 1])) &&
                    !(i && (std::isalnum(static_cast<unsigned char>(l[i - 1])) || l[i - 1] == '_'))) {
                    std::size_t j = i + 1; unsigned v = 0;
                    while (j < l.size() && std::isdigit(static_cast<unsigned char>(l[j]))) v = v * 10 + unsigned(l[j++] - '0');
                    auto it = reg.find(v);
                    if (it == reg.end()) { it = reg.emplace(v, count).first; registers_ = ++count; }   // read, never written
                    r += "R" + std::to_string(it->second);
                    i = j;
                } else r += l[i++];
            }
            out += r + "\n";
        }
        return out;
    }
    unsigned registers_ = 0;

    std::string Header() {
        std::ostringstream h;
        h << (stage_ == Stage::Fragment ? "!!NVfp5.0\n" : stage_ == Stage::Vertex ? "!!NVvp5.0\n" : "!!NVgp5.0\n");
        h << "OPTION NV_internal;\nOPTION NV_gpu_program_fp64;\n";
        if (!usedStorage_.empty()) h << "OPTION NV_shader_storage_buffer;\n";
        h << "OPTION NV_bindless_texture;\n";
        if (stage_ == Stage::Geometry) {
            h << "PRIMITIVE_IN " << gsIn_ << ";\nPRIMITIVE_OUT " << gsOut_ << ";\nVERTICES_OUT " << gsVertices_ << ";\n";
            if (gsInvocations_ > 1) h << "INVOCATIONS " << gsInvocations_ << ";\n";
        }
        for (int b : usedStorage_) h << "STORAGE sbo_buf" << b << "[] = { program.storage[" << b << "] };\n";
        std::set<int> buffers = usedBuffers_;
        if (usesHandles_) buffers.insert(14);
        for (int b : buffers) h << "CBUFFER buf" << b << "[] = { program.buffer[" << b << "] };\n";
        if (stage_ == Stage::Fragment) {
            h << "ATTRIB fragment_position = fragment.position;\n";
            for (int a : attribs_)
                h << (noPerspectiveLocs_.count(a) ? "NOPERSPECTIVE " : "") << "ATTRIB fragment_attrib" << a << "[] = { fragment.attrib[" << a << ".." << a << "] };\n";
            for (int a : flatAttribs_) h << "FLAT ATTRIB fragment_attrib" << a << "[] = { fragment.attrib[" << a << ".." << a << "] };\n";
        }
        if (registers_) {
            h << "TEMP ";
            for (unsigned r = 0; r < registers_; ++r) h << (r ? ", " : "") << "R" << r;
            h << ";\n";
        }
        h << "LONG TEMP D0, D1;\nTEMP RC;\nSHORT TEMP HC;\n";
        if (stage_ == Stage::Fragment) for (int c : colorOutputs_) h << "OUTPUT result_color" << c << " = result.color" << (c ? "[" + std::to_string(c) + "]" : std::string()) << ";\n";
        return h.str();
    }
};

}  // namespace detail

inline Result Translate(const std::vector<std::uint32_t>& words, const Options& options = {}) {
    detail::Translator t(words, options);
    return t.Run();
}

}  // namespace spirv_front

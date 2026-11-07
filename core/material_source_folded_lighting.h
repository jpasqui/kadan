// Port of material_folded_lighting_contract.py's recovery (eligible, prepare, contract, material_trace,
// material_values, captured, recover, joint_formulas, resolve_defaults, quotient_formulas) and of
// material_underidentified_lighting.diagnosis, for the water and Bubble effect masters whose
// translucent per-vertex directional lighting the compiler folded into constants.
//
// Their shaders hold no BaseColor, Metallic or Specular: outside reflection captures the diffuse
// carrier is an unconditional zero, inside them the constant 0.018. The graph therefore keeps
// Unreal's standard defaults for those pins (checked against the cooked master, which retains none
// of them) and recovers EmissiveColor, Opacity and Normal from the real native consumers. The
// numeric carrier proof, the whole-program interface closure and the ambient-occlusion kernels of
// the Python module stay with the oracle; this stage gives a graph, never a proof.
#pragma once
#include "material_source_particle_reflection_owner.h"
#include "material_engine_hashes.h"
#include "material_source_folded_varyings.h"
#include "material_source_bubble_contracts.h"
#include "material_source_common.h"
namespace P4MapKit
{
namespace Material
{
namespace FoldedLightingDetail
{
inline std::string RenderSetting(const Json& settings, const char* name)
{
    if (!settings.Has("render_settings") || !settings.At("render_settings").Has(name))
        return {};
    const auto& value = settings.At("render_settings").At(name);
    return value.kind == Json::String ? value.string : std::string();
}
inline bool FullyRough(const Json& settings)
{
    return settings.Has("render_settings") && settings.At("render_settings").Has("bFullyRough") &&
           settings.At("render_settings").At("bFullyRough").kind == Json::Boolean &&
           settings.At("render_settings").At("bFullyRough").boolean;
}
inline bool Float32Equal(double a, double b)
{
    const float x = float(a), y = float(b);
    std::uint32_t p, q;
    std::memcpy(&p, &x, 4);
    std::memcpy(&q, &y, 4);
    return p == q;
}
inline bool ViewCaptureLoad(const AnchorInstruction& line, const MaterialPreparedProgram& program)
{
    if (line.op != "LDC")
        return false;
    const auto ops = BubbleSourceDetail::OperandTexts(line.text);
    if (ops.size() < 2)
        return false;
    for (const auto& buffer : program.BaseBinding.Buffers)
        if (buffer.second == "View" && ops[1] == "buf" + std::to_string(buffer.first) + "[3436]")
            return true;
    return false;
}
}
// material_underidentified_lighting.diagnosis: the actual named View capture read and constant fold of
// a known collapsed policy. Empty for every other program; it authorizes nothing by itself.
struct MaterialFoldedDiagnosis
{
    bool Found = false;
    std::vector<std::size_t> FoldLines, CaptureLines;
};
inline MaterialFoldedDiagnosis DiagnoseOriginalFoldedLighting(const std::vector<AnchorInstruction>& code,
                                                              const MaterialPreparedProgram& program,
                                                              const Json& settings, const MaterialAnchors& anchors)
{
    MaterialFoldedDiagnosis result;
    if (settings.At("domain").Text() != "MD_Surface" || settings.At("shading_model").Text() != "MSM_DefaultLit" ||
        anchors.count("BaseColor") || anchors.count("stop"))
        return result;
    const bool rough = FoldedLightingDetail::FullyRough(settings);
    const auto mode = FoldedLightingDetail::RenderSetting(settings, "TranslucencyLightingMode");
    for (std::size_t i = 0; i < code.size(); ++i)
    {
        const auto& line = code[i];
        for (const auto& operand : line.src)
        {
            if (!operand.literal || operand.negative || operand.absolute)
                continue;
            const auto value = operand.scalar();
            if ((!rough && line.op == "MOV" && line.mask.size() == 3 && std::fabs(value - .018) < 1e-7) ||
                (rough && line.op == "MUL" && line.mask == "xyz" &&
                 std::fabs(value - .018 / 3.14159265358979323846) < 1e-9))
                result.FoldLines.push_back(i);
        }
        if (FoldedLightingDetail::ViewCaptureLoad(line, program))
            result.CaptureLines.push_back(i);
    }
    if (result.FoldLines.empty())
        return {};
    if (!rough && (mode != "TLM_VolumetricPerVertexDirectional" || result.CaptureLines.empty()))
        return {};
    result.Found = true;
    return result;
}
// contract(): the actual native consumer boundaries, with no synthetic raw lighting anchors.
struct MaterialFoldedContract
{
    std::size_t FoldLine = 0, ZeroLine = 0, BranchLine = 0, CaptureLine = 0, Boundary = 0;
    std::string Register, Comps;
    MaterialEmissiveAnchor Emissive;
    bool HasNormal = false, NormalFromSH = false;
    MaterialAnchor Normal;
    bool HasOpacity = false;
    MaterialScheduledAnchor Opacity;
};
inline MaterialFoldedContract CheckOriginalFoldedLightingContract(const std::vector<AnchorInstruction>& code,
                                                                  const MaterialPreparedProgram& program,
                                                                  const Json& settings, const MaterialAnchors& found,
                                                                  const MaterialBubbleNormalSeal& seal,
                                                                  const std::vector<std::string>& lines)
{
    const char* name = "folded lighting contract";
    if (settings.At("domain").Text() != "MD_Surface" || settings.At("shading_model").Text() != "MSM_DefaultLit" ||
        settings.At("blend_mode").Text() != "BLEND_Translucent" ||
        FoldedLightingDetail::RenderSetting(settings, "TranslucencyLightingMode") !=
            "TLM_VolumetricPerVertexDirectional")
        throw Refused("folded canonical lighting needs the exact cooked per-vertex directional-volume policy", name);
    const auto diagnosis = DiagnoseOriginalFoldedLighting(code, program, settings, found);
    if (!diagnosis.Found)
        throw Refused("no source-identified folded lighting contract in this native program", name);
    if (FoldedLightingDetail::FullyRough(settings))
        throw Refused("fully rough folded carrier needs its direct Lambert consumer contract", name);
    std::vector<std::size_t> candidates;
    for (const auto at : diagnosis.FoldLines)
    {
        const auto& line = code[at];
        const std::set<char> distinct(line.mask.begin(), line.mask.end());
        if (line.op != "MOV" || line.mods != std::vector<std::string>{"F"} ||
            !BubbleSourceDetail::Unconditional(line) || line.mask.size() != 3 || distinct.size() != 3 ||
            line.src.empty() || !line.src[0].literal ||
            !FoldedLightingDetail::Float32Equal(line.src[0].scalar(), .018) || at + 1 >= code.size() ||
            code[at + 1].op != "ENDIF")
            continue;
        candidates.push_back(at);
    }
    if (candidates.size() != 1)
        throw Refused("folded lighting does not have one exact capture-expanded diffuse carrier", name);
    MaterialFoldedContract result;
    const auto at = candidates[0];
    result.FoldLine = at;
    result.Register = code[at].dst;
    result.Comps = code[at].mask;
    result.Boundary = at + 2;
    if (at < 1)
        throw Refused("capture carrier has no zero initialization", name);
    bool written = false;
    for (std::size_t i = 0; i + 1 < at; ++i)
        if (code[i].dst == result.Register && code[i].mask.find_first_of(result.Comps) != std::string::npos)
        {
            written = true;
            result.ZeroLine = i;
        }
    if (!written)
        throw Refused("capture carrier has no zero initialization", name);
    const auto depths = MaterialControlDepths(code);
    const auto& zero = code[result.ZeroLine];
    const auto& branch = code[at - 1];
    result.BranchLine = at - 1;
    if (zero.op != "MOV" || zero.dst != result.Register || zero.mask != result.Comps ||
        zero.mods != std::vector<std::string>{"F"} || zero.src.empty() || !zero.src[0].literal ||
        zero.src[0].scalar() != 0. || branch.text != "IF NE.x" || depths[at - 1] != 0 || depths[result.ZeroLine] != 0 ||
        !BubbleSourceDetail::Unconditional(zero))
        throw Refused("capture carrier is not exactly unconditional zero followed by the capture-only branch", name);
    bool condition = false;
    for (std::size_t i = 0; i + 1 < at; ++i)
        if (code[i].has("CC") && code[i].mask.find('x') != std::string::npos)
        {
            condition = true;
            result.CaptureLine = i;
        }
    if (!condition)
        throw Refused("capture branch has no proven condition-register producer", name);
    const auto& capture = code[result.CaptureLine];
    if (capture.op != "LDC" || capture.mods != std::vector<std::string>{"F32", "CC"} || capture.dst != "HC" ||
        capture.mask != "x" || capture.src.size() != 1 || depths[result.CaptureLine] != 0 ||
        !BubbleSourceDetail::Unconditional(capture) || !FoldedLightingDetail::ViewCaptureLoad(capture, program))
        throw Refused("capture condition is not the exact named View.RenderingReflectionCaptureMask load", name);
    const auto stop = result.Boundary;
    result.Emissive = FindScheduledMaterialEmissiveAnchor(code, stop);
    if (!result.Emissive.Found)
        throw Refused("folded lighting has no exact native emissive clamp/add consumer", name);
    for (const auto* pin : {"NormalWorld", "Normal", "NormalDecal"})
    {
        const auto normal = found.find(pin);
        if (normal != found.end())
        {
            result.HasNormal = true;
            result.Normal = normal->second;
            break;
        }
    }
    if (!result.HasNormal)
    {
        const auto sh = FindOriginalBubbleNormal(code, seal, lines);
        if (sh.Found)
        {
            result.HasNormal = true;
            result.NormalFromSH = true;
            result.Normal = sh.Anchor;
        }
    }
    std::vector<std::size_t> alpha;
    for (std::size_t i = 0; i < code.size(); ++i)
        if (code[i].dst == "result_color0" && code[i].mask == "w")
            alpha.push_back(i);
    if (alpha.size() != 1 || code[alpha[0]].op != "MOV" || code[alpha[0]].src.size() != 1)
        throw Refused("folded translucent alpha does not have one direct native output consumer", name);
    const auto& output = code[alpha[0]];
    const auto& operand = output.src[0];
    if (operand.negative || operand.absolute || operand.literal)
        throw Refused("folded alpha is not a direct material opacity carrier", name);
    const auto read = output.reads(0, "w");
    result.Opacity =
        BuildDelayedMaterialAnchor(code, stop, alpha[0], AnchorOperand(read.first + "." + read.second), 'w');
    result.HasOpacity = true;
    if (found.count("Roughness") || found.count("AmbientOcclusion"))
        throw Refused("folded scalar BSDF consumer needs a separate exact pre-consumer contract", name);
    return result;
}
// material_trace(): undefined original lanes stay symbolic through both branch merges, and native
// arithmetic on them stays undefined (seed_native_inputs, tainted_step).
class OriginalFoldedSymbolicMachine : public OriginalParticleLitSymbolicMachine
{
public:
    std::shared_ptr<std::map<const Expression*, bool>> undefined =
        std::make_shared<std::map<const Expression*, bool>>();
    OriginalFoldedSymbolicMachine(const std::vector<std::string>& l, std::shared_ptr<SymbolicArena> a, InputBounds b)
        : OriginalParticleLitSymbolicMachine(l, std::move(a), std::move(b))
    {
        for (int c = 0; c < 4; ++c)
            cc[c] = arena->mk("UNINITIALIZED_NATIVE_CC", {c});
        static const std::regex name("^[A-Za-z_]\\w*(?:\\[\\d+\\])?$"), indexed("\\[([A-Za-z_]\\w*)\\.[xyzw]\\]");
        std::set<std::string> names;
        for (const auto& text : lines)
        {
            const AnchorInstruction line(text);
            if (!line.dst.empty())
                names.insert(line.dst);
            for (const auto& operand : line.src)
                if (!operand.base.empty())
                    names.insert(operand.base);
            for (std::sregex_iterator item(text.begin(), text.end(), indexed), end; item != end; ++item)
                names.insert((*item)[1].str());
        }
        for (const auto& item : names)
            if (std::regex_match(item, name) && item != "TR" && item != "FL")
            {
                Vec value;
                for (int c = 0; c < 4; ++c)
                    value[c] = arena->mk("UNINITIALIZED_NATIVE_REGISTER", {item, c});
                reg[item] = value;
            }
    }
    bool unknown(const Value& value)
    {
        if (!symbolic(value))
            return false;
        struct Frame
        {
            Value Node;
            std::size_t Next = 0;
        };
        std::vector<Frame> stack = {{value, 0}};
        while (!stack.empty())
        {
            auto& frame = stack.back();
            const auto& x = *frame.Node.expression;
            if (undefined->count(&x))
            {
                stack.pop_back();
                continue;
            }
            const std::size_t width = x.op == "TEX" && x.args.size() >= 2 && x.args[0].kind == Value::Text &&
                                              x.args[0].text == "TEX" && x.args[1].kind == Value::Text &&
                                              x.args[1].text == "2D"
                                          ? std::min<std::size_t>(6, x.args.size())
                                          : x.args.size();
            if (frame.Next < width)
            {
                const auto child = x.args[frame.Next++];
                if (symbolic(child) && !undefined->count(child.expression.get()))
                    stack.push_back({child, 0});
                continue;
            }
            bool result = starts(x.op, "UNINITIALIZED_NATIVE");
            for (std::size_t i = 0; i < width && !result; ++i)
                result = symbolic(x.args[i]) && undefined->at(x.args[i].expression.get());
            (*undefined)[&x] = result;
            stack.pop_back();
        }
        return undefined->at(value.expression.get());
    }
    void step(std::size_t index, const std::string& text) override
    {
        const AnchorInstruction line(text);
        std::map<int, Value> taint;
        if (!line.dst.empty() && !IsMember(line.op, {"LDC", "LDB", "TEX", "TXL", "TXB", "TXF", "TXP"}))
            for (const auto c : line.mask)
            {
                const auto k = std::string("xyzw").find(c);
                if (k == std::string::npos)
                    continue;
                bool tainted = false;
                for (const auto& item : MaterialInstructionReads(line, std::string(1, c)))
                {
                    Value input;
                    if (item.first == "CC")
                        input = cc[std::string("xyzw").find(item.second)];
                    else
                    {
                        const auto found = reg.find(item.first);
                        if (found == reg.end())
                            continue;
                        input = found->second[std::string("xyzw").find(item.second)];
                    }
                    if (unknown(input))
                    {
                        tainted = true;
                        break;
                    }
                }
                if (tainted)
                    taint[int(k)] = arena->mk("UNINITIALIZED_NATIVE_ARITHMETIC", {int(index), std::string(1, c)});
            }
        OriginalParticleLitSymbolicMachine::step(index, text);
        for (const auto& item : taint)
        {
            reg[line.dst][std::size_t(item.first)] = item.second;
            if (line.has("CC"))
                cc[std::size_t(item.first)] = item.second;
        }
    }
    void run_folded(std::size_t start, std::size_t end, std::size_t stop)
    {
        end = std::min(end, stop);
        for (std::size_t pc = start; pc < end; ++pc)
        {
            if (starts(lines[pc], "IF "))
            {
                const auto test = condition(lines[pc].substr(3))[0];
                const auto range = blocks(pc);
                if (symbolic(test))
                {
                    if (stop != std::numeric_limits<std::size_t>::max() && pc < stop && stop <= range.second)
                        throw EvalError("the stop line is inside an IF whose condition is not a number");
                    auto a = *this, b = *this;
                    a.path.push_back({test, true});
                    b.path.push_back({test, false});
                    a.run_folded(pc + 1, range.first == lines.size() ? range.second : range.first,
                                 std::numeric_limits<std::size_t>::max());
                    if (range.first != lines.size())
                        b.run_folded(range.first + 1, range.second, std::numeric_limits<std::size_t>::max());
                    std::set<std::string> names;
                    for (const auto& r : a.reg)
                        names.insert(r.first);
                    for (const auto& r : b.reg)
                        names.insert(r.first);
                    for (const auto& item : names)
                    {
                        const auto av = a.registered(item), bv = b.registered(item);
                        Vec value;
                        for (int c = 0; c < 4; ++c)
                            value[c] = select(test, av[c], bv[c]);
                        reg[item] = value;
                    }
                    for (int c = 0; c < 4; ++c)
                        cc[c] = select(test, a.cc[c], b.cc[c]);
                    pc = range.second;
                }
                else if (!test.truth())
                    pc = range.first == lines.size() ? range.second : range.first;
            }
            else if (lines[pc] == "ELSE")
                pc = skip(pc, {"ENDIF"});
            else if (lines[pc] != "ENDIF")
                step(pc, lines[pc]);
        }
    }
    OriginalFoldedSymbolicMachine& run(std::size_t stop = std::numeric_limits<std::size_t>::max()) override
    {
        run_folded(0, lines.size(), stop);
        return *this;
    }
};
// material_values(): an actual material producer never reads an undefined native lane, and only the
// unread packed coordinates of an ordinary 2D sample are removed before binding.
inline std::vector<Value> OriginalFoldedMaterialValues(SymbolicArena& arena, const std::vector<Value>& values)
{
    for (const auto& node : walk_formulas(values))
        if (starts(node.expression->op, "UNINITIALIZED_NATIVE"))
        {
            // semantic_nodes ignores the packed z/w of an ordinary 2D sample; a lane read only there is not consumed.
            bool consumed = false;
            std::set<const Expression*> seen;
            std::vector<Value> stack(values.begin(), values.end());
            while (!stack.empty() && !consumed)
            {
                const auto v = stack.back();
                stack.pop_back();
                if (!symbolic(v) || !seen.insert(v.expression.get()).second)
                    continue;
                const auto& x = *v.expression;
                if (starts(x.op, "UNINITIALIZED_NATIVE"))
                {
                    consumed = true;
                    break;
                }
                const std::size_t width = x.op == "TEX" && x.args.size() >= 2 && x.args[0].kind == Value::Text &&
                                                  x.args[0].text == "TEX" && x.args[1].kind == Value::Text &&
                                                  x.args[1].text == "2D"
                                              ? std::min<std::size_t>(6, x.args.size())
                                              : x.args.size();
                for (std::size_t i = 0; i < width; ++i)
                    stack.push_back(x.args[i]);
            }
            if (consumed)
                throw Refused("actual material producer consumes an uninitialized native lane",
                              "folded material inputs");
            break;
        }
    return rewrite(arena, values,
                   [&](const Value& value)
                   {
                       if (is(value, "TEX") && Args(value).size() == 8 && Args(value)[0].kind == Value::Text &&
                           Args(value)[0].text == "TEX" && Args(value)[1].kind == Value::Text &&
                           Args(value)[1].text == "2D")
                       {
                           auto args = Args(value);
                           args[6] = Value(0.);
                           args[7] = Value(0.);
                           return arena.mk("TEX", args);
                       }
                       return value;
                   });
}
// The program one folded capture reads: the canonical original reference, its preparation and the
// prepare() identity checks (aliases, storage, View layout, Bubble clip, SH binding, engine varyings).
struct MaterialFoldedProgram
{
    MaterialOriginalParticleLitProgram Lit;
    MaterialBubbleClipContract Clip;
    MaterialBubbleNormalSeal NormalSeal;
    MaterialFoldedEngineVaryings Varyings;
    std::string Factory;
};
inline MaterialFoldedProgram
PrepareOriginalFoldedProgram(const MaterialOriginalOwnerSource& owner, const FMaterialShaderReference& ref,
                             const MaterialOriginalArchiveAccess& archive, MaterialDomainSourceScope scope,
                             const std::function<void(SymbolicArena&, BindingContext&)>& engineBinding)
{
    MaterialFoldedProgram result;
    result.Lit = PrepareOriginalParticleLitProgram(owner, ref, archive, scope, engineBinding);
    const auto& program = result.Lit.Source.Program;
    const auto& plan = result.Lit.Plan.Source.Recovery;
    result.Factory = MaterialFactoryName(program.Reference.VertexFactoryHash);
    // The original material-map aliases of this program agree on their named buffers.
    std::vector<FMaterialShaderReference> refs;
    FReadError error;
    if (!ReadMaterialShaderReferences(owner.Map, refs, error))
        throw Refused(error.Code, "folded lighting program");
    const auto indices = archive.MapIndices(owner.Map.ResourceHash);
    const auto native = [&](const FMaterialShaderReference& r)
    {
        return r.ResourceIndex >= 0 && std::size_t(r.ResourceIndex) < indices.size()
                   ? std::int64_t(indices[std::size_t(r.ResourceIndex)])
                   : std::int64_t(-1);
    };
    bool typed = false;
    for (const auto& alias : refs)
        if (alias.Frequency == 3 && alias.VertexFactoryHash == program.Reference.VertexFactoryHash &&
            native(alias) == native(program.Reference) &&
            IsBasePassShaderName(MaterialOriginalSourceShaderName(alias.TypeHash)))
        {
            if (alias.TypeHash == program.Reference.TypeHash)
                typed = true;
            if (alias.UniformBufferHashes != program.Reference.UniformBufferHashes ||
                alias.NamedBuffers != program.Reference.NamedBuffers)
                throw Refused("folded named buffer identities disagree with original pixel shader metadata",
                              "folded lighting program");
        }
    if (!typed)
        throw Refused("folded shader has no original material-map reference", "folded lighting program");
    std::set<std::string> storage;
    for (const auto& declaration : result.Lit.Plan.Source.StorageDeclarations)
        if (!storage.insert(declaration.Name).second)
            throw Refused("unproved original storage alias declaration", "folded lighting program");
    result.Clip = PrepareOriginalBubbleClipContract(owner, program, plan);
    result.NormalSeal = SealOriginalBubbleNormal(program, plan.Lines);
    for (const auto& buffer : program.BaseBinding.Buffers)
        if (buffer.second == "View" && !BubbleSourceDetail::GameView(program, buffer.first))
            throw Refused("folded capture read lacks the exact game View layout", "folded lighting program");
    result.Varyings = PrepareOriginalFoldedEngineVaryings(owner, program.Reference, archive);
    return result;
}
// captured(): source-bound dynamic material roots at the real compiler consumers.
struct MaterialFoldedCapture
{
    std::vector<std::string> Pins;
    std::map<std::string, std::vector<Value>> Values;
    bool TangentSpaceNormal = true;
    std::string Factory;
    MaterialFoldedContract Contract;
};
inline MaterialFoldedCapture CaptureOriginalFoldedLighting(const std::shared_ptr<SymbolicArena>& arena,
                                                           const MaterialFoldedProgram& selected, const Json& settings,
                                                           const MaterialSurfaceRewrites& rewrites)
{
    const auto& plan = selected.Lit.Plan.Source.Recovery;
    const auto code = ParseMaterialAnchorInstructions(plan.Lines);
    for (const auto& line : code)
        if (line.op == "TEX" && !line.text.empty())
        {
            const auto ops = BubbleSourceDetail::OperandTexts(line.text);
            if (!ops.empty() && ops.back() == "2D" && (line.mods != std::vector<std::string>{"F"} || ops.size() != 4))
                throw Refused("unreviewed ordinary 2D texture instruction semantics", "folded material inputs");
        }
    MaterialFoldedCapture result;
    result.Factory = selected.Factory;
    result.Contract =
        CheckOriginalFoldedLightingContract(code, selected.Lit.Source.Program, settings,
                                            selected.Lit.Anchors.Source.Surface, selected.NormalSeal, plan.Lines);
    const auto& contract = result.Contract;
    auto stop = contract.Boundary;
    while (stop && starts(plan.Lines[stop - 1], "IF "))
        --stop;
    const auto bounds = MaterialPlanArrayBounds(plan, *arena);
    OriginalFoldedSymbolicMachine machine(plan.Lines, arena, bounds);
    try
    {
        machine.run(stop);
    }
    catch (const EvalError& error)
    {
        throw Refused(error.what(), "material symbolic trace");
    }
    auto context = plan.Binding;
    if (plan.ConfigureBinding)
        plan.ConfigureBinding(*arena, context);
    InstallOriginalBubbleClipLeaves(context, selected.Lit.Source.Program, arena);
    Binder binder(*arena, context);
    const auto bind = [&](const std::vector<Value>& raw)
    {
        std::vector<Value> out;
        for (const auto& value : OriginalFoldedMaterialValues(*arena, raw))
            out.push_back(BindMaterialComponent(binder, value));
        return out;
    };
    const auto scheduled = [&](const MaterialScheduledAnchor& anchor)
    {
        if (anchor.ValueAnchor.has_constant)
            return std::vector<Value>{Value(anchor.ValueAnchor.constant)};
        if (anchor.Indices.empty())
        {
            const auto found = machine.reg.find(anchor.ValueAnchor.register_name);
            if (found == machine.reg.end())
                throw Refused("folded material register is absent", "material output register");
            std::vector<Value> out;
            for (const auto c : anchor.ValueAnchor.comps)
                out.push_back(found->second[std::string("xyzw").find(c)]);
            return out;
        }
        auto later = machine;
        std::size_t previous = 0;
        bool first = true;
        for (const auto at : anchor.Indices)
        {
            if (at >= plan.Lines.size() || (!first && at <= previous) || at < anchor.ValueAnchor.line)
                throw Refused("invalid native material schedule", "material scheduled range");
            try
            {
                later.step(at, plan.Lines[at]);
            }
            catch (const EvalError& error)
            {
                throw Refused(error.what(), "material scheduled trace");
            }
            previous = at;
            first = false;
        }
        const auto found = later.reg.find(anchor.ValueAnchor.register_name);
        if (found == later.reg.end())
            throw Refused("scheduled material register is absent", "material scheduled register");
        std::vector<Value> out;
        for (const auto c : anchor.ValueAnchor.comps)
            out.push_back(found->second[std::string("xyzw").find(c)]);
        return out;
    };
    result.Pins.push_back("EmissiveColor");
    result.Values["EmissiveColor"] = bind(scheduled(contract.Emissive.Value));
    if (contract.HasOpacity)
    {
        result.Pins.push_back("Opacity");
        result.Values["Opacity"] = bind(scheduled(contract.Opacity));
    }
    if (contract.HasNormal)
    {
        OriginalFoldedSymbolicMachine normal(plan.Lines, arena, bounds);
        try
        {
            normal.run(contract.Normal.line + 1);
        }
        catch (const EvalError& error)
        {
            throw Refused(error.what(), "material symbolic trace");
        }
        const auto found = normal.reg.find(contract.Normal.register_name);
        if (found == normal.reg.end())
            throw Refused("folded normal register is absent", "material output register");
        std::vector<Value> raw;
        for (const auto c : contract.Normal.comps)
            raw.push_back(found->second[std::string("xyzw").find(c)]);
        result.Pins.push_back("Normal");
        result.Values["Normal"] = bind(raw);
    }
    std::vector<Value> flat;
    for (const auto& pin : result.Pins)
        flat.insert(flat.end(), result.Values[pin].begin(), result.Values[pin].end());
    const auto width = flat.size();
    auto sceneProgram = selected.Lit.Source.Program;
    sceneProgram.Lines = plan.Lines;
    const auto scene = PrepareOriginalMaterialSceneRewrites(sceneProgram, settings, rewrites);
    if (scene.SceneAndSun)
        flat = scene.SceneAndSun(*arena, flat);
    if (flat.size() != width)
        throw Refused("scene recovery changed the folded pin width", "folded material inputs");
    flat = frame_transforms(*arena, FacingSigns(*arena, flat));
    std::size_t at = 0;
    for (const auto& pin : result.Pins)
        for (auto& value : result.Values[pin])
            value = flat.at(at++);
    if (result.Values.count("Normal"))
    {
        const auto normal = MaterialNormal(*arena, result.Values["Normal"]);
        result.TangentSpaceNormal = normal.tangent_space;
        result.Values["Normal"] = normal.has_pin ? normal.pin : std::vector<Value>{Value(0.), Value(0.), Value(1.)};
    }
    if (!binder.assumed.empty())
    {
        std::string fields;
        for (const auto& item : binder.assumed)
            fields += (fields.empty() ? "" : ", ") + item.first.first + "[" + std::to_string(item.first.second) + "]";
        throw Refused("folded material roots read unresolved engine words: " + fields, "folded material inputs");
    }
    return result;
}
namespace FoldedLightingDetail
{
// quotient_formulas(): comparison-only source identities; graph arithmetic stays unchanged.
inline std::vector<Value> Quotient(SymbolicArena& arena, const std::vector<Value>& values)
{
    const auto position = source(arena, "SvPosition", Value::null(), 3);
    return rewrite(arena, values,
                   [&](const Value& value)
                   {
                       const auto simplified = ParticleSourceDetail::Simplify(arena, value);
                       if (is(simplified, "RCP") && same(Args(simplified)[0], position))
                           return source(arena, "PixelDepth");
                       return simplified;
                   });
}
inline bool ArithmeticZero(const std::vector<Value>& values)
{
    bool zero = std::any_of(values.begin(), values.end(), [](const Value& v) { return NumberIs(v, 0); });
    for (const auto& node : walk_formulas(values))
        if (ParticleSourceDetail::Arithmetic(node.expression->op))
            for (const auto& arg : node.expression->args)
                zero = zero || NumberIs(arg, 0);
    return zero;
}
struct Resolved
{
    bool Found = false;
    std::array<double, 4> Default{{1, 1, 1, 1}};
};
// resolve_defaults(): literal candidates projected before the checked engine identities.
inline Resolved ResolveDefaults(SymbolicArena& arena, const std::vector<Value>& particle,
                                const std::vector<Value>& local)
{
    std::set<int> used;
    for (const auto& node : walk_formulas(particle))
        if (is(node, "COMP") && is(Args(node)[0], "SRC"))
        {
            const auto& a = Args(Args(node)[0]);
            if (a.size() == 2 && a[0].kind == Value::Text && a[0].text == "ParticleDynamicParameter" &&
                NumberIs(a[1], 0))
                used.insert(int(Args(node)[1].number()));
        }
    const auto normalized = Quotient(arena, ParticleSourceDetail::Normalize(arena, local));
    const std::vector<double> literals = ArithmeticZero(local) ? std::vector<double>{1., 0.} : std::vector<double>{1.};
    const std::vector<int> channels(used.begin(), used.end());
    std::vector<std::array<double, 4>> candidates;
    std::size_t count = 1;
    for (std::size_t i = 0; i < channels.size(); ++i)
        count *= literals.size();
    for (std::size_t index = 0; index < count; ++index)
    {
        std::array<double, 4> value{{1, 1, 1, 1}};
        auto rest = index;
        for (std::size_t i = channels.size(); i-- > 0;)
        {
            value[std::size_t(channels[i])] = literals[rest % literals.size()];
            rest /= literals.size();
        }
        if (ParticleLitSourceDetail::Equal(Quotient(arena, ParticleSourceDetail::LocalFallback(arena, particle, value)),
                                           normalized))
            candidates.push_back(value);
    }
    Resolved result;
    if (candidates.empty())
        return result;
    result.Found = true;
    result.Default =
        *std::min_element(candidates.begin(), candidates.end(),
                          [](const std::array<double, 4>& a, const std::array<double, 4>& b)
                          {
                              const auto x = std::count_if(a.begin(), a.end(), [](double v) { return v != 1.; }),
                                         y = std::count_if(b.begin(), b.end(), [](double v) { return v != 1.; });
                              return x != y ? x < y : a < b;
                          });
    return result;
}
}
// joint_formulas(): the local and particle projections jointly fix erased vertex-color factors and the
// source-literal dynamic defaults of the folded pins.
struct MaterialFoldedJoint
{
    std::vector<Value> Color;
    std::array<double, 4> Default{{1, 1, 1, 1}};
};
inline MaterialFoldedJoint ResolveOriginalFoldedJoint(SymbolicArena& arena, const std::vector<Value>& particle,
                                                      const std::vector<Value>& local, const std::string& factory)
{
    using namespace FoldedLightingDetail;
    const auto formalLocal = Quotient(arena, local);
    std::vector<std::vector<Value>> attempted = {particle};
    if (ParticleSourceDetail::Contains(local, "VertexColor") &&
        !ParticleSourceDetail::Contains(particle, "VertexColor") &&
        IsMember(factory,
                 {"FGPUSpriteVertexFactory", "FParticleSpriteVertexFactory", "FParticleBeamTrailVertexFactory"}))
    {
        attempted.push_back(rewrite(arena, particle,
                                    [&](const Value& value)
                                    {
                                        if (is(value, "COMP") && is(Args(value)[0], "SRC") &&
                                            Args(Args(value)[0])[0].kind == Value::Text &&
                                            Args(Args(value)[0])[0].text == "ParticleColor")
                                            return arena.mk("MUL", {value, source(arena, "VertexColor", Value::null(),
                                                                                  int(Args(value)[1].number()))});
                                        return value;
                                    }));
        std::vector<Value> restored;
        for (std::size_t c = 0; c < particle.size(); ++c)
        {
            const auto& value = particle[c];
            const auto color = source(arena, "VertexColor", Value::null(), int(c));
            if (is(value, "MAX") &&
                std::any_of(Args(value).begin(), Args(value).end(), [](const Value& v) { return NumberIs(v, 0); }))
            {
                const auto inner = NumberIs(Args(value)[0], 0) ? Args(value)[1] : Args(value)[0];
                restored.push_back(arena.op("MAX", {simplify(arena, "MUL", {inner, color}), 0.}));
            }
            else if (is(value, "SAT"))
                restored.push_back(arena.op("SAT", {simplify(arena, "MUL", {Args(value)[0], color})}));
            else if (NumberIs(value, 0))
                restored.push_back(value);
            else
                throw Refused("joint vertex color cannot be placed before the native output clamp",
                              "particle joint graph");
        }
        attempted.push_back(restored);
    }
    std::vector<std::string> failures;
    for (std::size_t index = 0; index < attempted.size(); ++index)
    {
        const auto& values = attempted[index];
        const auto found = ResolveDefaults(arena, values, formalLocal);
        if (!found.Found)
        {
            failures.push_back("no checked joint native literal projection");
            continue;
        }
        if (index)
        {
            const auto projected =
                rewrite(arena, values,
                        [&](const Value& v)
                        {
                            if (is(v, "COMP") && is(Args(v)[0], "SRC") && Args(Args(v)[0])[0].kind == Value::Text &&
                                Args(Args(v)[0])[0].text == "VertexColor")
                                return Value(1.);
                            return ParticleSourceDetail::Simplify(arena, v);
                        });
            if (!ParticleLitSourceDetail::Equal(projected, ParticleSourceDetail::Normalize(arena, particle)))
                throw Refused("joint folded pins do not equal the original particle projection",
                              "folded particle defaults");
        }
        MaterialFoldedJoint result;
        result.Color = values;
        result.Default = found.Default;
        return result;
    }
    std::string text;
    for (const auto& failure : failures)
        text += (text.empty() ? "" : "; ") + failure;
    throw Refused("no exact local/particle joint folded pins: " + text, "folded particle defaults");
}
// policy_refs(): the first reference of every distinct base-pass program/factory, in sorted order.
inline std::vector<FMaterialShaderReference> OriginalFoldedPolicyReferences(const MaterialOriginalOwnerSource& owner)
{
    std::map<std::pair<std::string, std::int32_t>, FMaterialShaderReference> groups;
    const ShaderPolicyIndex policies(owner.References);
    for (const auto& policy : policies.BasePass())
        groups.emplace(
            std::make_pair(MaterialFactoryName(policy.Reference.VertexFactoryHash), policy.Reference.ResourceIndex),
            policy.Reference);
    std::vector<FMaterialShaderReference> result;
    for (const auto& item : groups)
        result.push_back(item.second);
    return result;
}
inline bool OriginalFoldedSkylight(const FMaterialShaderReference& ref)
{
    return IsSkylightShaderName(MaterialOriginalSourceShaderName(ref.TypeHash));
}
// An owner whose translucent per-vertex directional lighting the compiler folded into constants: its cooked policy is
// a lit, translucent, per-vertex directional surface that is not fully rough (the contract of
// CheckOriginalFoldedLightingContract), and its first base-pass program holds the fold and the View capture read
// (DiagnoseOriginalFoldedLighting).
inline bool OriginalFoldedLightingOwner(const MaterialOriginalOwnerSource& owner,
                                        const MaterialOriginalArchiveAccess& archive, MaterialDomainSourceScope scope,
                                        const std::function<void(SymbolicArena&, BindingContext&)>& binding)
{
    const auto& settings = owner.User.Settings;
    const auto& cooked = owner.User.Cooked;
    if (cooked.Domain != "MD_Surface" || cooked.ShadingModel != "MSM_DefaultLit" ||
        cooked.BlendMode != "BLEND_Translucent" ||
        FoldedLightingDetail::RenderSetting(settings, "TranslucencyLightingMode") !=
            "TLM_VolumetricPerVertexDirectional" ||
        FoldedLightingDetail::FullyRough(settings))
        return false;
    const auto policies = OriginalFoldedPolicyReferences(owner);
    if (policies.empty())
        return false;
    const auto lit = PrepareOriginalParticleLitProgram(owner, policies.front(), archive, scope, binding);
    const auto code = ParseMaterialAnchorInstructions(lit.Plan.Source.Recovery.Lines);
    return DiagnoseOriginalFoldedLighting(code, lit.Source.Program, settings, lit.Anchors.Source.Surface).Found;
}
// recover(_sky): one ENABLE_SKY_LIGHT policy's graph.
inline Json RecoverOriginalFoldedPolicyGraph(const MaterialOriginalOwnerSource& owner,
                                             const MaterialOriginalArchiveAccess& archive,
                                             MaterialDomainSourceScope scope,
                                             const std::function<void(SymbolicArena&, BindingContext&)>& binding,
                                             const MaterialSurfaceRewrites& rewrites, bool sky)
{
    const auto defaults = CheckOriginalCollapsedLightingDefaults(owner);
    const auto all = OriginalFoldedPolicyReferences(owner);
    std::vector<FMaterialShaderReference> choices;
    for (const auto& ref : all)
        if (OriginalFoldedSkylight(ref) == sky)
            choices.push_back(ref);
    if (choices.empty())
        throw Refused("folded family lacks both checked sky compilation policies", "folded lighting policy");
    const std::map<std::string, int> order = {{"FMeshParticleVertexFactory", 0},
                                              {"FParticleSpriteVertexFactory", 1},
                                              {"FParticleBeamTrailVertexFactory", 2},
                                              {"FGPUSpriteVertexFactory", 3}};
    std::stable_sort(choices.begin(), choices.end(),
                     [&](const FMaterialShaderReference& a, const FMaterialShaderReference& b)
                     {
                         const auto key = [&](const FMaterialShaderReference& ref)
                         {
                             const auto factory = MaterialFactoryName(ref.VertexFactoryHash);
                             const auto found = order.find(factory);
                             return std::make_tuple(
                                 !MaterialParticleFactory(factory), found == order.end() ? 4 : found->second,
                                 OriginalFoldedSkylight(ref), MaterialOriginalSourceShaderName(ref.TypeHash));
                         };
                         return key(a) < key(b);
                     });
    const auto& settings = owner.User.Settings;
    auto arena = std::make_shared<SymbolicArena>();
    const auto primary = PrepareOriginalFoldedProgram(owner, choices[0], archive, scope, binding);
    auto captured = CaptureOriginalFoldedLighting(arena, primary, settings, rewrites);
    if (!captured.Values.count("Normal"))
    {
        for (const auto& ref : all)
            if (MaterialFactoryName(ref.VertexFactoryHash) == captured.Factory && OriginalFoldedSkylight(ref))
            {
                const auto alternate = CaptureOriginalFoldedLighting(
                    arena, PrepareOriginalFoldedProgram(owner, ref, archive, scope, binding), settings, rewrites);
                if (alternate.Values.count("Normal"))
                {
                    captured.Pins.push_back("Normal");
                    captured.Values["Normal"] = alternate.Values.at("Normal");
                    captured.TangentSpaceNormal = alternate.TangentSpaceNormal;
                }
                break;
            }
    }
    Json dynamic = Json::MakeObject();
    const FMaterialShaderReference* local = nullptr;
    for (const auto& ref : choices)
        if (ref.VertexFactoryHash == EngineHash::LocalVertexFactory)
        {
            local = &ref;
            break;
        }
    if (MaterialParticleFactory(captured.Factory) && local)
    {
        const auto preview = CaptureOriginalFoldedLighting(
            arena, PrepareOriginalFoldedProgram(owner, *local, archive, scope, binding), settings, rewrites);
        if (!captured.Values.count("Opacity") || !preview.Values.count("Opacity"))
            throw Refused("joint folded pins lack the native opacity", "folded particle defaults");
        std::vector<Value> color, local4;
        for (const auto& v : captured.Values.at("EmissiveColor"))
            color.push_back(arena->mk("MAX", {v, 0.}));
        color.push_back(arena->mk("SAT", {captured.Values.at("Opacity")[0]}));
        for (const auto& v : preview.Values.at("EmissiveColor"))
            local4.push_back(arena->mk("MAX", {v, 0.}));
        local4.push_back(arena->mk("SAT", {preview.Values.at("Opacity")[0]}));
        const auto joint = ResolveOriginalFoldedJoint(*arena, color, local4, captured.Factory);
        if (joint.Color.size() != 4)
            throw Refused("joint folded pins have an unexpected width", "folded particle defaults");
        std::vector<Value> emission;
        for (std::size_t c = 0; c < 3; ++c)
        {
            const auto& v = joint.Color[c];
            emission.push_back(is(v, "MAX") && Args(v).size() == 2 && NumberIs(Args(v)[1], 0) ? Args(v)[0] : v);
        }
        captured.Values["EmissiveColor"] = emission;
        const auto& alpha = joint.Color[3];
        captured.Values["Opacity"] = {is(alpha, "SAT") ? Args(alpha)[0] : alpha};
        auto chosen = Json::MakeArray();
        for (const auto value : joint.Default)
            chosen.array.push_back(value);
        dynamic["0"] = chosen;
    }
    auto plan = primary.Lit.Plan.Source.Recovery;
    plan.Stop = captured.Contract.Boundary;
    plan.Defaults["particle_dynamic_defaults"] = dynamic;
    plan.Settings["tangent_space_normal"] = captured.TangentSpaceNormal;
    plan.NormalSamples = normal_sample_ids;
    MaterialBoundOutputs bound;
    bound.Arena = arena;
    for (const auto& pin : captured.Pins)
    {
        bound.Pins.push_back(pin);
        bound.Values.push_back(captured.Values.at(pin));
    }
    bound.Pins.push_back("BaseColor");
    bound.Values.push_back({Value(0.), Value(0.), Value(0.)});
    bound.Pins.push_back("Metallic");
    bound.Values.push_back({Value(defaults.Metallic)});
    bound.Pins.push_back("Specular");
    bound.Values.push_back({Value(defaults.Specular)});
    return LiftMaterialBoundOutputs(plan, std::move(bound)).Graph;
}
inline MaterialOriginalParticleOwnerRecovery RecoverOriginalFoldedLightingOwnerExpressionGraph(
    const MaterialOriginalOwnerSource& owner, const MaterialOriginalArchiveAccess& archive,
    MaterialDomainSourceScope scope = MaterialDomainSourceScope::QualifiedGlobalTail,
    const std::function<void(SymbolicArena&, BindingContext&)>& binding = {},
    const MaterialSurfaceRewrites& rewrites = {})
{
    if (!OriginalFoldedLightingOwner(owner, archive, scope, binding))
        throw Refused("outside the checked folded lighting families", "folded lighting contracts");
    const auto noSky = RecoverOriginalFoldedPolicyGraph(owner, archive, scope, binding, rewrites, false);
    const auto sky = RecoverOriginalFoldedPolicyGraph(owner, archive, scope, binding, rewrites, true);
    MaterialOriginalParticleOwnerRecovery result;
    auto graph = MergeOriginalAuthoredReflectionGraphs(sky, noSky);
    // The water-height cut of the first policy, where it has one.
    {
        const auto policies = OriginalFoldedPolicyReferences(owner);
        if (policies.empty())
            throw Refused("folded lighting has no original base-pass program", "Bubble clip contract");
        const auto first = PrepareOriginalFoldedProgram(owner, policies.front(), archive, scope, binding);
        if (first.Clip.Found)
            graph = WrapOriginalBubbleClipGraph(graph, first.Clip);
    }
    // The primary program of record is the first sorted policy of the no-sky graph.
    const auto policies = OriginalFoldedPolicyReferences(owner);
    std::vector<FMaterialShaderReference> noSkyChoices;
    for (const auto& ref : policies)
        if (!OriginalFoldedSkylight(ref))
            noSkyChoices.push_back(ref);
    const auto record = PrepareOriginalFoldedProgram(
        owner, noSkyChoices.empty() ? policies.front() : noSkyChoices.front(), archive, scope, binding);
    result.Recovery.Primary.Material.Graph = graph;
    result.Recovery.Primary.Material.Validation = ValidateGraph(graph);
    result.Recovery.Primary.Source = record.Lit.Source;
    result.Recovery.Primary.OriginalLines = record.Lit.Plan.Source.OriginalLines;
    result.Recovery.Primary.Storage = record.Lit.Plan.Source.StorageDeclarations;
    auto unaccounted = Json::MakeArray();
    for (
        const auto* fact :
        {"original authored lighting pins are underidentified; standard graph uses native-proved compiled-equivalent defaults",
         "vertex stages are not recovered",
         "primary recovery checks base-pass pixel interfaces; auxiliary pixel stages require separate source/native certificates",
         "engine reflection Custom expressions require ReflectionStruct and ENABLE_SKY_LIGHT compilation context"})
        unaccounted.array.push_back(Json(fact));
    result.ParticleFacts = Object(
        {{"canonical_compiled_lighting", true},
         {"original_authored_lighting_inputs_identified", false},
         {"raw_lighting_pins_not_identified",
          Array({"BaseColor", "Metallic", "Specular", "Roughness", "AmbientOcclusion"})},
         {"canonical_defaults",
          Object({{"Metallic", 0.0},
                  {"Specular", 0.5},
                  {"value_evidence", "canonical standard UE defaults; game CDO not independently recovered"}})},
         {"unaccounted_values", unaccounted},
         {"proof_required",
          "all native folded carriers, observed dynamic pins, whole-original-program symbolic material-interface closure and exact source/native indirect-lighting composition"}});
    return result;
}
}
} // P4MapKit::Material

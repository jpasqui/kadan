// Ports of the Bubble and collapsed-lighting contracts the folded lighting producer uses:
// material_bubble_exact_defaults.py (checked_defaults), material_bubble_exact_clip.py (contract,
// expression, wrap, validate), material_bubble_exact_normal.py (prepare, anchor) and
// material_bubble_distortion_contract.py (recover_input with its exact auxiliary program checks).
// The numeric proofs of those modules (evaluate, killed, prove) stay with the Python oracle.
#pragma once
#include "material_source_particle_lit_plan.h"
#include "material_engine_hashes.h"
#include "material_preshader_eval.h"
#include "material_source_folded_varyings.h"
#include "material_source_common.h"
namespace P4MapKit
{
namespace Material
{
namespace BubbleSourceDetail
{
// material_probe._table_entries: a uniform buffer's (resource, bind slot) words in a resource table.
inline std::vector<std::pair<int, int>> TableEntries(const std::vector<std::uint32_t>& table, int ub)
{
    std::vector<std::pair<int, int>> out;
    if (table.empty() || ub < 0 || std::size_t(ub) >= table.size() || !table[std::size_t(ub)])
        return out;
    for (std::size_t at = table[std::size_t(ub)]; at < table.size(); ++at)
    {
        const auto w = table[at];
        if (w == 0xFFFFFFFFu || int(w >> 24) != ub)
            break;
        out.push_back({int((w >> 8) & 0xFFFF), int(w & 0xFF)});
    }
    return out;
}
// The operand texts of one instruction, as material_probe.Operand.text keeps them.
inline std::vector<std::string> OperandTexts(const std::string& line)
{
    const auto space = line.find(' ');
    if (space == std::string::npos)
        return {};
    std::vector<std::string> out;
    for (const auto& operand : split_operands(line.substr(space + 1)))
        out.push_back(trim(operand));
    return out;
}
inline bool Unconditional(const AnchorInstruction& line)
{
    const auto ops = OperandTexts(line.text);
    return ops.empty() || ops[0].find('(') == std::string::npos;
}
inline std::vector<int> ViewBuffers(const MaterialPreparedProgram& program)
{
    std::vector<int> out;
    for (const auto& buffer : program.BaseBinding.Buffers)
        if (buffer.second == "View")
            out.push_back(buffer.first);
    return out;
}
inline bool GameView(const MaterialPreparedProgram& program, int buffer)
{
    return buffer >= 0 && std::size_t(buffer) < program.Header.layout_hashes.size() &&
           program.Header.layout_hashes[std::size_t(buffer)] == EngineHash::ViewLayout;
}
inline double ScalarDefault(const FMaterialUniformParameter& parameter)
{
    float value;
    std::memcpy(&value, &parameter.DefaultBits[0], 4);
    return double(value);
}
}
// material_bubble_exact_defaults.checked_defaults: the master's cooked Material export keeps no
// Metallic or Specular input and no material attributes, so the standard defaults are a proposal
// for the independent carrier proof, never the game's own CDO values.
struct MaterialCollapsedLightingDefaults
{
    double Metallic = 0, Specular = .5;
};
inline MaterialCollapsedLightingDefaults
CheckOriginalCollapsedLightingDefaults(const MaterialOriginalOwnerSource& owner)
{
    if (owner.User.Chain.empty())
        throw Refused("folded lighting defaults need the owner's master", "folded lighting defaults");
    const auto& master = owner.User.Chain.front();
    if (master.Class != "Material")
        throw Refused("cooked default evidence needs exactly one Material export", "folded lighting defaults");
    std::set<std::string> names;
    for (const auto& field : master.Properties.Fields)
        if (!names.insert(field.first).second)
            throw Refused("duplicate/array top-level material tag", "folded lighting defaults");
    if (names.count("Metallic") || names.count("Specular"))
        throw Refused("retained lighting input is present, not an absent default", "folded lighting defaults");
    if (const auto attributes = master.Properties.Field("bUseMaterialAttributes"))
    {
        bool value = true;
        if (!MaterialBool(*attributes, value) || value)
            throw Refused("material attributes can replace the individual inputs", "folded lighting defaults");
    }
    if (names.count("MaterialAttributes"))
        throw Refused("material attributes input is present", "folded lighting defaults");
    return {};
}
// ----- material_bubble_exact_clip.py -----
// The water-height cut: a single native discard whose predicate reads the game View's global clipping plane and two
// material scalars, an enable switch and a height. The scalars are found in the bound predicate, not by name.
inline const std::string& OriginalBubbleClipCode()
{
    static const std::string code =
        "if (ResolvedView.C4GlobalClippingPlane.x > 0.5 && Enable > 1.0 && !((Position.z - Height) * (ResolvedView.C4GlobalClippingPlane.y * ResolvedView.C4XluGlobalClippingPlaneSign) > 0.0)) { clip(-1.0); } return Value;";
    return code;
}
// FIELD_CODES: the game View extension fields as Custom nodes ("source").
inline bool OriginalBubbleViewField(const std::string& name, std::string& code, int& width)
{
    if (name == "C4GlobalClippingPlane")
    {
        code = "return ResolvedView.C4GlobalClippingPlane.xy;";
        width = 2;
        return true;
    }
    if (name == "C4XluGlobalClippingPlaneSign")
    {
        code = "return ResolvedView.C4XluGlobalClippingPlaneSign;";
        width = 1;
        return true;
    }
    return false;
}
// The bound native discard predicate, operation for operation ("expression"), for the scalars that play the enable
// switch and the height.
inline Value OriginalBubbleClipExpression(SymbolicArena& arena, const std::string& enableName,
                                          const std::string& heightName)
{
    const auto k = [&](const std::string& op, const std::vector<Value>& args)
    {
        return arena.mk(op, args);
    };
    const auto globalX = source(arena, "View", Value("C4GlobalClippingPlane"), 0),
               globalY = source(arena, "View", Value("C4GlobalClippingPlane"), 1);
    const auto sign = source(arena, "View", Value("C4XluGlobalClippingPlaneSign"), 0);
    const auto enable = source(arena, "ScalarParameter", Value(enableName), 0),
               height = source(arena, "ScalarParameter", Value(heightName), 0);
    const auto worldZ = source(arena, "WorldPosition", Value::null(), 2);
    const auto gate = k("I.AND", {k("TRUNC.U", {k("F.SGT", {globalX, .5})}), k("TRUNC.U", {k("F.SGT", {enable, 1.})})});
    const auto passHeight =
        k("TRUNC.U", {k("F.SGT", {k("MUL", {k("ADD", {worldZ, k("NEG", {height})}), k("MUL", {globalY, sign})}), 0.})});
    const auto survives = k("T.NE", {k("I.OR", {k("NEG", {k("UC.SEQ", {gate, 0.})}), passHeight})});
    return k("T.NE", {k("TRUNC.U", {k("F.SLT", {k("SEL", {survives, 1., -1.}), 0.})})});
}
// bind_leaf: the typed game View extension words the cut reads, as named View sources.
inline void InstallOriginalBubbleClipLeaves(BindingContext& context, const MaterialPreparedProgram& program,
                                            const std::shared_ptr<SymbolicArena>& arena)
{
    const auto previous = context.LeafOverride;
    const auto buffers = program.BaseBinding.Buffers;
    const auto layouts = program.Header.layout_hashes;
    context.LeafOverride = [previous, buffers, layouts, arena](const Value& value, Value& out)
    {
        if (is(value, "IN") && Args(value).size() == 3 && Args(value)[0].kind == Value::Text &&
            Args(value)[0].text == "buffer" && IsNumber(Args(value)[1]) && IsNumber(Args(value)[2]))
        {
            const int buffer = int(Args(value)[1].number());
            const auto name = buffers.find(buffer);
            const auto offset = Args(value)[2].number();
            if (name != buffers.end() && name->second == "View" && (offset == 2048 || offset == 2052 || offset == 3948))
            {
                if (buffer < 0 || std::size_t(buffer) >= layouts.size() ||
                    layouts[std::size_t(buffer)] != EngineHash::ViewLayout)
                    throw Refused("named Bubble clip field lacks exact typed game View identity",
                                  "Bubble clip contract");
                out = offset == 3948 ? source(*arena, "View", Value("C4XluGlobalClippingPlaneSign"), 0)
                                     : source(*arena, "View", Value("C4GlobalClippingPlane"), int((offset - 2048) / 4));
                return true;
            }
        }
        return previous ? previous(value, out) : false;
    };
}
struct MaterialBubbleClipContract
{
    bool Found = false;
    std::size_t Line = 0;
    Value BoundKill;
    double EnableDefault = 0, HeightDefault = 0;
    std::string EnableName, HeightName;
    std::string CodeSha256;
};
// The names of the material scalars a bound expression reads.
inline std::vector<std::string> OriginalBubbleClipScalars(const Value& expression)
{
    std::set<std::string> names;
    for (const auto& value : walk_formulas({expression}))
        if (is(value, "SRC") && Args(value).size() == 2 && Args(value)[0].kind == Value::Text &&
            Args(value)[0].text == "ScalarParameter" && Args(value)[1].kind == Value::Text)
            names.insert(Args(value)[1].text);
    return std::vector<std::string>(names.begin(), names.end());
}
// contract(): every original kill, its branch condition and its bound predicate, exactly.
inline MaterialBubbleClipContract PrepareOriginalBubbleClipContract(const MaterialOriginalOwnerSource& owner,
                                                                    const MaterialPreparedProgram& program,
                                                                    const MaterialRecoveryPlan& plan)
{
    using namespace BubbleSourceDetail;
    MaterialBubbleClipContract result;
    const auto code = ParseMaterialAnchorInstructions(plan.Lines);
    std::vector<std::size_t> kills;
    for (std::size_t i = 0; i < code.size(); ++i)
        if (code[i].op == "KIL")
            kills.push_back(i);
    if (kills.empty())
        return result;
    if (kills.size() != 1)
        throw Refused("unproved native discard: more than one kill", "Bubble clip contract");
    const auto views = ViewBuffers(program);
    if (views.size() != 1 || !GameView(program, views[0]))
        throw Refused("Bubble cut requires actual typed game View identity", "Bubble clip contract");
    const auto at = kills[0];
    if (plan.Lines[at] != "KIL NE.x" || at + 1 >= plan.Lines.size() || plan.Lines[at + 1] != "ENDIF")
        throw Refused("Bubble cut has an unproved native kill consumer", "Bubble clip contract");
    for (const auto& load : {std::make_pair(2048, std::string("F32X2")), std::make_pair(3948, std::string("F32"))})
    {
        std::vector<const AnchorInstruction*> found;
        const auto text = "buf" + std::to_string(views[0]) + "[" + std::to_string(load.first) + "]";
        for (std::size_t i = 0; i < at; ++i)
        {
            const auto ops = OperandTexts(code[i].text);
            if (code[i].op == "LDC" && ops.size() >= 2 && ops[1] == text)
                found.push_back(&code[i]);
        }
        if (found.size() != 1 || found[0]->mods != std::vector<std::string>{load.second} || !Unconditional(*found[0]))
            throw Refused("Bubble cut changes an exact native float View load type/predicate", "Bubble clip contract");
    }
    auto arena = std::make_shared<SymbolicArena>();
    SymbolicMachine machine(plan.Lines, arena, MaterialPlanArrayBounds(plan, *arena));
    try
    {
        machine.run(at + 2);
    }
    catch (const EvalError& error)
    {
        throw Refused(error.what(), "Bubble clip contract");
    }
    if (machine.symbolic_kills->size() != 1)
        throw Refused("Bubble clip did not account for every native kill path", "Bubble clip contract");
    const auto& kill = machine.symbolic_kills->front();
    if (kill.line != at || kill.condition.kind != Value::Boolean || !kill.condition.boolean || kill.path.size() != 1 ||
        !kill.path[0].second)
        throw Refused("Bubble clip has an unproved native branch/kill condition", "Bubble clip contract");
    auto context = plan.Binding;
    context.Collections.clear();
    if (plan.ConfigureBinding)
        plan.ConfigureBinding(*arena, context);
    InstallOriginalBubbleClipLeaves(context, program, arena);
    Binder binder(*arena, context);
    const auto actual = frame_transforms(*arena, {BindMaterialComponent(binder, kill.path[0].first)})[0];
    const auto scalars = OriginalBubbleClipScalars(actual);
    if (scalars.size() != 2 || !binder.assumed.empty())
        throw Refused("Bubble cut does not read exactly two material scalars", "Bubble clip contract");
    for (const auto& order : {std::make_pair(scalars[0], scalars[1]), std::make_pair(scalars[1], scalars[0])})
        if (same(actual, OriginalBubbleClipExpression(*arena, order.first, order.second)))
        {
            result.EnableName = order.first;
            result.HeightName = order.second;
        }
    if (result.EnableName.empty())
        throw Refused("Bubble cut differs from exact ordered native predicate", "Bubble clip contract");
    double enable = 0, height = 0;
    for (const auto& name : {result.EnableName, result.HeightName})
    {
        std::size_t count = 0;
        for (const auto& p : owner.Map.Uniforms.Scalars)
            if (p.Name == name)
            {
                ++count;
                (name == result.EnableName ? enable : height) = ScalarDefault(p);
            }
        if (count != 1)
            throw Refused("Bubble cut scalar is not one retained scalar parameter: " + name, "Bubble clip contract");
    }
    result.Found = true;
    result.Line = at;
    result.BoundKill = actual;
    result.EnableDefault = enable;
    result.HeightDefault = height;
    result.CodeSha256 = ProgramDigest(plan.Lines);
    return result;
}
// validate(): every live EmissiveColor path ends in the exact Custom clip with its named inputs.
inline void ValidateOriginalBubbleClipGraph(const Json& graph)
{
    if (graph.Has("bubble_clip_contract"))
    {
        const auto& marker = graph.At("bubble_clip_contract");
        if (!marker.Has("view_layout") || marker.At("view_layout").Numeric() != double(EngineHash::ViewLayout) ||
            !marker.Has("view_fields") ||
            marker.At("view_fields").Dump() != Object({{"C4GlobalClippingPlane", Array({2048.0, 2052.0})},
                                                       {"C4XluGlobalClippingPlaneSign", Array({3948.0})}})
                                                   .Dump())
            throw Refused("Bubble Custom clip was omitted, modified or uses wrong native fields", "Bubble clip graph");
    }
    const auto& nodes = graph.At("nodes");
    const std::function<void(const Json&, std::set<std::string>)> live =
        [&](const Json& link, std::set<std::string> path)
    {
        if (link.kind != Json::Array || link.array.size() < 2 || link.array[1].Text() != "" ||
            path.count(link.array[0].Text()))
            throw Refused("invalid/cyclic live Bubble clip output", "Bubble clip graph");
        const auto name = link.array[0].Text();
        const auto& node = nodes.At(name);
        path.insert(name);
        const auto type = node.At("type").Text();
        const auto props = node.Has("props") ? node.At("props") : Json::MakeObject();
        const auto inputs = node.Has("inputs") ? node.At("inputs") : Json::MakeObject();
        const auto inputNames = [&]()
        {
            std::set<std::string> out;
            for (const auto& item : inputs.object)
                out.insert(item.first);
            return out;
        };
        if (type == "StaticSwitchParameter")
        {
            if (inputNames() != std::set<std::string>{"A", "B"})
                throw Refused("unchecked Bubble clip static selector", "Bubble clip graph");
            live(inputs.At("A"), path);
            live(inputs.At("B"), path);
            return;
        }
        const auto codeOf = [&]()
        {
            return props.Has("Code") ? props.At("Code").Text() : std::string();
        };
        if (type == "Custom" && codeOf() == "#if ENABLE_SKY_LIGHT\nreturn Sky;\n#else\nreturn NoSky;\n#endif")
        {
            if (inputNames() != std::set<std::string>{"Sky", "NoSky"} || !props.Has("OutputType") ||
                props.At("OutputType").Text() != "CMOT_Float3" || !props.Has("Inputs") ||
                props.At("Inputs").Dump() !=
                    Array({Object({{"InputName", "Sky"}}), Object({{"InputName", "NoSky"}})}).Dump() ||
                !props.Has("Description") ||
                props.At("Description").Text() != "P4 native ENABLE_SKY_LIGHT material pin")
                throw Refused("unchecked Bubble clip compilation selector", "Bubble clip graph");
            live(inputs.At("Sky"), path);
            live(inputs.At("NoSky"), path);
            return;
        }
        const auto expectedInputs = Array({Object({{"InputName", "Value"}}), Object({{"InputName", "Enable"}}),
                                           Object({{"InputName", "Height"}}), Object({{"InputName", "Position"}})});
        if (type != "Custom" || codeOf() != OriginalBubbleClipCode() || !props.Has("OutputType") ||
            props.At("OutputType").Text() != "CMOT_Float3" || !props.Has("Inputs") ||
            props.At("Inputs").Dump() != expectedInputs.Dump() ||
            inputNames() != std::set<std::string>{"Value", "Enable", "Height", "Position"})
            throw Refused("graph omits or changes native Bubble discard behavior", "Bubble clip graph");
        const auto& marker = graph.At("bubble_clip_contract");
        const auto& names = marker.At("parameters");
        const auto& defaults = marker.At("parameter_defaults");
        for (const auto& pair : {std::make_pair(std::string("Enable"), names.array.at(0).Text()),
                                 std::make_pair(std::string("Height"), names.array.at(1).Text())})
        {
            const auto& reference = inputs.At(pair.first);
            const auto target = reference.array.at(0).Text();
            if (reference.array.at(1).Text() != "" || !nodes.Has(target) ||
                nodes.At(target).At("type").Text() != "ScalarParameter" ||
                (nodes.At(target).Has("inputs") && !nodes.At(target).At("inputs").object.empty()) ||
                nodes.At(target).At("props").Dump() !=
                    Object({{"ParameterName", pair.second}, {"DefaultValue", defaults.At(pair.second)}}).Dump())
                throw Refused("Bubble clip input does not retain its exact named scalar/default", "Bubble clip graph");
        }
        const auto& position = inputs.At("Position");
        const auto target = position.array.at(0).Text();
        if (position.array.at(1).Text() != "" || !nodes.Has(target) ||
            nodes.At(target).At("type").Text() != "WorldPosition" ||
            (nodes.At(target).Has("inputs") && !nodes.At(target).At("inputs").object.empty()) ||
            (nodes.At(target).Has("props") && !nodes.At(target).At("props").object.empty()))
            throw Refused("Bubble clip position changed from absolute world including shader offsets",
                          "Bubble clip graph");
    };
    live(graph.At("outputs").At("EmissiveColor"), {});
}
// wrap(): the cut as a compilable standard Custom expression with the real artist parameters.
inline Json WrapOriginalBubbleClipGraph(Json graph, const MaterialBubbleClipContract& native)
{
    if (!native.Found)
        return graph;
    auto& nodes = graph["nodes"];
    for (const auto& item : nodes.object)
        if (item.second.At("type").Text() == "Custom" && item.second.Has("props") &&
            item.second.At("props").Has("Code") &&
            item.second.At("props").At("Code").Text() == OriginalBubbleClipCode())
        {
            ValidateOriginalBubbleClipGraph(graph);
            return graph;
        }
    const std::string enable = "ExactBubbleCutEnable", height = "ExactBubbleCutHeight",
                      position = "ExactBubbleCutPosition", clip = "ExactBubbleCutClip";
    for (const auto* name : {&enable, &height, &position, &clip})
        if (nodes.Has(*name))
            throw Refused("Bubble cut graph node collision", "Bubble clip graph");
    const auto link = [](const std::string& id)
    {
        return Array({id, ""});
    };
    nodes[enable] =
        Object({{"type", "ScalarParameter"},
                {"props", Object({{"ParameterName", native.EnableName}, {"DefaultValue", native.EnableDefault}})},
                {"inputs", Json::MakeObject()}});
    nodes[height] =
        Object({{"type", "ScalarParameter"},
                {"props", Object({{"ParameterName", native.HeightName}, {"DefaultValue", native.HeightDefault}})},
                {"inputs", Json::MakeObject()}});
    nodes[position] =
        Object({{"type", "WorldPosition"}, {"props", Json::MakeObject()}, {"inputs", Json::MakeObject()}});
    auto inputs = Json::MakeObject();
    inputs["Value"] = graph.At("outputs").At("EmissiveColor");
    inputs["Enable"] = link(enable);
    inputs["Height"] = link(height);
    inputs["Position"] = link(position);
    nodes[clip] = Object(
        {{"type", "Custom"},
         {"props", Object({{"Code", OriginalBubbleClipCode()},
                           {"OutputType", "CMOT_Float3"},
                           {"Inputs", Array({Object({{"InputName", "Value"}}), Object({{"InputName", "Enable"}}),
                                             Object({{"InputName", "Height"}}), Object({{"InputName", "Position"}})})},
                           {"Description", "Exact native Bubble water-height discard; game View extension required"}})},
         {"inputs", inputs}});
    graph["outputs"]["EmissiveColor"] = link(clip);
    graph["bubble_clip_contract"] =
        Object({{"node", clip},
                {"parameters", Array({native.EnableName, native.HeightName})},
                {"parameter_defaults",
                 Object({{native.EnableName, native.EnableDefault}, {native.HeightName, native.HeightDefault}})},
                {"view_fields", Object({{"C4GlobalClippingPlane", Array({2048.0, 2052.0})},
                                        {"C4XluGlobalClippingPlaneSign", Array({3948.0})}})},
                {"view_layout", double(EngineHash::ViewLayout)},
                {"required_import_extension",
                 "game View.C4GlobalClippingPlane and View.C4XluGlobalClippingPlaneSign; no stock View fallback"}});
    ValidateOriginalBubbleClipGraph(graph);
    return graph;
}
// ----- material_bubble_exact_normal.py -----
// prepare(): the one View.SkyIrradianceEnvironmentMap binding (resource 51) of the game View.
struct MaterialBubbleNormalSeal
{
    std::vector<std::pair<int, std::pair<int, int>>> Bindings;
    std::string CodeSha256;
};
inline MaterialBubbleNormalSeal SealOriginalBubbleNormal(const MaterialPreparedProgram& program,
                                                         const std::vector<std::string>& lines)
{
    using namespace BubbleSourceDetail;
    const auto views = ViewBuffers(program);
    if (views.empty() || std::any_of(views.begin(), views.end(), [&](int b) { return !GameView(program, b); }))
        throw Refused("SH normal requires the exact game View layout", "Bubble SH normal");
    MaterialBubbleNormalSeal seal;
    for (const auto b : views)
        for (const auto& entry : TableEntries(program.Header.srv, b))
            if (entry.first == 51)
                seal.Bindings.push_back({b, entry});
    seal.CodeSha256 = ProgramDigest(lines);
    return seal;
}
// anchor(): the world normal at the renderer's first SH polynomial, float4(WorldNormal, 1) read by
// three LDB/DP4 rows, produced by a DP3/RSQ/MUL normalization. A checkpoint of an original carrier.
struct MaterialBubbleNormalAnchor
{
    bool Found = false;
    MaterialAnchor Anchor;
    std::size_t ConsumerLine = 0;
};
inline MaterialBubbleNormalAnchor FindOriginalBubbleNormal(const std::vector<AnchorInstruction>& code,
                                                           const MaterialBubbleNormalSeal& seal,
                                                           const std::vector<std::string>& lines)
{
    using namespace BubbleSourceDetail;
    const auto depths = MaterialControlDepths(code);
    std::vector<MaterialBubbleNormalAnchor> hits;
    static const std::regex row("^sbo_buf(\\d+)\\[(0|16|32)\\]$");
    for (std::size_t at = 2; at + 5 < code.size(); ++at)
    {
        bool skip = false;
        for (std::size_t i = at - 2; i < at + 6; ++i)
            if (depths[i] || !Unconditional(code[i]))
                skip = true;
        if (skip)
            continue;
        std::set<int> offsets;
        std::string vector;
        bool vectorSet = false, accepted = true;
        for (std::size_t r = 0; r < 3; ++r)
        {
            const auto& load = code[at + 2 * r];
            const auto& dot = code[at + 2 * r + 1];
            const auto ops = OperandTexts(load.text);
            std::smatch m;
            if (load.op != "LDB" || load.mods != std::vector<std::string>{"F32X4"} || ops.size() < 2 ||
                !std::regex_match(ops[1], m, row) || dot.op != "DP4" || dot.mods != std::vector<std::string>{"F32"} ||
                dot.src.size() != 2)
            {
                accepted = false;
                break;
            }
            const int offset = std::stoi(m[2]);
            if (m[1].str() != "0" || dot.mask != std::string(1, "xyz"[offset / 16]))
            {
                accepted = false;
                break;
            }
            std::vector<std::size_t> positions;
            for (std::size_t i = 0; i < 2; ++i)
                if (dot.src[i].base == load.dst && !dot.src[i].negative && !dot.src[i].absolute &&
                    dot.reads(i, "xyzw").second == "xyzw")
                    positions.push_back(i);
            if (positions.size() != 1)
            {
                accepted = false;
                break;
            }
            const auto other = dot.reads(1 - positions[0], "xyzw");
            if (other.second != "xyzw" || (vectorSet && other.first != vector))
            {
                accepted = false;
                break;
            }
            vector = other.first;
            vectorSet = true;
            offsets.insert(offset);
        }
        if (!accepted || offsets != std::set<int>{0, 16, 32})
            continue;
        const auto& copy = code[at - 2];
        const auto& one = code[at - 1];
        if (copy.op != "MOV" || copy.mask != "xyz" || copy.dst != vector ||
            copy.mods != std::vector<std::string>{"F"} || copy.src.size() != 1 || copy.src[0].literal ||
            copy.src[0].negative || copy.src[0].absolute || copy.reads(0, "xyz").second != "xyz" || one.op != "MOV" ||
            one.mask != "w" || one.dst != vector || one.mods != std::vector<std::string>{"F"} || one.src.size() != 1 ||
            !one.src[0].literal || one.src[0].scalar() != 1.)
            continue;
        const auto normal = copy.src[0].base;
        auto current = normal;
        std::size_t limit = at - 2;
        bool checkpointSet = false;
        std::size_t checkpointLine = 0;
        std::string checkpointRegister;
        for (int step = 0; step < 6; ++step)
        {
            std::size_t index = 0;
            bool writer = false;
            for (std::size_t i = 0; i < limit; ++i)
                if (code[i].dst == current && code[i].mask.find_first_of("xyz") != std::string::npos)
                {
                    index = i;
                    writer = true;
                }
            if (!writer)
                break;
            const auto& producer = code[index];
            if (depths[index] || !Unconditional(producer))
                break;
            if (producer.mask != "xyz" || std::any_of(producer.src.begin(), producer.src.end(),
                                                      [](const AnchorOperand& o) { return o.negative || o.absolute; }))
                break;
            if (producer.op == "MOV" && producer.mods == std::vector<std::string>{"F"} && producer.src.size() == 1 &&
                producer.reads(0, "xyz").second == "xyz")
            {
                if (!checkpointSet)
                {
                    checkpointSet = true;
                    checkpointLine = index;
                    checkpointRegister = current;
                }
                current = producer.src[0].base;
                limit = index;
                continue;
            }
            if (producer.op != "MUL" || producer.mods != std::vector<std::string>{"F32"} || producer.src.size() != 2)
                break;
            bool normalized = false;
            for (std::size_t scalar = 0; scalar < 2 && !normalized; ++scalar)
            {
                const auto scale = producer.reads(scalar, "xyz");
                const auto src = producer.reads(1 - scalar, "xyz");
                if (scale.second.empty() ||
                    std::any_of(scale.second.begin(), scale.second.end(),
                                [&](char c) { return c != scale.second[0]; }) ||
                    src.second != "xyz")
                    continue;
                std::size_t rsqAt = 0;
                bool rsqFound = false;
                for (std::size_t i = 0; i < index; ++i)
                    if (code[i].dst == scale.first && code[i].mask.find(scale.second[0]) != std::string::npos)
                    {
                        rsqAt = i;
                        rsqFound = true;
                    }
                if (!rsqFound)
                    continue;
                const auto& rsq = code[rsqAt];
                if (depths[rsqAt] || !Unconditional(rsq) || rsq.op != "RSQ" ||
                    rsq.mods != std::vector<std::string>{"F32"} || rsq.src.size() != 1)
                    continue;
                const auto dotReg = rsq.reads(0, rsq.mask);
                std::size_t dotAt = 0;
                bool dotFound = false;
                for (std::size_t i = 0; i < rsqAt; ++i)
                    if (code[i].dst == dotReg.first && !dotReg.second.empty() &&
                        code[i].mask.find(dotReg.second[0]) != std::string::npos)
                    {
                        dotAt = i;
                        dotFound = true;
                    }
                if (!dotFound)
                    continue;
                const auto& dot = code[dotAt];
                if (!depths[dotAt] && Unconditional(dot) && dot.op == "DP3" &&
                    dot.mods == std::vector<std::string>{"F32"} && dot.src.size() == 2 &&
                    dot.reads(0, "xyz") == std::make_pair(src.first, std::string("xyz")) &&
                    dot.reads(1, "xyz") == std::make_pair(src.first, std::string("xyz")) &&
                    !AnchorWritten(code, src.first, "xyz", dotAt + 1, index))
                    normalized = true;
            }
            if (normalized)
            {
                MaterialBubbleNormalAnchor hit;
                hit.Found = true;
                hit.Anchor = checkpointSet ? MaterialAnchor::Register(checkpointLine, checkpointRegister, "xyz")
                                           : MaterialAnchor::Register(index, current, "xyz");
                hit.ConsumerLine = at - 2;
                hits.push_back(hit);
            }
            break;
        }
    }
    if (hits.size() > 1)
        throw Refused("native SH normal consumer is ambiguous", "Bubble SH normal");
    if (hits.empty())
        return {};
    if (seal.CodeSha256 != ProgramDigest(lines) || seal.Bindings.size() != 1 ||
        seal.Bindings[0].second != std::make_pair(51, 0))
        throw Refused("SH normal consumer lacks its original View.SkyIrradianceEnvironmentMap resource binding",
                      "Bubble SH normal");
    return hits[0];
}
}
} // P4MapKit::Material

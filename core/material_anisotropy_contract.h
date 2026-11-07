// Original normal-coupled anisotropy inputs. Recovery and selected CPU checks
// are separate from shader compiler, family completeness and renderer proof.
#pragma once
#include "material_prepare.h"
#include "material_normal.h"
#include "material_rewrite.h"
#include "material_lifter.h"
#include "material_source_common.h"
namespace P4MapKit
{
namespace Material
{
namespace AnisotropyDetail
{
using Lane = std::pair<std::string, int>;
using Slice = std::map<std::size_t, std::set<int>>;
inline bool Clean(const AnchorInstruction& line)
{
    return (line.mods == std::vector<std::string>{"F32"} || line.mods == std::vector<std::string>{"F"}) &&
           line.text.substr(0, line.text.find(',')).find('(') == std::string::npos;
}
inline std::vector<int> Depths(const std::vector<AnchorInstruction>& code)
{
    int end = 0;
    const auto result = MaterialControlDepths(code, &end);
    if (std::any_of(result.begin(), result.end(), [](int depth) { return depth < 0; }))
        throw Refused("anisotropy control nesting is invalid", "anisotropy source control");
    if (end)
        throw Refused("anisotropy control block is unterminated", "anisotropy source control");
    return result;
}
inline std::size_t Last(const std::vector<AnchorInstruction>& code, const std::string& reg, const std::string& comps,
                        std::size_t until)
{
    if (until > code.size() || comps.empty())
        throw Refused("anisotropy writer range is invalid", "anisotropy source range");
    std::set<std::size_t> writers;
    for (char component : comps)
    {
        std::size_t at = until;
        while (at)
        {
            --at;
            if (code[at].dst == reg && code[at].mask.find(component) != std::string::npos)
                break;
        }
        if (until == 0 || code[at].dst != reg || code[at].mask.find(component) == std::string::npos)
            return code.size();
        writers.insert(at);
    }
    return writers.size() == 1 ? *writers.begin() : code.size();
}
inline bool Unchanged(const std::vector<AnchorInstruction>& code, const std::string& reg, const std::string& comps,
                      std::size_t begin, std::size_t end)
{
    return begin < end && !AnchorWritten(code, reg, comps, begin + 1, end);
}
inline std::string Components(const AnchorOperand& operand, int size = 3)
{
    std::string result;
    for (int c = 0; c < size; ++c)
        result += operand.component(std::size_t(c));
    return result;
}
inline bool Plain(const AnchorOperand& operand)
{
    return !operand.literal && !operand.negative && !operand.absolute;
}
struct Cross
{
    bool Found = false;
    std::string A, B;
    std::size_t Begin = 0;
};
inline Cross CrossAt(const std::vector<AnchorInstruction>& code, std::size_t at)
{
    if (at >= code.size())
        return {};
    const auto& line = code[at];
    if (line.op != "MAD" || line.mask != "xyz" || line.src.size() != 3 || !line.src[2].negative ||
        line.src[2].absolute || line.src[2].literal || !Clean(line))
        return {};
    const auto before = Last(code, line.src[2].base, "xyz", at);
    if (before == code.size())
        return {};
    const auto& multiply = code[before];
    if (multiply.op != "MUL" || multiply.mask != "xyz" || multiply.src.size() != 2 || !Clean(multiply) ||
        !Plain(line.src[0]) || !Plain(line.src[1]) || !Plain(multiply.src[0]) || !Plain(multiply.src[1]) ||
        Components(line.src[2]) != "xyz" || multiply.src[0].base != line.src[0].base ||
        multiply.src[1].base != line.src[1].base || Components(line.src[0]) != "yzx" ||
        Components(line.src[1]) != "zxy" || Components(multiply.src[0]) != "zxy" ||
        Components(multiply.src[1]) != "yzx" || !Unchanged(code, line.src[0].base, "xyz", before, at) ||
        !Unchanged(code, line.src[1].base, "xyz", before, at))
        return {};
    return {true, line.src[0].base, line.src[1].base, before};
}
inline std::vector<std::pair<std::string, std::set<int>>> Reads(const AnchorInstruction& line,
                                                                const std::set<int>& selected, bool producer = true)
{
    if (IsMember(line.op, {"IF", "ELSE", "ENDIF", "REP", "ENDREP", "BRK", "RET", "LDC"}))
        return {};
    if (line.dst.empty())
        throw Refused("unreviewed anisotropy destination", "anisotropy source operands");
    const auto rest = line.text.substr(line.text.find(' ') + 1);
    const auto operands = split_operands(rest);
    std::vector<std::pair<std::string, std::set<int>>> result;
    if (IsMember(line.op, {"TEX", "TXB", "TXL", "TXF", "TXD", "TXP", "TXFMS", "TXQ", "TXG"}))
    {
        if (operands.size() != 4 || (producer && (operands[3] != "2D" || !IsMember(line.op, {"TEX", "TXB", "TXL"}))))
            throw Refused("unreviewed anisotropy texture target", "anisotropy texture source");
        std::smatch handle;
        static const std::regex pattern("^handle\\((\\w+)(?:\\.([xyzw]))?\\)$");
        if (!std::regex_match(operands[2], handle, pattern))
            throw Refused("anisotropy texture has no original handle", "anisotropy texture source");
        result.push_back({operands[1], line.op == "TEX" ? std::set<int>{0, 1} : std::set<int>{0, 1, 3}});
        result.push_back({handle[1].str() + "." + (handle[2].matched ? handle[2].str() : "x"), {0}});
        return result;
    }
    std::set<int> lanes = selected;
    if (IsMember(line.op, {"DP2", "DP3", "DP4"}))
    {
        lanes.clear();
        for (int c = 0; c < line.op[2] - '0'; ++c)
            lanes.insert(c);
    }
    else if (IsMember(line.op, {"RCP", "RSQ", "SIN", "COS", "EX2", "LG2", "POW"}))
        lanes = {0};
    for (std::size_t source = 1; source < operands.size(); ++source)
        result.push_back({operands[source], line.op == "DIV" && source == 2 ? std::set<int>{0} : lanes});
    return result;
}
inline Slice Select(const std::vector<AnchorInstruction>& code, std::size_t stop, std::set<Lane> live)
{
    if (stop > code.size() || live.empty() || live.size() > 32)
        throw Refused("anisotropy slice range/output is invalid", "anisotropy source range");
    const auto depths = Depths(code);
    Slice selected;
    static const std::regex reg("^(\\w+)(?:\\.([xyzw]+))?$");
    for (std::size_t cursor = stop; cursor;)
    {
        const auto at = --cursor;
        const auto& line = code[at];
        std::set<Lane> writes, needed;
        for (char c : line.mask)
            writes.insert({line.dst, NumericMachine::component(c)});
        std::set_intersection(writes.begin(), writes.end(), live.begin(), live.end(),
                              std::inserter(needed, needed.end()));
        if (needed.empty())
            continue;
        if (depths[at] || line.text.substr(0, line.text.find(',')).find('(') != std::string::npos)
            throw Refused("anisotropy producer is conditional", "anisotropy source conditional");
        auto& lanes = selected[at];
        for (const auto& lane : needed)
            lanes.insert(lane.second);
        for (const auto& lane : writes)
            live.erase(lane);
        for (const auto& read : Reads(line, lanes))
        {
            auto operand = trim(read.first);
            if (starts(operand, "-"))
                operand = trim(operand.substr(1));
            if (operand.size() >= 2 && operand.front() == '|' && operand.back() == '|')
                operand = trim(operand.substr(1, operand.size() - 2));
            std::smatch match;
            if (!std::regex_match(operand, match, reg))
                continue;
            const AnchorOperand source(operand);
            for (int lane : read.second)
                live.insert({match[1], NumericMachine::component(source.component(std::size_t(lane)))});
        }
    }
    if (!live.empty())
        throw Refused("anisotropy producer reads uninitialized lanes", "anisotropy source uninitialized");
    return selected;
}
inline SymbolicMachine Trace(const std::vector<std::string>& lines, std::size_t stop, const std::set<Lane>& outputs,
                             std::shared_ptr<SymbolicArena> arena = std::make_shared<SymbolicArena>())
{
    const auto code = ParseMaterialAnchorInstructions(lines);
    const auto selected = Select(code, stop, outputs);
    SymbolicMachine machine(lines, std::move(arena));
    for (const auto& step : selected)
        machine.step(step.first, lines[step.first]);
    return machine;
}
}
struct MaterialAnisotropyProjection
{
    std::size_t Line = 0, RawLine = 0;
    std::string Register, Normal, Tangent;
};
inline MaterialAnisotropyProjection FindOriginalAnisotropyProjection(const std::vector<AnchorInstruction>& code)
{
    using namespace AnisotropyDetail;
    const auto depths = Depths(code);
    std::vector<MaterialAnisotropyProjection> found;
    for (std::size_t at = 0; at < code.size(); ++at)
    {
        const auto& line = code[at];
        if (line.op != "MUL" || line.mask != "xyz" || line.src.size() != 2 || depths[at] || !Clean(line))
            continue;
        for (std::size_t order = 0; order < 2; ++order)
        {
            const auto& inverse = line.src[order];
            const auto& vector = line.src[1 - order];
            if (!Plain(inverse) || !Plain(vector))
                continue;
            const auto component = inverse.component(0);
            if (inverse.component(1) != component || inverse.component(2) != component)
                continue;
            const auto rootAt = Last(code, inverse.base, std::string(1, component), at);
            if (rootAt == code.size() || code[rootAt].op != "RSQ" || code[rootAt].src.size() != 1)
                continue;
            const auto& root = code[rootAt].src[0];
            const auto dotAt =
                Last(code, root.base, std::string(1, root.component(std::size_t(NumericMachine::component(component)))),
                     rootAt);
            if (dotAt == code.size())
                continue;
            const auto& dot = code[dotAt];
            if (dot.op != "DP3" || dot.src.size() != 2 || dot.src[0].base != vector.base ||
                dot.src[1].base != vector.base)
                continue;
            const auto secondAt = Last(code, vector.base, "xyz", dotAt);
            if (secondAt == code.size())
                continue;
            const auto second = CrossAt(code, secondAt);
            if (!second.Found)
                continue;
            const auto firstAt = Last(code, second.A, "xyz", second.Begin);
            if (firstAt == code.size())
                continue;
            const auto first = CrossAt(code, firstAt);
            if (!first.Found || first.A != second.B || Components(vector) != "xyz" || !Plain(dot.src[0]) ||
                !Plain(dot.src[1]) || Components(dot.src[0]) != "xyz" || Components(dot.src[1]) != "xyz" ||
                !Plain(root) || !Unchanged(code, vector.base, "xyz", secondAt, at) ||
                !Unchanged(code, inverse.base, std::string(1, component), rootAt, at) ||
                !Unchanged(code, first.A, "xyz", first.Begin, second.Begin))
                continue;
            bool clean = true;
            for (auto index : {at, rootAt, dotAt, secondAt, firstAt, first.Begin, second.Begin})
                clean = clean && Clean(code[index]) && !depths[index];
            if (clean)
                found.push_back({at + 1, first.Begin, line.dst, first.A, first.B});
        }
    }
    if (found.size() != 1)
        throw Refused("anisotropy has no unique normal-coupled tangent projection", "anisotropy projection");
    return found.front();
}
struct MaterialAnisotropyScalar
{
    std::size_t Line = 0;
    std::string Register, Components;
    Value Formula;
};
inline MaterialAnisotropyScalar FindOriginalAnisotropyScalar(const MaterialPreparedProgram& program,
                                                             std::shared_ptr<SymbolicArena> arena)
{
    using namespace AnisotropyDetail;
    const auto code = ParseMaterialAnchorInstructions(program.Lines);
    const auto depths = Depths(code);
    std::vector<MaterialAnisotropyScalar> found;
    for (std::size_t at = 0; at < code.size(); ++at)
    {
        const auto& clamp = code[at];
        if (clamp.op != "MAX" || clamp.src.size() != 2 || !clamp.src[1].literal || clamp.src[1].scalar() != -1 ||
            clamp.mask.size() != 1 || depths[at] || !Clean(clamp) || !Plain(clamp.src[0]))
            continue;
        const auto minimumAt = Last(code, clamp.src[0].base, std::string(1, clamp.src[0].component(0)), at);
        if (minimumAt == code.size())
            continue;
        const auto& minimum = code[minimumAt];
        if (minimum.op != "MIN" || minimum.src.size() != 2 || !minimum.src[1].literal || minimum.src[1].scalar() != 1 ||
            !Clean(minimum) || !Plain(minimum.src[0]) || minimum.mask.size() != 1)
            continue;
        const auto lane = NumericMachine::component(
            minimum.src[0].component(std::size_t(NumericMachine::component(minimum.mask[0]))));
        const auto trace = Trace(program.Lines, minimumAt, {{minimum.src[0].base, lane}}, arena);
        Binder binder(*arena, program.BaseBinding);
        const auto formula = binder.bind(trace.reg.at(minimum.src[0].base)[std::size_t(lane)]);
        if (!binder.assumed.empty())
            throw Refused("anisotropy scalar binding assumed engine words", "anisotropy scalar binding");
        if (same(formula, source(*arena, "ScalarParameter", "Anisotropy")))
            found.push_back({at + 1, clamp.dst, clamp.mask, formula});
    }
    if (found.size() != 1)
        throw Refused("anisotropy has no unique named scalar clamp", "anisotropy scalar clamp");
    return found.front();
}
inline std::size_t OriginalAnisotropyConsumer(const std::vector<AnchorInstruction>& code, const std::string& reg,
                                              const std::string& comps, std::size_t start)
{
    if (start > code.size())
        throw Refused("anisotropy consumer range invalid", "anisotropy source range");
    for (std::size_t at = start; at < code.size(); ++at)
    {
        std::set<int> lanes;
        for (char c : code[at].mask)
            lanes.insert(NumericMachine::component(c));
        for (const auto& read : AnisotropyDetail::Reads(code[at], lanes, false))
        {
            AnchorOperand operand(read.first);
            if (operand.base == reg)
                for (int lane : read.second)
                    if (comps.find(operand.component(std::size_t(lane))) != std::string::npos)
                        return at + 1;
        }
        if (code[at].dst == reg)
            for (char c : comps)
                if (code[at].mask.find(c) != std::string::npos)
                    throw Refused("anisotropy carrier overwritten before consumer", "anisotropy downstream consumer");
    }
    throw Refused("anisotropy carrier has no downstream consumer", "anisotropy downstream consumer");
}
struct MaterialAnisotropyInputs
{
    std::shared_ptr<SymbolicArena> Arena;
    MaterialAnisotropyProjection Projection;
    MaterialAnisotropyScalar Scalar;
    std::vector<Value> Tangent;
};
inline void CertifyOriginalAnisotropyTextureSource(const MaterialPreparedProgram& program,
                                                   const MaterialAnisotropyProjection& projection,
                                                   const MaterialAnisotropyScalar& scalar)
{
    using namespace AnisotropyDetail;
    int view = -1;
    for (const auto& entry : program.BaseBinding.Buffers)
        if (entry.second == "View")
        {
            if (view != -1)
                throw Refused("anisotropy View binding is not unique", "anisotropy texture source");
            view = entry.first;
        }
    if (view < 0)
        throw Refused("anisotropy View binding is absent", "anisotropy texture source");
    const auto code = ParseMaterialAnchorInstructions(program.Lines);
    std::set<Lane> outputs = {{projection.Register, 0},
                              {projection.Register, 1},
                              {projection.Register, 2},
                              {scalar.Register, NumericMachine::component(scalar.Components.at(0))}};
    const auto selected = Select(code, std::max(projection.Line, scalar.Line), outputs);
    for (const auto& step : selected)
    {
        const auto& instruction = code[step.first];
        if (!IsMember(instruction.op, {"TEX", "TXB", "TXL"}))
            continue;
        const auto operands = split_operands(instruction.text.substr(instruction.text.find(' ') + 1));
        std::smatch coordinate, handle;
        static const std::regex vector("^(\\w+)\\.([xyzw]{4})$"), descriptor("^handle\\((\\w+)\\.([xyzw])\\)$");
        if (operands.size() != 4 || !std::regex_match(operands[1], coordinate, vector) ||
            !std::regex_match(operands[2], handle, descriptor))
            throw Refused("anisotropy sample coordinates/handle are not explicit", "anisotropy texture source");
        std::set<Lane> reads;
        for (int component : {0, 1, 3})
            reads.insert({coordinate[1], NumericMachine::component(coordinate[2].str()[std::size_t(component)])});
        reads.insert({handle[1], NumericMachine::component(handle[2].str()[0])});
        auto machine = Trace(program.Lines, step.first, reads);
        const auto handles = machine.handle(operands[2]);
        if (handles.first < 328 || handles.second < 1352 || (handles.first - 328) % 8 || (handles.second - 1352) % 8)
            throw Refused("anisotropy sample handle is not an exact cache slot", "anisotropy texture source");
        const auto texture = (handles.first - 328) / 8, sampler = (handles.second - 1352) / 8;
        const auto name = program.BaseBinding.TextureParameters.find(texture),
                   alias = program.SamplerParameters.find(sampler);
        if (texture > 63 || sampler > 31 || name == program.BaseBinding.TextureParameters.end() ||
            alias == program.SamplerParameters.end() || name->second != alias->second)
            throw Refused("anisotropy sample has no exact asset sampler", "anisotropy texture source");
        if (instruction.op != "TXB" || operands[3] != "2D")
            throw Refused("anisotropy sample is not automatic biased 2D", "anisotropy texture source");
        const auto wanted = machine.arena->mk("IN", {"buffer", view, 2452});
        if (!same(machine.read(operands[1])[3], wanted))
            throw Refused("anisotropy bias is not View.MaterialTextureMipBias", "anisotropy texture source");
    }
}
inline MaterialAnisotropyInputs RecoverOriginalAnisotropyInputs(const MaterialPreparedProgram& program)
{
    MaterialAnisotropyInputs result;
    result.Arena = std::make_shared<SymbolicArena>();
    const auto code = ParseMaterialAnchorInstructions(program.Lines);
    result.Projection = FindOriginalAnisotropyProjection(code);
    result.Scalar = FindOriginalAnisotropyScalar(program, result.Arena);
    const auto& projection = result.Projection;
    CertifyOriginalAnisotropyTextureSource(program, projection, result.Scalar);
    const auto trace = AnisotropyDetail::Trace(
        program.Lines, projection.RawLine, {{projection.Tangent, 0}, {projection.Tangent, 1}, {projection.Tangent, 2}},
        result.Arena);
    Binder binder(*result.Arena, program.BaseBinding);
    std::vector<Value> world;
    for (int c = 0; c < 3; ++c)
        world.push_back(binder.bind(trace.reg.at(projection.Tangent)[std::size_t(c)]));
    if (!binder.assumed.empty())
        throw Refused("anisotropy tangent binding assumed engine words", "anisotropy tangent binding");
    // The tangent is the normalized vertex TangentX unless the program changes it; a changed tangent is a tangent
    // map, recovered by its raw tangent-space inverse.
    std::vector<Value> tx;
    for (int c = 0; c < 3; ++c)
        tx.push_back(source(*result.Arena, "TangentX", Value::null(), c));
    const auto inverse = result.Arena->mk("RSQ", {NormalDetail::dot(*result.Arena, tx)});
    bool vertex = true;
    for (int c = 0; c < 3; ++c)
        vertex = vertex && same(world[std::size_t(c)], simplify(*result.Arena, "MUL", {inverse, tx[std::size_t(c)]}));
    if (vertex)
        result.Tangent = {1., 0., 0.};
    else
    {
        const auto material = MaterialNormal(*result.Arena, frame_transforms(*result.Arena, world));
        if (!material.tangent_space || !material.has_pin)
            throw Refused("anisotropy tangent map has no raw tangent-space inverse", "anisotropy tangent inverse");
        result.Tangent = material.pin;
    }
    return result;
}
}
} // P4MapKit::Material

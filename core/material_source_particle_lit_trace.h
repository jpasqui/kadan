// Exact additional CubeArray operand form used by original material prefixes.
// No instruction is rewritten and no sampled value is supplied by this reader.
#pragma once
#include "material_source_emission.h"
namespace P4MapKit
{
namespace Material
{
class OriginalParticleLitSymbolicMachine : public SymbolicMachine
{
public:
    using SymbolicMachine::SymbolicMachine;
    void step(std::size_t index, const std::string& line) override
    {
        const auto space = line.find(' ');
        const auto head = line.substr(0, space);
        const auto operands = split_operands(space == std::string::npos ? "" : line.substr(space + 1));
        if (!operands.empty() && operands.back() == "ARRAYCUBE")
        {
            if (head.substr(0, head.find('.')) != "TXL" || operands.size() != 5)
                throw EvalError("unchecked CubeArray operand form");
            std::vector<std::string> mods;
            for (auto dot = head.find('.'); dot != std::string::npos;)
            {
                const auto next = head.find('.', dot + 1);
                mods.push_back(head.substr(dot + 1, next == std::string::npos ? next : next - dot - 1));
                dot = next;
            }
            const auto h = handle(operands[3]);
            const auto coordinates = read(operands[1]);
            const auto lod = read(operands[2])[0];
            std::vector<Value> args = {"TXL", "ARRAYCUBE", h.first < 0 ? Value::null() : Value(h.first),
                                       h.second < 0 ? Value::null() : Value(h.second)};
            args.insert(args.end(), coordinates.begin(), coordinates.end());
            args.push_back(lod);
            const auto tex = arena->mk("TEX", args);
            Vec value;
            for (int c = 0; c < 4; ++c)
                value[c] = arena->mk("COMP", {tex, c});
            write(operands[0], value, mods);
            return;
        }
        SymbolicMachine::step(index, line);
    }
    void run_range(std::size_t start, std::size_t end, std::size_t stop)
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
                    a.run_range(pc + 1, range.first == lines.size() ? range.second : range.first,
                                std::numeric_limits<std::size_t>::max());
                    if (range.first != lines.size())
                        b.run_range(range.first + 1, range.second, std::numeric_limits<std::size_t>::max());
                    std::set<std::string> names;
                    for (const auto& r : a.reg)
                        names.insert(r.first);
                    for (const auto& r : b.reg)
                        names.insert(r.first);
                    for (const auto& name : names)
                    {
                        const auto av = a.registered(name), bv = b.registered(name);
                        Vec value;
                        for (int c = 0; c < 4; ++c)
                            value[c] = select(test, av[c], bv[c]);
                        reg[name] = value;
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
    OriginalParticleLitSymbolicMachine& run(std::size_t stop = std::numeric_limits<std::size_t>::max()) override
    {
        run_range(0, lines.size(), stop);
        return *this;
    }
};
inline std::vector<Value> ReadOriginalParticleLitScheduledValue(const MaterialScheduledAnchor& anchor,
                                                                const OriginalParticleLitSymbolicMachine& prefix)
{
    if (anchor.ValueAnchor.has_constant)
        return {Value(anchor.ValueAnchor.constant)};
    auto later = prefix;
    std::size_t previous = 0;
    bool first = true;
    for (const auto at : anchor.Indices)
    {
        if (at >= prefix.lines.size() || (!first && at <= previous) || at < anchor.ValueAnchor.line)
            throw Refused("invalid native material schedule", "material scheduled range");
        later.step(at, prefix.lines[at]);
        previous = at;
        first = false;
    }
    const auto found = later.reg.find(anchor.ValueAnchor.register_name);
    if (found == later.reg.end())
        throw Refused("scheduled material register is absent", "material scheduled register");
    std::vector<Value> out;
    for (const auto c : anchor.ValueAnchor.comps)
    {
        const auto at = std::string("xyzw").find(c);
        if (at == std::string::npos)
            throw Refused("invalid native scheduled component", "material scheduled component");
        out.push_back(found->second[at]);
    }
    return out;
}
inline MaterialLateEmissionInputs
RecoverOriginalParticleLitEmissionInputs(const MaterialRecoveryPlan& plan,
                                         const OriginalParticleLitSymbolicMachine& prefix)
{
    if (plan.Lines != prefix.lines || plan.Stop > plan.Lines.size())
        throw Refused("emission source differs from its original prefix", "material scheduled program");
    const auto code = ParseMaterialAnchorInstructions(plan.Lines);
    MaterialLateEmissionInputs result;
    result.Anchor = FindScheduledMaterialEmissiveAnchor(code, plan.Stop);
    std::vector<Value> raw;
    auto& arena = *prefix.arena;
    if (result.Anchor.Found)
        raw = ReadOriginalParticleLitScheduledValue(result.Anchor.Value, prefix);
    else
    {
        std::string reg, comps;
        if (!code.empty() && starts(code.back().dst, "result"))
        {
            const auto end = std::max(plan.Stop, code.size() > 26 ? code.size() - 26 : std::size_t(0));
            for (std::size_t at = code.size() > 1 ? code.size() - 2 : 0; at > end; --at)
            {
                const auto& line = code[at];
                if (line.op != "MAX" || line.mask != "xyz" || line.src.size() != 2 || !line.src[1].literal ||
                    line.src[1].scalar() != 0 || line.src[0].literal)
                    continue;
                const auto input = line.reads(0, "xyz");
                if (!AnchorWritten(code, input.first, input.second, plan.Stop, at))
                {
                    reg = input.first;
                    comps = input.second;
                }
                break;
            }
        }
        if (reg.empty())
        {
            int collection = -1;
            for (const auto& buffer : plan.Binding.Buffers)
                if (starts(buffer.second, "MaterialCollection"))
                {
                    collection = buffer.first;
                    break;
                }
            if (collection < 0)
                return result;
            std::set<const Expression*> leaves;
            for (const auto offset : {0, 4})
                leaves.insert(arena.mk("IN", {"buffer", collection, offset}).expression.get());
            std::vector<std::string> candidates;
            for (const auto& row : prefix.reg)
            {
                if (std::any_of(row.second.begin(), row.second.end(), [](const Value& value)
                                { return value.kind == Value::Tuple || value.kind == Value::Handle; }))
                    continue;
                bool depending = false;
                for (int c = 0; c < 3; ++c)
                    depending = depending || OriginalMaterialDependsOnEmission(row.second[std::size_t(c)], leaves);
                if (depending && OriginalMaterialReadsBeforeWrite(code, row.first, 'x', plan.Stop) &&
                    OriginalMaterialReadsBeforeWrite(code, row.first, 'y', plan.Stop) &&
                    OriginalMaterialReadsBeforeWrite(code, row.first, 'z', plan.Stop))
                    candidates.push_back(row.first);
            }
            if (candidates.empty())
                return result;
            if (candidates.size() != 1)
                throw Refused("several registers could be the original emissive color",
                              "several registers could be the emissive color");
            reg = candidates[0];
            comps = "xyz";
        }
        const auto found = prefix.reg.find(reg);
        if (found == prefix.reg.end())
            throw Refused("original emission register was never written", "material output register");
        for (const auto c : comps)
            raw.push_back(found->second.at(std::string("xyzw").find(c)));
    }
    auto context = plan.Binding;
    if (plan.ConfigureBinding)
        plan.ConfigureBinding(arena, context);
    Binder binder(arena, context);
    std::vector<Value> values;
    for (const auto& value : raw)
        values.push_back(BindMaterialComponent(binder, value));
    result.Found = true;
    result.Zero = std::all_of(values.begin(), values.end(),
                              [](const Value& value) { return IsNumber(value) && value.number() == 0; });
    result.Bound.Arena = prefix.arena;
    if (!result.Zero)
    {
        result.Bound.Pins = {"EmissiveColor"};
        result.Bound.Values = {values};
    }
    result.Bound.AssumedEngineFields = binder.assumed;
    result.Bound.CanonicalUnitFields = binder.canonical_unit_fields;
    return result;
}
}
} // P4MapKit::Material

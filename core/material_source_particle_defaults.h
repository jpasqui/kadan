// Joint particle/local formulas use compiled literal candidates and exact
// symbolic source projections. No sampled values establish an authored default.
#pragma once
#include "material_source_particle_subuv.h"
#include "material_rewrite.h"
namespace P4MapKit
{
namespace Material
{
namespace ParticleSourceDetail
{
inline bool Number(const Value& v)
{
    return v.kind == Value::Float || v.kind == Value::Int;
}
inline bool FiniteLeaf(const Value& v)
{
    if (!symbolic(v))
        return Number(v) && std::isfinite(v.number());
    if (!is(v, "COMP") || v.expression->args.size() != 2)
        return false;
    const auto& p = v.expression->args[0];
    return is(p, "SAMPLE") || is(p, "FIXED_SAMPLE") ||
           (is(p, "SRC") && p.expression->args.size() == 2 && p.expression->args[0].kind == Value::Text &&
            p.expression->args[0].text == "VertexColor");
}
inline bool Nonzero(const Value& v)
{
    if (!symbolic(v))
        return Number(v) && v.number() != 0;
    if (is(v, "COMP") && is(v.expression->args[0], "SRC"))
    {
        const auto& parent = v.expression->args[0].expression->args;
        return parent.size() == 2 && parent[0].kind == Value::Text && parent[0].text == "SvPosition" &&
               NumberIs(v.expression->args[1], 3);
    }
    if (is(v, "MAX") || is(v, "MIN"))
        for (const auto& a : v.expression->args)
            if (Number(a) && (is(v, "MAX") ? a.number() > 0 : a.number() < 0))
                return true;
    return false;
}
inline Value Simplify(SymbolicArena& arena, const Value& v)
{
    if (!symbolic(v))
        return v;
    const auto& x = *v.expression;
    if (x.op == "MUL" && x.args.size() == 2)
        for (std::size_t side = 0; side < 2; ++side)
            if (NumberIs(x.args[side], 0) && FiniteLeaf(x.args[1 - side]))
                return 0.;
    if (x.op == "DIV" && x.args.size() == 2 && Nonzero(x.args[1]))
        return simplify(arena, "MUL", {x.args[0], arena.op("RCP", {x.args[1]})});
    if (IsMember(x.op, {"ADD", "MUL", "SUB", "DIV", "MIN", "MAX", "NEG", "ABS", "SAT", "RCP", "RSQ", "SIN", "COS",
                        "EX2", "LG2", "POW", "TOINT", "ARRAYINDEX"}) ||
        starts(x.op, "U.") || starts(x.op, "I.") || starts(x.op, "F.") || starts(x.op, "T.") || starts(x.op, "S.") ||
        starts(x.op, "UC."))
        return simplify(arena, x.op, x.args);
    return v;
}
inline std::vector<Value> Normalize(SymbolicArena& arena, const std::vector<Value>& values)
{
    return rewrite(arena, values, [&](const Value& v) { return Simplify(arena, v); });
}
inline std::vector<Value> LocalFallback(SymbolicArena& arena, const std::vector<Value>& values,
                                        const std::array<double, 4>& defaults)
{
    return rewrite(
        arena, values,
        [&](const Value& v)
        {
            if (is(v, "COMP") && is(v.expression->args[0], "SRC"))
            {
                const auto& a = v.expression->args[0].expression->args;
                const auto& component = v.expression->args[1];
                if (a.size() != 2 || a[0].kind != Value::Text || !Number(component) || component.number() < 0 ||
                    component.number() > 3 || std::floor(component.number()) != component.number())
                    throw Refused("particle source component metadata is invalid",
                                  "material particle source component");
                const auto c = int(component.number());
                const auto& kind = a[0].text;
                if (kind == "ParticleColor")
                    return Value(1.);
                if (kind == "ParticleDynamicParameter" && NumberIs(a[1], 0))
                    return Value(defaults[std::size_t(c)]);
                if (kind == "ParticleSubUVProperty")
                    return NumberIs(a[1], 0) || NumberIs(a[1], 1) ? source(arena, "TexCoord", 0, c) : Value(0.);
                if (kind == "ParticlePositionWS")
                    return simplify(arena, "NEG", {source(arena, "View", "pre_view_translation", c)});
                if (kind == "ParticleTranslatedPosition")
                    return Value(0.);
                if (kind == "ParticleRadius")
                    return Value(.001);
            }
            return Simplify(arena, v);
        });
}
struct Form
{
    std::string Tag;
    double Number = 0;
    std::vector<std::size_t> Children;
};
class Canonical
{
    std::map<std::string, std::size_t> intern;
    std::vector<Form> forms;
    std::map<const Expression*, std::size_t> memo;
    std::set<std::size_t> finite;
    std::size_t make(const std::string& tag, double number, const std::vector<std::size_t>& children)
    {
        if (number == 0)
            number = 0;
        std::uint64_t bits;
        std::memcpy(&bits, &number, 8);
        std::string key = std::to_string(tag.size()) + ":" + tag + ":" + std::to_string(bits);
        for (const auto child : children)
            key += ":" + std::to_string(child);
        const auto hit = intern.find(key);
        if (hit != intern.end())
            return hit->second;
        if (forms.size() >= 1000000)
            throw Refused("particle canonical expression budget exceeded", "material particle formula budget");
        const auto id = forms.size();
        forms.push_back({tag, number, children});
        intern.emplace(std::move(key), id);
        return id;
    }
    std::size_t multiply(double coefficient, std::vector<std::size_t> factors)
    {
        if (factors.empty())
            return make("number", coefficient, {});
        if (coefficient == 1 && factors.size() == 1)
            return factors[0];
        if (coefficient == 0 && factors.size() == 1 && finite.count(factors[0]))
            return make("number", 0, {});
        std::sort(factors.begin(), factors.end());
        return make("product", coefficient, factors);
    }
    std::size_t atom(const Value& v)
    {
        if (Number(v))
            return make("number", v.number(), {});
        return make("literal:" + value_key(v), 0, {});
    }

public:
    std::size_t value(const Value& root)
    {
        if (!symbolic(root))
            return atom(root);
        struct Pending
        {
            Value Node;
            std::size_t Next = 0;
        };
        std::vector<Pending> stack = {{root, 0}};
        while (!stack.empty())
        {
            auto& frame = stack.back();
            const auto& x = *frame.Node.expression;
            if (memo.count(&x))
            {
                stack.pop_back();
                continue;
            }
            if (frame.Next < x.args.size())
            {
                const auto child = x.args[frame.Next++];
                if (symbolic(child) && !memo.count(child.expression.get()))
                    stack.push_back({child, 0});
                continue;
            }
            std::vector<std::size_t> args;
            for (const auto& a : x.args)
                args.push_back(symbolic(a) ? memo.at(a.expression.get()) : atom(a));
            std::size_t out;
            if (x.op == "MUL" || x.op == "NEG")
            {
                double coefficient = x.op == "NEG" ? -1. : 1.;
                std::vector<std::size_t> factors;
                for (const auto arg : args)
                {
                    const auto f = forms[arg];
                    if (f.Tag == "number")
                        coefficient *= f.Number;
                    else if (f.Tag == "product")
                    {
                        coefficient *= f.Number;
                        factors.insert(factors.end(), f.Children.begin(), f.Children.end());
                    }
                    else
                        factors.push_back(arg);
                }
                out = multiply(coefficient, factors);
            }
            else if (x.op == "ADD")
            {
                double constant = 0;
                std::vector<std::size_t> terms;
                for (const auto arg : args)
                {
                    const auto f = forms[arg];
                    if (f.Tag == "sum")
                    {
                        constant += f.Number;
                        terms.insert(terms.end(), f.Children.begin(), f.Children.end());
                    }
                    else if (f.Tag == "number")
                        constant += f.Number;
                    else
                        terms.push_back(arg);
                }
                std::map<std::size_t, double> groups;
                std::vector<std::size_t> other;
                for (const auto term : terms)
                {
                    double coefficient = 1;
                    auto factor = term;
                    const auto f = forms[term];
                    if (f.Tag == "product" && f.Children.size() == 1)
                    {
                        coefficient = f.Number;
                        factor = f.Children[0];
                    }
                    if (finite.count(factor))
                        groups[factor] += coefficient;
                    else
                        other.push_back(term);
                }
                for (const auto& group : groups)
                    if (group.second != 0)
                        other.push_back(multiply(group.second, {group.first}));
                if (other.empty())
                    out = make("number", constant, {});
                else if (constant == 0 && other.size() == 1)
                    out = other[0];
                else
                {
                    std::sort(other.begin(), other.end());
                    out = make("sum", constant, other);
                }
            }
            else
                out = make("operation:" + x.op, 0, args);
            if (FiniteLeaf(frame.Node))
                finite.insert(out);
            memo[&x] = out;
            stack.pop_back();
        }
        return memo.at(root.expression.get());
    }
};
// Source-qualified bounded polynomial comparison additionally admits compiler
// distribution only over the same finite source domains as the original helper.
class Polynomial
{
    using Terms = std::map<std::vector<std::size_t>, double>;
    std::map<const Expression*, bool> finiteMemo;
    std::map<const Expression*, Terms> memo;
    std::map<std::string, std::size_t> atoms;
    std::size_t atom(const std::string& key)
    {
        auto r = atoms.emplace(key, atoms.size());
        return r.first->second;
    }
    static std::string key(const Terms& terms)
    {
        std::string out;
        for (const auto& t : terms)
        {
            std::uint64_t bits;
            std::memcpy(&bits, &t.second, 8);
            out += "[" + std::to_string(bits);
            for (const auto id : t.first)
                out += ":" + std::to_string(id);
            out += "]";
        }
        return out;
    }
    static void trimTerms(Terms& terms)
    {
        if (terms.size() > 256)
            throw Refused("particle compiler distribution exceeds bound", "material particle distribution bound");
        for (auto i = terms.begin(); i != terms.end();)
            if (i->second == 0)
                i = terms.erase(i);
            else
                ++i;
    }

public:
    bool finiteValue(const Value& root)
    {
        if (!symbolic(root))
            return Number(root) && std::isfinite(root.number());
        struct Frame
        {
            Value Node;
            std::size_t Next = 0;
        };
        std::vector<Frame> stack = {{root, 0}};
        while (!stack.empty())
        {
            auto& frame = stack.back();
            const auto& x = *frame.Node.expression;
            if (finiteMemo.count(&x))
            {
                stack.pop_back();
                continue;
            }
            if (frame.Next < x.args.size())
            {
                auto child = x.args[frame.Next++];
                if (symbolic(child) && !finiteMemo.count(child.expression.get()))
                    stack.push_back({child, 0});
                continue;
            }
            const auto all = [&]()
            {
                return std::all_of(x.args.begin(), x.args.end(),
                                   [&](const Value& a)
                                   {
                                       return symbolic(a) ? finiteMemo.at(a.expression.get())
                                                          : Number(a) && std::isfinite(a.number());
                                   });
            };
            bool result = FiniteLeaf(frame.Node);
            if (!result && x.op == "COMP" && x.args.size() == 2 && is(x.args[0], "SRC"))
            {
                const auto& a = x.args[0].expression->args;
                result = a.size() == 2 && a[0].kind == Value::Text &&
                         IsMember(a[0].text, {"ParticleColor", "VertexColor", "VectorParameter", "ScalarParameter",
                                              "EyeAdaptation"});
            }
            else if (IsMember(x.op, {"ADD", "MUL", "NEG", "MAX", "MIN", "SAT"}))
                result = all();
            else if (x.op == "RCP" && x.args.size() == 1 && is(x.args[0], "MAX"))
            {
                const auto& denominator = *x.args[0].expression;
                result = std::any_of(denominator.args.begin(), denominator.args.end(),
                                     [](const Value& a) { return Number(a) && a.number() > 0; }) &&
                         finiteMemo.at(&denominator);
            }
            finiteMemo[&x] = result;
            stack.pop_back();
        }
        return finiteMemo.at(root.expression.get());
    }
    Terms value(const Value& root)
    {
        if (!symbolic(root))
        {
            if (Number(root))
                return root.number() == 0 ? Terms() : Terms{{{}, root.number()}};
            return {{{atom("literal:" + value_key(root))}, 1.}};
        }
        struct Frame
        {
            Value Node;
            std::size_t Next = 0;
        };
        std::vector<Frame> stack = {{root, 0}};
        while (!stack.empty())
        {
            auto& frame = stack.back();
            const auto& x = *frame.Node.expression;
            if (memo.count(&x))
            {
                stack.pop_back();
                continue;
            }
            if (frame.Next < x.args.size())
            {
                auto child = x.args[frame.Next++];
                if (symbolic(child) && !memo.count(child.expression.get()))
                    stack.push_back({child, 0});
                continue;
            }
            Terms result;
            const auto child = [&](const Value& a)
            {
                return symbolic(a) ? memo.at(a.expression.get()) : value(a);
            };
            if (x.op == "ADD" || x.op == "NEG")
            {
                for (const auto& a : x.args)
                    for (const auto& t : child(a))
                        result[t.first] += t.second * (x.op == "NEG" ? -1. : 1.);
                trimTerms(result);
            }
            else if (x.op == "MUL")
            {
                result[{}] = 1;
                for (const auto& a : x.args)
                {
                    const auto other = child(a);
                    if (result.size() * other.size() > 256)
                        throw Refused("particle compiler distribution exceeds bound",
                                      "material particle distribution bound");
                    Terms expanded;
                    for (const auto& t : result)
                        for (const auto& u : other)
                        {
                            auto m = t.first;
                            m.insert(m.end(), u.first.begin(), u.first.end());
                            std::sort(m.begin(), m.end());
                            expanded[m] += t.second * u.second;
                        }
                    result = std::move(expanded);
                }
                trimTerms(result);
            }
            else
            {
                std::vector<std::string> args;
                const bool numeric = IsMember(x.op, {"MAX", "MIN", "SAT", "RCP"});
                for (const auto& a : x.args)
                    args.push_back(symbolic(a) || (numeric && Number(a)) ? "poly:" + key(child(a))
                                                                         : "literal:" + value_key(a));
                if (x.op == "MAX" || x.op == "MIN")
                    std::sort(args.begin(), args.end());
                std::string k = "operation:" + x.op;
                for (const auto& a : args)
                    k += ":" + std::to_string(a.size()) + ":" + a;
                result[{atom(k)}] = 1;
            }
            memo[&x] = result;
            stack.pop_back();
        }
        return memo.at(root.expression.get());
    }
};
inline bool Equal(const std::vector<Value>& a, const std::vector<Value>& b)
{
    if (a.size() != b.size())
        return false;
    Canonical canonical;
    bool direct = true;
    for (std::size_t i = 0; i < a.size(); ++i)
        direct = direct && (canonical.value(a[i]) == canonical.value(b[i]));
    if (direct)
        return true;
    Polynomial polynomial;
    for (const auto& v : a)
        if (!polynomial.finiteValue(v))
            return false;
    for (const auto& v : b)
        if (!polynomial.finiteValue(v))
            return false;
    try
    {
        for (std::size_t i = 0; i < a.size(); ++i)
            if (polynomial.value(a[i]) != polynomial.value(b[i]))
                return false;
    }
    catch (const Refused&)
    {
        return false;
    }
    return true;
}
inline bool Contains(const std::vector<Value>& values, const std::string& sourceKind)
{
    for (const auto& node : walk_formulas(values))
        if (is(node, "SRC") && node.expression->args.size() == 2 && node.expression->args[0].kind == Value::Text &&
            node.expression->args[0].text == sourceKind)
            return true;
    return false;
}
inline bool Arithmetic(const std::string& op)
{
    return IsMember(op,
                    {"NEG", "ABS", "SAT", "POW", "DIV", "TOINT", "ARRAYINDEX", "ADD",     "SUB",   "MUL",  "MIN", "MAX",
                     "RCP", "RSQ", "SIN", "COS", "EX2", "LG2",   "TRUNC.U",    "TRUNC.S", "I2F.U", "I2F.S"}) ||
           starts(op, "U.") || starts(op, "I.") || starts(op, "F.") || starts(op, "S.") || starts(op, "UC.") ||
           starts(op, "T.");
}
inline std::vector<std::array<double, 4>> Candidates(SymbolicArena& arena, const std::vector<Value>& values,
                                                     const std::vector<Value>& local)
{
    std::set<int> used;
    for (const auto& node : walk_formulas(values))
        if (is(node, "COMP") && is(node.expression->args[0], "SRC"))
        {
            const auto& a = node.expression->args[0].expression->args;
            if (a.size() == 2 && a[0].kind == Value::Text && a[0].text == "ParticleDynamicParameter" &&
                NumberIs(a[1], 0))
            {
                const auto& c = node.expression->args[1];
                if (!Number(c) || c.number() < 0 || c.number() > 3 || std::floor(c.number()) != c.number())
                    throw Refused("particle dynamic component exceeds float4", "material particle dynamic component");
                used.insert(int(c.number()));
            }
        }
    if (used.empty())
        return {};
    bool zero = std::any_of(local.begin(), local.end(), [](const Value& v) { return NumberIs(v, 0); });
    for (const auto& node : walk_formulas(local))
        if (Arithmetic(node.expression->op))
            for (const auto& arg : node.expression->args)
                zero = zero || NumberIs(arg, 0);
    const auto normalized = Normalize(arena, local);
    std::vector<int> components(used.begin(), used.end());
    std::vector<std::array<double, 4>> candidates;
    const unsigned count = zero ? 1u << components.size() : 1u;
    for (unsigned mask = 0; mask < count; ++mask)
    {
        std::array<double, 4> d{{1, 1, 1, 1}};
        for (std::size_t i = 0; i < components.size(); ++i)
            if (mask & (1u << i))
                d[std::size_t(components[i])] = 0;
        if (Equal(LocalFallback(arena, values, d), normalized))
            candidates.push_back(d);
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const std::array<double, 4>& a, const std::array<double, 4>& b)
              {
                  const auto countZero = [](const std::array<double, 4>& d)
                  {
                      return std::count(d.begin(), d.end(), 0.);
                  };
                  const auto x = countZero(a), y = countZero(b);
                  return x != y ? x < y : a < b;
              });
    return candidates;
}
}
struct MaterialParticleJointFormulas
{
    std::vector<Value> Color;
    Json Defaults = Json::MakeObject();
    std::vector<std::array<double, 4>> EquivalentCandidates;
    bool VertexColorRestored = false;
    std::vector<std::string> Unaccounted;
};
inline MaterialParticleJointFormulas ResolveOriginalParticleFormulas(SymbolicArena& arena,
                                                                     const std::vector<Value>& particle,
                                                                     const std::vector<Value>& local,
                                                                     const std::string& factory)
{
    if (particle.size() != 4 || local.size() != 4 || !MaterialParticleFactory(factory) ||
        size(particle) + size(local) > 1000000)
        throw Refused("joint particle source outputs are not bounded float4 formulas",
                      "material particle formula outputs");
    using namespace ParticleSourceDetail;
    const bool dynamic = Contains(particle, "ParticleDynamicParameter"),
               vertex = Contains(local, "VertexColor") && !Contains(particle, "VertexColor");
    std::vector<std::vector<Value>> attempted = {particle};
    if (vertex && IsMember(factory, {"FParticleSpriteVertexFactory", "FParticleBeamTrailVertexFactory",
                                     "FGPUSpriteVertexFactory"}))
    {
        std::vector<Value> restored;
        for (std::size_t c = 0; c < 4; ++c)
        {
            const auto& value = particle[c];
            const auto color = source(arena, "VertexColor", Value::null(), int(c));
            if (is(value, "MAX") && std::any_of(value.expression->args.begin(), value.expression->args.end(),
                                                [](const Value& v) { return NumberIs(v, 0); }))
            {
                const auto& a = value.expression->args;
                const auto inner = NumberIs(a[0], 0) ? a[1] : a[0];
                restored.push_back(arena.op("MAX", {simplify(arena, "MUL", {inner, color}), 0.}));
            }
            else if (is(value, "SAT"))
                restored.push_back(arena.op("SAT", {simplify(arena, "MUL", {value.expression->args[0], color})}));
            else if (NumberIs(value, 0))
                restored.push_back(value);
            else
                throw Refused("joint vertex color cannot be placed before the native output clamp",
                              "particle joint graph");
        }
        attempted.push_back(restored);
    }
    for (std::size_t index = 0; index < attempted.size(); ++index)
    {
        const auto& values = attempted[index];
        auto candidates = dynamic ? Candidates(arena, values, local)
                          : Equal(LocalFallback(arena, values, {{1, 1, 1, 1}}), Normalize(arena, local))
                              ? std::vector<std::array<double, 4>>{{{1, 1, 1, 1}}}
                              : std::vector<std::array<double, 4>>{};
        if (candidates.empty())
            continue;
        if (index)
        {
            const auto projected = rewrite(arena, values,
                                           [&](const Value& v)
                                           {
                                               if (is(v, "COMP") && is(v.expression->args[0], "SRC") &&
                                                   v.expression->args[0].expression->args[0].kind == Value::Text &&
                                                   v.expression->args[0].expression->args[0].text == "VertexColor")
                                                   return Value(1.);
                                               return Simplify(arena, v);
                                           });
            if (!Equal(projected, Normalize(arena, particle)))
                throw Refused("joint graph does not equal its original particle shader", "particle joint graph");
        }
        MaterialParticleJointFormulas result;
        result.Color = values;
        result.VertexColorRestored = index != 0;
        if (dynamic)
        {
            auto d = Json::MakeArray();
            for (const auto value : candidates[0])
                d.array.push_back(value);
            result.Defaults["0"] = d;
            result.EquivalentCandidates = candidates;
            result.Unaccounted.push_back(
                "authored dynamic default is not uniquely recoverable; compiled-literal default is symbolically proved equivalent");
        }
        return result;
    }
    throw Refused("no compiled-literal default and joint graph equal both native factory formulas",
                  "particle joint graph");
}
}
} // P4MapKit::Material

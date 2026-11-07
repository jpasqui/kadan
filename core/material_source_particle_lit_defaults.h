// Literal defaults must match every independently captured native material pin.
// Authored defaults remain ambiguous; this comparison does not prove a renderer.
#pragma once
#include "material_source_particle_defaults.h"
namespace P4MapKit
{
namespace Material
{
namespace ParticleLitSourceDetail
{
class Canonical
{
    using Form = ParticleSourceDetail::Form;
    std::map<std::string, std::size_t> intern;
    std::vector<Form> forms;
    std::map<const Expression*, std::size_t> memo;
    std::set<std::size_t> finite;
    std::size_t make(const std::string& tag, double number, std::vector<std::size_t> children)
    {
        if (number == 0)
            number = 0;
        std::uint64_t bits;
        std::memcpy(&bits, &number, 8);
        std::string key = std::to_string(tag.size()) + ":" + tag + ":" + std::to_string(bits);
        for (const auto child : children)
            key += ":" + std::to_string(child);
        const auto found = intern.find(key);
        if (found != intern.end())
            return found->second;
        if (forms.size() >= 1000000)
            throw Refused("lit particle canonical expression budget exceeded", "material particle formula budget");
        const auto id = forms.size();
        forms.push_back({tag, number, std::move(children)});
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
        return make("product", coefficient, std::move(factors));
    }
    std::size_t atom(const Value& value)
    {
        return ParticleSourceDetail::Number(value) ? make("number", value.number(), {})
                                                   : make("literal:" + value_key(value), 0, {});
    }

public:
    std::size_t value(const Value& root)
    {
        if (!symbolic(root))
            return atom(root);
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
                const auto child = x.args[frame.Next++];
                if (symbolic(child) && !memo.count(child.expression.get()))
                    stack.push_back({child, 0});
                continue;
            }
            std::vector<std::size_t> args;
            for (const auto& a : x.args)
                args.push_back(symbolic(a) ? memo.at(a.expression.get()) : atom(a));
            std::size_t result;
            if (x.op == "MUL" || x.op == "NEG")
            {
                double coefficient = x.op == "NEG" ? -1. : 1.;
                std::vector<std::size_t> factors;
                for (const auto id : args)
                {
                    const auto& form = forms[id];
                    if (form.Tag == "number")
                        coefficient *= form.Number;
                    else if (form.Tag == "product")
                    {
                        coefficient *= form.Number;
                        factors.insert(factors.end(), form.Children.begin(), form.Children.end());
                    }
                    else
                        factors.push_back(id);
                }
                result = multiply(coefficient, std::move(factors));
            }
            else if (x.op == "ADD")
            {
                double constant = 0;
                std::vector<std::size_t> terms;
                for (const auto id : args)
                {
                    const auto& form = forms[id];
                    if (form.Tag == "sum")
                    {
                        constant += form.Number;
                        terms.insert(terms.end(), form.Children.begin(), form.Children.end());
                    }
                    else if (form.Tag == "number")
                        constant += form.Number;
                    else
                        terms.push_back(id);
                }
                std::map<std::size_t, double> groups;
                std::vector<std::size_t> other;
                for (const auto id : terms)
                {
                    double coefficient = 1;
                    auto factor = id;
                    const auto& form = forms[id];
                    if (form.Tag == "product" && form.Children.size() == 1)
                    {
                        coefficient = form.Number;
                        factor = form.Children[0];
                    }
                    if (finite.count(factor))
                        groups[factor] += coefficient;
                    else
                        other.push_back(id);
                }
                for (const auto& group : groups)
                    if (group.second != 0)
                        other.push_back(multiply(group.second, {group.first}));
                if (other.empty())
                    result = make("number", constant, {});
                else if (constant == 0 && other.size() == 1)
                    result = other[0];
                else
                {
                    std::sort(other.begin(), other.end());
                    result = make("sum", constant, std::move(other));
                }
            }
            else
            {
                if (x.op == "MAX" || x.op == "MIN")
                    std::sort(args.begin(), args.end());
                result = make("operation:" + x.op, 0, std::move(args));
            }
            if (ParticleSourceDetail::FiniteLeaf(frame.Node))
                finite.insert(result);
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
    for (std::size_t i = 0; i < a.size(); ++i)
        if (canonical.value(a[i]) != canonical.value(b[i]))
            return false;
    return true;
}
}
using MaterialParticlePinFormulas = std::map<std::string, std::vector<Value>>;
struct MaterialParticleLitDefaults
{
    Json Defaults = Json::MakeObject();
    std::vector<std::string> Unaccounted;
    std::map<const Expression*, Value> Replacements;
};
inline MaterialParticleLitDefaults
ResolveOriginalParticleLitLiteralDefaults(SymbolicArena& arena, const MaterialParticlePinFormulas& particle,
                                          const MaterialParticlePinFormulas& local, bool* matched = nullptr)
{
    std::vector<Value> values, preview;
    for (const auto& pin : particle)
    {
        const auto found = local.find(pin.first);
        if (found == local.end())
            continue;
        if (pin.second.empty() || pin.second.size() > 4 || pin.second.size() != found->second.size())
            throw Refused("lit dynamic default shared pin width disagrees", "particle dynamic defaults");
        values.insert(values.end(), pin.second.begin(), pin.second.end());
        preview.insert(preview.end(), found->second.begin(), found->second.end());
    }
    if (values.empty())
    {
        if (matched)
        {
            *matched = false;
            return {};
        }
        throw Refused("lit dynamic default has no independently shared pins", "particle dynamic defaults");
    }
    if (size(values) + size(preview) > 1000000)
        throw Refused("lit particle formula budget exceeded", "material particle formula budget");
    std::set<int> used;
    for (const auto& value : walk_formulas(values))
        if (is(value, "COMP") && Args(value).size() == 2 && is(Args(value)[0], "SRC"))
        {
            const auto& a = Args(Args(value)[0]);
            if (a.size() == 2 && a[0].kind == Value::Text && a[0].text == "ParticleDynamicParameter" &&
                NumberIs(a[1], 0))
            {
                if (!ParticleSourceDetail::Number(Args(value)[1]) || Args(value)[1].number() < 0 ||
                    Args(value)[1].number() > 3 || std::floor(Args(value)[1].number()) != Args(value)[1].number())
                    throw Refused("lit dynamic component exceeds float4", "material particle dynamic component");
                used.insert(int(Args(value)[1].number()));
            }
        }
    bool zero = std::any_of(preview.begin(), preview.end(), [](const Value& value) { return NumberIs(value, 0); });
    for (const auto& value : walk_formulas(preview))
        if (ParticleSourceDetail::Arithmetic(value.expression->op))
            for (const auto& a : Args(value))
                zero = zero || NumberIs(a, 0);
    const auto normalized = ParticleSourceDetail::Normalize(arena, preview);
    const std::vector<int> channels(used.begin(), used.end());
    std::vector<std::array<double, 4>> candidates;
    for (unsigned mask = 0; mask < (zero ? 1u << channels.size() : 1u); ++mask)
    {
        std::array<double, 4> candidate{{1, 1, 1, 1}};
        for (std::size_t i = 0; i < channels.size(); ++i)
            if (mask & (1u << i))
                candidate[std::size_t(channels[i])] = 0;
        if (ParticleLitSourceDetail::Equal(ParticleSourceDetail::LocalFallback(arena, values, candidate), normalized))
            candidates.push_back(candidate);
    }
    if (candidates.empty())
    {
        if (matched)
        {
            *matched = false;
            return {};
        }
        throw Refused("no compiled-literal dynamic default equals every native local material pin",
                      "particle dynamic defaults");
    }
    if (matched)
        *matched = true;
    std::sort(candidates.begin(), candidates.end(),
              [](const std::array<double, 4>& a, const std::array<double, 4>& b)
              {
                  const auto x = std::count(a.begin(), a.end(), 0.), y = std::count(b.begin(), b.end(), 0.);
                  return x != y ? x < y : a < b;
              });
    MaterialParticleLitDefaults result;
    auto chosen = Json::MakeArray();
    for (const auto value : candidates.front())
        chosen.array.push_back(value);
    result.Defaults["0"] = chosen;
    result.Unaccounted.push_back(
        "authored dynamic default is not uniquely recoverable; compiled-literal default is symbolically proved on all native material pins");
    return result;
}
inline MaterialParticleLitDefaults ResolveOriginalParticleLitDefaults(SymbolicArena& arena,
                                                                      const MaterialParticlePinFormulas& particle,
                                                                      const MaterialParticlePinFormulas& local,
                                                                      const std::string& factory)
{
    bool matched = false;
    const auto literal = ResolveOriginalParticleLitLiteralDefaults(arena, particle, local, &matched);
    if (matched)
        return literal;
    if (!IsMember(factory,
                  {"FParticleSpriteVertexFactory", "FGPUSpriteVertexFactory", "FParticleBeamTrailVertexFactory"}))
        return ResolveOriginalParticleLitLiteralDefaults(arena, particle, local);
    auto joint = particle;
    std::map<const Expression*, Value> replacements;
    for (const auto& pin : {std::make_pair(std::string("BaseColor"), std::vector<int>{0, 1, 2}),
                            std::make_pair(std::string("Opacity"), std::vector<int>{3})})
    {
        const auto p = particle.find(pin.first), l = local.find(pin.first);
        if (p == particle.end() || l == local.end())
            continue;
        if (ParticleSourceDetail::Contains(p->second, "VertexColor") ||
            !ParticleSourceDetail::Contains(l->second, "VertexColor"))
            continue;
        if (p->second.size() != pin.second.size())
            throw Refused("joint lit color pin has unexpected width", "particle joint graph");
        std::vector<Value> restored;
        for (std::size_t i = 0; i < p->second.size(); ++i)
        {
            const auto& value = p->second[i];
            if (!is(value, "SAT") || Args(value).size() != 1)
                throw Refused("joint lit color lacks its independent native clamp", "particle joint graph");
            restored.push_back(arena.op(
                "SAT", {simplify(arena, "MUL",
                                 {Args(value)[0], source(arena, "VertexColor", Value::null(), pin.second[i])})}));
        }
        const auto projection = rewrite(
            arena, restored,
            [&](const Value& value)
            {
                if (is(value, "COMP") && Args(value).size() == 2 && is(Args(value)[0], "SRC") &&
                    Args(Args(value)[0])[0].kind == Value::Text && Args(Args(value)[0])[0].text == "VertexColor")
                    return Value(1.);
                return ParticleSourceDetail::Simplify(arena, value);
            });
        if (!ParticleLitSourceDetail::Equal(projection, ParticleSourceDetail::Normalize(arena, p->second)))
            throw Refused("joint lit pin does not equal its original particle projection", "particle joint graph");
        joint[pin.first] = restored;
        for (std::size_t i = 0; i < p->second.size(); ++i)
        {
            const auto* key = p->second[i].expression.get();
            const auto prior = replacements.find(key);
            if (prior != replacements.end() && !same(prior->second, restored[i]))
                throw Refused("joint color needs different channel replacements for one shared native root",
                              "particle joint graph");
            replacements[key] = restored[i];
        }
    }
    if (replacements.empty())
        throw Refused("no exact joint lit vertex color placement", "particle joint graph");
    auto result = ResolveOriginalParticleLitLiteralDefaults(arena, joint, local);
    result.Replacements = std::move(replacements);
    result.Unaccounted.push_back(
        "sprite-erased vertex color is restored by exact local and particle material-pin projections");
    return result;
}
}
} // P4MapKit::Material

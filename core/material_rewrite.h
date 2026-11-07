// Production formula rewrites from material_decompile.py. These recognizers
// name only structurally established normal unpacking and tangent/facing math.
#pragma once
#include "material_bind.h"
#include "material_lifter.h"
#include "material_facing.h"
#include <functional>
#include <unordered_map>

namespace P4MapKit
{
namespace Material
{
using RewriteMemo = std::unordered_map<const Expression*, Value>;
using RewriteFunction = std::function<Value(const Value&)>;
using ExpressionSet = std::set<std::shared_ptr<const Expression>>;

inline Value rewrite(SymbolicArena& arena, const Value& value, const RewriteFunction& fn, RewriteMemo& memo)
{
    if (!symbolic(value))
        return value;
    struct Pending
    {
        Value node;
        std::size_t next = 0;
    };
    std::vector<Pending> stack = {{value, 0}};
    while (!stack.empty())
    {
        auto& frame = stack.back();
        const auto* node = frame.node.expression.get();
        if (memo.count(node))
        {
            stack.pop_back();
            continue;
        }
        if (frame.next < node->args.size())
        {
            auto child = node->args[frame.next++];
            if (symbolic(child) && !memo.count(child.expression.get()))
                stack.push_back({child, 0});
            continue;
        }
        std::vector<Value> args;
        bool changed = false;
        for (const auto& arg : node->args)
        {
            auto replacement = symbolic(arg) ? memo.at(arg.expression.get()) : arg;
            changed = changed || !same(arg, replacement);
            args.push_back(replacement);
        }
        const auto original = frame.node;
        const auto candidate = changed ? arena.mk(node->op, args) : original;
        memo.emplace(node, fn(candidate));
        stack.pop_back();
    }
    return memo.at(value.expression.get());
}
inline std::vector<Value> rewrite(SymbolicArena& arena, const std::vector<Value>& values, const RewriteFunction& fn)
{
    RewriteMemo memo;
    std::vector<Value> result;
    for (const auto& v : values)
        result.push_back(rewrite(arena, v, fn, memo));
    return result;
}
inline std::vector<Value> walk_formulas(const std::vector<Value>& values)
{
    std::vector<Value> result, stack;
    std::set<const Expression*> seen;
    for (const auto& v : values)
        if (symbolic(v))
            stack.push_back(v);
    while (!stack.empty())
    {
        auto v = stack.back();
        stack.pop_back();
        if (!seen.insert(v.expression.get()).second)
            continue;
        result.push_back(v);
        for (const auto& arg : v.expression->args)
            if (symbolic(arg))
                stack.push_back(arg);
    }
    return result;
}
inline ExpressionSet normal_samples(Lifter& lifter, const std::vector<Value>& values)
{
    const auto nodes = walk_formulas(values);
    std::map<const Expression*, std::shared_ptr<const Expression>> forms;
    for (const auto& x : nodes)
    {
        const auto hit = lifter.unpacked(x);
        if (hit.valid)
        {
            forms[x.expression.get()] = hit.source.expression;
            if (hit.component < 2)
                forms[x.expression->args.at(0).expression.get()] = hit.source.expression;
        }
    }
    ExpressionSet raw, used;
    for (const auto& x : nodes)
        for (const auto& a : x.expression->args)
            if (is(a, "COMP") && is(a.expression->args.at(0), "SAMPLE"))
            {
                const auto read = a.expression->args[0].expression;
                auto match = forms.find(x.expression.get());
                if (match != forms.end() && match->second == read &&
                    (NumberIs(a.expression->args.at(1), 0) || NumberIs(a.expression->args.at(1), 1)))
                    used.insert(read);
                else
                    raw.insert(read);
            }
    for (const auto& v : values)
        if (is(v, "COMP") && is(v.expression->args.at(0), "SAMPLE"))
            raw.insert(v.expression->args[0].expression);
    for (const auto& node : raw)
        used.erase(node);
    return used;
}
inline std::set<std::size_t> normal_sample_ids(Lifter& lifter, const std::vector<Value>& values)
{
    std::set<std::size_t> result;
    for (const auto& sample : normal_samples(lifter, values))
        result.insert(sample->id);
    return result;
}

namespace RewriteDetail
{
using Frame = std::map<const Expression*, std::pair<int, int>>;
inline Frame frame(SymbolicArena& arena)
{
    Frame result;
    Value tx[3], tz[3];
    auto sign = source(arena, "TangentSign");
    for (int c = 0; c < 3; ++c)
    {
        tx[c] = source(arena, "TangentX", Value::null(), c);
        tz[c] = source(arena, "TangentZ", Value::null(), c);
        result[tx[c].expression.get()] = {0, c};
        result[tz[c].expression.get()] = {2, c};
    }
    for (int c = 0; c < 3; ++c)
    {
        int a = (c + 1) % 3, b = (c + 2) % 3;
        auto cross = simplify(
            arena, "ADD",
            {simplify(arena, "MUL", {tz[a], tx[b]}), simplify(arena, "NEG", {simplify(arena, "MUL", {tz[b], tx[a]})})});
        result[simplify(arena, "MUL", {sign, cross}).expression.get()] = {1, c};
        result[simplify(arena, "MUL", {cross, sign}).expression.get()] = {1, c};
    }
    return result;
}
struct Term
{
    int row, component;
    Value factor;
};
inline bool frame_terms(const Value& x, const Frame& frame, std::vector<Term>& out)
{
    std::vector<Value> terms, stack = {x};
    while (!stack.empty())
    {
        auto t = stack.back();
        stack.pop_back();
        if (is(t, "ADD"))
            for (const auto& a : t.expression->args)
                stack.push_back(a);
        else
            terms.push_back(t);
    }
    for (const auto& t : terms)
    {
        auto direct = symbolic(t) ? frame.find(t.expression.get()) : frame.end();
        if (direct != frame.end())
        {
            out.push_back({direct->second.first, direct->second.second, 1.0});
            continue;
        }
        bool product = false;
        if (is(t, "MUL") && t.expression->args.size() == 2)
            for (int side = 0; side < 2; ++side)
            {
                const auto& a = t.expression->args[side];
                auto hit = symbolic(a) ? frame.find(a.expression.get()) : frame.end();
                if (hit != frame.end())
                {
                    out.push_back({hit->second.first, hit->second.second, t.expression->args[1 - side]});
                    product = true;
                    break;
                }
            }
        if (product)
            continue;
        if (is(t, "COMP") && t.expression->args.size() == 2 && is(t.expression->args[0], "TOWORLD") &&
            IsNumber(t.expression->args[1]))
        {
            const auto& args = t.expression->args[0].expression->args;
            int c = int(t.expression->args[1].number());
            if (c < 0 || c > 2 || args.size() != 3)
                return false;
            for (int row = 0; row < 3; ++row)
                if (!NumberIs(args[row], 0))
                    out.push_back({row, c, args[row]});
        }
        else
            return false;
    }
    return true;
}

}
inline std::vector<Value> frame_transforms(SymbolicArena& arena, const std::vector<Value>& values)
{
    const auto frame = RewriteDetail::frame(arena);
    std::map<std::string, std::set<int>> rows_seen;
    auto finder = [&](bool replace) -> RewriteFunction
    {
        return [&, replace](const Value& x) -> Value
        {
            if (!is(x, "ADD"))
                return x;
            std::vector<RewriteDetail::Term> terms;
            if (!RewriteDetail::frame_terms(x, frame, terms) || terms.size() < 2)
                return x;
            std::set<int> rows, components;
            for (const auto& t : terms)
            {
                rows.insert(t.row);
                components.insert(t.component);
            }
            if (components.size() == 1 && rows.size() == terms.size())
            {
                std::vector<Value> n = {0.0, 0.0, 0.0};
                for (const auto& t : terms)
                    n.at(t.row) = t.factor;
                return arena.mk("COMP", {arena.mk("TOWORLD", n), Value(terms[0].component)});
            }
            if (rows.size() == 1 && terms.size() == 3 && components == std::set<int>({0, 1, 2}))
            {
                std::vector<Value> v(3, Value::null());
                for (const auto& t : terms)
                    v.at(t.component) = t.factor;
                const auto key = value_key(Value::tuple_value(v));
                auto& seen = rows_seen[key];
                seen.insert(terms[0].row);
                if (replace && (seen.count(0) || seen.count(1)))
                    return arena.mk("COMP", {arena.mk("TOTANGENT", v), Value(terms[0].row)});
            }
            return x;
        };
    };
    rewrite(arena, values, finder(false));
    return rewrite(arena, values, finder(true));
}
inline std::vector<Value> facing_signs(SymbolicArena& arena, const std::vector<Value>& values)
{
    return FacingSigns(arena, values);
}
}
}

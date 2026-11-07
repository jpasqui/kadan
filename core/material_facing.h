// Static TwoSidedSign recognition. No engine/game inputs are synthesized.
#pragma once
#include "material_symbolic.h"
#include <unordered_map>
namespace P4MapKit
{
namespace Material
{
namespace FacingDetail
{
inline bool is(const Value& v, const char* op)
{
    return symbolic(v) && v.expression && v.expression->op == op;
}
inline Value facing_value(const Value& root, double facing)
{
    if (!symbolic(root))
        return root;
    struct Pending
    {
        Value node;
        std::size_t next = 0;
    };
    std::vector<Pending> stack = {{root, 0}};
    std::unordered_map<const Expression*, Value> memo;
    auto operand = [&](const Value& v) -> Value
    {
        return symbolic(v) ? memo.at(v.expression.get()) : v;
    };
    while (!stack.empty())
    {
        auto& pending = stack.back();
        const auto& v = pending.node;
        const auto* node = v.expression.get();
        if (memo.count(node))
        {
            stack.pop_back();
            continue;
        }
        if (FacingDetail::is(v, "COMP") && node->args.size() == 2 && FacingDetail::is(node->args[0], "SRC"))
        {
            const auto& src = node->args[0].expression->args;
            if (!src.empty() && src[0].kind == Value::Text &&
                (src[0].text == "Facing" || src[0].text == "TwoSidedSign"))
            {
                memo.emplace(node, facing);
                stack.pop_back();
                continue;
            }
        }
        if (node->op == "SEL")
        {
            if (node->args.size() != 3)
                throw EvalError("malformed facing choice");
            const auto& test = node->args[0];
            if (symbolic(test) && !memo.count(test.expression.get()))
            {
                stack.push_back({test, 0});
                continue;
            }
            const auto& chosen = node->args[operand(test).truth() ? 1 : 2];
            if (symbolic(chosen) && !memo.count(chosen.expression.get()))
            {
                stack.push_back({chosen, 0});
                continue;
            }
            memo.emplace(node, operand(chosen));
            stack.pop_back();
            continue;
        }
        if (pending.next < node->args.size())
        {
            auto child = node->args[pending.next++];
            if (symbolic(child) && !memo.count(child.expression.get()))
                stack.push_back({child, 0});
            continue;
        }
        if (node->op.compare(0, 2, "P.") == 0)
            throw EvalError("preshader operation is outside the facing recognizer");
        std::vector<Value> args;
        for (const auto& a : node->args)
            args.push_back(operand(a));
        memo.emplace(node, numeric_operation(node->op, args));
        stack.pop_back();
    }
    return memo.at(root.expression.get());
}
}
inline bool FacingSign(const Value& value)
{
    if (!symbolic(value))
        return false;
    try
    {
        const auto front = FacingDetail::facing_value(value, 1.0), back = FacingDetail::facing_value(value, -1.0);
        auto number = [](const Value& v)
        {
            return v.kind == Value::Float || v.kind == Value::Int || v.kind == Value::Boolean;
        };
        return number(front) && number(back) && front.number() == 1.0 && back.number() == -1.0;
    }
    catch (const EvalError&)
    {
        return false;
    }
    catch (const std::out_of_range&)
    {
        return false;
    }
    catch (const std::domain_error&)
    {
        return false;
    }
}
inline std::vector<Value> FacingSigns(SymbolicArena& arena, const std::vector<Value>& values)
{
    std::unordered_map<const Expression*, Value> memo;
    struct Pending
    {
        Value node;
        std::size_t next = 0;
    };
    std::vector<Value> result;
    for (const auto& root : values)
    {
        if (!symbolic(root))
        {
            result.push_back(root);
            continue;
        }
        std::vector<Pending> stack = {{root, 0}};
        while (!stack.empty())
        {
            auto& pending = stack.back();
            const auto* node = pending.node.expression.get();
            if (memo.count(node))
            {
                stack.pop_back();
                continue;
            }
            if (pending.next < node->args.size())
            {
                auto child = node->args[pending.next++];
                if (symbolic(child) && !memo.count(child.expression.get()))
                    stack.push_back({child, 0});
                continue;
            }
            bool changed = false;
            std::vector<Value> args;
            for (const auto& a : node->args)
            {
                auto replacement = symbolic(a) ? memo.at(a.expression.get()) : a;
                changed = changed || !same(a, replacement);
                args.push_back(replacement);
            }
            auto value = changed ? arena.mk(node->op, args) : pending.node;
            const auto& op = node->op;
            if ((op == "SEL" || op == "I2F.S" || op == "I2F.U" || op == "MUL" || op == "NEG") && FacingSign(value))
                value = arena.mk("COMP", {arena.mk("SRC", {Value("TwoSidedSign"), Value::null()}), Value(0)});
            memo.emplace(node, value);
            stack.pop_back();
        }
        result.push_back(memo.at(root.expression.get()));
    }
    return result;
}
}
}

// Additive plan seam: lift exact native infinite equality choices through the
// existing bound-value callback. Unsupported constants still reach the strict
// ordinary lifter; this does not qualify a renderer or an original family.
#pragma once
#include "material_decompile.h"
#include "material_rewrite.h"
#include "material_infinite_equality.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialInfiniteEqualityFacts
{
    std::size_t Choices = 0;
    bool RendererProven = false, ProductionAdmitted = false;
};
inline std::size_t PrimeOriginalInfiniteEqualityChoices(Lifter& lifter, const std::vector<Value>& values)
{
    std::set<const Expression*> done, active;
    std::map<const Expression*, Value> conjunctions;
    std::vector<std::pair<Value, bool>> pending;
    for (const auto& value : values)
        pending.push_back({value, false});
    std::size_t count = 0, budget = 0;
    while (!pending.empty())
    {
        const auto item = pending.back();
        pending.pop_back();
        const auto& value = item.first;
        if (value.kind == Value::Tuple)
        {
            for (const auto& child : value.tuple)
                pending.push_back({child, false});
            continue;
        }
        if (!symbolic(value) || done.count(value.expression.get()))
            continue;
        const auto* identity = value.expression.get();
        const auto& expression = *identity;
        if (!item.second)
        {
            if (++budget > 1000000 || !active.insert(identity).second)
                throw Refused("infinite equality walk exceeds its bounded native DAG",
                              "material infinite equality budget");
            pending.push_back({value, true});
            // The seven original UI programs AND two TRUNC.U(F.SEQ(...,+Inf))
            // predicates. Match that native Boolean-mask shape before the ordinary
            // lifter creates its equivalent nested SEL choices on demand.
            if (expression.op == "SEL" && expression.args.size() == 3 && IsExpr(expression.args[0], "T.NE") &&
                Args(expression.args[0]).size() == 1)
            {
                const auto& inner = Args(expression.args[0])[0];
                if (IsExpr(inner, "I.AND") && Args(inner).size() == 2 && lifter.booleanMask(inner))
                {
                    bool exact = true;
                    for (const auto& child : Args(inner))
                    {
                        std::vector<Value> pair;
                        bool matched = false;
                        for (const auto& kind : std::vector<std::string>{"F.SEQ", "F.SNE"})
                            if (lifter.comparison(lifter.arena.op("T.NE", {child}), kind, pair) && pair.size() == 2 &&
                                (MaterialInfiniteLiteral(pair[0]) != MaterialInfiniteLiteral(pair[1])))
                                matched = true;
                        exact &= matched;
                    }
                    if (exact)
                    {
                        const auto a = lifter.arena.op("T.NE", {Args(inner)[0]}),
                                   b = lifter.arena.op("T.NE", {Args(inner)[1]});
                        const auto nested = lifter.arena.select(b, expression.args[1], expression.args[2]);
                        const auto equivalent = lifter.arena.select(a, nested, expression.args[2]);
                        conjunctions.emplace(identity, equivalent);
                        pending.push_back({equivalent, false});
                        continue;
                    }
                }
            }
            for (auto at = expression.args.rbegin(); at != expression.args.rend(); ++at)
                pending.push_back({*at, false});
            continue;
        }
        active.erase(identity);
        done.insert(identity);
        const auto conjunction = conjunctions.find(identity);
        if (conjunction != conjunctions.end())
        {
            const auto output = lifter.lift({conjunction->second});
            const auto key = ValueKey(value) + ";";
            const auto existing = lifter.memo.find(key);
            if (existing != lifter.memo.end() && existing->second.Dump() != output.Dump())
                throw Refused("infinite conjunction conflicts with an existing lifted native choice",
                              "material infinite equality memo");
            lifter.memo.emplace(key, output);
            continue;
        }
        if (expression.op != "SEL" || expression.args.size() != 3)
            continue;
        auto test = expression.args[0];
        bool inverted = false;
        if (IsExpr(test, "T.EQ") && Args(test).size() == 1 && lifter.booleanMask(Args(test)[0]))
        {
            test = lifter.arena.op("T.NE", {Args(test)[0]});
            inverted = true;
        }
        for (const auto& kind : std::vector<std::string>{"F.SEQ", "F.SNE"})
        {
            std::vector<Value> pair;
            Json output;
            if (!lifter.comparison(test, kind, pair))
                continue;
            if (!LiftOriginalInfiniteEquality(lifter, kind, pair, {expression.args[inverted ? 2 : 1]},
                                              {expression.args[inverted ? 1 : 2]}, output))
                continue;
            const auto key = ValueKey(value) + ";";
            const auto existing = lifter.memo.find(key);
            if (existing != lifter.memo.end() && existing->second.Dump() != output.Dump())
                throw Refused("infinite equality conflicts with an existing lifted native choice",
                              "material infinite equality memo");
            lifter.memo.emplace(key, std::move(output));
            ++count;
            break;
        }
    }
    return count;
}
inline std::shared_ptr<MaterialInfiniteEqualityFacts> InstallOriginalInfiniteEqualityPlan(MaterialRecoveryPlan& plan)
{
    const auto prior = plan.NormalSamples;
    auto facts = std::make_shared<MaterialInfiniteEqualityFacts>();
    plan.NormalSamples = [prior, facts](Lifter& lifter, const std::vector<Value>& values)
    {
        facts->Choices += PrimeOriginalInfiniteEqualityChoices(lifter, values);
        return prior ? prior(lifter, values) : normal_sample_ids(lifter, values);
    };
    return facts;
}
}
}

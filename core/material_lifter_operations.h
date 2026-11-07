// Lifter: operations, integer bounds, comparisons and choices (part of material_lifter.h).
#pragma once
#include "material_lifter.h"
namespace P4MapKit
{
namespace Material
{
inline bool Lifter::whole(const std::vector<Value>& values)
{
    if (values.empty())
        return false;
    auto first = part(values[0]);
    bool source = first.valid;
    for (const auto& v : values)
    {
        auto p = part(v);
        source &= p.valid && Same(p.source, first.source) && p.normal == first.normal;
    }
    if (source)
        return true;
    if (values[0].kind != Value::Expr)
        return false;
    auto name = values[0].expression->op;
    if (name == "COMP" || name == "SRC" || name == "SAMPLE" || name == "SEL")
        return false;
    for (const auto& v : values)
        if (v.kind != Value::Expr || v.expression->op != name)
            return false;
    return true;
}
inline Json Lifter::dot(const std::vector<std::vector<Value>>& columns)
{
    if (columns.size() != 2 || columns[0].size() != 1)
        return Json();
    std::vector<Value> terms{columns[1][0]};
    auto rest = columns[0][0];
    while (IsExpr(rest, "ADD"))
    {
        terms.insert(terms.begin(), Args(rest)[1]);
        rest = Args(rest)[0];
    }
    terms.insert(terms.begin(), rest);
    if (terms.size() < 2 || terms.size() > 3)
        return Json();
    std::vector<Value> a, b;
    for (const auto& t : terms)
    {
        if (!IsExpr(t, "MUL"))
            return Json();
        a.push_back(Args(t)[0]);
        b.push_back(Args(t)[1]);
    }
    if (!whole(a) || !whole(b) || Same(a[0], a[1]) || Same(b[0], b[1]))
        return Json();
    return link(node("DotProduct", Object({{"A", lift(a)}, {"B", lift(b)}})));
}
inline Json Lifter::lerp(const std::vector<std::vector<Value>>& columns)
{
    if (columns.size() != 2)
        return Json();
    for (const auto& v : columns[0])
        if (!IsExpr(v, "MUL"))
            return Json();
    const auto& a = columns[1];
    bool weighted = true;
    for (const auto& v : a)
        weighted &= IsExpr(v, "MUL") && IsExpr(Args(v)[0], "SUB") && NumberIs(Args(Args(v)[0])[0], 1);
    if (weighted)
    {
        std::vector<Value> alpha, left, right;
        bool same = true;
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            alpha.push_back(Args(Args(a[i])[0])[1]);
            left.push_back(Args(a[i])[1]);
            right.push_back(Args(columns[0][i])[1]);
            same &= Same(Args(columns[0][i])[0], alpha.back());
        }
        if (same)
            return link(
                node("LinearInterpolate", Object({{"A", lift(left)}, {"B", lift(right)}, {"Alpha", lift(alpha)}})));
    }
    for (int at = 0; at < 2; ++at)
    {
        std::vector<Value> alpha, b;
        bool matched = true;
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            alpha.push_back(Args(columns[0][i])[at]);
            const auto& span = Args(columns[0][i])[1 - at];
            if (!IsExpr(span, "ADD") || !IsExpr(Args(span)[1], "NEG") || !Same(Args(Args(span)[1])[0], a[i]))
            {
                matched = false;
                break;
            }
            b.push_back(Args(span)[0]);
        }
        if (matched)
            return link(node("LinearInterpolate", Object({{"A", lift(a)}, {"B", lift(b)}, {"Alpha", lift(alpha)}})));
    }
    return Json();
}
inline Json Lifter::operation(const std::string& name, const std::vector<std::vector<Value>>& columns)
{
    auto get = [&](std::size_t i) -> Json
    {
        if (i >= columns.size())
            throw Refused("operation " + name + " has too few operands", "operation arity");
        return lift(columns[i]);
    };
    const std::map<std::string, std::string> plain = {{"ADD", "Add"},    {"MUL", "Multiply"}, {"SUB", "Subtract"},
                                                      {"DIV", "Divide"}, {"MIN", "Min"},      {"MAX", "Max"}};
    const std::map<std::string, std::string> one = {
        {"SAT", "Saturate"}, {"ABS", "Abs"}, {"U.FRC", "Frac"}, {"U.FLR", "Floor"}, {"U.CEIL", "Ceil"}};
    if ((name == "I2F.S" || name == "I2F.U") && columns.size() == 1)
    {
        std::vector<Value> values;
        bool valid = true;
        for (const auto& v : columns[0])
        {
            auto bounds = integerBounds(v);
            Value lowered;
            valid &= bounds.valid && (name != "I2F.U" || bounds.lo >= 0);
            if (!valid)
                break;
            const auto cast = arena.mk(name, {v}), facing = FacingSigns(arena, {cast})[0];
            if (!Same(cast, facing))
                lowered = facing;
            else
                valid &= floatInteger(v, lowered);
            if (!valid)
                break;
            values.push_back(lowered);
        }
        if (valid)
            return lift(values);
    }
    if (name == "ADD" && columns.size() == 2)
    {
        bool negRight = true, negLeft = true, ones = true;
        for (const auto& v : columns[1])
        {
            negRight &= IsExpr(v, "NEG");
            ones &= NumberIs(v, 1);
        }
        for (const auto& v : columns[0])
            negLeft &= IsExpr(v, "NEG");
        // The compiled Sign node uses unsigned boolean conversions of the
        // strict comparisons. Matching both operands preserves its exact shape.
        auto flag = [](const Value& v, const char* comparison) -> Value
        {
            if (!IsExpr(v, "I2F.U") || !IsExpr(Args(v)[0], "TRUNC.U") || !IsExpr(Args(Args(v)[0])[0], comparison))
                return Value::null();
            const auto& a = Args(Args(Args(v)[0])[0]);
            return a.size() == 2 && NumberIs(a[1], 0) ? a[0] : Value::null();
        };
        std::vector<Value> signInput;
        bool sign = columns[0].size() == columns[1].size();
        for (std::size_t i = 0; sign && i < columns[0].size(); ++i)
        {
            auto above = flag(columns[0][i], "F.SGT"),
                 below = IsExpr(columns[1][i], "NEG") ? flag(Args(columns[1][i])[0], "F.SLT") : Value::null();
            sign = above.kind == Value::Expr && Same(above, below);
            signInput.push_back(above);
        }
        if (sign)
            return link(node("Sign", Object({{"Input", lift(signInput)}})));
        auto d = dot(columns);
        if (d.kind != Json::Null)
            return d;
        auto inner = [&](std::size_t i)
        {
            std::vector<Value> a;
            for (const auto& v : columns[i])
                a.push_back(Args(v)[0]);
            return lift(a);
        };
        if (negRight)
            return link(node("Subtract", Object({{"A", get(0)}, {"B", inner(1)}})));
        if (negLeft && ones)
            return link(node("OneMinus", Object({{"Input", inner(0)}})));
        if (negLeft)
            return link(node("Subtract", Object({{"A", get(1)}, {"B", inner(0)}})));
        auto l = lerp(columns);
        if (l.kind == Json::Null)
            l = lerp({columns[1], columns[0]});
        if (l.kind != Json::Null)
            return l;
    }
    auto p = plain.find(name);
    if (p != plain.end())
        return link(node(p->second, Object({{"A", get(0)}, {"B", get(1)}})));
    auto u = one.find(name);
    if (u != one.end())
        return link(node(u->second, Object({{"Input", get(0)}})));
    if (name == "NEG")
        return link(node("Multiply", Object({{"A", get(0)}, {"B", lift({Value(-1.0)})}})));
    if (name == "SIN" || name == "COS")
        return link(node(name == "SIN" ? "Sine" : "Cosine", Object({{"Input", get(0)}}), Object({{"Period", 0.0}})));
    if (name == "RCP")
    {
        bool root = true;
        for (const auto& v : columns.at(0))
            root &= IsExpr(v, "RSQ");
        if (root)
        {
            std::vector<Value> a;
            for (const auto& v : columns[0])
                a.push_back(Args(v)[0]);
            return link(node("SquareRoot", Object({{"Input", lift(a)}})));
        }
        return link(node("Divide", Object({{"A", lift({Value(1.0)})}, {"B", get(0)}})));
    }
    if (name == "RSQ")
    {
        auto root = node("SquareRoot", Object({{"Input", get(0)}}));
        return link(node("Divide", Object({{"A", lift({Value(1.0)})}, {"B", link(root)}})));
    }
    if (name == "POW")
    {
        bool base = true, exponent = true;
        for (const auto& v : columns.at(0))
            base &= IsNumber(v) && v.number() > 0;
        for (const auto& v : columns.at(1))
            exponent &= IsNumber(v) && v.number() > 0;
        if (base)
            return link(node("Power", Object({{"Base", get(0)}, {"Exponent", get(1)}})));
        if (exponent)
            return link(node("Power",
                             Object({{"Base", link(node("Abs", Object({{"Input", get(0)}})))}, {"Exponent", get(1)}})));
        throw Refused("a power without the test of its base that Unreal's Power node makes");
    }
    if (name == "DDX" || name == "DDY")
        return link(node(name, Object({{"Value", get(0)}})));
    if (name == "LG2")
        return link(node("Logarithm2", Object({{"X", get(0)}})));
    if (name == "U.TRUNC")
    {
        std::vector<Value> a;
        for (const auto& v : columns.at(0))
            a.push_back(arena.select(arena.op("T.NE", {arena.op("F.SGE", {v, Value(0)})}), arena.op("U.FLR", {v}),
                                     arena.op("U.CEIL", {v})));
        return lift(a);
    }
    if (name == "U.ROUND")
    {
        std::vector<Value> a;
        for (const auto& v : columns.at(0))
        {
            auto low = arena.op("U.FLR", {v}), fraction = arena.op("ADD", {v, arena.op("NEG", {low})}),
                 high = arena.op("ADD", {low, Value(1)}),
                 parity = arena.op("U.FRC", {arena.op("MUL", {low, Value(.5)})});
            auto tie = arena.select(arena.op("T.NE", {arena.op("F.SEQ", {parity, Value(0)})}), low, high);
            auto above = arena.select(arena.op("T.NE", {arena.op("F.SGT", {fraction, Value(.5)})}), high, tie);
            a.push_back(arena.select(arena.op("T.NE", {arena.op("F.SLT", {fraction, Value(.5)})}), low, above));
        }
        return lift(a);
    }
    throw Refused("the operation " + name);
}
inline IntegerInterval Lifter::integerBounds(const Value& value)
{
    constexpr double exact = 16777216.0;
    if (IsNumber(value))
    {
        double n = value.number();
        if (std::isfinite(n) && std::floor(n) == n && std::fabs(n) <= exact)
            return {true, n, n};
        return {};
    }
    if (value.kind != Value::Expr)
        return {};
    const auto& x = *value.expression;
    const auto& a = x.args;
    if (x.op == "COMP" && IsExpr(a[0], "SRC") && IsMember(Args(a[0])[0].text, {"Facing", "TwoSidedSign"}))
        return {true, -1, 1};
    if (IsMember(x.op, {"F.SGT", "F.SGE", "F.SLT", "F.SLE", "F.SEQ", "F.SNE"}))
        return {true, 0, 1};
    if (IsMember(x.op, {"S.SGT", "S.SGE", "S.SLT", "S.SLE", "S.SEQ", "S.SNE", "UC.SGT", "UC.SGE", "UC.SLT", "UC.SLE",
                        "UC.SEQ", "UC.SNE"}))
        return {true, -1, 0};
    if (x.op == "SEL")
    {
        auto left = integerBounds(a[1]), right = integerBounds(a[2]);
        if (left.valid && right.valid)
            return {true, std::min(left.lo, right.lo), std::max(left.hi, right.hi)};
        return {};
    }
    if (IsMember(x.op, {"NEG", "TOINT", "TRUNC.S", "TRUNC.U"}))
    {
        auto b = integerBounds(a[0]);
        if (!b.valid)
            return {};
        if (x.op == "NEG")
            return {true, -b.hi, -b.lo};
        if (x.op == "TRUNC.U")
            return {true, std::max(0.0, b.lo), std::max(0.0, b.hi)};
        return b;
    }
    if (x.op == "I.AND")
    {
        for (int i = 0; i < 2; ++i)
        {
            auto mask = integerBounds(a[i]);
            if (IsNumber(a[i]) && mask.valid && mask.lo >= 0 && integerBounds(a[1 - i]).valid)
                return {true, 0, mask.lo};
        }
        return {};
    }
    if (IsMember(x.op, {"ADD", "MUL", "MIN", "MAX"}))
    {
        auto left = integerBounds(a[0]), right = integerBounds(a[1]);
        if (!left.valid || !right.valid)
            return {};
        IntegerInterval out;
        out.valid = true;
        if (x.op == "ADD")
        {
            out.lo = left.lo + right.lo;
            out.hi = left.hi + right.hi;
        }
        else if (x.op == "MUL")
        {
            double products[] = {left.lo * right.lo, left.lo * right.hi, left.hi * right.lo, left.hi * right.hi};
            out.lo = *std::min_element(products, products + 4);
            out.hi = *std::max_element(products, products + 4);
        }
        else if (x.op == "MIN")
        {
            out.lo = std::min(left.lo, right.lo);
            out.hi = std::min(left.hi, right.hi);
        }
        else
        {
            out.lo = std::max(left.lo, right.lo);
            out.hi = std::max(left.hi, right.hi);
        }
        if (std::max(std::fabs(out.lo), std::fabs(out.hi)) <= exact)
            return out;
    }
    return {};
}
inline bool Lifter::floatInteger(const Value& value, Value& result)
{
    if (!integerBounds(value).valid)
        return false;
    if (IsNumber(value))
    {
        result = Value(value.number());
        return true;
    }
    const auto& x = *value.expression;
    const auto& a = x.args;
    if (x.op == "COMP" && IsExpr(a[0], "SRC") && IsMember(Args(a[0])[0].text, {"Facing", "TwoSidedSign"}))
    {
        result = src("TwoSidedSign", Value::null());
        return true;
    }
    if (x.op == "SEL")
    {
        Value left, right;
        if (!floatInteger(a[1], left) || !floatInteger(a[2], right))
            return false;
        result = arena.select(a[0], left, right);
        return true;
    }
    if (x.op == "TOINT" || x.op == "TRUNC.S")
        return floatInteger(a[0], result);
    if (x.op == "TRUNC.U")
    {
        Value inner;
        if (!floatInteger(a[0], inner))
            return false;
        result = simp("MAX", {inner, Value(0.0)});
        return true;
    }
    if (IsMember(x.op, {"NEG", "ADD", "MUL", "MIN", "MAX"}))
    {
        std::vector<Value> parts;
        for (const auto& v : a)
        {
            Value inner;
            if (!floatInteger(v, inner))
                return false;
            parts.push_back(inner);
        }
        result = simp(x.op, parts);
        return true;
    }
    if (IsMember(x.op, {"F.SGT", "F.SGE", "F.SLT", "F.SLE", "F.SEQ", "F.SNE"}))
    {
        result = arena.select(arena.op("T.NE", {value}), Value(1.0), Value(0.0));
        return true;
    }
    if (IsMember(x.op, {"S.SGT", "S.SGE", "S.SLT", "S.SLE", "S.SEQ", "S.SNE", "UC.SGT", "UC.SGE", "UC.SLT", "UC.SLE",
                        "UC.SEQ", "UC.SNE"}))
    {
        auto left = integerBounds(a[0]), right = integerBounds(a[1]);
        if (!left.valid || !right.valid || (x.op.substr(0, 3) == "UC." && (left.lo < 0 || right.lo < 0)))
            return false;
        Value l, r;
        if (!floatInteger(a[0], l) || !floatInteger(a[1], r))
            return false;
        auto name = "F." + x.op.substr(x.op.find('.') + 1);
        result = arena.select(arena.op("T.NE", {arena.op(name, {l, r})}), Value(-1.0), Value(0.0));
        return true;
    }
    if (x.op == "I.AND")
        for (int at = 0; at < 2; ++at)
        {
            if (NumberIs(a[at], 2) && integerBounds(a[1 - at]).valid)
            {
                Value source;
                if (!floatInteger(a[1 - at], source))
                    return false;
                auto low = arena.op("U.FLR", {arena.op("DIV", {source, Value(2.0)})}),
                     high = arena.op("U.FLR", {arena.op("DIV", {source, Value(4.0)})});
                result = simp("MUL", {Value(2.0), simp("ADD", {low, simp("NEG", {simp("MUL", {Value(2.0), high})})})});
                return true;
            }
        }
    return false;
}
inline bool Lifter::comparison(const Value& test, const std::string& name, std::vector<Value>& pair)
{
    if (IsExpr(test, name.c_str()))
    {
        pair = Args(test);
        return pair.size() == 2;
    }
    if (IsExpr(test, "T.NE") && Args(test).size() == 1)
    {
        const auto& inner = Args(test)[0];
        if (IsExpr(inner, name.c_str()))
        {
            pair = Args(inner);
            return pair.size() == 2;
        }
        if (IsExpr(inner, "TRUNC.U") && IsExpr(Args(inner)[0], name.c_str()))
        {
            pair = Args(Args(inner)[0]);
            return pair.size() == 2;
        }
    }
    return false;
}
inline bool Lifter::booleanMask(const Value& value)
{
    if (IsNumber(value))
        return NumberIs(value, 0) || NumberIs(value, 1) || NumberIs(value, -1);
    if (value.kind != Value::Expr)
        return false;
    const auto& x = *value.expression;
    if (x.op.size() > 3 && (x.op.substr(0, 3) == "F.S" || x.op.substr(0, 3) == "S.S" || x.op.substr(0, 4) == "UC.S") &&
        x.args.size() == 2)
        return true;
    if (x.op == "TRUNC.U" && x.args.size() == 1 && x.args[0].kind == Value::Expr &&
        x.args[0].expression->op.substr(0, 3) == "F.S")
        return booleanMask(x.args[0]);
    if ((x.op == "I.AND" || x.op == "I.OR") && x.args.size() == 2)
        return booleanMask(x.args[0]) && booleanMask(x.args[1]);
    return false;
}
inline Json Lifter::choice(const Value& test, const std::vector<Value>& yes, const std::vector<Value>& no)
{
    if (IsNumber(test))
        return lift(test.truth() ? yes : no);
    std::vector<Value> pair;
    if (IsExpr(test, "T.NE") && Args(test).size() == 1 && Args(test)[0].kind == Value::Expr)
    {
        const auto& inner = Args(test)[0];
        if ((IsExpr(inner, "I.AND") || IsExpr(inner, "I.OR")) && booleanMask(inner))
        {
            auto a = arena.op("T.NE", {Args(inner)[0]}), b = arena.op("T.NE", {Args(inner)[1]});
            std::vector<Value> result;
            for (std::size_t i = 0; i < yes.size(); ++i)
            {
                auto nested = arena.select(b, yes[i], no[i]);
                result.push_back(IsExpr(inner, "I.AND") ? arena.select(a, nested, no[i])
                                                        : arena.select(a, yes[i], nested));
            }
            return lift(result);
        }
    }
    if (IsExpr(test, "T.EQ") && Args(test).size() == 1 && booleanMask(Args(test)[0]))
        return choice(arena.op("T.NE", {Args(test)[0]}), no, yes);
    const auto capture = src("View", Value("reflection_capture_mask"));
    for (const auto& rule : std::vector<std::tuple<std::string, double, bool>>{
             {"F.SEQ", 1, true}, {"F.SNE", 1, false}, {"F.SEQ", 0, false}, {"F.SNE", 0, true}, {"F.SGT", 0, true}})
    {
        if (comparison(test, std::get<0>(rule), pair) && Same(pair[0], capture) && NumberIs(pair[1], std::get<1>(rule)))
        {
            const bool reflectionFirst = std::get<2>(rule);
            return link(
                node("ReflectionCapturePassSwitch", Object({{"Default", lift(reflectionFirst ? no : yes)},
                                                            {"Reflection", lift(reflectionFirst ? yes : no)}})));
        }
    }
    if (comparison(test, "F.SLE", pair) && IsNumber(pair[1]))
    {
        bool zeros = true, powers = true;
        for (const auto& v : yes)
            zeros &= NumberIs(v, 0);
        for (const auto& v : no)
            powers &= IsExpr(v, "POW");
        if (zeros && powers)
        {
            bool tested = true;
            std::vector<Value> base, exponent;
            for (const auto& v : no)
            {
                const auto& b = Args(v)[0];
                tested &= (NumberIs(pair[1], 0) && Same(b, pair[0])) ||
                          (IsExpr(b, "ADD") && Same(Args(b)[0], pair[0]) && NumberIs(Args(b)[1], -pair[1].number()));
                base.push_back(b);
                exponent.push_back(Args(v)[1]);
            }
            if (tested)
                return link(node("Power", Object({{"Base", lift(base)}, {"Exponent", lift(exponent)}})));
        }
    }
    for (const auto& kind : std::vector<std::string>{"F.SGE", "F.SLT", "F.SLE"})
        if (comparison(test, kind, pair))
        {
            const bool swapped = kind == "F.SLE", negated = kind == "F.SLT";
            return link(node("If", Object({{"A", lift({pair[swapped ? 1 : 0]})},
                                           {"B", lift({pair[swapped ? 0 : 1]})},
                                           {"AGreaterThanB", lift(negated ? no : yes)},
                                           {"ALessThanB", lift(negated ? yes : no)}})));
        }
    if (comparison(test, "F.SGT", pair))
    {
        if (IsNumber(pair[1]) && IsExpr(pair[0], "ABS") && IsExpr(Args(pair[0])[0], "ADD"))
        {
            const auto& gap = Args(Args(pair[0])[0]);
            std::vector<Value> reference, greater, less;
            bool matches = true;
            for (const auto& v : yes)
            {
                std::vector<Value> inner;
                if (!IsExpr(v, "SEL") || !comparison(Args(v)[0], "F.SGE", inner))
                {
                    matches = false;
                    break;
                }
                if (reference.empty())
                    reference = inner;
                else if (!Same(reference[0], inner[0]) || !Same(reference[1], inner[1]))
                {
                    matches = false;
                    break;
                }
                greater.push_back(Args(v)[1]);
                less.push_back(Args(v)[2]);
            }
            matches = matches && !reference.empty() && Same(gap[0], reference[0]) &&
                      ((IsExpr(gap[1], "NEG") && Same(Args(gap[1])[0], reference[1])) ||
                       (IsNumber(gap[1]) && IsNumber(reference[1]) && gap[1].number() == -reference[1].number()));
            if (matches)
                return link(node("If",
                                 Object({{"A", lift({reference[0]})},
                                         {"B", lift({reference[1]})},
                                         {"AGreaterThanB", lift(greater)},
                                         {"ALessThanB", lift(less)},
                                         {"AEqualsB", lift(no)}}),
                                 Object({{"EqualsThreshold", pair[1].number()}})));
        }
        return link(node("If", Object({{"A", lift({pair[1]})},
                                       {"B", lift({pair[0]})},
                                       {"AGreaterThanB", lift(no)},
                                       {"ALessThanB", lift(yes)}})));
    }
    for (const auto& kind : std::vector<std::string>{"F.SEQ", "F.SNE"})
        if (comparison(test, kind, pair))
        {
            const auto a = lift({pair[0]}), b = lift({pair[1]}), differ = lift(kind == "F.SNE" ? yes : no),
                       equal = lift(kind == "F.SNE" ? no : yes);
            return link(node(
                "If",
                Object({{"A", a}, {"B", b}, {"AGreaterThanB", differ}, {"ALessThanB", differ}, {"AEqualsB", equal}}),
                Object({{"EqualsThreshold", 0.0}})));
        }
    throw Refused("a choice that is not one of the If node's", "a choice that is not one of the If node's");
}
}
} // P4MapKit::Material

// The Material uniform buffer UE 4.26 makes from a material graph, without the editor
// (tools/navigation/material_uniform_order.py, the same model in C++).
//
// A material's uniform expression set (its vector and scalar preshaders and its textures) is laid out in the order
// the HLSL translator first accesses each expression (FHLSLMaterialTranslator::AccessUniformExpression, AddUnique),
// not the order it makes them. This replays that order over a graph: the properties in Translate's order and only
// where UE compiles them, each expression once per shader frequency, Add/Sub/Mul/Div hashing both operands first,
// every other access a printf argument (MSVC evaluates those right to left), constants never accessed.
//
// The graph is held in insertion order (CanonGraph): the rewrites of material_graph_canon.h take the first match in
// node order, as the Python model does over its dicts.
#pragma once
#include "material_json.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <set>
#include <unordered_map>
#include "material_uniform_order_graph.h"
#include "material_uniform_order_translator.h"
namespace P4MapKit
{
namespace Material
{
// ----- preshader text: parse, evaluate -----

namespace UniformOrder
{
struct ParseError : std::runtime_error
{
    explicit ParseError(const std::string& t) : std::runtime_error(t)
    {
    }
};
struct Tree
{
    std::string Kind;           // param, const, op, fn, append, swz
    std::string Name;           // param name, op symbol, function name, swizzle letters
    std::vector<double> Values; // const: 4 numbers
    int Count = 0;              // append's component count
    std::vector<Tree> Args;
    bool operator==(const Tree& o) const
    {
        return Kind == o.Kind && Name == o.Name && Count == o.Count && Args == o.Args &&
               Values.size() == o.Values.size() && std::equal(Values.begin(), Values.end(), o.Values.begin());
    }
};
// Python's float() of a number the preshader text holds.
inline double PyFloat(const std::string& text)
{
    if (text == "inf")
        return HUGE_VAL;
    if (text == "-inf")
        return -HUGE_VAL;
    if (text == "nan")
        return std::nan("");
    char* end = nullptr;
    const double v = std::strtod(text.c_str(), &end);
    if (text.empty() || end != text.c_str() + text.size())
        throw ParseError(text);
    return v;
}
class TreeParser
{
    const std::string& t;
    std::vector<std::string> names;
    bool At(std::size_t at, const std::string& word) const
    {
        return t.compare(at, word.size(), word) == 0;
    }
    // -?[0-9.e+-]+|-?inf|nan
    bool Number(std::size_t at, std::size_t& end, std::string& text) const
    {
        std::size_t p = at;
        if (p < t.size() && t[p] == '-')
            ++p;
        std::size_t q = p;
        while (q < t.size() && (std::isdigit(static_cast<unsigned char>(t[q])) || t[q] == '.' || t[q] == 'e' ||
                                t[q] == '+' || t[q] == '-'))
            ++q;
        if (q > p && (q == t.size() || t[q] == ',' || t[q] == ')'))
        {
            end = q;
            text = t.substr(at, q - at);
            return true;
        }
        q = at;
        while (q < t.size() && (std::isdigit(static_cast<unsigned char>(t[q])) || t[q] == '.' || t[q] == 'e' ||
                                t[q] == '+' || t[q] == '-'))
            ++q;
        if (q > at && (q == t.size() || t[q] == ',' || t[q] == ')'))
        {
            end = q;
            text = t.substr(at, q - at);
            return true;
        }
        if (At(p, "inf"))
        {
            end = p + 3;
            text = t.substr(at, end - at);
            return true;
        }
        if (At(at, "nan"))
        {
            end = at + 3;
            text = "nan";
            return true;
        }
        return false;
    }
    bool Constant(std::size_t at, Tree& out, std::size_t& end) const
    {
        if (!At(at, "("))
            return false;
        std::size_t p = at + 1;
        std::vector<double> values;
        for (int i = 0; i < 4; ++i)
        {
            std::size_t e = 0;
            std::string text;
            if (!Number(p, e, text))
                return false;
            const char* sep = i < 3 ? ", " : ")";
            if (!At(e, sep))
                return false;
            values.push_back(PyFloat(text));
            p = e + std::strlen(sep);
        }
        out.Kind = "const";
        out.Values = values;
        end = p;
        return true;
    }

public:
    TreeParser(const std::string& text, const std::set<std::string>& known) : t(text), names(known.begin(), known.end())
    {
        std::stable_sort(names.begin(), names.end(), [](const std::string& a, const std::string& b)
                         { return a.size() != b.size() ? a.size() > b.size() : a < b; });
    }
    Tree Expr(std::size_t at, std::size_t& end)
    {
        Tree node = Atom(at, end);
        while (end < t.size() && t[end] == '.' && end + 1 < t.size() &&
               std::string("rgba").find(t[end + 1]) != std::string::npos)
        {
            std::size_t e = end + 1;
            while (e < t.size() && std::string("rgba").find(t[e]) != std::string::npos)
                ++e;
            Tree swz;
            swz.Kind = "swz";
            swz.Name = t.substr(end + 1, e - end - 1);
            swz.Args.push_back(node);
            node = swz;
            end = e;
        }
        return node;
    }
    Tree Atom(std::size_t at, std::size_t& end)
    {
        Tree out;
        if (Constant(at, out, end))
            return out;
        if (At(at, "append") && at + 7 < t.size() && std::isdigit(static_cast<unsigned char>(t[at + 6])) &&
            t[at + 7] == '(')
        {
            std::size_t e = at + 6;
            while (e < t.size() && std::isdigit(static_cast<unsigned char>(t[e])))
                ++e;
            if (e < t.size() && t[e] == '(')
            {
                out.Kind = "append";
                out.Count = std::stoi(t.substr(at + 6, e - at - 6));
                std::size_t a2 = 0, a3 = 0;
                out.Args.push_back(Expr(e + 1, a2));
                if (!At(a2, ", "))
                    throw ParseError(t);
                out.Args.push_back(Expr(a2 + 2, a3));
                if (!At(a3, ")"))
                    throw ParseError(t);
                end = a3 + 1;
                return out;
            }
        }
        static const char* functions[] = {"saturate", "floor", "ceil",  "frac", "abs",   "sqrt", "length",
                                          "sign",     "round", "trunc", "log2", "log10", "min",  "max",
                                          "clamp",    "dot",   "cross", "fmod", "sin",   "cos"};
        for (const char* fn : functions)
            if (At(at, std::string(fn) + "("))
            {
                out.Kind = "fn";
                out.Name = fn;
                std::size_t a2 = at + std::strlen(fn) + 1;
                for (;;)
                {
                    std::size_t e = 0;
                    out.Args.push_back(Expr(a2, e));
                    a2 = e;
                    if (At(a2, ", "))
                    {
                        a2 += 2;
                        continue;
                    }
                    if (At(a2, ")"))
                    {
                        end = a2 + 1;
                        return out;
                    }
                    throw ParseError(t);
                }
            }
        if (At(at, "("))
        {
            std::size_t a2 = 0;
            Tree a = Expr(at + 1, a2);
            for (const char* sym : {"+", "-", "*", "/"})
                if (At(a2, std::string(" ") + sym + " "))
                {
                    std::size_t a3 = 0;
                    Tree b = Expr(a2 + 3, a3);
                    if (!At(a3, ")"))
                        throw ParseError(t);
                    out.Kind = "op";
                    out.Name = sym;
                    out.Args = {a, b};
                    end = a3 + 1;
                    return out;
                }
            throw ParseError(t);
        }
        for (const auto& name : names)
            if (At(at, name))
            {
                out.Kind = "param";
                out.Name = name;
                end = at + name.size();
                return out;
            }
        throw ParseError(t);
    }
};
inline Tree Parse(const std::string& text, const std::set<std::string>& names)
{
    TreeParser parser(text, names);
    std::size_t end = 0;
    Tree node = parser.Expr(0, end);
    if (end != text.size())
        throw ParseError(text);
    return node;
}

// Python's arithmetic errors: what material_graph_canon.same_value catches (ValueError, OverflowError, ...).
struct EvalFailure : std::runtime_error
{
    explicit EvalFailure(const std::string& t) : std::runtime_error(t)
    {
    }
};
inline std::vector<double> Widen(std::vector<double> v, std::size_t n)
{
    return v.size() == 1 ? std::vector<double>(n, v[0]) : v;
}
inline double Integral(double v, const char* what)
{
    if (std::isnan(v))
        throw EvalFailure(std::string(what) + " of nan");
    if (std::isinf(v))
        throw EvalFailure(std::string(what) + " of inf");
    return v;
}
inline std::vector<double> Evaluate(const Tree& node, const std::map<std::string, double>& scalars,
                                    const std::map<std::string, std::vector<double>>& vectors)
{
    if (node.Kind == "param")
    {
        const auto s = scalars.find(node.Name);
        if (s != scalars.end())
            return {s->second};
        const auto v = vectors.find(node.Name);
        if (v == vectors.end())
            throw EvalFailure("no value for " + node.Name);
        return v->second;
    }
    if (node.Kind == "const")
    {
        const auto& v = node.Values;
        // v.count(v[0]) == 4: v[0] counts itself (a list's count matches identity first), the others by ==.
        int same = 1;
        for (std::size_t i = 1; i < 4; ++i)
            same += v[i] == v[0];
        if (same == 4)
            return {v[0]};
        std::size_t last = 0;
        for (std::size_t i = 0; i < 4; ++i)
            if (v[i] != 0.0 || i == 0)
                last = i;
        return std::vector<double>(v.begin(), v.begin() + std::ptrdiff_t(last + 1));
    }
    if (node.Kind == "swz")
    {
        const auto v = Evaluate(node.Args[0], scalars, vectors);
        std::vector<double> out;
        for (const char ch : node.Name)
        {
            if (v.size() == 1)
            {
                out.push_back(v[0]);
                continue;
            }
            const auto at = std::string("rgba").find(ch);
            if (at >= v.size())
                throw EvalFailure("swizzle out of range");
            out.push_back(v[at]);
        }
        return out;
    }
    if (node.Kind == "append")
    {
        auto a = Evaluate(node.Args[0], scalars, vectors);
        const auto b = Evaluate(node.Args[1], scalars, vectors);
        a.insert(a.end(), b.begin(), b.end());
        return a;
    }
    if (node.Kind == "op")
    {
        auto a = Evaluate(node.Args[0], scalars, vectors), b = Evaluate(node.Args[1], scalars, vectors);
        const auto n = std::max(a.size(), b.size());
        a = Widen(a, n);
        b = Widen(b, n);
        std::vector<double> out;
        const char op = node.Name[0];
        for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i)
        {
            const double x = a[i], y = b[i];
            out.push_back(op == '+'   ? x + y
                          : op == '-' ? x - y
                          : op == '*' ? x * y
                                      : x / (std::fabs(y) >= 1e-5 ? y : (y >= 0 ? 1e-5 : -1e-5)));
        }
        return out;
    }
    if (node.Kind == "fn")
    {
        std::vector<std::vector<double>> args;
        for (const auto& a : node.Args)
            args.push_back(Evaluate(a, scalars, vectors));
        const auto& name = node.Name;
        const auto arity = [&](std::size_t n)
        {
            if (args.size() != n)
                throw EvalFailure(name + " takes another number of arguments");
        };
        if (name == "dot")
        {
            arity(2);
            const auto n = std::max(args[0].size(), args[1].size());
            const auto a = Widen(args[0], n), b = Widen(args[1], n);
            double sum = 0;
            for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i)
                sum += a[i] * b[i];
            return {sum};
        }
        if (name == "length")
        {
            if (args.empty())
                throw EvalFailure("length has no argument");
            double sum = 0;
            for (const double x : args[0])
                sum += x * x;
            return {std::sqrt(sum)};
        }
        if (name == "min" || name == "max" || name == "fmod")
        {
            arity(2);
            const auto& a = args[0];
            const auto b = args[1].size() == 1 ? std::vector<double>(a.size(), args[1][0]) : args[1];
            std::vector<double> out;
            for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i)
            {
                const double x = a[i], y = b[i];
                if (name == "min")
                    out.push_back(PyMin(x, y));
                else if (name == "max")
                    out.push_back(PyMax(x, y));
                else
                {
                    if (std::isnan(x) || std::isnan(y))
                        out.push_back(std::nan(""));
                    else if (y == 0 || std::isinf(x))
                        throw EvalFailure("math domain error");
                    else
                        out.push_back(std::fmod(x, y));
                }
            }
            return out;
        }
        if (name == "clamp")
        {
            arity(3);
            const auto& x = args[0];
            const auto lo = args[1].size() == 1 ? std::vector<double>(x.size(), args[1][0]) : args[1];
            const auto hi = args[2].size() == 1 ? std::vector<double>(x.size(), args[2][0]) : args[2];
            std::vector<double> out;
            for (std::size_t i = 0; i < std::min({x.size(), lo.size(), hi.size()}); ++i)
                out.push_back(PyMin(PyMax(x[i], lo[i]), hi[i]));
            return out;
        }
        static const std::set<std::string> one = {"saturate", "floor", "ceil", "frac", "abs",  "sqrt", "sign",
                                                  "round",    "trunc", "sin",  "cos",  "log2", "log10"};
        if (one.count(name))
        {
            if (args.empty())
                throw EvalFailure(name + " has no argument");
            std::vector<double> out;
            for (const double v : args[0])
            {
                double r = 0;
                if (name == "saturate")
                    r = PyMin(PyMax(v, 0.0), 1.0);
                else if (name == "floor")
                    r = std::floor(Integral(v, "floor"));
                else if (name == "ceil")
                    r = std::ceil(Integral(v, "ceil"));
                else if (name == "frac")
                    r = v - std::floor(Integral(v, "floor"));
                else if (name == "abs")
                    r = std::fabs(v);
                else if (name == "sqrt")
                    r = std::sqrt(PyMax(v, 0.0));
                else if (name == "sign")
                    r = double((v > 0) - (v < 0));
                else if (name == "round")
                    r = std::nearbyint(Integral(v, "round"));
                else if (name == "trunc")
                    r = std::trunc(Integral(v, "trunc"));
                else if (name == "sin" || name == "cos")
                {
                    if (std::isinf(v))
                        throw EvalFailure("math domain error");
                    r = name == "sin" ? std::sin(v) : std::cos(v);
                }
                else if (name == "log2")
                    r = v > 0 ? std::log2(v) : -HUGE_VAL;
                else
                    r = v > 0 ? std::log10(v) : -HUGE_VAL;
                out.push_back(r);
            }
            return out;
        }
    }
    throw ParseError(node.Kind + " " + node.Name);
}
} // UniformOrder
}
} // P4MapKit::Material

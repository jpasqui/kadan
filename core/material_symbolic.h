// Interned expression execution of the material subset, with lazy branch joins.
// Python oracle: material_symbolic.py and material_arrays.py.
#pragma once
#include "material_numeric.h"
#include <set>
#include <tuple>

namespace P4MapKit
{
namespace Material
{
struct Expression
{
    std::string op;
    std::vector<Value> args;
    std::size_t id = 0;
    ~Expression();
};
// Release long chains without recursing through shared_ptr member destructors.
inline Expression::~Expression()
{
    struct Cleanup
    {
        bool active = false;
        std::vector<std::shared_ptr<const Expression>> nodes;
    };
    static thread_local Cleanup cleanup;
    for (auto& arg : args)
        if (arg.expression)
            cleanup.nodes.push_back(std::move(arg.expression));
    if (cleanup.active)
        return;
    cleanup.active = true;
    while (!cleanup.nodes.empty())
    {
        auto node = std::move(cleanup.nodes.back());
        cleanup.nodes.pop_back();
        node.reset();
    }
    cleanup.active = false;
}
inline bool symbolic(const Value& v)
{
    return v.kind == Value::Expr;
}
// The key is the text an ostringstream gave, written without one: a stream takes the runtime's locale lock when
// it is made, and the material workers made one for every value they interned.
inline void append_hex(std::string& out, std::uint64_t bits, const char* digits, int width)
{
    char text[16];
    int n = 0;
    do
    {
        text[n++] = digits[bits & 15];
        bits >>= 4;
    } while (bits);
    for (int pad = n; pad < width; ++pad)
        out += '0';
    while (n)
        out += text[--n];
}
inline void append_value_key(std::string& s, const Value& v)
{
    s += std::to_string(int(v.kind));
    s += ':';
    switch (v.kind)
    {
        case Value::Float:
        {
            std::uint64_t bits;
            std::memcpy(&bits, &v.real, 8);
            append_hex(s, bits, "0123456789abcdef", 0);
            break;
        }
        case Value::Int:
            s += v.integer.key();
            break;
        case Value::Boolean:
            s += v.boolean ? '1' : '0';
            break;
        case Value::Text:
            s += std::to_string(v.text.size());
            s += ':';
            s += v.text;
            break;
        case Value::Expr:
            // As a stream writes a pointer (%p): every digit of the address, upper case.
            append_hex(s, std::uint64_t(reinterpret_cast<std::uintptr_t>(v.expression.get())), "0123456789ABCDEF",
                int(2 * sizeof(void*)));
            break;
        case Value::Handle:
            for (auto w : v.handle)
            {
                s += std::to_string(w.first);
                s += ',';
                s += std::to_string(w.second);
                s += ';';
            }
            break;
        case Value::Null:
            break;
        case Value::Tuple:
            for (const auto& item : v.tuple)
            {
                std::string k;
                append_value_key(k, item);
                s += std::to_string(k.size());
                s += ':';
                s += k;
            }
            break;
    }
}
// A value as a person reads it, expressions written out a few levels deep, for refusal messages.
inline std::string value_text(const Value& v, int depth = 4)
{
    switch (v.kind)
    {
        case Value::Float:
            return std::to_string(v.real);
        case Value::Int:
            return v.integer.key();
        case Value::Boolean:
            return v.boolean ? "true" : "false";
        case Value::Text:
            return v.text;
        case Value::Expr:
        {
            if (!v.expression)
                return "?";
            std::string s = v.expression->op + "(";
            for (std::size_t i = 0; i < v.expression->args.size(); ++i)
                s += (i ? ", " : "") + (depth > 0 ? value_text(v.expression->args[i], depth - 1) : std::string("..."));
            return s + ")";
        }
        default:
            return "?";
    }
}
inline std::string value_key(const Value& v)
{
    std::string s;
    append_value_key(s, v);
    return s;
}
// Whether a value is, or is built from, another value.
inline bool value_uses(const Value& v, const Value& part, int depth = 32)
{
    if (value_key(v) == value_key(part))
        return true;
    if (v.kind != Value::Expr || !v.expression || depth <= 0)
        return false;
    for (const auto& arg : v.expression->args)
        if (value_uses(arg, part, depth - 1))
            return true;
    return false;
}
inline bool same(const Value& a, const Value& b)
{
    if (a.kind == Value::Expr || b.kind == Value::Expr)
        return a.kind == b.kind && a.expression == b.expression;
    return value_key(a) == value_key(b);
}
class SymbolicArena
{
public:
    std::map<std::string, std::shared_ptr<const Expression>> table;
    Value mk(const std::string& name, const std::vector<Value>& args)
    {
        std::string key = std::to_string(name.size()) + ":" + name;
        for (const auto& a : args)
        {
            auto k = value_key(a);
            key += "|" + std::to_string(k.size()) + ":" + k;
        }
        auto at = table.find(key);
        Value v;
        v.kind = Value::Expr;
        if (at != table.end())
            v.expression = at->second;
        else
        {
            auto node = std::make_shared<Expression>();
            node->op = name;
            node->args = args;
            node->id = table.size();
            v.expression = node;
            table.emplace(key, node);
        }
        return v;
    }
    Value op(const std::string& name, const std::vector<Value>& args)
    {
        for (const auto& a : args)
            if (symbolic(a))
                return mk(name, args);
        return numeric_operation(name, args);
    }
    Value select(const Value& t, const Value& a, const Value& b)
    {
        if (!symbolic(t))
            return t.truth() ? a : b;
        if (same(a, b))
            return a;
        return mk("SEL", {t, a, b});
    }
};
struct Bound
{
    bool has_lower = false, has_upper = false;
    double lower = 0, upper = 0;
    Bound() = default;
    Bound(double l, double h) : has_lower(true), has_upper(true), lower(l), upper(h)
    {
    }
    static Bound lower_only(double l)
    {
        Bound b;
        b.has_lower = true;
        b.lower = l;
        return b;
    }
};
using InputBounds = std::map<std::size_t, Bound>;
inline Bound bounds(const Value& v, const InputBounds& known, std::map<std::size_t, Bound>& memo)
{
    if (!symbolic(v))
    {
        if ((v.kind != Value::Float && v.kind != Value::Int) || !std::isfinite(v.number()))
            return Bound();
        return Bound(v.number(), v.number());
    }
    auto& x = *v.expression;
    auto key = known.find(x.id);
    if (key != known.end())
        return key->second;
    auto done = memo.find(x.id);
    if (done != memo.end())
        return done->second;
    auto at = [&](std::size_t i)
    {
        return bounds(x.args.at(i), known, memo);
    };
    Bound result;
    if (x.op == "TOINT" || x.op == "TRUNC.S" || x.op == "TRUNC.U")
    {
        result = at(0);
        if (x.op != "TOINT")
        {
            double lo = x.op == "TRUNC.U" ? 0 : -2147483648.0, hi = x.op == "TRUNC.U" ? 4294967295.0 : 2147483647.0;
            result.lower = result.has_lower ? std::min(hi, std::max(lo, result.lower)) : lo;
            result.upper = result.has_upper ? std::min(hi, std::max(lo, result.upper)) : hi;
            result.has_lower = result.has_upper = true;
        }
        if (result.has_lower)
            result.lower = std::trunc(result.lower);
        if (result.has_upper)
            result.upper = std::trunc(result.upper);
    }
    else if (x.op == "U.FLR")
    {
        result = at(0);
        if (result.has_lower)
            result.lower = std::floor(result.lower);
        if (result.has_upper)
            result.upper = std::floor(result.upper);
    }
    else if (x.op == "ABS")
    {
        auto b = at(0);
        result.has_lower = true;
        result.lower = 0;
        if (b.has_lower && b.has_upper)
        {
            result.has_upper = true;
            result.upper = std::max(std::fabs(b.lower), std::fabs(b.upper));
        }
    }
    else if (x.op == "NEG")
    {
        auto b = at(0);
        result.has_lower = b.has_upper;
        result.has_upper = b.has_lower;
        result.lower = -b.upper;
        result.upper = -b.lower;
    }
    else if (x.op == "I.MOD" && !symbolic(x.args[1]) && x.args[1].as_int().compare(Integer()) > 0)
    {
        double divisor = x.args[1].as_int().number();
        auto b = at(0);
        result = Bound(b.has_lower && b.lower >= 0 ? 0 : 1 - divisor, b.has_upper && b.upper <= 0 ? 0 : divisor - 1);
    }
    else if (x.op == "I.AND" && !symbolic(x.args[1]) && x.args[1].as_int().compare(Integer()) >= 0)
        result = Bound(0, x.args[1].as_int().number());
    else if (x.op == "I.MAD")
    {
        auto a = at(0), b = at(1), c = at(2);
        if (a.has_lower && a.has_upper && b.has_lower && b.has_upper && c.has_lower && c.has_upper)
        {
            double products[] = {std::trunc(a.lower) * std::trunc(b.lower), std::trunc(a.lower) * std::trunc(b.upper),
                                 std::trunc(a.upper) * std::trunc(b.lower), std::trunc(a.upper) * std::trunc(b.upper)};
            result = Bound(*std::min_element(products, products + 4) + std::trunc(c.lower),
                           *std::max_element(products, products + 4) + std::trunc(c.upper));
        }
    }
    else if (x.op == "SEL")
    {
        auto a = at(1), b = at(2);
        result.has_lower = a.has_lower && b.has_lower;
        result.has_upper = a.has_upper && b.has_upper;
        result.lower = std::min(a.lower, b.lower);
        result.upper = std::max(a.upper, b.upper);
    }
    memo[x.id] = result;
    return result;
}
inline Bound bounds(const Value& v, const InputBounds& known = InputBounds())
{
    std::map<std::size_t, Bound> memo;
    return bounds(v, known, memo);
}
struct SymbolicKill
{
    std::vector<std::pair<Value, bool>> path;
    Value condition;
    std::size_t line = 0;
};
class SymbolicMachine : public NumericMachine
{
public:
    std::shared_ptr<SymbolicArena> arena;
    InputBounds input_bounds;
    std::shared_ptr<std::vector<SymbolicKill>> symbolic_kills = std::make_shared<std::vector<SymbolicKill>>();
    std::vector<std::pair<Value, bool>> path;
    SymbolicMachine(std::vector<std::string> l, std::shared_ptr<SymbolicArena> a = std::make_shared<SymbolicArena>(),
                    InputBounds b = InputBounds())
        : NumericMachine(std::move(l)), arena(std::move(a)), input_bounds(std::move(b))
    {
    }
    Value operation(const std::string& name, const std::vector<Value>& a) override
    {
        return arena->op(name, a);
    }
    Value select(const Value& t, const Value& a, const Value& b) override
    {
        return arena->select(t, a, b);
    }
    Vec input(const std::string& kind, int i, int k = 0) override
    {
        Vec out;
        for (int c = 0; c < 4; ++c)
            out[c] = arena->mk("IN", {kind, i, kind == "buffer" ? k : kind == "facing" ? 0 : c});
        return out;
    }
    Vec indexed(const std::string& name, const Value& at) override
    {
        if (!symbolic(at))
            return NumericMachine::indexed(name, at);
        auto range = bounds(at, input_bounds);
        if (!range.has_lower || !range.has_upper || !std::isfinite(range.lower) || !std::isfinite(range.upper) ||
            range.upper - range.lower + 1 > 64 || range.upper < range.lower)
            throw EvalError("a local array address has no bounded finite range: " + name);
        // Bounds must fit the checked finite selector enumeration, with no cast UB.
        if (range.lower < -2147483648.0 || range.upper > 2147483647.0)
            throw EvalError("a local array address has no bounded finite range: " + name);
        int lo = int(range.lower), hi = int(range.upper);
        std::vector<std::string> keys;
        std::string missing;
        for (std::int64_t i = lo; i <= hi; ++i)
        {
            auto key = name + "[" + std::to_string(i) + "]";
            keys.push_back(key);
            if (reg.find(key) == reg.end())
                missing += (missing.empty() ? "" : ", ") + key;
        }
        if (!missing.empty())
            throw EvalError("a local array address can read uninitialized entries: " + missing);
        Vec result = reg.at(keys.back());
        auto index = arena->mk("ARRAYINDEX", {at});
        for (std::size_t i = keys.size() - 1; i; --i)
        {
            auto test = arena->op("F.SEQ", {index, lo + int(i) - 1});
            for (int c = 0; c < 4; ++c)
                result[c] = select(test, reg.at(keys[i - 1])[c], result[c]);
        }
        return result;
    }
    Vec condition(const std::string& text) override
    {
        std::string s = trim(text);
        if (s == "TR" || starts(s, "TR."))
        { // Base parser still validates swizzles.
            std::smatch m;
            if (std::regex_match(s, m, std::regex("^TR(?:\\.([xyzw]+))?$")) &&
                (!m[1].matched || m[1].str().size() == 1 || m[1].str().size() == 4))
                return splat(true);
        }
        if (s == "FL" || starts(s, "FL."))
        {
            std::smatch m;
            if (std::regex_match(s, m, std::regex("^FL(?:\\.([xyzw]+))?$")) &&
                (!m[1].matched || m[1].str().size() == 1 || m[1].str().size() == 4))
                return splat(false);
        }
        return NumericMachine::condition(text);
    }
    void record_kill(std::size_t i, const Value& dead) override
    {
        symbolic_kills->push_back({path, dead, i});
    }
    Vec texture_read(const std::string& op, const std::string& target, int t, int s, const Vec& coordinates) override
    {
        std::vector<Value> args = {op, target, t < 0 ? Value::null() : Value(t), s < 0 ? Value::null() : Value(s)};
        args.insert(args.end(), coordinates.begin(), coordinates.end());
        auto tex = arena->mk("TEX", args);
        Vec out;
        for (int c = 0; c < 4; ++c)
            out[c] = arena->mk("COMP", {tex, c});
        return out;
    }
    Vec derivative(std::size_t, const std::string& op, const std::string& source) override
    {
        Vec out = read(source);
        for (auto& v : out)
            v = symbolic(v) ? arena->mk(op, {v}) : Value(0.0);
        return out;
    }
    std::pair<std::size_t, std::size_t> blocks(std::size_t pc) const
    {
        int depth = 0;
        std::size_t other = lines.size();
        for (std::size_t k = pc + 1; k < lines.size(); ++k)
        {
            if (starts(lines[k], "IF "))
                ++depth;
            else if (depth == 0 && lines[k] == "ELSE")
                other = k;
            else if (lines[k] == "ENDIF")
            {
                if (depth == 0)
                    return {other, k};
                --depth;
            }
        }
        throw EvalError("IF without ENDIF");
    }
    void run_range(std::size_t start, std::size_t end, std::size_t stop)
    {
        end = std::min(end, stop);
        for (std::size_t pc = start; pc < end; ++pc)
        {
            if (starts(lines[pc], "IF "))
            {
                auto test = condition(lines[pc].substr(3))[0];
                auto range = blocks(pc);
                if (symbolic(test))
                {
                    if (stop != std::numeric_limits<std::size_t>::max() && pc < stop && stop <= range.second)
                        throw EvalError("the stop line is inside an IF whose condition is not a number");
                    SymbolicMachine a = *this, b = *this;
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
                        auto va = a.registered(name), vb = b.registered(name);
                        Vec v;
                        for (int c = 0; c < 4; ++c)
                            v[c] = select(test, va[c], vb[c]);
                        reg[name] = v;
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
    SymbolicMachine& run(std::size_t stop = std::numeric_limits<std::size_t>::max()) override
    {
        run_range(0, lines.size(), stop);
        return *this;
    }
};
inline SymbolicMachine symbolic_trace(const std::vector<std::string>& lines,
                                      std::shared_ptr<SymbolicArena> arena = std::make_shared<SymbolicArena>(),
                                      std::size_t stop = std::numeric_limits<std::size_t>::max(),
                                      const InputBounds& bounds = InputBounds())
{
    SymbolicMachine m(lines, arena, bounds);
    m.run(stop);
    return m;
}

// Evaluation uses an explicit work stack so renderer-sized DAGs do not consume
// the host thread's call stack. SEL schedules only the selected branch.
class Evaluation
{
    using Key = std::tuple<const Expression*, int, int>;
    NumericEnv env;
    std::map<Key, Value> memo;
    std::map<Key, Vec> textures;
    Value ready(const Value& v, int dx, int dy) const
    {
        return symbolic(v) ? memo.at(Key(v.expression.get(), dx, dy)) : v;
    }

public:
    explicit Evaluation(const NumericEnv& e) : env(e)
    {
    }
    Value value(const Value& v, int dx = 0, int dy = 0)
    {
        if (!symbolic(v))
            return v;
        struct Frame
        {
            std::shared_ptr<const Expression> expression;
            int dx, dy;
        };
        std::vector<Frame> work = {{v.expression, dx, dy}};
        while (!work.empty())
        {
            const Frame frame = work.back();
            const auto& x = *frame.expression;
            Key key(&x, frame.dx, frame.dy);
            if (memo.find(key) != memo.end())
            {
                work.pop_back();
                continue;
            }
            auto need = [&](const Value& arg, int ox, int oy)
            {
                if (symbolic(arg) && memo.find(Key(arg.expression.get(), ox, oy)) == memo.end())
                {
                    work.push_back({arg.expression, ox, oy});
                    return true;
                }
                return false;
            };
            auto at = [&](std::size_t i)
            {
                return ready(x.args.at(i), frame.dx, frame.dy);
            };
            Value out;
            if (x.op == "IN")
            {
                auto kind = x.args.at(0).text;
                int index = int(x.args.at(1).number()), component = int(x.args.at(2).number());
                if (kind == "position")
                {
                    out = env.position.at(component);
                    if (frame.dx || frame.dy)
                    {
                        if (component == 0)
                            out = numeric_operation("ADD", {out, frame.dx});
                        else if (component == 1)
                            out = numeric_operation("ADD", {out, frame.dy});
                    }
                }
                else if (kind == "facing")
                    out = env.facing;
                else if (kind == "attrib")
                {
                    if (env.attrib_function)
                    {
                        double px = env.position[0].number(), py = env.position[1].number();
                        if (frame.dx || frame.dy)
                        {
                            px += frame.dx;
                            py += frame.dy;
                        }
                        auto table = env.attrib_function(px, py);
                        auto found = table.find(index);
                        if (found == table.end())
                            throw EvalError("no interpolant " + std::to_string(index));
                        out = found->second.at(component);
                    }
                    else
                        out = env.attrib(index).at(component);
                }
                else
                    out = env.word(index, component);
            }
            else if (x.op == "SEL")
            {
                if (need(x.args.at(0), frame.dx, frame.dy))
                    continue;
                const auto& selected = x.args.at(at(0).truth() ? 1 : 2);
                if (need(selected, frame.dx, frame.dy))
                    continue;
                out = ready(selected, frame.dx, frame.dy);
            }
            else if (x.op == "COMP")
            {
                if (!symbolic(x.args.at(0)) || x.args[0].expression->op != "TEX")
                    throw EvalError("COMP source is not a texture");
                auto tex = x.args[0].expression;
                Key tk(tex.get(), frame.dx, frame.dy);
                auto found = textures.find(tk);
                if (found == textures.end())
                {
                    const auto& a = tex->args;
                    bool pending = false;
                    for (int c = 0; c < 4; ++c)
                        if (need(a.at(c + 4), frame.dx, frame.dy))
                        {
                            pending = true;
                            break;
                        }
                    if (pending)
                        continue;
                    std::string name = a.at(0).text, target = a.at(1).text;
                    int texture = a.at(2).kind == Value::Null ? -1 : int(a.at(2).number());
                    int sampler = a.at(3).kind == Value::Null ? -1 : int(a.at(3).number());
                    Vec coords;
                    for (int c = 0; c < 4; ++c)
                        coords[c] = ready(a.at(c + 4), frame.dx, frame.dy);
                    Vec color;
                    if ((name == "TEX" || name == "TXB" || name == "TXL") && target == "2D")
                    {
                        if (!env.texture)
                            throw EvalError("texture read without a texture function");
                        color = env.texture(texture, sampler, {{coords[0].number(), coords[1].number()}});
                    }
                    else
                        color = env.other ? env.other(name, target, texture, sampler, coords) : splat(0.0);
                    found = textures.emplace(tk, color).first;
                }
                out = found->second.at(std::size_t(x.args.at(1).number()));
            }
            else if (x.op == "DDX" || x.op == "DDY")
            {
                int ox = frame.dx + (x.op == "DDX"), oy = frame.dy + (x.op == "DDY");
                if (need(x.args.at(0), ox, oy) || need(x.args.at(0), frame.dx, frame.dy))
                    continue;
                out = numeric_operation("SUB", {ready(x.args.at(0), ox, oy), at(0)});
            }
            else
            {
                bool pending = false;
                for (const auto& arg : x.args)
                    if (need(arg, frame.dx, frame.dy))
                    {
                        pending = true;
                        break;
                    }
                if (pending)
                    continue;
                std::vector<Value> args;
                for (const auto& arg : x.args)
                    args.push_back(ready(arg, frame.dx, frame.dy));
                out = numeric_operation(x.op, args);
            }
            memo.emplace(key, out);
            work.pop_back();
        }
        return memo.at(Key(v.expression.get(), dx, dy));
    }
    Vec vector(const Vec& v)
    {
        Vec out;
        for (int c = 0; c < 4; ++c)
            out[c] = value(v[c]);
        return out;
    }
    bool killed(const SymbolicMachine& m)
    {
        for (const auto& kill : *m.symbolic_kills)
        {
            bool active = true;
            for (const auto& p : kill.path)
                if (value(p.first).truth() != p.second)
                {
                    active = false;
                    break;
                }
            if (active && value(kill.condition).truth())
                return true;
        }
        return false;
    }
};
inline Value evaluate(const Value& v, const NumericEnv& env)
{
    Evaluation e(env);
    return e.value(v);
}
inline std::size_t size(const std::vector<Value>& values)
{
    std::set<const Expression*> seen;
    std::vector<Value> stack = values;
    while (!stack.empty())
    {
        auto v = stack.back();
        stack.pop_back();
        if (symbolic(v) && seen.insert(v.expression.get()).second)
            stack.insert(stack.end(), v.expression->args.begin(), v.expression->args.end());
    }
    return seen.size();
}
}
}

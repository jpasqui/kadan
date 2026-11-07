// Bind raw shader DAG inputs to recovered material inputs. Unsupported policies
// remain named; an unknown engine word is never replaced with an anonymous zero.
#pragma once
#include "material_symbolic.h"
#include "material_shader_map.h"
#include "material_preshader_symbolic.h"

namespace P4MapKit
{
namespace Material
{
struct BindingRefused : EvalError
{
    std::string name;
    BindingRefused(const std::string& reason, const std::string& category) : EvalError(reason), name(category)
    {
    }
};
inline Value source(SymbolicArena& a, const std::string& kind, const Value& detail = Value::null(), int component = 0)
{
    return a.mk("COMP", {a.mk("SRC", {kind, detail}), component});
}
inline bool is(const Value& v, const std::string& op)
{
    return symbolic(v) && v.expression->op == op;
}
inline Value simplify(SymbolicArena& a, const std::string& name, const std::vector<Value>& args)
{
    const auto number = [&](std::size_t i, double n)
    {
        const auto& v = args.at(i);
        return v.kind != Value::Expr && v.kind != Value::Tuple && v.kind != Value::Handle && v.kind != Value::Text &&
               v.kind != Value::Null && !has_nan(v) && compare_numbers(v, Value(n)) == 0;
    };
    if (name == "MUL")
    {
        if (number(0, 1))
            return args[1];
        if (number(1, 1))
            return args[0];
    }
    if (name == "ADD")
    {
        if (number(0, 0))
            return args[1];
        if (number(1, 0))
            return args[0];
    }
    if (name == "SUB" && number(1, 0))
        return args[0];
    if (name == "DIV" && number(1, 1))
        return args[0];
    if (name == "NEG" && is(args[0], "NEG"))
        return args[0].expression->args.at(0);
    return a.op(name, args);
}
struct CollectionBinding
{
    std::string Package;
    std::vector<std::string> Scalars, Vectors;
    bool Available = true;
};
struct BindingContext
{
    std::map<int, std::string> Buffers;
    std::map<std::string, int> Varyings;
    // Interpolant mappings are already source components/literals, supplied by
    // a checked vertex-factory contract. Empty uses the local packed layout.
    std::map<std::string, Vec> Interpolants;
    FMaterialUniformSet Uniforms;
    std::vector<CollectionBinding> Collections;
    std::map<int, std::string> TextureParameters;
    std::map<int, std::size_t> TextureEntries;
    std::set<int> Samplers;
    bool ViewSamplersKnown = false;
    bool AllowUnresolvedSyntheticSamplers = false;
    std::map<int, int> ViewSamplers;                   // shader sampler slot -> View resource 0/1
    std::map<int, std::string> MaterialSharedSamplers; // handle word -> sampler source
    int PrecomputedAOSlot = -1;
    // Extension contracts receive original nodes and must return false when
    // unrecognized. Their callers retain their independent source evidence.
    std::function<bool(const Value&, Value&)> LeafOverride, ComponentOverride, BoundOverride;
    std::function<bool(const Value&, const Vec&, Value&)> TextureOverride;
    std::function<Vec(const FMaterialPreshaderInstruction&)> RuntimePreshader;
};
class Binder
{
    std::map<const Expression*, Value> memo;
    std::map<const Expression*, Value> idioms;
    std::map<std::pair<char, std::size_t>, Vec> words;
    std::map<int, std::string> interpolant_names;
    std::map<std::string, Vec> interpolant_rows;
    Value s(const std::string& kind, const Value& detail = Value::null(), int c = 0)
    {
        return source(arena, kind, detail, c);
    }
    Value apply(const std::string& name, const std::vector<Value>& args)
    {
        return simplify(arena, name, args);
    }
    Value done(const Value& v) const
    {
        return symbolic(v) ? memo.at(v.expression.get()) : v;
    }
    bool nonnegative(const Value& v, std::map<const Expression*, bool>& facts) const
    {
        if (!symbolic(v))
            return (v.kind == Value::Float || v.kind == Value::Int) && std::isfinite(v.number()) && v.number() >= 0;
        auto found = facts.find(v.expression.get());
        if (found != facts.end())
            return found->second;
        const auto& x = *v.expression;
        bool result = false;
        if (x.op == "SAT" || x.op == "ABS" || x.op == "TRUNC.U" || x.op == "I2F.U" || x.op == "POW" ||
            starts(x.op, "F.S"))
            result = true;
        else if (x.op == "ADD" || x.op == "MIN")
            result = nonnegative(x.args.at(0), facts) && nonnegative(x.args.at(1), facts);
        else if (x.op == "MAX")
            result = nonnegative(x.args.at(0), facts) || nonnegative(x.args.at(1), facts);
        else if (x.op == "MUL")
            result = same(x.args.at(0), x.args.at(1)) ||
                     (nonnegative(x.args.at(0), facts) && nonnegative(x.args.at(1), facts));
        else if (x.op == "U.FLR" || x.op == "U.CEIL")
            result = nonnegative(x.args.at(0), facts);
        else if (x.op == "SEL")
            result = nonnegative(x.args.at(1), facts) && nonnegative(x.args.at(2), facts);
        facts[&x] = result;
        return result;
    }
    Value preshader_operation(EMaterialPreshaderOpcode op, const std::vector<Value>& args)
    {
        using O = EMaterialPreshaderOpcode;
        static const char* names[] = {"Nop",
                                      "ConstantZero",
                                      "Constant",
                                      "ScalarParameter",
                                      "VectorParameter",
                                      "Add",
                                      "Sub",
                                      "Mul",
                                      "Div",
                                      "Fmod",
                                      "Min",
                                      "Max",
                                      "Clamp",
                                      "Sin",
                                      "Cos",
                                      "Tan",
                                      "Asin",
                                      "Acos",
                                      "Atan",
                                      "Atan2",
                                      "Dot",
                                      "Cross",
                                      "Sqrt",
                                      "Length",
                                      "Saturate",
                                      "Abs",
                                      "Floor",
                                      "Ceil",
                                      "Round",
                                      "Trunc",
                                      "Sign",
                                      "Frac",
                                      "Fractional",
                                      "Log2",
                                      "Log10"};
        const char* plain = nullptr;
        switch (op)
        {
            case O::Add:
                plain = "ADD";
                break;
            case O::Sub:
                plain = "SUB";
                break;
            case O::Mul:
                plain = "MUL";
                break;
            case O::Min:
                plain = "MIN";
                break;
            case O::Max:
                plain = "MAX";
                break;
            case O::Saturate:
                plain = "SAT";
                break;
            case O::Abs:
                plain = "ABS";
                break;
            case O::Floor:
                plain = "U.FLR";
                break;
            case O::Ceil:
                plain = "U.CEIL";
                break;
            case O::Frac:
                plain = "U.FRC";
                break;
            case O::Sin:
                plain = "SIN";
                break;
            case O::Cos:
                plain = "COS";
                break;
            default:
                break;
        }
        bool symbolic_args = false;
        for (const auto& a : args)
            symbolic_args = symbolic_args || symbolic(a);
        if (op == O::Div)
        {
            const Value delta(double(float(0.00001)));
            const auto& r = args.at(1);
            auto sign =
                arena.select(arena.op("T.NE", {arena.op("F.SLT", {r, Value(0)})}), Value(-delta.number()), delta);
            auto safe = arena.select(arena.op("T.NE", {arena.op("F.SLT", {arena.op("ABS", {r}), delta})}), sign, r);
            return arena.op("DIV", {args.at(0), safe});
        }
        if (op == O::Sqrt && symbolic_args)
        {
            std::map<const Expression*, bool> facts;
            if (nonnegative(args[0], facts))
                return arena.op("RCP", {arena.op("RSQ", {args[0]})});
            return arena.mk("PRESHADER", {"Sqrt", args[0]});
        }
        if (symbolic_args)
        {
            if (plain)
                return apply(plain, args);
            std::vector<Value> named = {names[std::size_t(op)]};
            named.insert(named.end(), args.begin(), args.end());
            return arena.mk("PRESHADER", named);
        }
        if (plain)
            return numeric_operation(plain, args);
        if (op == O::Trunc)
            return numeric_operation("U.TRUNC", args);
        if (op == O::Round)
            return numeric_operation("U.FLR", {numeric_operation("ADD", {args[0], 0.5})});
        if (op == O::Fractional)
            return numeric_operation("SUB", {args[0], numeric_operation("U.TRUNC", {args[0]})});
        if (op == O::Sign)
        {
            double d = args[0].number();
            return d > 0 ? 1.0 : d < 0 ? -1.0 : 0.0;
        }
        if (op == O::Log2)
            return args[0].number() > 0 ? numeric_operation("LG2", args) : Value(-infinity());
        if (op == O::Sqrt)
            return numeric_operation("P.SQRT", args);
        if (op == O::Log10)
            return numeric_operation("P.LOG10", args);
        if (op == O::Fmod)
            return numeric_operation("P.FMOD", args);
        if (op == O::Atan2)
            return numeric_operation("P.ATAN2", args);
        if (op == O::Tan)
            return numeric_operation("P.TAN", args);
        if (op == O::Asin)
            return numeric_operation("P.ASIN", args);
        if (op == O::Acos)
            return numeric_operation("P.ACOS", args);
        if (op == O::Atan)
            return numeric_operation("P.ATAN", args);
        throw BindingRefused("unsupported symbolic preshader operation", "material preshader operation");
    }
    Vec preshader(const FMaterialPreshaderProgram& program)
    {
        using O = EMaterialPreshaderOpcode;
        std::vector<Vec> stack;
        const auto pop = [&]()
        {
            if (stack.empty())
                throw BindingRefused("preshader stack underflow", "material preshader input");
            Vec value = stack.back();
            stack.pop_back();
            return value;
        };
        for (const auto& i : program.Instructions)
        {
            const auto op = i.Opcode;
            int n = int(op);
            Vec result = splat(0.0);
            if (op == O::Nop)
                continue;
            if (op == O::ConstantZero)
            {
            }
            else if (op == O::Constant)
                for (int c = 0; c < 4; ++c)
                {
                    float value;
                    std::memcpy(&value, &i.ConstantBits[c], 4);
                    result[c] = double(value);
                }
            else if (op == O::ScalarParameter)
                result = splat(s("ScalarParameter", context.Uniforms.Scalars.at(i.Parameter).Name));
            else if (op == O::VectorParameter)
                for (int c = 0; c < 4; ++c)
                    result[c] = s("VectorParameter", context.Uniforms.Vectors.at(i.Parameter).Name, c);
            else if (n >= 37)
            {
                if (!context.RuntimePreshader)
                    throw BindingRefused("unbound dynamic material preshader input", "material preshader input");
                result = context.RuntimePreshader(i);
            }
            else if (op == O::Cross || op == O::Length)
                throw BindingRefused("preshader operation is not admitted: " +
                                         std::string(op == O::Cross ? "Cross" : "Length"),
                                     "material preshader operation");
            else if (op == O::Dot)
            {
                auto b = pop(), a = pop();
                int count = 1 + (i.ValueType >= 2) + (i.ValueType >= 4) + (i.ValueType >= 8);
                Value total = preshader_operation(O::Mul, {a[0], b[0]});
                for (int c = 1; c < count; ++c)
                    total = preshader_operation(O::Add, {total, preshader_operation(O::Mul, {a[c], b[c]})});
                result = splat(total);
            }
            else if (op == O::ComponentSwizzle)
            {
                auto a = pop();
                if (i.Components == 1)
                    result = splat(a.at(i.Swizzle[0]));
                else
                    for (int c = 0; c < i.Components; ++c)
                        result.at(c) = a.at(i.Swizzle[c]);
            }
            else if (op == O::AppendVector)
            {
                auto b = pop(), a = pop();
                for (int c = 0; c < 4; ++c)
                    result[c] = c < i.Components ? a[c] : b.at(c - i.Components);
            }
            else if (op == O::Clamp)
            {
                auto high = pop(), low = pop(), a = pop();
                for (int c = 0; c < 4; ++c)
                    result[c] = preshader_operation(O::Min, {preshader_operation(O::Max, {a[c], low[c]}), high[c]});
            }
            else if ((n >= 5 && n <= 11) || op == O::Atan2)
            {
                auto b = pop(), a = pop();
                for (int c = 0; c < 4; ++c)
                    result[c] = preshader_operation(op, {a[c], b[c]});
            }
            else
            {
                auto a = pop();
                for (int c = 0; c < 4; ++c)
                    result[c] = preshader_operation(op, {a[c]});
            }
            stack.push_back(result);
        }
        if (stack.size() > 1)
            throw BindingRefused("preshader result stack is ambiguous", "material preshader input");
        return stack.empty() ? splat(0.0) : stack.back();
    }
    void remember_idiom(const Value& from, const Value& to)
    {
        if (symbolic(from))
            idioms[from.expression.get()] = to;
    }
    std::vector<Value> homogeneous_rows(int c)
    {
        std::vector<Value> result;
        for (int mask = 0; mask < 8; ++mask)
        {
            Value terms[3];
            for (int r = 0; r < 3; ++r)
            {
                auto position = s("SvPosition", Value::null(), r),
                     matrix = s("View", "sv_to_translated_world", 4 * r + c);
                terms[r] = apply("MUL", (mask & (1 << r)) ? std::vector<Value>{matrix, position}
                                                          : std::vector<Value>{position, matrix});
            }
            result.push_back(apply("ADD", {s("View", "sv_to_translated_world", 12 + c),
                                           apply("ADD", {terms[2], apply("ADD", {terms[1], terms[0]})})}));
        }
        return result;
    }
    void build_idioms()
    {
        const auto denominators = homogeneous_rows(3);
        for (int c = 0; c < 3; ++c)
        {
            auto translated = s("TranslatedWorldPosition", Value::null(), c);
            for (const auto& numerator : homogeneous_rows(c))
                for (const auto& denominator : denominators)
                {
                    remember_idiom(apply("DIV", {numerator, denominator}), translated);
                    auto reciprocal = arena.mk("RCP", {denominator});
                    remember_idiom(apply("MUL", {numerator, reciprocal}), translated);
                    remember_idiom(apply("MUL", {reciprocal, numerator}), translated);
                }
            auto back = apply("NEG", {s("View", "pre_view_translation", c)});
            for (const auto& pair : std::vector<std::pair<std::string, std::string>>{
                     {"TranslatedWorldPosition", "WorldPosition"},
                     {"TranslatedWorldPositionNoOffsets", "WorldPositionNoOffsets"},
                     {"ParticleTranslatedPosition", "ParticlePositionWS"}})
            {
                auto input = s(pair.first, Value::null(), c), output = s(pair.second, Value::null(), c);
                remember_idiom(apply("ADD", {input, back}), output);
                remember_idiom(apply("ADD", {back, input}), output);
            }
        }
        Vec away = splat(0.0);
        Value length = 0;
        for (int c = 0; c < 3; ++c)
        {
            away[c] = apply("NEG", {s("TranslatedWorldPosition", Value::null(), c)});
            length = apply("ADD", {length, apply("MUL", {away[c], away[c]})});
        }
        auto inverse = arena.mk("RSQ", {length});
        for (int c = 0; c < 3; ++c)
            remember_idiom(apply("MUL", {inverse, away[c]}), s("CameraVector", Value::null(), c));
    }
    void default_interpolants()
    {
        for (int pair = 0; pair < 4; ++pair)
            interpolant_rows["in_texcoord" + std::to_string(pair)] = {
                {s("TexCoord", pair * 2, 0), s("TexCoord", pair * 2, 1), s("TexCoord", pair * 2 + 1, 0),
                 s("TexCoord", pair * 2 + 1, 1)}};
        Vec color;
        for (int c = 0; c < 4; ++c)
            color[c] = s("VertexColor", Value::null(), c);
        interpolant_rows["in_color0"] = color;
        interpolant_rows["in_texcoord10"] = {
            {s("TangentX", Value::null(), 0), s("TangentX", Value::null(), 1), s("TangentX", Value::null(), 2), 0.0}};
        interpolant_rows["in_texcoord11"] = {{s("TangentZ", Value::null(), 0), s("TangentZ", Value::null(), 1),
                                              s("TangentZ", Value::null(), 2), s("TangentSign")}};
        interpolant_rows["in_texcoord7"] = {{0.0, 0.0, 0.0, 1.0}};
        interpolant_rows["in_texcoord9"] = {{s("TranslatedWorldPositionNoOffsets", Value::null(), 0),
                                             s("TranslatedWorldPositionNoOffsets", Value::null(), 1),
                                             s("TranslatedWorldPositionNoOffsets", Value::null(), 2), 1.0}};
    }
    bool primitive_component(const Value& v, const std::string& name, int c) const
    {
        if (!is(v, "COMP") || v.expression->args.size() != 2 || symbolic(v.expression->args[1]) ||
            v.expression->args[1].number() != c)
            return false;
        auto inner = v.expression->args[0];
        return is(inner, "SRC") && inner.expression->args.size() == 2 &&
               inner.expression->args[0].text == "PrimitiveMatrix" && inner.expression->args[1].text == name;
    }
    Value primitive_transform(const Value& v)
    {
        if (!is(v, "ADD"))
            return v;
        for (int c = 0; c < 3; ++c)
            for (int turn = 0; turn < 2; ++turn)
            {
                const auto& a = v.expression->args;
                auto translation = a.at(turn), total = a.at(1 - turn);
                if (!primitive_component(translation, "WorldToLocal", 12 + c))
                    continue;
                std::vector<Value> parts;
                for (int r = 2; r >= 1; --r)
                {
                    if (!is(total, "ADD"))
                        break;
                    parts.push_back(total.expression->args.at(0));
                    total = total.expression->args.at(1);
                }
                if (parts.size() != 2)
                    continue;
                parts.push_back(total);
                std::map<int, Value> inputs;
                for (int i = 0; i < 3; ++i)
                {
                    auto product = parts[i];
                    if (!is(product, "MUL"))
                        break;
                    for (int side = 0; side < 2; ++side)
                        if (primitive_component(product.expression->args.at(side), "WorldToLocal", (2 - i) * 4 + c))
                        {
                            inputs[2 - i] = product.expression->args.at(1 - side);
                            break;
                        }
                }
                if (inputs.size() == 3)
                    return arena.mk("COMP",
                                    {arena.mk("WORLD_TO_LOCAL_POSITION", {inputs[0], inputs[1], inputs[2]}), c});
            }
        return v;
    }
    Value lower_index(const Value& v)
    {
        if (!symbolic(v))
            return v.as_int();
        const auto& x = *v.expression;
        auto truncate = [&](Value input, bool clamp, bool unsigned_value)
        {
            if (clamp)
                input =
                    apply("MIN", {apply("MAX", {input, unsigned_value ? Value(0) : Value(std::int64_t(-2147483648LL))}),
                                  unsigned_value ? Value(4294967295.0) : Value(2147483647)});
            if (is(input, "U.FLR"))
                return input;
            return arena.select(apply("T.NE", {apply("F.SGE", {input, 0})}), apply("U.FLR", {input}),
                                apply("U.CEIL", {input}));
        };
        if (x.op == "TOINT" || x.op == "TRUNC.S" || x.op == "TRUNC.U")
            return truncate(bind(x.args.at(0)), x.op != "TOINT", x.op == "TRUNC.U");
        if (x.op == "I.MOD")
        {
            auto a = lower_index(x.args.at(0)), b = lower_index(x.args.at(1));
            if (symbolic(b) || !b.truth())
                throw BindingRefused("a local array modulo has no constant nonzero divisor", "local array index");
            return apply("SUB", {a, apply("MUL", {truncate(apply("DIV", {a, b}), false, false), b})});
        }
        if (x.op == "I.MAD")
            return apply("ADD", {apply("MUL", {lower_index(x.args.at(0)), lower_index(x.args.at(1))}),
                                 lower_index(x.args.at(2))});
        throw BindingRefused("a local array integer operation is not lifted: " + x.op, "local array index");
    }
    Value viewport_pixels(const Value& v)
    {
        if (!symbolic(v))
            return v;
        // The selector address alone needs viewport-relative pixel positions;
        // ordinary SvPosition stays available to scene-depth reconstruction.
        std::map<const Expression*, Value> rewritten;
        std::vector<Value> stack = {v};
        while (!stack.empty())
        {
            auto x = stack.back();
            if (!symbolic(x))
            {
                stack.pop_back();
                continue;
            }
            if (rewritten.count(x.expression.get()))
            {
                stack.pop_back();
                continue;
            }
            bool pending = false;
            for (const auto& a : x.expression->args)
                if (symbolic(a) && !rewritten.count(a.expression.get()))
                {
                    stack.push_back(a);
                    pending = true;
                    break;
                }
            if (pending)
                continue;
            std::vector<Value> args;
            for (const auto& a : x.expression->args)
                args.push_back(symbolic(a) ? rewritten.at(a.expression.get()) : a);
            auto out = arena.mk(x.expression->op, args);
            for (int c = 0; c < 2; ++c)
                if (same(out, apply("ADD", {s("SvPosition", Value::null(), c),
                                            apply("NEG", {s("View", "view_rect_min", c)})})))
                    out = s("ViewportPixelPosition", Value::null(), c);
            rewritten[x.expression.get()] = out;
            stack.pop_back();
        }
        return rewritten.at(v.expression.get());
    }

public:
    SymbolicArena& arena;
    const BindingContext& context;
    std::map<std::pair<std::string, int>, std::string> assumed;
    std::set<std::string> shared_samplers;
    std::set<std::pair<std::string, int>> canonical_unit_fields;
    Binder(SymbolicArena& a, const BindingContext& c) : arena(a), context(c)
    {
        for (const auto& v : c.Varyings)
        {
            if (interpolant_names.count(v.second) && interpolant_names[v.second] != v.first)
                throw BindingRefused("interpolant binding has multiple names", "interpolant identity");
            interpolant_names[v.second] = v.first;
        }
        if (c.Interpolants.empty())
            default_interpolants();
        else
            interpolant_rows = c.Interpolants;
        build_idioms();
    }
    Value leaf(const Value& v)
    {
        Value override_value;
        if (context.LeafOverride && context.LeafOverride(v, override_value))
            return override_value;
        const auto& a = v.expression->args;
        std::string kind = a.at(0).text;
        int index = int(a.at(1).number()), k = int(a.at(2).number());
        if (kind == "position")
            return s("SvPosition", Value::null(), k);
        if (kind == "facing")
            return s("Facing");
        if (kind == "attrib")
        {
            auto it = interpolant_names.find(index);
            std::string name = it == interpolant_names.end() ? "interpolant " + std::to_string(index) : it->second;
            auto known = interpolant_rows.find(name);
            return known == interpolant_rows.end() ? s("Interpolant", name, k) : known->second.at(k);
        }
        if (kind != "buffer")
            throw BindingRefused("unknown shader input " + kind, "shader input");
        auto buffer = context.Buffers.find(index);
        std::string name = buffer == context.Buffers.end() ? "buffer " + std::to_string(index) : buffer->second;
        if (k < 0 || k % 4)
            throw BindingRefused("unaligned shader buffer word: " + name + " " + std::to_string(k),
                                 "buffer word alignment");
        if (name == "View")
        {
            if (k == 2180 || k == 2184 || k == 2428)
            {
                canonical_unit_fields.insert({name, k});
                return 1.0;
            }
            if (k == 2440)
                return s("Time");
            struct Field
            {
                const char* Name;
                int Start, Count;
            };
            for (const auto& f : std::vector<Field>{{"sv_to_translated_world", 704, 16},
                                                    {"world_camera_origin", 1072, 3},
                                                    {"pre_view_translation", 1120, 3},
                                                    {"view_rect_min", 2080, 2},
                                                    {"view_size", 2096, 4},
                                                    {"mip_bias", 2452, 1},
                                                    {"sun_direction", 2512, 3},
                                                    {"c4_sky_light_param", 2704, 4},
                                                    {"reflection_capture_mask", 3436, 1},
                                                    {"inv_device_z", 1040, 4},
                                                    {"screen_scale_bias", 1056, 4},
                                                    {"buffer_uv_min_max", 2144, 4},
                                                    {"atmosphere_light_direction", 3072, 3},
                                                    {"atmosphere_light_disc_luminance", 3168, 3},
                                                    {"atmosphere_light_disc_cos_half_apex", 3200, 1},
                                                    {"sky_world_camera_origin", 3248, 3},
                                                    {"sky_planet_center", 3264, 3},
                                                    {"sky_bottom_radius_km", 3368, 1},
                                                    {"sky_top_radius_km", 3372, 1}})
                if (k >= f.Start && k < f.Start + 4 * f.Count)
                    return s("View", f.Name, (k - f.Start) / 4);
            if (k == 3628)
            {
                assumed[{name, k}] = "the decal block: the view draws decals";
                return 0.0;
            }
        }
        else if (name == "Primitive")
        {
            for (const auto& m : std::vector<std::pair<const char*, int>>{{"LocalToWorld", 0},
                                                                          {"WorldToLocal", 96},
                                                                          {"PreviousLocalToWorld", 160},
                                                                          {"PreviousWorldToLocal", 224}})
                if (k >= m.second && k < m.second + 64)
                    return s("PrimitiveMatrix", m.first, (k - m.second) / 4);
            if (k == 76)
            {
                canonical_unit_fields.insert({name, k});
                return 1.0;
            }
            for (const auto& f : std::vector<std::tuple<const char*, int, int>>{{"object_position", 80, 3},
                                                                                {"object_radius", 92, 1},
                                                                                {"actor_position", 288, 3},
                                                                                {"object_orientation", 336, 3}})
                if (k >= std::get<1>(f) && k < std::get<1>(f) + 4 * std::get<2>(f))
                    return s("Primitive", std::get<0>(f), (k - std::get<1>(f)) / 4);
            if (k == 320 || k == 328)
            {
                assumed[{name, k}] =
                    k == 320 ? "the decal block: the primitive receives decals" : "the volumetric lightmap block";
                return 0.0;
            }
        }
        else if (name == "Material")
            return material_word(k);
        else if (starts(name, "MaterialCollection"))
        {
            auto suffix = name.substr(18);
            if (suffix.empty() || suffix.find_first_not_of("0123456789") != std::string::npos)
                throw BindingRefused("malformed collection buffer " + name, "parameter collection identity");
            return collection_word(std::size_t(std::stoull(suffix)), k);
        }
        throw BindingRefused("unmodeled engine input: " + name + " byte " + std::to_string(k),
                             "unmodeled engine input");
    }
    Value material_word(int offset)
    {
        if (context.Uniforms.VirtualStackCount)
            throw BindingRefused("the Material virtual texture uniform prefix requires a checked layout",
                                 "virtual texture Material buffer layout");
        for (const auto& texture : context.Uniforms.Textures)
            if (texture.TextureType == 4)
                throw BindingRefused("the Material virtual texture uniform prefix requires a checked layout",
                                     "virtual texture Material buffer layout");
        if (offset < 0 || offset % 4)
            throw BindingRefused("unaligned Material buffer word", "Material buffer word");
        auto vector_count = context.Uniforms.VectorPrograms.size();
        std::pair<char, std::size_t> key;
        int component = 0;
        if (std::size_t(offset) < 16 * vector_count)
        {
            key = {'v', std::size_t(offset) / 16};
            component = (offset % 16) / 4;
        }
        else
            key = {'s', (std::size_t(offset) - 16 * vector_count) / 4};
        auto found = words.find(key);
        if (found == words.end())
        {
            const auto& programs = key.first == 'v' ? context.Uniforms.VectorPrograms : context.Uniforms.ScalarPrograms;
            if (key.second >= programs.size())
                throw BindingRefused("the Material buffer has no value at byte " + std::to_string(offset),
                                     "a word of the Material buffer with no value");
            found = words.emplace(key, preshader(programs[key.second])).first;
        }
        return found->second.at(component);
    }
    Value collection_word(std::size_t index, int offset)
    {
        if (index >= context.Collections.size() || !context.Collections[index].Available)
            throw BindingRefused("parameter collection " + std::to_string(index) + " is not among the extracted files",
                                 "a parameter collection that is not among the extracted files");
        if (offset < 0 || offset % 4)
            throw BindingRefused("unaligned collection word", "parameter collection word");
        const auto& c = context.Collections[index];
        auto scalar_count = c.Scalars.size();
        if (std::size_t(offset) < 4 * scalar_count)
            return s("CollectionScalar", Value::tuple_value({c.Package, c.Scalars[std::size_t(offset) / 4]}));
        auto base = ((scalar_count + 3) / 4) * 16;
        if (std::size_t(offset) < base || (std::size_t(offset) - base) / 16 >= c.Vectors.size())
            throw BindingRefused("collection " + c.Package + " has no parameter at byte " + std::to_string(offset),
                                 "a word of a parameter collection with no parameter");
        return s("CollectionVector", Value::tuple_value({c.Package, c.Vectors[(std::size_t(offset) - base) / 16]}),
                 int((std::size_t(offset) - base) % 16) / 4);
    }
    std::string sampler_source(const std::string& parameter, const Value& sampler, const FMaterialUniformTexture* entry)
    {
        static const char* sources[] = {"SSM_FromTextureAsset", "SSM_Wrap_WorldGroupSettings",
                                        "SSM_Clamp_WorldGroupSettings"};
        int source_index = 0, slot = sampler.kind == Value::Null ? -1 : (int(sampler.number()) - 1352) / 8;
        auto material = context.MaterialSharedSamplers.find(sampler.kind == Value::Null ? -1 : int(sampler.number()));
        if (material != context.MaterialSharedSamplers.end())
        {
            bool found = false;
            for (int i = 0; i < 3; ++i)
                if (material->second == sources[i])
                {
                    source_index = i;
                    found = true;
                }
            if (!found)
                throw BindingRefused("unknown material sampler source: " + material->second, "sampler identity");
            shared_samplers.insert(parameter);
        }
        else if (slot < 0 || context.Samplers.count(slot))
            source_index = 0;
        else if (!context.ViewSamplersKnown)
        {
            if (!context.AllowUnresolvedSyntheticSamplers)
                return "?";
            shared_samplers.insert(parameter);
            return "";
        }
        else
        {
            auto shared = context.ViewSamplers.find(slot);
            if (shared == context.ViewSamplers.end() || (shared->second != 0 && shared->second != 1))
                return "?";
            source_index = 1 + shared->second;
            shared_samplers.insert(parameter);
        }
        int said = entry ? entry->SamplerSource : 0;
        if (said != 0 && said != source_index)
            throw BindingRefused("sampler source disagreement for " + parameter, "settings disagree: sampler source");
        return std::string("/") + sources[source_index];
    }
    Value texture(const Value& v)
    {
        const auto& a = v.expression->args;
        Vec coordinates;
        for (int c = 0; c < 4; ++c)
            coordinates[c] = done(a.at(c + 4));
        Value output;
        if (context.TextureOverride && context.TextureOverride(v, coordinates, output))
            return output;
        std::string name = a.at(0).text, target = a.at(1).text;
        int slot = a.at(2).kind == Value::Null ? -1 : (int(a.at(2).number()) - 328) / 8;
        if (slot >= 0 && slot == context.PrecomputedAOSlot)
        {
            if (target != "2D" || name != "TEX")
                throw BindingRefused("a precomputed AO read that is not the stock 2D sample", "precomputed AO inputs");
            for (int c = 0; c < 2; ++c)
            {
                const auto& uv = a.at(c + 4);
                if (!is(uv, "IN") || uv.expression->args.at(0).text != "attrib" ||
                    uv.expression->args.at(2).number() != c)
                    throw BindingRefused("precomputed AO uses coordinates other than the lightmap varying",
                                         "precomputed AO inputs");
                auto at = interpolant_names.find(int(uv.expression->args.at(1).number()));
                if (at == interpolant_names.end() || at->second != "in_texcoord4")
                    throw BindingRefused("precomputed AO uses coordinates other than the lightmap varying",
                                         "precomputed AO inputs");
            }
            return arena.mk("PRECOMPUTED_AO_SAMPLE", std::vector<Value>(coordinates.begin(), coordinates.end()));
        }
        auto parameter = context.TextureParameters.find(slot);
        auto engine = [&](const std::string& why, const std::string& detail)
        {
            std::vector<Value> args = {why, detail};
            args.insert(args.end(), coordinates.begin(), coordinates.end());
            return arena.mk("ENGINE", args);
        };
        if (parameter == context.TextureParameters.end())
        {
            if (target == "2D" && (name == "TEX" || name == "TXL"))
                return arena.mk("SCENETEX", {slot < 0 ? Value::null() : Value(slot), coordinates[0], coordinates[1]});
            return engine("a texture that is not the material's",
                          "slot " + (slot < 0 ? std::string("None") : std::to_string(slot)));
        }
        auto texture_entry = context.TextureEntries.find(slot);
        const FMaterialUniformTexture* entry =
            texture_entry == context.TextureEntries.end() || texture_entry->second >= context.Uniforms.Textures.size()
                ? nullptr
                : &context.Uniforms.Textures[texture_entry->second];
        if (entry && (parameter->second == "?" || parameter->second == "None" || parameter->second.empty()))
            return engine("a texture that is not a parameter",
                          "referenced texture " + std::to_string(entry->TextureIndex));
        auto sampler = sampler_source(parameter->second, a.at(3), entry);
        if (sampler == "?")
            return engine("a sampler that is neither the texture's own nor one of the two the view shares",
                          parameter->second);
        if (target == "CUBE" && name == "TEX" && entry && entry->TextureType == 1 && parameter->second != "?" &&
            parameter->second != "None" && !parameter->second.empty())
            return arena.mk("CUBE_SAMPLE",
                            {parameter->second, sampler, coordinates[0], coordinates[1], coordinates[2]});
        if (target != "2D")
            return engine("a " + target + " texture read", name + " of " + parameter->second);
        const auto bias = s("View", "mip_bias");
        if (name == "TEX" || (name == "TXB" && same(coordinates[3], bias)))
            return arena.mk("SAMPLE", {parameter->second, "auto" + sampler, coordinates[0], coordinates[1]});
        if (name == "TXB" && !symbolic(coordinates[3]) && !coordinates[3].truth())
            return arena.mk("SAMPLE", {parameter->second, "plain" + sampler, coordinates[0], coordinates[1]});
        if (name == "TXB" && is(coordinates[3], "ADD"))
        {
            const auto& args = coordinates[3].expression->args;
            for (int i = 0; i < 2; ++i)
                if (same(args.at(i), bias))
                    return arena.mk("SAMPLE", {parameter->second, "bias" + sampler, coordinates[0], coordinates[1],
                                               args.at(1 - i)});
        }
        // A sample in mip bias mode with the view's bias turned off reads with its own bias alone.
        if (name == "TXB" && !value_uses(coordinates[3], bias))
            return arena.mk("SAMPLE",
                            {parameter->second, "plainbias" + sampler, coordinates[0], coordinates[1], coordinates[3]});
        if (name == "TXL")
        {
            if (sampler == "/SSM_FromTextureAsset" && is(coordinates[3], "ADD"))
            {
                const auto& args = coordinates[3].expression->args;
                for (int i = 0; i < 2; ++i)
                    if (same(args.at(i), bias))
                        return arena.mk("SAMPLE", {parameter->second, "levelauto" + sampler, coordinates[0],
                                                   coordinates[1], args.at(1 - i)});
            }
            return arena.mk("SAMPLE",
                            {parameter->second, "level" + sampler, coordinates[0], coordinates[1], coordinates[3]});
        }
        return engine("a " + name + " texture read",
                      parameter->second + (name == "TXB" ? " with bias " + value_text(coordinates[3]) : std::string()));
    }
    Value bind(const Value& v)
    {
        if (!symbolic(v))
            return v;
        std::vector<Value> work = {v};
        while (!work.empty())
        {
            const auto current = work.back();
            const auto& x = *current.expression;
            if (memo.count(&x))
            {
                work.pop_back();
                continue;
            }
            Value out;
            if (context.ComponentOverride && context.ComponentOverride(current, out))
            {
                memo[&x] = out;
                work.pop_back();
                continue;
            }
            if (x.op == "IN")
                out = leaf(current);
            else if (x.op == "ARRAYINDEX")
                out = viewport_pixels(lower_index(x.args.at(0)));
            else
            {
                bool pending = false;
                auto need = [&](const Value& a)
                {
                    if (symbolic(a) && !memo.count(a.expression.get()))
                    {
                        work.push_back(a);
                        return true;
                    }
                    return false;
                };
                if (x.op == "SEL")
                {
                    if (need(x.args.at(0)))
                        continue;
                    auto test = done(x.args[0]);
                    if (!symbolic(test))
                    {
                        const auto& selected = x.args.at(test.truth() ? 1 : 2);
                        if (need(selected))
                            continue;
                        out = done(selected);
                    }
                    else
                    {
                        if (need(x.args.at(1)) || need(x.args.at(2)))
                            continue;
                        out = arena.select(test, done(x.args[1]), done(x.args[2]));
                    }
                }
                else
                {
                    std::size_t first = x.op == "TEX" ? 4 : 0;
                    for (std::size_t i = first; i < x.args.size(); ++i)
                        if (need(x.args[i]))
                        {
                            pending = true;
                            break;
                        }
                    if (pending)
                        continue;
                    if (x.op == "TEX")
                        out = texture(current);
                    else if (x.op == "COMP")
                        out = arena.mk("COMP", {done(x.args.at(0)), x.args.at(1)});
                    else if (x.op == "DDX" || x.op == "DDY")
                    {
                        auto input = done(x.args.at(0));
                        out = symbolic(input) ? arena.mk(x.op, {input}) : Value(0.0);
                    }
                    else
                    {
                        std::vector<Value> args;
                        for (const auto& a : x.args)
                            args.push_back(done(a));
                        if (x.op == "MUL" && same(args.at(0), args.at(1)) && is(args[0], "COMP") &&
                            args[0].expression->args.at(1).number() == 0 &&
                            is(args[0].expression->args.at(0), "PRECOMPUTED_AO_SAMPLE"))
                            out = s("PrecomputedAOMask");
                        else
                            out = apply(x.op, args);
                        if (symbolic(out))
                        {
                            auto matched = idioms.find(out.expression.get());
                            if (matched != idioms.end())
                                out = matched->second;
                        }
                    }
                }
            }
            out = primitive_transform(out);
            Value boundOverride;
            if (context.BoundOverride && context.BoundOverride(out, boundOverride))
                out = boundOverride;
            memo[&x] = out;
            work.pop_back();
        }
        return memo.at(v.expression.get());
    }
    Vec bind(const Vec& values)
    {
        Vec out;
        for (int c = 0; c < 4; ++c)
            out[c] = bind(values[c]);
        return out;
    }
};
}
}

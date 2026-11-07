// Formula-to-expression graph stage of material_decompile.py. Unsupported
// policy formulas retain named refusals; this does not qualify renderer execution.
#pragma once
#include "material_graph.h"
#include "material_symbolic.h"
#include "material_facing.h"
#include "material_post_nodes.h"
#include <cstring>
#include <unordered_map>
#include <tuple>

namespace P4MapKit
{
namespace Material
{
inline bool IsExpr(const Value& v, const char* name)
{
    return v.kind == Value::Expr && v.expression && v.expression->op == name;
}
inline bool IsNumber(const Value& v)
{
    return v.kind == Value::Float || v.kind == Value::Int || v.kind == Value::Boolean;
}
inline const std::vector<Value>& Args(const Value& v)
{
    if (v.kind != Value::Expr || !v.expression)
        throw Refused("an expression was required");
    return v.expression->args;
}
inline std::string ValueKey(const Value& v)
{
    return value_key(v);
}
inline bool Same(const Value& a, const Value& b)
{
    return ValueKey(a) == ValueKey(b);
}
inline bool NumberIs(const Value& v, double n)
{
    return IsNumber(v) && v.number() == n;
}
struct FormulaPart
{
    Value source;
    int component = 0;
    bool normal = false;
    bool valid = false;
};
struct IntegerInterval
{
    bool valid = false;
    double lo = 0, hi = 0;
};
struct Lifter
{
    SymbolicArena& arena;
    Json defaults;
    Json nodes = Json::MakeObject();
    std::map<std::string, std::string> by_key;
    std::map<std::string, std::size_t> count;
    std::unordered_map<std::string, Json> memo;
    std::map<const Expression*, Value> viewport_memo;
    bool resolving = false;
    struct NeedLift
    {
        std::vector<Value> values;
    };
    std::set<std::size_t> normal_samples;
    std::set<std::string> unknown_samplers;
    explicit Lifter(SymbolicArena& a, const Json& d = Json::MakeObject()) : arena(a), defaults(d)
    {
    }
    std::string node(const std::string& kind, const Json& inputs = Json::MakeObject(),
                     const Json& props = Json::MakeObject())
    {
        auto key = Array({kind, inputs, props}).Dump();
        auto it = by_key.find(key);
        if (it != by_key.end())
            return it->second;
        auto id = kind + "_" + std::to_string(count[kind]++);
        nodes[id] = Object({{"type", kind}, {"inputs", inputs}, {"props", props}});
        by_key[key] = id;
        return id;
    }
    Json link(const std::string& id, const std::string& mask = "")
    {
        return Array({id, mask});
    }
    Value operationValue(const std::string& name, const std::vector<Value>& args)
    {
        return arena.op(name, args);
    }
    Value simp(const std::string& name, const std::vector<Value>& a)
    {
        if (name == "MUL")
        {
            if (NumberIs(a[0], 1))
                return a[1];
            if (NumberIs(a[1], 1))
                return a[0];
        }
        if (name == "ADD")
        {
            if (NumberIs(a[0], 0))
                return a[1];
            if (NumberIs(a[1], 0))
                return a[0];
        }
        if (name == "SUB" && NumberIs(a[1], 0))
            return a[0];
        if (name == "DIV" && NumberIs(a[1], 1))
            return a[0];
        if (name == "NEG" && IsExpr(a[0], "NEG"))
            return Args(a[0])[0];
        return arena.op(name, a);
    }
    Value src(const std::string& kind, const Value& detail, int c = 0)
    {
        return arena.mk("COMP", {arena.mk("SRC", {Value(kind), detail}), Value(c)});
    }
    bool DetailNone(const Value& v) const
    {
        return !IsNumber(v) && v.kind != Value::Text && v.kind != Value::Expr;
    }
    const Json* Default(const std::string& kind, const std::string& name) const
    {
        if (!defaults.Has(kind))
            return nullptr;
        const auto& group = defaults.At(kind);
        auto i = group.object.find(name);
        return i == group.object.end() ? nullptr : &i->second;
    }
    std::pair<std::string, int> source(const Value& x, bool normal = false);
    FormulaPart unpacked(const Value& x);
    FormulaPart part(const Value& x);
    Json lift(const std::vector<Value>& values);
    std::vector<Value> pixelValues(const std::vector<Value>& values);
    Json liftInternal(const std::vector<Value>& original);
    Json transformInput(const std::vector<Value>& values);
    Json particleLift(const std::vector<Value>& values);
    Json offsetScene(const Value& x);
    Json primitiveLift(const std::vector<Value>& values);
    bool whole(const std::vector<Value>& values);
    Json dot(const std::vector<std::vector<Value>>& columns);
    Json lerp(const std::vector<std::vector<Value>>& columns);
    Json operation(const std::string& name, const std::vector<std::vector<Value>>& columns);
    IntegerInterval integerBounds(const Value& value);
    bool floatInteger(const Value& value, Value& result);
    bool comparison(const Value& test, const std::string& name, std::vector<Value>& pair);
    bool booleanMask(const Value& value);
    Json choice(const Value& test, const std::vector<Value>& yes, const std::vector<Value>& no);
};
}
} // P4MapKit::Material
#include "material_lifter_sources.h"
#include "material_lifter_lift.h"
#include "material_lifter_operations.h"

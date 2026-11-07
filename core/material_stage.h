// Bounded intermediate expression file. It records a stage boundary, never proof.
#pragma once
#include "material_lifter.h"
namespace P4MapKit
{
namespace Material
{
inline Value StageAtom(const Json& json, const std::map<std::string, Value>& values, std::size_t depth = 0)
{
    if (depth > 128)
        throw Refused("expression argument nesting limit", "expression depth limit");
    switch (json.kind)
    {
        case Json::Null:
            return Value::null();
        case Json::Boolean:
            return Value(json.boolean);
        case Json::Number:
            return Value(json.number);
        case Json::String:
            return Value(json.string);
        case Json::Array:
        {
            std::vector<Value> a;
            for (const auto& item : json.array)
                a.push_back(StageAtom(item, values, depth + 1));
            return Value::tuple_value(a);
        }
        case Json::Object:
        {
            if (json.object.size() == 1 && json.Has("ref"))
            {
                const auto id = json.At("ref").Text();
                auto i = values.find(id);
                if (i == values.end())
                    throw Refused("forward or missing expression " + id, "expression reference");
                return i->second;
            }
            if (json.object.size() == 1 && json.Has("float_bits"))
            {
                const auto text = json.At("float_bits").Text();
                if (text.size() != 16)
                    throw Refused("float_bits requires sixteen hexadecimal digits", "float literal");
                std::uint64_t bits = 0;
                for (const auto c : text)
                {
                    int digit = c >= '0' && c <= '9'   ? c - '0'
                                : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                       : -1;
                    if (digit < 0)
                        throw Refused("invalid float_bits hexadecimal digit", "float literal");
                    bits = (bits << 4) | std::uint64_t(digit);
                }
                double value;
                std::memcpy(&value, &bits, 8);
                return Value(value);
            }
            if (json.object.size() == 1 && json.Has("integer"))
            {
                const auto text = json.At("integer").Text();
                if (text.empty() || text.size() > 4096)
                    throw Refused("invalid integer literal", "integer literal");
                bool negative = text[0] == '-';
                Integer n;
                std::size_t i = negative ? 1 : 0;
                if (i == text.size())
                    throw Refused("invalid integer literal", "integer literal");
                for (; i < text.size(); ++i)
                {
                    if (text[i] < '0' || text[i] > '9')
                        throw Refused("invalid integer literal", "integer literal");
                    n = n * Integer(10) + Integer(text[i] - '0');
                }
                return Value(negative ? n.negated() : n);
            }
            throw Refused("unknown expression atom", "expression atom");
        }
    }
    throw Refused("unknown expression atom", "expression atom");
}
inline Json LiftStage(const Json& stage)
{
    const auto stageName = stage.At("stage").Text();
    if (stage.At("stage_format").Numeric() != 1 ||
        (stageName != "bound-material-formulas" && stageName != "bound-material-requests"))
        throw Refused("unknown material stage format", "material stage format");
    const auto& formulas = stage.At("formulas");
    RequireKind(formulas, Json::Array, "formulas");
    if (formulas.array.size() > 1000000)
        throw Refused("expression node limit", "expression node limit");
    SymbolicArena arena;
    std::map<std::string, Value> values;
    for (const auto& formula : formulas.array)
    {
        const auto id = formula.At("id").Text(), op = formula.At("op").Text();
        RequireKind(formula.At("args"), Json::Array, id + ".args");
        if (id.empty() || op.empty() || values.count(id))
            throw Refused("duplicate/empty expression ID or operation", "expression ID");
        std::vector<Value> a;
        for (const auto& arg : formula.At("args").array)
            a.push_back(StageAtom(arg, values));
        const auto n = a.size();
        if ((op == "SRC" && n != 2) || (op == "COMP" && n != 2) || (op == "SEL" && n != 3) ||
            (op == "SCENE" && n != 3) || (op == "SAMPLE" && (n < 4 || n > 5)) || (op == "TOWORLD" && n != 3) ||
            (op == "TOTANGENT" && n != 3))
            throw Refused("invalid expression arity for " + op, "expression arity");
        const std::set<std::string> unary = {"NEG",   "ABS",   "SAT",     "RCP",     "RSQ",    "SIN",     "COS",
                                             "EX2",   "LG2",   "U.FRC",   "U.FLR",   "U.CEIL", "U.ROUND", "U.TRUNC",
                                             "I2F.S", "I2F.U", "TRUNC.S", "TRUNC.U", "TOINT",  "DDX",     "DDY",
                                             "T.NE",  "T.EQ",  "T.GT",    "T.GE",    "T.LT",   "T.LE"};
        const std::set<std::string> binary = {"ADD",   "SUB",   "MUL",   "DIV",   "MIN",   "MAX",  "POW",
                                              "F.SGE", "F.SLE", "F.SGT", "F.SLT", "F.SEQ", "F.SNE"};
        if ((unary.count(op) && n != 1) || (binary.count(op) && n != 2))
            throw Refused("invalid expression arity for " + op, "expression arity");
        values.emplace(id, arena.mk(op, a));
    }
    Lifter lifter(arena, stage.Has("defaults") ? stage.At("defaults") : Json::MakeObject());
    if (stage.Has("normal_samples"))
    {
        RequireKind(stage.At("normal_samples"), Json::Array, "normal_samples");
        for (const auto& id : stage.At("normal_samples").array)
        {
            auto i = values.find(id.Text());
            if (i == values.end() || !IsExpr(i->second, "SAMPLE"))
                throw Refused("normal sample ID is not a sample", "normal sample");
            lifter.normal_samples.insert(i->second.expression->id);
        }
    }
    if (stageName == "bound-material-requests")
    {
        Json links = Json::MakeArray();
        const auto& requests = stage.At("requests");
        RequireKind(requests, Json::Array, "requests");
        for (const auto& request : requests.array)
        {
            RequireKind(request, Json::Array, "request");
            std::vector<Value> a;
            for (const auto& value : request.array)
                a.push_back(StageAtom(value, values));
            links.array.push_back(lifter.lift(a));
        }
        return Object({{"nodes", lifter.nodes}, {"links", links}});
    }
    Json outputs = Json::MakeObject();
    const auto& pins = stage.At("outputs");
    RequireKind(pins, Json::Array, "outputs");
    for (const auto& pin : pins.array)
    {
        const auto name = pin.At("pin").Text();
        if (outputs.Has(name))
            throw Refused("duplicate material pin " + name, "material pin");
        RequireKind(pin.At("values"), Json::Array, name + ".values");
        std::vector<Value> a;
        for (const auto& value : pin.At("values").array)
            a.push_back(StageAtom(value, values));
        outputs[name] = lifter.lift(a);
    }
    Json graph = stage.At("settings");
    RequireKind(graph, Json::Object, "settings");
    graph["format"] = 2;
    graph["nodes"] = lifter.nodes;
    graph["outputs"] = outputs;
    ValidateGraph(graph);
    return graph;
}
}
} // P4MapKit::Material

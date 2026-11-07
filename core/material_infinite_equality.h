// Typed F.SEQ/F.SNE representation for one exact infinite literal. An integer
// word test avoids UE If's abs(A-B) tolerance and carries no nonfinite property.
// Call only at the corresponding recovered native comparison; renderer proof
// and full original-program qualification are separate.
#pragma once
#include "material_graph.h"
#include "material_symbolic.h"
namespace P4MapKit
{
namespace Material
{
inline bool MaterialInfiniteLiteral(const Value& value)
{
    return (value.kind == Value::Float || value.kind == Value::Int) && std::isinf(value.number());
}
template <class Lift>
inline bool LiftOriginalInfiniteEquality(Lift& lifter, const std::string& kind, const std::vector<Value>& pair,
                                         const std::vector<Value>& yes, const std::vector<Value>& no, Json& output)
{
    if ((kind != "F.SEQ" && kind != "F.SNE") || pair.size() != 2)
        return false;
    const bool left = MaterialInfiniteLiteral(pair[0]), right = MaterialInfiniteLiteral(pair[1]);
    if (left == right)
        return false;
    const auto& literal = pair[left ? 0 : 1];
    const auto& input = pair[left ? 1 : 0];
    if (input.kind != Value::Expr || !input.expression || yes.empty() || yes.size() > 4 || yes.size() != no.size())
        throw Refused("infinite equality requires one native scalar expression and equal bounded branch widths",
                      "material infinite equality shape");
    const auto word = std::signbit(literal.number()) ? "0xff800000u" : "0x7f800000u";
    const auto inputLink = lifter.lift({input}), equal = lifter.lift(kind == "F.SEQ" ? yes : no),
               differ = lifter.lift(kind == "F.SEQ" ? no : yes);
    const auto inputs = Object({{"Value", inputLink}, {"Equal", equal}, {"Unequal", differ}});
    const auto names =
        Array({Object({{"InputName", "Value"}}), Object({{"InputName", "Equal"}}), Object({{"InputName", "Unequal"}})});
    const auto code = std::string("return asuint(Value) == ") + word + " ? Equal : Unequal;";
    const auto outputType = std::string("CMOT_Float") + std::to_string(yes.size());
    output = lifter.link(lifter.node("Custom", inputs,
                                     Object({{"Code", code},
                                             {"Inputs", names},
                                             {"OutputType", outputType},
                                             {"Description", "Original ordered infinite equality: " + kind}})));
    return true;
}
}
}

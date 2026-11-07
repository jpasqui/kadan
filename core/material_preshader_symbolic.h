#pragma once
#include "material_preshader_eval.h"
#include "material_symbolic.h"

namespace P4MapKit
{
inline FMaterialPreshaderAlgebra<Material::Value>
MaterialSymbolicPreshaderAlgebra(Material::SymbolicArena& Arena, const std::vector<std::string>& ScalarNames,
                                 const std::vector<std::string>& VectorNames, bool SourceCrossCoercion = false)
{
    using V = Material::Value;
    using O = EMaterialPreshaderOpcode;
    FMaterialPreshaderAlgebra<V> A;
    A.CoerceCross = SourceCrossCoercion;
    A.Constant = [](std::uint32_t Bits)
    {
        float V;
        std::memcpy(&V, &Bits, 4);
        return Material::Value(double(V));
    };
    A.Scalar = [&Arena, ScalarNames](std::uint16_t I)
    {
        return Arena.mk("COMP", {Arena.mk("SRC", {V("ScalarParameter"), V(ScalarNames.at(I))}), V(0)});
    };
    A.Vector = [&Arena, VectorNames](std::uint16_t I)
    {
        const auto Source = Arena.mk("SRC", {V("VectorParameter"), V(VectorNames.at(I))});
        Material::Vec Out;
        for (int C = 0; C < 4; ++C)
            Out[C] = Arena.mk("COMP", {Source, V(C)});
        return Out;
    };
    A.Clamp = [&Arena](const V& Value, const V& Low, const V& High)
    {
        return Arena.op("MIN", {Arena.op("MAX", {Value, Low}), High});
    };
    A.Binary = [&Arena](O Op, const V& L, const V& R)
    {
        if (Op == O::Div)
        {
            const V Delta(double(float(0.00001)));
            const auto Sign =
                Arena.select(Arena.op("T.NE", {Arena.op("F.SLT", {R, V(0.0)})}), V(-Delta.number()), Delta);
            const auto Safe =
                Arena.select(Arena.op("T.NE", {Arena.op("F.SLT", {Arena.op("ABS", {R}), Delta})}), Sign, R);
            return Arena.op("DIV", {L, Safe});
        }
        const char* Name = nullptr;
        switch (Op)
        {
            case O::Add:
                Name = "ADD";
                break;
            case O::Sub:
                Name = "SUB";
                break;
            case O::Mul:
                Name = "MUL";
                break;
            case O::Min:
                Name = "MIN";
                break;
            case O::Max:
                Name = "MAX";
                break;
            case O::Fmod:
                Name = "P.FMOD";
                break;
            case O::Atan2:
                Name = "P.ATAN2";
                break;
            default:
                throw Material::EvalError("preshader-binary-operation");
        }
        return Arena.op(Name, {L, R});
    };
    A.Unary = [&Arena](O Op, const V& Value)
    {
        const char* Name = nullptr;
        switch (Op)
        {
            case O::Sin:
                Name = "SIN";
                break;
            case O::Cos:
                Name = "COS";
                break;
            case O::Tan:
                Name = "P.TAN";
                break;
            case O::Asin:
                Name = "P.ASIN";
                break;
            case O::Acos:
                Name = "P.ACOS";
                break;
            case O::Atan:
                Name = "P.ATAN";
                break;
            case O::Sqrt:
                Name = "P.SQRT";
                break;
            case O::Abs:
                Name = "ABS";
                break;
            case O::Saturate:
                Name = "SAT";
                break;
            case O::Floor:
                Name = "U.FLR";
                break;
            case O::Ceil:
                Name = "U.CEIL";
                break;
            case O::Trunc:
                Name = "U.TRUNC";
                break;
            case O::Sign:
                Name = "U.SSG";
                break;
            case O::Frac:
                Name = "U.FRC";
                break;
            case O::Log2:
                Name = "LG2";
                break;
            case O::Log10:
                Name = "P.LOG10";
                break;
            case O::Round:
                return Arena.op("U.FLR", {Arena.op("ADD", {Value, V(0.5)})});
            case O::Fractional:
                return Arena.op("SUB", {Value, Arena.op("U.TRUNC", {Value})});
            default:
                throw Material::EvalError("preshader-unary-operation");
        }
        return Arena.op(Name, {Value});
    };
    return A;
}
}

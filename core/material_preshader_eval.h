// Shared execution order for numeric and symbolic preshader algebras.
#pragma once
#include "material_preshader.h"
#include <cmath>
#include <functional>
#include <stdexcept>

namespace P4MapKit
{
template <class T>
struct FMaterialPreshaderAlgebra
{
    std::function<T(std::uint32_t)> Constant;
    std::function<T(std::uint16_t)> Scalar;
    std::function<std::array<T, 4>(std::uint16_t)> Vector;
    std::function<std::array<T, 4>(const FMaterialPreshaderInstruction&)> Runtime;
    std::function<T(EMaterialPreshaderOpcode, const T&)> Unary;
    std::function<T(EMaterialPreshaderOpcode, const T&, const T&)> Binary;
    std::function<T(const T&, const T&, const T&)> Clamp;
    // The legacy Python oracle omits cross coercion. Source mode retains it.
    bool CoerceCross = true;
};
template <class T>
inline bool EvaluateMaterialPreshader(const FMaterialPreshaderProgram& Program, const FMaterialPreshaderAlgebra<T>& A,
                                      std::array<T, 4>& Out, FReadError& Error)
{
    Error = {};
    using Op = EMaterialPreshaderOpcode;
    using Vec = std::array<T, 4>;
    if (!A.Constant || !A.Unary || !A.Binary || !A.Clamp)
    {
        Error = {0, "preshader-algebra-missing"};
        return false;
    }
    const auto Zero = A.Constant(0);
    const auto Splat = [&](const T& V)
    {
        return Vec{{V, V, V, V}};
    };
    std::vector<Vec> Stack;
    Stack.reserve(Program.PeakStack);
    const auto Pop = [&]()
    {
        auto V = Stack.back();
        Stack.pop_back();
        return V;
    };
    for (const auto& I : Program.Instructions)
    {
        const auto N = std::uint8_t(I.Opcode);
        const auto O = I.Opcode;
        if (N == 0)
            continue;
        if (N == 1)
        {
            Stack.push_back(Splat(Zero));
            continue;
        }
        if (N == 2)
        {
            Vec V;
            for (int C = 0; C < 4; ++C)
                V[C] = A.Constant(I.ConstantBits[C]);
            Stack.push_back(V);
            continue;
        }
        if (N == 3)
        {
            if (!A.Scalar)
            {
                Error = {I.Offset, "preshader-scalar-unbound"};
                return false;
            }
            Stack.push_back(Splat(A.Scalar(I.Parameter)));
            continue;
        }
        if (N == 4)
        {
            if (!A.Vector)
            {
                Error = {I.Offset, "preshader-vector-unbound"};
                return false;
            }
            Stack.push_back(A.Vector(I.Parameter));
            continue;
        }
        if (N >= 37)
        {
            if (!A.Runtime)
            {
                Error = {I.Offset, "preshader-runtime-unbound"};
                return false;
            }
            Stack.push_back(A.Runtime(I));
            continue;
        }
        std::size_t Need = N == 12 ? 3 : (N >= 5 && N <= 11) || N == 19 || N == 20 || N == 21 || N == 36 ? 2 : 1;
        if (Stack.size() < Need)
        {
            Error = {I.Offset, "preshader-stack-underflow"};
            return false;
        }
        Vec V;
        if (N == 12)
        {
            auto C = Pop(), B = Pop(), L = Pop();
            for (int K = 0; K < 4; ++K)
                V[K] = A.Clamp(L[K], B[K], C[K]);
        }
        else if (N == 35)
        {
            auto L = Pop();
            V = Splat(Zero);
            if (I.Components == 1)
                V = Splat(L[I.Swizzle[0]]);
            else
                for (std::size_t K = 0; K < I.Components; ++K)
                    V[K] = L[I.Swizzle[K]];
        }
        else if (N == 36)
        {
            auto B = Pop(), L = Pop();
            for (std::size_t K = 0; K < 4; ++K)
                V[K] = K < I.Components ? L[K] : B[K - I.Components];
        }
        else if (N == 20 || N == 23)
        {
            auto B = Pop(), L = N == 20 ? Pop() : B;
            T Sum = Zero;
            const auto Count = 1 + (I.ValueType >= 2) + (I.ValueType >= 4) + (I.ValueType >= 8);
            for (int K = 0; K < Count; ++K)
                Sum = A.Binary(Op::Add, Sum, A.Binary(Op::Mul, L[K], B[K]));
            V = Splat(N == 23 ? A.Unary(Op::Sqrt, Sum) : Sum);
        }
        else if (N == 21)
        {
            auto B = Pop(), L = Pop();
            if (A.CoerceCross)
            {
                if (I.ValueType == 15)
                {
                    L[1] = L[2] = L[0];
                    B[1] = B[2] = B[0];
                }
                else if (I.ValueType == 1)
                {
                    L[1] = L[2] = B[1] = B[2] = Zero;
                }
                else if (I.ValueType == 2)
                    L[2] = B[2] = Zero;
            }
            for (int K = 0; K < 3; ++K)
                V[K] = A.Binary(Op::Sub, A.Binary(Op::Mul, L[(K + 1) % 3], B[(K + 2) % 3]),
                                A.Binary(Op::Mul, L[(K + 2) % 3], B[(K + 1) % 3]));
            V[3] = Zero;
        }
        else if (Need == 2)
        {
            auto B = Pop(), L = Pop();
            for (int K = 0; K < 4; ++K)
                V[K] = A.Binary(O, L[K], B[K]);
        }
        else
        {
            auto L = Pop();
            for (int K = 0; K < 4; ++K)
                V[K] = A.Unary(O, L[K]);
        }
        Stack.push_back(V);
    }
    if (Stack.size() > 1)
    {
        Error = {0, "preshader-result-stack"};
        return false;
    }
    Out = Stack.empty() ? Splat(Zero) : Stack.back();
    return true;
}
enum class EMaterialPreshaderMath
{
    PythonOracle,
    EngineFloat32
};
inline FMaterialPreshaderAlgebra<double>
MaterialNumericPreshaderAlgebra(const std::vector<double>& Scalars, const std::vector<std::array<double, 4>>& Vectors,
                                EMaterialPreshaderMath Math = EMaterialPreshaderMath::PythonOracle)
{
    FMaterialPreshaderAlgebra<double> A;
    const auto Round = [=](double V)
    {
        return Math == EMaterialPreshaderMath::EngineFloat32 ? double(float(V)) : V;
    };
    A.Constant = [](std::uint32_t Bits)
    {
        float V;
        std::memcpy(&V, &Bits, 4);
        return double(V);
    };
    // Captured inputs are copied so algebras never hold references to temporaries.
    A.Scalar = [Scalars, Round](std::uint16_t I)
    {
        return Round(Scalars.at(I));
    };
    A.Vector = [Vectors, Round](std::uint16_t I)
    {
        auto V = Vectors.at(I);
        for (auto& C : V)
            C = Round(C);
        return V;
    };
    A.CoerceCross = Math == EMaterialPreshaderMath::EngineFloat32;
    A.Binary = [Round, Math](EMaterialPreshaderOpcode Op, double L, double R)
    {
        double V = 0;
        using O = EMaterialPreshaderOpcode;
        switch (Op)
        {
            case O::Add:
                V = L + R;
                break;
            case O::Sub:
                V = L - R;
                break;
            case O::Mul:
                V = L * R;
                break;
            case O::Div:
            {
                const double Delta = double(float(0.00001));
                const auto Safe = std::fabs(R) < Delta ? (R < 0 ? -Delta : Delta) : R;
                V = L / Safe;
                break;
            }
            case O::Fmod:
                V = R || Math == EMaterialPreshaderMath::EngineFloat32 ? std::fmod(L, R) : 0;
                break;
            case O::Min:
                V = Math == EMaterialPreshaderMath::EngineFloat32 ? (L < R ? L : R) : std::min(L, R);
                break;
            case O::Max:
                V = Math == EMaterialPreshaderMath::EngineFloat32 ? (L > R ? L : R) : std::max(L, R);
                break;
            case O::Atan2:
                V = std::atan2(L, R);
                break;
            default:
                throw std::invalid_argument("preshader-binary-operation");
        }
        return Round(V);
    };
    A.Clamp = [Round](double V, double Low, double High)
    {
        return Round(std::min(std::max(V, Low), High));
    };
    A.Unary = [Round, Math](EMaterialPreshaderOpcode Op, double V)
    {
        using O = EMaterialPreshaderOpcode;
        double Result = 0;
        switch (Op)
        {
            case O::Sin:
                Result = std::sin(V);
                break;
            case O::Cos:
                Result = std::cos(V);
                break;
            case O::Tan:
                Result = std::tan(V);
                break;
            case O::Asin:
                Result = std::asin(V);
                break;
            case O::Acos:
                Result = std::acos(V);
                break;
            case O::Atan:
                Result = std::atan(V);
                break;
            case O::Sqrt:
                Result = std::sqrt(V);
                break;
            case O::Saturate:
                Result = std::min(std::max(V, 0.0), 1.0);
                break;
            case O::Abs:
                Result = std::fabs(V);
                break;
            case O::Floor:
                Result = std::floor(V);
                break;
            case O::Ceil:
                Result = std::ceil(V);
                break;
            case O::Round:
                Result = std::floor(V + 0.5);
                break;
            case O::Trunc:
                Result = std::trunc(V);
                break;
            case O::Sign:
                Result = (V > 0) - (V < 0);
                break;
            case O::Frac:
                Result = V - std::floor(V);
                break;
            case O::Fractional:
                Result = V - std::trunc(V);
                break;
            case O::Log2:
                Result = Math == EMaterialPreshaderMath::EngineFloat32 ? double(std::log2(float(V)))
                         : V > 0                                       ? std::log(V) / std::log(2.0)
                                                                       : -std::numeric_limits<double>::infinity();
                break;
            case O::Log10:
                Result = Math == EMaterialPreshaderMath::EngineFloat32
                             ? double(std::log(float(V)) * (1.0f / std::log(10.0f)))
                         : V > 0 ? std::log10(V)
                                 : -std::numeric_limits<double>::infinity();
                break;
            default:
                throw std::invalid_argument("preshader-unary-operation");
        }
        return Round(Result);
    };
    return A;
}
}

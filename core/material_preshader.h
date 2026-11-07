// Bounded UE4.26 preshader bytecode reader. Float constants retain archive bits.
#pragma once
#include "package_reader.h"

namespace P4MapKit
{
enum class EMaterialPreshaderOpcode : std::uint8_t
{
    Nop,
    ConstantZero,
    Constant,
    ScalarParameter,
    VectorParameter,
    Add,
    Sub,
    Mul,
    Div,
    Fmod,
    Min,
    Max,
    Clamp,
    Sin,
    Cos,
    Tan,
    Asin,
    Acos,
    Atan,
    Atan2,
    Dot,
    Cross,
    Sqrt,
    Length,
    Saturate,
    Abs,
    Floor,
    Ceil,
    Round,
    Trunc,
    Sign,
    Frac,
    Fractional,
    Log2,
    Log10,
    ComponentSwizzle,
    AppendVector,
    TextureSize,
    TexelSize,
    ExternalTextureCoordinateScaleRotation,
    ExternalTextureCoordinateOffset,
    RuntimeVirtualTextureUniform
};
struct FMaterialPreshaderInstruction
{
    EMaterialPreshaderOpcode Opcode = EMaterialPreshaderOpcode::Nop;
    std::size_t Offset = 0;
    std::array<std::uint32_t, 4> ConstantBits{{0, 0, 0, 0}};
    std::uint16_t Parameter = 0;
    std::uint8_t ValueType = 0, Components = 0;
    std::array<std::uint8_t, 4> Swizzle{{0, 0, 0, 0}};
    std::int32_t LayerIndex = -1, TextureIndex = -1, VectorIndex = 0;
    std::uint8_t Association = 2;
    std::array<std::uint8_t, 16> ExternalGuid{{}};
};
struct FMaterialPreshaderProgram
{
    std::vector<FMaterialPreshaderInstruction> Instructions;
    std::size_t PeakStack = 0;
};
inline bool ParseMaterialPreshader(const std::vector<std::uint8_t>& Code, std::size_t ScalarCount,
                                   std::size_t VectorCount, std::size_t NameCount, FMaterialPreshaderProgram& Out,
                                   FReadError& Error, std::size_t InstructionLimit = 100000)
{
    FByteReader R(Code);
    FMaterialPreshaderProgram Result;
    std::size_t Stack = 0;
    const auto Byte = [&](std::uint8_t& V)
    {
        std::uint64_t N = 0;
        if (!R.Unsigned(1, N))
            return false;
        V = std::uint8_t(N);
        return true;
    };
    const auto Name = [&](std::uint16_t& V, std::size_t Limit)
    {
        std::uint64_t N = 0;
        if (!R.Unsigned(2, N))
            return false;
        V = std::uint16_t(N);
        return N < Limit || R.Reject("preshader-parameter-index");
    };
    while (R.Tell() < R.Limit() && R.Error.Code.empty())
    {
        FMaterialPreshaderInstruction I;
        I.Offset = R.Tell();
        std::uint8_t Op = 0;
        if (Result.Instructions.size() >= InstructionLimit)
        {
            R.Reject("preshader-instruction-budget");
            break;
        }
        if (!Byte(Op))
            break;
        if (Op > 41)
        {
            R.Reject("preshader-unknown-opcode");
            break;
        }
        I.Opcode = EMaterialPreshaderOpcode(Op);
        std::size_t Pop = 0, Push = 1;
        if (!Op)
            Push = 0;
        else if (Op == 1)
        {
        }
        else if (Op == 2)
        {
            for (auto& Bits : I.ConstantBits)
                if (!R.U32(Bits))
                    break;
        }
        else if (Op == 3 || Op == 4)
            Name(I.Parameter, Op == 3 ? ScalarCount : VectorCount);
        else if (Op >= 37)
        {
            Name(I.Parameter, NameCount);
            if (Op == 39 || Op == 40)
            {
                for (auto& V : I.ExternalGuid)
                    if (!Byte(V))
                        break;
                R.I32(I.TextureIndex);
            }
            else
            {
                R.I32(I.LayerIndex);
                Byte(I.Association);
                R.I32(I.TextureIndex);
                if (Op == 41)
                    R.I32(I.VectorIndex);
            }
            if (I.Association > 2 || I.LayerIndex < -1 || I.TextureIndex < -1 || I.VectorIndex < -1)
                R.Reject("preshader-runtime-identity");
        }
        else
        {
            Pop = 1;
            if ((Op >= 5 && Op <= 11) || Op == 19 || Op == 20 || Op == 21 || Op == 36)
                Pop = 2;
            if (Op == 12)
                Pop = 3;
            if (Op == 20 || Op == 21 || Op == 23)
            {
                Byte(I.ValueType);
                if (I.ValueType != 1 && I.ValueType != 2 && I.ValueType != 4 && I.ValueType != 8 && I.ValueType != 15)
                    R.Reject("preshader-value-type");
            }
            if (Op == 35)
            {
                Byte(I.Components);
                for (auto& C : I.Swizzle)
                    Byte(C);
                if (I.Components < 1 || I.Components > 4)
                    R.Reject("preshader-swizzle-count");
                for (std::size_t C = 0; C < I.Components && C < 4; ++C)
                    if (I.Swizzle[C] > 3)
                        R.Reject("preshader-swizzle-index");
            }
            if (Op == 36)
            {
                Byte(I.Components);
                if (I.Components > 4)
                    R.Reject("preshader-append-count");
            }
        }
        if (!R.Error.Code.empty())
            break;
        if (Stack < Pop)
        {
            R.Reject("preshader-stack-underflow");
            break;
        }
        Stack = Stack - Pop + Push;
        Result.PeakStack = std::max(Result.PeakStack, Stack);
        Result.Instructions.push_back(I);
    }
    // Empty streams are the engine's zero expression. A nonempty stream must
    // leave exactly one result; trailing hidden values are not accepted.
    if (R.Error.Code.empty() && Stack > 1)
        R.Reject("preshader-result-stack");
    Error = R.Error;
    if (!Error.Code.empty())
        return false;
    Out = std::move(Result);
    return true;
}
}

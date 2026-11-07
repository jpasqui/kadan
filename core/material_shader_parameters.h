// Portable read-only FShaderParameterBindings records. Semantic names require
// independently proved parameter-structure layout metadata from the caller.
// Ref must originate from ReadMaterialShaderReferences for this same Map.
#pragma once
#include "material_shader_map.h"

namespace P4MapKit
{
struct FMaterialShaderParameter
{
    std::uint16_t BufferIndex = 0, BaseIndex = 0, ByteOffset = 0, ByteSize = 0;
};
struct FMaterialShaderResourceParameter
{
    std::uint16_t ByteOffset = 0;
    std::uint8_t BaseIndex = 0, BaseType = 0;
};
struct FMaterialShaderParameterReference
{
    std::uint16_t BufferIndex = 0, ByteOffset = 0;
};
struct FMaterialShaderParameters
{
    std::vector<FMaterialShaderParameter> Parameters;
    std::vector<FMaterialShaderResourceParameter> Resources;
    std::vector<FMaterialShaderParameterReference> GraphUniformBuffers, ParameterReferences;
    std::uint32_t StructureLayoutHash = 0;
    std::uint16_t RootParameterBufferIndex = 0xffff;
};
inline bool ReadMaterialShaderParameters(const FMaterialShaderMapEnvelope& Map, const FMaterialShaderReference& Ref,
                                         FMaterialShaderParameters& Out, FReadError& Error)
{
    FMaterialFrozenReader R(Map.Frozen);
    FMaterialShaderParameters Result;
    const auto Fail = [&]()
    {
        Error = R.Error;
        return false;
    };
    const auto Base = Ref.FrozenOffset;
    if (Base > Map.Frozen.size() || Map.Frozen.size() - Base < 200)
    {
        R.Reject(Base, "material-shader-parameter-header-truncated");
        return Fail();
    }
    std::uint64_t Type = 0, Target = 0;
    std::int32_t Resource = 0;
    if (!R.U(Base + 176, 8, Type) || !R.U(Base + 192, 4, Target) || !R.I(Base + 196, Resource))
        return Fail();
    if (Ref.Frequency > 5 || Ref.ResourceIndex < 0 || !(Type & 1) || (Type >> 1) >= Map.ShaderTypes.size() ||
        Map.ShaderTypes[std::size_t(Type >> 1)] != Ref.TypeHash || (Target & 15) != Ref.Frequency ||
        Resource != Ref.ResourceIndex)
    {
        R.Reject(Base, "material-shader-parameter-reference-mismatch");
        return Fail();
    }
    std::size_t At = 0, Count = 0;
    std::uint64_t A = 0, B = 0, C = 0, D = 0;
    if (!R.Array(Base, 8, At, Count))
        return Fail();
    if (Count > 1000000)
    {
        R.Reject(Base, "material-shader-parameter-budget");
        return Fail();
    }
    for (std::size_t I = 0; I < Count; ++I)
    {
        if (!R.U(At + 8 * I, 2, A) || !R.U(At + 8 * I + 2, 2, B) || !R.U(At + 8 * I + 4, 2, C) ||
            !R.U(At + 8 * I + 6, 2, D))
            return Fail();
        Result.Parameters.push_back({std::uint16_t(A), std::uint16_t(B), std::uint16_t(C), std::uint16_t(D)});
    }
    if (!R.Array(Base + 16, 4, At, Count))
        return Fail();
    if (Count > 1000000)
    {
        R.Reject(Base + 16, "material-shader-parameter-budget");
        return Fail();
    }
    for (std::size_t I = 0; I < Count; ++I)
    {
        if (!R.U(At + 4 * I, 2, A) || !R.U(At + 4 * I + 2, 1, B) || !R.U(At + 4 * I + 3, 1, C))
            return Fail();
        Result.Resources.push_back({std::uint16_t(A), std::uint8_t(B), std::uint8_t(C)});
    }
    for (auto Pair : {std::make_pair(32, &Result.GraphUniformBuffers), std::make_pair(48, &Result.ParameterReferences)})
    {
        if (!R.Array(Base + std::size_t(Pair.first), 4, At, Count))
            return Fail();
        if (Count > 1000000)
        {
            R.Reject(Base + std::size_t(Pair.first), "material-shader-parameter-budget");
            return Fail();
        }
        for (std::size_t I = 0; I < Count; ++I)
        {
            if (!R.U(At + 4 * I, 2, A) || !R.U(At + 4 * I + 2, 2, B))
                return Fail();
            Pair.second->push_back({std::uint16_t(A), std::uint16_t(B)});
        }
    }
    if (!R.U(Base + 64, 4, A) || !R.U(Base + 68, 2, B))
        return Fail();
    Result.StructureLayoutHash = std::uint32_t(A);
    Result.RootParameterBufferIndex = std::uint16_t(B);
    Error = {};
    Out = std::move(Result);
    return true;
}
} // namespace P4MapKit

#pragma once
#include "material_shader_map.h"
#include "material_preshader_eval.h"

namespace P4MapKit
{
inline double MaterialFloatFromBits(std::uint32_t Bits)
{
    float V;
    std::memcpy(&V, &Bits, 4);
    return double(V);
}
inline FMaterialPreshaderAlgebra<double>
MaterialUniformNumericAlgebra(const FMaterialUniformSet& Set, const std::map<std::size_t, double>& ScalarOverrides = {},
                              const std::map<std::size_t, std::array<double, 4>>& VectorOverrides = {},
                              EMaterialPreshaderMath Math = EMaterialPreshaderMath::PythonOracle)
{
    std::vector<double> Scalars;
    std::vector<std::array<double, 4>> Vectors;
    for (const auto& P : Set.Scalars)
        Scalars.push_back(MaterialFloatFromBits(P.DefaultBits[0]));
    for (const auto& P : Set.Vectors)
    {
        std::array<double, 4> V;
        for (int C = 0; C < 4; ++C)
            V[C] = MaterialFloatFromBits(P.DefaultBits[C]);
        Vectors.push_back(V);
    }
    // Overrides use the archive parameter index; equal names with different
    // layer/association identities remain distinct instead of silently merging.
    for (const auto& Pair : ScalarOverrides)
        Scalars.at(Pair.first) = Pair.second;
    for (const auto& Pair : VectorOverrides)
        Vectors.at(Pair.first) = Pair.second;
    return MaterialNumericPreshaderAlgebra(Scalars, Vectors, Math);
}
template <class T>
inline bool EvaluateMaterialUniformBuffer(
    const FMaterialUniformSet& Set, const FMaterialPreshaderAlgebra<T>& Algebra, std::map<std::size_t, T>& Out,
    FReadError& Error, const std::function<std::array<T, 4>(const FMaterialUniformTexture&)>& VirtualUniform = {})
{
    std::map<std::size_t, T> Result;
    Error = {};
    std::size_t Base = 0;
    // UE4.26 puts VTPackedUniform uint4 words before vector/scalar expressions.
    // They are real renderer inputs; a caller must provide the packed words.
    for (const auto& Texture : Set.Textures)
        if (Texture.TextureType == 4)
        {
            if (!VirtualUniform)
            {
                Error = {0, "material-virtual-uniform-unbound"};
                return false;
            }
            auto V = VirtualUniform(Texture);
            for (std::size_t C = 0; C < 4; ++C)
                Result[Base + C * 4] = V[C];
            Base += 16;
        }
    for (const auto& P : Set.VectorPrograms)
    {
        std::array<T, 4> V;
        if (!EvaluateMaterialPreshader(P, Algebra, V, Error))
            return false;
        for (std::size_t C = 0; C < 4; ++C)
            Result[Base + C * 4] = V[C];
        Base += 16;
    }
    for (const auto& P : Set.ScalarPrograms)
    {
        std::array<T, 4> V;
        if (!EvaluateMaterialPreshader(P, Algebra, V, Error))
            return false;
        Result[Base] = V[0];
        Base += 4;
    }
    Out = std::move(Result);
    return true;
}
}

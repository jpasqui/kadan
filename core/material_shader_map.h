// The frozen uniform-expression boundary, not a claim to decode shader machine code.
#pragma once
#include "material_preshader.h"
#include <map>

namespace P4MapKit
{
struct FMaterialUniformParameter
{
    std::string Name;
    std::int32_t LayerIndex = -1;
    std::uint8_t Association = 2;
    bool NameIsNone = false;
    std::array<std::uint32_t, 4> DefaultBits{{}};
};
struct FMaterialUniformTexture
{
    FMaterialUniformParameter Parameter;
    std::int32_t TextureIndex = -1;
    std::uint8_t SamplerSource = 0, VirtualLayer = 0, TextureType = 0;
};
struct FMaterialUniformSet
{
    std::vector<FMaterialUniformParameter> Scalars, Vectors;
    std::vector<FMaterialUniformTexture> Textures;
    std::vector<std::string> PreshaderNames;
    std::vector<FMaterialPreshaderProgram> VectorPrograms, ScalarPrograms;
    std::vector<std::array<std::uint8_t, 16>> Collections;
    std::size_t ExternalTextureCount = 0, VirtualStackCount = 0;
};
struct FMaterialShaderMapEnvelope
{
    std::int32_t Quality = 0, Feature = 0;
    std::uint32_t MaximumAlignment = 0;
    std::array<std::uint8_t, 20> CookedIdHash{{}}, ResourceHash{{}};
    std::vector<std::uint8_t> Frozen;
    std::map<std::size_t, std::string> ScriptNames;
    std::map<std::size_t, std::pair<std::string, std::int32_t>> ScriptNameIdentities;
    std::vector<std::uint64_t> ShaderTypes, VertexFactoryTypes;
    FMaterialUniformSet Uniforms;
};
struct FMaterialShaderReference
{
    std::uint64_t TypeHash = 0, VertexFactoryHash = 0;
    std::uint8_t Frequency = 0;
    std::int32_t ResourceIndex = -1, Permutation = 0;
    std::size_t FrozenOffset = 0;
    std::map<std::uint16_t, std::uint64_t> UniformBufferHashes;
    std::map<std::uint16_t, std::string> NamedBuffers;
    // FShaderParameterMapInfo records {BaseIndex:u16, Size:u16}, expanded slots.
    std::vector<std::uint32_t> LooseTextures, LooseSamplers;
};
class FMaterialFrozenReader
{
    const std::vector<std::uint8_t>& Bytes;

public:
    FReadError Error;
    explicit FMaterialFrozenReader(const std::vector<std::uint8_t>& In) : Bytes(In)
    {
    }
    bool Reject(std::size_t At, const char* Code)
    {
        if (Error.Code.empty())
            Error = {At, Code};
        return false;
    }
    bool U(std::size_t At, std::size_t Width, std::uint64_t& V)
    {
        FByteReader R(Bytes, At);
        if (!R.Unsigned(Width, V))
        {
            Error = R.Error;
            return false;
        }
        return true;
    }
    bool I(std::size_t At, std::int32_t& V)
    {
        FByteReader R(Bytes, At);
        if (!R.I32(V))
        {
            Error = R.Error;
            return false;
        }
        return true;
    }
    bool Pointer(std::size_t At, std::size_t& Start, bool& Present)
    {
        std::uint64_t Bits = 0;
        if (!U(At, 8, Bits))
            return false;
        Present = Bits != 0;
        Start = 0;
        if (!Present)
            return true;
        if (!(Bits & 1))
            return Reject(At, "material-pointer-not-frozen");
        const bool Negative = (Bits >> 63) != 0;
        const std::uint64_t Magnitude = Negative ? ((~Bits + 1) >> 1) + 1 : (Bits >> 1);
        if (Negative)
        {
            if (Magnitude > At)
                return Reject(At, "material-pointer-bounds");
            Start = At - std::size_t(Magnitude);
        }
        else
        {
            if (Magnitude > Bytes.size() || At > Bytes.size() - std::size_t(Magnitude))
                return Reject(At, "material-pointer-bounds");
            Start = At + std::size_t(Magnitude);
        }
        if (Start >= Bytes.size())
            return Reject(At, "material-pointer-bounds");
        return true;
    }
    bool Array(std::size_t At, std::size_t Stride, std::size_t& Start, std::size_t& Count)
    {
        std::uint64_t Bits = 0;
        std::int32_t Num = 0, Max = 0;
        if (!U(At, 8, Bits) || !I(At + 8, Num) || !I(At + 12, Max))
            return false;
        if (Num < 0 || Max < Num)
            return Reject(At, "material-array-count");
        Count = std::size_t(Num);
        Start = 0;
        if (!Bits)
            return !Count || Reject(At, "material-array-null");
        if (!(Bits & 1))
            return Reject(At, "material-pointer-not-frozen");
        // Decode signed relative pointer without implementation-defined shifts.
        const bool Negative = (Bits >> 63) != 0;
        const std::uint64_t Magnitude = Negative ? ((~Bits + 1) >> 1) + 1 : (Bits >> 1);
        if (Negative)
        {
            if (Magnitude > At)
                return Reject(At, "material-pointer-bounds");
            Start = At - std::size_t(Magnitude);
        }
        else
        {
            if (Magnitude > Bytes.size() || At > Bytes.size() - std::size_t(Magnitude))
                return Reject(At, "material-pointer-bounds");
            Start = At + std::size_t(Magnitude);
        }
        if (!Count || Start > Bytes.size() || Count > (Bytes.size() - Start) / Stride)
            return Reject(At, "material-array-bounds");
        return true;
    }
};
inline bool ReadMaterialShaderReferences(const FMaterialShaderMapEnvelope& Map,
                                         std::vector<FMaterialShaderReference>& Out, FReadError& Error)
{
    FMaterialFrozenReader R(Map.Frozen);
    std::vector<FMaterialShaderReference> Result;
    const auto Content = [&](std::size_t Base, std::uint64_t MeshFactory)
    {
        std::size_t Types = 0, Permutations = 0, Shaders = 0, TypeCount = 0, PermutationCount = 0, ShaderCount = 0;
        if (!R.Array(Base + 24, 8, Types, TypeCount) || !R.Array(Base + 40, 4, Permutations, PermutationCount) ||
            !R.Array(Base + 56, 8, Shaders, ShaderCount))
            return false;
        if (TypeCount != PermutationCount || TypeCount != ShaderCount)
            return R.Reject(Base, "material-shader-array-counts");
        for (std::size_t J = 0; J < ShaderCount; ++J)
        {
            FMaterialShaderReference V;
            std::size_t At = 0;
            bool Present = false;
            std::uint64_t TypeIndex = 0, VFIndex = 0, Target = 0;
            if (!R.U(Types + J * 8, 8, V.TypeHash) || !R.I(Permutations + J * 4, V.Permutation) ||
                !R.Pointer(Shaders + J * 8, At, Present))
                return false;
            if (!Present)
                return R.Reject(Shaders + J * 8, "material-shader-null");
            V.FrozenOffset = At;
            V.VertexFactoryHash = MeshFactory;
            if (!R.U(At + 176, 8, TypeIndex) || !R.U(At + 184, 8, VFIndex) || !R.U(At + 192, 4, Target) ||
                !R.I(At + 196, V.ResourceIndex))
                return false;
            if (!(TypeIndex & 1) || (TypeIndex >> 1) >= Map.ShaderTypes.size() ||
                Map.ShaderTypes[std::size_t(TypeIndex >> 1)] != V.TypeHash)
                return R.Reject(At + 176, "material-shader-type-table");
            if (VFIndex)
            {
                if (!(VFIndex & 1) || (VFIndex >> 1) >= Map.VertexFactoryTypes.size())
                    return R.Reject(At + 184, "material-factory-type-table");
                V.VertexFactoryHash = Map.VertexFactoryTypes[std::size_t(VFIndex >> 1)];
            }
            V.Frequency = std::uint8_t(Target & 15);
            if (V.Frequency > 5 || V.ResourceIndex < 0)
                return R.Reject(At + 192, "material-shader-target");
            for (auto Pair : {std::make_pair(88, &V.LooseSamplers), std::make_pair(104, &V.LooseTextures)})
            {
                std::size_t Allocations = 0, Count = 0;
                if (!R.Array(At + Pair.first, 4, Allocations, Count))
                    return false;
                for (std::size_t K = 0; K < Count; ++K)
                {
                    std::uint64_t BaseIndex = 0, Size = 0;
                    if (!R.U(Allocations + K * 4, 2, BaseIndex) || !R.U(Allocations + K * 4 + 2, 2, Size))
                        return false;
                    Size = std::max(std::uint64_t(1), Size);
                    if (BaseIndex + Size > 65536 || Pair.second->size() + Size > 1000000)
                        return R.Reject(Allocations + K * 4, "material-loose-slot-budget");
                    for (std::uint64_t Slot = BaseIndex; Slot < BaseIndex + Size; ++Slot)
                        Pair.second->push_back(std::uint32_t(Slot));
                }
            }
            std::size_t BufferNames = 0, Bindings = 0, NameCount = 0, BindingCount = 0;
            if (!R.Array(At + 144, 8, BufferNames, NameCount) || !R.Array(At + 160, 2, Bindings, BindingCount))
                return false;
            if (NameCount != BindingCount)
                return R.Reject(At + 144, "material-buffer-array-counts");
            for (std::size_t K = 0; K < NameCount; ++K)
            {
                std::uint64_t Hash = 0, Binding = 0;
                if (!R.U(BufferNames + K * 8, 8, Hash) || !R.U(Bindings + K * 2, 2, Binding))
                    return false;
                auto Old = V.UniformBufferHashes.find(std::uint16_t(Binding));
                if (Old != V.UniformBufferHashes.end() && Old->second != Hash)
                    return R.Reject(At, "material-buffer-binding-conflict");
                V.UniformBufferHashes[std::uint16_t(Binding)] = Hash;
            }
            std::size_t Collections = 0, CollectionCount = 0;
            if (!R.Array(At + 208, 2, Collections, CollectionCount))
                return false;
            for (std::size_t K = 0; K < CollectionCount; ++K)
            {
                std::uint64_t Binding = 0;
                if (!R.U(Collections + K * 2, 2, Binding))
                    return false;
                if (Binding != 0xffff)
                    V.NamedBuffers[std::uint16_t(Binding)] = "MaterialCollection" + std::to_string(K);
            }
            for (auto Pair : {std::make_pair(224, "Material"), std::make_pair(240, "Pass")})
            {
                std::uint64_t Binding = 0;
                if (!R.U(At + Pair.first, 2, Binding))
                    return false;
                if (Binding != 0xffff)
                    V.NamedBuffers[std::uint16_t(Binding)] = Pair.second;
            }
            Result.push_back(std::move(V));
        }
        return true;
    };
    if (Map.Frozen.size() < 0x1fc || !Content(0, 0))
    {
        Error = R.Error;
        if (Error.Code.empty())
            Error = {0, "material-frozen-root-truncated"};
        return false;
    }
    std::size_t Meshes = 0, Count = 0;
    if (!R.Array(96, 8, Meshes, Count))
    {
        Error = R.Error;
        return false;
    }
    for (std::size_t I = 0; I < Count; ++I)
    {
        std::size_t At = 0;
        bool Present = false;
        std::uint64_t VF = 0;
        if (!R.Pointer(Meshes + I * 8, At, Present))
        {
            Error = R.Error;
            return false;
        }
        if (!Present)
            continue;
        if (!R.U(At + 96, 8, VF) || !Content(At, VF))
        {
            Error = R.Error;
            return false;
        }
    }
    Error = {};
    Out = std::move(Result);
    return true;
}
inline bool ReadMaterialUniformSet(const std::vector<std::uint8_t>& Frozen,
                                   const std::map<std::size_t, std::string>& ScriptNames, FMaterialUniformSet& Out,
                                   FReadError& Error)
{
    FMaterialFrozenReader R(Frozen);
    FMaterialUniformSet Result;
    Error = {};
    if (Frozen.size() < 0x1fc)
    {
        Error = {0, "material-frozen-root-truncated"};
        return false;
    }
    const auto Name = [&](std::size_t At, std::string& Into)
    {
        const auto I = ScriptNames.find(At);
        if (I == ScriptNames.end())
        {
            // FScriptName::None has no patch entry. An absent nonzero name is opaque.
            std::uint64_t A = 0, B = 0;
            if (!R.U(At, 8, A) || !R.U(At + 8, 4, B))
                return false;
            if (A || B)
                return R.Reject(At, "material-script-name-missing");
            Into = "None";
            return true;
        }
        Into = I->second;
        return true;
    };
    const auto Param = [&](std::size_t At, FMaterialUniformParameter& P)
    {
        std::uint64_t A = 0;
        if (!Name(At, P.Name) || !R.I(At + 12, P.LayerIndex) || !R.U(At + 16, 1, A))
            return false;
        P.NameIsNone = ScriptNames.find(At) == ScriptNames.end();
        P.Association = std::uint8_t(A);
        return (P.Association <= 2 && P.LayerIndex >= -1) || R.Reject(At, "material-parameter-identity");
    };
    const auto Params =
        [&](std::size_t At, std::size_t Stride, std::size_t Components, std::vector<FMaterialUniformParameter>& Into)
    {
        std::size_t P = 0, N = 0;
        if (!R.Array(At, Stride, P, N))
            return false;
        for (std::size_t J = 0; J < N; ++J)
        {
            FMaterialUniformParameter V;
            if (!Param(P + J * Stride, V))
                return false;
            for (std::size_t C = 0; C < Components; ++C)
            {
                std::uint64_t B = 0;
                if (!R.U(P + J * Stride + 20 + C * 4, 4, B))
                    return false;
                V.DefaultBits[C] = std::uint32_t(B);
            }
            Into.push_back(V);
        }
        return true;
    };
    if (!Params(144, 24, 1, Result.Scalars) || !Params(160, 36, 4, Result.Vectors))
    {
        Error = R.Error;
        return false;
    }
    for (std::size_t T = 0; T < 5; ++T)
    {
        std::size_t P = 0, N = 0;
        if (!R.Array(176 + 16 * T, 28, P, N))
        {
            Error = R.Error;
            return false;
        }
        for (std::size_t J = 0; J < N; ++J)
        {
            const auto At = P + J * 28;
            FMaterialUniformTexture V;
            V.TextureType = std::uint8_t(T);
            std::uint64_t S = 0, L = 0;
            if (!Param(At, V.Parameter) || !R.I(At + 20, V.TextureIndex) || !R.U(At + 24, 1, S) || !R.U(At + 25, 1, L))
            {
                Error = R.Error;
                return false;
            }
            if (S > 2 || V.TextureIndex < -1)
            {
                Error = {At, "material-texture-identity"};
                return false;
            }
            V.SamplerSource = std::uint8_t(S);
            V.VirtualLayer = std::uint8_t(L);
            Result.Textures.push_back(V);
        }
    }
    std::size_t P = 0, N = 0;
    if (!R.Array(256, 32, P, Result.ExternalTextureCount) || !R.Array(304, 40, P, Result.VirtualStackCount) ||
        !R.Array(320, 16, P, N))
    {
        Error = R.Error;
        return false;
    }
    for (std::size_t J = 0; J < N; ++J)
    {
        std::array<std::uint8_t, 16> G;
        std::copy_n(Frozen.begin() + P + J * 16, 16, G.begin());
        Result.Collections.push_back(G);
    }
    if (!R.Array(272, 12, P, N))
    {
        Error = R.Error;
        return false;
    }
    for (std::size_t J = 0; J < N; ++J)
    {
        std::string V;
        if (!Name(P + J * 12, V))
        {
            Error = R.Error;
            return false;
        }
        Result.PreshaderNames.push_back(V);
    }
    std::size_t DataAt = 0, DataSize = 0;
    if (!R.Array(288, 1, DataAt, DataSize))
    {
        Error = R.Error;
        return false;
    }
    const auto Programs = [&](std::size_t At, std::vector<FMaterialPreshaderProgram>& Into)
    {
        std::size_t Start = 0, Count = 0;
        if (!R.Array(At, 8, Start, Count))
            return false;
        for (std::size_t J = 0; J < Count; ++J)
        {
            std::uint64_t O = 0, L = 0;
            if (!R.U(Start + J * 8, 4, O) || !R.U(Start + J * 8 + 4, 4, L))
                return false;
            if (O > DataSize || L > DataSize - O)
                return R.Reject(Start + J * 8, "material-preshader-range");
            std::vector<std::uint8_t> Code(Frozen.begin() + DataAt + std::size_t(O),
                                           Frozen.begin() + DataAt + std::size_t(O + L));
            FMaterialPreshaderProgram V;
            FReadError E;
            if (!ParseMaterialPreshader(Code, Result.Scalars.size(), Result.Vectors.size(),
                                        Result.PreshaderNames.size(), V, E))
            {
                E.Offset += DataAt + std::size_t(O);
                R.Error = E;
                return false;
            }
            Into.push_back(std::move(V));
        }
        return true;
    };
    if (!Programs(112, Result.VectorPrograms) || !Programs(128, Result.ScalarPrograms))
    {
        Error = R.Error;
        return false;
    }
    Out = std::move(Result);
    return true;
}
// ResourceStart is the validated start of SerializeInlineShaderMaps in one
// export, supplied by its owner. Every read stays inside that export. The byte
// count provides the precise end, so no hash scanning or heuristic acceptance.
inline bool ReadMaterialShaderMapEnvelope(const std::vector<std::uint8_t>& Bytes, std::size_t ResourceStart,
                                          std::size_t ExportEnd, FMaterialShaderMapEnvelope& Out, FReadError& Error)
{
    FByteReader R(Bytes, ResourceStart, ExportEnd);
    FMaterialShaderMapEnvelope Result;
    const auto Finish = [&]()
    {
        Error = R.Error;
        return false;
    };
    std::size_t Resources = 0, Names = 0, Locations = 0;
    if (!R.Count(Resources, 1) || Resources != 1)
    {
        R.Reject("material-resource-count");
        return Finish();
    }
    if (!R.Count(Names, 4096))
        return Finish();
    std::vector<std::string> NameTable;
    for (std::size_t I = 0; I < Names; ++I)
    {
        std::string Name;
        std::uint32_t Hash = 0;
        if (!R.String(Name) || !R.U32(Hash))
            return Finish();
        NameTable.push_back(std::move(Name));
    }
    if (!R.Count(Locations, 64) || !Locations)
    {
        R.Reject("material-resource-locations");
        return Finish();
    }
    if (!R.Skip(Locations * 6))
        return Finish();
    std::size_t DataSize = 0;
    if (!R.Count(DataSize, std::numeric_limits<std::int32_t>::max()))
        return Finish();
    const auto DataStart = R.Tell();
    if (DataSize > R.Limit() - DataStart)
    {
        R.Reject("material-resource-range");
        return Finish();
    }
    FByteReader D(Bytes, DataStart, DataStart + DataSize);
    const auto Fail = [&]()
    {
        Error = D.Error;
        return false;
    };
    std::uint32_t Cooked = 0, Valid = 0, Flags = 0, FrozenSize = 0;
    if (!D.U32(Cooked) || !D.U32(Valid) || !D.I32(Result.Quality) || !D.I32(Result.Feature))
        return Fail();
    if (Cooked != 1 || Valid != 1)
    {
        D.Reject("material-resource-not-cooked");
        return Fail();
    }
    const auto Hash = [&](std::array<std::uint8_t, 20>& Into)
    {
        const auto At = D.Tell();
        if (!D.Skip(20))
            return false;
        std::copy_n(Bytes.begin() + At, 20, Into.begin());
        return true;
    };
    if (!Hash(Result.CookedIdHash) || !D.U32(Result.MaximumAlignment) || !D.U32(Flags) || !D.U32(FrozenSize))
        return Fail();
    // UINT32_MAX is FPlatformTypeLayoutParameters' unrestricted alignment.
    if (Flags != 1 || !Result.MaximumAlignment ||
        (Result.MaximumAlignment != UINT32_MAX && (Result.MaximumAlignment & (Result.MaximumAlignment - 1))))
    {
        D.Reject("material-frozen-layout");
        return Fail();
    }
    auto At = D.Tell();
    if (!D.Skip(FrozenSize))
        return Fail();
    Result.Frozen.assign(Bytes.begin() + At, Bytes.begin() + At + FrozenSize);
    std::uint32_t VTables = 0, ScriptPatches = 0, MinimalPatches = 0;
    if (!D.U32(VTables) || !D.U32(ScriptPatches) || !D.U32(MinimalPatches))
        return Fail();
    if (std::uint64_t(VTables) * 12 > D.Limit() - D.Tell())
    {
        D.Reject("material-vtable-patches");
        return Fail();
    }
    for (std::uint32_t I = 0; I < VTables; ++I)
    {
        std::uint64_t HashValue = 0;
        std::uint32_t Count = 0;
        if (!D.Unsigned(8, HashValue) || !D.U32(Count) || !D.Skip(std::size_t(Count) * 8))
            return Fail();
    }
    if (std::uint64_t(ScriptPatches) + MinimalPatches > (D.Limit() - D.Tell()) / 12)
    {
        D.Reject("material-name-patches");
        return Fail();
    }
    for (std::uint64_t I = 0; I < std::uint64_t(ScriptPatches) + MinimalPatches; ++I)
    {
        std::int32_t Index = 0, Number = 0;
        std::uint32_t Count = 0;
        if (!D.I32(Index) || !D.I32(Number) || !D.U32(Count))
            return Fail();
        if (Index < 0 || std::size_t(Index) >= NameTable.size() || Number < 0 || Count > (D.Limit() - D.Tell()) / 4)
        {
            D.Reject("material-name-patch-index");
            return Fail();
        }
        const auto Identity = std::make_pair(NameTable[std::size_t(Index)], Number);
        const auto Name = Identity.first + (Number ? "_" + std::to_string(Number - 1) : "");
        for (std::uint32_t J = 0; J < Count; ++J)
        {
            std::uint32_t Offset = 0;
            if (!D.U32(Offset))
                return Fail();
            if (Offset > Result.Frozen.size() || 12 > Result.Frozen.size() - Offset)
            {
                D.Reject("material-name-patch-offset");
                return Fail();
            }
            const auto Existing = Result.ScriptNameIdentities.find(Offset);
            if (Existing != Result.ScriptNameIdentities.end() && Existing->second != Identity)
            {
                D.Reject("material-name-patch-conflict");
                return Fail();
            }
            Result.ScriptNames[Offset] = Name;
            Result.ScriptNameIdentities[Offset] = Identity;
        }
    }
    std::size_t ShaderTypes = 0, VFTypes = 0;
    if (!D.Count(ShaderTypes) || !D.Count(VFTypes))
        return Fail();
    for (auto Pair :
         {std::make_pair(ShaderTypes, &Result.ShaderTypes), std::make_pair(VFTypes, &Result.VertexFactoryTypes)})
        for (std::size_t I = 0; I < Pair.first; ++I)
        {
            std::uint64_t V = 0;
            if (!D.Unsigned(8, V))
                return Fail();
            Pair.second->push_back(V);
        }
    std::size_t Dependencies = 0;
    if (!D.Count(Dependencies) || !D.Skip(Dependencies * 32))
        return Fail();
    std::uint32_t Shared = 0;
    if (!D.U32(Shared))
        return Fail();
    if (Shared != 1)
    {
        D.Reject("material-inline-code-unsupported");
        return Fail();
    }
    if (!Hash(Result.ResourceHash))
        return Fail();
    if (D.Tell() != D.Limit())
    {
        D.Reject("material-resource-trailing-bytes");
        return Fail();
    }
    if (!ReadMaterialUniformSet(Result.Frozen, Result.ScriptNames, Result.Uniforms, Error))
        return false;
    Error = {};
    Out = std::move(Result);
    return true;
}
}

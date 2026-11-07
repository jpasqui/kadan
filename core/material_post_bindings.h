// Postprocess fields are selected by original FShaderParameterBindings offsets.
// Header metadata comes from the validated original pixel payload, never a slot
// guess. Supplemental scene/fixed resources remain explicit caller contracts.
#pragma once
#include "material_shader_map.h"
#include "material_engine_hashes.h"
namespace P4MapKit
{
namespace Material
{
struct MaterialPostPackedArray
{
    std::uint32_t Size = 0;
    std::uint16_t Kind = 0, BufferIndex = 0;
};
struct MaterialPostHeaderMetadata
{
    std::map<int, int> NativeBuffers;
    std::vector<MaterialPostPackedArray> PackedGlobalArrays;
    std::size_t InputVaryingCount = 0;
};
struct MaterialPostParameterRecord
{
    std::uint16_t Kind = 0, Index = 0, Offset = 0, Size = 0;
};
struct MaterialPostBindings
{
    std::map<std::pair<int, int>, std::pair<std::string, int>> Words;
    int SceneSlot = -1;
    std::map<int, bool> Samplers;
    std::vector<MaterialPostParameterRecord> ParameterRecords;
};
struct MaterialPostSceneHeaderMetadata
{
    std::vector<std::uint32_t> LayoutHashes;
    std::map<int, std::string> Buffers;
    std::map<int, std::vector<std::pair<int, int>>> TextureEntries, SRVEntries, SamplerEntries;
};
struct MaterialPostSupplementalBindings
{
    bool Enabled = false, ViewLayoutProved = false;
    std::map<std::string, int> EngineTextureRoles, SceneSamplers;
};
inline bool ReadMaterialPostSupplemental(const FMaterialShaderMapEnvelope* Map,
                                         const MaterialPostSceneHeaderMetadata& Header,
                                         MaterialPostSupplementalBindings& Out, FReadError& Error)
{
    MaterialPostSupplementalBindings Result;
    if (Map)
    {
        static const std::array<std::uint8_t, 20> A{{0x5c, 0x62, 0xdc, 0x52, 0xfc, 0x7d, 0xae, 0x33, 0x22, 0x4e,
                                                     0xc0, 0xd3, 0xa8, 0x02, 0xdb, 0xd7, 0xd3, 0x7d, 0x87, 0xed}},
            B{{0xdc, 0x57, 0x8f, 0x42, 0x8f, 0x6f, 0xfa, 0x23, 0x79, 0x5d,
               0xba, 0xe7, 0x8f, 0x2d, 0x58, 0x91, 0xff, 0xb9, 0x25, 0x61}};
        if (Map->ResourceHash != A && Map->ResourceHash != B)
        {
            Error = {};
            Out = Result;
            return true;
        }
    }
    Result.Enabled = true;
    const auto Entries = [](const std::map<int, std::vector<std::pair<int, int>>>& Table, int Buffer)
    {
        std::map<int, int> M;
        auto I = Table.find(Buffer);
        if (I != Table.end())
            for (auto V : I->second)
                M[V.first] = V.second;
        return M;
    };
    const auto Put = [&](std::map<std::string, int>& Into, const std::string& Name, int Slot, const std::string& Prefix)
    {
        auto Old = Into.find(Name);
        if (Old != Into.end() && Old->second != Slot)
        {
            Error = {0, Prefix + Name};
            return false;
        }
        Into[Name] = Slot;
        return true;
    };
    for (std::size_t Buffer = 0; Buffer < Header.LayoutHashes.size(); ++Buffer)
    {
        const auto Layout = Header.LayoutHashes[Buffer];
        auto Name = Header.Buffers.find(int(Buffer));
        if (Name != Header.Buffers.end() && Name->second == "View" && Layout == EngineHash::ViewLayout)
            Result.ViewLayoutProved = true;
        auto Textures = Entries(Header.TextureEntries, int(Buffer));
        for (auto V : Entries(Header.SRVEntries, int(Buffer)))
            Textures[V.first] = V.second;
        auto Samplers = Entries(Header.SamplerEntries, int(Buffer));
        std::map<std::string, int> Roles;
        if (Layout == 0x0d480502)
            Roles = {{"eye_adaptation", 65}, {"scene_color_copy", 66}, {"scene_depth", 36}, {"scene_stencil", 47}};
        else if (Layout == 0x05ec0dee)
            Roles = {{"eye_adaptation", 77}, {"scene_color_copy", 78}, {"scene_depth", 36}, {"scene_stencil", 47}};
        else if (Layout == 0x097f0609)
            Roles = {{"scene_color_linear", 0}, {"scene_depth", 1},     {"gbuffer_a", 2},     {"gbuffer_b", 3},
                     {"gbuffer_c", 4},          {"gbuffer_d", 5},       {"gbuffer_e", 6},     {"gbuffer_f", 7},
                     {"velocity", 8},           {"screen_space_ao", 9}, {"custom_depth", 10}, {"custom_stencil", 11},
                     {"scene_stencil", 12}};
        for (auto Role : Roles)
            if (Textures.count(Role.second) &&
                !Put(Result.EngineTextureRoles, Role.first, Textures.at(Role.second), "several compiled bindings for "))
                return false;
        if (Layout == EngineHash::ViewLayout && Samplers.count(43))
            Result.SceneSamplers["view_clamp"] = Samplers.at(43);
        if (!Roles.empty())
            for (auto Field : {std::make_pair("copy", Layout == 0x0d480502 ? 67 : 79), std::make_pair("point", 48)})
                if (Samplers.count(Field.second) && !Put(Result.SceneSamplers, Field.first, Samplers.at(Field.second),
                                                         "several scene sampler bindings for "))
                    return false;
    }
    for (std::size_t Buffer = 0; Buffer < Header.LayoutHashes.size(); ++Buffer)
        if (Header.LayoutHashes[Buffer] == 0x097f0609)
        {
            auto Samplers = Entries(Header.SamplerEntries, int(Buffer));
            if (Samplers.count(13))
            {
                auto Old = Result.SceneSamplers.find("point");
                if (Old != Result.SceneSamplers.end() && Old->second != Samplers.at(13))
                {
                    Error = {0, "several checked point-clamp bindings"};
                    return false;
                }
                Result.SceneSamplers["point"] = Samplers.at(13);
            }
        }
    Error = {};
    Out = std::move(Result);
    return true;
}
inline bool ReadMaterialPostBindings(const FMaterialShaderMapEnvelope& Map, const FMaterialShaderReference& Ref,
                                     const MaterialPostHeaderMetadata& Header, MaterialPostBindings& Out,
                                     FReadError& Error)
{
    FMaterialFrozenReader R(Map.Frozen);
    MaterialPostBindings Result;
    const auto Fail = [&]()
    {
        Error = R.Error;
        return false;
    };
    std::size_t Types = 0, Permutations = 0, Shaders = 0, N = 0, NP = 0, NS = 0;
    if (!R.Array(24, 8, Types, N) || !R.Array(40, 4, Permutations, NP) || !R.Array(56, 8, Shaders, NS))
        return Fail();
    if (N != NP || N != NS)
    {
        R.Reject(24, "frozen global shader arrays disagree");
        return Fail();
    }
    std::size_t Base = 0, Matches = 0;
    for (std::size_t I = 0; I < N; ++I)
    {
        std::uint64_t Type = 0;
        std::int32_t Permutation = 0, Resource = 0;
        std::size_t At = 0;
        bool Present = false;
        if (!R.U(Types + I * 8, 8, Type) || !R.I(Permutations + I * 4, Permutation) ||
            !R.Pointer(Shaders + I * 8, At, Present))
            return Fail();
        if (!Present)
        {
            R.Reject(Shaders + I * 8, "material-shader-null");
            return Fail();
        }
        if (!R.I(At + 196, Resource))
            return Fail();
        if (Type == Ref.TypeHash && Permutation == Ref.Permutation && Resource == Ref.ResourceIndex)
        {
            Base = At;
            ++Matches;
        }
    }
    if (Matches != 1)
    {
        R.Reject(24, "no unique frozen global shader binding record");
        return Fail();
    }
    std::map<int, std::pair<std::uint32_t, int>> Packed;
    for (const auto& A : Header.PackedGlobalArrays)
    {
        auto B = Header.NativeBuffers.find(A.BufferIndex);
        Packed[A.Kind] = {A.Size, B == Header.NativeBuffers.end() ? -1 : B->second};
    }
    static const std::map<unsigned, std::pair<std::string, int>> Fields = {
        {64, {"OutputMin", 2}},   {88, {"OutputInverse", 2}}, {208, {"InputBias", 2}},
        {224, {"InputScale", 2}}, {240, {"InputMin", 2}},     {248, {"InputMax", 2}}};
    std::size_t Parameters = 0, Count = 0;
    if (!R.Array(Base, 8, Parameters, Count))
        return Fail();
    for (std::size_t I = 0; I < Count; ++I)
    {
        MaterialPostParameterRecord V;
        std::uint64_t Kind = 0, Index = 0, Offset = 0, Size = 0;
        const auto At = Parameters + 8 * I;
        if (!R.U(At, 2, Kind) || !R.U(At + 2, 2, Index) || !R.U(At + 4, 2, Offset) || !R.U(At + 6, 2, Size))
            return Fail();
        auto P = Packed.find(int(Kind));
        if (P == Packed.end() || P->second.second < 0 || Index + Size > P->second.first)
        {
            R.Reject(At, "postprocess parameter does not fit its packed global array");
            return Fail();
        }
        auto Field = Fields.find(unsigned(Offset));
        if (Field == Fields.end() || Size != std::uint64_t(4 * Field->second.second))
        {
            R.Error = {At, "unbound postprocess parameter at struct byte " + std::to_string(Offset)};
            return Fail();
        }
        if ((Field->second.first == "OutputMin") != (Kind == unsigned('u')))
        {
            R.Reject(At, "postprocess field has the wrong packed numeric type");
            return Fail();
        }
        for (int Component = 0; Component < Field->second.second; ++Component)
            Result.Words[{P->second.second, int(Index + 4 * Component)}] = {Field->second.first, Component};
        V.Kind = std::uint16_t(Kind);
        V.Index = std::uint16_t(Index);
        V.Offset = std::uint16_t(Offset);
        V.Size = std::uint16_t(Size);
        Result.ParameterRecords.push_back(V);
    }
    std::size_t Resources = 0;
    if (!R.Array(Base + 16, 4, Resources, Count))
        return Fail();
    std::size_t Scenes = 0;
    for (std::size_t I = 0; I < Count; ++I)
    {
        std::uint64_t Offset = 0, Index = 0, Kind = 0;
        const auto At = Resources + 4 * I;
        if (!R.U(At, 2, Offset) || !R.U(At + 2, 1, Index) || !R.U(At + 3, 1, Kind))
            return Fail();
        if (Offset == 256 && Kind == 9)
        {
            Result.SceneSlot = int(Index);
            ++Scenes;
        }
        else if (Offset == 264 && Kind == 8)
            Result.Samplers[int(Index)] = false;
        else if (Offset == 784 && Kind == 8)
            Result.Samplers[int(Index)] = true;
        else
        {
            R.Error = {At, "unbound postprocess resource at struct byte " + std::to_string(Offset)};
            return Fail();
        }
    }
    if (Scenes != 1)
    {
        R.Reject(Base + 16, "postprocess input zero has no unique texture binding");
        return Fail();
    }
    if (Header.InputVaryingCount)
    {
        R.Reject(Base, "postprocess custom vertex interpolators are not recovered");
        return Fail();
    }
    Error = {};
    Out = std::move(Result);
    return true;
}
}
}

// Layout cases for the native data of a level's own objects. Every byte is made here; none is the game's.
#pragma once
#include "core/level_native.h"
#include <functional>
static void P4Zeros(std::vector<std::uint8_t>& Bytes, std::size_t Count) { Bytes.insert(Bytes.end(), Count, 0); }
static float P4Real(std::uint32_t Bits) { float Out = 0; std::memcpy(&Out, &Bits, 4); return Out; }
static std::uint64_t P4Ref(std::int32_t Index) { return std::uint64_t(std::uint32_t(Index)); }
// Every shorter span and a span one byte longer has to be refused.
static void P4CheckCuts(const std::vector<std::uint8_t>& Bytes, const std::function<bool(const std::vector<std::uint8_t>&)>& Reads)
{
    Check(Reads(Bytes));
    for (std::size_t Size = 0; Size < Bytes.size(); ++Size) { auto Cut = Bytes; Cut.resize(Size); Check(!Reads(Cut)); }
    auto Long = Bytes; Long.push_back(0); Check(!Reads(Long));
}
static std::vector<std::uint8_t> P4LevelBytes(std::size_t Buckets = 0, std::size_t Values = 2)
{
    std::vector<std::uint8_t> L;
    P4Put(L, 0); P4Put(L, 3); P4Put(L, 6); P4Put(L, 0); P4Put(L, 3);
    P4String(L, "unreal"); P4Put(L, 0); P4String(L, "/Game/Map"); P4Put(L, 0); P4Put(L, 1); P4String(L, "Option");
    P4Put(L, 7777); P4Put(L, 1); P4Put(L, 5); P4Put(L, 1); P4Put(L, 9); P4Put(L, 3); P4Put(L, 11); P4Put(L, 12);
    P4Zeros(L, 24); P4Put(L, Buckets); P4Zeros(L, 41); P4Put(L, Values); P4Zeros(L, Values * 4);
    return L;
}
static std::vector<std::uint8_t> P4ModelBytes(std::uint64_t Global, std::uint64_t Class, std::size_t Vectors, std::size_t Surfaces,
    std::size_t Buffer, std::size_t Lightmass, std::uint64_t NodeSize = 64, std::size_t Nodes = 0)
{
    std::vector<std::uint8_t> M;
    P4Put(M, 0); P4Put(M, Global, 1); P4Put(M, Class, 1); P4Zeros(M, 28);
    P4Put(M, 12); P4Put(M, Vectors); for (std::size_t Index = 0; Index < Vectors * 3; ++Index) P4Put(M, 0x3f800000 + Index);
    P4Put(M, 12); P4Put(M, 0); P4Put(M, NodeSize); P4Put(M, Nodes);
    for (std::size_t Index = 0; Index < Nodes; ++Index)
    { P4Zeros(M, 16); P4Put(M, 20 + Index); P4Put(M, 30 + Index); P4Zeros(M, 30); P4Put(M, 4, 1); P4Put(M, 2, 1); P4Zeros(M, 8); }
    P4Put(M, Surfaces);
    for (std::size_t Index = 0; Index < Surfaces; ++Index)
    { for (std::uint64_t Part : {0u, 9u, 1u, 2u, 3u, 4u, 5u, 0u}) P4Put(M, Part); P4Zeros(M, 16); P4Put(M, 0x42000000); P4Put(M, 7 + Index); }
    P4Put(M, 24); P4Put(M, 0); P4Put(M, 4); P4Put(M, 1); P4Put(M, 0); P4Put(M, 7);
    if ((Class & 1) == 0) { P4Put(M, Buffer); P4Zeros(M, Buffer * 36); }
    for (std::uint64_t Part : {1u, 2u, 3u, 4u}) P4Put(M, Part);
    P4Put(M, Lightmass);
    for (std::size_t Index = 0; Index < Lightmass; ++Index) { for (std::uint64_t Part : {1u, 0u, 0u, 1u}) P4Put(M, Part); P4Zeros(M, 16); P4Put(M, 0x3f800000); }
    return M;
}
static std::vector<std::uint8_t> P4ClassBytes(std::uint64_t VariableType, std::size_t Script, bool Extras)
{
    std::vector<std::uint8_t> C;
    P4Put(C, 0); P4Put(C, P4Ref(-6)); P4Put(C, Extras ? 1 : 0); if (Extras) P4Put(C, 7);
    P4Put(C, 1); P4Put(C, VariableType); P4Put(C, 0); P4Put(C, 3); P4Put(C, 0); P4Put(C, 0x00200001); P4Put(C, 1); P4Put(C, 8);
    P4Put(C, 0x0000000000010005ull, 8); P4Put(C, 0, 2); P4Put(C, 0); P4Put(C, 0); P4Put(C, 0, 1);
    P4Put(C, Script); P4Put(C, Script); P4Zeros(C, Script);
    P4Put(C, Extras ? 1 : 0); if (Extras) { P4Put(C, 5); P4Put(C, 0); P4Put(C, 7); }
    P4Put(C, 0x00840a14); P4Put(C, P4Ref(-3)); P4Put(C, 1); P4Put(C, 0); P4Put(C, 0);
    P4Put(C, Extras ? 1 : 0); if (Extras) P4Zeros(C, 12);
    P4Put(C, 0); P4Put(C, 0); P4Put(C, 0); P4Put(C, 1); P4Put(C, 2);
    return C;
}
static void CheckLevelNativeReaders()
{
    using namespace P4MapKit;
    {
        const auto Bytes = P4LevelBytes();
        P4CheckCuts(Bytes, [](const std::vector<std::uint8_t>& B) { return ReadLevelNative(B, 0, B.size()).Error.Code.empty(); });
        const auto Level = ReadLevelNative(Bytes, 0, Bytes.size());
        Check(Level.Actors == std::vector<std::int32_t>({6, 0, 3})); Check(Level.Protocol == "unreal"); Check(Level.Host.empty());
        Check(Level.Map == "/Game/Map"); Check(Level.Portal.empty()); Check(Level.Options == std::vector<std::string>({"Option"}));
        Check(Level.Port == 7777); Check(Level.Valid == 1); Check(Level.Model == 5);
        Check(Level.ModelComponents == std::vector<std::int32_t>({9})); Check(Level.LevelScriptActor == 3);
        Check(Level.NavListStart == 11); Check(Level.NavListEnd == 12); Check(Level.VisibilityBuckets == 0);
        Check(Level.DistanceFieldValues == 2); Check(Level.End == Bytes.size());
        // The span may start anywhere in the file.
        std::vector<std::uint8_t> Shifted(5, 0xee); Shifted.insert(Shifted.end(), Bytes.begin(), Bytes.end());
        Check(ReadLevelNative(Shifted, 5, Shifted.size()).Actors == Level.Actors);
        const auto Cells = P4LevelBytes(1);
        Check(ReadLevelNative(Cells, 0, Cells.size()).Error.Code == "precomputed-visibility-not-decoded");
        auto Id = Bytes; P4Patch(Id, 0, 2);
        Check(ReadLevelNative(Id, 0, Id.size()).Error.Code == "invalid-object-id-flag");
        auto WithId = Bytes; P4Patch(WithId, 0, 1); WithId.insert(WithId.begin() + 4, 16, 0xab);
        Check(ReadLevelNative(WithId, 0, WithId.size()).Actors == Level.Actors);
        auto Many = Bytes; P4Patch(Many, 4, 1000);
        Check(ReadLevelNative(Many, 0, Many.size()).Error.Code == "truncated");
    }
    {
        std::vector<std::uint8_t> Bytes;
        P4Put(Bytes, 0); P4Put(Bytes, 4); P4Put(Bytes, 1); P4Put(Bytes, P4Ref(-2)); P4Put(Bytes, 2); P4Put(Bytes, 9); P4Put(Bytes, 10);
        P4CheckCuts(Bytes, [](const std::vector<std::uint8_t>& B) { return ReadWorldNative(B, 0, B.size()).Error.Code.empty(); });
        const auto World = ReadWorldNative(Bytes, 0, Bytes.size());
        Check(World.PersistentLevel == 4); Check(World.ExtraReferencedObjects == std::vector<std::int32_t>({-2}));
        Check(World.StreamingLevels == std::vector<std::int32_t>({9, 10})); Check(World.End == Bytes.size());
    }
    {
        const auto Reads = [](const std::vector<std::uint8_t>& B) { return ReadModelNative(B, 0, B.size()).Error.Code.empty(); };
        const auto Bytes = P4ModelBytes(1, 0, 0, 0, 0, 0);
        P4CheckCuts(Bytes, Reads);
        const auto Model = ReadModelNative(Bytes, 0, Bytes.size());
        Check(Model.Empty()); Check(Model.SharedSides == 4); Check(Model.RootOutside); Check(!Model.Linked); Check(Model.UniqueVertices == 7);
        Check((Model.LightingGuid == std::array<std::uint32_t, 4>{{1, 2, 3, 4}})); Check(Model.End == Bytes.size());
        const auto Full = P4ModelBytes(1, 0, 2, 0, 3, 0);
        P4CheckCuts(Full, Reads);
        const auto Filled = ReadModelNative(Full, 0, Full.size());
        Check(!Filled.Empty()); Check(Filled.Vectors.size() == 2); Check(Filled.BufferVertices == 3);
        Check(Filled.Vectors[0][0] == 1.0f); Check(Filled.Vectors[1][2] > 1.0f);
        // A brush's model has no vertex buffer.
        const auto Brush = P4ModelBytes(1, 1, 1, 0, 0, 0);
        P4CheckCuts(Brush, Reads);
        Check(ReadModelNative(Brush, 0, Brush.size()).Vectors.size() == 1);
        // A brush's shape: polygons on surfaces, and a lighting build's settings.
        const auto Shape = P4ModelBytes(1, 1, 3, 2, 0, 1, 64, 2);
        P4CheckCuts(Shape, Reads);
        const auto Shaped = ReadModelNative(Shape, 0, Shape.size());
        Check(Shaped.Nodes.size() == 2); Check(Shaped.Nodes[1].VertexPool == 21); Check(Shaped.Nodes[1].Surface == 31);
        Check(Shaped.Nodes[0].Vertices == 4); Check(Shaped.Nodes[0].Flags == 2);
        Check(Shaped.Surfaces.size() == 2); Check(Shaped.Surfaces[0].PolyFlags == 9); Check(Shaped.Surfaces[0].Base == 1);
        Check(Shaped.Surfaces[0].Normal == 2); Check(Shaped.Surfaces[0].TextureU == 3); Check(Shaped.Surfaces[0].TextureV == 4);
        Check(Shaped.Surfaces[0].BrushPolygon == 5); Check(Shaped.Surfaces[1].LightmassIndex == 8); Check(Shaped.Surfaces[1].LightMapScale == 32.0f);
        Check(Shaped.LightmassSettings.size() == 1); Check(Shaped.LightmassSettings[0].Flags[3] == 1); Check(Shaped.LightmassSettings[0].Values[4] == 1.0f);
        Check(!Shaped.Empty());
        // Settings alone are not geometry.
        const auto Settings = P4ModelBytes(1, 0, 0, 0, 0, 1);
        P4CheckCuts(Settings, Reads);
        Check(ReadModelNative(Settings, 0, Settings.size()).Empty());
        for (const std::uint64_t Global : {0u, 2u, 4u})
        { const auto Raw = P4ModelBytes(Global, 0, 0, 0, 0, 0); Check(ReadModelNative(Raw, 0, Raw.size()).Error.Code == "model-not-cooked"); }
        const auto Class = P4ModelBytes(1, 2, 0, 0, 0, 0);
        Check(ReadModelNative(Class, 0, Class.size()).Error.Code == "model-not-cooked");
        const auto Size = P4ModelBytes(1, 0, 0, 0, 0, 0, 60);
        Check(ReadModelNative(Size, 0, Size.size()).Error.Code == "unexpected-element-size");
        auto Flag = Bytes; P4Patch(Flag, Flag.size() - 36, 2);
        Check(ReadModelNative(Flag, 0, Flag.size()).Error.Code == "invalid-flag");
    }
    {
        const auto Reads = [](const std::vector<std::uint8_t>& B) { return ReadStaticMeshComponentNative(B, 0, B.size()).Error.Code.empty(); };
        const auto Bytes = [](std::uint64_t Global, std::uint64_t Class, std::uint64_t Painted)
        {
            std::vector<std::uint8_t> C;
            P4Put(C, 0); P4Put(C, 2);
            for (std::uint64_t Lod = 0; Lod < 2; ++Lod)
            {
                P4Put(C, Global, 1); P4Put(C, Class, 1);
                if ((Global & 2) == 0) for (std::uint64_t Part : {5u, 6u, 7u, 8u}) P4Put(C, Part + Lod);
                if ((Class & 1) == 0) P4Put(C, Painted, 1);
            }
            return C;
        };
        std::vector<std::uint8_t> None; P4Put(None, 0); P4Put(None, 0);
        P4CheckCuts(None, Reads);
        Check(ReadStaticMeshComponentNative(None, 0, None.size()).Lods == 0);
        // Colors taken out by the cook, colors kept and empty, and a file made for a server.
        for (const auto& Case : {Bytes(1, 1, 0), Bytes(1, 0, 0), Bytes(3, 1, 0)}) P4CheckCuts(Case, Reads);
        const auto Stripped = Bytes(1, 1, 0);
        const auto Component = ReadStaticMeshComponentNative(Stripped, 0, Stripped.size());
        Check(Component.Lods == 2); Check(Component.LightingIds.size() == 2);
        Check((Component.LightingIds[1] == std::array<std::uint32_t, 4>{{6, 7, 8, 9}})); Check(Component.End == Stripped.size());
        const auto Server = Bytes(3, 1, 0);
        Check(ReadStaticMeshComponentNative(Server, 0, Server.size()).LightingIds.empty());
        const auto Painted = Bytes(1, 0, 1);
        Check(ReadStaticMeshComponentNative(Painted, 0, Painted.size()).Error.Code == "painted-vertex-colors-not-decoded");
        const auto Flag = Bytes(1, 0, 2);
        Check(ReadStaticMeshComponentNative(Flag, 0, Flag.size()).Error.Code == "invalid-flag");
        const auto Strip = Bytes(4, 0, 0);
        Check(ReadStaticMeshComponentNative(Strip, 0, Strip.size()).Error.Code == "invalid-strip-flags");
    }
    {
        // An instanced component: one level of detail, two instances, drawn in 16-bit reals, a tree of one node.
        const auto Bytes = [](bool Cooked, bool Halves, std::size_t Drawn, std::uint64_t Said, bool Tree)
        {
            std::vector<std::uint8_t> C;
            P4Put(C, 0); P4Put(C, 1); P4Put(C, 1, 1); P4Put(C, 1, 1); for (std::uint64_t Part : {5u, 6u, 7u, 8u}) P4Put(C, Part);
            P4Put(C, Cooked ? 1 : 0);
            P4Put(C, 64); P4Put(C, 2); for (std::size_t Index = 0; Index < 32; ++Index) P4Put(C, 0x3f800000 + Index);
            P4Put(C, 4); P4Put(C, 2); P4Put(C, 0x40000000); P4Put(C, 0x40400000);
            if (Cooked)
            {
                const std::size_t Row = Halves ? 24 : 48, Size = Drawn ? 12 + 4 * 8 + Drawn * (16 + 8 + Row + 4) : 0;
                P4Put(C, Said == ~std::uint64_t(0) ? Size : Said, 8);
                if (Drawn)
                {
                    P4Put(C, Halves ? 1 : 0); P4Put(C, Drawn); P4Put(C, 1);
                    P4Put(C, 16); P4Put(C, Drawn); for (std::size_t Index = 0; Index < Drawn * 4; ++Index) P4Put(C, 0x41000000 + Index);
                    P4Put(C, 8); P4Put(C, Drawn); for (std::size_t Index = 0; Index < Drawn * 4; ++Index) P4Put(C, 0xfffe - Index, 2);
                    P4Put(C, Row); P4Put(C, Drawn);
                    for (std::size_t Index = 0; Index < Drawn * 12; ++Index) { if (Halves) P4Put(C, 0x3c00, 2); else P4Put(C, 0x3f800000); }
                    P4Put(C, 4); P4Put(C, Drawn); for (std::size_t Index = 0; Index < Drawn; ++Index) P4Put(C, 0x40a00000);
                }
            }
            if (Tree)
            {
                P4Put(C, 64); P4Put(C, 1);
                for (std::uint64_t Part : {0x3f800000u, 0x40000000u, 0x40400000u, 1u, 0x40800000u, 0x40a00000u, 0x40c00000u, 2u, 3u, 4u}) P4Put(C, Part);
                P4Zeros(C, 24);
            }
            return C;
        };
        const auto Tree = [](const std::vector<std::uint8_t>& B) { return ReadInstancedMeshNative(B, 0, B.size(), true).Error.Code.empty(); };
        const auto Flat = [](const std::vector<std::uint8_t>& B) { return ReadInstancedMeshNative(B, 0, B.size(), false).Error.Code.empty(); };
        const auto Whole = Bytes(true, true, 2, ~std::uint64_t(0), true);
        P4CheckCuts(Whole, Tree);
        P4CheckCuts(Bytes(true, false, 2, ~std::uint64_t(0), false), Flat);
        P4CheckCuts(Bytes(true, true, 0, ~std::uint64_t(0), true), Tree);
        P4CheckCuts(Bytes(false, true, 0, 0, false), Flat);
        const auto Read = ReadInstancedMeshNative(Whole, 0, Whole.size(), true);
        Check(Read.Mesh.Lods == 1); Check(Read.Cooked); Check(Read.Instances.size() == 2); Check(Read.Instances[1][0] == P4Real(0x3f800010));
        Check(Read.Custom.size() == 2); Check(Read.Custom[1] == 3.0f); Check(Read.DrawnBytes == 148); Check(Read.HalfReals); Check(Read.Drawn == 2);
        Check(Read.CustomPerInstance == 1); Check(Read.Origins.size() == 2); Check(Read.Origins[1][3] == P4Real(0x41000007));
        Check(Read.LightMaps.size() == 2); Check(Read.LightMaps[0][0] == -2); Check(Read.LightMaps[1][3] == -9);
        Check(Read.Rows.size() == 2); Check(Read.Rows[1][11] == 1.0f); Check(Read.DrawnCustom.size() == 2); Check(Read.DrawnCustom[1] == 5.0f);
        Check(Read.Clusters.size() == 1); Check(Read.Clusters[0].Min[2] == 3.0f); Check(Read.Clusters[0].FirstChild == 1); Check(Read.Clusters[0].Max[0] == 4.0f);
        Check(Read.Clusters[0].LastChild == 2); Check(Read.Clusters[0].FirstInstance == 3); Check(Read.Clusters[0].LastInstance == 4);
        Check(Read.End == Whole.size());
        // A tree is not read as none, a count that is not the instances' is refused, and so is a size that is not what follows.
        Check(!Flat(Whole));
        const auto Wrong = Bytes(true, true, 2, 147, true);
        Check(ReadInstancedMeshNative(Wrong, 0, Wrong.size(), true).Error.Code == "unexpected-drawn-size");
        // 16-bit reals: one, minus two, the smallest, nothing, and a half.
        Check(LevelNative::Half(0x3c00) == 1.0f); Check(LevelNative::Half(0xc000) == -2.0f); Check(LevelNative::Half(0x0001) == P4Real(0x33800000));
        Check(LevelNative::Half(0x0000) == 0.0f); Check(LevelNative::Half(0x3800) == 0.5f); Check(LevelNative::Half(0x7c00) == P4Real(0x7f800000));
    }
    {
        const auto Reads = [](const std::vector<std::uint8_t>& B) { return ReadFoliageActorNative(B, 0, B.size()).Error.Code.empty(); };
        const auto Bytes = [](std::uint64_t Kind)
        {
            std::vector<std::uint8_t> F;
            P4Put(F, 0); P4Put(F, 2);
            P4Put(F, P4Ref(-7)); P4Put(F, 1, 1); P4Put(F, 4);
            P4Put(F, P4Ref(-8)); P4Put(F, Kind, 1); P4Put(F, 5);
            return F;
        };
        std::vector<std::uint8_t> None; P4Put(None, 0); P4Put(None, 0);
        P4CheckCuts(None, Reads);
        P4CheckCuts(Bytes(1), Reads);
        const auto Two = Bytes(1);
        const auto Foliage = ReadFoliageActorNative(Two, 0, Two.size());
        Check(Foliage.Placed.size() == 2); Check(Foliage.Placed[1].Type == -8); Check(Foliage.Placed[1].Component == 5); Check(Foliage.End == Two.size());
        const auto Actors = Bytes(2);
        Check(ReadFoliageActorNative(Actors, 0, Actors.size()).Error.Code == "foliage-kind-not-decoded");
    }
    {
        const std::vector<std::string> Names{"None", "Engine", "Int64Property", "LatestSerialId", "ObjectProperty", "Tick"};
        const auto Reads = [&](const std::vector<std::uint8_t>& B) { return ReadGeneratedClassNative(B, Names, 0, B.size()).Error.Code.empty(); };
        const auto Bytes = P4ClassBytes(2, 0, false);
        P4CheckCuts(Bytes, Reads);
        const auto Class = ReadGeneratedClassNative(Bytes, Names, 0, Bytes.size());
        Check(Class.Super == -6); Check(Class.Children.empty()); Check(Class.Variables.size() == 1);
        Check(Class.Variables[0].Type == "Int64Property"); Check(Class.Variables[0].Name == "LatestSerialId");
        Check(Class.Variables[0].ObjectFlags == 0x00200001); Check(Class.Variables[0].ArrayDim == 1); Check(Class.Variables[0].ElementSize == 8);
        Check(Class.Variables[0].PropertyFlags == 0x10005); Check(Class.Variables[0].RepNotify == "None");
        Check(Class.ScriptBytes == 0); Check(Class.Functions.empty()); Check(Class.ClassFlags == 0x00840a14); Check(Class.Within == -3);
        Check(Class.ConfigName == "Engine"); Check(Class.GeneratedBy == 0); Check(Class.Interfaces == 0); Check(Class.Cooked);
        Check(Class.DefaultObject == 2); Check(Class.End == Bytes.size());
        // A class with a function, code and an interface is read through; what it holds is counted.
        const auto More = P4ClassBytes(2, 3, true);
        P4CheckCuts(More, Reads);
        const auto Coded = ReadGeneratedClassNative(More, Names, 0, More.size());
        Check(Coded.Children == std::vector<std::int32_t>({7})); Check(Coded.ScriptBytes == 3); Check(Coded.Interfaces == 1);
        Check(Coded.Functions.size() == 1); Check(Coded.Functions[0].first == "Tick"); Check(Coded.Functions[0].second == 7);
        const auto Other = P4ClassBytes(4, 0, false);
        const auto Refused = ReadGeneratedClassNative(Other, Names, 0, Other.size());
        Check(Refused.Error.Code == "class-variable-type-not-decoded"); Check(Refused.UndecodedType == "ObjectProperty");
        Check(Refused.UnsupportedVariable); Check(!Class.UnsupportedVariable);
        const auto NoName = P4ClassBytes(9, 0, false);
        Check(ReadGeneratedClassNative(NoName, Names, 0, NoName.size()).Error.Code == "invalid-name");
        Check(!ReadGeneratedClassNative(NoName, Names, 0, NoName.size()).UnsupportedVariable);
        auto Truncated = Other; Truncated.resize(4);
        Check(!ReadGeneratedClassNative(Truncated, Names, 0, Truncated.size()).UnsupportedVariable);
    }
}

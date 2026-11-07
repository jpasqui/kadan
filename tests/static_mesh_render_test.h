#pragma once
#include "core/static_mesh_render.h"

static void P4MeshPut(std::vector<std::uint8_t>& B, std::uint64_t N, std::size_t Width = 4)
{ for (std::size_t At = 0; At < Width; ++At) B.push_back(std::uint8_t(N >> (At * 8))); }
static void P4MeshFloat(std::vector<std::uint8_t>& B, float V)
{ std::uint32_t Bits = 0; std::memcpy(&Bits, &V, 4); P4MeshPut(B, Bits); }
static void P4MeshPatch(std::vector<std::uint8_t>& B, std::size_t Offset, std::uint64_t N, std::size_t Width = 4)
{ for (std::size_t At = 0; At < Width; ++At) B[Offset + At] = std::uint8_t(N >> (At * 8)); }
struct FP4StaticMeshFixture
{
    std::vector<std::uint8_t> Bytes, Sidecar;
    std::size_t LODCount = 0, Strip = 0, Section = 0, MaxDeviation = 0, CookedOut = 0, Inlined = 0;
    std::size_t Buffer = 0, PositionStride = 0, PositionCount = 0, Vertex = 0, TexCoords = 0, Precision = 0;
    std::size_t PositionArray = 0, TangentArray = 0, UVArray = 0, Color = 0, Index = 0, IndexArray = 0;
    std::size_t Sampler = 0, AliasCount = 0, Bulk = 0, Metadata = 0, DistanceStrip = 0, DistanceBox = 0;
    std::size_t InlinedCount = 0, Screen = 0, SpeedTree = 0, MaterialCount = 0, MaterialName = 0;
};
static FP4StaticMeshFixture P4StaticMeshFixture(bool High = false, bool FullUV = false,
    std::uint8_t Classes = 9, bool Bulk = false, bool Colors = true, bool Distance = false, bool ObjectGuid = false,
    bool Wireframe = false)
{
    FP4StaticMeshFixture F; auto& B = F.Bytes;
    const auto Put = [&](std::uint64_t N, std::size_t Width = 4) { P4MeshPut(B, N, Width); };
    const auto Float = [&](float V) { P4MeshFloat(B, V); };
    Put(ObjectGuid ? 1 : 0); if (ObjectGuid) for (auto Word : {0u, 0x80000000u, UINT32_MAX, 123u}) Put(Word);
    B.insert(B.end(), {1, 0}); Put(1); Put(2); Put(3);
    for (std::uint32_t Word = 4; Word < 8; ++Word) Put(Word);
    Put(1); Put(4); F.LODCount = B.size(); Put(1);
    F.Strip = B.size(); B.insert(B.end(), {1, Classes}); Put(1);
    F.Section = B.size(); for (auto N : {0u, 0u, 1u, 0u, 2u, 1u, 1u, 0u, 1u}) Put(N);
    F.MaxDeviation = B.size(); Float(.25f); F.CookedOut = B.size(); Put(0);
    F.Inlined = B.size(); Put(Bulk ? 0 : 1);
    const auto Start = B.size(); F.Buffer = Start;
    B.insert(B.end(), {std::uint8_t(Wireframe ? 0 : 1), Classes}); F.PositionStride = B.size(); Put(12);
    F.PositionCount = B.size(); Put(3); F.PositionArray = B.size(); Put(12); Put(3);
    for (const auto V : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, -0.f}) Float(V);
    F.Vertex = B.size(); B.insert(B.end(), {1, 0}); F.TexCoords = B.size(); Put(2); Put(3);
    F.Precision = B.size(); Put(FullUV ? 1 : 0); Put(High ? 1 : 0);
    F.TangentArray = B.size(); Put(High ? 16 : 8); Put(3);
    for (unsigned Vertex = 0; Vertex < 3; ++Vertex)
        for (const auto Value : {32767, 0, 0, -32768, 0, 0, 32767, Vertex == 2 ? -32767 : 32767})
            Put(High ? std::uint16_t(Value) : std::uint8_t(Value == -32768 ? -128 : Value / 258), High ? 2 : 1);
    F.UVArray = B.size(); Put(FullUV ? 8 : 4); Put(6);
    for (unsigned Vertex = 0; Vertex < 3; ++Vertex)
        for (const auto Half : {0u, 0x3c00u, 0x3800u, 0x8000u})
            if (FullUV) Float(Half == 0 ? 0.f : Half == 0x3c00 ? 1.f : Half == 0x3800 ? .5f : -0.f);
            else Put(Half, 2);
    F.Color = B.size(); B.insert(B.end(), {1, 0}); Put(Colors ? 4 : 0); Put(Colors ? 3 : 0);
    if (Colors) { Put(4); Put(3); for (unsigned Vertex = 0; Vertex < 3; ++Vertex) B.insert(B.end(), {10, 20, 30, 40}); }
    const auto Indices = [&]() { Put(0); Put(1); Put(6); Put(0, 2); Put(1, 2); Put(2, 2); Put(1); };
    F.Index = B.size(); F.IndexArray = B.size() + 4; Indices();
    if (!(Classes & 4)) Indices(); Indices(); if (!(Classes & 4)) Indices();
    if (Wireframe) { Put(0); Put(1); Put(4); Put(0, 2); Put(1, 2); Put(0); }
    if (!(Classes & 1)) { Put(0); Put(1); Put(0); Put(0); }
    if (!(Classes & 8)) { Put(1); Put(3); B.insert(B.end(), {11, 12, 13}); }
    F.Sampler = B.size(); Put(1); Float(1); F.AliasCount = B.size(); Put(1); Put(0); Float(.5f);
    Put(1); Float(1); Put(1); Put(0); Float(.5f);
    if (Bulk)
    {
        const std::vector<std::uint8_t> Data(B.begin() + Start, B.end()); B.resize(Start);
        F.Bulk = B.size(); Put(P4MapKit::BulkAtEnd | P4MapKit::BulkSeparate | P4MapKit::BulkNoFixup);
        Put(Data.size()); Put(Data.size()); Put(16, 8); F.Metadata = B.size();
        Put(1); Put((Classes & 4 ? 0 : 12) | 2 | (Colors ? 16 : 0) | (Classes & 8 ? 0 : 64) | (Wireframe ? 32 : 0));
        Put(2); Put(3); Put(FullUV ? 1 : 0); Put(High ? 1 : 0); Put(12); Put(3); Put(Colors ? 4 : 0); Put(Colors ? 3 : 0);
        for (auto Count : {3u, Classes & 4 ? 0u : 3u, 3u, Classes & 4 ? 0u : 3u, Wireframe ? 2u : 0u, 0u}) { Put(Count); Put(0); }
        F.Sidecar.resize(16); F.Sidecar.insert(F.Sidecar.end(), Data.begin(), Data.end());
    }
    Put(111); Put(22); Put(33); F.InlinedCount = B.size(); Put(Bulk ? 0 : 1, 1);
    F.DistanceStrip = B.size(); B.insert(B.end(), {1, 0}); Put(Distance ? 1 : 0);
    if (Distance)
    {
        Put(2); B.insert(B.end(), {50, 60}); for (auto N : {1u, 2u, 3u}) Put(N);
        for (auto V : {0.f, 0.f, 0.f, 1.f, 2.f, 3.f}) Float(V);
        F.DistanceBox = B.size(); Put(1, 1); Float(-1); Float(1); Put(1); Put(0); Put(0);
    }
    for (auto V : {0.f, 0.f, 0.f, 1.f, 2.f, 3.f, 4.f}) Float(V); Put(1);
    F.Screen = B.size(); for (unsigned Index = 0; Index < 8; ++Index) { Put(1); Float(1.f / (Index + 1)); }
    Put(0); F.SpeedTree = B.size(); Put(0); F.MaterialCount = B.size(); Put(2);
    for (unsigned Index = 0; Index < 2; ++Index)
    {
        Put(std::uint32_t(-5)); if (!Index) F.MaterialName = B.size(); Put(Index + 1); Put(Index);
        Put(1); Put(0); for (auto V : {1.f, 2.f, 3.f, 4.f}) Float(V);
    }
    return F;
}
static void CheckStaticMeshRenderReaders()
{
    using namespace P4MapKit;
    const std::vector<std::string> Names{"None", "SlotA", "SlotB"};
    const auto Read = [&](const FP4StaticMeshFixture& F, FStaticMeshRenderPayload& P, FReadError& E,
        const FStaticMeshRenderLimits& Limits = FStaticMeshRenderLimits{}, const std::function<bool()>& Cancel = std::function<bool()>{}) {
        FByteBulkContext Context; Context.FileSizes[0] = std::int64_t(F.Bytes.size());
        Context.FileSizes[1] = F.Sidecar.empty() ? -1 : std::int64_t(F.Sidecar.size());
        return ReadCookedStaticMeshRender(F.Bytes, 0, F.Bytes.size(), Names, EStaticMeshRenderContract::RetocUE426, Context,
            [&](EBulkSource Source, std::uint64_t At, std::uint8_t* Out, std::size_t Size) {
                if (Source != EBulkSource::Regular || At > F.Sidecar.size() || Size > F.Sidecar.size() - std::size_t(At)) return false;
                std::memcpy(Out, F.Sidecar.data() + std::size_t(At), Size); return true;
            }, P, E, Limits, Cancel);
    };
    for (const bool High : {false, true}) for (const bool FullUV : {false, true})
        for (const bool Bulk : {false, true}) for (const auto Classes : {0u, 1u, 4u, 8u, 9u, 13u})
        {
            const auto F = P4StaticMeshFixture(High, FullUV, std::uint8_t(Classes), Bulk, true, true, true);
            FStaticMeshRenderPayload P; FReadError E; Check(Read(F, P, E)); Check(E.Code.empty());
            Check(P.RenderDataDecoded); Check(!P.EditableAssetQualified && !P.OriginalDefaultsQualified && !P.DependenciesQualified);
            Check(P.HasObjectGuid && P.ObjectGuid[2] == UINT32_MAX); Check(P.ObjectGuid[1] == 0x80000000u);
            Check(P.BodySetupIndex == 2 && P.NavCollisionIndex == 3 && P.SocketIndices == std::vector<std::int32_t>{4});
            Check(P.LODs.size() == 1 && P.LODs[0].HasGeometry); const auto& L = P.LODs[0]; const auto& B = L.Buffers;
            Check(L.Inlined != Bulk); Check(L.MaxDeviation == .25f); Check(L.Sections[0].Collision && L.Sections[0].Shadow && L.Sections[0].RayTracing);
            Check(B.FullPrecisionUV == FullUV && B.HighPrecisionTangents == High && B.PositionValues.size() == 3 && B.TangentValues.size() == 3);
            Check(B.Main.Indices == std::vector<std::uint32_t>({0, 1, 2}) && B.Main.ExpandOnLoad);
            Check(B.PositionValues[1][0] == 1.f && std::signbit(B.PositionValues[2][2]));
            Check(B.TangentValues[0][0] == (High ? 32767 : 127)); Check(B.TangentValues[0][3] == (High ? -32768 : -128));
            Check(B.TangentValues[2][7] == (High ? -32767 : -127));
            Check(B.UVValues.size() == 2 && B.UVValues[0][0][1] == 1.f && B.UVValues[1][2][0] == .5f && std::signbit(B.UVValues[1][1][1]));
            Check((B.ColorRGBA == std::vector<std::array<std::uint8_t, 4>>(3, {{30, 20, 10, 40}})));
            Check(B.Samplers.size() == 2 && B.Samplers[0].Probabilities == std::vector<float>{1.f} && B.Samplers[1].Aliases == std::vector<std::int32_t>{0});
            Check(B.Reversed.Indices.size() == (Classes & 4 ? 0u : 3u)); Check(B.RayTracing.Bytes.size() == (Classes & 8 ? 0u : 3u));
            Check(P.DistanceFields.size() == 1 && P.DistanceFields[0].Present && P.DistanceFields[0].CompressedVolume.Bytes == std::vector<std::uint8_t>({50, 60}));
            Check(P.DistanceFields[0].Size[2] == 3 && P.DistanceFields[0].MinMax[0] == -1 && P.DistanceFields[0].Closed);
            Check(P.Materials.size() == 2 && P.Materials[0].Slot == "SlotA" && P.Materials[1].Slot == "SlotB_0" && P.Materials[0].ObjectIndex == -5);
            Check(P.End == F.Bytes.size() && P.ScreenSizes[0] == 1.f && P.ShareStaticLighting);
            if (Bulk) { Check(L.Bulk.Source == EBulkSource::Regular && L.Bulk.Offset == 16); Check(B.Positions.Span.Source == EBulkSource::Regular); }
            else { Check(B.Positions.Span.Source == EBulkSource::Body); Check(L.BufferSpan.Offset == F.Buffer); }
        }
    const auto Good = P4StaticMeshFixture();
    const auto Refuse = [&](const FP4StaticMeshFixture& F, const char* Code = nullptr,
        const FStaticMeshRenderLimits& Limits = FStaticMeshRenderLimits{}, const std::function<bool()>& Cancel = std::function<bool()>{}) {
        FStaticMeshRenderPayload P; P.End = 999; FReadError E; Check(!Read(F, P, E, Limits, Cancel));
        Check(!E.Code.empty()); Check(!Code || E.Code == Code); Check(P.End == 999 && P.LODs.empty() && !P.RenderDataDecoded);
    };
    for (std::size_t Size = 0; Size < Good.Bytes.size(); ++Size)
    { auto F = Good; F.Bytes.resize(Size); Refuse(F); }
    const auto Patch = [&](std::size_t Offset, std::uint64_t Value, const char* Code, std::size_t Width = 4) {
        auto F = Good; P4MeshPatch(F.Bytes, Offset, Value, Width); Refuse(F, Code);
    };
    Patch(0, 2, "static-mesh-invalid-bool"); Patch(4, 4, "static-mesh-strip-layout-unqualified", 1);
    Patch(6, 0, "static-mesh-uncooked-or-server-layout"); Patch(Good.LODCount, 0, "static-mesh-empty-lods");
    Patch(Good.LODCount, 9, "invalid-count"); Patch(Good.Strip + 1, 16, "static-mesh-strip-layout-unqualified", 1);
    Patch(Good.Strip + 1, 2, "static-mesh-stripped-lod-unqualified", 1);
    Patch(Good.CookedOut, 1, "static-mesh-missing-lod-geometry"); Patch(Good.MaxDeviation, 0x7fc00000, "static-mesh-nonfinite-float");
    Patch(Good.PositionStride, 8, "static-mesh-position-layout-mismatch"); Patch(Good.PositionCount, 0, "static-mesh-position-layout-mismatch");
    Patch(Good.PositionArray, 8, "static-mesh-array-layout-mismatch"); Patch(Good.PositionArray + 4, 2, "static-mesh-array-layout-mismatch");
    Patch(Good.TexCoords, 9, "static-mesh-vertex-layout-mismatch"); Patch(Good.Precision, 2, "static-mesh-invalid-bool");
    Patch(Good.TangentArray, 16, "static-mesh-array-layout-mismatch"); Patch(Good.UVArray + 4, 5, "static-mesh-array-layout-mismatch");
    Patch(Good.Color + 6, 2, "static-mesh-color-layout-mismatch"); Patch(Good.IndexArray + 4, 5, "static-mesh-index-layout-mismatch");
    Patch(Good.IndexArray + 8, 3, "static-mesh-index-out-of-bounds", 2);
    Patch(Good.Section + 4, 3, "static-mesh-section-layout-mismatch"); Patch(Good.Section + 8, 2, "static-mesh-section-out-of-bounds");
    Patch(Good.Section, 9, "static-mesh-section-material-out-of-bounds"); Patch(Good.AliasCount, 0, "static-mesh-sampler-count-mismatch");
    Patch(Good.InlinedCount, 2, "static-mesh-inline-lod-count-mismatch", 1);
    Patch(Good.Screen, 0, "static-mesh-screen-size-layout-unqualified"); Patch(Good.SpeedTree, 1, "static-mesh-speedtree-unqualified");
    Patch(Good.MaterialName, 99, "invalid-name"); Patch(Good.MaterialCount, 0, "static-mesh-section-material-out-of-bounds");
    // Independent safety cases: supported-size data must still refuse source
    // flags, precision values and section ranges that cannot represent a mesh.
    Patch(4, 0, "static-mesh-uncooked-or-server-layout", 1);
    Patch(Good.Strip, 0, "static-mesh-stripped-lod-unqualified", 1);
    Patch(Good.Strip + 2, 0, "static-mesh-stripped-lod-unqualified");
    Patch(Good.MaxDeviation, 0xbf800000, "static-mesh-missing-lod-geometry");
    Patch(Good.Buffer + 1, 8, "static-mesh-buffer-strip-mismatch", 1);
    Patch(Good.Buffer, 3, "static-mesh-buffer-global-strip-unqualified", 1);
    Patch(Good.Vertex, 3, "static-mesh-vertex-layout-mismatch", 1);
    Patch(Good.TexCoords + 4, 2, "static-mesh-vertex-layout-mismatch");
    Patch(Good.TexCoords, 0, "static-mesh-vertex-layout-mismatch");
    Patch(Good.Color, 3, "static-mesh-color-layout-mismatch", 1);
    Patch(Good.Color + 2, 8, "static-mesh-color-layout-mismatch");
    Patch(Good.PositionArray + 8, 0x7f800000, "static-mesh-nonfinite-float");
    Patch(Good.UVArray + 8, 0x7e00, "static-mesh-nonfinite-uv", 2);
    Patch(Good.Section, UINT32_MAX, "static-mesh-section-layout-mismatch");
    Patch(Good.Section + 8, 0, "static-mesh-section-layout-mismatch");
    Patch(Good.Section + 12, 3, "static-mesh-section-layout-mismatch");
    Patch(Good.Section + 16, 3, "static-mesh-section-layout-mismatch");
    Patch(Good.Section + 12, 1, "static-mesh-section-vertex-range-mismatch");
    Patch(Good.Section + 16, 1, "static-mesh-section-vertex-range-mismatch");
    Patch(Good.Screen - 8, 0xbf800000, "static-mesh-negative-bounds");
    Patch(Good.Screen + 4, 0xbf800000, "static-mesh-screen-size-layout-unqualified");
    auto Full = P4StaticMeshFixture(false, true);
    P4MeshPatch(Full.Bytes, Full.UVArray + 8, 0x7fc00000); Refuse(Full, "static-mesh-nonfinite-float");
    auto Wide = Good; std::vector<std::uint8_t> WideIndices;
    for (auto Index : {0u, 1u, 2u}) P4MeshPut(WideIndices, Index);
    Wide.Bytes.erase(Wide.Bytes.begin() + Good.IndexArray + 8, Wide.Bytes.begin() + Good.IndexArray + 14);
    Wide.Bytes.insert(Wide.Bytes.begin() + Good.IndexArray + 8, WideIndices.begin(), WideIndices.end());
    P4MeshPatch(Wide.Bytes, Good.Index, 1); P4MeshPatch(Wide.Bytes, Good.IndexArray + 4, 12);
    FStaticMeshRenderPayload WideP; FReadError WideE; Check(Read(Wide, WideP, WideE));
    Check(WideP.LODs[0].Buffers.Main.Wide && WideP.LODs[0].Buffers.Main.Indices == std::vector<std::uint32_t>({0, 1, 2}));
    P4MeshPatch(Wide.Bytes, Good.IndexArray + 8, 0x80000000u); Refuse(Wide, "static-mesh-index-out-of-bounds");
    auto F = Good; F.Bytes.push_back(0); Refuse(F, "static-mesh-unconsumed-native-data");
    auto Missing = P4StaticMeshFixture(false, false, 9, true); Missing.Sidecar.clear(); Refuse(Missing, "missing-bulk-source");
    auto Mismatch = P4StaticMeshFixture(false, false, 9, true); P4MeshPatch(Mismatch.Bytes, Mismatch.Metadata + 8, 3); Refuse(Mismatch, "static-mesh-bulk-metadata-mismatch");
    // Serialized metadata is an independent claim, not authority to overwrite
    // the actual bulk vertex/index buffers. Reject each contradictory field.
    const auto BulkGood = P4StaticMeshFixture(false, false, 9, true);
    for (auto FieldValue : {std::pair<std::size_t, std::uint32_t>{0, 2}, {4, 0}, {12, 2}, {16, 1}, {20, 1},
                           {24, 8}, {28, 2}, {32, 8}, {36, 2}})
    {
        auto Corrupt = BulkGood; P4MeshPatch(Corrupt.Bytes, Corrupt.Metadata + FieldValue.first, FieldValue.second);
        Refuse(Corrupt, "static-mesh-bulk-metadata-mismatch");
    }
    auto UnknownFlags = BulkGood; P4MeshPatch(UnknownFlags.Bytes, UnknownFlags.Metadata + 4, 128);
    Refuse(UnknownFlags, "static-mesh-availability-flags-unqualified");
    auto WrongIndexCount = BulkGood; P4MeshPatch(WrongIndexCount.Bytes, WrongIndexCount.Metadata + 40, 2);
    Refuse(WrongIndexCount, "static-mesh-bulk-index-metadata-mismatch");
    auto WrongIndexWidth = BulkGood; P4MeshPatch(WrongIndexWidth.Bytes, WrongIndexWidth.Metadata + 44, 1);
    Refuse(WrongIndexWidth, "static-mesh-bulk-index-metadata-mismatch");
    auto DamagedSidecar = BulkGood; P4MeshPatch(DamagedSidecar.Sidecar, 16, 3, 1);
    Refuse(DamagedSidecar, "static-mesh-buffer-global-strip-unqualified");
    for (std::size_t Size = BulkGood.Metadata; Size < BulkGood.Bytes.size(); ++Size)
    { auto Truncated = BulkGood; Truncated.Bytes.resize(Size); Refuse(Truncated); }
    const auto DistanceGood = P4StaticMeshFixture(false, false, 9, false, true, true, true);
    for (std::size_t Size = 0; Size < DistanceGood.Bytes.size(); ++Size)
    { auto Truncated = DistanceGood; Truncated.Bytes.resize(Size); Refuse(Truncated); }
    auto BadBox = DistanceGood; P4MeshPatch(BadBox.Bytes, BadBox.DistanceBox, 2, 1);
    Refuse(BadBox, "static-mesh-invalid-box-validity");
    auto BadDistanceStrip = Good; P4MeshPatch(BadDistanceStrip.Bytes, BadDistanceStrip.DistanceStrip, 0, 1);
    Refuse(BadDistanceStrip, "static-mesh-distance-strip-unqualified");
    auto StrippedDistance = Good; P4MeshPatch(StrippedDistance.Bytes, StrippedDistance.DistanceStrip + 1, 1, 1);
    StrippedDistance.Bytes.erase(StrippedDistance.Bytes.begin() + StrippedDistance.DistanceStrip + 2,
                               StrippedDistance.Bytes.begin() + StrippedDistance.DistanceStrip + 6);
    FStaticMeshRenderPayload DistanceP; FReadError DistanceE; Check(Read(StrippedDistance, DistanceP, DistanceE));
    Check(DistanceP.DistanceFields.empty());
    auto NoColor = P4StaticMeshFixture(false, false, 9, false, false); FStaticMeshRenderPayload P; FReadError E;
    Check(Read(NoColor, P, E)); Check(P.LODs[0].Buffers.ColorRGBA.empty() && P.LODs[0].Buffers.ColorVertices == 0);
    auto Wire = P4StaticMeshFixture(false, false, 9, true, true, false, false, true);
    Check(Read(Wire, P, E)); Check(P.LODs[0].Buffers.Wireframe.Indices == std::vector<std::uint32_t>({0, 1}));
    auto Two = Good; const std::vector<std::uint8_t> LOD(Good.Bytes.begin() + Good.Strip, Good.Bytes.begin() + Good.InlinedCount);
    Two.Bytes.insert(Two.Bytes.begin() + Good.InlinedCount, LOD.begin(), LOD.end());
    Two.Bytes.insert(Two.Bytes.begin() + Good.DistanceStrip + LOD.size() + 6, 4, 0);
    P4MeshPatch(Two.Bytes, Good.LODCount, 2); P4MeshPatch(Two.Bytes, Good.InlinedCount + LOD.size(), 2, 1);
    Check(Read(Two, P, E)); Check(P.LODs.size() == 2 && P.LODs[1].Buffers.PositionValues == P.LODs[0].Buffers.PositionValues);
    P4MeshPatch(Two.Bytes, Good.InlinedCount + Good.IndexArray + 8 - Good.Strip, 99, 2);
    Refuse(Two, "static-mesh-index-out-of-bounds");
    auto L = FStaticMeshRenderLimits{}; L.InputBytes = Good.Bytes.size() - 1; Refuse(Good, "static-mesh-contract-or-budget-unqualified", L);
    L = {}; L.CopiedBytes = 1; Refuse(Good, "static-mesh-copy-limit", L); L = {}; L.Vertices = 2; Refuse(Good, "static-mesh-position-layout-mismatch", L);
    L = {}; L.Sections = 0; Refuse(Good, "invalid-count", L); L = {}; L.Materials = 0; Refuse(Good, "invalid-count", L);
    Refuse(Good, "static-mesh-cancelled", {}, []() { return true; });
    unsigned CompleteCalls = 0; Check(Read(Good, P, E, {}, [&]() { ++CompleteCalls; return false; }));
    for (unsigned Stop = 1; Stop <= CompleteCalls; ++Stop)
    { unsigned Calls = 0; Refuse(Good, "static-mesh-cancelled", {}, [&]() { return ++Calls >= Stop; }); }
    Check(StaticMeshRenderDetail::Half(0x0001) == std::ldexp(1.f, -24));
    Check(StaticMeshRenderDetail::Half(0x7bff) == 65504.f); Check(std::signbit(StaticMeshRenderDetail::Half(0x8000)));
    Check(std::isnan(StaticMeshRenderDetail::Half(0x7e00))); Check(std::isinf(StaticMeshRenderDetail::Half(0xfc00)));
}

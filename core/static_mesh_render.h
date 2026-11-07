// Exact surviving UE4.26 cooked StaticMesh native render payload.
// Caller binds the package/version/property-end and source custody separately.
// Derived render data never qualifies original defaults, dependencies or assets.
#pragma once
#include "bulk_data.h"
#include <cmath>

namespace P4MapKit
{
enum class EStaticMeshRenderContract { RetocUE426 = 1 };
struct FStaticMeshByteSpan
{
    EBulkSource Source = EBulkSource::Body;
    std::uint64_t Offset = 0, Size = 0;
};
struct FStaticMeshArray
{
    std::uint32_t ElementSize = 0;
    std::size_t Count = 0;
    FStaticMeshByteSpan Span;
    std::vector<std::uint8_t> Bytes;
};
struct FStaticMeshIndexBuffer
{
    bool Wide = false, ExpandOnLoad = false;
    FStaticMeshArray Data;
    std::vector<std::uint32_t> Indices;
};
struct FStaticMeshSampler
{
    std::vector<float> Probabilities;
    std::vector<std::int32_t> Aliases;
    float TotalWeight = 0;
};
struct FStaticMeshSection
{
    std::int32_t MaterialIndex = 0;
    std::uint32_t FirstIndex = 0, Triangles = 0, MinVertex = 0, MaxVertex = 0;
    bool Collision = false, Shadow = false, ForceOpaque = false, RayTracing = false;
};
struct FStaticMeshBuffers
{
    std::array<std::uint8_t, 2> Strip{}, VertexStrip{}, ColorStrip{};
    std::uint32_t Vertices = 0, TexCoords = 0, PositionStride = 0, ColorStride = 0, ColorVertices = 0;
    bool FullPrecisionUV = false, HighPrecisionTangents = false;
    FStaticMeshArray Positions, Tangents, UV, Colors, RayTracing;
    FStaticMeshIndexBuffer Main, Reversed, DepthOnly, ReversedDepthOnly, Wireframe, Adjacency;
    std::vector<FStaticMeshSampler> Samplers;
    // Exactly decoded original packed values; no normalization/welding/defaults.
    std::vector<std::array<float, 3>> PositionValues;
    std::vector<std::array<std::int16_t, 8>> TangentValues;
    std::vector<std::vector<std::array<float, 2>>> UVValues;
    std::vector<std::array<std::uint8_t, 4>> ColorRGBA;
};
struct FStaticMeshAvailability
{
    std::uint32_t DepthTriangles = 0, Flags = 0, TexCoords = 0, Vertices = 0;
    std::uint32_t PositionStride = 0, PositionVertices = 0, ColorStride = 0, ColorVertices = 0;
    bool FullPrecisionUV = false, HighPrecisionTangents = false;
    std::array<std::uint32_t, 6> IndexCounts{};
    std::array<bool, 6> IndexWide{};
};
struct FStaticMeshLOD
{
    std::array<std::uint8_t, 2> Strip{};
    std::vector<FStaticMeshSection> Sections;
    float MaxDeviation = 0;
    bool CookedOut = false, Inlined = false, HasGeometry = false;
    FByteBulkRange Bulk;
    FStaticMeshByteSpan BufferSpan;
    FStaticMeshAvailability Availability;
    std::array<std::uint32_t, 3> BufferSizes{};
    FStaticMeshBuffers Buffers;
};
struct FStaticMeshDistanceField
{
    bool Present = false, BoxValid = false, Closed = false, TwoSided = false, Plane = false;
    FStaticMeshArray CompressedVolume;
    std::array<std::int32_t, 3> Size{};
    std::array<float, 6> Bounds{};
    std::array<float, 2> MinMax{};
};
struct FStaticMeshMaterial
{
    std::int32_t ObjectIndex = 0;
    std::string Slot;
    bool UVInitialized = false, UVOverride = false;
    std::array<float, 4> UVDensities{};
};
struct FStaticMeshRenderPayload
{
    EStaticMeshRenderContract Contract = EStaticMeshRenderContract::RetocUE426;
    std::size_t Start = 0, End = 0, CopiedBytes = 0;
    bool HasObjectGuid = false, ShareStaticLighting = false, HasOccluder = false;
    std::array<std::uint32_t, 4> ObjectGuid{}, LightingGuid{};
    std::array<std::uint8_t, 2> Strip{}, DistanceFieldStrip{};
    std::int32_t BodySetupIndex = 0, NavCollisionIndex = 0;
    std::vector<std::int32_t> SocketIndices;
    std::vector<FStaticMeshLOD> LODs;
    std::uint8_t InlinedLODs = 0;
    std::vector<FStaticMeshDistanceField> DistanceFields;
    std::array<float, 7> Bounds{};
    std::array<float, 8> ScreenSizes{};
    FStaticMeshArray OccluderVertices, OccluderIndices;
    std::vector<FStaticMeshMaterial> Materials;
    bool RenderDataDecoded = false, OriginalDefaultsQualified = false;
    bool DependenciesQualified = false, EditableAssetQualified = false;
};
struct FStaticMeshRenderLimits
{
    std::size_t InputBytes = 64u * 1024u * 1024u, CopiedBytes = 128u * 1024u * 1024u;
    std::size_t Vertices = 1000000, Indices = 6000000, Sections = 1024, Materials = 4096;
    std::size_t SamplerEntries = 6000000, Sockets = 1024;
};
using FStaticMeshBulkRead = std::function<bool(EBulkSource, std::uint64_t, std::uint8_t*, std::size_t)>;
namespace StaticMeshRenderDetail
{
struct FState
{
    const FStaticMeshRenderLimits& Limits;
    const std::function<bool()>& Cancel;
    std::size_t Copied = 0;
    bool Continue(FByteReader& R) const { return !Cancel || !Cancel() || R.Reject("static-mesh-cancelled"); }
    bool Charge(FByteReader& R, std::size_t Count, std::size_t Width = 1)
    {
        if (Width && Count > (Limits.CopiedBytes - std::min(Copied, Limits.CopiedBytes)) / Width)
            return R.Reject("static-mesh-copy-limit");
        Copied += Count * Width; return true;
    }
};
struct FArchive
{
    const std::vector<std::uint8_t>& Bytes;
    FByteReader R;
    FState& State;
    EBulkSource Source;
    std::uint64_t SourceOffset;
    FArchive(const std::vector<std::uint8_t>& Data, std::size_t Start, std::size_t End, FState& S,
        EBulkSource Kind = EBulkSource::Body, std::uint64_t Offset = 0)
        : Bytes(Data), R(Data, Start, End), State(S), Source(Kind), SourceOffset(Offset) {}
    bool Bool(bool& Value)
    { std::uint32_t N = 0; if (!R.U32(N)) return false; if (N > 1) return R.Reject("static-mesh-invalid-bool"); Value = N != 0; return true; }
    bool Byte(std::uint8_t& Value)
    { std::uint64_t N = 0; if (!R.Unsigned(1, N)) return false; Value = std::uint8_t(N); return true; }
    bool Float(float& Value)
    {
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559, "IEEE float32 required");
        std::uint32_t Bits = 0; if (!R.U32(Bits)) return false;
        std::memcpy(&Value, &Bits, 4);
        return std::isfinite(Value) || R.Reject("static-mesh-nonfinite-float");
    }
    bool Strip(std::array<std::uint8_t, 2>& Value, std::uint8_t Classes)
    {
        if (!Byte(Value[0]) || !Byte(Value[1])) return false;
        return (!(Value[0] & ~3u) && !(Value[1] & ~Classes)) || R.Reject("static-mesh-strip-layout-unqualified");
    }
    bool Copy(FStaticMeshArray& Out, std::uint32_t Width, std::size_t Count)
    {
        if (Width == 0 || Count > std::numeric_limits<std::size_t>::max() / Width)
            return R.Reject("static-mesh-array-overflow");
        const auto Size = Count * Width, At = R.Tell();
        if (!State.Charge(R, Count, Width) || !R.Skip(Size)) return false;
        Out.ElementSize = Width; Out.Count = Count; Out.Span = {Source, SourceOffset + At, Size};
        Out.Bytes.assign(Bytes.begin() + At, Bytes.begin() + At + Size); return true;
    }
    bool Array(FStaticMeshArray& Out, std::uint32_t Width, std::size_t Maximum,
        std::size_t ExactCount = std::numeric_limits<std::size_t>::max())
    {
        std::uint32_t Size = 0; std::size_t Count = 0;
        if (!R.U32(Size) || !R.Count(Count, Maximum)) return false;
        if (Size != Width || (ExactCount != std::numeric_limits<std::size_t>::max() && Count != ExactCount))
            return R.Reject("static-mesh-array-layout-mismatch");
        return Copy(Out, Size, Count);
    }
    bool Index(FStaticMeshIndexBuffer& Out)
    {
        if (!Bool(Out.Wide) || !Array(Out.Data, 1, State.Limits.Indices * 4)) return false;
        const auto Width = Out.Wide ? 4u : 2u;
        if (Out.Data.Count % Width || Out.Data.Count / Width > State.Limits.Indices)
            return R.Reject("static-mesh-index-layout-mismatch");
        if (!Bool(Out.ExpandOnLoad)) return false;
        if (!State.Charge(R, Out.Data.Count / Width, sizeof(std::uint32_t))) return false;
        FByteReader I(Out.Data.Bytes); std::uint64_t Value = 0;
        while (I.Tell() < I.Limit())
        {
            if ((Out.Indices.size() & 4095) == 0 && !State.Continue(R)) return false;
            if (!I.Unsigned(Width, Value)) { R.Error = I.Error; return false; }
            Out.Indices.push_back(std::uint32_t(Value));
        }
        return true;
    }
    bool Sampler(FStaticMeshSampler& Out)
    {
        std::size_t Count = 0, Aliases = 0;
        if (!R.Count(Count, State.Limits.SamplerEntries) || !State.Charge(R, Count, sizeof(float))) return false;
        for (std::size_t At = 0; At < Count; ++At)
        {
            if ((At & 4095) == 0 && !State.Continue(R)) return false;
            float P = 0; if (!Float(P)) return false; Out.Probabilities.push_back(P);
        }
        if (!R.Count(Aliases, State.Limits.SamplerEntries)) return false;
        if (Count != Aliases) return R.Reject("static-mesh-sampler-count-mismatch");
        if (!State.Charge(R, Count, sizeof(std::int32_t))) return false;
        for (std::size_t At = 0; At < Count; ++At)
        {
            if ((At & 4095) == 0 && !State.Continue(R)) return false;
            std::int32_t Alias = 0; if (!R.I32(Alias)) return false; Out.Aliases.push_back(Alias);
        }
        return Float(Out.TotalWeight);
    }
};
inline float Half(std::uint16_t Bits)
{
    const auto Mantissa = Bits & 1023u, Exponent = (Bits >> 10) & 31u;
    const float Magnitude = Exponent == 0 ? std::ldexp(float(Mantissa), -24) :
        Exponent == 31 ? (Mantissa ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity()) :
        std::ldexp(float(1024u + Mantissa), int(Exponent) - 25);
    return Bits & 0x8000u ? -Magnitude : Magnitude;
}
inline bool Decode(FArchive& A, FStaticMeshBuffers& B)
{
    const auto NV = std::size_t(B.Vertices), NT = std::size_t(B.TexCoords);
    if (!A.State.Charge(A.R, NV, sizeof(std::array<float, 3>) + sizeof(std::array<std::int16_t, 8>)) ||
        !A.State.Charge(A.R, NV * NT, sizeof(std::array<float, 2>)) ||
        !A.State.Charge(A.R, B.ColorVertices, sizeof(std::array<std::uint8_t, 4>))) return false;
    B.UVValues.resize(NT);
    FArchive P(B.Positions.Bytes, 0, B.Positions.Bytes.size(), A.State),
        T(B.Tangents.Bytes, 0, B.Tangents.Bytes.size(), A.State), U(B.UV.Bytes, 0, B.UV.Bytes.size(), A.State);
    for (std::size_t Vertex = 0; Vertex < NV; ++Vertex)
    {
        if ((Vertex & 4095) == 0 && !A.State.Continue(A.R)) return false;
        std::array<float, 3> Position{}; std::array<std::int16_t, 8> Tangent{};
        for (auto& V : Position) if (!P.Float(V)) { A.R.Error = P.R.Error; return false; }
        for (auto& V : Tangent)
        {
            std::uint64_t Bits = 0; if (!T.R.Unsigned(B.HighPrecisionTangents ? 2 : 1, Bits)) { A.R.Error = T.R.Error; return false; }
            const auto Maximum = B.HighPrecisionTangents ? 32767u : 127u, Mask = B.HighPrecisionTangents ? 65535u : 255u;
            V = Bits <= Maximum ? std::int16_t(Bits) : std::int16_t(-1 - std::int32_t(Mask - Bits));
        }
        B.PositionValues.push_back(Position); B.TangentValues.push_back(Tangent);
        for (std::size_t Channel = 0; Channel < NT; ++Channel)
        {
            std::array<float, 2> UV{};
            for (auto& V : UV)
            {
                if (B.FullPrecisionUV) { if (!U.Float(V)) { A.R.Error = U.R.Error; return false; } }
                else { std::uint64_t Bits = 0; if (!U.R.Unsigned(2, Bits)) { A.R.Error = U.R.Error; return false; } V = Half(std::uint16_t(Bits)); }
                if (!std::isfinite(V)) return A.R.Reject("static-mesh-nonfinite-uv");
            }
            B.UVValues[Channel].push_back(UV);
        }
        if (B.ColorVertices)
        { const auto At = Vertex * 4; B.ColorRGBA.push_back({{B.Colors.Bytes[At + 2], B.Colors.Bytes[At + 1], B.Colors.Bytes[At], B.Colors.Bytes[At + 3]}}); }
    }
    return true;
}
inline bool Buffers(FArchive& A, std::uint8_t ClassFlags, std::size_t Sections, FStaticMeshBuffers& B)
{
    if (!A.Strip(B.Strip, 15) || B.Strip[1] != ClassFlags) return A.R.Reject("static-mesh-buffer-strip-mismatch");
    if (B.Strip[0] & 2) return A.R.Reject("static-mesh-buffer-global-strip-unqualified");
    if (!A.R.U32(B.PositionStride) || !A.R.U32(B.Vertices)) return false;
    if (B.PositionStride != 12 || B.Vertices == 0 || B.Vertices > A.State.Limits.Vertices)
        return A.R.Reject("static-mesh-position-layout-mismatch");
    if (!A.Array(B.Positions, 12, A.State.Limits.Vertices, B.Vertices) || !A.Strip(B.VertexStrip, 0) ||
        !A.R.U32(B.TexCoords)) return false;
    std::uint32_t Vertices = 0;
    if (!A.R.U32(Vertices) || !A.Bool(B.FullPrecisionUV) || !A.Bool(B.HighPrecisionTangents)) return false;
    if ((B.VertexStrip[0] & 2) || Vertices != B.Vertices || !B.TexCoords || B.TexCoords > 8)
        return A.R.Reject("static-mesh-vertex-layout-mismatch");
    if (!A.Array(B.Tangents, B.HighPrecisionTangents ? 16 : 8, A.State.Limits.Vertices, B.Vertices) ||
        !A.Array(B.UV, B.FullPrecisionUV ? 8 : 4, A.State.Limits.Vertices * 8, std::size_t(B.Vertices) * B.TexCoords) ||
        !A.Strip(B.ColorStrip, 0) || !A.R.U32(B.ColorStride) || !A.R.U32(B.ColorVertices)) return false;
    if ((B.ColorStrip[0] & 2) || (B.ColorVertices && (B.ColorStride != 4 || B.ColorVertices != B.Vertices)))
        return A.R.Reject("static-mesh-color-layout-mismatch");
    if (B.ColorVertices && !A.Array(B.Colors, 4, A.State.Limits.Vertices, B.ColorVertices)) return false;
    if (!A.Index(B.Main)) return false;
    if (!(ClassFlags & 4) && !A.Index(B.Reversed)) return false;
    if (!A.Index(B.DepthOnly)) return false;
    if (!(ClassFlags & 4) && !A.Index(B.ReversedDepthOnly)) return false;
    if (!(B.Strip[0] & 1) && !A.Index(B.Wireframe)) return false;
    if (!(ClassFlags & 1) && !A.Index(B.Adjacency)) return false;
    if (!(ClassFlags & 8) && !A.Array(B.RayTracing, 1, A.State.Limits.InputBytes)) return false;
    for (std::size_t Index = 0; Index <= Sections; ++Index)
    { FStaticMeshSampler S; if (!A.Sampler(S)) return false; B.Samplers.push_back(std::move(S)); }
    return Decode(A, B);
}
inline bool Availability(FArchive& A, FStaticMeshAvailability& M)
{
    if (!A.R.U32(M.DepthTriangles) || !A.R.U32(M.Flags) || !A.R.U32(M.TexCoords) || !A.R.U32(M.Vertices) ||
        !A.Bool(M.FullPrecisionUV) || !A.Bool(M.HighPrecisionTangents) || !A.R.U32(M.PositionStride) ||
        !A.R.U32(M.PositionVertices) || !A.R.U32(M.ColorStride) || !A.R.U32(M.ColorVertices)) return false;
    if (M.Flags & ~127u) return A.R.Reject("static-mesh-availability-flags-unqualified");
    for (std::size_t Index = 0; Index < 6; ++Index)
        if (!A.R.U32(M.IndexCounts[Index]) || !A.Bool(M.IndexWide[Index])) return false;
    return true;
}
inline bool MatchAvailability(FArchive& A, const FStaticMeshAvailability& M, const FStaticMeshBuffers& B)
{
    const std::uint32_t Flags = (!B.Adjacency.Indices.empty() ? 1u : 0u) | (!B.DepthOnly.Indices.empty() ? 2u : 0u) |
        (!B.Reversed.Indices.empty() ? 4u : 0u) | (!B.ReversedDepthOnly.Indices.empty() ? 8u : 0u) |
        (B.ColorVertices ? 16u : 0u) | (!B.Wireframe.Indices.empty() ? 32u : 0u) | (!B.RayTracing.Bytes.empty() ? 64u : 0u);
    if (M.TexCoords != B.TexCoords || M.Vertices != B.Vertices || M.FullPrecisionUV != B.FullPrecisionUV ||
        M.HighPrecisionTangents != B.HighPrecisionTangents || M.PositionStride != B.PositionStride ||
        M.PositionVertices != B.Vertices || M.ColorStride != B.ColorStride || M.ColorVertices != B.ColorVertices ||
        M.DepthTriangles != B.DepthOnly.Indices.size() / 3 || M.Flags != Flags) return A.R.Reject("static-mesh-bulk-metadata-mismatch");
    const FStaticMeshIndexBuffer* Buffers[] = {&B.Main, &B.Reversed, &B.DepthOnly, &B.ReversedDepthOnly, &B.Wireframe, &B.Adjacency};
    for (std::size_t Index = 0; Index < 6; ++Index)
        if (M.IndexCounts[Index] != Buffers[Index]->Indices.size() ||
            (M.IndexCounts[Index] && M.IndexWide[Index] != Buffers[Index]->Wide))
            return A.R.Reject("static-mesh-bulk-index-metadata-mismatch");
    return true;
}
inline bool ValidateLOD(FArchive& A, const FStaticMeshLOD& L)
{
    const auto& B = L.Buffers;
    const FStaticMeshIndexBuffer* Buffers[] = {&B.Main, &B.Reversed, &B.DepthOnly, &B.ReversedDepthOnly, &B.Wireframe, &B.Adjacency};
    for (const auto* Buffer : Buffers) for (std::size_t At = 0; At < Buffer->Indices.size(); ++At)
    {
        if ((At & 4095) == 0 && !A.State.Continue(A.R)) return false;
        if (Buffer->Indices[At] >= B.Vertices) return A.R.Reject("static-mesh-index-out-of-bounds");
    }
    if (B.Main.Indices.empty() || B.Main.Indices.size() % 3) return A.R.Reject("static-mesh-triangle-index-count");
    std::uint64_t End = 0;
    for (const auto& Section : L.Sections)
    {
        if (Section.MaterialIndex < 0 || Section.FirstIndex != End || !Section.Triangles || Section.MinVertex > Section.MaxVertex ||
            Section.MaxVertex >= B.Vertices) return A.R.Reject("static-mesh-section-layout-mismatch");
        End += std::uint64_t(Section.Triangles) * 3;
        if (End > B.Main.Indices.size()) return A.R.Reject("static-mesh-section-out-of-bounds");
        for (std::size_t At = Section.FirstIndex; At < End; ++At)
        {
            if ((At & 4095) == 0 && !A.State.Continue(A.R)) return false;
            if (B.Main.Indices[At] < Section.MinVertex || B.Main.Indices[At] > Section.MaxVertex)
                return A.R.Reject("static-mesh-section-vertex-range-mismatch");
        }
    }
    return End == B.Main.Indices.size() || A.R.Reject("static-mesh-section-index-gap");
}
}

// Explicit contract refers only to retoc's version-zero UE4.26 cooked layout.
// Bulk offsets need independently qualified Context; no plausible-offset guessing.
// ReadAt may access only caller-held immutable sidecars. Body reads use Body.
// Every refusal/cancellation leaves Out intact, including a later LOD failure.
inline bool ReadCookedStaticMeshRender(const std::vector<std::uint8_t>& Body,
    std::size_t Start, std::size_t End, const std::vector<std::string>& Names,
    EStaticMeshRenderContract Contract, const FByteBulkContext& Context,
    const FStaticMeshBulkRead& ReadAt, FStaticMeshRenderPayload& Out, FReadError& Error,
    const FStaticMeshRenderLimits& Limits = {}, const std::function<bool()>& Cancel = {})
{
    using namespace StaticMeshRenderDetail;
    Error = {}; FState State{Limits, Cancel, 0}; FArchive A(Body, Start, End, State);
    const auto Fail = [&]() { Error = A.R.Error; return false; };
    if (!A.R.Error.Code.empty()) return Fail();
    if (Contract != EStaticMeshRenderContract::RetocUE426 || Body.size() > Limits.InputBytes ||
        End - Start > Limits.InputBytes || !Limits.Vertices || Limits.Vertices > 1000000 ||
        !Limits.Indices || Limits.Indices > 6000000 || Limits.Sections > 1024 || Limits.Materials > 4096 ||
        Limits.Sockets > 1024 || Limits.SamplerEntries > 6000000)
    { A.R.Reject("static-mesh-contract-or-budget-unqualified"); return Fail(); }
    if (!State.Continue(A.R)) return Fail();
    FStaticMeshRenderPayload P; P.Start = Start; P.Contract = Contract;
    if (!A.Bool(P.HasObjectGuid)) return Fail();
    if (P.HasObjectGuid) for (auto& Word : P.ObjectGuid) if (!A.R.U32(Word)) return Fail();
    bool Cooked = false;
    if (!A.Strip(P.Strip, 0) || !A.Bool(Cooked)) return Fail();
    if (P.Strip[0] != 1 || !Cooked) { A.R.Reject("static-mesh-uncooked-or-server-layout"); return Fail(); }
    if (!A.R.I32(P.BodySetupIndex) || !A.R.I32(P.NavCollisionIndex)) return Fail();
    for (auto& Word : P.LightingGuid) if (!A.R.U32(Word)) return Fail();
    std::size_t Count = 0;
    if (!A.R.Count(Count, Limits.Sockets) || !State.Charge(A.R, Count, sizeof(std::int32_t))) return Fail();
    for (std::size_t Index = 0; Index < Count; ++Index)
    { std::int32_t Reference = 0; if (!A.R.I32(Reference)) return Fail(); P.SocketIndices.push_back(Reference); }
    if (!A.R.Count(Count, 8)) return Fail();
    if (!Count) { A.R.Reject("static-mesh-empty-lods"); return Fail(); }
    for (std::size_t Index = 0; Index < Count; ++Index)
    {
        if (!State.Continue(A.R)) return Fail();
        FStaticMeshLOD L; std::size_t Sections = 0;
        if (!A.Strip(L.Strip, 15) || !A.R.Count(Sections, Limits.Sections) ||
            !State.Charge(A.R, Sections, sizeof(FStaticMeshSection))) return Fail();
        if (L.Strip[0] != 1 || (L.Strip[1] & 2) || !Sections)
        { A.R.Reject("static-mesh-stripped-lod-unqualified"); return Fail(); }
        for (std::size_t Section = 0; Section < Sections; ++Section)
        {
            FStaticMeshSection S;
            if (!A.R.I32(S.MaterialIndex) || !A.R.U32(S.FirstIndex) || !A.R.U32(S.Triangles) || !A.R.U32(S.MinVertex) ||
                !A.R.U32(S.MaxVertex) || !A.Bool(S.Collision) || !A.Bool(S.Shadow) || !A.Bool(S.ForceOpaque) || !A.Bool(S.RayTracing)) return Fail();
            L.Sections.push_back(S);
        }
        if (!A.Float(L.MaxDeviation) || !A.Bool(L.CookedOut) || !A.Bool(L.Inlined)) return Fail();
        if (L.CookedOut || L.MaxDeviation < 0) { A.R.Reject("static-mesh-missing-lod-geometry"); return Fail(); }
        if (L.Inlined)
        {
            const auto At = A.R.Tell();
            if (!Buffers(A, L.Strip[1], Sections, L.Buffers)) return Fail();
            L.BufferSpan = {EBulkSource::Body, At, A.R.Tell() - At};
        }
        else
        {
            if (!ReadByteBulk(A.R, Context, L.Bulk) || !Availability(A, L.Availability)) return Fail();
            if (L.Bulk.Source == EBulkSource::None || !L.Bulk.Size)
            { A.R.Reject("static-mesh-missing-bulk-geometry"); return Fail(); }
            if (L.Bulk.Size > Limits.InputBytes) { A.R.Reject("static-mesh-bulk-byte-limit"); return Fail(); }
            if (!State.Charge(A.R, std::size_t(L.Bulk.Size))) return Fail();
            std::vector<std::uint8_t> Data; Data.reserve(std::size_t(L.Bulk.Size)); FReadError BulkError;
            const auto Read = [&](EBulkSource Kind, std::uint64_t Offset, std::uint8_t* To, std::size_t Size) {
                if (Kind != EBulkSource::Body) return ReadAt && ReadAt(Kind, Offset, To, Size);
                if (Offset > Body.size() || Size > Body.size() - std::size_t(Offset)) return false;
                std::memcpy(To, Body.data() + std::size_t(Offset), Size); return true;
            };
            if (!StreamByteBulk(L.Bulk, Read, [&](const std::uint8_t* From, std::size_t Size) {
                Data.insert(Data.end(), From, From + Size); return true;
            }, Cancel, BulkError)) { A.R.Error = BulkError; return Fail(); }
            FArchive External(Data, 0, Data.size(), State, L.Bulk.Source, L.Bulk.Offset);
            if (!Buffers(External, L.Strip[1], Sections, L.Buffers)) { A.R.Error = External.R.Error; return Fail(); }
            if (External.R.Tell() != External.R.Limit()) { A.R.Reject("static-mesh-unconsumed-bulk-data"); return Fail(); }
            if (!MatchAvailability(A, L.Availability, L.Buffers)) return Fail();
            L.BufferSpan = {L.Bulk.Source, L.Bulk.Offset, L.Bulk.Size};
        }
        for (auto& Size : L.BufferSizes) if (!A.R.U32(Size)) return Fail();
        if (!ValidateLOD(A, L)) return Fail();
        L.HasGeometry = true; P.LODs.push_back(std::move(L));
    }
    if (!A.Byte(P.InlinedLODs) || !A.Strip(P.DistanceFieldStrip, 1)) return Fail();
    std::size_t InlineCount = 0; bool InlineSeen = false;
    for (const auto& L : P.LODs)
    {
        if (L.Inlined) { ++InlineCount; InlineSeen = true; }
        else if (InlineSeen) { A.R.Reject("static-mesh-inline-lod-order-mismatch"); return Fail(); }
    }
    if (P.InlinedLODs != InlineCount) { A.R.Reject("static-mesh-inline-lod-count-mismatch"); return Fail(); }
    if (P.DistanceFieldStrip[0] != 1) { A.R.Reject("static-mesh-distance-strip-unqualified"); return Fail(); }
    if (!(P.DistanceFieldStrip[1] & 1))
    {
        for (std::size_t Index = 0; Index < P.LODs.size(); ++Index)
        {
            FStaticMeshDistanceField D; if (!A.Bool(D.Present)) return Fail();
            if (D.Present)
            {
                std::size_t Size = 0; if (!A.R.Count(Size, Limits.InputBytes) || !A.Copy(D.CompressedVolume, 1, Size)) return Fail();
                for (auto& V : D.Size) if (!A.R.I32(V)) return Fail();
                for (auto& V : D.Bounds) if (!A.Float(V)) return Fail();
                std::uint8_t Valid = 0; if (!A.Byte(Valid)) return Fail();
                if (Valid > 1) { A.R.Reject("static-mesh-invalid-box-validity"); return Fail(); } D.BoxValid = Valid != 0;
                for (auto& V : D.MinMax) if (!A.Float(V)) return Fail();
                if (!A.Bool(D.Closed) || !A.Bool(D.TwoSided) || !A.Bool(D.Plane)) return Fail();
            }
            P.DistanceFields.push_back(std::move(D));
        }
    }
    for (auto& V : P.Bounds) if (!A.Float(V)) return Fail();
    for (std::size_t Index = 3; Index < 7; ++Index)
        if (P.Bounds[Index] < 0) { A.R.Reject("static-mesh-negative-bounds"); return Fail(); }
    if (!A.Bool(P.ShareStaticLighting)) return Fail();
    for (auto& V : P.ScreenSizes)
    {
        bool CookedValue = false; if (!A.Bool(CookedValue) || !A.Float(V)) return Fail();
        if (!CookedValue || V < 0) { A.R.Reject("static-mesh-screen-size-layout-unqualified"); return Fail(); }
    }
    if (!A.Bool(P.HasOccluder)) return Fail();
    if (P.HasOccluder && (!A.Array(P.OccluderVertices, 12, Limits.Vertices) || !A.Array(P.OccluderIndices, 2, Limits.Indices))) return Fail();
    bool SpeedTree = false; if (!A.Bool(SpeedTree)) return Fail();
    if (SpeedTree) { A.R.Reject("static-mesh-speedtree-unqualified"); return Fail(); }
    if (!A.R.Count(Count, Limits.Materials) || !State.Charge(A.R, Count, sizeof(FStaticMeshMaterial))) return Fail();
    for (std::size_t Index = 0; Index < Count; ++Index)
    {
        FStaticMeshMaterial M;
        if (!State.Continue(A.R) || !A.R.I32(M.ObjectIndex) || !A.R.Name(Names, M.Slot) || !State.Charge(A.R, M.Slot.size()) ||
            !A.Bool(M.UVInitialized) || !A.Bool(M.UVOverride)) return Fail();
        for (auto& V : M.UVDensities) if (!A.Float(V)) return Fail();
        P.Materials.push_back(std::move(M));
    }
    for (const auto& L : P.LODs) for (const auto& S : L.Sections)
        if (std::size_t(S.MaterialIndex) >= P.Materials.size()) { A.R.Reject("static-mesh-section-material-out-of-bounds"); return Fail(); }
    if (A.R.Tell() != A.R.Limit()) { A.R.Reject("static-mesh-unconsumed-native-data"); return Fail(); }
    if (!State.Continue(A.R)) return Fail();
    P.End = A.R.Tell(); P.CopiedBytes = State.Copied; P.RenderDataDecoded = true;
    Out = std::move(P); return true;
}
}

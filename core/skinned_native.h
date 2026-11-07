// The native data of the game's cooked skeletons, skinned meshes and animation sequences: what follows
// an object's tagged properties in a UE4.26 package as retoc writes it (version-zero legacy form).
// Every byte between Start and End is read or the read is refused. Nothing here judges whether an
// editable asset can be made of what was read.
#pragma once
#include "package_reader.h"
#include <cmath>
#include <cstdio>
#include <functional>

namespace P4MapKit
{
// A bone's place: rotation (x, y, z, w), translation, scale, as the file has them.
using FSkinnedPose = std::array<float, 10>;
struct FSkinnedBone { std::string Name; std::int32_t Parent = -1; FSkinnedPose Pose{}; };
struct FSkinnedBytes { std::uint32_t ElementSize = 0, Count = 0; std::vector<std::uint8_t> Bytes; };

struct FSkeletonNative
{
    struct FRetargetSource { std::string Name, PoseName; std::vector<FSkinnedPose> Pose; };
    struct FCurve { std::string Name; bool Material = false, MorphTarget = false; std::vector<std::string> LinkedBones; std::uint8_t MaxLOD = 255; };
    struct FNameMapping { std::string Name; std::vector<FCurve> Curves; };
    std::vector<FSkinnedBone> Bones;
    std::vector<FRetargetSource> RetargetSources;
    std::array<std::uint32_t, 4> Guid{};
    std::vector<FNameMapping> SmartNames;
    std::array<std::uint8_t, 2> Strip{};
};

struct FSkinnedMaterial
{
    std::int32_t Object = 0;
    std::string Slot, ImportedSlot;
    bool HasImportedSlot = false, UVInitialized = false, UVOverride = false;
    std::array<float, 4> UVDensities{};
};
struct FSkinnedSection
{
    std::array<std::uint8_t, 2> Strip{};
    std::uint16_t Material = 0;
    std::uint32_t BaseIndex = 0, Triangles = 0, BaseVertex = 0;
    bool RecomputeTangent = false, CastShadow = false, Disabled = false;
    std::uint8_t RecomputeTangentMask = 0;
    std::vector<std::uint16_t> BoneMap;
    std::int32_t Vertices = 0, MaxBoneInfluences = 0, ClothLod = -1;
    std::int16_t ClothAsset = -1;
    std::array<std::uint32_t, 4> ClothGuid{};
    // For each vertex of the section, how many other vertices lie where it does and where their list
    // starts (DuplicateIndex, eight bytes a vertex), and the lists (DuplicateVertices).
    FSkinnedBytes DuplicateVertices, DuplicateIndex;
};
struct FSkinnedLOD
{
    std::array<std::uint8_t, 2> Strip{}, StreamStrip{}, VertexStrip{}, WeightStrip{}, LookupStrip{}, ColorStrip{};
    bool CookedOut = false, Inlined = false;
    std::vector<std::uint16_t> RequiredBones, ActiveBones;
    std::vector<FSkinnedSection> Sections;
    std::uint32_t BuffersSize = 0;
    std::uint8_t IndexSize = 0, AdjacencyIndexSize = 0;
    FSkinnedBytes Indices, Positions, Tangents, UVs, Weights, Lookup, Colors, Adjacency;
    std::uint32_t PositionStride = 0, Vertices = 0, TexCoords = 0, TangentVertices = 0;
    bool FullPrecisionUVs = false, HighPrecisionTangents = false;
    bool VariableBones = false, WideBoneIndices = false, HasColors = false, HasAdjacency = false;
    std::uint32_t MaxBoneInfluences = 0, WeightBones = 0, WeightVertices = 0, LookupVertices = 0;
    std::uint32_t ColorStride = 0, ColorVertices = 0;
    // How many bytes of the file the buffers take, and how many of them the adjacency indices.
    std::size_t StreamBytes = 0, AdjacencyBytes = 0;
};
struct FSkeletalMeshNative
{
    std::array<std::uint8_t, 2> Strip{};
    std::array<float, 7> Bounds{};
    std::vector<FSkinnedMaterial> Materials;
    std::vector<FSkinnedBone> Bones;
    std::vector<FSkinnedLOD> LODs;
    std::uint8_t InlinedLODs = 0, NonOptionalLODs = 0;
};

struct FAnimSequenceNative
{
    std::array<std::uint32_t, 4> SkeletonGuid{};
    std::array<std::uint8_t, 2> Strip{};
    std::int32_t RawSize = 0, Frames = 0, ByteStreamSize = 0, TrackOffsets = 0, ScaleOffsets = 0, ScaleStrip = 0;
    std::vector<std::int32_t> TrackMap;
    std::vector<std::string> CurveNames;
    std::vector<std::uint8_t> Stream, CurveBytes;
    std::string BoneCodec, CurveCodec;
    std::uint8_t KeyEncoding = 0, TranslationFormat = 0, RotationFormat = 0, ScaleFormat = 0;
};

// One thing a file says of an object's native data. Blob, when given, is a buffer said by its size and
// digest. Made is set on what a cook works out for its platform or a compressor chooses, which two
// cooks of one asset need not agree on.
struct FNativeLine { std::string Text; const std::vector<std::uint8_t>* Blob = nullptr; bool Made = false; };

namespace SkinnedDetail
{
struct FIn
{
    const std::vector<std::uint8_t>& Body;
    const std::vector<std::string>& Names;
    FByteReader R;
    FIn(const std::vector<std::uint8_t>& B, std::size_t Start, std::size_t End, const std::vector<std::string>& N) : Body(B), Names(N), R(B, Start, End) {}
    bool Bool(bool& Value)
    { std::uint32_t N = 0; if (!R.U32(N)) return false; if (N > 1) return R.Reject("skinned-invalid-bool"); Value = N != 0; return true; }
    bool Byte(std::uint8_t& Value) { std::uint64_t N = 0; if (!R.Unsigned(1, N)) return false; Value = std::uint8_t(N); return true; }
    bool U16(std::uint16_t& Value) { std::uint64_t N = 0; if (!R.Unsigned(2, N)) return false; Value = std::uint16_t(N); return true; }
    bool Float(float& Value)
    {
        std::uint32_t Bits = 0; if (!R.U32(Bits)) return false;
        std::memcpy(&Value, &Bits, 4);
        return std::isfinite(Value) || R.Reject("skinned-nonfinite-float");
    }
    bool Strip(std::array<std::uint8_t, 2>& Value) { return Byte(Value[0]) && Byte(Value[1]); }
    bool Guid(std::array<std::uint32_t, 4>& Value) { for (auto& Word : Value) if (!R.U32(Word)) return false; return true; }
    bool Raw(std::vector<std::uint8_t>& Out, std::size_t Size)
    {
        const auto At = R.Tell();
        if (!R.Skip(Size)) return false;
        Out.assign(Body.begin() + At, Body.begin() + At + Size); return true;
    }
    // An array the engine writes in one piece: the size of an element, how many, the bytes.
    bool Bulk(FSkinnedBytes& Out, std::uint32_t Width, std::size_t Maximum = 64u * 1024u * 1024u)
    {
        std::size_t Count = 0;
        if (!R.U32(Out.ElementSize) || !R.Count(Count, Maximum)) return false;
        if (Out.ElementSize != Width) return R.Reject("skinned-array-element-size");
        Out.Count = std::uint32_t(Count);
        return Raw(Out.Bytes, Count * Width);
    }
    // An array the engine writes element by element: how many, then each.
    bool Plain(FSkinnedBytes& Out, std::uint32_t Width, std::size_t Maximum = 16u * 1024u * 1024u)
    {
        std::size_t Count = 0;
        if (!R.Count(Count, Maximum)) return false;
        Out.ElementSize = Width; Out.Count = std::uint32_t(Count);
        return Raw(Out.Bytes, Count * Width);
    }
    bool Words(std::vector<std::uint16_t>& Out)
    {
        std::size_t Count = 0; if (!R.Count(Count, 65536)) return false;
        Out.resize(Count);
        for (auto& Value : Out) if (!U16(Value)) return false;
        return true;
    }
    bool Pose(FSkinnedPose& Out) { for (auto& Value : Out) if (!Float(Value)) return false; return true; }
    // A reference skeleton: the bones, their places, and the map from name to index, which has to be
    // the bones' own order.
    bool Reference(std::vector<FSkinnedBone>& Bones)
    {
        std::size_t Count = 0, Poses = 0, Mapped = 0;
        if (!R.Count(Count, 65536)) return false;
        Bones.resize(Count);
        for (std::size_t Index = 0; Index < Count; ++Index)
        {
            auto& Bone = Bones[Index];
            if (!R.Name(Names, Bone.Name) || !R.I32(Bone.Parent)) return false;
            if (Bone.Parent < -1 || Bone.Parent >= std::int32_t(Index) || (Index == 0) != (Bone.Parent == -1)) return R.Reject("skinned-bone-parent");
        }
        if (!R.Count(Poses, 65536)) return false;
        if (Poses != Count) return R.Reject("skinned-bone-pose-count");
        for (auto& Bone : Bones) if (!Pose(Bone.Pose)) return false;
        if (!R.Count(Mapped, 65536)) return false;
        if (Mapped != Count) return R.Reject("skinned-bone-map-count");
        for (std::size_t Index = 0; Index < Count; ++Index)
        {
            std::string Name; std::int32_t Value = 0;
            if (!R.Name(Names, Name) || !R.I32(Value)) return false;
            if (Name != Bones[Index].Name || Value != std::int32_t(Index)) return R.Reject("skinned-bone-map-order");
        }
        return true;
    }
    bool End() { return R.Tell() == R.Limit() || R.Reject("skinned-unread-native-data"); }
};
inline std::string Number(float Value) { char Text[40]; std::snprintf(Text, sizeof Text, "%.9g", double(Value)); return Text; }
inline std::string Hex(std::uint32_t Value) { char Text[12]; std::snprintf(Text, sizeof Text, "%08x", Value); return Text; }
inline std::string GuidText(const std::array<std::uint32_t, 4>& Value) { return Hex(Value[0]) + Hex(Value[1]) + Hex(Value[2]) + Hex(Value[3]); }
inline std::string PoseText(const FSkinnedPose& Pose)
{ std::string Out; for (const float Value : Pose) Out += (Out.empty() ? "" : " ") + Number(Value); return Out; }
template <class T> std::string List(const std::vector<T>& Values)
{ std::string Out; for (const auto& Value : Values) Out += (Out.empty() ? "" : " ") + std::to_string(Value); return Out; }
inline void Bones(const std::vector<FSkinnedBone>& In, std::vector<FNativeLine>& Out)
{
    Out.push_back({"bones = " + std::to_string(In.size())});
    for (std::size_t Index = 0; Index < In.size(); ++Index)
        Out.push_back({"bone " + std::to_string(Index) + " = " + In[Index].Name + ", parent " + std::to_string(In[Index].Parent) + ", at " + PoseText(In[Index].Pose)});
}
}

inline bool ReadSkeletonNative(const std::vector<std::uint8_t>& Body, std::size_t Start, std::size_t End,
    const std::vector<std::string>& Names, FSkeletonNative& Out, FReadError& Error)
{
    SkinnedDetail::FIn A(Body, Start, End, Names); FSkeletonNative S; bool HasGuid = false;
    const auto Fail = [&]() { Error = A.R.Error; return false; };
    if (!A.R.Error.Code.empty() || !A.Bool(HasGuid)) return Fail();
    if (HasGuid) { A.R.Reject("skinned-object-guid"); return Fail(); }
    if (!A.Reference(S.Bones)) return Fail();
    std::size_t Count = 0;
    if (!A.R.Count(Count, 4096)) return Fail();
    S.RetargetSources.resize(Count);
    for (auto& Source : S.RetargetSources)
    {
        std::size_t Poses = 0;
        if (!A.R.Name(Names, Source.Name) || !A.R.Name(Names, Source.PoseName) || !A.R.Count(Poses, 65536)) return Fail();
        Source.Pose.resize(Poses);
        for (auto& Pose : Source.Pose) if (!A.Pose(Pose)) return Fail();
    }
    if (!A.Guid(S.Guid) || !A.R.Count(Count, 64)) return Fail();
    S.SmartNames.resize(Count);
    for (auto& Mapping : S.SmartNames)
    {
        std::size_t Curves = 0;
        if (!A.R.Name(Names, Mapping.Name) || !A.R.Count(Curves, 65536)) return Fail();
        Mapping.Curves.resize(Curves);
        for (auto& Curve : Mapping.Curves)
        {
            std::size_t Linked = 0;
            if (!A.R.Name(Names, Curve.Name) || !A.Bool(Curve.Material) || !A.Bool(Curve.MorphTarget) || !A.R.Count(Linked, 65536)) return Fail();
            Curve.LinkedBones.resize(Linked);
            for (auto& Bone : Curve.LinkedBones) if (!A.R.Name(Names, Bone)) return Fail();
            if (!A.Byte(Curve.MaxLOD)) return Fail();
        }
    }
    if (!A.Strip(S.Strip)) return Fail();
    // Without the editor's data stripped, the names of the skeleton's sync markers follow.
    if (S.Strip[0] != 1 || S.Strip[1] != 0) { A.R.Reject("skinned-skeleton-not-cooked"); return Fail(); }
    if (!A.End()) return Fail();
    Out = std::move(S); return true;
}
inline std::vector<FNativeLine> SkeletonLines(const FSkeletonNative& S)
{
    using namespace SkinnedDetail;
    std::vector<FNativeLine> Out;
    Bones(S.Bones, Out);
    Out.push_back({"retarget sources = " + std::to_string(S.RetargetSources.size())});
    for (const auto& Source : S.RetargetSources)
    {
        Out.push_back({"retarget source " + Source.Name + " = pose " + Source.PoseName + ", " + std::to_string(Source.Pose.size()) + " bones"});
        for (std::size_t Index = 0; Index < Source.Pose.size(); ++Index)
            Out.push_back({"retarget source " + Source.Name + " bone " + std::to_string(Index) + " at " + PoseText(Source.Pose[Index])});
    }
    Out.push_back({"skeleton id = " + GuidText(S.Guid)});
    Out.push_back({"name mappings = " + std::to_string(S.SmartNames.size())});
    for (std::size_t Mapping = 0; Mapping < S.SmartNames.size(); ++Mapping)
    {
        const auto& M = S.SmartNames[Mapping];
        Out.push_back({"name mapping " + std::to_string(Mapping) + " = " + M.Name + ", " + std::to_string(M.Curves.size()) + " curves"});
        for (std::size_t Index = 0; Index < M.Curves.size(); ++Index)
        {
            const auto& C = M.Curves[Index]; std::string Linked;
            for (const auto& Bone : C.LinkedBones) Linked += " " + Bone;
            Out.push_back({"name mapping " + M.Name + " curve " + std::to_string(Index) + " = " + C.Name + (C.Material ? ", material" : "") +
                (C.MorphTarget ? ", morph target" : "") + ", up to level of detail " + std::to_string(C.MaxLOD) + ", bones" + (Linked.empty() ? " none" : Linked)});
        }
    }
    Out.push_back({"strip flags = " + std::to_string(S.Strip[0]) + " " + std::to_string(S.Strip[1])});
    return Out;
}

// VertexColors is the mesh's bHasVertexColors: the file has a color buffer in each level of detail only then.
inline bool ReadSkeletalMeshNative(const std::vector<std::uint8_t>& Body, std::size_t Start, std::size_t End,
    const std::vector<std::string>& Names, bool VertexColors, FSkeletalMeshNative& Out, FReadError& Error)
{
    SkinnedDetail::FIn A(Body, Start, End, Names); FSkeletalMeshNative M; bool HasGuid = false, Cooked = false;
    const auto Fail = [&]() { Error = A.R.Error; return false; };
    const auto Refuse = [&](const char* Code) { A.R.Reject(Code); Error = A.R.Error; return false; };
    if (!A.R.Error.Code.empty() || !A.Bool(HasGuid)) return Fail();
    if (HasGuid) return Refuse("skinned-object-guid");
    if (!A.Strip(M.Strip)) return Fail();
    if (M.Strip[0] != 1 || M.Strip[1] != 0) return Refuse("skinned-mesh-not-cooked");
    for (auto& Value : M.Bounds) if (!A.Float(Value)) return Fail();
    std::size_t Count = 0;
    if (!A.R.Count(Count, 4096)) return Fail();
    M.Materials.resize(Count);
    for (auto& Material : M.Materials)
    {
        if (!A.R.I32(Material.Object) || !A.R.Name(Names, Material.Slot) || !A.Bool(Material.HasImportedSlot)) return Fail();
        if (Material.HasImportedSlot && !A.R.Name(Names, Material.ImportedSlot)) return Fail();
        if (!A.Bool(Material.UVInitialized) || !A.Bool(Material.UVOverride)) return Fail();
        for (auto& Value : Material.UVDensities) if (!A.Float(Value)) return Fail();
    }
    if (!A.Reference(M.Bones) || !A.Bool(Cooked)) return Fail();
    if (!Cooked) return Refuse("skinned-mesh-without-render-data");
    if (!A.R.Count(Count, 8)) return Fail();
    if (!Count) return Refuse("skinned-mesh-without-levels-of-detail");
    M.LODs.resize(Count);
    for (auto& L : M.LODs)
    {
        std::size_t Sections = 0;
        if (!A.Strip(L.Strip) || !A.Bool(L.CookedOut) || !A.Bool(L.Inlined) || !A.Words(L.RequiredBones)) return Fail();
        // Class flags: 1 the adjacency indices are left out, 2 the level of detail is below the platform's first.
        if (L.Strip[0] != 1 || (L.Strip[1] & ~1u) || L.CookedOut || !L.Inlined) return Refuse("skinned-level-of-detail-not-inline");
        if (!A.R.Count(Sections, 4096)) return Fail();
        if (!Sections) return Refuse("skinned-level-of-detail-without-sections");
        L.Sections.resize(Sections);
        for (auto& S : L.Sections)
        {
            std::size_t Cloth = 0;
            if (!A.Strip(S.Strip) || !A.U16(S.Material) || !A.R.U32(S.BaseIndex) || !A.R.U32(S.Triangles) || !A.Bool(S.RecomputeTangent) ||
                !A.Byte(S.RecomputeTangentMask) || !A.Bool(S.CastShadow) || !A.R.U32(S.BaseVertex) || !A.R.Count(Cloth, 0) || !A.Words(S.BoneMap) ||
                !A.R.I32(S.Vertices) || !A.R.I32(S.MaxBoneInfluences)) return Fail();
            std::uint16_t ClothAsset = 0;
            if (!A.U16(ClothAsset) || !A.Guid(S.ClothGuid) || !A.R.I32(S.ClothLod)) return Fail();
            S.ClothAsset = std::int16_t(ClothAsset);
            // Class flag 1: the table of vertices that share a place is left out.
            if (S.Strip[0] != 1 || S.Strip[1] != 0 || S.Vertices <= 0 || S.Material >= M.Materials.size()) return Refuse("skinned-section-layout");
            if (!A.Plain(S.DuplicateVertices, 4) || !A.Plain(S.DuplicateIndex, 8)) return Fail();
            if (S.DuplicateIndex.Count != std::uint32_t(S.Vertices) || !S.DuplicateVertices.Count) return Refuse("skinned-section-duplicate-table");
            if (!A.Bool(S.Disabled)) return Fail();
        }
        if (!A.Words(L.ActiveBones) || !A.R.U32(L.BuffersSize)) return Fail();
        const auto StreamAt = A.R.Tell();
        if (!A.Strip(L.StreamStrip) || !A.Byte(L.IndexSize)) return Fail();
        if (L.StreamStrip[0] != 1 || L.StreamStrip[1] != L.Strip[1] || (L.IndexSize != 2 && L.IndexSize != 4)) return Refuse("skinned-buffer-layout");
        if (!A.Bulk(L.Indices, L.IndexSize) || !A.R.U32(L.PositionStride) || !A.R.U32(L.Vertices)) return Fail();
        if (L.PositionStride != 12 || !L.Vertices) return Refuse("skinned-position-layout");
        if (!A.Bulk(L.Positions, 12) || !A.Strip(L.VertexStrip) || !A.R.U32(L.TexCoords) || !A.R.U32(L.TangentVertices) ||
            !A.Bool(L.FullPrecisionUVs) || !A.Bool(L.HighPrecisionTangents)) return Fail();
        if (L.Positions.Count != L.Vertices || L.VertexStrip[0] != 1 || L.VertexStrip[1] != 0 || L.TangentVertices != L.Vertices || !L.TexCoords || L.TexCoords > 4)
            return Refuse("skinned-vertex-layout");
        if (!A.Bulk(L.Tangents, L.HighPrecisionTangents ? 16 : 8) || !A.Bulk(L.UVs, L.FullPrecisionUVs ? 8 : 4)) return Fail();
        if (L.Tangents.Count != L.Vertices || L.UVs.Count != L.Vertices * L.TexCoords) return Refuse("skinned-vertex-count");
        if (!A.Strip(L.WeightStrip) || !A.Bool(L.VariableBones) || !A.R.U32(L.MaxBoneInfluences) || !A.R.U32(L.WeightBones) || !A.R.U32(L.WeightVertices) ||
            !A.Bool(L.WideBoneIndices)) return Fail();
        if (L.WeightStrip[0] != 1 || L.WeightStrip[1] != 0 || L.VariableBones || L.WideBoneIndices || L.WeightVertices != L.Vertices ||
            (L.MaxBoneInfluences != 4 && L.MaxBoneInfluences != 8) || L.WeightBones != L.Vertices * L.MaxBoneInfluences) return Refuse("skinned-weight-layout");
        // A vertex's weights: its bone indices, one byte each, then its weights, one byte each.
        if (!A.Bulk(L.Weights, 1) || !A.Strip(L.LookupStrip) || !A.R.U32(L.LookupVertices)) return Fail();
        // Where each vertex's weights start is kept only when vertices have different numbers of them.
        if (L.Weights.Count != L.WeightBones * 2 || L.LookupStrip[0] != 1 || L.LookupStrip[1] != 0 || L.LookupVertices != 0) return Refuse("skinned-weight-count");
        if (!A.Bulk(L.Lookup, 4)) return Fail();
        if (L.Lookup.Count != 0) return Refuse("skinned-weight-lookup-count");
        L.HasColors = VertexColors;
        if (VertexColors)
        {
            if (!A.Strip(L.ColorStrip) || !A.R.U32(L.ColorStride) || !A.R.U32(L.ColorVertices)) return Fail();
            if (L.ColorStrip[0] != 1 || L.ColorStrip[1] != 0 || (L.ColorVertices && (L.ColorStride != 4 || L.ColorVertices != L.Vertices))) return Refuse("skinned-color-layout");
            if (L.ColorVertices && !A.Bulk(L.Colors, 4)) return Fail();
        }
        L.HasAdjacency = !(L.Strip[1] & 1);
        if (L.HasAdjacency)
        {
            const auto At = A.R.Tell();
            if (!A.Byte(L.AdjacencyIndexSize)) return Fail();
            if (L.AdjacencyIndexSize != 2 && L.AdjacencyIndexSize != 4) return Refuse("skinned-adjacency-layout");
            if (!A.Bulk(L.Adjacency, L.AdjacencyIndexSize)) return Fail();
            L.AdjacencyBytes = A.R.Tell() - At;
        }
        std::size_t Profiles = 0;
        if (!A.R.Count(Profiles, 0)) return Fail();
        L.StreamBytes = A.R.Tell() - StreamAt;
        if (L.BuffersSize != L.StreamBytes) return Refuse("skinned-buffer-size");
        // Every index names a vertex, and the sections lie one after another in both buffers.
        const std::size_t Indices = L.Indices.Count; std::uint64_t IndexEnd = 0, VertexEnd = 0;
        for (const auto& S : L.Sections)
        {
            if (S.BaseIndex != IndexEnd || S.BaseVertex != VertexEnd) return Refuse("skinned-section-order");
            IndexEnd += std::uint64_t(S.Triangles) * 3; VertexEnd += std::uint64_t(S.Vertices);
        }
        if (IndexEnd != Indices || VertexEnd != L.Vertices) return Refuse("skinned-section-coverage");
        for (std::size_t At = 0; At < Indices; ++At)
        {
            std::uint32_t Value = 0;
            std::memcpy(&Value, L.Indices.Bytes.data() + At * L.IndexSize, L.IndexSize);
            if (Value >= L.Vertices) return Refuse("skinned-index-out-of-bounds");
        }
    }
    std::size_t Legacy = 0;
    if (!A.Byte(M.InlinedLODs) || !A.Byte(M.NonOptionalLODs) || !A.R.Count(Legacy, 0) || !A.End()) return Fail();
    Out = std::move(M); return true;
}
// Reference, when given, says what an object index of the file is.
inline std::vector<FNativeLine> SkeletalMeshLines(const FSkeletalMeshNative& M, const std::function<std::string(std::int32_t)>& Reference = {})
{
    using namespace SkinnedDetail;
    std::vector<FNativeLine> Out;
    std::string Bounds;
    for (const float Value : M.Bounds) Bounds += (Bounds.empty() ? "" : " ") + Number(Value);
    Out.push_back({"bounds = " + Bounds});
    Out.push_back({"material slots = " + std::to_string(M.Materials.size())});
    for (std::size_t Index = 0; Index < M.Materials.size(); ++Index)
    {
        const auto& S = M.Materials[Index];
        Out.push_back({"material slot " + std::to_string(Index) + " = " + S.Slot + ", " + (Reference ? Reference(S.Object) : std::to_string(S.Object)) +
            (S.HasImportedSlot ? ", imported as " + S.ImportedSlot : "") + ", densities " + (S.UVInitialized ? "set" : "not set") + (S.UVOverride ? " by hand" : "") +
            " " + Number(S.UVDensities[0]) + " " + Number(S.UVDensities[1]) + " " + Number(S.UVDensities[2]) + " " + Number(S.UVDensities[3])});
    }
    Bones(M.Bones, Out);
    Out.push_back({"levels of detail = " + std::to_string(M.LODs.size()) + ", " + std::to_string(M.InlinedLODs) + " in the file, " + std::to_string(M.NonOptionalLODs) + " always there"});
    for (std::size_t Index = 0; Index < M.LODs.size(); ++Index)
    {
        const auto& L = M.LODs[Index]; const std::string At = "level of detail " + std::to_string(Index) + " ";
        Out.push_back({At + "required bones = " + List(L.RequiredBones)});
        Out.push_back({At + "active bones = " + List(L.ActiveBones)});
        Out.push_back({At + "sections = " + std::to_string(L.Sections.size())});
        for (std::size_t Section = 0; Section < L.Sections.size(); ++Section)
        {
            const auto& S = L.Sections[Section]; const std::string Here = At + "section " + std::to_string(Section) + " ";
            Out.push_back({Here + "= material " + std::to_string(S.Material) + ", triangles " + std::to_string(S.Triangles) + " from index " + std::to_string(S.BaseIndex) +
                ", vertices " + std::to_string(S.Vertices) + " from " + std::to_string(S.BaseVertex) + ", up to " + std::to_string(S.MaxBoneInfluences) + " bones a vertex" +
                (S.CastShadow ? ", casts a shadow" : "") + (S.RecomputeTangent ? ", tangents recomputed" : "") + (S.Disabled ? ", disabled" : "") +
                ", tangent mask " + std::to_string(S.RecomputeTangentMask) + ", cloth " + std::to_string(S.ClothAsset) + " " + GuidText(S.ClothGuid) + " " + std::to_string(S.ClothLod)});
            Out.push_back({Here + "bones = " + List(S.BoneMap)});
            Out.push_back({Here + "vertices at one place, the lists", &S.DuplicateVertices.Bytes});
            Out.push_back({Here + "vertices at one place, the index", &S.DuplicateIndex.Bytes});
        }
        Out.push_back({At + "vertices = " + std::to_string(L.Vertices) + ", " + std::to_string(L.TexCoords) + " texture coordinates" + (L.FullPrecisionUVs ? " of full precision" : "") +
            (L.HighPrecisionTangents ? ", tangents of high precision" : "") + ", " + std::to_string(L.MaxBoneInfluences) + " weights a vertex, indices of " +
            std::to_string(L.IndexSize) + " bytes" + (L.HasColors ? ", colors of " + std::to_string(L.ColorVertices) + " vertices" : ", no colors")});
        Out.push_back({At + "indices", &L.Indices.Bytes});
        Out.push_back({At + "positions", &L.Positions.Bytes});
        Out.push_back({At + "tangents", &L.Tangents.Bytes});
        Out.push_back({At + "texture coordinates", &L.UVs.Bytes});
        Out.push_back({At + "weights", &L.Weights.Bytes});
        if (L.ColorVertices) Out.push_back({At + "colors", &L.Colors.Bytes});
        Out.push_back({At + "buffer bytes without adjacency indices = " + std::to_string(L.StreamBytes - L.AdjacencyBytes)});
        // Whether a cook keeps the indices tessellation uses is the platform's choice.
        Out.push_back({At + "adjacency indices " + (L.HasAdjacency ? "kept, " + std::to_string(L.AdjacencyBytes) + " bytes" : "left out"), nullptr, true});
        if (L.HasAdjacency) Out.push_back({At + "adjacency indices", &L.Adjacency.Bytes, true});
    }
    return Out;
}

inline bool ReadAnimSequenceNative(const std::vector<std::uint8_t>& Body, std::size_t Start, std::size_t End,
    const std::vector<std::string>& Names, FAnimSequenceNative& Out, FReadError& Error)
{
    SkinnedDetail::FIn A(Body, Start, End, Names); FAnimSequenceNative S; bool HasGuid = false, Compressed = false, Bulk = false, RawOnly = false;
    const auto Fail = [&]() { Error = A.R.Error; return false; };
    const auto Refuse = [&](const char* Code) { A.R.Reject(Code); Error = A.R.Error; return false; };
    if (!A.R.Error.Code.empty() || !A.Bool(HasGuid)) return Fail();
    if (HasGuid) return Refuse("skinned-object-guid");
    if (!A.Guid(S.SkeletonGuid) || !A.Strip(S.Strip) || !A.Bool(Compressed)) return Fail();
    if (S.Strip[0] != 1 || S.Strip[1] != 0 || !Compressed) return Refuse("skinned-sequence-not-cooked");
    std::size_t Count = 0;
    if (!A.R.I32(S.RawSize) || !A.R.Count(Count, 65536)) return Fail();
    S.TrackMap.resize(Count);
    for (auto& Track : S.TrackMap) if (!A.R.I32(Track)) return Fail();
    if (!A.R.Count(Count, 65536)) return Fail();
    S.CurveNames.resize(Count);
    for (auto& Name : S.CurveNames) if (!A.R.Name(Names, Name)) return Fail();
    if (!A.R.Count(Count, 64u * 1024u * 1024u) || !A.Bool(Bulk)) return Fail();
    if (Bulk) return Refuse("skinned-sequence-stream-outside-the-file");
    if (!A.Raw(S.Stream, Count) || !A.R.String(S.BoneCodec) || !A.R.String(S.CurveCodec) || !A.R.Count(Count, 64u * 1024u * 1024u) ||
        !A.Raw(S.CurveBytes, Count) || !A.R.I32(S.Frames) || !A.Byte(S.KeyEncoding) || !A.Byte(S.TranslationFormat) || !A.Byte(S.RotationFormat) ||
        !A.Byte(S.ScaleFormat) || !A.R.I32(S.ByteStreamSize) || !A.R.I32(S.TrackOffsets) || !A.R.I32(S.ScaleOffsets) || !A.R.I32(S.ScaleStrip) ||
        !A.Bool(RawOnly)) return Fail();
    if (RawOnly || S.RawSize < 0 || S.Frames <= 0 || S.ByteStreamSize < 0 || S.TrackOffsets < 0 || S.ScaleOffsets < 0 ||
        std::uint64_t(S.ByteStreamSize) + 4ull * (std::uint64_t(S.TrackOffsets) + std::uint64_t(S.ScaleOffsets)) != S.Stream.size())
        return Refuse("skinned-sequence-stream-layout");
    if (!A.End()) return Fail();
    Out = std::move(S); return true;
}
inline std::vector<FNativeLine> AnimSequenceLines(const FAnimSequenceNative& S)
{
    using namespace SkinnedDetail;
    std::vector<FNativeLine> Out; std::string Curves;
    for (const auto& Name : S.CurveNames) Curves += " " + Name;
    Out.push_back({"skeleton id = " + GuidText(S.SkeletonGuid)});
    Out.push_back({"frames = " + std::to_string(S.Frames)});
    Out.push_back({"tracks = " + List(S.TrackMap)});
    Out.push_back({"curves =" + (Curves.empty() ? " none" : Curves)});
    Out.push_back({"curve codec = " + S.CurveCodec});
    // What a compressor works out: two compressions of the same motion need not give the same bytes.
    Out.push_back({"size of the keys before compression = " + std::to_string(S.RawSize), nullptr, true});
    Out.push_back({"bone codec = " + S.BoneCodec, nullptr, true});
    Out.push_back({"key formats = " + std::to_string(S.KeyEncoding) + " " + std::to_string(S.TranslationFormat) + " " + std::to_string(S.RotationFormat) + " " +
        std::to_string(S.ScaleFormat) + ", " + std::to_string(S.TrackOffsets) + " track offsets, " + std::to_string(S.ScaleOffsets) + " scale offsets by " +
        std::to_string(S.ScaleStrip) + ", " + std::to_string(S.ByteStreamSize) + " bytes of keys", nullptr, true});
    Out.push_back({"compressed bone keys", &S.Stream, true});
    Out.push_back({"compressed curves", &S.CurveBytes, true});
    return Out;
}
// The same compressed data in the form the engine keeps it in its derived data cache, which its own
// reader takes from memory: names as text, and no word on whether the keys lie outside the file.
inline std::vector<std::uint8_t> AnimSequenceCacheForm(const FAnimSequenceNative& S)
{
    std::vector<std::uint8_t> Out;
    const auto I32 = [&](std::int32_t Value) { for (int Byte = 0; Byte < 4; ++Byte) Out.push_back(std::uint8_t(std::uint32_t(Value) >> (Byte * 8))); };
    const auto Text = [&](const std::string& Value)
    {
        if (Value.empty()) { I32(0); return; }
        I32(std::int32_t(Value.size() + 1)); Out.insert(Out.end(), Value.begin(), Value.end()); Out.push_back(0);
    };
    I32(S.RawSize); I32(std::int32_t(S.TrackMap.size()));
    for (const auto Track : S.TrackMap) I32(Track);
    I32(std::int32_t(S.CurveNames.size()));
    for (const auto& Name : S.CurveNames) Text(Name);
    I32(std::int32_t(S.Stream.size())); Out.insert(Out.end(), S.Stream.begin(), S.Stream.end());
    Text(S.BoneCodec); Text(S.CurveCodec);
    I32(std::int32_t(S.CurveBytes.size())); Out.insert(Out.end(), S.CurveBytes.begin(), S.CurveBytes.end());
    I32(S.Frames);
    Out.push_back(S.KeyEncoding); Out.push_back(S.TranslationFormat); Out.push_back(S.RotationFormat); Out.push_back(S.ScaleFormat);
    I32(S.ByteStreamSize); I32(S.TrackOffsets); I32(S.ScaleOffsets); I32(S.ScaleStrip);
    return Out;
}
}

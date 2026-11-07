#include "P4StaticMeshStaging.h"
#include "StaticMeshAttributes.h"
#include "PackedNormal.h"
#include "Containers/StringConv.h"

namespace
{
using namespace P4MapKit;
bool Reject(FString& Error, const TCHAR* Message)
{
    Error = Message;
    return false;
}
bool Continue(const TFunction<bool()>& Cancel, FString& Error)
{
    return !Cancel || !Cancel() || Reject(Error, TEXT("mesh-stage-cancelled"));
}
bool Same(float A, float B)
{
    return FMemory::Memcmp(&A, &B, sizeof(float)) == 0;
}
bool UTF8(const std::string& Source, FString& Out)
{
    if (Source.size() > 1024 || Source.find('\0') != std::string::npos)
        return false;
    FUTF8ToTCHAR Decode(Source.data(), int32(Source.size()));
    Out = FString(Decode.Length(), Decode.Get());
    FTCHARToUTF8 Encode(*Out, Out.Len());
    return Encode.Length() == int32(Source.size()) &&
        (!Encode.Length() || FMemory::Memcmp(Encode.Get(), Source.data(), Source.size()) == 0);
}
bool Packs(const FVector& Vector, const std::int16_t* Q, bool High)
{
    if (High)
    {
        const FPackedRGBA16N P(Vector);
        return P.X == Q[0] && P.Y == Q[1] && P.Z == Q[2];
    }
    const FPackedNormal P(Vector);
    return P.Vector.X == Q[0] && P.Vector.Y == Q[1] && P.Vector.Z == Q[2];
}
// The engine normalizes a tangent or a normal before it packs it, so a mesh built from these
// descriptions gets the game's packed components back only from a unit vector that packs to them.
// The vectors that pack to Q fill a small box, half a step each way around Q. This takes the point of
// unit length on the line from the box's nearest point to the origin to its farthest, first in a box
// kept well clear of the rounding edges and then in wider ones.
bool UnitForm(const std::int16_t* Q, bool High, FVector& Out)
{
    const double Scale = High ? MAX_int16 : MAX_int8;
    for (const double Margin : {0.25, 0.4, 0.47, 0.495, 0.4995, 0.49995})
    {
        double Near[3], Far[3], NearSize = 0, FarSize = 0;
        for (int32 K = 0; K < 3; ++K)
        {
            const double Low = Q[K] - Margin, High2 = Q[K] + Margin;
            Near[K] = Low <= 0 && High2 >= 0 ? 0 : (FMath::Abs(Low) < FMath::Abs(High2) ? Low : High2);
            Far[K] = FMath::Abs(Low) > FMath::Abs(High2) ? Low : High2;
            NearSize += Near[K] * Near[K];
            FarSize += Far[K] * Far[K];
        }
        if (NearSize > Scale * Scale || FarSize < Scale * Scale)
            continue;
        double From = 0, To = 1;
        for (int32 Step = 0; Step < 48; ++Step)
        {
            const double Mid = (From + To) / 2;
            double Size = 0;
            for (int32 K = 0; K < 3; ++K)
            {
                const double P = Near[K] + Mid * (Far[K] - Near[K]);
                Size += P * P;
            }
            (Size < Scale * Scale ? From : To) = Mid;
        }
        FVector Candidate;
        for (int32 K = 0; K < 3; ++K)
            Candidate[K] = float((Near[K] + From * (Far[K] - Near[K])) / Scale);
        // As staged, and as the mesh builder will pack it after normalizing it.
        if (Packs(Candidate, Q, High) && Packs(Candidate.GetSafeNormal(), Q, High))
        {
            Out = Candidate;
            return true;
        }
    }
    return false;
}
bool Frame(const FStaticMeshBuffers& B, size_t V, FVector& X, FVector& Z, float& Sign, FString& Error)
{
    const auto& T = B.TangentValues[V];
    const int32 Scale = B.HighPrecisionTangents ? MAX_int16 : MAX_int8;
    if (T[3] != Scale || (T[7] != Scale && T[7] != -Scale))
        return Reject(Error, TEXT("mesh-stage-unrepresentable-packed-tangent-w"));
    for (int32 K = 0; K < 8; ++K)
        if (T[K] < (B.HighPrecisionTangents ? MIN_int16 : MIN_int8) || T[K] > Scale)
            return Reject(Error, TEXT("mesh-stage-packed-tangent-range"));
    X = FVector(float(T[0]) / Scale, float(T[1]) / Scale, float(T[2]) / Scale);
    Z = FVector(float(T[4]) / Scale, float(T[5]) / Scale, float(T[6]) / Scale);
    Sign = T[7] > 0 ? 1.f : -1.f;
    // Zero vectors are present in the game's lower LODs. They are exactly
    // representable in the source description; the ordinary engine builder
    // must not silently replace them. The direct reconstruction path preserves
    // those values and their independently stored binormal sign.
    const bool ZeroX = X.IsZero(), ZeroZ = Z.IsZero();
    if (!ZeroX && !ZeroZ && FVector::CrossProduct(X, Z).SizeSquared() == 0 && Sign < 0)
        return Reject(Error, TEXT("mesh-stage-parallel-tangent-frame-with-negative-sign"));
    if ((!ZeroX && !UnitForm(T.data(), B.HighPrecisionTangents, X)) ||
        (!ZeroZ && !UnitForm(T.data() + 4, B.HighPrecisionTangents, Z)))
        return Reject(Error, TEXT("mesh-stage-packed-vector-has-no-unit-form"));
    return true;
}
// The color of a source vertex. A mesh without a color buffer is opaque white throughout.
std::array<std::uint8_t, 4> SourceColor(const FStaticMeshBuffers& B, size_t V)
{
    if (B.ColorRGBA.empty())
        return {{255, 255, 255, 255}};
    return B.ColorRGBA[V];
}
bool CheckPacked(const FStaticMeshBuffers& B, size_t V, const FVector& X, const FVector& Z, float Sign)
{
    const auto& T = B.TangentValues[V];
    if ((Sign != 1.f && Sign != -1.f) || X.ContainsNaN() || Z.ContainsNaN())
        return false;
    if (B.HighPrecisionTangents)
    {
        FPackedRGBA16N PX(X), PZ(FVector4(Z, Sign));
        const int16 Values[] = {PX.X, PX.Y, PX.Z, PX.W, PZ.X, PZ.Y, PZ.Z, PZ.W};
        for (int32 K = 0; K < 8; ++K)
            if (Values[K] != T[K])
                return false;
    }
    else
    {
        FPackedNormal PX(X), PZ(FVector4(Z, Sign));
        const int16 Values[] = {
            PX.Vector.X, PX.Vector.Y, PX.Vector.Z, PX.Vector.W, PZ.Vector.X, PZ.Vector.Y, PZ.Vector.Z, PZ.Vector.W};
        for (int32 K = 0; K < 8; ++K)
            if (Values[K] != T[K])
                return false;
    }
    return true;
}
void Gaps(FP4MeshStageReport& R)
{
    R.UnrepresentedFields = {TEXT("Original class/native defaults and tagged settings"),
        TEXT("Original material/texture dependency objects"), TEXT("BodySetup/NavCollision/sockets"),
        TEXT("All source GUIDs/bounds/screen sizes/LOD error and build history"),
        TEXT("Section collision/shadow/opacity/ray-tracing flags and source vertex min/max"),
        TEXT("Auxiliary index buffers, expansion/strip/availability records and buffer sizes"),
        TEXT("Samplers/ray-tracing/distance-field/occluder data"),
        TEXT("Exact stock rebuilt render topology and packed precision; save/restart/cook")};
}
bool Validate(const FStaticMeshRenderPayload& Source, const TArray<FP4ExactMeshMaterialSlot>& Slots,
    FP4MeshStageReport& R, FString& Error, const FP4MeshStageLimits& Limits, const TFunction<bool()>& Cancel)
{
    if (!IsInGameThread())
        return Reject(Error, TEXT("mesh-stage-requires-editor-thread"));
    if (!Continue(Cancel, Error))
        return false;
    if (!Source.RenderDataDecoded || Source.Contract != EStaticMeshRenderContract::RetocUE426 || Source.LODs.empty() ||
        Source.LODs.size() > 8 || Source.Materials.empty() || Source.Materials.size() > 4096 ||
        Source.Materials.size() != size_t(Slots.Num()) || !Limits.Vertices || !Limits.Corners ||
        Limits.Vertices > 1000000 || Limits.Corners > 6000000)
        return Reject(Error, TEXT("mesh-stage-source-or-budget-unqualified"));
    for (int32 M = 0; M < Slots.Num(); ++M)
    {
        FString Native;
        bool HasNul = false;
        for (int32 I = 0; I < Slots[M].ImportedSlot.Len(); ++I)
            HasNul |= Slots[M].ImportedSlot[I] == TCHAR(0);
        if (!UTF8(Source.Materials[M].Slot, Native) || Native != Slots[M].NativeSlot ||
            Source.Materials[M].ObjectIndex != Slots[M].ObjectIndex || Slots[M].ImportedSlot.Len() >= NAME_SIZE ||
            HasNul || FName(*Slots[M].ImportedSlot).ToString() != Slots[M].ImportedSlot)
            return Reject(Error, TEXT("mesh-stage-material-label-not-exactly-representable"));
    }
    uint64 Bytes = 0;
    for (const auto& L : Source.LODs)
    {
        if (!Continue(Cancel, Error))
            return false;
        const auto& B = L.Buffers;
        if (!L.HasGeometry || L.CookedOut || !B.Vertices || B.Vertices > Limits.Vertices ||
            B.PositionValues.size() != B.Vertices || B.TangentValues.size() != B.Vertices || B.TexCoords < 1 ||
            B.TexCoords > 8 || B.UVValues.size() != B.TexCoords || B.Main.Indices.empty() ||
            B.Main.Indices.size() % 3 || B.Main.Indices.size() > Limits.Corners || L.Sections.empty() ||
            L.Sections.size() > 1024)
            return Reject(Error, TEXT("mesh-stage-lod-counts-invalid"));
        // Absence means white source values; a present all-white buffer remains
        // a distinct layout, kept by the direct reconstruction record.
        if (B.ColorVertices || !B.ColorRGBA.empty())
        {
            if (B.ColorVertices != B.Vertices || B.ColorRGBA.size() != B.Vertices)
                return Reject(Error, TEXT("mesh-stage-color-count-invalid"));
        }
        for (const auto& UV : B.UVValues)
            if (UV.size() != B.Vertices)
                return Reject(Error, TEXT("mesh-stage-uv-count-invalid"));
        R.Vertices += B.Vertices;
        R.Corners += B.Main.Indices.size();
        R.Sections += L.Sections.size();
        // Conservative bounded source+corner attributes/topology allocation.
        Bytes += uint64(B.Vertices) * (256 + uint64(B.TexCoords) * 16) +
            uint64(B.Main.Indices.size()) * (1024 + uint64(B.TexCoords) * 32);
        if (R.Vertices > Limits.Vertices || R.Corners > Limits.Corners || Bytes > Limits.EstimatedBytes)
            return Reject(Error, TEXT("mesh-stage-total-budget-exceeded"));
        for (size_t V = 0; V < B.Vertices; ++V)
        {
            if ((V & 4095) == 0 && !Continue(Cancel, Error))
                return false;
            for (float P : B.PositionValues[V])
                if (!FMath::IsFinite(P))
                    return Reject(Error, TEXT("mesh-stage-nonfinite-position"));
            for (const auto& UV : B.UVValues)
                for (float P : UV[V])
                    if (!FMath::IsFinite(P))
                        return Reject(Error, TEXT("mesh-stage-nonfinite-uv"));
            FVector X, Z;
            float Sign;
            if (!Frame(B, V, X, Z, Sign, Error) || !CheckPacked(B, V, X, Z, Sign))
                return Error.IsEmpty() ? Reject(Error, TEXT("mesh-stage-packed-frame-loss")) : false;
        }
        uint64 End = 0;
        for (const auto& S : L.Sections)
        {
            if (S.MaterialIndex < 0 || S.MaterialIndex >= Slots.Num() || S.FirstIndex != End || !S.Triangles)
                return Reject(Error, TEXT("mesh-stage-section-invalid"));
            End += uint64(S.Triangles) * 3;
            if (End > B.Main.Indices.size())
                return Reject(Error, TEXT("mesh-stage-section-extent-invalid"));
        }
        if (End != B.Main.Indices.size())
            return Reject(Error, TEXT("mesh-stage-section-index-gap"));
        for (size_t I = 0; I < B.Main.Indices.size(); I += 3)
        {
            if ((I & 4095) == 0 && !Continue(Cancel, Error))
                return false;
            const auto A = B.Main.Indices[I], C = B.Main.Indices[I + 1], D = B.Main.Indices[I + 2];
            if (A >= B.Vertices || C >= B.Vertices || D >= B.Vertices || A == C || C == D || D == A)
                return Reject(Error, TEXT("mesh-stage-index-or-repeated-corner-invalid"));
        }
    }
    Gaps(R);
    return true;
}
void SetAttributes(FMeshDescription& D, const FStaticMeshBuffers& B, size_t V, FVertexInstanceID I)
{
    FStaticMeshAttributes A(D);
    FVector X, Z;
    float Sign = 0;
    FString Error;
    Frame(B, V, X, Z, Sign, Error);
    A.GetVertexInstanceTangents()[I] = X;
    A.GetVertexInstanceNormals()[I] = Z;
    A.GetVertexInstanceBinormalSigns()[I] = Sign;
    const auto C = SourceColor(B, V);
    A.GetVertexInstanceColors()[I] = FVector4(FLinearColor::FromSRGBColor(FColor(C[0], C[1], C[2], C[3])));
    auto UV = A.GetVertexInstanceUVs();
    for (uint32 U = 0; U < B.TexCoords; ++U)
        UV.Set(I, U, FVector2D(B.UVValues[U][V][0], B.UVValues[U][V][1]));
}
bool VerifyAttributes(
    const FMeshDescription& D, const FStaticMeshBuffers& B, size_t V, FVertexInstanceID I, FString& Error)
{
    FStaticMeshConstAttributes A(D);
    if (!D.VertexInstances().IsValid(I) ||
        !CheckPacked(B, V, A.GetVertexInstanceTangents()[I], A.GetVertexInstanceNormals()[I],
            A.GetVertexInstanceBinormalSigns()[I]))
        return Reject(Error, TEXT("mesh-stage-readback-packed-frame-loss"));
    const FVector4 C = A.GetVertexInstanceColors()[I];
    const FColor Packed = FLinearColor(C.X, C.Y, C.Z, C.W).ToFColor(true);
    const auto Wanted = SourceColor(B, V);
    if (Packed.R != Wanted[0] || Packed.G != Wanted[1] || Packed.B != Wanted[2] || Packed.A != Wanted[3])
        return Reject(Error, TEXT("mesh-stage-readback-color-loss"));
    auto UV = A.GetVertexInstanceUVs();
    for (uint32 U = 0; U < B.TexCoords; ++U)
    {
        const FVector2D Value = UV.Get(I, U);
        if (!Same(Value.X, B.UVValues[U][V][0]) || !Same(Value.Y, B.UVValues[U][V][1]))
            return Reject(Error, TEXT("mesh-stage-readback-uv-loss"));
    }
    return true;
}
} //namespace

bool P4VerifyPrivateMeshDescriptions(const FStaticMeshRenderPayload& Source,
    const TArray<FP4ExactMeshMaterialSlot>& Slots, const FP4PrivateMeshStage& Stage, FP4MeshStageReport& Report,
    FString& Error, const FP4MeshStageLimits& Limits, const TFunction<bool()>& Cancel)
{
    Error.Reset();
    FP4MeshStageReport Pending;
    if (!Validate(Source, Slots, Pending, Error, Limits, Cancel))
        return false;
    if (Stage.LODs.Num() != int32(Source.LODs.size()))
        return Reject(Error, TEXT("mesh-stage-readback-lod-loss"));
    for (int32 L = 0; L < Stage.LODs.Num(); ++L)
    {
        const auto& S = Stage.LODs[L];
        const auto& B = Source.LODs[L].Buffers;
        if (!S.Description)
            return Reject(Error, TEXT("mesh-stage-readback-description-absent"));
        const auto& D = *S.Description;
        FStaticMeshConstAttributes A(D);
        if (!A.GetVertexPositions().IsValid() || !A.GetVertexInstanceUVs().IsValid() ||
            !A.GetVertexInstanceTangents().IsValid() || !A.GetVertexInstanceNormals().IsValid() ||
            !A.GetVertexInstanceBinormalSigns().IsValid() || !A.GetVertexInstanceColors().IsValid() ||
            !A.GetPolygonGroupMaterialSlotNames().IsValid())
            return Reject(Error, TEXT("mesh-stage-readback-attribute-absent"));
        if (S.Vertices.Num() != int32(B.Vertices) || S.Corners.Num() != int32(B.Main.Indices.size()) ||
            S.UnusedInstances.Num() != int32(B.Vertices) || S.Sections.Num() != int32(Source.LODs[L].Sections.size()) ||
            S.Polygons.Num() != int32(B.Main.Indices.size() / 3) || D.Vertices().Num() != int32(B.Vertices) ||
            D.Polygons().Num() != S.Polygons.Num() || D.Triangles().Num() != S.Polygons.Num() ||
            D.PolygonGroups().Num() != S.Sections.Num() ||
            A.GetVertexInstanceUVs().GetNumIndices() != int32(B.TexCoords))
            return Reject(Error, TEXT("mesh-stage-readback-count-loss"));
        TSet<FVertexID> SeenVertices;
        TSet<FVertexInstanceID> SeenInstances;
        TSet<FPolygonID> SeenPolygons;
        TArray<bool> Used;
        Used.Init(false, B.Vertices);
        for (uint32 V : B.Main.Indices)
            Used[V] = true;
        for (uint32 V = 0; V < B.Vertices; ++V)
        {
            if ((V & 4095) == 0 && !Continue(Cancel, Error))
                return false;
            const FVertexID ID = S.Vertices[V];
            if (!D.Vertices().IsValid(ID) || SeenVertices.Contains(ID))
                return Reject(Error, TEXT("mesh-stage-readback-vertex-identity-loss"));
            SeenVertices.Add(ID);
            const FVector P = A.GetVertexPositions()[ID];
            for (int32 K = 0; K < 3; ++K)
                if (!Same(P[K], B.PositionValues[V][K]))
                    return Reject(Error, TEXT("mesh-stage-readback-position-loss"));
            const auto I = S.UnusedInstances[V];
            if (!Used[V])
            {
                if (!D.VertexInstances().IsValid(I) || D.GetVertexInstanceVertex(I) != ID ||
                    D.GetVertexInstanceConnectedPolygons(I).Num() != 0 || SeenInstances.Contains(I) ||
                    !VerifyAttributes(D, B, V, I, Error))
                    return Error.IsEmpty() ? Reject(Error, TEXT("mesh-stage-unused-vertex-loss")) : false;
                SeenInstances.Add(I);
                ++Pending.UnusedVertices;
            }
            else if (I != FVertexInstanceID(INDEX_NONE))
                return Reject(Error, TEXT("mesh-stage-extra-unused-instance"));
        }
        TSet<FPolygonGroupID> SeenGroups;
        for (int32 Section = 0; Section < S.Sections.Num(); ++Section)
        {
            const auto Group = S.Sections[Section];
            const auto& Original = Source.LODs[L].Sections[Section];
            if (!D.PolygonGroups().IsValid(Group) || SeenGroups.Contains(Group) ||
                A.GetPolygonGroupMaterialSlotNames()[Group].ToString() != Slots[Original.MaterialIndex].ImportedSlot)
                return Reject(Error, TEXT("mesh-stage-readback-section-identity-loss"));
            SeenGroups.Add(Group);
            for (uint64 At = Original.FirstIndex; At < uint64(Original.FirstIndex) + uint64(Original.Triangles) * 3;
                At += 3)
            {
                if ((At & 4095) == 0 && !Continue(Cancel, Error))
                    return false;
                const auto Polygon = S.Polygons[int32(At / 3)];
                if (!D.Polygons().IsValid(Polygon) || SeenPolygons.Contains(Polygon) ||
                    D.GetPolygonPolygonGroup(Polygon) != Group)
                    return Reject(Error, TEXT("mesh-stage-readback-polygon-identity-loss"));
                SeenPolygons.Add(Polygon);
                const auto Corners = D.GetPolygonVertexInstances(Polygon);
                if (Corners.Num() != 3)
                    return Reject(Error, TEXT("mesh-stage-readback-polygon-corner-count"));
                const auto Triangles = D.GetPolygonTriangleIDs(Polygon);
                if (Triangles.Num() != 1)
                    return Reject(Error, TEXT("mesh-stage-readback-triangulation-loss"));
                const auto TriangleCorners = D.GetTriangleVertexInstances(Triangles[0]);
                if (TriangleCorners.Num() != 3)
                    return Reject(Error, TEXT("mesh-stage-readback-triangle-corner-count"));
                for (int32 K = 0; K < 3; ++K)
                {
                    const auto I = Corners[K];
                    const uint32 V = B.Main.Indices[size_t(At) + K];
                    if (!D.VertexInstances().IsValid(I) || TriangleCorners[K] != I || I != S.Corners[int32(At) + K] ||
                        SeenInstances.Contains(I) || D.GetVertexInstanceVertex(I) != S.Vertices[V])
                        return Reject(Error, TEXT("mesh-stage-readback-oriented-corner-loss"));
                    SeenInstances.Add(I);
                    if (!VerifyAttributes(D, B, V, I, Error))
                        return false;
                }
            }
        }
        if (D.VertexInstances().Num() != SeenInstances.Num())
            return Reject(Error, TEXT("mesh-stage-readback-extra-instance"));
    }
    if (!Continue(Cancel, Error))
        return false;
    Pending.GeometryAttributesVerified = true;
    Report = MoveTemp(Pending);
    return true;
}

bool P4StagePrivateMeshDescriptions(const FStaticMeshRenderPayload& Source,
    const TArray<FP4ExactMeshMaterialSlot>& Slots, FP4PrivateMeshStage& Out, FP4MeshStageReport& Report, FString& Error,
    const FP4MeshStageLimits& Limits, const TFunction<bool()>& Cancel)
{
    Error.Reset();
    FP4MeshStageReport Preflight;
    if (!Validate(Source, Slots, Preflight, Error, Limits, Cancel))
        return false;
    FP4PrivateMeshStage Pending;
    for (const auto& L : Source.LODs)
    {
        FP4PrivateMeshLODStage S;
        S.Description = MakeUnique<FMeshDescription>();
        auto& D = *S.Description;
        FStaticMeshAttributes A(D);
        A.Register();
        A.GetVertexInstanceUVs().SetNumIndices(L.Buffers.TexCoords);
        const auto& B = L.Buffers;
        TArray<bool> Used;
        Used.Init(false, B.Vertices);
        S.UnusedInstances.Init(FVertexInstanceID(INDEX_NONE), B.Vertices);
        for (uint32 V : B.Main.Indices)
            Used[V] = true;
        for (uint32 V = 0; V < B.Vertices; ++V)
        {
            if ((V & 4095) == 0 && !Continue(Cancel, Error))
                return false;
            const auto ID = D.CreateVertex();
            S.Vertices.Add(ID);
            A.GetVertexPositions()[ID] = FVector(
                B.PositionValues[V][0], B.PositionValues[V][1], B.PositionValues[V][2]);
            if (!Used[V])
            {
                const auto I = D.CreateVertexInstance(ID);
                S.UnusedInstances[V] = I;
                SetAttributes(D, B, V, I);
            }
        }
        for (const auto& Section : L.Sections)
        {
            const auto Group = D.CreatePolygonGroup();
            S.Sections.Add(Group);
            A.GetPolygonGroupMaterialSlotNames()[Group] = FName(*Slots[Section.MaterialIndex].ImportedSlot);
            for (uint64 At = Section.FirstIndex; At < uint64(Section.FirstIndex) + uint64(Section.Triangles) * 3;
                At += 3)
            {
                if ((At & 4095) == 0 && !Continue(Cancel, Error))
                    return false;
                TArray<FVertexInstanceID> Corners;
                for (int32 K = 0; K < 3; ++K)
                {
                    const uint32 V = B.Main.Indices[size_t(At) + K];
                    const auto I = D.CreateVertexInstance(S.Vertices[V]);
                    SetAttributes(D, B, V, I);
                    S.Corners.Add(I);
                    Corners.Add(I);
                }
                S.Polygons.Add(D.CreatePolygon(Group, Corners));
            }
        }
        Pending.LODs.Add(MoveTemp(S));
    }
    FP4MeshStageReport Verified;
    if (!P4VerifyPrivateMeshDescriptions(Source, Slots, Pending, Verified, Error, Limits, Cancel))
        return false;
    Out = MoveTemp(Pending);
    Report = MoveTemp(Verified);
    return true;
}

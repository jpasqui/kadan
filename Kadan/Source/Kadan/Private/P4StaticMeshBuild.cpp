#include "P4StaticMeshBuild.h"
#include "P4StaticMeshSockets.h"
#include "P4MeshSourceReconstruction.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Materials/MaterialInterface.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
enum : uint32
{
    CornerPosition = 1,
    CornerTangents = 2,
    CornerUVs = 4,
    CornerColor = 8,
    CornerAll = 15
};

void Put(TArray<uint8>& Out, const void* Data, int32 Size)
{
    Out.Append(static_cast<const uint8*>(Data), Size);
}

// One corner of a triangle as bytes, written the same way from the game's data and from the engine's:
// three position floats, the eight packed tangent and normal components as 16-bit values, two floats
// for each UV channel, and the color. A mesh without a color buffer is opaque white.
void SourceCorner(const P4MapKit::FStaticMeshBuffers& B, uint32 V, uint32 Parts, TArray<uint8>& Out)
{
    if (Parts & CornerPosition)
        Put(Out, B.PositionValues[V].data(), 12);
    if (Parts & CornerTangents)
        Put(Out, B.TangentValues[V].data(), 16);
    if (Parts & CornerUVs)
        for (uint32 U = 0; U < B.TexCoords; ++U)
            Put(Out, B.UVValues[U][V].data(), 8);
    if (Parts & CornerColor)
    {
        const uint8 White[4] = {255, 255, 255, 255};
        Put(Out, B.ColorRGBA.empty() ? White : B.ColorRGBA[V].data(), 4);
    }
}
void BuiltCorner(FStaticMeshLODResources& R, uint32 V, uint32 Parts, TArray<uint8>& Out);
// The vertices of the game's mesh whose corners the built mesh does not have, when each of them has a
// tangent frame without a binormal (a zero vector, or a tangent along the normal), which the engine's
// mesh builder replaces with one of its own. -1 when another corner differs.
int32 RebuiltFrames(const P4MapKit::FStaticMeshLOD& Original, FStaticMeshLODResources& Built)
{
    const P4MapKit::FStaticMeshBuffers& B = Original.Buffers;
    TSet<FString> BuiltCorners;
    for (int32 I = 0; I < Built.IndexBuffer.GetNumIndices(); ++I)
    {
        TArray<uint8> Key;
        BuiltCorner(Built, Built.IndexBuffer.GetIndex(I), CornerAll, Key);
        BuiltCorners.Add(BytesToHex(Key.GetData(), Key.Num()));
    }
    TSet<uint32> Rebuilt;
    for (const auto Index : B.Main.Indices)
    {
        const uint32 V = uint32(Index);
        TArray<uint8> Key;
        SourceCorner(B, V, CornerAll, Key);
        if (BuiltCorners.Contains(BytesToHex(Key.GetData(), Key.Num())))
            continue;
        const auto& T = B.TangentValues[V];
        const FVector X(T[0], T[1], T[2]), Z(T[4], T[5], T[6]);
        if (!X.IsZero() && !Z.IsZero() && !FVector::CrossProduct(X, Z).IsZero())
            return -1;
        Rebuilt.Add(V);
    }
    return Rebuilt.Num();
}
void BuiltCorner(FStaticMeshLODResources& R, uint32 V, uint32 Parts, TArray<uint8>& Out)
{
    FStaticMeshVertexBuffer& Buffer = R.VertexBuffers.StaticMeshVertexBuffer;
    if (Parts & CornerPosition)
    {
        const FVector P = R.VertexBuffers.PositionVertexBuffer.VertexPosition(V);
        const float Values[3] = {P.X, P.Y, P.Z};
        Put(Out, Values, 12);
    }
    if (Parts & CornerTangents)
    {
        // The buffer holds tangent X then tangent Z for each vertex, four signed components each.
        std::int16_t Values[8];
        if (Buffer.GetUseHighPrecisionTangentBasis())
            FMemory::Memcpy(Values, static_cast<const uint8*>(Buffer.GetTangentData()) + SIZE_T(V) * 16, 16);
        else
        {
            const int8* Packed = static_cast<const int8*>(Buffer.GetTangentData()) + SIZE_T(V) * 8;
            for (int32 K = 0; K < 8; ++K)
                Values[K] = Packed[K];
        }
        Put(Out, Values, 16);
    }
    if (Parts & CornerUVs)
        for (uint32 U = 0; U < Buffer.GetNumTexCoords(); ++U)
        {
            const FVector2D UV = Buffer.GetVertexUV(V, U);
            const float Values[2] = {UV.X, UV.Y};
            Put(Out, Values, 8);
        }
    if (Parts & CornerColor)
    {
        const FColorVertexBuffer& Colors = R.VertexBuffers.ColorVertexBuffer;
        const FColor C = Colors.GetNumVertices() ? Colors.VertexColor(V) : FColor::White;
        const uint8 Values[4] = {C.R, C.G, C.B, C.A};
        Put(Out, Values, 4);
    }
}
bool Before(const TArray<uint8>& A, const TArray<uint8>& B)
{
    return A.Num() != B.Num() ? A.Num() < B.Num() : FMemory::Memcmp(A.GetData(), B.GetData(), A.Num()) < 0;
}
// The triangles of one section as sorted byte strings. Each triangle is written from the corner that
// gives the smallest string, so the corner the engine starts it at does not matter. Winding is kept.
TArray<TArray<uint8>> Triangles(uint32 Count, const TFunctionRef<void(uint32 Corner, TArray<uint8>& Out)>& Corner)
{
    TArray<TArray<uint8>> Out;
    Out.Reserve(int32(Count));
    for (uint32 T = 0; T < Count; ++T)
    {
        TArray<uint8> Corners[3];
        for (uint32 K = 0; K < 3; ++K)
            Corner(T * 3 + K, Corners[K]);
        TArray<uint8> Best;
        for (int32 Turn = 0; Turn < 3; ++Turn)
        {
            TArray<uint8> Candidate = Corners[Turn];
            Candidate.Append(Corners[(Turn + 1) % 3]);
            Candidate.Append(Corners[(Turn + 2) % 3]);
            if (Turn == 0 || Before(Candidate, Best))
                Best = MoveTemp(Candidate);
        }
        Out.Add(MoveTemp(Best));
    }
    Out.Sort(Before);
    return Out;
}
// True when every section holds the same triangles in the game's LOD and in the built one.
bool SameTriangles(const P4MapKit::FStaticMeshLOD& Source, FStaticMeshLODResources& Built, uint32 Parts)
{
    const P4MapKit::FStaticMeshBuffers& B = Source.Buffers;
    const FIndexArrayView Indices = Built.IndexBuffer.GetArrayView();
    for (int32 Section = 0; Section < Built.Sections.Num(); ++Section)
    {
        const P4MapKit::FStaticMeshSection& Ours = Source.Sections[size_t(Section)];
        const ::FStaticMeshSection& Theirs = Built.Sections[Section];
        if (uint64(Theirs.FirstIndex) + uint64(Theirs.NumTriangles) * 3 > uint64(Indices.Num()))
            return false;
        const auto Wanted = Triangles(Ours.Triangles, [&](uint32 Corner, TArray<uint8>& Out)
            { SourceCorner(B, B.Main.Indices[size_t(Ours.FirstIndex) + Corner], Parts, Out); });
        const auto Actual = Triangles(Theirs.NumTriangles, [&](uint32 Corner, TArray<uint8>& Out)
            { BuiltCorner(Built, Indices[int32(Theirs.FirstIndex + Corner)], Parts, Out); });
        if (Wanted != Actual)
            return false;
    }
    return true;
}
bool SameBits(float A, float B)
{
    return FMemory::Memcmp(&A, &B, sizeof(float)) == 0;
}
// The largest UV difference between the game's triangles and the built ones when they are equal in
// every other respect, or -1 when they are not. A triangle is written as its three corners without
// their UVs, from the corner that sorts first, and then the three UVs. Triangles are paired in sorted
// order.
float UVGap(const P4MapKit::FStaticMeshLOD& Source, FStaticMeshLODResources& Built)
{
    const P4MapKit::FStaticMeshBuffers& B = Source.Buffers;
    const FIndexArrayView Indices = Built.IndexBuffer.GetArrayView();
    const uint32 Others = CornerAll & ~uint32(CornerUVs);
    const auto Records =
        [Others](uint32 Count, const TFunctionRef<void(uint32 Corner, uint32 Parts, TArray<uint8> & Out)>& Corner)
    {
        TArray<TArray<uint8>> Out;
        Out.Reserve(int32(Count));
        for (uint32 T = 0; T < Count; ++T)
        {
            TArray<uint8> Key[3], UV[3], Best;
            for (uint32 K = 0; K < 3; ++K)
            {
                Corner(T * 3 + K, Others, Key[K]);
                Corner(T * 3 + K, CornerUVs, UV[K]);
            }
            int32 Start = 0;
            for (int32 Turn = 0; Turn < 3; ++Turn)
            {
                TArray<uint8> Candidate = Key[Turn];
                Candidate.Append(Key[(Turn + 1) % 3]);
                Candidate.Append(Key[(Turn + 2) % 3]);
                if (Turn == 0 || Before(Candidate, Best))
                {
                    Best = MoveTemp(Candidate);
                    Start = Turn;
                }
            }
            for (int32 K = 0; K < 3; ++K)
                Best.Append(UV[(Start + K) % 3]);
            Out.Add(MoveTemp(Best));
        }
        Out.Sort(Before);
        return Out;
    };
    float Largest = 0;
    for (int32 Section = 0; Section < Built.Sections.Num(); ++Section)
    {
        const P4MapKit::FStaticMeshSection& Ours = Source.Sections[size_t(Section)];
        const ::FStaticMeshSection& Theirs = Built.Sections[Section];
        if (Ours.Triangles != Theirs.NumTriangles ||
            uint64(Theirs.FirstIndex) + uint64(Theirs.NumTriangles) * 3 > uint64(Indices.Num()))
            return -1.f;
        const auto Wanted = Records(Ours.Triangles, [&](uint32 Corner, uint32 Parts, TArray<uint8>& Out)
            { SourceCorner(B, B.Main.Indices[size_t(Ours.FirstIndex) + Corner], Parts, Out); });
        const auto Actual = Records(Theirs.NumTriangles, [&](uint32 Corner, uint32 Parts, TArray<uint8>& Out)
            { BuiltCorner(Built, Indices[int32(Theirs.FirstIndex + Corner)], Parts, Out); });
        const int32 UVBytes = int32(B.TexCoords) * 8 * 3;
        for (int32 T = 0; T < Wanted.Num(); ++T)
        {
            const TArray<uint8>& A = Wanted[T];
            const TArray<uint8>& C = Actual[T];
            const int32 Fixed = A.Num() - UVBytes;
            if (A.Num() != C.Num() || Fixed <= 0 || FMemory::Memcmp(A.GetData(), C.GetData(), Fixed))
                return -1.f;
            for (int32 At = Fixed; At < A.Num(); At += 4)
            {
                float X, Y;
                FMemory::Memcpy(&X, A.GetData() + At, 4);
                FMemory::Memcpy(&Y, C.GetData() + At, 4);
                Largest = FMath::Max(Largest, FMath::Abs(X - Y));
            }
        }
    }
    return Largest;
}
} //namespace

UStaticMesh* P4BuildOriginalMesh(const FP4OriginalMeshSource& Source, UObject* Outer, FName Name, EObjectFlags Flags,
    FP4MeshBuildReport& Report, FString& Error, UStaticMesh* Existing)
{
    Report = FP4MeshBuildReport();
    Error.Reset();
    if (!IsInGameThread() || !Outer)
    {
        Error = TEXT("mesh-build-requires-editor-thread-and-outer");
        return nullptr;
    }
    const P4MapKit::FStaticMeshRenderPayload& Render = Source.Render;
    TArray<FP4ExactMeshMaterialSlot> Slots;
    for (const auto& Material : Source.Materials)
        Slots.Add(Material.Slot);
    FP4PrivateMeshStage Stage;
    FP4MeshStageReport StageReport;
    if (!P4StagePrivateMeshDescriptions(Render, Slots, Stage, StageReport, Error))
        return nullptr;

    UStaticMesh* Mesh = Existing ? Existing : NewObject<UStaticMesh>(Outer, Name, Flags);
    TStrongObjectPtr<UStaticMesh> Guard(Mesh);
    if (Existing)
    {
        Mesh->SetNumSourceModels(0);
        Mesh->StaticMaterials.Reset();
        Mesh->GetSectionInfoMap().Clear();
        Mesh->GetOriginalSectionInfoMap().Clear();
    }
    Mesh->SetNumSourceModels(Stage.LODs.Num());
    Mesh->bAutoComputeLODScreenSize = false;
    Mesh->ImportVersion = EImportStaticMeshVersion::LastVersion;
    for (const auto& Material : Source.Materials)
    {
        UMaterialInterface* Found = Material.ObjectPath.IsEmpty()
            ? nullptr
            : FindObject<UMaterialInterface>(nullptr, *Material.ObjectPath);
        if (!Found && !Material.ObjectPath.IsEmpty())
            Report.MissingMaterials.Add(Material.ObjectPath);
        FStaticMaterial Slot(Found, FName(*Material.Slot.NativeSlot), FName(*Material.Slot.ImportedSlot));
        Slot.UVChannelData.bInitialized = Material.UVInitialized;
        Slot.UVChannelData.bOverrideDensities = Material.UVOverride;
        for (int32 K = 0; K < 4; ++K)
            Slot.UVChannelData.LocalUVDensities[K] = Material.UVDensities[size_t(K)];
        Mesh->StaticMaterials.Add(Slot);
    }
    for (int32 L = 0; L < Stage.LODs.Num(); ++L)
    {
        const P4MapKit::FStaticMeshLOD& Original = Render.LODs[size_t(L)];
        const P4MapKit::FStaticMeshBuffers& B = Original.Buffers;
        FStaticMeshSourceModel& Model = Mesh->GetSourceModel(L);
        // The description already holds the game's own vertices: nothing is recomputed, welded,
        // removed, reduced or generated when it is built.
        FMeshBuildSettings& Settings = Model.BuildSettings;
        Settings.bRecomputeNormals = false;
        Settings.bRecomputeTangents = false;
        Settings.bUseMikkTSpace = false;
        Settings.bComputeWeightedNormals = false;
        Settings.bRemoveDegenerates = false;
        Settings.bBuildAdjacencyBuffer = false;
        Settings.bBuildReversedIndexBuffer = false;
        Settings.bGenerateLightmapUVs = false;
        Settings.BuildScale3D = FVector(1.f, 1.f, 1.f);
        Settings.bUseHighPrecisionTangentBasis = B.HighPrecisionTangents;
        Settings.bUseFullPrecisionUVs = B.FullPrecisionUV;
        Model.ReductionSettings.PercentTriangles = 1.f;
        Model.ReductionSettings.PercentVertices = 1.f;
        Model.ReductionSettings.MaxDeviation = 0.f;
        Model.ScreenSize.Default = Render.ScreenSizes[size_t(L)];
        Mesh->CreateMeshDescription(L, MoveTemp(*Stage.LODs[L].Description));
        Mesh->CommitMeshDescription(L);
        // The engine would give two sections that share an imported slot name the same material. The
        // section table says which material and which flags each section has.
        for (int32 Section = 0; Section < int32(Original.Sections.size()); ++Section)
        {
            const P4MapKit::FStaticMeshSection& From = Original.Sections[size_t(Section)];
            FMeshSectionInfo Info;
            Info.MaterialIndex = From.MaterialIndex;
            Info.bEnableCollision = From.Collision;
            Info.bCastShadow = From.Shadow;
            Info.bVisibleInRayTracing = From.RayTracing;
            Info.bForceOpaque = From.ForceOpaque;
            Mesh->GetSectionInfoMap().Set(L, Section, Info);
            Mesh->GetOriginalSectionInfoMap().Set(L, Section, Info);
        }
    }
    bool ZeroFrame = false;
    bool PresentWhite = false;
    for (const auto& L : Render.LODs)
    {
        for (const auto& T : L.Buffers.TangentValues)
            ZeroFrame |= (T[0] == 0 && T[1] == 0 && T[2] == 0) || (T[4] == 0 && T[5] == 0 && T[6] == 0);
        bool White = !L.Buffers.ColorRGBA.empty();
        for (const auto& C : L.Buffers.ColorRGBA)
            White &= C[0] == 255 && C[1] == 255 && C[2] == 255 && C[3] == 255;
        PresentWhite |= White;
    }
    TArray<FText> Problems;
    if (ZeroFrame || PresentWhite)
    {
        if (!P4RecordMeshSourceReconstruction(Mesh, Source, Error) || !P4RestoreMeshSourceReconstruction(Mesh, Error))
        {
            if (!Existing)
                Mesh->MarkPendingKill();
            return nullptr;
        }
        Report.SourceReconstructed = true;
    }
    else
        Mesh->Build(true, &Problems);
    if (Problems.Num() || !Mesh->RenderData || Mesh->RenderData->LODResources.Num() != Stage.LODs.Num())
    {
        Error = TEXT("mesh-build-engine-build-failed") +
            (Problems.Num() ? TEXT(": ") + Problems[0].ToString() : FString());
        if (!Existing)
            Mesh->MarkPendingKill();
        return nullptr;
    }

    if (!P4CompareOriginalMeshBuild(Source, Mesh, Report, Error))
    {
        if (!Existing)
            Mesh->MarkPendingKill();
        return nullptr;
    }
    if (!Report.SourceReconstructed && !Report.EqualWithinEngineWeld && !Report.EqualWithinEngineTangents)
    {
        // The stock builder can change valid source bounds/frames/layout. Use
        // the same editable, source-qualified reconstruction as zero frames;
        // the importer still requires exact independent comparison afterwards.
        if (!P4RecordMeshSourceReconstruction(Mesh, Source, Error) || !P4RestoreMeshSourceReconstruction(Mesh, Error))
        {
            if (!Existing)
                Mesh->MarkPendingKill();
            return nullptr;
        }
        Report.SourceReconstructed = true;
        if (!P4CompareOriginalMeshBuild(Source, Mesh, Report, Error))
        {
            if (!Existing)
                Mesh->MarkPendingKill();
            return nullptr;
        }
    }
    if (!P4BuildOriginalMeshSockets(Mesh, Source.SocketProperties, Error))
    {
        if (!Existing)
            Mesh->MarkPendingKill();
        return nullptr;
    }
    // Whether the game draws every LOD with the first LOD's built lighting. An editor build decides that from the
    // LODs it is given, which here are all the game's own, so the game's value is kept for its lighting
    // (P4OriginalBuiltLighting.cpp).
    Mesh->GetOutermost()->GetMetaData()->SetValue(
        Mesh, P4LODsShareStaticLightingKey, Render.ShareStaticLighting ? TEXT("true") : TEXT("false"));
    return Mesh;
}

bool P4CompareOriginalMeshBuild(
    const FP4OriginalMeshSource& Source, UStaticMesh* Mesh, FP4MeshBuildReport& Report, FString& Error)
{
    Error.Reset();
    const auto& Render = Source.Render;
    if (!IsInGameThread() || !IsValid(Mesh) || !Mesh->RenderData || !Render.RenderDataDecoded || Render.LODs.empty() ||
        Render.LODs.size() > 8 || Mesh->RenderData->LODResources.Num() != int32(Render.LODs.size()))
    {
        Error = TEXT("mesh-build-comparison-requires-complete-source-and-render-data");
        return false;
    }
    Report.LODs.Reset();
    Report.LargestUVGap = 0;
    Report.RebuiltTangentFrames = 0;
    bool Equal = true, Welded = true, Rebuilt = true;
    for (int32 L = 0; L < int32(Render.LODs.size()); ++L)
    {
        const P4MapKit::FStaticMeshLOD& Original = Render.LODs[size_t(L)];
        const P4MapKit::FStaticMeshBuffers& B = Original.Buffers;
        FStaticMeshLODResources& Built = Mesh->RenderData->LODResources[L];
        const FStaticMeshVertexBuffer& Buffer = Built.VertexBuffers.StaticMeshVertexBuffer;
        const uint32 Colors = Built.VertexBuffers.ColorVertexBuffer.GetNumVertices();
        FP4MeshBuildLOD Out;
        Out.SourceVertices = int32(B.Vertices);
        Out.Triangles = int32(B.Main.Indices.size() / 3);
        Out.BuiltVertices = int32(Built.VertexBuffers.PositionVertexBuffer.GetNumVertices());
        Out.LayoutEqual = Buffer.GetNumTexCoords() == B.TexCoords &&
            Buffer.GetNumVertices() == uint32(Out.BuiltVertices) &&
            Buffer.GetUseHighPrecisionTangentBasis() == B.HighPrecisionTangents &&
            Buffer.GetUseFullPrecisionUVs() == B.FullPrecisionUV && Built.IndexBuffer.Is32Bit() == B.Main.Wide &&
            (Colors != 0) == !B.ColorRGBA.empty() && (!Colors || Colors == uint32(Out.BuiltVertices));
        Out.SectionsEqual = Built.Sections.Num() == int32(Original.Sections.size());
        for (int32 Section = 0; Out.SectionsEqual && Section < Built.Sections.Num(); ++Section)
        {
            const P4MapKit::FStaticMeshSection& From = Original.Sections[size_t(Section)];
            const ::FStaticMeshSection& To = Built.Sections[Section];
            Out.SectionsEqual = To.MaterialIndex == From.MaterialIndex && To.NumTriangles == From.Triangles &&
                To.bEnableCollision == From.Collision && To.bCastShadow == From.Shadow &&
                To.bForceOpaque == From.ForceOpaque && To.bVisibleInRayTracing == From.RayTracing;
        }
        // Corners are only comparable when both sides have the same sections and UV channels.
        if (Built.Sections.Num() == int32(Original.Sections.size()) && Buffer.GetNumTexCoords() == B.TexCoords)
        {
            Out.TrianglesEqual = SameTriangles(Original, Built, CornerAll);
            Out.EqualWithoutTangents = Out.TrianglesEqual ||
                SameTriangles(Original, Built, CornerAll & ~uint32(CornerTangents));
            Out.PositionsEqual = Out.EqualWithoutTangents || SameTriangles(Original, Built, CornerPosition);
            Out.LargestUVGap = Out.TrianglesEqual ? 0.f : UVGap(Original, Built);
            Out.EqualWithoutUVs = Out.LargestUVGap >= 0;
        }
        if (!Out.TrianglesEqual && Out.EqualWithoutTangents && Out.SectionsEqual && Out.LayoutEqual)
            Out.RebuiltTangentFrames = RebuiltFrames(Original, Built);
        Rebuilt = Rebuilt && Out.SectionsEqual && Out.LayoutEqual &&
            (Out.TrianglesEqual || Out.RebuiltTangentFrames >= 0);
        Report.RebuiltTangentFrames += FMath::Max(0, Out.RebuiltTangentFrames);
        Equal = Equal && Out.SectionsEqual && Out.LayoutEqual && Out.TrianglesEqual;
        // THRESH_UVS_ARE_SAME, the distance under which the engine's builder gives two such vertices one UV.
        Welded = Welded && Out.SectionsEqual && Out.LayoutEqual && Out.EqualWithoutUVs &&
            Out.LargestUVGap < 1.f / 1024.f;
        Report.LargestUVGap = FMath::Max(Report.LargestUVGap, Out.LargestUVGap);
        Report.LODs.Add(Out);
    }
    const FBoxSphereBounds& Bounds = Mesh->RenderData->Bounds;
    const float BuiltBounds[7] = {Bounds.Origin.X, Bounds.Origin.Y, Bounds.Origin.Z, Bounds.BoxExtent.X,
        Bounds.BoxExtent.Y, Bounds.BoxExtent.Z, Bounds.SphereRadius};
    Report.BoundsEqual = true;
    for (int32 K = 0; K < 7; ++K)
    {
        Report.SourceBounds[K] = Render.Bounds[size_t(K)];
        Report.BuiltBounds[K] = BuiltBounds[K];
        Report.BoundsEqual = Report.BoundsEqual && SameBits(BuiltBounds[K], Render.Bounds[size_t(K)]);
    }
    Report.ScreenSizesEqual = true;
    for (int32 L = 0; L < int32(Render.LODs.size()); ++L)
        Report.ScreenSizesEqual = Report.ScreenSizesEqual &&
            SameBits(Mesh->RenderData->ScreenSize[L].Default, Render.ScreenSizes[size_t(L)]);
    Report.Equal = Equal && Report.BoundsEqual && Report.ScreenSizesEqual;
    Report.EqualWithinEngineWeld = Welded && Report.BoundsEqual && Report.ScreenSizesEqual;
    Report.EqualWithinEngineTangents = Rebuilt && Report.BoundsEqual && Report.ScreenSizesEqual;
    return true;
}

FString P4BuildOriginalMeshReport(const FString& HeaderFile, const FString& BodyFile, const FString& ExportObject)
{
    FP4OriginalMeshSource Source;
    FP4MeshBuildReport Report;
    FString Error;
    TStrongObjectPtr<UStaticMesh> Mesh;
    bool Valid = P4DecodeOriginalMesh(HeaderFile, BodyFile, ExportObject, Source, Error);
    if (Valid)
    {
        UPackage* Transient = GetTransientPackage();
        Mesh.Reset(P4BuildOriginalMesh(Source, Transient,
            MakeUniqueObjectName(Transient, UStaticMesh::StaticClass(), TEXT("P4BuiltMesh")), RF_Transient, Report,
            Error));
        Valid = Mesh.IsValid();
    }
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("valid"), Valid);
    Result->SetStringField(TEXT("error"), Error);
    Result->SetStringField(TEXT("export_object"), Source.ExportObject.IsEmpty() ? ExportObject : Source.ExportObject);
    Result->SetStringField(TEXT("package"), Source.Package);
    Result->SetBoolField(TEXT("equal"), Valid && Report.Equal);
    Result->SetBoolField(TEXT("source_reconstructed"), Valid && Report.SourceReconstructed);
    Result->SetBoolField(TEXT("equal_within_engine_weld"), Valid && Report.EqualWithinEngineWeld);
    Result->SetBoolField(TEXT("equal_within_engine_tangents"), Valid && Report.EqualWithinEngineTangents);
    Result->SetNumberField(TEXT("rebuilt_tangent_frames"), Valid ? Report.RebuiltTangentFrames : -1);
    Result->SetNumberField(TEXT("largest_uv_gap"), Valid ? Report.LargestUVGap : -1.f);
    Result->SetBoolField(TEXT("bounds_equal"), Valid && Report.BoundsEqual);
    Result->SetBoolField(TEXT("screen_sizes_equal"), Valid && Report.ScreenSizesEqual);
    TArray<TSharedPtr<FJsonValue>> Missing, SourceBounds, BuiltBounds, LODs;
    for (const FString& Path : Report.MissingMaterials)
        Missing.Add(MakeShared<FJsonValueString>(Path));
    for (int32 K = 0; K < 7; ++K)
    {
        SourceBounds.Add(MakeShared<FJsonValueNumber>(Report.SourceBounds[K]));
        BuiltBounds.Add(MakeShared<FJsonValueNumber>(Report.BuiltBounds[K]));
    }
    for (int32 L = 0; L < Report.LODs.Num(); ++L)
    {
        const FP4MeshBuildLOD& From = Report.LODs[L];
        auto Item = MakeShared<FJsonObject>();
        Item->SetNumberField(TEXT("lod"), L);
        Item->SetNumberField(TEXT("triangles"), From.Triangles);
        Item->SetNumberField(TEXT("source_vertices"), From.SourceVertices);
        Item->SetNumberField(TEXT("built_vertices"), From.BuiltVertices);
        Item->SetBoolField(TEXT("sections_equal"), From.SectionsEqual);
        Item->SetBoolField(TEXT("layout_equal"), From.LayoutEqual);
        Item->SetBoolField(TEXT("triangles_equal"), From.TrianglesEqual);
        Item->SetBoolField(TEXT("equal_without_tangents"), From.EqualWithoutTangents);
        Item->SetBoolField(TEXT("positions_equal"), From.PositionsEqual);
        Item->SetBoolField(TEXT("equal_without_uvs"), From.EqualWithoutUVs);
        Item->SetNumberField(TEXT("largest_uv_gap"), From.LargestUVGap);
        Item->SetNumberField(TEXT("rebuilt_tangent_frames"), From.RebuiltTangentFrames);
        LODs.Add(MakeShared<FJsonValueObject>(Item));
    }
    Result->SetArrayField(TEXT("missing_materials"), Missing);
    Result->SetArrayField(TEXT("source_bounds"), SourceBounds);
    Result->SetArrayField(TEXT("built_bounds"), BuiltBounds);
    Result->SetArrayField(TEXT("lods"), LODs);
    // The mesh was only built to be compared. Nothing of it stays.
    if (Mesh.IsValid())
        Mesh->MarkPendingKill();
    FString Json;
    FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Json));
    return Json;
}

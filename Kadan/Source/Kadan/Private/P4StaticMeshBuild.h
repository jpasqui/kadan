#pragma once
#include "P4NumericContract.h"
#include "P4OriginalMeshStaging.h"

class UStaticMesh;

// What the engine built from one LOD, against the game's render data for that LOD.
struct FP4MeshBuildLOD
{
    int32 SourceVertices = 0, BuiltVertices = 0, Triangles = 0;
    // Section count, and each section's material, triangle count and flags.
    bool SectionsEqual = false;
    // UV channel count, UV and tangent precision, index width, and whether there is a color buffer.
    bool LayoutEqual = false;
    // Every triangle of every section, corner by corner: position, packed tangent and normal, every
    // UV channel and color. Triangle order inside a section and the corner a triangle starts at are
    // not compared: the engine reorders both when it builds a mesh.
    bool TrianglesEqual = false;
    // The same comparison with attributes left out, to say what differs when the full one fails.
    bool EqualWithoutTangents = false, EqualWithoutUVs = false, PositionsEqual = false;
    // The largest difference in any UV component between the game's triangles and the built ones,
    // when everything else is equal. The engine's builder gives vertices that share a position,
    // tangent, normal and color one UV when their UVs are closer than 1/1024 (THRESH_UVS_ARE_SAME).
    // -1 when it was not measured.
    float LargestUVGap = -1.f;
    // Vertices of the game's mesh whose tangent frame has no binormal (a zero vector, or a tangent along
    // the normal), given a frame of its own by the engine's mesh builder, when they are all that differs.
    // -1 when it was not measured or something else differs.
    int32 RebuiltTangentFrames = -1;
};
struct FP4MeshBuildReport
{
    TArray<FP4MeshBuildLOD> LODs;
    TArray<FString> MissingMaterials;
    bool BoundsEqual = false, ScreenSizesEqual = false;
    float SourceBounds[7] = {}, BuiltBounds[7] = {};
    // Every LOD equal, with equal bounds and screen sizes.
    bool Equal = false;
    // Direct source reconstruction is measured independently and remains
    // publication-gated until save/reload/repeated-build validation passes.
    bool SourceReconstructed = false;
    // Equal, or different only in UVs the engine's builder welded: vertices that share a position,
    // tangent, normal and color and are closer than 1/1024 in UV get one UV. Such a mesh is accepted
    // as imported (decided 2026-10-08) and LargestUVGap says by how much it differs.
    bool EqualWithinEngineWeld = false;
    // Equal, or different only in the tangent frames the engine's builder makes for vertices whose own
    // frame has no binormal. RebuiltTangentFrames says at how many vertices.
    bool EqualWithinEngineTangents = false;
    int32 RebuiltTangentFrames = 0;
    float LargestUVGap = 0.f;
};

// Makes an ordinary editable UStaticMesh from a decoded original mesh and builds it with the engine's
// own mesh builder, then compares what the engine built with the game's render data. Outer is the
// package the mesh is made in. The mesh is returned whatever the comparison found, for the caller to
// keep or discard; null with Error set when it could not be made.
// Not set here: collision, sockets, lightmap settings and the other tagged properties of the mesh.
KADAN_API UStaticMesh* P4BuildOriginalMesh(const FP4OriginalMeshSource& Source, UObject* Outer, FName Name,
    EObjectFlags Flags, FP4MeshBuildReport& Report, FString& Error, UStaticMesh* Existing = nullptr);
// The package metadata, set on a mesh this builds, that says whether the game draws all of the mesh's LODs with the
// first LOD's built lighting ("true" or "false").
static const TCHAR* const P4LODsShareStaticLightingKey = TEXT("Kadan.LODsShareStaticLighting");

// Independent readback of an existing/reloaded asset. No build or normalization;
// exact equality includes every original LOD's packed triangle attributes,
// layout, sections, bounds and screen sizes.
KADAN_API bool P4CompareOriginalMeshBuild(const FP4OriginalMeshSource& Source, UStaticMesh* Mesh,
    FP4MeshBuildReport& Report, FString& Error);

// Reads the mesh from the game's files, builds it in the transient package, reports the comparison as
// JSON and discards the mesh. Nothing is added to Content.
FString P4BuildOriginalMeshReport(const FString& HeaderFile, const FString& BodyFile, const FString& ExportObject);

#pragma once
#include "CoreMinimal.h"

class UStaticMesh;
struct FP4OriginalMeshImportResult;

// A digest of what a static mesh saves that a cook reads: its settings, its material slots, every corner
// of every triangle of every LOD's source geometry, its collision settings and its sockets. Left out are
// the things the engine rewrites without an edit: the lighting id a build renews, the collision id, the
// record of the file the mesh was imported from, and a slot's texture densities unless a person set
// them. Empty when the mesh has no source geometry.
KADAN_API FString P4StaticMeshState(UStaticMesh* Mesh);

// Private native acceptance entry point using the ordinary publication path.
KADAN_API FP4OriginalMeshImportResult P4ValidateReconstructedMeshImport(const FString& HeaderFile,
    const FString& BodyFile, bool SaveToDisk);
KADAN_API bool P4OriginalMeshBaselineUnchanged(UStaticMesh* Mesh);
// Refresh existing native derived navigation only for an unchanged, qualified
// original mesh. Never creates navigation objects or changes authored settings.
KADAN_API bool P4RefreshOriginalMeshNavigation(UStaticMesh* Mesh);
// Read-only bounded diagnostics using the same fields as the baseline state.
KADAN_API TMap<FString, FString> P4StaticMeshStateParts(UStaticMesh* Mesh);

// Imports one of the game's cooked static meshes as an ordinary static mesh at its game path in Content:
// built by the engine's own mesh builder from the game's vertices and compared with the game's render
// data, with the game's material slots, lightmap settings and collision settings, and a record of what
// it was imported as. Its materials have to be in the project, and so do the collision profile and the
// collision channels its file names (see P4CollisionSettings.h). A mesh already there is left alone when
// it came from the same game file and was not edited, and is never replaced otherwise. Returns JSON.
// The mesh is read from its header and body (the header's name with .uexp when BodyFile is empty) and
// the bulk files beside them.
FString P4ImportOriginalMesh(const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk);

// Whether a mesh is one the kit imported from the game, and whether it is still as imported.
FString P4OriginalMeshStatus(UStaticMesh* Mesh);

// The assets a mesh refers to: its materials.
void P4StaticMeshSaveReferences(UStaticMesh* Mesh, TArray<UObject*>& Out);

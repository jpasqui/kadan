#pragma once
#include "CoreMinimal.h"
#include "P4OriginalMeshStaging.h"

class UStaticMesh;
// Editor reconstruction only. The saved MeshDescription is the attribute source;
// this record contains only layout/settings which the engine fast path replaces.
KADAN_API bool P4RecordMeshSourceReconstruction(UStaticMesh* Mesh, const FP4OriginalMeshSource& Source, FString& Error);
KADAN_API bool P4RestoreMeshSourceReconstruction(UStaticMesh* Mesh, FString& Error);
KADAN_API bool P4HasMeshSourceReconstruction(const UStaticMesh* Mesh);
KADAN_API void P4StartMeshSourceReconstruction();
KADAN_API void P4StopMeshSourceReconstruction();

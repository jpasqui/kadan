#pragma once
#include "CoreMinimal.h"
class UStaticMesh;

struct FP4CreatorSurfaceAdvice
{
    FString Code, Message;
    int32 MaterialSlot = INDEX_NONE;
};

// Read-only help for the selected-assets page. This does not alter geometry,
// material graphs, painted masks or game-owned meshes.
KADAN_API TArray<FP4CreatorSurfaceAdvice> P4InspectCreatorSurface(UStaticMesh* Mesh);
KADAN_API FString P4MeshCollisionExplanation(const UStaticMesh* Mesh);
// A creator mesh whose FBX import did not take the file's vertex colors (Vertex Color Import Option Ignore or
// Override) has one color on every vertex. When one of its materials reads vertex colors, the sentence that
// says so and how to reimport it; empty otherwise.
KADAN_API FString P4IgnoredVertexColorsNote(UStaticMesh* Mesh);

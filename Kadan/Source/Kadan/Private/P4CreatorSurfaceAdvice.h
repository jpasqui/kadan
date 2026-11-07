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

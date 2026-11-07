#pragma once
#include "CoreMinimal.h"

// Raw native shape only. This does not establish stand-in trust, unchanged provenance,
// source fidelity, or permission to fill an asset.
struct FP4SkinnedPlaceholderShape
{
    bool Observed = false;
    FString ObjectPath;
    FString ClassPath;
    bool IsSkeleton = false;
    bool IsSkeletalMesh = false;
    bool ImportedModelPresent = false;
    int32 ImportedLODCount = -1;
    bool RenderDataPresent = false;
    int32 RenderLODCount = -1;
    bool NativeRollbackShapeSupported = false;
};

// Reads the already loaded object. Does not allocate native models/render data, load an
// asset, initialize resources, wait for caches, or change its package/dirty state.
KADAN_API FP4SkinnedPlaceholderShape P4InspectSkinnedPlaceholderShape(UObject* Object);

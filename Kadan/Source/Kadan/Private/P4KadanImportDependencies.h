#pragma once

#include "CoreMinimal.h"

struct FP4KadanImportPlan;

inline bool P4KadanImportFollowsSoftReferences(const FString& Kind)
{
    return Kind == TEXT("World") || Kind == TEXT("ActorPlacementInfoAsset") ||
           Kind == TEXT("ActorPlacementInfoListAsset");
}

// Includes current source references to existing packages. Material execution can
// discover more resources later. Does not load or write editor assets.
KADAN_API bool P4KadanImportKnownRequiredReferences(const FP4KadanImportPlan& Plan,
                                                    const TSet<FString>& Selected, TArray<FString>& Required,
                                                    FString& Error);

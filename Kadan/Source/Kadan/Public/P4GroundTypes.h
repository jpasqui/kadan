#pragma once
#include "CoreMinimal.h"

class AActor;
class UStaticMeshComponent;
struct KADAN_API FP4GroundType
{
    FString Name;
    uint32 Tag = 0;
};

// Choices come from actual imported game collision components and their mesh material slots.
KADAN_API TArray<FP4GroundType> P4ImportedGroundTypes();
KADAN_API bool P4ReadGroundTags(UStaticMeshComponent* Component, TArray<uint32>& Out);
// Added scenery keeps the game's native collision component and one terrain tag per material slot.
// Game actors already present in an original level are left to the supported map-change routes.
KADAN_API bool P4SetCreatorGroundType(AActor* Actor, const FP4GroundType& Type, FString& Error);

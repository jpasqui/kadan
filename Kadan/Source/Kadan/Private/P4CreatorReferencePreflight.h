#pragma once
#include "CoreMinimal.h"
#include "P4ExportDriver.h"
#include "P4ActorAssetValuePlanning.h"

class AActor;
class UWorld;
struct FP4LevelChangesResult;

// Native read-only inputs. Typed actor provenance stays attached to its value;
// this inspection never rewrites map values, compiles, acquires, or stages.
struct FP4CreatorReferenceInputs
{
    TArray<FString> Packages;
    TArray<AActor*> AddedActors;
    TArray<FP4ActorAssetReference> AssetValues;
};

KADAN_API bool P4InspectLevelCreatorReferences(UWorld* World, const FP4LevelChangesResult& Changes,
    FP4CreatorReferenceInputs& Out, FString& Error);
// The explicit capability argument is a native exporter/qualification seam;
// the panel always passes the public production capability.
KADAN_API FP4ExportAvailability P4CreatorPreflightAvailability(UObject* Asset,
    const FP4ModDescriptor& Mod, bool CustomMaterials);
KADAN_API bool P4PreflightCreatorReferences(const FP4ModDescriptor& Mod,
    const FP4CreatorReferenceInputs& Inputs, const FP4ExportGameInventory* Inventory,
    const TMap<FName, FString>& Claims, bool CustomMaterials, TArray<FString>& OutPackages,
    FString& Error);

#pragma once
#include "CoreMinimal.h"
#include "P4ExportContainer.h"
#include "P4WorldExportGraph.h"

struct KADAN_API FP4ExportMapGroup
{
    FString Map;
    TArray<FString> Levels;
};

// Resolve each edited level through the project's real streaming references to its original map.
// The open map is a useful hint, but does not limit which maps a mod can contain.
KADAN_API bool P4GroupExportMaps(const TArray<FString>& EditedLevels, const FP4ExportGameInventory& Inventory,
    const FString& OpenMap, TArray<FP4ExportMapGroup>& Out, FString& Error,
    bool* MissingPersistentMap = nullptr);

// Group the inspected streaming graphs without saving, cooking or changing editor objects.
KADAN_API bool P4GroupExportMapGraphs(const TArray<FString>& EditedLevels,
    const TArray<FP4WorldStreamingGraphResult>& Graphs, TArray<FP4ExportMapGroup>& Out, FString& Error,
    bool* MissingPersistentMap = nullptr);

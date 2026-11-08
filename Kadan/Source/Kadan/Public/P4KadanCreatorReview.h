#pragma once
#include "P4ExportSelection.h"

class UWorld;
class FP4ExportGameInventory;
struct FP4KadanCreatorReview
{
    TArray<FP4ExportSelectionRow> Rows;
    TArray<FString> Blockers;
    int32 Ships = 0, UsesGame = 0;
    bool ManifestOnly = false;
    bool CanExport() const { return (Ships > 0 || ManifestOnly) && Blockers.Num() == 0; }
};

// The imported game levels of the open map with changes of their own that the review's rows leave out, by package.
// Their changes do not ship: export takes the levels a mod has, never the map's others.
KADAN_API TArray<FString> P4ChangedLevelsOutsideSelection(
    UWorld* OpenMap, const FP4ExportGameInventory& Inventory, const FP4KadanCreatorReview& Review);

// The creator-facing readiness check uses the same supported routes as export.
// It never saves, bakes, cooks or publishes. Export independently validates again.
KADAN_API FP4KadanCreatorReview P4ReviewCreatorSelection(
    const FP4ModDescriptor& Mod, const FP4ExportSelectionPreview& Preview, UWorld* OpenMap,
    const FP4ExportGameInventory* Inventory = nullptr, const TMap<FName, FString>* Claims = nullptr,
    const FString& GameFolder = FString());

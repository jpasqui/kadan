#pragma once
#include "P4ExportDriver.h"
#include "P4KadanCreatorReview.h"

struct FP4KadanModReview
{
    FP4ExportGameInventory Inventory;
    FP4ExportSelectionPreview Selection;
    FP4KadanCreatorReview Creator;
};

// Both the panel and scripting review the same selection and other project mods.
KADAN_API bool P4KadanReviewMod(FP4ExportModRequest& Request, UWorld* OpenMap,
    FP4KadanModReview& Out, FString& Error);
KADAN_API bool P4KadanExportMod(FP4ExportModRequest Request, UWorld* OpenMap,
    FP4ExportModResult& Out, FString& Error);

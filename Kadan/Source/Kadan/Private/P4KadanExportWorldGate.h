#pragma once
#include "P4ExportSelection.h"

// Table field edits do not need a persistent World. A map is required only when
// the selected native census actually contains original World changes.
inline bool P4KadanSelectionNeedsWorld(const FP4ExportSelectionPreview& Preview)
{
    return Preview.Rows.ContainsByPredicate([&](const auto& Row)
    {
        return Row.AssetClass == TEXT("World") && Preview.GameEditCandidates.Contains(Row.Package);
    });
}

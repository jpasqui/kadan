#pragma once

#include "P4ExportSelection.h"

KADAN_API bool P4KadanExportPackageIsSelected(
    const FString& Package,
    const TArray<FP4ExportRoot>& Roots,
    const TArray<FP4ExportSelectionRow>& ReviewedRows);

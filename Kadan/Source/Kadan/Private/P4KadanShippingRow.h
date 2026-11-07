#pragma once

#include "CoreMinimal.h"
#include "P4KadanExportIssues.h"
#include "Widgets/Views/STableRow.h"

struct FP4KadanShippingActions
{
    TFunction<void()> LocateAsset;
    TFunction<void()> LocateRoot;
    TFunction<void()> RemoveRoot;
    TFunction<void()> OpenMap;
    TFunction<void()> FindMap;
};

TSharedRef<ITableRow> P4KadanShippingRow(const FP4ExportSelectionRow& Row, const FP4KadanExportIssue& Issue,
                                         const FString& Disposition, TAttribute<bool> ReviewCurrent,
                                         FP4KadanShippingActions Actions,
                                         const TSharedRef<STableViewBase>& Owner);

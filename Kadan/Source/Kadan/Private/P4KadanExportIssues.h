#pragma once
#include "CoreMinimal.h"
#include "P4ExportSelection.h"

struct FP4KadanExportIssue
{
    FString Asset, Cause, NextStep, Detail, SelectingRoot;
    bool NeedsPersistentMap = false;
};

// Unknown diagnostics remain exact technical details rather than guessed repairs.
KADAN_API FP4KadanExportIssue P4KadanExportIssue(const FP4ExportSelectionRow& Row);
KADAN_API FString P4KadanExportDetails(const FString& Error, const TArray<FString>& Refused);

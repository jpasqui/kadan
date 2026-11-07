#pragma once
#include "CoreMinimal.h"

class UWorld;

// Caller supplies the original lines only after validating their recorded digest.
// Reconcile a pruned redundant source override's identity; property lines remain exact.
void P4WithInheritedTemplateAliases(UWorld* World, const TArray<FString>& Imported, TArray<FString>& Content);

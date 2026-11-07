#pragma once
#include "CoreMinimal.h"

// Renames a validated prepared folder and restores the previous folder on failure.
KADAN_API bool P4PublishPreparedExport(const FString& Prepared, const FString& Target, bool HadTarget,
                                       TArray<FString>& Told, FString& Error);

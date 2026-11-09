#pragma once
#include "CoreMinimal.h"

// Renames a validated prepared folder and restores the previous folder on failure.
KADAN_API bool P4PublishPreparedExport(const FString& Prepared, const FString& Target, bool HadTarget,
                                       TArray<FString>& Told, FString& Error);

// Opens a lock file for this process alone, or null when another export or editor holds it. A lock file left behind
// by an editor that closed in the middle (a crash, or Unreal ended from Task Manager) holds nothing: nobody has it
// open, so it is deleted and taken.
KADAN_API TUniquePtr<FArchive> P4HoldLockFile(const FString& Path);

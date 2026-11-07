#pragma once

#include "CoreMinimal.h"

// Choose reusable private staging on the output volume without creating anything.
// Mounted directories are excluded because Unreal scans Saved and Content.
KADAN_API FString P4ExportWorkingDirectory(
    const FString& OutputDirectory,
    const FString& RequestedWorkDirectory,
    const FString& ModId,
    const TArray<FString>& MountedDirectories);

#pragma once
#include "CoreMinimal.h"

struct FP4KadanPathDefaults
{
    FString GameFolder, GamePaks, Retoc, Repak, ExportFolder;
    TArray<FString> MissingTools;
};

// Read-only discovery. Never creates directories, runs programs or downloads tools.
// Valid explicit executable paths take precedence over portable/project/PATH candidates.
KADAN_API FP4KadanPathDefaults P4KadanDiscoverPaths(const FP4KadanPathDefaults& Current,
    const FString& PluginRoot, const FString& ProjectRoot, const FString& SavedRoot, const FString& EnvironmentPath);

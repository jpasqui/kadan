#pragma once
#include "CoreMinimal.h"
class UWorld;
KADAN_API void P4WithDroppedVisibilityIds(UWorld* World, const TArray<FString>& Imported, TArray<FString>& Current);
// Puts the imported texture streaming data of every component that is still there in place of what the editor
// built for it since. That data is what a lighting or texture streaming build works out from the meshes and
// textures a component draws, not a change of the person's, and it cannot be changed by hand.
KADAN_API void P4WithImportedStreamingTextureData(const TArray<FString>& Imported, TArray<FString>& Current);

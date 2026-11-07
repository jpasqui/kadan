#pragma once
#include "CoreMinimal.h"
class UWorld;
class ULandscapeMaterialInstanceConstant;
class FJsonObject;
class FJsonValue;
// Drawing cache only. No original parent/parameter, source comparison, or
// exporter admission is changed by these source-derived base-color previews.
KADAN_API bool P4CacheOriginalLandscapeColorPreviews(UWorld*, const FString& SourceDigest,
                                                     const TArray<TSharedPtr<FJsonValue>>& Recipes, FString& Error);
KADAN_API int32 P4ApplyOriginalLandscapeColorPreviews(UWorld*);
KADAN_API void P4RestoreOriginalLandscapeColorPreviews();
KADAN_API void P4StartOriginalLandscapeColorPreviews();
KADAN_API void P4StopOriginalLandscapeColorPreviews();
KADAN_API int32 P4OriginalLandscapeColorPreviewCount();
// What a cached preview is checked against: the owner's own values, empty when it cannot be checked.
KADAN_API FString P4OriginalLandscapeOwnerGuard(ULandscapeMaterialInstanceConstant*);

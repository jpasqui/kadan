#pragma once
#include "CoreMinimal.h"
#include "P4MaterialUsedResources.h"
class UMaterialParameterCollection;
class FShaderParametersMetadata;
// The explicit completion notification is ONLY for the native importer,
// immediately AFTER its real ConditionalPostLoad returned. Never a Ready fixup.
KADAN_API void P4MaterialCollectionPostLoadCompleted(UMaterialParameterCollection*);
KADAN_API void P4StartMaterialCollectionReadiness();
KADAN_API void P4StopMaterialCollectionReadiness();
// Read-only typed source/native proof; never cooks, PostLoads, saves or writes
// provenance. Ordinary native import owns resource creation and cook comparison.
KADAN_API bool P4OriginalMaterialCollectionMatchesSource(UMaterialParameterCollection*,
    const FP4MaterialUsedResourceRequest&,const FP4KadanSourceBinding&,FString& Error);

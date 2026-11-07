#pragma once
#include "CoreMinimal.h"
struct FP4KadanImportPlan;
struct FP4KadanSourceAsset;
// A map reimport need not replace an animation whose incoming source and complete
// current native state still match. Explicit animation reimports never use this path.
KADAN_API bool P4RetainUnchangedAnimationDependency(const FP4KadanImportPlan& Plan, const FP4KadanSourceAsset& Source,
                                                    UObject*& Existing, FString& Error);

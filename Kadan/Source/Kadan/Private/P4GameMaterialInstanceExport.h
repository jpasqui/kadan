#pragma once

#include "CoreMinimal.h"

class UMaterialInstanceConstant;
struct FP4ExportAvailability;

// Reuse the normal instance validator while retaining the original package route.
bool P4GameMaterialInstanceAvailability(UObject* Asset, const FString& Root, bool CustomMaterials,
                                        FP4ExportAvailability& Out);
KADAN_API FString P4MaterialInstanceExportIssue(UMaterialInstanceConstant* Instance, const FString& Root, bool CustomMaterials);

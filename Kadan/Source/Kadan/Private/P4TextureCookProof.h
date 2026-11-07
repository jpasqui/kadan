#pragma once
#include "CoreMinimal.h"
// Exact typed properties, layout and compressed mip byte hashes; offsets/storage may change in packing.
// Both arguments are actual cooked .uasset files with their .uexp/.ubulk companions.
bool P4VerifyCookedTextureContent(const FString& Expected, const FString& Actual, FString& Error);

#pragma once
#include "CoreMinimal.h"
#include <vector>
KADAN_API bool P4TranslateRecoveredFoliageCook(std::vector<uint8>& Header, std::vector<uint8>& Body, FString& Error);
KADAN_API void P4RefreshRecoveredFoliageDefaults();

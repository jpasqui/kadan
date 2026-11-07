#pragma once
#include "CoreMinimal.h"
#include <vector>

// Restore only the known editor storage class's game import identity. Tagged
// export bytes are preserved; this is not a renderer or an edited-export route.
KADAN_API bool P4TranslateRecoveredLightCook(std::vector<uint8>& Header, std::vector<uint8>& Body, FString& Error);

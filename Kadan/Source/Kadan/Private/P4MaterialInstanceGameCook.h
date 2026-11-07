#pragma once
#include "CoreMinimal.h"
#include <vector>
bool P4PackageHasMaterialInstanceC4(UPackage* Package);
bool P4TranslateMaterialInstanceC4Cook(UPackage* Package, std::vector<uint8>& Header, std::vector<uint8>& Body,
                                       FString& Error);

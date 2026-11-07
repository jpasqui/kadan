#pragma once
#include "CoreMinimal.h"

// Original source identity may use an exact qualified editor storage child. This
// grants no compatibility to arbitrary subclasses or to other source base types.
P4BLUEPRINTIMPORT_API bool P4BlueprintSourceSubobjectClassMatches(const UClass* Actual, const UClass* Declared);

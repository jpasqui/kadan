#pragma once
#include "CoreMinimal.h"
class UActorComponent;

// Called only for authoritative replacements admitted to an original actor family.
KADAN_API bool P4RestoreOriginalSplineCurves(UActorComponent* Old, UActorComponent* New);

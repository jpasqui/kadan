#pragma once
#include "CoreMinimal.h"
class UWorld;
// Temporary editor visibility only. Original, unchanged meshes with collision and no material
// in any slot, in the game or on the component, are collision helpers. The Outliner can
// reveal them; their components, collision, source record and cooked state remain.
KADAN_API int32 P4HideOriginalCollisionHelpers(UWorld* World);

#pragma once
#include "CoreMinimal.h"

class AActor;
class UActorComponent;

// Called only after the original-level/import-world admission check. The engine's
// actual replacement map supplies identity; no component-name lookup is permitted.
KADAN_API int32 P4RemapOriginalWeakComponentReferences(const TMap<UObject*, UObject*>& Replacements,
    UActorComponent* Old, UActorComponent* New, AActor* Actor);

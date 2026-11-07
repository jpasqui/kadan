#pragma once
#include "CoreMinimal.h"

// Registry discovery may load only Blueprints whose exact native parent is already
// registered as an actor. The post-load game ancestry/placeability checks still apply.
KADAN_API bool P4VisitLoadedActorBlueprintParent(const FString& ParentExportText, TFunctionRef<void()> Load);

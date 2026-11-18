#pragma once
#include "CoreMinimal.h"
// The Details panel of one of the game's gameplay objects starts with Game Options: the properties of the object
// and of its components that its placement row holds (core/placement_layouts.json), which are the options the game
// reads for that object. The other properties stay where they are.
class AActor;
class FProperty;
class UObject;
// The options of a gameplay object its placement row holds, in the order of its row's blobs: the object or its
// component that holds each, and the property. Empty for an actor that is not a gameplay object.
KADAN_API TArray<TPair<UObject*, FProperty*>> P4GameOptionsOf(AActor* Actor);
KADAN_API void P4RegisterGameOptionsDetails();
KADAN_API void P4UnregisterGameOptionsDetails();

#pragma once
#include "CoreMinimal.h"
// The Details panel of one of the game's gameplay objects starts with Game Options: the properties of the object
// and of its components that its placement row holds (core/placement_layouts.json), which are the options the game
// reads for that object. The other properties stay where they are; the game's ones that cannot reach the game are shown
// but not editable.
class AActor;
class FProperty;
class UObject;
// The options of a gameplay object its placement row holds, in the order of its row's blobs: the object or its
// component that holds each, and the property. Empty for an actor that is not a gameplay object.
KADAN_API TArray<TPair<UObject*, FProperty*>> P4GameOptionsOf(AActor* Actor);
// Whether a change to the property of one of a gameplay object's objects reaches the game: an option its row holds in a
// blob Kadan can write for this placement, a row setting, or an engine property. A new placement uses the same
// whole-blob eligibility as export. The panel shows the others without letting them be edited.
KADAN_API bool P4GameOptionReachesGame(AActor* Actor, UObject* Object, FProperty* Property);
KADAN_API void P4RegisterGameOptionsDetails();
KADAN_API void P4UnregisterGameOptionsDetails();

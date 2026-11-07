#pragma once
#include "CoreMinimal.h"

struct FBodyInstance;

// Collision settings name a collision profile and response channels, and what those names mean is the
// project's (Project Settings, Collision). When collision settings load, the engine reads the named
// profile from the project, renames a profile the project does not have to Custom, and drops responses
// to channels the project does not have, so the next save loses them.
//
// This does to the settings what a load does and says what would not survive it: the profile or the
// channel the project does not have, or the profile the project defines differently. Empty when the
// settings come through unchanged, and they are then as a loaded asset's are.
KADAN_API FString P4CollisionProjectIssue(FBodyInstance& Instance, UObject* Owner);

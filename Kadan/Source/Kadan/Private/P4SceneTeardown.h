#pragma once
#include "CoreMinimal.h"

// UE 4.26 keeps every FPrecomputedVolumetricLightmap in one global map (SourceRegistries in
// PrecomputedVolumetricLightmap.cpp) with no lock. A ULevel adds its volume when it is constructed
// and removes it in FinishDestroy, both on the game thread. Every FScene holds one as well, and a
// world's scene is deleted on the rendering thread after the world's FinishDestroy. So when a
// world with a scene is collected, the rendering thread removes from the map while the game
// thread goes on removing the volumes of dying levels or adding those of levels it loads. The
// map's free list breaks, and a later level construction stops the editor with
// "Assertion failed: !AllocationFlags[Index]" (SparseArray.h line 95).
//
// Kadan makes a world with a scene for every level it imports and lets the garbage collector take
// them, many at once when the import opens the imported map. The guard keeps the two threads
// apart: when a dying world still has its scene, the volumes of the dying levels are removed
// first (after their render removal is done), and the collection waits for the rendering thread
// before it returns. Started with the module.
void P4StartSceneTeardownGuard();
void P4StopSceneTeardownGuard();
bool P4SceneTeardownGuardActive();

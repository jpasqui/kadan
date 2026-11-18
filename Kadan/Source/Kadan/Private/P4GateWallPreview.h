#pragma once
#include "CoreMinimal.h"
class AActor;
class UStaticMeshComponent;
class UWorld;
// A gate's wall as the game builds it when the gate begins play (core/gate_wall.h): the pieces its length option
// (AddWallNum) adds, and the far side, pillars, boxes and collisions moved and stretched to that length. The
// stand-in gate class runs none of the game's code, so the editor shows a gate as its level was saved, or as its
// class has it. The preview adds the pieces as transient components that no save holds, and moves the map's own
// components with the map's values kept: a save or an export (FP4SuspendNativePreviews) sees the map as it is.
// The editor's navigation sees the moved collisions and boxes. A gate is drawn again when its options change, when
// it is moved or placed, and after an undo.

// Every gate of the world; how many are drawn.
KADAN_API int32 P4ApplyGateWallPreviews(UWorld* World);
// One gate; false when it is not a gate whose class adds walls (bAddWall), or while previews are suspended.
KADAN_API bool P4ApplyGateWallPreview(AActor* Gate);
// The pieces the preview gave the gate, in the order the game adds them.
KADAN_API TArray<UStaticMeshComponent*> P4GateWallPreviewPieces(AActor* Gate);
// Whether the actor's native class is the game's gate (VarGateBaseActor) or one of its kind.
KADAN_API bool P4IsGateWall(const AActor* Actor);
KADAN_API void P4StartGateWallPreviews();
KADAN_API void P4StopGateWallPreviews();

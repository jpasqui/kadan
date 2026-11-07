#pragma once

#include "CoreMinimal.h"
#include "P4LevelChanges.h"

class UWorld;
class ULevel;

// One validated choice, retained from the Play click through cooking and warp.
// Floor contact is separate because a deliberately elevated Player Start must
// retain its actor-centre height. No base-placement policy is implied by it.
struct KADAN_API FP4GamePreviewStartResult
{
    TWeakObjectPtr<UWorld> World;
    FTransform Transform = FTransform::Identity;
    FVector FloorPoint = FVector::ZeroVector;
    FVector FloorNormal = FVector::UpVector;
    bool HasFloor = false;
    FString FingerprintText() const;
};

// A session's clean loaded-editor state, separate from retained game-import
// provenance. Connection owns this snapshot and resets it on world changes.
struct KADAN_API FP4GamePreviewBaseline
{
    struct FSource
    {
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<ULevel> Level;
        FP4LevelChangesResult Changes;
    };
    TWeakObjectPtr<UWorld> Root;
    TArray<FSource> Sources;
    void Reset();
    bool IsFor(UWorld* World) const;
};

// Captures all loaded imported source levels only while their packages are
// clean. A failed capture leaves Out intact. No source is saved or re-baselined.
KADAN_API bool P4CaptureGamePreviewCleanBaseline(UWorld* World, FP4GamePreviewBaseline& Out, FString& Error);

// Pure semantic comparison used by the session guard. Removes only unchanged
// baseline differences; edits to a baseline difference also fail, including
// reverting it. MutableStarts must contain only verified native Player Starts.
KADAN_API bool P4GamePreviewDeltaSinceBaseline(const FP4LevelChangesResult& Baseline,
    FP4LevelChangesResult& Current, FString& Error, const TSet<FString>& MutableStarts = TSet<FString>());

// Resolves an explicit Player Start for the next game preview, using yaw only as
// UE4's SpawnDefaultPawnFor does. A scratch level without a Player Start uses the
// active perspective editor camera, projected onto collision below it.
//
// True supplies a safe actor-centre transform. False with an empty Error keeps
// the game's own start on an imported level without an explicit Player Start;
// false with an Error refuses an unsafe or missing authored start. No objects,
// selection, navigation or packages are changed. The default clearance is the
// native APlayerStart capsule; callers with proven pawn dimensions may override it.
KADAN_API bool P4GamePreviewStart(UWorld* World, bool bScratchWorld,
    FTransform& OutTransform, FString& Source, FString& Error,
    float CapsuleRadius = 40.f, float CapsuleHalfHeight = 92.f);
KADAN_API bool P4GamePreviewStart(UWorld* World, bool bScratchWorld,
    FP4GamePreviewStartResult& Out, FString& Source, FString& Error,
    float CapsuleRadius = 40.f, float CapsuleHalfHeight = 92.f);

// Explicit Play-time admission for dirty imported levels. Every dirty loaded
// level must still match its imported semantic baseline except for added or
// edited Player Starts. This may perform a temporary diagnostic cook; never use
// it for UI polling. It neither saves the source nor clears its dirty flag.
// Scratch levels have their own whole-world preview and bypass this check.
KADAN_API bool P4GamePreviewOnlyStartChanges(UWorld* World, FString& Error);

// The same conservative imported-level comparison also permits added actors
// that the live mirror can recreate: populated static-mesh actors, game actors,
// and compiled creator Blueprint actors in the current persistent level. Existing
// game actors may only change when they are real Player Starts. Removed actors,
// level/build/streaming changes and unsupported additions still require export.
KADAN_API bool P4GamePreviewOnlyLiveChanges(UWorld* World, FString& Error,
    const FP4GamePreviewBaseline* Baseline = nullptr);

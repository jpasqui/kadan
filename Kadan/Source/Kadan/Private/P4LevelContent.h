#pragma once
#include "CoreMinimal.h"

class UWorld;
class AActor;
class UInstancedStaticMeshComponent;

// What a level is, as a digest: the level is saved the way a cook saves it, to a file that is read back
// and thrown away, and the digest is of what that file holds, object by object: each object's place,
// class, properties and native data. Left out is what the engine makes for itself: the id and the
// reference of the level's lighting build data, the address the level was last saved under and the
// lighting id of its BSP model. Empty when the level cannot be saved that way.
KADAN_API FString P4LevelState(UWorld* World);
// What that digest is of, as text: one line for each thing the level says, in a fixed order. Two of
// these side by side show what an edit changed.
FString P4LevelContentText(UWorld* World);

// Imports one of the game's cooked levels as an ordinary Unreal level at its game path in Content.
// The level is made new by the engine: its world settings, its level script, and its actors, spawned
// from their classes, with the components the level adds. Every object is then made what a load of the
// game's file would make it: what its archetype has, and on top of that what the file says. The level is
// saved the way a cook saves it and compared with the game's file object by object, and a level that
// differs is not kept. The construction scripts are then run, as a load runs them, and what such a run
// does not keep is told in the result.
//
// Nothing the game's file holds is left out without a word. A level with anything this does not bring
// over yet (BSP geometry of its own, level script code, an instanced component that is not foliage, an
// actor of a class the project does not have) is refused whole, by name. What is the engine's or the game's own code's
// to make is not compared and is listed in the result. The project's world settings class has to be
// the one the game's file names, or the engine replaces the world settings when the level loads.
//
// A level already there is left alone when it came from the same game file and was not edited, and is
// never replaced otherwise. Returns JSON.
FString P4ImportOriginalLevel(const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk);

// Whether a level is one the kit imported from the game, and whether it is still as imported.
FString P4OriginalLevelStatus(UWorld* World);
// What an edit to an imported level amounts to: which of the game's actors were taken out, which were
// moved and to where, which actors were added, and what else changed (another change to one of the
// game's actors, a change to the level itself), line by line. The first three are what a list of level
// changes for the mod loader holds. Told against what the level said when it was imported, which is
// kept with the level. Returns JSON.
FString P4OriginalLevelChanges(UWorld* World);
FString P4WriteActorNavigationCollision(AActor* Actor, const FString& File);
FString P4EraseFoliage(UWorld* World, const FVector& Center, float Radius);
FString P4AddFoliage(UWorld* World, UInstancedStaticMeshComponent* Kind, const FVector& Location, const FRotator& Rotation, const FVector& Scale);
// Why an added actor is not one a lighting build of the mod's own can light. Empty when it is one.
KADAN_API FString P4WhyNotLitByABuildOfTheMods(const class AActor* Actor);

// Whether a Blueprint class of the project says what the game's cooked file of the class says: its
// default object, its component templates and its construction script's nodes, each with what it is
// made from. Its code is not compared here. A level says only what differs from its classes, so this
// is what makes a level compared with the game's file the same level. Returns JSON with what differs
// on each side.
FString P4CompareOriginalClass(const FString& HeaderFile, const FString& BodyFile);

// Imports one of the game's cooked assets that is properties only (a data asset, a curve, a set of
// environment values) as an ordinary asset of its class at its game path: made of its class, given what
// the game's file says on top of what the class has, saved the way a cook saves it and compared with the
// game's file. An asset that holds native data, or anything the reader does not read, is refused by
// name. Kinds that have an importer of their own (textures, meshes, materials, levels) hold such data
// and so never come through here. Returns JSON.
//
// ReferenceOnly is for an asset with native data, which an object of a class without the game's code
// cannot hold: the asset is still made, with its properties, so that what refers to it can be brought
// over, and it is recorded as for reference only, with the size of what it does not hold. Its status
// says so, and it is never to go into a mod: the game has its own.
FString P4ImportOriginalObjectAsset(const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk, bool ReferenceOnly = false);
// Whether an asset was imported that way, and whether it is still as imported. Returns JSON.
FString P4OriginalObjectAssetStatus(UObject* Asset);
// The digest of what a cook's save of such an asset holds.
KADAN_API FString P4OriginalObjectAssetState(UObject* Asset);
// Explicit native diagnostic: actual cooked semantic lines plus any admitted source-bound
// comparison projection used by state comparison. Projected GUIDs need not be literal cook bytes.
// Not a UI poll; performs one temporary native cook without changing comparison admission.
KADAN_API FString P4OriginalObjectAssetStateAndLines(UObject* Asset, TArray<FString>& OutLines);
// Separate copied-content Review boundary. One ordinary temporary cook, fully
// decoded root MIC plus only its separately validated intrinsic role if emitted.
// No original comparison projection, capture, or baseline mutation.
bool P4MaterialInstanceCopyCookBoundary(UObject* Asset, const UObject* ValidatedRole, FString& Error);
// Native test seam: mutation controls over one actual cook, never live values.
KADAN_API bool P4MaterialInstanceCopyBoundaryControls(UObject* Asset, const UObject* ValidatedRole, FString& Error);
// Old empty-source vector references require current source bytes and an actual
// cook reproducing their existing v1 digest. This never creates a baseline.
struct FP4OriginalRecord;
KADAN_API bool P4LegacyEmptyVectorFieldState(UObject* Asset, const FP4OriginalRecord& Record, FString& Out, FString& Error);
bool P4IsOriginalObjectAsset(UObject* Asset);
// The names of the game's actors in an imported level, as the level said when it was imported. Empty for
// a level that was not imported from the game.
KADAN_API TSet<FName> P4ImportedLevelActors(UWorld* World);


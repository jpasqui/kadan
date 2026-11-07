#pragma once
#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "P4ExportDriver.h"

class AActor;
class UWorld;

// A gameplay object: an actor of one of the game's actor classes (GActor). The game saves what it
// makes of these only when they come from a placement list, so export puts them in one, never in a level.
KADAN_API bool P4IsGameplayActor(const AActor* Actor);
// Why a placed gameplay object cannot be a row of a placement list. Empty when it can.
KADAN_API FString P4PlacementIssue(const AActor* Actor);

// The placement list of one map: an ActorPlacementInfoAsset whose first row is the mark row, and an
// ActorPlacementInfoListAsset that holds it. Both live in packages of the mod's own that exist only in
// memory until they are cooked; P4ReleasePlacementList takes them out again.
struct FP4PlacementList
{
    FString Package, ListPackage;
    TStrongObjectPtr<UObject> Asset, ListAsset;
    uint64 MarkId = 0;
    TArray<FP4PlacementRow> Rows;
};

// Builds the list for the gameplay objects placed in the edits of MapPackage. Each row names its class by
// path, takes the actor's place, team and generator settings, and gets a placement number of the mod's own.
// A blob is written only when the creator changed one of its options, in full, from the actor's values.
KADAN_API bool P4BuildPlacementList(const FString& ModId, const FString& PackageRoot, const FString& MapPackage,
    const TArray<AActor*>& Actors, FP4PlacementList& Out, FString& Error);
KADAN_API void P4ReleasePlacementList(FP4PlacementList& List);

// A row of one of the game's placement lists, as the game's file has it: its number, its class, its versions
// and every blob that is not empty, by the row's name and half ("AI.Static").
struct FP4GamePlacementRow
{
    uint64 Id = 0;
    FString List, Class;
    uint64 AssetVersion = 0, GeneratorVersion = 0;
    TMap<FString, TArray<uint8>> Blobs;
};
// Reads the rows of every placement list (AP_*) in a folder of the game's files.
KADAN_API bool P4ReadGamePlacementLists(const FString& Folder, TArray<FP4GamePlacementRow>& Rows, FString& Error);
// The folder of the game's files that holds the placement lists of the map a level belongs to, or empty.
KADAN_API FString P4GamePlacementFolder(const FString& GameRoot, const FString& LevelPackage);

// What a level's gameplay objects took from the game's placement lists.
struct FP4PlacementOptionsReport
{
    // The gameplay objects that have a row, and the values set from the rows' blobs.
    int32 Rows = 0, Values = 0;
    TArray<AActor*> Changed;
    // Gameplay objects without a row, by name.
    TArray<FString> WithoutRow;
    // Blobs whose layout is not read yet, by class and blob ("GDemoAreaBox_C DemoTrigger.Static"), with how many.
    TMap<FString, int32> NotRead;
    // Values that could not be set or that the actor does not hold as the game has them, one line each.
    TArray<FString> NotHeld;
};
// Gives the gameplay objects of an imported level the values the game's placement lists hold for them.
KADAN_API FP4PlacementOptionsReport P4ApplyGamePlacementOptions(UWorld* World, const TArray<FP4GamePlacementRow>& Rows);
// The row of the game's placement lists for a gameplay object, by its placement number. Null when it has none.
KADAN_API const FP4GamePlacementRow* P4GamePlacementRowOf(const AActor* Actor, const TArray<FP4GamePlacementRow>& Rows, FString* Why = nullptr);
// The same by what the actor was: its placement number, the name of its level and its class path.
KADAN_API const FP4GamePlacementRow* P4GamePlacementRowFor(uint64 Id, const FString& Level, const FString& Class,
    const TArray<FP4GamePlacementRow>& Rows, FString* Why = nullptr);
// The package of one of the game's placement lists (List is its asset name, AP_...) of the map a level belongs to.
KADAN_API FString P4GamePlacementListPackage(const FString& LevelPackage, const FString& List);

struct FP4LevelChangesResult;
// The game rows behind the gameplay objects a level edit moves, changes or removes, by actor name. Rows are the
// game's rows of the level's map (P4ReadGamePlacementLists); null when they could not be found, which refuses an
// edit of a gameplay object that has a placement number.
KADAN_API bool P4GameRowsOfLevelEdit(UWorld* World, const FP4LevelChangesResult& Changes, const TArray<FP4GamePlacementRow>* Rows,
    TMap<FString, FP4GameRowEdit>& Out, FString& Error);
// Where a gameplay object's values differ from what its row's decoded blobs say, one line each. Fields that the
// stream counts or that no property holds are left out; blobs that are not read are told as such.
KADAN_API TArray<FString> P4PlacementOptionDifferences(AActor* Actor, const FP4GamePlacementRow& Row);

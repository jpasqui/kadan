#pragma once
#include "CoreMinimal.h"
class UPackage;
class UDataTable;
class AActor;
class FProperty;
class SWidget;
struct FAssetData;
namespace P4Kadan
{
void RefreshReadOnlyAssets();
KADAN_API void UpdateReadOnlyAsset(UObject* Asset);
void InitializeMutationGuards();
void ShutdownMutationGuards();
KADAN_API void InitializePropertyGuards();
void ShutdownPropertyGuards();
void InitializeReadOnlyRouting();
void ShutdownReadOnlyRouting();

// A package the shared family/source policy locks, including unrecovered stand-ins.
// Answered from the editor's own list of locked packages, so it is cheap.
KADAN_API bool IsLockedPackage(const UPackage* Package);
KADAN_API bool IsLockedPackageName(FName Package);
// The lock mark the Content Browser shows on a locked asset's tile. A null widget for any other asset.
KADAN_API TSharedRef<SWidget> LockMark(const FAssetData& Asset);
// An object that belongs to a locked game asset: the asset, anything inside its package, and the default
// objects of the game's own classes.
KADAN_API bool IsLockedObject(const UObject* Object);

// Whether Tsuchi can apply a change of this property on one of the game's actors in an imported level,
// or on one of its components. Why says why not, in a sentence that names the actor and the property.
KADAN_API bool CanEditGameProperty(UObject* Object, const FProperty& Property, const TArray<const FProperty*>& Parents = {}, FString* Why = nullptr);
// Whether a field of a game table's row can be edited: values Tsuchi writes into a row, and the length
// of an array of them.
KADAN_API bool CanEditTableProperty(const FProperty& Property, const TArray<const FProperty*>& Parents = {});
// The game's actor that Object is, or that Object is part of, in a level imported from the game. Null
// for an actor a creator added and for anything outside an imported level.
KADAN_API AActor* GameActorOf(const UObject* Object);
// The rows a data table had in the game, in the game's order. Empty for a table that is not the game's.
KADAN_API TArray<FName> GameRowsOf(const UDataTable* Table);
// Whether new rows of this table make a new kind of treasure, creature or Pikmin.
KADAN_API bool IsKindTable(const UDataTable* Table);

// Puts back what an editor, a script or a utility changed on a locked asset, and refuses level and table
// edits Tsuchi cannot apply. Runs on the editor's tick; tests call it to have it run at once.
KADAN_API void FlushReadOnlyGuards();
// What Kadan told the person since the last call, in the order it was told.
KADAN_API TArray<FString> TakeReadOnlyMessages();
// Tells the person once, in the editor's notifications and the log.
void Tell(const FString& Message);
}

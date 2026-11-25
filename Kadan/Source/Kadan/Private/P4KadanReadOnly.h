#pragma once
#include "CoreMinimal.h"
class UDataTable;
class UPackage;
class AActor;
class FProperty;
class SWidget;
struct FAssetData;
namespace P4Kadan
{
KADAN_API void TrackAssetSource(UObject* Asset);
void AcceptCompletedCreatorSource(UObject* Asset);
void InitializeMutationGuards();
void ShutdownMutationGuards();
KADAN_API void InitializePropertyGuards();
void ShutdownPropertyGuards();
void InitializeAssetActions();
void ShutdownAssetActions();
KADAN_API bool IsLockedPackage(const UPackage* Package);
KADAN_API bool IsLockedPackageName(FName Package);
KADAN_API bool IsLockedObject(const UObject* Object);
KADAN_API TSharedRef<SWidget> LockMark(const FAssetData& Asset);

// Whether Tsuchi can apply a change of this property on one of the game's actors in an imported level,
// or on one of its components. Why says why not, in a sentence that names the actor and the property.
KADAN_API bool CanEditGameProperty(UObject* Object, const FProperty& Property, const TArray<const FProperty*>& Parents = {}, FString* Why = nullptr);
// The game's actor that Object is, or that Object is part of, in a level imported from the game. Null
// for an actor a creator added and for anything outside an imported level.
KADAN_API AActor* GameActorOf(const UObject* Object);
// Whether new rows of this table make a new kind of treasure, creature or Pikmin.
KADAN_API bool IsKindTable(const UDataTable* Table);

// Restores incomplete reference assets and refuses imported actor edits Tsuchi cannot apply.
KADAN_API void FlushReadOnlyGuards();
// What Kadan told the person since the last call, in the order it was told.
KADAN_API TArray<FString> TakeReadOnlyMessages();
// Tells the person once, in the editor's notifications and the log.
void Tell(const FString& Message);
}

#pragma once
#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class AActor;
class FP4GameLink;
class UObject;
class UWorld;
class ULevel;
class UTransBuffer;
struct FPropertyChangedEvent;

// Preview actors live in a transient level. Original actors remain unchanged until the user
// explicitly keeps selected simulation changes, using Unreal's own property copier and Undo.
class KADAN_API FP4GameMirror
{
public:
    explicit FP4GameMirror(FP4GameLink& InLink, const FString& InOwner = FString());
    ~FP4GameMirror();

    void Tick(double Now);
    void Attach(UWorld* InWorld);
    void Clear();
    bool CanKeepSimulationChanges() const;
    int32 KeepSimulationChanges();
    int32 Count() const { return Entries.Num(); }
    int32 Missing() const { return MissingClasses.Num(); }

    // The game handle of an actor in the level: a live copy, or a level actor with the game's placement number.
    FString HandleOf(const AActor* Actor) const;
    // The creator packages this Play sent to the game. An actor placed during Play that needs
    // anything else of the creator's is not spawned, and the editor says to press Play again.
    void SetPlayContent(const TArray<FString>& Packages);
    // The short name of the first creator asset the actor needs that this Play did not send, or empty.
    FString NotInPlay(const AActor* Actor) const;

private:
    friend class FP4GameMirrorEditsTest;
    struct FEntry
    {
        FString Handle, Kind, ClassPath;
        uint64 Placement = 0;
        FVector At = FVector::ZeroVector, Shown = FVector::ZeroVector;
        FRotator Turn = FRotator::ZeroRotator;
        TWeakObjectPtr<AActor> Actor;      // the live copy, when one is spawned
        TWeakObjectPtr<AActor> Anchor;     // transient parent, preserving an attached original's coordinate space
        TWeakObjectPtr<AActor> LevelActor; // the level's own actor for it
        bool bAsked = false;
        bool bUserPlaced = false;
        bool bOrdinary = false, bPendingSpawn = false;
        bool bCooked = false; // already in the cooked scratch world; never owned by preview cleanup
        FName EditorFolder;
        double MovedByUser = 0;
    };

    void Query();
    void AskClasses();
    void Place(FEntry& Entry);
    void Spawn(FEntry& Entry);
    void SpawnPlaced(const FString& EditorKey, FEntry& Entry);
    void BindCookedActors();
    bool BindCookedActor(AActor* Original, const FString& Handle, const FVector& At, const FRotator& Turn);
    void Forget(FEntry& Entry);
    void OnSelection(UObject* Selection);
    void OnPropertyChanged(UObject* Object, FPropertyChangedEvent& Event);
    void OnActorMoved(AActor* Actor);
    void OnActorAdded(AActor* Actor);
    void OnActorDeleted(AActor* Actor);
    void PullFields(AActor* Actor, const FString& Handle);
    // Sends the values a placed actor holds apart from its class's defaults (the ones Keep Simulation Changes kept
    // in the level, say) to its spawned copy in the game.
    void PushFields(AActor* Actor, const FString& Handle);
    void OriginalPresentation(bool bPreview);
    FEntry* EntryOf(const AActor* Actor);

    FP4GameLink& Link;
    FString Owner;
    bool bWaiting = false, bClassesWaiting = false, bApplying = false;
    double NextQuery = 0;
    bool bBindCooked = false, bCookedWaiting = false;
    double NextCookedBind = 0;
    int32 CookedBindCursor = 0, CookedBindPasses = 0, CookedQueryCursor = 0;
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<ULevel> LiveLevel, PreviousLevel;
    struct FOriginal
    {
        TWeakObjectPtr<AActor> Actor;
        bool bEditable = true, bHidden = false;
    };
    TArray<FOriginal> Originals;
    // Pending UDP replies must not touch a stopped or destroyed mirror.
    TSharedPtr<uint8> Lifetime = MakeShared<uint8>(0);
    TWeakObjectPtr<UTransBuffer> Transactions;
    TMap<FString, FEntry> Entries;
    TSet<FString> MissingClasses;
    TSet<FString> PlayPackages;
    bool bHasPlayContent = false;
    FDelegateHandle SelectionHandle, PropertyHandle, MovedHandle, AddedHandle, DeletedHandle, BeforeUndoHandle, UndoHandle, RedoHandle;
    FDelegateHandle PreSaveHandle, PostSaveHandle;
};

#pragma once
#include "CoreMinimal.h"

class UWorld;

enum class EP4LevelChangesError : uint8
{
    None, EditorThreadRequired, LevelRequired, ImportedLevelRequired,
    ImportedComparisonRequired, CookFailed, StateUnreadable
};

struct FP4LevelLineChanges
{
    TArray<FString> Before, After;
};

struct FP4LevelActorMove
{
    FString Actor;
    FVector Location = FVector::ZeroVector;
    FRotator Rotation = FRotator::ZeroRotator;
    FVector Scale = FVector::OneVector;
};

struct FP4LevelActorChanges
{
    FString Actor;
    FP4LevelLineChanges Lines;
};

// What the imported level said of an actor that is no longer there: its class path and, for a gameplay
// object, its placement number (GenerateInfo.DebugUniqueId), 0 when the level gave it none.
struct FP4RemovedActor
{
    FString Class;
    uint64 PlacementNumber = 0;
};

// Explicit semantic comparison. This may cook a temporary package and also
// supplies the original-level comparison used by export selection.
struct FP4LevelChangesResult
{
    EP4LevelChangesError Error = EP4LevelChangesError::None;
    FString ErrorDetail;
    FString Package, Level, State, ComparisonSchema = TEXT("level-lines-v1"), WorldSettings;
    bool Unchanged = false, HeldByLevelChanges = false;
    TArray<FString> Removed, Added, EditorNavigation;
    TMap<FString, FP4RemovedActor> RemovedActors;
    TArray<FP4LevelActorMove> Moved;
    TArray<FP4LevelActorChanges> OtherChanged;
    FP4LevelLineChanges LevelChanges;
    TArray<FString> NavigationRelevant, LitByGame, AddedUnlit;
    bool IsValid() const { return Error == EP4LevelChangesError::None; }
};

KADAN_API FP4LevelChangesResult P4AnalyzeOriginalLevelChanges(UWorld* World);
FString P4LevelChangesErrorText(const FP4LevelChangesResult& Result);
KADAN_API FString P4LevelChangesJson(const FP4LevelChangesResult& Result);

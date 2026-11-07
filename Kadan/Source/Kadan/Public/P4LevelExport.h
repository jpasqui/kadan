#pragma once
#include "CoreMinimal.h"

class UWorld;

enum class EP4LevelExportError : uint8
{
    None,
    EditorThreadRequired,
    LevelRequired,
    ChangesFailed,
    ClassDefaultsMismatch,
    ReferenceCrossesLevel,
    LightingSetupFailed,
    OriginalComparisonMissing,
    ReplacementNotChanged,
    ConstructionAddsValues,
    ActorMissing,
    NoAddedActors,
    InvalidModPath,
    OccupiedModPath,
    FolderRequired,
    AbsoluteFolderRequired,
    WorldCreationFailed,
    CopyFailed,
    LightingDataMissing,
    FolderCreationFailed,
    CookFailed,
    CookedActorMismatch
};

struct FP4LevelUnlitActor
{
    FString Actor, Reason;
};

struct FP4LevelExportResult
{
    EP4LevelExportError Error = EP4LevelExportError::None;
    FString ErrorDetail;
    FString SourcePackage, Package, Level, LitLevel;
    bool HasLevelDecision = false, LevelMade = false, LightingConsidered = false, CountsReady = false,
         HasObjectDifference = false;
    TArray<FString> Actors, Files, Replaced, LitActors, LentClasses, PulledActors;
    TArray<FP4LevelUnlitActor> UnlitActors;
    TArray<FString> MissingLines, ExtraLines, DifferentObjectLines;
    int32 ChildActors = 0, RestoredConstructionValues = 0, RestoredGameValues = 0, CullDistancesOmitted = 0,
          GameValuesFromClass = 0, LightingComponents = 0, InstancesWithoutGameLighting = 0, Objects = 0, Lines = 0;
    bool IsValid() const { return Error == EP4LevelExportError::None; }
};

// The actors added to an imported level, and the changed actors of the game's named in Replaced, as a
// level of the mod's own at ModLevel. It is saved the way a cook saves it under Folder, where it lies as
// it does in the game's content, ready to pack. Nothing of it is kept in the project. LitLevel, when
// given, takes the added actors a lighting build of the mod's own can light, as a level held in the
// editor at that path and never saved. The caller lights it, cooks it and throws it away.
// An imported game level as it is in the editor, saved the way a cook saves it at Base (.umap and .uexp), by the
// same cook an import compares with the game's file.
KADAN_API bool P4CookWholeOriginalLevel(UWorld* World, const FString& Base, FString& Error);
// Leave names added actors that go elsewhere (the gameplay objects, which go into a placement list).
// WhyNotBaked, when given, says why an added static mesh cannot have baked lighting. Empty means it can.
KADAN_API FP4LevelExportResult P4ExportOriginalLevelActors(UWorld* World, const FString& ModLevel,
    const FString& Folder, const TArray<FString>& Replaced = {}, const FString& GameFiles = FString(),
    const FString& LitLevel = FString(), const TArray<FString>& Leave = TArray<FString>(),
    const TFunction<bool(UWorld*, FString&)>& PrepareCook = nullptr,
    const TFunction<FString(const AActor*)>& WhyNotBaked = nullptr);

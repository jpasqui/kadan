#pragma once
#include "CoreMinimal.h"

class UWorld;

enum class EP4OriginalLevelImportError : uint8
{
    None, EditorThreadRequired, SourceUnreadable, DecodeFailed, InvalidGamePath,
    UnsupportedContent, OccupiedDestination, RecordValidationRequired, EditedOriginal,
    EarlierImportReplacementUnsupported, BuildFailed, CookFailed, CookDiffers,
    StateUnreadable, RecordFailed, SaveFailed
};

struct KADAN_API FP4OriginalLevelImportResult
{
    EP4OriginalLevelImportError Error = EP4OriginalLevelImportError::None;
    FString ErrorDetail, Package, AssetPath, SourceDigest, ImportedState, State;
    UWorld* Asset = nullptr;
    bool Created = false, Saved = false;
    // A projected editor layer. The game's complete original remains the runtime layer; only
    // reconstructed editor actors participate in overlay edits. This is not full-source equality.
    bool GameBacked = false;
    TArray<FString> UneditableOriginalActors;
    FString GameBackedReason;
    bool ActorCountsKnown = false;
    int32 OriginalActorCount = 0, EditableOriginalActorCount = 0;
    bool IdentityKnown = false, ClassesKnown = false, BuildDetailsKnown = false,
        ValuesKnown = false, TemplatesKnown = false, DifferencesKnown = false,
        ComparedKnown = false, ConstructionKnown = false, BaselineKnown = false;
    int32 Objects = 0, ClassesCompared = 0, EquivalentParentTemplates = 0,
        Differences = 0, Compared = 0, NotKeptCount = 0, AddedCount = 0;
    TArray<FString> DifferingClasses, LentClasses, ClassProblems, UnshippedReferences,
        ProjectOnlyObjects, ValuesNotHeld, OnlyGameSays, OnlyLevelSays,
        EngineMadeNotCompared, NotKept, Added;
    // What the level's gameplay objects took from the map's placement lists: how many have a row, how many
    // values were given, the objects without a row, the blobs not read yet by class (with how many), and the
    // values that could not be given.
    bool PlacementKnown = false;
    int32 PlacementRows = 0, PlacementValues = 0;
    TArray<FString> PlacementWithoutRow, PlacementNotRead, PlacementNotHeld;
    bool IsValid() const { return Error == EP4OriginalLevelImportError::None; }
};

KADAN_API FP4OriginalLevelImportResult P4ImportOriginalLevelNative(const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk);
// Rebuild through the normal complete-source importer, then replay creator changes against the new
// verified baseline. The existing package is retained until the rebuilt map and edits pass validation.
KADAN_API FP4OriginalLevelImportResult P4ReimportOriginalLevelNative(UWorld* World, const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk);
// Explicit best-effort reimport may improve an existing projection when full
// reconstruction still refuses. Existing editable actors and creator edits must survive.
KADAN_API FP4OriginalLevelImportResult P4ReimportOriginalLevelWithGameReferencesNative(UWorld* World,
    const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk, const TSet<FName>& UnavailablePackages);
// Resume preserves an existing original layer and its creator edits. It verifies actual source
// identity/hash and the imported baseline; it never cooks, reconstructs, migrates or re-baselines.
KADAN_API FP4OriginalLevelImportResult P4ResumeOriginalLevelNative(UWorld* World, const FString& HeaderFile, const FString& BodyFile);
KADAN_API FP4OriginalLevelImportResult P4ImportGameBackedLevelNative(const FString& HeaderFile, const FString& BodyFile,
    bool SaveToDisk, const TSet<FName>& UnavailablePackages, const FString& Reason, bool TrySupportedActors = true,
    const TSet<FString>& OmitActors = TSet<FString>());
// Validates source identity custody against the imported editor baseline. Omitted names are reserved
// for the original game actors and cannot be reused by creator additions.
KADAN_API bool P4ReadGameBackedLevelCustody(UWorld* World, TSet<FName>& OmittedActors, FString& SourceDigest, FString& Error);
// A source file a level's custody names, written so that the project still finds it when it is copied
// to another folder or machine: {project}/... inside the project, {game}/... inside the game folder set
// on Kadan's Import page, otherwise the full path. Resolve gives the full path back.
KADAN_API FString P4PortableSourceCustodyPath(const FString& FullPath);
KADAN_API FString P4ResolveSourceCustodyPath(const FString& Stored);
// Editor presentation only: load/show actual retained common and selected variant layers.
// No original asset is saved or re-baselined; ExpectedPackages records the source graph selection.
KADAN_API bool P4ActivateOriginalMapEditorLayers(UWorld* World, const FString& Variant, TSet<FName>& ExpectedPackages, FString& Error);
// Run before an imported map's world initializes. Its original layers start shown, as in the map the import made, and
// opening the map then selects the layers. Saving writes the selection into the map, and a layer that loads hidden
// never registers its components, while Unreal's navigation still reads every component of every loaded level.
KADAN_API void P4ShowOriginalLayersForOpen(UWorld* World);
KADAN_API FString P4OriginalLevelImportJson(const FP4OriginalLevelImportResult& Result);

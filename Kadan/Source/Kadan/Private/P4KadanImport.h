#pragma once
#include "CoreMinimal.h"
#include "P4OriginalAssetImport.h"
#include "P4KadanExecutionEdges.h"

struct FP4KadanSourceAsset
{
    FString Package, Kind, Header, Body;
    // Every game package this one names. The ones it names only by soft reference (streaming levels,
    // soft class and object paths) are in SoftDependencies too: they set no import order and may form cycles.
    TArray<FString> Dependencies, SoftDependencies;
    int64 SourceBytes = 0;
};
struct FP4KadanImportSettings
{
    FString GameFolder, GamePaks, Retoc, ShaderArchive;
    // Actual game inventory validation for preserving unsupported original asset references.
    FString Repak;
    // Where packages converted from the containers are kept for the next import (empty: Saved/Kadan/ConvertedOriginals).
    FString ConvertedCache;
    // Off: a level is imported whole or the import fails with the reason (strict checks).
    // On (the editor's import): best effort. A package that cannot be rebuilt stays the game's, a
    // level keeps the actors that need it in the game, and the import goes on with a warning.
    bool AllowGameBackedLevels = false;
};
struct FP4KadanImportPlan
{
    TArray<TSharedPtr<FP4KadanSourceAsset>> Assets;
    // Every package the scan found. Material recovery merges a master from all of its instances.
    TArray<TSharedPtr<FP4KadanSourceAsset>> Scanned;
    TArray<FString> Problems, Warnings;
    // The warnings above that name a package the plan did not find, by that package. An import that
    // reads the package from the game's containers after all takes the warning back.
    TMap<FString, FName> MissingWarnings;
    // Actual native class-typed table fields whose values can follow successful class
    // declarations. Their hard non-Blueprint dependents wait at the same boundary.
    TMap<FName, TArray<FP4OriginalTableClassReference>> DeclarationReferences;
    TSet<FName> DeclarationDeferred;
    // Native animation assets can refer back through notify/link metadata.
    // Declare these source-typed identities before filling either side.
    TSet<FName> CyclicAnimations;
    // Exact requested roots, distinct from their expanded execution closure.
    // Empty on legacy/manual plans. Only settings-backed material-role plans
    // rebuild from these current-read identities.
    TArray<FString> RequestedPackages;
    TMap<FName,FString> RequestedSourceDigests;
    bool bMaterialExecutionDeferred = false;
    FP4KadanExecutionEdges ExecutionEdges;
    int32 Roots = 0, Dependencies = 0, SharedDependencies = 0, AlreadyPresent = 0, GameReferences = 0;
    int64 SourceBytes = 0, RemainingSourceBytes = 0;
};
struct FP4KadanImportResult
{
    bool Ok = false, Cancelled = false;
    TArray<FString> Problems, Warnings;
    // Packages this run made, with their kind, and the packages it did not import (failed, or skipped
    // because a package they need did not import).
    TMap<FString, FString> Imported;
    TArray<FString> NotImported, ReferenceOnly;
    // Objects a Blueprint names that the game does not ship. Their references are empty, as in the game.
    TArray<FString> EmptyReferences;
    // Game assets a Blueprint names that stay the game's, with nothing in the project in their place:
    // Kadan could not import them even for reference. The Blueprint keeps the game's value with a note.
    TArray<FString> KeptAsTheGames;
    // Per-package reconstruction diagnosis for valid Blueprint identities
    // retained in the original game instead of reconstructed/editor assets.
    TMap<FString, FString> GameReferenceReasons;
    int32 LevelObjects = 0, OriginalIdentityPackages = 0;
    // The saved report (Saved/Kadan/import-result.json) as JSON text.
    FString Report;
};
// Reads only headers plus companion file lengths. No package payload copies or editor asset loads.
KADAN_API bool P4KadanScanGame(const FString& Folder, TArray<TSharedPtr<FP4KadanSourceAsset>>& Assets, TArray<FString>& Problems);
KADAN_API FP4KadanImportPlan P4KadanPlanImport(const TArray<TSharedPtr<FP4KadanSourceAsset>>& Assets, const TSet<FString>& Roots, bool Resume = true, bool DeferMaterialExecution = false);
// Selected roots absent from the extracted folder are read from the configured original containers.
// Uses one reusable Saved folder; all readers and the normal importer consume those actual sources.
KADAN_API FP4KadanImportPlan P4KadanPlanImport(const FP4KadanImportSettings& Settings, const TSet<FString>& Roots, bool Resume = true);
// Find existing game imports by their own recorded source identity, including legacy records.
// Catalog is the entire source catalog, independent of browser filters. Creator copies and replacements
// are excluded. Cancellation returns false without publishing a partial selection. Never saves assets.
KADAN_API bool P4KadanFindImportedPackages(const TSet<FString>& Catalog, TSet<FString>& Imported);
// Imported packages are preserved on subsequent runs. Refusals and per-asset replies are checkpointed
// in Saved/Kadan/import-result.json, so cancellation leaves resumable completed imports. A package that
// fails is reported and skipped with the packages that need it, and the rest of the plan goes on.
KADAN_API FP4KadanImportResult P4KadanRunImport(const FP4KadanImportSettings& Settings, const FP4KadanImportPlan& Plan, bool Resume);
// Scan, plan and run for one map (or any one package) and everything it names.
KADAN_API FP4KadanImportResult P4KadanImportMap(const FString& GameFolder, const FString& MapPackage, bool Resume = true);
KADAN_API FP4KadanImportResult P4KadanImportMap(const FP4KadanImportSettings& Settings, const FString& MapPackage, bool Resume = true);

// Seconds between the import report's writes while an import runs (the finished report is always written).
extern KADAN_API double P4KadanReportInterval;
class FP4KadanContainerIndex;
// One container listing per source identity in an editor session. Failed listings are never cached.
KADAN_API bool P4KadanReadContainerIndex(const FP4KadanImportSettings& Settings, FP4KadanContainerIndex& Index,
    FString& Error, bool* FromCache = nullptr);
// The project's cache of packages converted from the containers, emptied when the containers or retoc
// change. Empty when it cannot be made (conversion then goes straight into the import's folder).
KADAN_API FString P4KadanConversionCache(const FP4KadanImportSettings& Settings);
// Read-only preflight: proves the retained cache has the actual current conversion
// identity. Never creates, refreshes or invalidates a cache or its stamp.
KADAN_API bool P4KadanRetainedConversionCacheCurrent(const FP4KadanImportSettings& Settings,
    FString& Cache, FString& Error);
// Puts one game package from the containers into Into (as retoc converts it), converting it only when the
// project's cache does not hold it yet. FromCache tells whether retoc was spared.
KADAN_API bool P4KadanConvertOriginal(const FP4KadanImportSettings& Settings, const FP4KadanContainerIndex& Index,
    const FString& Package, const FString& Into, FString& Error, bool* FromCache = nullptr);

struct FP4BlueprintImportRunResult;
// Actual map admission of the native batch: declaration presence alone is insufficient.
KADAN_API FP4KadanImportResult P4KadanBlueprintBatchResult(const FP4BlueprintImportRunResult& Result,
    const TArray<TSharedPtr<FP4KadanSourceAsset>>& Selected,
    const TSet<FName>& VerifiedGameReferencePackages = {});

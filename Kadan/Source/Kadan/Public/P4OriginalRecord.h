#pragma once
#include "CoreMinimal.h"

class UObject;
class FJsonObject;

// Source metadata and retained values. No saved-package fingerprints or sidecar records.
enum class EP4OriginalState : uint8 { NotImported, Unchanged, Dirty, SavedEdit, NeedsMigration, Unreadable, UnsupportedRecord, Recorded };

struct KADAN_API FP4OriginalValueNote
{
    FString Kind, Object, Property, GameValue, Action;
};

struct KADAN_API FP4OriginalRecord
{
    int32 Version = 2;
    FString Package, Class, Kind, SourceDigest, SourceDigestKind;
    FString ComparisonDigest, ComparisonSchema, Fidelity = TEXT("exact");
    bool ReferenceOnly = false, ReadOnly = false;
    TArray<FP4OriginalValueNote> Values;
    // Retain the old record verbatim. Migration never changes the old content proof or its meaning.
    FString LegacyKey, LegacyEvidence;
};

struct KADAN_API FP4OriginalStatus
{
    EP4OriginalState State = EP4OriginalState::NotImported;
    FP4OriginalRecord Record;
    FString Error;
    bool IsOriginal() const { return !Record.Package.IsEmpty(); }
    bool IsUnchanged() const { return State == EP4OriginalState::Unchanged; }
};

enum class EP4OriginalComparison : uint8 { Equal, Different, NotCompared, Unreadable };
struct KADAN_API FP4OriginalDiagnostic
{
    FP4OriginalStatus Provenance;
    EP4OriginalComparison Comparison = EP4OriginalComparison::NotCompared;
    FString CurrentDigest, Error;
};

// Both reads are side-effect free. They never save/cook or rewrite an older record.
KADAN_API bool P4ReadOriginalRecord(UObject* Asset, FP4OriginalRecord& Out, FString& Error);
KADAN_API FP4OriginalStatus P4QueryOriginalAsset(UObject* Asset);
// Explicit content comparison. Levels/objects may perform a temporary cook; never use for UI polling.
KADAN_API FP4OriginalDiagnostic P4DiagnoseOriginalAsset(UObject* Asset);
// Store source metadata before the ordinary engine save.
KADAN_API bool P4MarkOriginalAsset(UObject* Asset, const FP4OriginalRecord& Record, FString& Error);
KADAN_API bool P4CaptureOriginalRecord(UObject* Asset, FP4OriginalRecord Record, FString& Error);
// Compatibility check at an import save boundary; validates source metadata without writing files.
KADAN_API bool P4CaptureValidatedOriginal(UObject* Asset, FString& Error);
KADAN_API bool P4MarkLegacyOriginalAsset(UObject* Asset, const TArray<FP4OriginalValueNote>& Notes, FString& Error, const FString& ComparisonSchema = FString());
KADAN_API bool P4IsVerifiedStandIn(UObject* Asset);
// Current copied game data, from explicit metadata or intrinsic source carried by native copies.
// A completed supported creator replacement can supersede current data while retaining old history.
KADAN_API bool P4HasCopiedGameData(const UObject* Asset);
// Explicit, one-time validation of an old record. Does not save or modify the asset. The caller's
// validator uses the old comparison schema; an edited asset is never re-baselined.
KADAN_API bool P4MigrateOriginalRecord(UObject* Asset, const TFunction<bool()>& MatchesLegacy, FString& Error);
// Compatibility fields for the thin Native JSON adapters; unknown state is distinct from edited.
KADAN_API void P4OriginalStatusFields(const FP4OriginalStatus& Status, FJsonObject& Out);

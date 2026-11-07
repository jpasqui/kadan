#pragma once
#include "CoreMinimal.h"
#include "P4SkinnedReports.h"

class UObject;

enum class EP4OriginalAssetImportError : uint8
{
    None, EditorThreadRequired, SourceUnreadable, DecodeFailed, TablesUnreadable,
    MultipleTopObjects, InvalidGamePath, PropertiesUnreadable, NativeDataUnsupported,
    ObjectOutsideAsset, PlaceholderClassMismatch, EditedPlaceholder, SaveRequired,
    StagingFailed, BackupUnreadable, OccupiedDestination, RecordValidationRequired,
    EditedOriginal, EarlierImportReplacementUnsupported, ClassUnavailable,
    EngineObjectConflict, PropertyApplicationFailed, NativeFillFailed, InvalidRowType,
    RowPropertyUnavailable, RowPropertyApplicationFailed, DuplicateRowName,
    CookFailed, CookDiffers, NativeComparisonFailed, StateUnreadable, RecordFailed, SaveFailed
};

struct KADAN_API FP4OriginalAssetImportResult
{
    EP4OriginalAssetImportError Error = EP4OriginalAssetImportError::None;
    FString ErrorDetail, Package, AssetPath, Class, SourceDigest, ImportedState, State;
    UObject* Asset = nullptr;
    bool Created = false, Saved = false, FilledStandIn = false;
    bool ObjectsKnown = false, AssetIdentityKnown = false, RowsKnown = false;
    bool ImportedValuesKnown = false, DifferencesKnown = false, ComparedKnown = false;
    bool EngineMadeKnown = false, FilledStandInKnown = false, BaselineKnown = false;
    bool ReferenceOnly = false, HasNativeData = false, HasMeasured = false;
    int32 Objects = 0, Rows = 0, UnshippedReferences = 0, Differences = 0, Compared = 0;
    int32 NativeBytesNotHeld = 0, ValuesNotHeldCount = 0;
    // The game's values for what its engine adds to this engine's classes, kept as tags and proved by the
    // comparison (P4StockTags), and why any were not kept.
    int32 GameFieldsKept = 0;
    TArray<FString> GameFieldsRefused;
    TArray<FString> ValuesNotHeld, OnlyGameSays, OnlyAssetSays, GameEngineMade, EngineMade;
    FP4SkinnedFillReport NativeData;
    FP4SkinnedCompareReport Measured;
    bool IsValid() const { return Error == EP4OriginalAssetImportError::None; }
};

KADAN_API FP4OriginalAssetImportResult P4ImportOriginalAssetNative(const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk, bool ReferenceOnly = false);
KADAN_API FString P4OriginalAssetImportJson(const FP4OriginalAssetImportResult& Result);

struct FP4OriginalTableClassReference
{
    FString Field, ObjectPath, MetaClassPath;
};
// Read-only admission for tables deferred until genuine Blueprint declarations exist.
// Every requested package must be used by a serialized, native class-typed field.
// Object/CDO references, unknown schemas and ambiguous import identities are refused.
KADAN_API bool P4OriginalTableDeclarationReferences(const FString& HeaderFile, const FString& BodyFile,
    const TSet<FName>& TargetPackages, TArray<FP4OriginalTableClassReference>& References, FString& Error);
// Discover only used generated-class values through actual native FClassProperty
// schemas, then apply the same complete declaration-only proof to that target set.
// Unused imports, object/CDO values and soft paths do not select new Blueprints.
KADAN_API bool P4OriginalTableUsedClassReferences(const FString& HeaderFile, const FString& BodyFile,
    TArray<FP4OriginalTableClassReference>& References, FString& Error);

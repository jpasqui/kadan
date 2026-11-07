#pragma once
#include "CoreMinimal.h"
class UTexture;

enum class EP4OriginalTextureImportError : uint8
{
    None, RequiresEditorThread, SourceUnreadable, BuildFailed, SourceChanged,
    BuildDiffers, UnusableGamePath, BuiltSourceUnreadable, PlaceholderClassDiffers,
    EditedPlaceholder, PlaceholderFillNeedsSave, PlaceholderBackupUnreadable,
    DestinationOccupied, RecordNeedsValidation, EditedTexture, CreateFailed,
    CopyFailed, RecordFailed, SaveRefused
};

struct FP4OriginalTextureImportBaseline
{
    FString SourceDigest, State, GameFormat, GameGroup;
    int32 Width = 0, Height = 0, Slices = 0, Mips = 0, FirstMip = 0;
};

struct KADAN_API FP4OriginalTextureImportResult
{
    EP4OriginalTextureImportError Error = EP4OriginalTextureImportError::None;
    FString ErrorDetail, Class, Package, AssetPath, State;
    UTexture* Asset = nullptr;
    bool Created = false, Replaced = false, Saved = false, FilledStandIn = false;
    bool HasBuildReport = false, HasAsset = false, HasMutationReport = false;
    TOptional<double> LargestBuiltDifference;
    TOptional<FP4OriginalTextureImportBaseline> Baseline;
    bool IsValid() const { return Error == EP4OriginalTextureImportError::None; }
};

// Editor-thread import. Native callers consume this result directly; the legacy
// Blueprint/JSON entry point is encoded by a separate adapter.
KADAN_API FP4OriginalTextureImportResult P4ImportOriginalTextureNative(
    const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk);
// Legacy protocol and disk-report adapter. Import execution does not call it.
KADAN_API FString P4OriginalTextureImportResultJson(const FP4OriginalTextureImportResult& Result);

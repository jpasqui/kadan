#pragma once
#include "CoreMinimal.h"
class UStaticMesh;

enum class EP4OriginalMeshImportError : uint8
{
    None, EditorThreadRequired, DecodeFailed, InvalidGamePath, SocketsUnsupported,
    PropertiesUndecoded, PropertyUnrepresentable, CollisionUnrepresentable, MissingMaterial,
    PlaceholderClassMismatch, EditedPlaceholder, SaveRequired, BackupUnreadable,
    OccupiedDestination, RecordValidationRequired, EditedOriginal, EarlierImportReplacementUnsupported,
    BuildFailed, EngineBuildDiffers, PropertyPreparationFailed, CollisionProfileMismatch,
    StateUnreadable, FillDiffers, RenameFailed, RecordFailed, SaveFailed
};

struct FP4OriginalMeshImportResult
{
    EP4OriginalMeshImportError Error = EP4OriginalMeshImportError::None;
    FString ErrorDetail, Package, AssetPath, SourceDigest, ImportedState, State, Equality;
    UStaticMesh* Asset = nullptr;
    bool Created = false, Saved = false, FilledStandIn = false;
    bool MaterialsChecked = false, BuildComplete = false, BaselineRead = false;
    int32 UnshippedMaterialSlots = 0, Lods = 0;
    double LargestUVGap = 0;
    bool IsValid() const { return Error == EP4OriginalMeshImportError::None; }
};

FP4OriginalMeshImportResult P4ImportOriginalMeshNative(const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk);
FString P4OriginalMeshImportJson(const FP4OriginalMeshImportResult& Result);

#pragma once
#include "CoreMinimal.h"

class FJsonObject;

enum class EP4SkinnedFillKind : uint8
{
    None,
    Skeleton,
    SkeletalMesh,
    AnimSequence,
    AnimMontage
};

// Counts are produced directly by the native importer. Kind=None means no report was
// produced, including a refusal before the native data could be applied.
struct FP4SkinnedFillReport
{
    EP4SkinnedFillKind Kind = EP4SkinnedFillKind::None;
    int64 Bones = 0;
    int64 CurveNames = 0;
    int64 RetargetSources = 0;
    int64 LevelsOfDetail = 0;
    int64 Vertices = 0;
    int64 Triangles = 0;
    int64 MaterialSlots = 0;
    int64 MissingGameMaterials = 0;
    int64 Tracks = 0;
    int64 Frames = 0;
    int64 RawKeys = 0;
    int64 Curves = 0;
    int64 RawBytes = 0;
    int64 GameRawBytes = 0;
    bool OriginalCompressedStreamRetained = false;
    FString OriginalCacheSuffix;
};

// Measured may be true even when comparison refuses. ExactSourceComparison is present
// only when the current native source key still matches the retained original key.
struct FP4SkinnedCompareReport
{
    bool Measured = false;
    int64 PosesCompared = 0;
    float LargestTranslationDifference = 0;
    float LargestRotationDifferenceDegrees = 0;
    float LargestScaleDifference = 0;
    float LargestCurveDifference = 0;
    bool CompressedKeysEqual = false;
    bool CompressedCurvesEqual = false;
    bool BoneCodecEqual = false;
    bool RawSizeEqual = false;
    FString GameBoneCodec;
    FString MadeBoneCodec;
    int64 GameCompressedBytes = 0;
    int64 MadeCompressedBytes = 0;
    bool ExactSourceComparison = false;
};

// Compatibility output only; native importing and comparison never parse these fields.
KADAN_API void P4SkinnedFillReportFields(const FP4SkinnedFillReport& Report, FJsonObject& Out);
KADAN_API void P4SkinnedCompareReportFields(const FP4SkinnedCompareReport& Report, FJsonObject& Out);

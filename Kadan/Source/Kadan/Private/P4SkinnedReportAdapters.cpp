#include "P4SkinnedContent.h"
#include "Dom/JsonObject.h"

void P4SkinnedFillReportFields(const FP4SkinnedFillReport& Report, FJsonObject& Out)
{
    if (Report.Kind == EP4SkinnedFillKind::Skeleton)
    {
        Out.SetNumberField(TEXT("bones"), double(Report.Bones));
        Out.SetNumberField(TEXT("curve_names"), double(Report.CurveNames));
        Out.SetNumberField(TEXT("retarget_sources"), double(Report.RetargetSources));
    }
    else if (Report.Kind == EP4SkinnedFillKind::SkeletalMesh)
    {
        Out.SetNumberField(TEXT("levels_of_detail"), double(Report.LevelsOfDetail));
        Out.SetNumberField(TEXT("vertices"), double(Report.Vertices));
        Out.SetNumberField(TEXT("triangles"), double(Report.Triangles));
        Out.SetNumberField(TEXT("material_slots"), double(Report.MaterialSlots));
        Out.SetNumberField(TEXT("bones"), double(Report.Bones));
        Out.SetNumberField(TEXT("slots_whose_material_the_game_does_not_ship"), double(Report.MissingGameMaterials));
    }
    else if (Report.Kind == EP4SkinnedFillKind::AnimMontage)
    {
        Out.SetNumberField(TEXT("slot_tracks"), double(Report.Tracks));
        Out.SetNumberField(TEXT("notifies"), double(Report.RawKeys));
    }
    else if (Report.Kind == EP4SkinnedFillKind::AnimSequence)
    {
        Out.SetNumberField(TEXT("tracks"), double(Report.Tracks));
        Out.SetNumberField(TEXT("frames"), double(Report.Frames));
        Out.SetNumberField(TEXT("raw_keys"), double(Report.RawKeys));
        Out.SetNumberField(TEXT("curves"), double(Report.Curves));
        Out.SetNumberField(TEXT("raw_bytes"), double(Report.RawBytes));
        Out.SetNumberField(TEXT("raw_bytes_of_the_game"), double(Report.GameRawBytes));
        Out.SetBoolField(TEXT("original_compressed_stream_retained"), Report.OriginalCompressedStreamRetained);
        Out.SetStringField(TEXT("original_cache_suffix"), Report.OriginalCacheSuffix);
    }
}

void P4SkinnedCompareReportFields(const FP4SkinnedCompareReport& Report, FJsonObject& Out)
{
    if (!Report.Measured) return;
    Out.SetNumberField(TEXT("poses_compared"), double(Report.PosesCompared));
    Out.SetNumberField(TEXT("largest_translation_difference"), Report.LargestTranslationDifference);
    Out.SetNumberField(TEXT("largest_rotation_difference_degrees"), Report.LargestRotationDifferenceDegrees);
    Out.SetNumberField(TEXT("largest_scale_difference"), Report.LargestScaleDifference);
    Out.SetNumberField(TEXT("largest_curve_difference"), Report.LargestCurveDifference);
    Out.SetBoolField(TEXT("compressed_keys_equal"), Report.CompressedKeysEqual);
    Out.SetBoolField(TEXT("compressed_curves_equal"), Report.CompressedCurvesEqual);
    Out.SetBoolField(TEXT("bone_codec_equal"), Report.BoneCodecEqual);
    Out.SetBoolField(TEXT("raw_size_equal"), Report.RawSizeEqual);
    Out.SetStringField(TEXT("bone_codec_of_the_game"), Report.GameBoneCodec);
    Out.SetStringField(TEXT("bone_codec_made"), Report.MadeBoneCodec);
    Out.SetNumberField(TEXT("compressed_bytes_of_the_game"), double(Report.GameCompressedBytes));
    Out.SetNumberField(TEXT("compressed_bytes_made"), double(Report.MadeCompressedBytes));
    if (Report.ExactSourceComparison)
        Out.SetStringField(TEXT("comparison"), TEXT("original-compressed-data-exact"));
}

bool P4FillSkinnedNative(UObject* Object, const P4MapKit::FPackageInventory& Package, const std::vector<std::uint8_t>& Body, std::size_t Start, std::size_t End,
    const TFunction<FString(int32)>& Reference, FJsonObject& Report, FString& Error)
{
    FP4SkinnedFillReport Native;
    const bool Valid = P4FillSkinnedNative(Object, Package, Body, Start, End, Reference, Native, Error);
    P4SkinnedFillReportFields(Native, Report);
    return Valid;
}

bool P4CompareSkinnedCooked(UObject* Object, const P4MapKit::FPackageInventory& Game, const std::vector<std::uint8_t>& GameBody, std::size_t GameStart, std::size_t GameEnd,
    const P4MapKit::FPackageInventory& Mine, const std::vector<std::uint8_t>& MineBody, std::size_t MineStart, std::size_t MineEnd, FJsonObject& Report, FString& Error)
{
    FP4SkinnedCompareReport Native;
    const bool Valid = P4CompareSkinnedCooked(Object, Game, GameBody, GameStart, GameEnd, Mine, MineBody, MineStart, MineEnd, Native, Error);
    P4SkinnedCompareReportFields(Native, Report);
    return Valid;
}

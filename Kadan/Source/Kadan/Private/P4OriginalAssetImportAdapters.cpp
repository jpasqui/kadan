#include "P4OriginalAssetImport.h"
#include "P4Common.h"
#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"

namespace
{
TArray<TSharedPtr<FJsonValue>> P4OriginalAssetWords(const TArray<FString>& Words)
{
    TArray<TSharedPtr<FJsonValue>> Result;
    for (const FString& Word : Words)
        Result.Add(MakeShared<FJsonValueString>(Word));
    return Result;
}
} //namespace

FString P4OriginalAssetImportJson(const FP4OriginalAssetImportResult& Result)
{
    auto Json = MakeShared<FJsonObject>();
    Json->SetBoolField(TEXT("created"), Result.Created);
    Json->SetBoolField(TEXT("saved"), Result.Saved);
    if (Result.ObjectsKnown)
    {
        Json->SetStringField(TEXT("package"), Result.Package);
        Json->SetNumberField(TEXT("objects"), Result.Objects);
    }
    if (Result.AssetIdentityKnown)
    {
        Json->SetStringField(TEXT("asset"), Result.AssetPath);
        Json->SetStringField(TEXT("class"), Result.Class);
    }
    if (Result.RowsKnown)
        Json->SetNumberField(TEXT("rows"), Result.Rows);
    if (Result.ImportedValuesKnown)
    {
        Json->SetNumberField(TEXT("references_to_what_the_game_does_not_ship"), Result.UnshippedReferences);
        Json->SetArrayField(TEXT("values_this_engine_cannot_hold"), P4OriginalAssetWords(Result.ValuesNotHeld));
    }
    if (Result.DifferencesKnown)
    {
        Json->SetNumberField(TEXT("differences"), Result.Differences);
        Json->SetArrayField(TEXT("only_the_game_says"), P4OriginalAssetWords(Result.OnlyGameSays));
        Json->SetArrayField(TEXT("only_the_asset_made_says"), P4OriginalAssetWords(Result.OnlyAssetSays));
    }
    if (Result.ComparedKnown)
        Json->SetNumberField(TEXT("compared"), Result.Compared);
    if (Result.EngineMadeKnown)
    {
        Json->SetArrayField(TEXT("the_game_s_cook_made"), P4OriginalAssetWords(Result.GameEngineMade));
        Json->SetArrayField(TEXT("this_cook_made"), P4OriginalAssetWords(Result.EngineMade));
    }
    if (Result.HasNativeData)
    {
        auto Report = MakeShared<FJsonObject>();
        P4SkinnedFillReportFields(Result.NativeData, *Report);
        Json->SetObjectField(TEXT("native_data"), Report);
    }
    if (Result.HasMeasured)
    {
        auto Report = MakeShared<FJsonObject>();
        P4SkinnedCompareReportFields(Result.Measured, *Report);
        Json->SetObjectField(TEXT("measured"), Report);
    }
    if (Result.FilledStandInKnown)
        Json->SetBoolField(TEXT("filled_standin"), Result.FilledStandIn);
    if (Result.BaselineKnown)
    {
        Json->SetStringField(TEXT("source_sha256"), Result.SourceDigest);
        Json->SetStringField(TEXT("imported_state"), Result.ImportedState);
        Json->SetStringField(TEXT("state"), Result.State);
        Json->SetBoolField(TEXT("reference_only"), Result.ReferenceOnly);
        Json->SetNumberField(TEXT("native_bytes_not_held"), Result.NativeBytesNotHeld);
        Json->SetNumberField(TEXT("values_not_held"), Result.ValuesNotHeldCount);
    }
    Json->SetBoolField(TEXT("valid"), Result.IsValid());
    Json->SetStringField(TEXT("error"), Result.ErrorDetail);
    FString Text = P4CondensedJson(Json);
    return Text;
}

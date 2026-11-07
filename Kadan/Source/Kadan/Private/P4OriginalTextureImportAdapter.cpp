#include "P4TextureContent.h"
#include "P4Common.h"
#include "P4OriginalTextureImport.h"
#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"

FString P4OriginalTextureImportResultJson(const FP4OriginalTextureImportResult& Result)
{
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetBoolField(TEXT("valid"), Result.IsValid());
    Json->SetStringField(TEXT("error"), Result.ErrorDetail);
    Json->SetBoolField(TEXT("created"), Result.Created);
    Json->SetBoolField(TEXT("replaced"), Result.Replaced);
    Json->SetBoolField(TEXT("saved"), Result.Saved);
    if (Result.HasBuildReport)
    {
        Json->SetStringField(TEXT("class"), Result.Class);
        Json->SetStringField(TEXT("package"), Result.Package);
    }
    if (Result.LargestBuiltDifference.IsSet())
        Json->SetNumberField(TEXT("largest_built_difference"), Result.LargestBuiltDifference.GetValue());
    if (Result.Baseline.IsSet())
    {
        const auto& Baseline = Result.Baseline.GetValue();
        Json->SetStringField(TEXT("source_sha256"), Baseline.SourceDigest);
        Json->SetStringField(TEXT("imported_state"), Baseline.State);
        Json->SetStringField(TEXT("game_format"), Baseline.GameFormat);
        Json->SetStringField(TEXT("game_texture_group"), Baseline.GameGroup);
        Json->SetNumberField(TEXT("width"), Baseline.Width);
        Json->SetNumberField(TEXT("height"), Baseline.Height);
        Json->SetNumberField(TEXT("slices"), Baseline.Slices);
        Json->SetNumberField(TEXT("mips"), Baseline.Mips);
        Json->SetNumberField(TEXT("first_mip"), Baseline.FirstMip);
    }
    if (Result.HasAsset)
        Json->SetStringField(TEXT("asset"), Result.AssetPath);
    if (Result.HasMutationReport)
    {
        Json->SetBoolField(TEXT("filled_standin"), Result.FilledStandIn);
        Json->SetStringField(TEXT("state"), Result.State);
    }
    FString Text = P4CondensedJson(Json);
    return Text;
}

FString P4ImportOriginalTexture(const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk)
{
    return P4OriginalTextureImportResultJson(P4ImportOriginalTextureNative(HeaderFile, BodyFile, SaveToDisk));
}

#include "P4LightingBake.h"
#include "P4Common.h"
#include "P4LightingBakeResult.h"
#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"

namespace
{
TArray<TSharedPtr<FJsonValue>> P4BakeResultWords(const TArray<FString>& Values)
{
    TArray<TSharedPtr<FJsonValue>> Result;
    for (const FString& Value : Values)
        Result.Add(MakeShared<FJsonValueString>(Value));
    return Result;
}
} //namespace

FString P4LightingBakeJson(const FP4LightingBakeResult& Result)
{
    auto Json = MakeShared<FJsonObject>();
    if (Result.SetupCompleted)
    {
        Json->SetArrayField(TEXT("placed"), P4BakeResultWords(Result.Placed));
        Json->SetStringField(TEXT("sky_source"), Result.SkySource);
        Json->SetStringField(TEXT("sky_material"), Result.SkyMaterial);
        Json->SetArrayField(TEXT("values_not_set"), P4BakeResultWords(Result.ValuesNotSet));
    }
    if (Result.RemovalAttempted)
    {
        Json->SetArrayField(TEXT("taken_out"), P4BakeResultWords(Result.TakenOut));
        Json->SetArrayField(TEXT("left"), P4BakeResultWords(Result.Left));
    }
    if (Result.LightingDataKnown)
        Json->SetBoolField(TEXT("lighting_data"), Result.LightingData);
    if (Result.BuiltLightingKnown)
    {
        Json->SetBoolField(TEXT("volume_lighting_taken_out"), Result.VolumeLightingTakenOut);
        Json->SetNumberField(TEXT("meshes_with_built_lighting"), Result.MeshesWithBuiltLighting);
    }
    Json->SetBoolField(TEXT("valid"), Result.IsValid());
    Json->SetStringField(TEXT("error"), Result.ErrorDetail);
    FString Text = P4CondensedJson(Json);
    return Text;
}

FString P4LightingBakeSetup(UWorld* World, const FString& Setup)
{
    return P4LightingBakeJson(P4SetupLightingBakeFromJsonNative(World, Setup));
}
FString P4LightingBakeClear(UWorld* World)
{
    return P4LightingBakeJson(P4ClearLightingBakeNative(World));
}

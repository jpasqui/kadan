#include "P4OriginalMeshImport.h"
#include "P4Common.h"
#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"

FString P4OriginalMeshImportJson(const FP4OriginalMeshImportResult& R)
{
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetBoolField(TEXT("valid"), R.IsValid());
    Json->SetStringField(TEXT("error"), R.ErrorDetail);
    Json->SetBoolField(TEXT("created"), R.Created);
    Json->SetBoolField(TEXT("saved"), R.Saved);
    if (!R.Package.IsEmpty())
        Json->SetStringField(TEXT("package"), R.Package);
    if (!R.AssetPath.IsEmpty())
        Json->SetStringField(TEXT("asset"), R.AssetPath);
    if (R.MaterialsChecked)
        Json->SetNumberField(TEXT("slots_whose_material_the_game_does_not_ship"), R.UnshippedMaterialSlots);
    if (R.BuildComplete)
    {
        Json->SetBoolField(TEXT("filled_standin"), R.FilledStandIn);
        Json->SetNumberField(TEXT("lods"), R.Lods);
    }
    if (R.BaselineRead)
    {
        Json->SetStringField(TEXT("source_sha256"), R.SourceDigest);
        Json->SetStringField(TEXT("imported_state"), R.ImportedState);
        Json->SetStringField(TEXT("equal"), R.Equality);
        Json->SetNumberField(TEXT("largest_uv_gap"), R.LargestUVGap);
        Json->SetStringField(TEXT("state"), R.State);
    }
    FString Text = P4CondensedJson(Json);
    return Text;
}

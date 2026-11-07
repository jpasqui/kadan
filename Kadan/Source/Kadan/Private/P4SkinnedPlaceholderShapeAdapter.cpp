#include "P4AuthoringRows.h"
#include "P4Common.h"
#include "P4SkinnedPlaceholderShape.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"

FString UP4AuthoringTools::NativeSkinnedPlaceholderShape(UObject* Asset)
{
    const FP4SkinnedPlaceholderShape Shape = P4InspectSkinnedPlaceholderShape(Asset);
    const TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
    Out->SetNumberField(TEXT("format"), 1);
    Out->SetBoolField(TEXT("observed"), Shape.Observed);
    Out->SetStringField(TEXT("object_path"), Shape.ObjectPath);
    Out->SetStringField(TEXT("class_path"), Shape.ClassPath);
    Out->SetBoolField(TEXT("is_skeleton"), Shape.IsSkeleton);
    Out->SetBoolField(TEXT("is_skeletal_mesh"), Shape.IsSkeletalMesh);
    Out->SetBoolField(TEXT("imported_model_present"), Shape.ImportedModelPresent);
    Out->SetNumberField(TEXT("imported_lod_count"), Shape.ImportedLODCount);
    Out->SetBoolField(TEXT("render_data_present"), Shape.RenderDataPresent);
    Out->SetNumberField(TEXT("render_lod_count"), Shape.RenderLODCount);
    Out->SetBoolField(TEXT("native_rollback_shape_supported"), Shape.NativeRollbackShapeSupported);
    Out->SetStringField(TEXT("scope"), TEXT("loaded-native-shape-only"));
    FString Json = P4CondensedJson(Out);
    return Json;
}

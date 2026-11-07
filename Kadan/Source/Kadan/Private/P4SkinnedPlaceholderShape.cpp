#include "P4SkinnedPlaceholderShape.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"

FP4SkinnedPlaceholderShape P4InspectSkinnedPlaceholderShape(UObject* Object)
{
    FP4SkinnedPlaceholderShape Result;
    if (!IsValid(Object)) return Result;
    Result.ObjectPath = Object->GetPathName();
    Result.ClassPath = Object->GetClass()->GetPathName();
    Result.IsSkeleton = Object->IsA<USkeleton>();
    USkeletalMesh* Mesh = Cast<USkeletalMesh>(Object);
    Result.IsSkeletalMesh = Mesh != nullptr;
    Result.Observed = Result.IsSkeleton || Result.IsSkeletalMesh;
    if (Result.IsSkeleton) Result.NativeRollbackShapeSupported = true;
    if (!Mesh) return Result;
    const FSkeletalMeshModel* Model = Mesh->GetImportedModel();
    const FSkeletalMeshRenderData* Render = Mesh->GetResourceForRendering();
    Result.ImportedModelPresent = Model != nullptr;
    Result.ImportedLODCount = Model ? Model->LODModels.Num() : -1;
    Result.RenderDataPresent = Render != nullptr;
    Result.RenderLODCount = Render ? Render->LODRenderData.Num() : -1;
    Result.NativeRollbackShapeSupported = Model && Model->LODModels.Num() == 0
        && Render && Render->LODRenderData.Num() == 0;
    return Result;
}

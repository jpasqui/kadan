#include "P4StandInFill.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Serialization/ObjectWriter.h"
#include "Misc/AutomationTest.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
namespace
{
struct FStandInSkeletonDoor : USkeleton
{
    static FReferenceSkeleton& Reference(USkeleton* S) { return S->*(&FStandInSkeletonDoor::ReferenceSkeleton); }
    static FGuid& Identity(USkeleton* S) { return S->*(&FStandInSkeletonDoor::Guid); }
    static void Settings(USkeleton* S) { (S->*(&FStandInSkeletonDoor::BoneTree)).AddDefaulted(); }
};
TArray<uint8> StandInBytes(UObject* Object)
{
    TArray<uint8> Bytes;
    FObjectWriter Writer(Object, Bytes, false, false, false);
    return Bytes;
}
} //namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SkinnedStandInRollbackTest, "P4MapKit.Skinned.StandInRollback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4SkinnedStandInRollbackTest::RunTest(const FString&)
{
    USkeleton* Skeleton = NewObject<USkeleton>();
    FStandInSkeletonDoor::Identity(Skeleton) = FGuid::NewGuid();
    FStandInSkeletonDoor::Settings(Skeleton);
    {
        FReferenceSkeletonModifier Modifier(FStandInSkeletonDoor::Reference(Skeleton), Skeleton);
        Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
    }
    const FGuid Identity = Skeleton->GetGuid();
    const TArray<uint8> SkeletonBefore = StandInBytes(Skeleton);
    USkeleton* SkeletonIdentity = Skeleton;
    {
        FP4StandInFill Fill(Skeleton);
        if (!TestTrue(TEXT("skeleton native snapshot ready"), Fill.IsReady()))
            return false;
        FStandInSkeletonDoor::Identity(Skeleton) = FGuid::NewGuid();
        FStandInSkeletonDoor::Settings(Skeleton);
        FReferenceSkeletonModifier Modifier(FStandInSkeletonDoor::Reference(Skeleton), Skeleton);
        Modifier.Add(FMeshBoneInfo(TEXT("failed_child"), TEXT("failed_child"), 0), FTransform(FVector(5, 0, 0)));
        // A refused import leaves the guard uncommitted.
    }
    TestTrue(TEXT("skeleton rollback preserves UObject identity"), Skeleton == SkeletonIdentity);
    TestTrue(TEXT("skeleton rollback restores native guid"), Skeleton->GetGuid() == Identity);
    TestEqual(TEXT("skeleton rollback restores reference bones"), Skeleton->GetReferenceSkeleton().GetRawBoneNum(), 1);
    TestTrue(TEXT("skeleton rollback native serialization exact"), StandInBytes(Skeleton) == SkeletonBefore);

    USkeletalMesh* Mesh = NewObject<USkeletalMesh>();
    USkeletalMesh* MeshIdentity = Mesh;
    // Allocate the empty placeholder renderer deliberately; then mutate it to a real native
    // LOD container so the rollback check cannot pass by skipping absent derived state.
    if (!Mesh->GetResourceForRendering())
        Mesh->AllocateResourceForRendering();
    const bool HadRenderData = Mesh->GetResourceForRendering() != nullptr;
    const int32 RenderLodsBefore = HadRenderData ? Mesh->GetResourceForRendering()->LODRenderData.Num() : -1;
    const TArray<uint8> MeshBefore = StandInBytes(Mesh);
    UMetaData* Metadata = Mesh->GetOutermost()->GetMetaData();
    Metadata->SetValue(Mesh, TEXT("P4BlueprintStandIn"), TEXT("1"));
    {
        FP4StandInFill Fill(Mesh);
        if (!TestTrue(TEXT("mesh native snapshot ready"), Fill.IsReady()))
            return false;
        Mesh->Skeleton = Skeleton;
        Mesh->Materials.Add(FSkeletalMaterial());
        Mesh->SetImportedBounds(FBoxSphereBounds(FVector(11, 12, 13), FVector(4, 5, 6), 9));
        {
            FReferenceSkeletonModifier Modifier(Mesh->RefSkeleton, Skeleton);
            Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
        }
        FSkeletalMeshModel* Model = Mesh->GetImportedModel();
        if (!TestNotNull(TEXT("mesh source model available"), Model))
            return false;
        FSkeletalMeshLODModel* Lod = new FSkeletalMeshLODModel();
        Lod->NumVertices = 3;
        Model->LODModels.Add(Lod);
        Mesh->GetResourceForRendering()->LODRenderData.Add(new FSkeletalMeshLODRenderData());
        Mesh->InvalidateDeriveDataCacheGUID();
        Metadata->RemoveValue(Mesh, TEXT("P4BlueprintStandIn"));
    }
    TestTrue(TEXT("mesh rollback preserves UObject identity"), Mesh == MeshIdentity);
    TestNull(TEXT("mesh rollback restores original skeleton reference"), Mesh->Skeleton);
    TestEqual(TEXT("mesh rollback restores material slots"), Mesh->Materials.Num(), 0);
    TestEqual(TEXT("mesh rollback restores source lods"), Mesh->GetImportedModel()->LODModels.Num(), 0);
    TestTrue(TEXT("mesh rollback restores renderer state presence"),
        (Mesh->GetResourceForRendering() != nullptr) == HadRenderData);
    TestEqual(TEXT("mesh rollback restores renderer lods"),
        Mesh->GetResourceForRendering() ? Mesh->GetResourceForRendering()->LODRenderData.Num() : -1, RenderLodsBefore);
    TestTrue(TEXT("mesh rollback restores stand-in metadata"),
        FString(Metadata->GetValue(Mesh, TEXT("P4BlueprintStandIn"))) == TEXT("1"));
    TestTrue(TEXT("mesh rollback native serialization exact"), StandInBytes(Mesh) == MeshBefore);
    Metadata->RemoveValue(Mesh, TEXT("P4BlueprintStandIn"));
    Mesh->MarkPendingKill();
    Skeleton->MarkPendingKill();
    return true;
}
#endif

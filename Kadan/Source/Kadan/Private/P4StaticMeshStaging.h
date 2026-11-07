#pragma once
#include "P4NumericContract.h"
#include "CoreMinimal.h"
#include "MeshDescription.h"
#include "P4MapKitCore/static_mesh_render.h"

// Root binds these labels to exact original tagged StaticMaterials entries.
// No imported label is inferred from the surviving native slot name.
struct FP4ExactMeshMaterialSlot
{
    FString NativeSlot, ImportedSlot;
    int32 ObjectIndex = 0;
};
struct FP4PrivateMeshLODStage
{
    TUniquePtr<FMeshDescription> Description;
    TArray<FVertexID> Vertices;
    TArray<FVertexInstanceID> Corners;
    // INDEX_NONE for used vertices; otherwise an unconnected attribute instance.
    TArray<FVertexInstanceID> UnusedInstances;
    TArray<FPolygonGroupID> Sections;
    TArray<FPolygonID> Polygons;
};
struct FP4PrivateMeshStage
{
    TArray<FP4PrivateMeshLODStage> LODs;
};
struct FP4MeshStageReport
{
    uint64 Vertices = 0, Corners = 0, UnusedVertices = 0, Sections = 0;
    bool GeometryAttributesVerified = false;
    bool OriginalDefaultsQualified = false, DependenciesQualified = false, EditableAssetQualified = false;
    TArray<FString> UnrepresentedFields;
};
struct FP4MeshStageLimits
{
    uint64 Vertices = 1000000, Corners = 6000000, EstimatedBytes = 512ull * 1024ull * 1024ull;
};

// All-or-nothing private in-memory descriptions. No UStaticMesh, package, build,
// AssetRegistry, Content publication, save/cook, defaults or dependency admission.
KADAN_API bool P4StagePrivateMeshDescriptions(const P4MapKit::FStaticMeshRenderPayload& Source,
    const TArray<FP4ExactMeshMaterialSlot>& Slots, FP4PrivateMeshStage& Out,
    FP4MeshStageReport& Report, FString& Error, const FP4MeshStageLimits& Limits = {},
    const TFunction<bool()>& Cancel = {});
KADAN_API bool P4VerifyPrivateMeshDescriptions(const P4MapKit::FStaticMeshRenderPayload& Source,
    const TArray<FP4ExactMeshMaterialSlot>& Slots, const FP4PrivateMeshStage& Stage,
    FP4MeshStageReport& Report, FString& Error, const FP4MeshStageLimits& Limits = {},
    const TFunction<bool()>& Cancel = {});

#if WITH_DEV_AUTOMATION_TESTS && P4_MAPKIT_TESTS
class FAutomationTestBase;
bool P4TestStaticMeshStaging(FAutomationTestBase& Test);
#endif

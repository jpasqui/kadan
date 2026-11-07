#if P4_MAPKIT_TESTS
#include "P4CreatorSurfaceAdvice.h"
#include "P4CreatorSource.h"
#include "P4StaticMeshContent.h"
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "AssetRegistryModule.h"
#include "IAssetTools.h"
#include "Engine/StaticMesh.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace P4ExportDriverTest
{
UObject* ImportOwn(FAutomationTestBase&, const FString&, const FString&, bool);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CreatorSurfaceUVAdviceTest, "P4MapKit.Quality.CreatorSurfaceUVAdvice",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CreatorSurfaceUVAdviceTest::RunTest(const FString&)
{
    TStrongObjectPtr<UStaticMesh> Mesh(Cast<UStaticMesh>(
        P4ExportDriverTest::ImportOwn(*this, TEXT("/Game/Mods/CreatorSurfaceUVTest"), TEXT("OwnSurface"), true)));
    if (!TestNotNull(TEXT("An actual creator file is imported"), Mesh.Get()))
        return false;
    ON_SCOPE_EXIT
    {
        FAssetRegistryModule::AssetDeleted(Mesh.Get());
        Mesh->ClearFlags(RF_Public | RF_Standalone);
        Mesh->MarkPendingKill();
        Mesh->GetOutermost()->SetDirtyFlag(false);
    };
    TStrongObjectPtr<UMaterial> Material(NewObject<UMaterial>());
    auto* UV = NewObject<UMaterialExpressionTextureCoordinate>(Material.Get());
    Material->Expressions.Add(UV);
    Material->BaseColor.Expression = UV;
    Mesh->SetMaterial(0, Material.Get());
    const auto Has = [&](const TCHAR* Code)
    {
        return P4InspectCreatorSurface(Mesh.Get())
            .ContainsByPredicate([&](const FP4CreatorSurfaceAdvice& Row) { return Row.Code == Code; });
    };
    UV->CoordinateIndex = 3;
    TestTrue(TEXT("A material's active missing channel is identified"), Has(TEXT("material-missing-uv")));
    UV->CoordinateIndex = 0;
    const FString Before = P4StaticMeshState(Mesh.Get());
    TestFalse(TEXT("The valid imported channel is accepted"),
        Has(TEXT("material-missing-uv")) || Has(TEXT("material-stretched-uv")));
    TestEqual(TEXT("Inspection leaves the creator surface unchanged"), P4StaticMeshState(Mesh.Get()), Before);
    FMeshDescription* Description = Mesh->GetMeshDescription(0);
    if (!TestNotNull(TEXT("The imported source geometry is retained"), Description))
        return false;
    FStaticMeshAttributes Attributes(*Description);
    for (const FVertexInstanceID Corner : Description->VertexInstances().GetElementIDs())
        Attributes.GetVertexInstanceUVs().Set(Corner, 0, FVector2D::ZeroVector);
    TestTrue(TEXT("A collapsed channel that the material reads is identified"), Has(TEXT("material-stretched-uv")));
    for (const FVertexInstanceID Corner : Description->VertexInstances().GetElementIDs())
        Attributes.GetVertexInstanceNormals()[Corner] = FVector::ZeroVector;
    TestTrue(TEXT("Invalid vertex frames receive actionable advice"), Has(TEXT("mesh-normal-tangent-frames")));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MeshCollisionExplanationTest, "P4MapKit.Kadan.MeshOwnTriangleCollisionExplanation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MeshCollisionExplanationTest::RunTest(const FString&)
{
    TStrongObjectPtr<UStaticMesh> Mesh(NewObject<UStaticMesh>());
    Mesh->CreateBodySetup();
    Mesh->BodySetup->CollisionTraceFlag = CTF_UseComplexAsSimple;
    TestNull(TEXT("Complex as simple does not need a separate mesh"), Mesh->ComplexCollisionMesh);
    const FString Help = P4MeshCollisionExplanation(Mesh.Get());
    TestTrue(TEXT("The selected-assets help explains the actual collision mode"),
        Help.Contains(TEXT("own triangles")) && Help.Contains(TEXT("can stay empty")));
    Mesh->BodySetup->CollisionTraceFlag = CTF_UseSimpleAndComplex;
    TestTrue(TEXT("A different collision mode gets no own-triangle claim"),
        P4MeshCollisionExplanation(Mesh.Get()).IsEmpty());
    return !HasAnyErrors();
}

// The five FBX controls are authored panels. Their geometry is not copied from
// game terrain. Run against an actual imported cave material and its switches.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CaveWallCreatorAdviceTest, "P4MapKit.Quality.CaveWallCreatorInputs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CaveWallCreatorAdviceTest::RunTest(const FString&)
{
    const FString Source = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_CREATOR_REPORT_PANEL_SOURCE"));
    if (Source.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. No creator wall controls were supplied."));
        return true;
    }
    const FString Parent = TEXT(
        "/Game/Carrot4/Terrain/Cave/Type/Soil/Common/Materials/MI_stg_Soil_Cmn_SoilWallTile03_Blend.MI_stg_Soil_Cmn_SoilWallTile03_Blend");
    UMaterialInterface* CaveWall = LoadObject<UMaterialInterface>(nullptr, *Parent);
    if (!TestNotNull(TEXT("The actual game cave wall material is imported"), CaveWall))
        return false;
    TArray<UStaticMesh*> Meshes;
    ON_SCOPE_EXIT
    {
        for (UStaticMesh* Mesh : Meshes)
        {
            FAssetRegistryModule::AssetDeleted(Mesh);
            Mesh->ClearFlags(RF_Public | RF_Standalone);
            Mesh->MarkPendingKill();
            Mesh->GetOutermost()->SetDirtyFlag(false);
        }
    };
    const auto Has = [](const TArray<FP4CreatorSurfaceAdvice>& Rows, const TCHAR* Code)
    { return Rows.ContainsByPredicate([&](const FP4CreatorSurfaceAdvice& Row) { return Row.Code == Code; }); };
    TStrongObjectPtr<UMaterial> UVMaterial(NewObject<UMaterial>());
    auto* UV = NewObject<UMaterialExpressionTextureCoordinate>(UVMaterial.Get());
    UVMaterial->Expressions.Add(UV);
    UVMaterial->BaseColor.Expression = UV;
    for (const TCHAR* Name :
        {TEXT("Good"), TEXT("BlackColors"), TEXT("NoColors"), TEXT("Stretched"), TEXT("BadNormals")})
    {
        auto* Options = NewObject<UFbxImportUI>();
        Options->MeshTypeToImport = FBXIT_StaticMesh;
        Options->bAutomatedImportShouldDetectType = false;
        Options->bImportMaterials = false;
        Options->bImportTextures = false;
        Options->StaticMeshImportData->bAutoGenerateCollision = false;
        Options->StaticMeshImportData->bGenerateLightmapUVs = false;
        Options->StaticMeshImportData->VertexColorImportOption = EVertexColorImportOption::Replace;
        Options->StaticMeshImportData->NormalImportMethod = FBXNIM_ImportNormalsAndTangents;
        auto* Task = NewObject<UAssetImportTask>();
        Task->Filename = Source / (FString(TEXT("OwnWall")) + Name + TEXT(".fbx"));
        Task->DestinationPath = TEXT("/Game/Mods/CreatorWallInputTest");
        Task->DestinationName = FString(TEXT("OwnWall")) + Name;
        Task->bAutomated = true;
        Task->bSave = false;
        Task->Options = Options;
        FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get().ImportAssetTasks({Task});
        UStaticMesh* Mesh = Task->ImportedObjectPaths.Num()
            ? LoadObject<UStaticMesh>(nullptr, *Task->ImportedObjectPaths[0])
            : nullptr;
        if (!TestNotNull(FString(TEXT("The own FBX wall is imported. ")) + Name, Mesh))
            return false;
        Meshes.Add(Mesh);
        TestTrue(TEXT("The actual file import has creator source"), P4HasCreatorSource(Mesh));
        Mesh->SetMaterial(0, CaveWall);
        const auto Advice = P4InspectCreatorSurface(Mesh);
        TestTrue(TEXT("The selected-assets help identifies the cave wall mask contract"),
            Has(Advice, TEXT("cave-wall-inputs")));
        const bool PaintedWrong = FString(Name) == TEXT("BlackColors");
        TestEqual(TEXT("Only the painted dark-alpha control gets the black-patch warning"),
            Has(Advice, TEXT("cave-wall-dark-alpha")), PaintedWrong);
        TestEqual(TEXT("Only the low-red control selects the ground projection"),
            Has(Advice, TEXT("cave-wall-ground-projection")), PaintedWrong);
        TestFalse(TEXT("The world-projected wall does not request an unused texture UV"),
            Has(Advice, TEXT("material-stretched-uv")) || Has(Advice, TEXT("material-missing-uv")));
        TestEqual(TEXT("The opposing-normal control is identified"), Has(Advice, TEXT("mesh-normal-tangent-frames")),
            FString(Name) == TEXT("BadNormals"));
        if (FString(Name) == TEXT("Stretched"))
        {
            Mesh->SetMaterial(0, UVMaterial.Get());
            UV->CoordinateIndex = 0;
            TestTrue(TEXT("The same collapsed UV matters when the material actually reads it"),
                Has(P4InspectCreatorSurface(Mesh), TEXT("material-stretched-uv")));
            UV->CoordinateIndex = 3;
            TestTrue(TEXT("An active missing texture channel is explained"),
                Has(P4InspectCreatorSurface(Mesh), TEXT("material-missing-uv")));
        }
    }
    UStaticMesh* Original = LoadObject<UStaticMesh>(nullptr,
        TEXT(
            "/Game/Carrot4/Terrain/Cave/Type/Soil/Meshes/CMA_T02_RM_5x4_002/COL_CMA_T02_RM_5x4_002_W.COL_CMA_T02_RM_5x4_002_W"));
    if (TestNotNull(TEXT("The actual room collision is imported"), Original))
    {
        const FString Before = P4StaticMeshState(Original);
        TestEqual(TEXT("Original meshes receive no creator repair advice"), P4InspectCreatorSurface(Original).Num(), 0);
        TestTrue(TEXT("The real wall collider explains its own triangle collision"),
            P4MeshCollisionExplanation(Original).Contains(TEXT("own triangles")));
        TestEqual(TEXT("Inspection preserves the actual game mesh"), P4StaticMeshState(Original), Before);
    }
    return !HasAnyErrors();
}
#endif

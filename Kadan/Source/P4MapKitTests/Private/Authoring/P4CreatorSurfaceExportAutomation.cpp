#if P4_MAPKIT_TESTS
#include "P4AuthoringRows.h"
#include "P4ExportDriver.h"
#include "P4GroundTypes.h"
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace P4ExportDriverTest
{
bool Request(FAutomationTestBase&, const FString&, const FString&, FP4ExportModRequest&, FP4ModNames&);
}

// An opt-in normal export of the reported material controls and a raised
// walking platform. The good panel's authored obstruction is intentional.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CreatorSurfaceExportFixture, "P4MapKit.Quality.CreatorSurfaceExportFixture",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CreatorSurfaceExportFixture::RunTest(const FString&)
{
    const FString Map = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_CREATOR_REPORT_MAP"));
    const FString Source = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_CREATOR_REPORT_PANEL_SOURCE"));
    const FString Game = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_CREATOR_REPORT_GAME"));
    if (Map.IsEmpty() || Source.IsEmpty() || Game.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. No creator surface export fixture was supplied."));
        return true;
    }
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Map);
    if (!TestNotNull(TEXT("The retained fresh cave opens"), World))
        return false;
    UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr,
        TEXT(
            "/Game/Carrot4/Terrain/Cave/Type/Soil/Common/Materials/MI_stg_Soil_Cmn_SoilWallTile03_Blend.MI_stg_Soil_Cmn_SoilWallTile03_Blend"));
    if (!TestNotNull(TEXT("The actual cave wall material is present"), Material))
        return false;
    // Soil is the first TerrainTag in the original Cave033 room's
    // COL_CMA_T02_RM_5x4_002_G_GEN_VARIABLE, read from its cooked Blueprint.
    // This fixture can reuse editor dependencies whose older source records
    // predate native terrain-tag reconstruction. Do not modify those originals.
    FP4GroundType GroundType;
    GroundType.Name = TEXT("Soil");
    GroundType.Tag = 0x80400022u;
    FP4ExportModRequest Request;
    FP4ModNames Names;
    if (!P4ExportDriverTest::Request(*this, TEXT("native.creator.surface"), TEXT("Creator surface controls"), Request, Names))
        return false;
    TArray<AActor*> Previous;
    for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
    {
        UStaticMesh* Mesh = It->GetStaticMeshComponent()->GetStaticMesh();
        if (It->GetActorLabel() == TEXT("CreatorReportVerifyGround") && Mesh &&
            Mesh->GetOutermost()->GetName().StartsWith(TEXT("/Game/Mods/CreatorReportVerify/")))
            Previous.Add(*It);
        else if (It->GetName().StartsWith(TEXT("CreatorReportWall")) && Mesh &&
            (Mesh->GetOutermost()->GetName().StartsWith(TEXT("/Game/Mods/CreatorSurfaceControls/")) ||
                Mesh->GetOutermost()->GetName().StartsWith(Names.PackageRoot + TEXT("/"))))
            Previous.Add(*It);
    }
    for (AActor* Actor : Previous)
        World->DestroyActor(Actor);
    int32 Index = 0;
    FString Error;
    for (const TCHAR* Name :
        {TEXT("Good"), TEXT("BlackColors"), TEXT("NoColors"), TEXT("Stretched"), TEXT("BadNormals"), TEXT("Ground")})
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
        Task->DestinationPath = Names.PackageRoot + TEXT("/SurfaceControls");
        Task->DestinationName = FString(TEXT("OwnWall")) + Name;
        Task->bAutomated = true;
        Task->bReplaceExisting = true;
        Task->bSave = false;
        Task->Options = Options;
        FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get().ImportAssetTasks({Task});
        UStaticMesh* Mesh = Task->ImportedObjectPaths.Num()
            ? LoadObject<UStaticMesh>(nullptr, *Task->ImportedObjectPaths[0])
            : nullptr;
        if (!TestNotNull(FString(TEXT("The own wall source imports. ")) + Name, Mesh))
            return false;
        Mesh->SetMaterial(0, Material);
        FActorSpawnParameters Spawn;
        Spawn.Name = MakeUniqueObjectName(
            World->PersistentLevel, AStaticMeshActor::StaticClass(), FName(*(FString(TEXT("CreatorReportWall")) + Name)));
        Spawn.OverrideLevel = World->PersistentLevel;
        AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(
            Index == 5 ? FVector(200.f, -200.f, 50.f) : FVector(100.f + Index * 240.f, 300.f, -20.f),
            FRotator::ZeroRotator, Spawn);
        if (!TestNotNull(TEXT("The creator control is placed in the original map"), Actor))
            return false;
        Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
        Actor->SetActorLabel(FString(TEXT("CreatorReportWall")) + Name);
        if (Index == 0 || Index == 5)
        {
            if (!TestTrue(TEXT("The ordinary ground action supplies real collision to the creator surface"),
                    P4SetCreatorGroundType(Actor, GroundType, Error)))
            {
                AddError(Error);
                return false;
            }
        }
        else
            Actor->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        if (!TestTrue(TEXT("The own control mesh is saved"),
                UEditorLoadingAndSavingUtils::SavePackages({Mesh->GetOutermost()}, true)))
            return false;
        ++Index;
    }
    // A second white-mask panel sits just in front of the original north wall,
    // so one game view compares both surfaces under the same local lighting.
    UStaticMesh* Good = LoadObject<UStaticMesh>(
        nullptr, *(Names.PackageRoot + TEXT("/SurfaceControls/OwnWallGood.OwnWallGood")));
    if (!TestNotNull(TEXT("The white-mask wall control remains available"), Good))
        return false;
    FActorSpawnParameters ReferenceSpawn;
    ReferenceSpawn.Name = MakeUniqueObjectName(
        World->PersistentLevel, AStaticMeshActor::StaticClass(), TEXT("CreatorReportWallReferenceGood"));
    ReferenceSpawn.OverrideLevel = World->PersistentLevel;
    auto* Reference = World->SpawnActor<AStaticMeshActor>(
        FVector(850.f, 1060.f, -20.f), FRotator::ZeroRotator, ReferenceSpawn);
    if (!TestNotNull(TEXT("The creator panel is placed beside the original wall"), Reference))
        return false;
    Reference->GetStaticMeshComponent()->SetStaticMesh(Good);
    Reference->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if (!TestTrue(TEXT("The authored control level is saved"),
            UEditorLoadingAndSavingUtils::SavePackages({World->GetOutermost()}, true)))
        return false;
    Request.MapPackage = Map;
    Request.ExtractedGameDirectory = Game;
    Request.Roots.Add({EP4ExportRootKind::Package, Map});
    // Original reference levels are deliberately not relit. The exporter bakes
    // only the controls and validates their resulting lighting before packing.
    AddExpectedError(TEXT("MyWorldSettings Maps need lighting rebuilt"), EAutomationExpectedErrorFlags::Contains, 0);
    FP4ExportModResult Result;
    const bool Exported = P4ExportMod(Request, Result, Error);
    auto Report = MakeShared<FJsonObject>();
    Report->SetBoolField(TEXT("exported"), Exported);
    Report->SetStringField(TEXT("error"), Error);
    Report->SetStringField(TEXT("directory"), Result.Directory);
    TArray<TSharedPtr<FJsonValue>> Notes, Refused;
    for (const FString& Note : Result.Told)
        Notes.Add(MakeShared<FJsonValueString>(Note));
    for (const FString& Refusal : Result.Refused)
        Refused.Add(MakeShared<FJsonValueString>(Refusal));
    Report->SetArrayField(TEXT("told"), Notes);
    Report->SetArrayField(TEXT("refused"), Refused);
    FString Text;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Text));
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit/CreatorSurfaceDiagnostic");
    IFileManager::Get().MakeDirectory(*Folder, true);
    TestTrue(TEXT("The actual normal export report is retained"),
        FFileHelper::SaveStringToFile(
            Text, *(Folder / TEXT("export.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    if (!TestTrue(TEXT("The complete normal export succeeds"), Exported))
        AddError(Error);
    return !HasAnyErrors();
}
#endif

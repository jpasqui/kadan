#if P4_MAPKIT_TESTS
#include "P4AddedLightingAttachment.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Components/StaticMeshComponent.h"
#include "ComponentRecreateRenderStateContext.h"
#include "LightingBuildOptions.h"
#include "Materials/Material.h"
#include "RenderingThread.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/GarbageCollection.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4AddedLightingAttachmentTest, "P4MapKit.Export.AddedLightingAttachment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4AddedLightingAttachmentTest::RunTest(const FString&)
{
    if (!GEngine || !GEditor || GEditor->IsLightingBuildCurrentlyRunning())
    {
        AddError(TEXT("The lifecycle test needs an idle editor."));
        return false;
    }
    const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UWorld* Map = UWorld::CreateWorld(
        EWorldType::EditorPreview, false, FName(*(TEXT("LightingMap_") + Id)), GetTransientPackage());
    UWorld* Creator = UWorld::CreateWorld(
        EWorldType::Inactive, false, FName(*(TEXT("LightingAdded_") + Id)), GetTransientPackage());
    if (!TestNotNull(TEXT("The owned map exists"), Map) ||
        !TestNotNull(TEXT("The owned creator world exists"), Creator))
        return false;
    GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(Map);
    ON_SCOPE_EXIT
    {
        Creator->DestroyWorld(false);
        Map->DestroyWorld(false);
        GEngine->DestroyWorldContext(Map);
    };
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!TestNotNull(TEXT("The engine cube is available"), Cube))
        return false;
    FActorSpawnParameters How;
    How.Name = TEXT("CreatorMesh");
    AStaticMeshActor* Source = Map->SpawnActor<AStaticMeshActor>(FVector(100, 0, 0), FRotator::ZeroRotator, How);
    AStaticMeshActor* Copy = Creator->SpawnActor<AStaticMeshActor>(FVector(100, 0, 0), FRotator::ZeroRotator, How);
    if (!TestNotNull(TEXT("The source mesh exists"), Source) || !TestNotNull(TEXT("The creator copy exists"), Copy))
        return false;
    UStaticMeshComponent* Part = Source->GetStaticMeshComponent();
    UStaticMeshComponent* Copied = Copy->GetStaticMeshComponent();
    Part->SetStaticMesh(Cube);
    Part->SetMobility(EComponentMobility::Static);
    Copied->SetStaticMesh(Cube);
    Copied->SetMobility(EComponentMobility::Static);
    ULevel* Original = Map->PersistentLevel;
    ULevel* Added = Creator->PersistentLevel;
    UWorld* PreviousOwner = Added->OwningWorld;
    const bool PreviousVisible = Added->bIsVisible;
    const bool PreviousComponentsRegistered = Added->bAreComponentsCurrentlyRegistered;
    const FGuid Guid(41, 42, 43, 44);
    UMapBuildDataRegistry* Registry = NewObject<UMapBuildDataRegistry>(GetTransientPackage());
    {
        FComponentRecreateRenderStateContext RenderState(Part);
        Part->SetLODDataCount(1, 1);
        Part->LODData[0].MapBuildDataId = Guid;
        Registry->AllocateMeshBuildData(Guid, false);
        Registry->SetupLightmapResourceClusters();
        Original->MapBuildData = Registry;
        Original->InitializeRenderingResources();
        FlushRenderingCommands();
    }
    UMapBuildDataRegistry* Existing = NewObject<UMapBuildDataRegistry>(GetTransientPackage());
    Added->MapBuildData = Existing;
    FString Error;
    {
        FP4AddedLightingAttachment Refused;
        TestFalse(TEXT("An earlier creator registry is refused"), Refused.Attach(Map, Creator, {Source}, Error));
        TestTrue(TEXT("Refusal does not suppress the source mesh"), Part->IsRegistered());
        TestFalse(TEXT("Refusal does not attach a level"), Map->GetLevels().Contains(Added));
    }
    Added->MapBuildData = nullptr;
    Added->OwningWorld = Map;
    {
        FP4AddedLightingAttachment Refused;
        TestFalse(TEXT("A creator level already owned by another map is refused"),
            Refused.Attach(Map, Creator, {Source}, Error));
        TestTrue(TEXT("Refusal preserves the other map's ownership and source registration"),
            Added->OwningWorld == Map && Part->IsRegistered());
    }
    Added->OwningWorld = PreviousOwner;
    const auto RefusesCopyDifference = [&](const TCHAR* Why)
    {
        FP4AddedLightingAttachment Refused;
        TestFalse(Why, Refused.Attach(Map, Creator, {Source}, Error));
        TestTrue(TEXT("Copy refusal keeps the actual source registered"), Part->IsRegistered());
        TestTrue(TEXT("Copy refusal keeps the creator ownership/bookkeeping"),
            Added->OwningWorld == PreviousOwner && Added->bIsVisible == PreviousVisible &&
                Added->bAreComponentsCurrentlyRegistered == PreviousComponentsRegistered);
        TestFalse(TEXT("Copy refusal leaves the map level list unchanged"), Map->GetLevels().Contains(Added));
    };
    const float Diffuse = Copied->LightmassSettings.DiffuseBoost;
    Copied->LightmassSettings.DiffuseBoost = Diffuse + 1;
    RefusesCopyDifference(TEXT("A changed lightmass setting is refused before source suppression"));
    Copied->LightmassSettings.DiffuseBoost = Diffuse;
    // The cube may already draw with the engine's default material, so the first engine material it does not use.
    UMaterialInterface* OtherMaterial = nullptr;
    for (UMaterialInterface* Candidate : {static_cast<UMaterialInterface*>(UMaterial::GetDefaultMaterial(MD_Surface)),
             LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")),
             LoadObject<UMaterialInterface>(
                 nullptr, TEXT("/Engine/EngineMaterials/DefaultDeferredDecalMaterial.DefaultDeferredDecalMaterial"))})
        if (Candidate && Candidate != Part->GetMaterial(0))
        {
            OtherMaterial = Candidate;
            break;
        }
    if (!TestTrue(TEXT("The material difference uses an actual distinct engine material"),
            OtherMaterial && OtherMaterial != Part->GetMaterial(0)))
        return false;
    Copied->SetMaterial(0, OtherMaterial);
    RefusesCopyDifference(TEXT("A changed material is refused before source suppression"));
    Copied->SetMaterial(0, Part->GetMaterial(0));
    const bool CopyRegistered = Copied->IsRegistered();
    {
        FP4AddedLightingAttachment Scope;
        if (!TestTrue(TEXT("The actual creator level attaches"), Scope.Attach(Map, Creator, {Source}, Error)))
        {
            AddError(Error);
            return false;
        }
        TestTrue(TEXT("The creator level joins the original scene"),
            Map->GetLevels().Contains(Added) && Added->OwningWorld == Map && Added->bIsVisible);
        TestFalse(TEXT("The editor source is excluded to avoid duplicate geometry"), Part->IsRegistered());
        TestTrue(TEXT("The creator copy is registered for the build"), Copied->IsRegistered());
        FLightingBuildOptions Options;
        TestTrue(TEXT("The actual native options are supplied"), Scope.BuildOptions(Options, Error));
        TestFalse(TEXT("Actor selection is not used as a preservation shortcut"), Options.bOnlyBuildSelected);
        TestTrue(TEXT("The creator level receives lighting"), Options.ShouldBuildLightingForLevel(Added));
        TestFalse(TEXT("The engine excludes the original registry from invalidation"),
            Options.ShouldBuildLightingForLevel(Original));
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        TestTrue(
            TEXT("The attachment keeps both actual actors alive across collection"), IsValid(Source) && IsValid(Copy));
        TestTrue(TEXT("The original registry pointer and GUID data are untouched"),
            Original->MapBuildData == Registry && Registry->GetMeshBuildDataDuringBuild(Guid));
        TestTrue(TEXT("The level detaches without destruction"), Scope.Detach(Error));
    }
    TestFalse(TEXT("The temporary level leaves the map's list"), Map->GetLevels().Contains(Added));
    TestTrue(TEXT("The original current level is restored"), Map->GetCurrentLevel() == Original);
    TestTrue(TEXT("The creator world's prior ownership is restored"), Added->OwningWorld == PreviousOwner);
    TestTrue(TEXT("The creator world's prior level visibility is restored"), Added->bIsVisible == PreviousVisible);
    TestTrue(TEXT("The creator world's component-registration bookkeeping is restored"),
        Added->bAreComponentsCurrentlyRegistered == PreviousComponentsRegistered);
    TestTrue(TEXT("The original source registration is restored"), Part->IsRegistered());
    TestTrue(TEXT("Creator registration returns to its prior state"), Copied->IsRegistered() == CopyRegistered);
    TestTrue(TEXT("Neither actor was marked pending kill"),
        IsValid(Source) && IsValid(Copy) && !Source->IsPendingKillPending() && !Copy->IsPendingKillPending());
    TestTrue(TEXT("The original GUID is unchanged"), Part->LODData[0].MapBuildDataId == Guid);
    {
        FP4AddedLightingAttachment Scope;
        if (!TestTrue(TEXT("A second independent scope can attach"), Scope.Attach(Map, Creator, {Source}, Error)))
            return false;
    }
    TestTrue(TEXT("Ordinary scope destruction restores the source and current level"),
        Part->IsRegistered() && Map->GetCurrentLevel() == Original && !Map->GetLevels().Contains(Added));
    return !HasAnyErrors();
}
#endif

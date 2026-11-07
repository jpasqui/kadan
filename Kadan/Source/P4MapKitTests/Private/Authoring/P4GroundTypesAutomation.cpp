#if P4_MAPKIT_TESTS
#include "P4GroundTypes.h"
#include "P4CreatorSource.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Editor.h"
#include "AssetRegistryModule.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "PhysicsEngine/BodySetup.h"
#include "UObject/StrongObjectPtr.h"
#include "AI/Navigation/NavRelevantInterface.h"
#include "NavMesh/RecastNavMeshGenerator.h"

namespace P4ExportDriverTest
{
UObject* ImportOwn(FAutomationTestBase&, const FString&, const FString&, bool);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CreatorGroundTypeTest, "P4MapKit.Parity.CreatorGroundTags",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CreatorGroundTypeTest::RunTest(const FString&)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!TestNotNull(TEXT("The editor world is available"), World))
        return false;
    TStrongObjectPtr<UStaticMesh> Mesh(Cast<UStaticMesh>(
        P4ExportDriverTest::ImportOwn(*this, TEXT("/Game/Mods/ParityGroundTest"), TEXT("SM_Ground"), true)));
    if (!TestNotNull(TEXT("The creator's surface is imported from an external mesh file"), Mesh.Get()))
        return false;
    ON_SCOPE_EXIT
    {
        FAssetRegistryModule::AssetDeleted(Mesh.Get());
        Mesh->ClearFlags(RF_Public | RF_Standalone);
        Mesh->MarkPendingKill();
        Mesh->GetOutermost()->SetDirtyFlag(false);
        Mesh.Reset();
    };
    Mesh->CreateBodySetup();
    FKBoxElem Box;
    Box.X = Box.Y = Box.Z = 100;
    Mesh->BodySetup->AggGeom.BoxElems.Add(Box);
    Mesh->BodySetup->InvalidatePhysicsData();
    Mesh->BodySetup->CreatePhysicsMeshes();
    AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>();
    if (!TestNotNull(TEXT("A creator mesh actor is placed"), Actor))
        return false;
    ON_SCOPE_EXIT
    {
        World->EditorDestroyActor(Actor, false);
    };
    Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh.Get());
    const int32 SourceSlots = Mesh->StaticMaterials.Num();
    const TArray<FStaticMaterial> SourceMaterials = Mesh->StaticMaterials;
    FP4GroundType Soil;
    Soil.Name = TEXT("Soil");
    Soil.Tag = 0x80400022u;
    FString Error;
    if (!TestTrue(TEXT("The surface gets the game's native terrain tags"), P4SetCreatorGroundType(Actor, Soil, Error)))
    {
        AddError(Error);
        return false;
    }
    TInlineComponentArray<UStaticMeshComponent*> Components(Actor);
    UStaticMeshComponent* Ground = nullptr;
    int32 Tagged = 0;
    for (UStaticMeshComponent* Component : Components)
    {
        TArray<uint32> Tags;
        if (!P4ReadGroundTags(Component, Tags))
            continue;
        Ground = Component;
        ++Tagged;
        TestEqual(TEXT("There is one tag for each material slot"), Tags.Num(), SourceSlots);
        for (uint32 Tag : Tags)
            TestEqual(TEXT("The actual native field holds the chosen game tag"), Tag, Soil.Tag);
    }
    if (!TestNotNull(TEXT("The game's collision component is on the actor"), Ground))
        return false;
    TestEqual(TEXT("Only one native collision component is added"), Tagged, 1);
    TestEqual(TEXT("The component uses the game's class by reference"), Ground->GetClass()->GetPathName(),
        FString(TEXT("/Script/Carrot4.StaticCollisionComponent")));
    TestTrue(TEXT("The visible mesh no longer supplies duplicate collision"),
        Actor->GetStaticMeshComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
    TestTrue(TEXT("The game component supplies collision on the original creator mesh"),
        Ground->GetStaticMesh() == Mesh.Get() && Ground->GetCollisionEnabled() != ECollisionEnabled::NoCollision);
    TestEqual(TEXT("Carrying ground queries see a WorldStatic terrain body"), Ground->GetCollisionObjectType(),
        ECC_WorldStatic);
    TestEqual(TEXT("The native body uses the game's terrain profile"), Ground->GetCollisionProfileName(),
        FName(TEXT("CP_Terrain")));
    FP4GroundType Sand;
    Sand.Name = TEXT("Sand");
    Sand.Tag = 0x83c00022u;
    TestTrue(TEXT("Changing the type reuses the same native component"), P4SetCreatorGroundType(Actor, Sand, Error));
    TArray<uint32> Changed;
    TestTrue(TEXT("The changed native tags read back"), P4ReadGroundTags(Ground, Changed));
    for (uint32 Tag : Changed)
        TestEqual(TEXT("The new choice is stored in the actual native property"), Tag, Sand.Tag);
    Components.Reset();
    Actor->GetComponents(Components);
    TestEqual(TEXT("Changing ground type does not add another collider"), Components.Num(), 2);
    TestTrue(TEXT("No source mesh material slots are rewritten"), Mesh->StaticMaterials == SourceMaterials);
    FP4GroundType Wall;
    Wall.Tag = 0xc0400022u;
    TestFalse(
        TEXT("A wall-only tag cannot be selected as a ground surface"), P4SetCreatorGroundType(Actor, Wall, Error));
    TestTrue(TEXT("The creator source remains the actual imported mesh"), P4HasCreatorSource(Mesh.Get()));
    return !HasAnyErrors();
}

// The Content Browser import can create BodySetup without any simple shapes.
// The ground action must make that actual surface usable, not just add tags.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4CreatorGroundWithoutShapesTest, "P4MapKit.Parity.CreatorGroundWithoutCollision",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4CreatorGroundWithoutShapesTest::RunTest(const FString&)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!TestNotNull(TEXT("The editor world is available"), World))
        return false;
    TStrongObjectPtr<UStaticMesh> Mesh(Cast<UStaticMesh>(
        P4ExportDriverTest::ImportOwn(*this, TEXT("/Game/Mods/GroundWithoutCollisionTest"), TEXT("SM_Ground"), true)));
    if (!TestNotNull(TEXT("An own mesh is imported with automatic collision disabled"), Mesh.Get()))
        return false;
    ON_SCOPE_EXIT
    {
        FAssetRegistryModule::AssetDeleted(Mesh.Get());
        Mesh->ClearFlags(RF_Public | RF_Standalone);
        Mesh->MarkPendingKill();
        Mesh->GetOutermost()->SetDirtyFlag(false);
    };
    Mesh->CreateBodySetup();
    TestEqual(
        TEXT("The imported BodySetup has no simple collision shapes"), Mesh->BodySetup->AggGeom.GetElementCount(), 0);
    TestTrue(
        TEXT("The imported surface contains triangles usable for collision"), Mesh->ContainsPhysicsTriMeshData(false));
    AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(FVector(20000, 20000, 20000), FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("The own mesh is placed away from other geometry"), Actor))
        return false;
    ON_SCOPE_EXIT
    {
        World->EditorDestroyActor(Actor, false);
    };
    Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh.Get());
    FP4GroundType Soil;
    Soil.Name = TEXT("Soil");
    Soil.Tag = 0x80400022u;
    FString Error;
    if (!TestTrue(TEXT("The ground action accepts an own mesh without simple shapes"),
            P4SetCreatorGroundType(Actor, Soil, Error)))
    {
        AddError(Error);
        return false;
    }
    UStaticMeshComponent* Ground = nullptr;
    for (UStaticMeshComponent* Part : TInlineComponentArray<UStaticMeshComponent*>(Actor))
    {
        TArray<uint32> Tags;
        if (P4ReadGroundTags(Part, Tags))
            Ground = Part;
    }
    if (!TestNotNull(TEXT("The action creates a hidden game terrain component"), Ground))
        return false;
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(KadanGroundWithoutShapes), false);
    const FVector At = Actor->GetActorLocation();
    TestTrue(TEXT("A simple gameplay trace now hits the own surface"),
        Ground->LineTraceComponent(Hit, At + FVector(0, 0, 150), At - FVector(0, 0, 150), Query));
    FNavigationRelevantData Geometry(*Ground);
    FRecastNavMeshGenerator::ExportComponentGeometry(Ground, Geometry);
    TestTrue(TEXT("Navigation receives actual collision geometry after the action"), Geometry.CollisionData.Num() > 0);
    TestTrue(TEXT("The generated collision stays hidden in the editor and game"),
        !Ground->IsVisible() && Ground->bHiddenInGame);
    TestTrue(TEXT("A repeated ground action reuses the same collision component"),
        P4SetCreatorGroundType(Actor, Soil, Error));
    TestEqual(TEXT("The visible and hidden components are the only two meshes"),
        TInlineComponentArray<UStaticMeshComponent*>(Actor).Num(), 2);
    return !HasAnyErrors();
}
#endif

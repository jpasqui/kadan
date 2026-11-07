#include "P4ExportNavigation.h"
#include "P4LevelChanges.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MovedNavigationVolume,
    "P4MapKit.Export.MovedNavigationVolume",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FP4MovedNavigationVolume::RunTest(const FString&)
{
    const FString Package = TEXT("/Temp/KadanNavigationClassification/") +
        FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UWorld* World = NewObject<UWorld>(CreatePackage(*Package),
        *FPackageName::GetShortName(Package), RF_Transient);
    World->WorldType = EWorldType::Editor;
    World->InitializeNewWorld(UWorld::InitializationValues().AllowAudioPlayback(false)
        .CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false)
        .ShouldSimulatePhysics(false));
    FActorSpawnParameters Spawn;
    Spawn.Name = TEXT("MovedBounds");
    ANavMeshBoundsVolume* Bounds = World->SpawnActor<ANavMeshBoundsVolume>(
        FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
    if (!TestNotNull(TEXT("The source navigation volume exists"), Bounds))
    {
        World->CleanupWorld();
        return false;
    }
    FP4LevelChangesResult Changes;
    Changes.Package = Package;
    FP4LevelActorMove Move;
    Move.Actor = Bounds->GetName();
    Changes.Moved.Add(Move);
    FString Why;
    TestTrue(TEXT("A moved navigation volume requests a rebuild"),
        P4EditChangesNavigation(Changes, Why));
    TestEqual(TEXT("The moved volume identifies the reason"), Why, Bounds->GetName());
    Changes.Moved.Reset();
    FP4LevelActorChanges Other;
    Other.Actor = Bounds->GetName();
    Changes.OtherChanged.Add(Other);
    TestTrue(TEXT("Another change to a navigation volume requests a rebuild"),
        P4EditChangesNavigation(Changes, Why));
    World->CleanupWorld();
    return !HasAnyErrors();
}
#endif

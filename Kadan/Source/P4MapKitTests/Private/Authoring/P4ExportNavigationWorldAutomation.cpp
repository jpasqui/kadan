#include "P4ExportNavigationWorld.h"

#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Engine/LevelStreamingAlwaysLoaded.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
UWorld* Fixture(const FString& Package, const TCHAR* Name)
{
    UWorld* World = NewObject<UWorld>(CreatePackage(*Package), Name, RF_Transient);
    World->WorldType = EWorldType::Editor;
    World->InitializeNewWorld(UWorld::InitializationValues().AllowAudioPlayback(false)
        .CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false)
        .ShouldSimulatePhysics(false));
    return World;
}

void Stream(UWorld* Parent, UWorld* Child, const FVector& Offset)
{
    ULevelStreamingAlwaysLoaded* Streaming = NewObject<ULevelStreamingAlwaysLoaded>(Parent);
    Streaming->SetWorldAssetByPackageName(FName(*Child->GetOutermost()->GetName()));
    Streaming->LevelTransform = FTransform(Offset);
    Parent->AddStreamingLevel(Streaming);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4NavigationWorldSnapshot, "P4MapKit.Export.NavigationWorldSnapshot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FP4NavigationWorldSnapshot::RunTest(const FString&)
{
    UWorld* Open = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    if (!TestNotNull(TEXT("The owned editor world opens"), Open))
    {
        return false;
    }
    AActor* Unsaved = Open->SpawnActor<AStaticMeshActor>(FVector(12, 34, 56), FRotator::ZeroRotator);
    Open->GetOutermost()->SetDirtyFlag(true);
    const FString Root = TEXT("/Temp/KadanNavigationFixture/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TStrongObjectPtr<UWorld> Source(Fixture(Root / TEXT("Root"), TEXT("Root")));
    TStrongObjectPtr<UWorld> Child(Fixture(Root / TEXT("Terrain"), TEXT("Terrain")));
    TStrongObjectPtr<UWorld> Hero(Fixture(Root / TEXT("Layer_Hero_Objects"), TEXT("Layer_Hero_Objects")));
    TStrongObjectPtr<UWorld> Nested(Fixture(Root / TEXT("Nested"), TEXT("Nested")));
    UMapBuildDataRegistry* BuildData = NewObject<UMapBuildDataRegistry>(Source->GetOutermost(),
        TEXT("SnapshotBuiltData"), RF_Public | RF_Standalone);
    Source->PersistentLevel->MapBuildData = BuildData;
    const EObjectFlags BuildDataFlags = BuildData->GetFlags();
    ON_SCOPE_EXIT
    {
        BuildData->ClearFlags(RF_Public | RF_Standalone);
        Nested->CleanupWorld();
        Hero->CleanupWorld();
        Child->CleanupWorld();
        Source->CleanupWorld();
    };
    Stream(Source.Get(), Child.Get(), FVector(100, 0, 0));
    Stream(Child.Get(), Hero.Get(), FVector(0, 200, 0));
    Stream(Hero.Get(), Nested.Get(), FVector(0, 0, 300));
    FActorSpawnParameters Spawn;
    Spawn.Name = TEXT("SnapshotGround");
    AStaticMeshActor* Ground = Child->SpawnActor<AStaticMeshActor>(FVector(10, 20, 30), FRotator::ZeroRotator, Spawn);
    Spawn.Name = TEXT("NestedGround");
    AActor* NestedGround = Nested->SpawnActor<AStaticMeshActor>(FVector(1, 2, 3), FRotator::ZeroRotator, Spawn);
    AActor* Anchor = Source->SpawnActor<AActor>();
    Ground->SetOwner(Anchor);
    Source->GetOutermost()->SetDirtyFlag(false);
    Child->GetOutermost()->SetDirtyFlag(true);
    const bool Navigation = Ground->GetStaticMeshComponent()->CanEverAffectNavigation();
    FString Error;
    {
        FP4ExportNavigationWorld Snapshot;
        UWorld* Copy = Snapshot.Load(Source->GetOutermost()->GetName(), Error);
        if (!TestNotNull(*Error, Copy))
        {
            return false;
        }
        TestNotEqual(TEXT("The navigation world is isolated"), Copy, Source.Get());
        UMapBuildDataRegistry* CopiedBuildData = Copy->PersistentLevel->MapBuildData;
        TestNull(TEXT("The collision snapshot excludes the source lighting build data"), CopiedBuildData);
        TestEqual(TEXT("Nested streaming levels are included"), Copy->GetLevels().Num(), 4);
        int32 HeroCount = 0;
        bool FoundGround = false;
        bool FoundNested = false;
        for (const auto& Pair : Snapshot.Variants())
        {
            TestEqual(TEXT("Each copied level belongs to the collision world"), Pair.Key->OwningWorld, Copy);
            HeroCount += Pair.Value ? 1 : 0;
            for (AActor* Actor : Pair.Key->Actors)
            {
                if (Actor && Actor->GetFName() == Ground->GetFName())
                {
                    FoundGround = true;
                    TestTrue(TEXT("The streaming transform is applied to copied collision"),
                        Actor->GetActorLocation().Equals(FVector(110, 20, 30)));
                    TestTrue(TEXT("The source actor is never registered in the snapshot"), Actor != Ground);
                    TestTrue(TEXT("Cross-level references point to copied actors"),
                        Actor->GetOwner() && Actor->GetOwner() != Anchor && Actor->GetOwner()->GetWorld() == Copy);
                }
                if (Actor && Actor->GetFName() == NestedGround->GetFName())
                {
                    FoundNested = true;
                    TestTrue(TEXT("Nested streaming transforms are composed"),
                        Actor->GetActorLocation().Equals(FVector(101, 202, 303)));
                }
            }
        }
        TestEqual(TEXT("Layer roles also apply to nested children"), HeroCount, 2);
        TestTrue(TEXT("The copied levels retain their collision actors"), FoundGround && FoundNested);
    }
    TestEqual(TEXT("The creator's open map remains current"), GEditor->GetEditorWorldContext().World(), Open);
    TestTrue(TEXT("The unsaved creator edit remains present"), IsValid(Unsaved) &&
        Unsaved->GetActorLocation().Equals(FVector(12, 34, 56)) && Open->GetOutermost()->IsDirty());
    TestTrue(TEXT("The source actor's transform is unchanged"), Ground->GetActorLocation().Equals(FVector(10, 20, 30)));
    TestEqual(TEXT("The source cross-level reference is unchanged"), Ground->GetOwner(), Anchor);
    TestTrue(TEXT("The nested source transform is unchanged"), NestedGround->GetActorLocation().Equals(FVector(1, 2, 3)));
    TestEqual(TEXT("The source component navigation setting is unchanged"),
        Ground->GetStaticMeshComponent()->CanEverAffectNavigation(), Navigation);
    TestFalse(TEXT("A clean source package remains clean"), Source->GetOutermost()->IsDirty());
    TestTrue(TEXT("An unsaved source package remains dirty"), Child->GetOutermost()->IsDirty());
    TestTrue(TEXT("The source build data keeps its original flags"), BuildData->GetFlags() == BuildDataFlags);
    Stream(Source.Get(), Child.Get(), FVector(300, 0, 0));
    {
        FP4ExportNavigationWorld Snapshot;
        TestNull(TEXT("Ambiguous repeated instances are refused"), Snapshot.Load(Source->GetOutermost()->GetName(), Error));
        TestFalse(TEXT("The refusal explains the graph limitation"), Error.IsEmpty());
        TestTrue(TEXT("The refusal names the repeated reference as the fix"),
            Error.Contains(TEXT("Remove the repeated streaming reference")));
    }
    TestEqual(TEXT("A failed snapshot also preserves the open map"), GEditor->GetEditorWorldContext().World(), Open);
    return !HasAnyErrors();
}
#endif

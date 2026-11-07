#if P4_MAPKIT_TESTS
#include "P4CollisionHelperDisplay.h"
#include "P4LevelChanges.h"
#include "P4OriginalLevelImport.h"
#include "P4StaticMeshContent.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalCollisionHelperDisplayTest, "P4MapKit.Editor.SourceCollisionHelperDisplay",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalCollisionHelperDisplayTest::RunTest(const FString&)
{
    const FString Map = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_COLLISION_HELPER_MAP"));
    if (Map.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. Select the owned Area002 fixture in P4_NATIVE_COLLISION_HELPER_MAP."));
        return true;
    }
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Map);
    if (!TestNotNull(TEXT("The actual imported surface map opens"), World))
        return false;
    ON_SCOPE_EXIT
    {
        UEditorLoadingAndSavingUtils::NewBlankMap(false);
    };
    TSet<FName> Expected;
    FString Error;
    if (!TestTrue(TEXT("Matching daytime captures activate only their qualified original day graph: ") + Error,
                  P4ActivateOriginalMapEditorLayers(World, TEXT("day"), Expected, Error)))
        return false;
    int32 AlternateLayers = 0;
    for (ULevelStreaming* Stream : World->GetStreamingLevels())
        if (Stream)
        {
            const bool Selected = Expected.Contains(Stream->GetWorldAssetPackageFName());
            TestEqual(TEXT("Each source streaming layer has selected daytime visibility"),
                      Stream->GetShouldBeVisibleInEditor(), Selected);
            if (!Selected)
                ++AlternateLayers;
        }
    TestTrue(TEXT("The real surface fixture distinguishes alternate night and hero layers"), AlternateLayers > 0);
    AActor* Helper = nullptr;
    for (ULevel* Level : World->GetLevels())
        if (Level)
            for (AActor* Actor : Level->Actors)
                if (IsValid(Actor) && Actor->GetClass()->GetName() == TEXT("GAirWallBox_C"))
                {
                    Helper = Actor;
                    break;
                }
    if (!TestNotNull(TEXT("The real game air-wall helper exists"), Helper))
        return false;
    ULevel* Level = Helper->GetLevel();
    UWorld* Source = Cast<UWorld>(Level->GetOuter());
    const bool WasHidden = Helper->IsTemporarilyHiddenInEditor();
    ON_SCOPE_EXIT
    {
        if (IsValid(Helper))
            Helper->SetIsTemporarilyHiddenInEditor(WasHidden);
    };
    Helper->SetIsTemporarilyHiddenInEditor(false);
    TInlineComponentArray<UStaticMeshComponent*> Parts(Helper);
    if (!TestTrue(TEXT("The actual helper has its collision geometry"), Parts.Num() > 0))
        return false;
    const FTransform Transform = Helper->GetActorTransform();
    const ECollisionEnabled::Type Collision = Parts[0]->GetCollisionEnabled();
    UStaticMesh* Mesh = Parts[0]->GetStaticMesh();
    const FString MeshState = P4StaticMeshState(Mesh);
    const auto Overrides = Parts[0]->OverrideMaterials;
    Parts[0]->SetMaterial(0, UMaterial::GetDefaultMaterial(MD_Surface));
    P4HideOriginalCollisionHelpers(World);
    TestFalse(TEXT("An explicit material makes an original helper a visible authored object"),
              Helper->IsTemporarilyHiddenInEditor());
    Parts[0]->OverrideMaterials = Overrides;
    Parts[0]->MarkRenderStateDirty();
    const auto ActorCount = [&]()
    {
        int32 Count = 0;
        for (AActor* Actor : Level->Actors)
            Count += IsValid(Actor);
        return Count;
    };
    const int32 Actors = ActorCount();
    const bool Dirty = Level->GetOutermost()->IsDirty();
    const FString State = P4AnalyzeOriginalLevelChanges(Source).State;
    FActorSpawnParameters Spawn;
    Spawn.OverrideLevel = Level;
    Spawn.ObjectFlags |= RF_Transient;
    AStaticMeshActor* Creator = World->SpawnActor<AStaticMeshActor>(Spawn);
    if (!TestNotNull(TEXT("The creator negative control is placed"), Creator))
        return false;
    ON_SCOPE_EXIT
    {
        if (IsValid(Creator))
            World->DestroyActor(Creator);
        Level->GetOutermost()->SetDirtyFlag(Dirty);
    };
    Creator->GetStaticMeshComponent()->SetStaticMesh(Mesh);
    P4HideOriginalCollisionHelpers(World);
    TestTrue(TEXT("An original collision-only helper is hidden temporarily in the editor"),
             Helper->IsTemporarilyHiddenInEditor());
    TestFalse(TEXT("Creator actors are not hidden by this source policy"), Creator->IsTemporarilyHiddenInEditor());
    World->DestroyActor(Creator);
    Creator = nullptr;
    TestEqual(TEXT("No original actor was removed"), ActorCount(), Actors);
    TestTrue(TEXT("The helper retains its editable transform"), Helper->GetActorTransform().Equals(Transform));
    TestEqual(TEXT("The helper retains its collision behavior"), int32(Parts[0]->GetCollisionEnabled()),
              int32(Collision));
    TestTrue(TEXT("The source mesh and its serialized geometry are unchanged"),
             P4StaticMeshState(Mesh).Equals(MeshState, ESearchCase::CaseSensitive));
    TestTrue(TEXT("The actual source level cook is unchanged"),
             P4AnalyzeOriginalLevelChanges(Source).State.Equals(State, ESearchCase::CaseSensitive));
    return !HasAnyErrors();
}
#endif

#include "P4SceneTeardown.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/DefaultPhysicsVolume.h"
#include "PrecomputedVolumetricLightmap.h"
#include "RenderCommandFence.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UnrealType.h"

namespace
{
FDelegateHandle FinishDestroyHandle, PreCollectHandle, PostCollectHandle, WorldCleanupHandle;
// A scene was released for deletion on the rendering thread and nothing has waited for it yet.
bool SceneDeletionPending = false;

void ReleaseDetachedPhysicsCaches(UWorld* Dying, bool, bool CleanupResources)
{
    if (!Dying || !CleanupResources || !IsInGameThread())
        return;
    FObjectProperty* Cache = FindFProperty<FObjectProperty>(UWorld::StaticClass(), TEXT("DefaultPhysicsVolume"));
    if (!Cache || Cache->GetOwnerStruct() != UWorld::StaticClass() ||
        Cache->PropertyClass != APhysicsVolume::StaticClass() || Cache->ArrayDim != 1 ||
        !Cache->HasAnyPropertyFlags(CPF_Transient))
        return;
    for (TObjectIterator<UWorld> It; It; ++It)
    {
        UWorld* Asset = *It;
        if (!IsValid(Asset) || Asset == Dying || !Asset->HasAnyFlags(RF_Standalone) ||
            (GEngine && GEngine->GetWorldContextFromWorld(Asset)))
            continue;
        auto* Volume = Cast<ADefaultPhysicsVolume>(Cache->GetObjectPropertyValue_InContainer(Asset));
        ULevel* Level = Volume ? Volume->GetLevel() : nullptr;
        if (!IsValid(Volume) || Volume->IsTemplate() || !Volume->HasAnyFlags(RF_Transient) || !Level ||
            Level->OwningWorld != Dying || Volume->GetOutermost() != Asset->GetOutermost())
            continue;
        // A retained sublevel asset can cache a volume in a formerly loaded level after
        // package replacement. That transient actor keeps Level->OwningWorld alive and
        // makes Map_Load's leak check fatal. Release only the dying-world cache; the
        // engine owns actor/level teardown and will recreate the cache when needed.
        Cache->SetObjectPropertyValue_InContainer(Asset, nullptr);
    }
}

// Removes the volumes of the levels this collection destroys, on the game thread, before any
// scene of the collection goes to the rendering thread to be deleted. ULevel::FinishDestroy then
// deletes nothing more (it deletes the pointer it holds, now null). A level's volume is only
// deleted once its BeginDestroy's render removal is done, as FinishDestroy itself waits for.
void RemoveDyingLevelVolumes()
{
    for (FRawObjectIterator It(false); It; ++It)
    {
        FUObjectItem* Item = *It;
        UObject* Object = Item ? static_cast<UObject*>(Item->Object) : nullptr;
        if (!Object || !Item->IsUnreachable() || !Object->IsA<ULevel>())
            continue;
        ULevel* Level = static_cast<ULevel*>(Object);
        if (!Level->HasAnyFlags(RF_BeginDestroyed) || Level->HasAnyFlags(RF_FinishDestroyed) ||
            !Level->PrecomputedVolumetricLightmap)
            continue;
        while (!Level->IsReadyForFinishDestroy())
            FPlatformProcess::Sleep(0);
        delete Level->PrecomputedVolumetricLightmap;
        Level->PrecomputedVolumetricLightmap = nullptr;
    }
}

void WaitForReleasedScenes()
{
    if (!SceneDeletionPending)
        return;
    SceneDeletionPending = false;
    FRenderCommandFence Fence;
    Fence.BeginFence();
    Fence.Wait();
}

void BeforeWorldFinishDestroy(UWorld* World)
{
    // UWorld::FinishDestroy releases the scene of an initialized world right after this.
    if (!World || !World->Scene || !World->bIsWorldInitialized || !IsInGameThread() || !IsGarbageCollecting())
        return;
    // Every time, not once per collection: a purge spread over several calls (or one that the next
    // collection finishes first) still has its dying levels' volumes removed before each release. Once
    // they are removed, a later scan in the same purge finds nothing to do.
    RemoveDyingLevelVolumes();
    SceneDeletionPending = true;
}

// The editor always purges in full inside CollectGarbage (GIsEditor in CollectGarbageInternal), so
// every world's FinishDestroy runs before PostGarbageCollect and the wait there covers it. A purge that
// ends outside a collection (an incremental purge from the engine's tick, which the editor never leaves
// work for) is waited for when the next collection starts, so nothing carries into it.
void BeforeCollection()
{
    WaitForReleasedScenes();
}
void AfterCollection()
{
    WaitForReleasedScenes();
}
} //namespace

void P4StartSceneTeardownGuard()
{
    if (FinishDestroyHandle.IsValid())
        return;
    FinishDestroyHandle = FWorldDelegates::OnPreWorldFinishDestroy.AddStatic(&BeforeWorldFinishDestroy);
    WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddStatic(&ReleaseDetachedPhysicsCaches);
    PreCollectHandle = FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddStatic(&BeforeCollection);
    PostCollectHandle = FCoreUObjectDelegates::GetPostGarbageCollect().AddStatic(&AfterCollection);
}

void P4StopSceneTeardownGuard()
{
    if (!FinishDestroyHandle.IsValid())
        return;
    FWorldDelegates::OnPreWorldFinishDestroy.Remove(FinishDestroyHandle);
    FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
    FCoreUObjectDelegates::GetPreGarbageCollectDelegate().Remove(PreCollectHandle);
    FCoreUObjectDelegates::GetPostGarbageCollect().Remove(PostCollectHandle);
    FinishDestroyHandle.Reset();
    WorldCleanupHandle.Reset();
    PreCollectHandle.Reset();
    PostCollectHandle.Reset();
    WaitForReleasedScenes();
}

bool P4SceneTeardownGuardActive()
{
    return FinishDestroyHandle.IsValid();
}

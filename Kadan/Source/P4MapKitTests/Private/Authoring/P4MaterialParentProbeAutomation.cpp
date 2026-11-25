#if P4_MAPKIT_TESTS
// Diagnostic probe, on with P4_NATIVE_PARENT_PROBE=1. After every automation test it names each material instance whose
// rendering-thread parent is not the parent the instance has. The engine's rendering thread follows that pointer for
// every instance when a shader compile finishes (SetShaderMapsOnMaterialResources_RenderThread), so a parent that was
// destroyed while an instance still points at it there stops the editor at the next compile.
#include "CoreMinimal.h"
#include "HAL/PlatformMisc.h"
#include "HAL/UnrealMemory.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"
#include "MaterialShared.h"
#include "Misc/AutomationTest.h"
#include "RenderingThread.h"
#include "UObject/ReferenceChainSearch.h"
#include "UObject/UObjectIterator.h"

namespace
{
int32 GProbeTests = 0;

FString ProbeName(const void* Object, const TSet<const void*>& Live)
{
    if (!Object)
        return TEXT("none");
    if (!Live.Contains(Object))
        return FString::Printf(TEXT("a destroyed object at %p"), Object);
    const UObject* Known = static_cast<const UObject*>(Object);
    return Known->GetPathName() + (Known->IsPendingKill() ? TEXT(" (pending kill)") : TEXT(""));
}

void ProbeParents()
{
    FString Test = FString::Printf(TEXT("test %d"), ++GProbeTests);
    if (const FAutomationTestBase* Current = FAutomationTestFramework::Get().GetCurrentTest())
        Test = Current->GetTestName();
    // What the game thread last sent is applied before the rendering thread's copy is read.
    FlushRenderingCommands();
    TSet<const void*> Live;
    for (FObjectIterator It; It; ++It)
        Live.Add(*It);
    int32 Seen = 0, Odd = 0;
    for (TObjectIterator<UMaterialInstance> It; It; ++It)
    {
        UMaterialInstance* Instance = *It;
        void* const* Slots = reinterpret_cast<void* const*>(Instance->Resource);
        if (!Slots)
            continue;
        ++Seen;
        // FMaterialInstanceResource holds, one after another, the rendering thread's parent, its owner and the game
        // thread's copy of the parent (MaterialInstanceSupport.h, private): found by the owner.
        const SIZE_T Size = FMemory::GetAllocSize(Instance->Resource);
        const int32 Count = Size ? int32(Size / sizeof(void*)) : 64;
        int32 At = INDEX_NONE;
        for (int32 Index = 1; Index + 1 < Count; ++Index)
            if (Slots[Index] == Instance)
            {
                At = Index;
                break;
            }
        if (At == INDEX_NONE)
        {
            UE_LOG(LogTemp, Warning, TEXT("KadanParentProbe after %s: no owner in the proxy of %s"), *Test,
                *Instance->GetPathName());
            continue;
        }
        const void* RenderParent = Slots[At - 1];
        const void* GameCopy = Slots[At + 1];
        const UMaterialInterface* Parent = Instance->Parent;
        const bool Destroyed = RenderParent && !Live.Contains(RenderParent);
        // An instance without a parent draws with the default material.
        if (!Destroyed && (RenderParent == Parent || !Parent))
            continue;
        ++Odd;
        // Whether the engine's walk over every proxy still reaches this one.
        const bool Registered = FMaterialRenderProxy::GetMaterialRenderProxyMap().Contains(
            reinterpret_cast<FMaterialRenderProxy*>(Instance->Resource));
        UE_LOG(LogTemp, Warning,
            TEXT("KadanParentProbe after %s: %s%s has the parent %s; the rendering thread has %s, the game thread's copy is %s; proxy %s, begin destroyed %d, unreachable %d, rooted %d"),
            *Test, *Instance->GetPathName(), Instance->IsPendingKill() ? TEXT(" (pending kill)") : TEXT(""),
            *ProbeName(Parent, Live), *ProbeName(RenderParent, Live), *ProbeName(GameCopy, Live),
            Registered ? TEXT("registered") : TEXT("not registered"), int32(Instance->HasAnyFlags(RF_BeginDestroyed)),
            int32(Instance->IsUnreachable()), int32(Instance->IsRooted()));
        // What keeps an instance with a destroyed parent alive, once for each instance.
        static TSet<const void*> Told;
        if (Destroyed && !Told.Contains(Instance))
        {
            Told.Add(Instance);
            UE_LOG(LogTemp, Warning, TEXT("KadanParentProbe references to %s:"), *Instance->GetPathName());
            FReferenceChainSearch Search(Instance, EReferenceChainSearchMode::Shortest | EReferenceChainSearchMode::PrintResults);
        }
    }
    UE_LOG(LogTemp, Display, TEXT("KadanParentProbe after %s: %d instances, %d with another parent on the rendering thread"),
        *Test, Seen, Odd);
}
} //namespace

void P4StartMaterialParentProbe()
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_PARENT_PROBE")) != TEXT("1"))
        return;
    FAutomationTestFramework::Get().PostTestingEvent.AddStatic(&ProbeParents);
    UE_LOG(LogTemp, Display, TEXT("KadanParentProbe is on"));
}
#else
void P4StartMaterialParentProbe() {}
#endif

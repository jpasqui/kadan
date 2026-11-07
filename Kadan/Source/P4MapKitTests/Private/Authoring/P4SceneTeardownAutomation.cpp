#include "P4KadanTestSeams.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "PrecomputedVolumetricLightmap.h"
#include "RenderingThread.h"
#include "SceneInterface.h"
#include "UObject/Package.h"
#include "P4LevelImportWorld.h"

// What a Kadan import does many times over, and what the tester's editor stopped on (Area002 import,
// 2026-10-22): worlds made for imported levels are collected while the rendering thread is behind,
// and the game thread then constructs levels (a map load). Each collected world's scene is deleted on
// the rendering thread and takes its volume out of the engine's unlocked volume map; each level
// constructed or destroyed on the game thread puts one in or takes one out. With the module's guard
// the scenes are gone when the collection returns. With P4_KADAN_SCENE_RACE_UNGUARDED=1 the guard is
// stopped for the run, which is the control: the editor is expected to stop on
// "Assertion failed: !AllocationFlags[Index]" (or another check of the same map). Needs a renderer:
// with -NullRHI a world gets no real scene and nothing races.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SceneTeardownRaceAutomation, "P4MapKit.Kadan.SceneTeardownRace",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4SceneTeardownRaceAutomation::RunTest(const FString& Parameters)
{
    const FP4KadanSceneTeardownTestSeams* Seams = P4KadanSceneTeardownSeams();
    if (!Seams)
    {
        AddWarning(FString(P4_MAPKIT_NOT_RUN_TEXT) + TEXT("This Kadan build has no scene teardown seam."));
        return true;
    }
    if (!TestTrue(TEXT("The scene teardown guard runs with the module"), Seams->Active()))
        return false;
    const bool Control = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_SCENE_RACE_UNGUARDED")) == TEXT("1");
    if (Control)
        Seams->Stop();
    ON_SCOPE_EXIT
    {
        if (Control)
            Seams->Start();
    };
    const int32 Rounds = FMath::Max(
        1, FCString::Atoi(*FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_SCENE_RACE_ROUNDS"))));
    const int32 PerRound = 24;
    int64 Volumes = 0;
    int32 Scenes = 0;
    for (int32 Round = 0; Round < FMath::Max(Rounds, 16); ++Round)
    {
        TArray<UWorld*> Made;
        for (int32 Index = 0; Index < PerRound; ++Index)
        {
            UPackage* Package = CreatePackage(
                *FString::Printf(TEXT("/Temp/P4SceneTeardownRace/R%d_W%d"), Round, Index));
            UWorld* World = P4CreateOriginalImportWorld(FName(*FString::Printf(TEXT("SceneRace%d"), Index)), Package);
            if (!TestNotNull(TEXT("Kadan makes an import world"), World))
                return false;
            World->SetFlags(RF_Public | RF_Standalone);
            if (World->Scene && World->Scene->GetRenderScene())
                ++Scenes;
            Made.Add(World);
        }
        if (Round == 0 && Scenes == 0)
        {
            for (UWorld* World : Made)
            {
                World->ClearFlags(RF_Public | RF_Standalone);
                World->DestroyWorld(false);
                World->MarkPendingKill();
            }
            CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
            AddWarning(FString(P4_MAPKIT_NOT_RUN_TEXT) +
                TEXT("Import worlds have no renderer scene here (-NullRHI); nothing can race."));
            return true;
        }
        // The rendering thread is behind when the scenes are released, as it is during a long import
        // (Slate waits on it there): the first scene released in the collection waits behind a hold.
        bool Held = false;
        const FDelegateHandle Hold = FWorldDelegates::OnPreWorldFinishDestroy.AddLambda(
            [&Held](UWorld* World)
            {
                if (Held || !World || !World->Scene)
                    return;
                Held = true;
                ENQUEUE_RENDER_COMMAND(P4SceneRaceHold)(
                    [](FRHICommandListImmediate&) { FPlatformProcess::Sleep(0.01f); });
            });
        ON_SCOPE_EXIT
        {
            FWorldDelegates::OnPreWorldFinishDestroy.Remove(Hold);
        };
        // Kadan's discard of a level it does not keep (P4LevelBuild.cpp, Discard), for all of them at once.
        for (UWorld* World : Made)
        {
            World->ClearFlags(RF_Public | RF_Standalone);
            World->DestroyWorld(false);
            World->MarkPendingKill();
            World->GetOutermost()->SetDirtyFlag(false);
        }
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        FWorldDelegates::OnPreWorldFinishDestroy.Remove(Hold);
        // What constructing and destroying levels does to the volume map (ULevel's constructor and
        // FinishDestroy), for as long as the deletions above can take.
        const double Until = FPlatformTime::Seconds() + 0.06;
        while (FPlatformTime::Seconds() < Until)
            for (int32 Step = 0; Step < 64; ++Step)
            {
                delete new FPrecomputedVolumetricLightmap();
                ++Volumes;
            }
        FlushRenderingCommands();
    }
    AddInfo(FString::Printf(
        TEXT(
            "%s: %d import worlds with a renderer scene collected in %d rounds, %lld level volumes made and removed after them."),
        Control ? TEXT("Unguarded control") : TEXT("Guarded"), Scenes, FMath::Max(Rounds, 16), Volumes));
    return true;
}
#endif

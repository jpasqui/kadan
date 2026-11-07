#if P4_MAPKIT_TESTS
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Components/ChildActorComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "FileHelpers.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ColdChildComponentsTest, "P4MapKit.Kadan.ColdOriginalChildComponentStructure",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ColdChildComponentsTest::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_COLD_CHILD_COMPONENTS")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. Select the owned complete Cave009 fixture."));
        return true;
    }
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(
        TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave009/Cave009_F00/Cave009_F00_P"));
    if (!TestNotNull(TEXT("The actual saved complete map opens cold"), World))
        return false;
    ON_SCOPE_EXIT
    {
        UEditorLoadingAndSavingUtils::NewBlankMap(false);
    };
    AActor* Slope = nullptr;
    for (ULevel* Level : World->GetLevels())
        if (Level)
            if (AActor* Candidate = FindObject<AActor>(Level, TEXT("GSlopeBothSidecut80uu_2")))
                Slope = Candidate;
    if (!TestNotNull(TEXT("The actual source slope actor is present"), Slope))
        return false;
    for (const TCHAR* Name : {TEXT("GuideHigher"), TEXT("GuideLower")})
    {
        auto* Component = FindObject<UChildActorComponent>(Slope, Name);
        AActor* Child = Component ? Component->GetChildActor() : nullptr;
        if (!TestNotNull(TEXT("The original child actor exists"), Child))
            return false;
        for (UClass* Class = Child->GetClass(); Class; Class = Class->GetSuperClass())
            if (auto* Generated = Cast<UBlueprintGeneratedClass>(Class))
                AddInfo(FString::Printf(TEXT("Cold child class %s: SCS=%s roots=%d nodes=%d"), *Class->GetPathName(),
                    *GetPathNameSafe(Generated->SimpleConstructionScript),
                    Generated->SimpleConstructionScript ? Generated->SimpleConstructionScript->GetRootNodes().Num()
                                                        : -1,
                    Generated->SimpleConstructionScript ? Generated->SimpleConstructionScript->GetAllNodes().Num()
                                                        : -1));
        for (const TCHAR* Part : {TEXT("Pointer"), TEXT("DemoOverlapArea"), TEXT("DefaultSceneRoot")})
            TestNotNull(TEXT("A cold child retains its original scene component: ") + FString(Name) + TEXT(".") + Part,
                FindObject<USceneComponent>(Child, Part));
        auto* Root = Child->GetRootComponent();
        TestTrue(TEXT("A recovered class's original SCS root is not replaced with an editor instance root"),
            Root && Root->CreationMethod == EComponentCreationMethod::SimpleConstructionScript);
        if (FString(Name) == TEXT("GuideHigher"))
        {
            FActorSpawnParameters Parameters;
            Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            AActor* Placed = World->SpawnActor<AActor>(Child->GetClass(), FTransform::Identity, Parameters);
            TestNotNull(TEXT("The same recovered class remains independently placeable"), Placed);
            if (Placed)
            {
                TestFalse(TEXT("Independent placement is not classified as a child actor"), Placed->IsChildActor());
                TestTrue(TEXT("Independent placement retains its constructed scene root"),
                    Placed->GetRootComponent() &&
                        Placed->GetRootComponent()->CreationMethod ==
                            EComponentCreationMethod::SimpleConstructionScript);
                TestNotNull(TEXT("Independent placement retains its original scene subtree"),
                    FindObject<USceneComponent>(Placed, TEXT("Pointer")));
                World->DestroyActor(Placed);
            }
        }
    }
    return !HasAnyErrors();
}
#endif

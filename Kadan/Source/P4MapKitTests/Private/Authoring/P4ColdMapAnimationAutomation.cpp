#if P4_MAPKIT_TESTS
#include "P4LevelChanges.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "FileHelpers.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ColdMapAnimationTest, "P4MapKit.Animation.ColdMapKeepsOriginalAnimationClasses",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ColdMapAnimationTest::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_COLD_MAP_ANIMATION")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. Select the owned cold Area002 fixture in a fresh editor."));
        return true;
    }
    if (!TestNull(TEXT("The game animation class has not been preloaded by another test"),
            FindObject<UClass>(nullptr, TEXT("/Game/Carrot4/Characters/Teki/Kaburi/ABP_Kaburi.ABP_Kaburi_C"))))
        return false;
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(TEXT("/Game/Carrot4/Maps/Main/Area/Area002/Area002_P"));
    if (!TestNotNull(TEXT("The actual surface map opens cold through the normal editor loader"), World))
        return false;
    ON_SCOPE_EXIT
    {
        UEditorLoadingAndSavingUtils::NewBlankMap(false);
    };
    int32 Compared = 0;
    TArray<TWeakObjectPtr<USkeletalMeshComponent>> Watched;
    for (ULevel* Level : World->GetLevels())
        if (Level && Level->GetOutermost()->GetName().EndsWith(TEXT("/Area002_Teki_Day")))
        {
            for (AActor* Actor : Level->Actors)
                if (IsValid(Actor) &&
                    (Actor->GetClass()->GetName() == TEXT("GKaburi_C") ||
                        Actor->GetClass()->GetName() == TEXT("GWhiteShijimi_C")))
                {
                    TInlineComponentArray<USkeletalMeshComponent*> Components(Actor);
                    for (USkeletalMeshComponent* Part : Components)
                        if (Part->GetFName() == TEXT("CarrotSkeletalMesh"))
                        {
                            const IAnimClassInterface* Interface = Part->AnimClass
                                ? IAnimClassInterface::GetFromClass(Part->AnimClass)
                                : nullptr;
                            TestTrue(
                                TEXT("Cold opening preserves the original resolved animation class and skeleton: ") +
                                    Actor->GetName(),
                                Interface && Part->SkeletalMesh &&
                                    Interface->GetTargetSkeleton() == Part->SkeletalMesh->Skeleton);
                            ++Compared;
                            Watched.Add(Part);
                            auto* Template = Cast<USkeletalMeshComponent>(Part->GetArchetype());
                            AddInfo(FString::Printf(
                                TEXT("Before comparison %s: instance %s, archetype %s, archetype class %s"),
                                *Part->GetPathName(), *GetPathNameSafe(Part->AnimClass), *GetPathNameSafe(Template),
                                *GetPathNameSafe(Template ? Template->AnimClass : nullptr)));
                        }
                }
            const auto Changes = P4AnalyzeOriginalLevelChanges(Cast<UWorld>(Level->GetOuter()));
            TestTrue(TEXT("Ordinary cold opening leaves all source Teki_Day fields unchanged: ") + Changes.ErrorDetail,
                Changes.IsValid() && Changes.Unchanged);
            if (!Changes.Unchanged)
                AddInfo(P4LevelChangesJson(Changes).Left(6000));
            for (const auto& Part : Watched)
                if (Part.IsValid())
                    AddInfo(FString::Printf(TEXT("After comparison %s: instance %s"), *Part->GetPathName(),
                        *GetPathNameSafe(Part->AnimClass)));
            if (Watched.Num() && Watched[0].IsValid())
            {
                USkeletalMeshComponent* Part = Watched[0].Get();
                UClass* Original = Part->AnimClass;
                ON_SCOPE_EXIT
                {
                    Part->AnimClass = Original;
                };
                Part->AnimClass = nullptr;
                const auto Edited = P4AnalyzeOriginalLevelChanges(Cast<UWorld>(Level->GetOuter()));
                TestTrue(TEXT("A creator clearing an inherited animation class remains a real source edit"),
                    Edited.IsValid() && !Edited.Unchanged &&
                        Edited.OtherChanged.ContainsByPredicate([&](const FP4LevelActorChanges& Change)
                            { return Change.Actor == Part->GetOwner()->GetName(); }));
                TestNull(TEXT("Comparison never restores the old animation class into the creator's component"),
                    static_cast<UClass*>(Part->AnimClass));
            }
        }
    TestEqual(TEXT("The actual source has all six affected original creature components"), Compared, 6);
    return !HasAnyErrors();
}
#endif

#if P4_MAPKIT_TESTS
// Saving an imported map writes the selected layers into it. Opening the saved map must give the same levels as
// opening it before the save: Unreal's navigation reads every loaded level, and a hidden layer that never registered
// makes it log an error for each navigation component (a test fails on those). P4_AUDIT_MAP names the map, which this
// saves without changing it.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Components/ActorComponent.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "NavRelevantComponent.h"

namespace
{
// One line per loaded level: whether it shows, its actors, and its navigation components.
TMap<FString, FString> Levels(UWorld* World)
{
    TMap<FString, FString> Out;
    for (ULevel* Level : World->GetLevels())
    {
        if (!Level)
            continue;
        int32 Actors = 0, Parts = 0, Registered = 0;
        for (AActor* Actor : Level->Actors)
        {
            if (!Actor)
                continue;
            ++Actors;
            TArray<UNavRelevantComponent*> Components;
            Actor->GetComponents<UNavRelevantComponent>(Components);
            Parts += Components.Num();
            for (UNavRelevantComponent* Component : Components)
                Registered += Component->IsRegistered() ? 1 : 0;
        }
        Out.Add(Level->GetOutermost()->GetName(), FString::Printf(TEXT("visible %d, %d actors, %d navigation parts, %d registered"),
            int32(Level->bIsVisible), Actors, Parts, Registered));
    }
    return Out;
}
} // namespace

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FP4OriginalLayersAfterSave, "P4MapKit.Kadan.OriginalLayersAfterSave",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
void FP4OriginalLayersAfterSave::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
    const FString Map = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_AUDIT_MAP"));
    if (Map.StartsWith(TEXT("/Game/")) && FPackageName::IsValidLongPackageName(Map))
    {
        Names.Add(FPackageName::GetLongPackageAssetName(Map));
        Commands.Add(Map);
    }
}
bool FP4OriginalLayersAfterSave::RunTest(const FString& Map)
{
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Map);
    if (!TestNotNull(TEXT("The map opens"), World))
        return false;
    const TMap<FString, FString> Before = Levels(World);
    if (!TestTrue(TEXT("The map saves"), UEditorLoadingAndSavingUtils::SaveMap(World, Map)))
        return false;
    World = UEditorLoadingAndSavingUtils::LoadMap(Map);
    if (!TestNotNull(TEXT("The saved map opens"), World))
        return false;
    const TMap<FString, FString> After = Levels(World);
    TestEqual(TEXT("The saved map has the same levels"), After.Num(), Before.Num());
    for (const auto& Pair : Before)
    {
        const FString* Now = After.Find(Pair.Key);
        TestTrue(Pair.Key + TEXT(" is the same after the save: ") + Pair.Value + TEXT(" then ") + (Now ? *Now : TEXT("gone")),
            Now && *Now == Pair.Value);
    }
    return !HasAnyErrors();
}
#endif

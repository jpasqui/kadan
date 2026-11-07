#if P4_MAPKIT_TESTS
#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "GameFramework/Actor.h"

// Private live-game validation seam. Use the same completed editor movement notification as a
// viewport drag; the game pose and Keep/Stop result are independently read by the test harness.
static FAutoConsoleCommand P4MoveSelectedLiveActor(TEXT("Kadan.Test.MoveSelectedLiveActor"),
    TEXT("Private test: move the selected Game (live) actor by the supplied XYZ editor delta."),
    FConsoleCommandWithArgsDelegate::CreateLambda(
        [](const TArray<FString>& Args)
        {
            if (!GEditor || Args.Num() != 3 || !Args[0].IsNumeric() || !Args[1].IsNumeric() || !Args[2].IsNumeric())
                return;
            const FVector Delta(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]), FCString::Atof(*Args[2]));
            if (Delta.ContainsNaN())
                return;
            for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
            {
                AActor* Actor = Cast<AActor>(*It);
                if (!IsValid(Actor) || Actor->GetFolderPath() != TEXT("Game (live)") ||
                    !Actor->HasAnyFlags(RF_Transient))
                    continue;
                const FVector Before = Actor->GetActorLocation();
                Actor->Modify();
                if (!Actor->SetActorLocation(Before + Delta, false, nullptr, ETeleportType::TeleportPhysics))
                    continue;
                Actor->PostEditMove(true);
                UE_LOG(LogTemp, Display, TEXT("Kadan live move probe: %s, before %s, after %s, PostEditMove complete"),
                    *Actor->GetPathName(), *Before.ToString(), *Actor->GetActorLocation().ToString());
            }
        }));
#endif

// The game connection's self-test: a bounded exercise through the toolbar's actual command bindings,
// including Pause, Resume and Stop. A development seam, compiled only into a test build.

#include "P4GameConnectionInternal.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet2/DebuggerCommands.h"
#include "Misc/PackageName.h"

#if P4_KADAN_TEST_SEAMS

namespace P4GameConnection
{
void StartSelfTest()
{
    TSharedRef<int32> Step = MakeShared<int32>(0);
    TSharedRef<double> At = MakeShared<double>(0);
    TSharedRef<TWeakObjectPtr<AActor>> Captain = MakeShared<TWeakObjectPtr<AActor>>();
    TSharedRef<int32> Waited = MakeShared<int32>(0);
    const auto Log = [](const FString& Line)
    {
        UE_LOG(LogTemp, Display, TEXT("Kadan self-test: %s"), *Line);
        Session->AddLine(Session->ConsoleLines, TEXT("self-test  ") + Line);
    };
    Session->SelfTest = [Step, At, Captain, Waited, Log](double Now)
    {
        FGameSession& S = *Session;
        if (Now < *At)
            return;
        const auto Screen = [](const TCHAR* Name)
        {
            const FString Folder = FPlatformMisc::GetEnvironmentVariable(TEXT("KADAN_SCREENSHOT_DIR"));
            if (!Folder.IsEmpty())
                Screenshot(Folder / Name);
        };
        const FNativePlayCommands Commands;
        const auto Actions = FPlayWorldCommands::GlobalPlayWorldActions;
        switch (*Step)
        {
            case 0:
            {
                if (!S.bControl || !S.Mirror || S.Mirror->Count() < 2)
                    return;
                UWorld* Level = EditorWorld();
                // Wait for the captain's live actor, which comes once the game has said its class.
                for (TActorIterator<AActor> It(Level); It && !Captain->IsValid(); ++It)
                    if (It->HasAnyFlags(RF_Transient) && It->GetActorLabel().StartsWith(TEXT("AVATAR")))
                        *Captain = *It;
                if (!Captain->IsValid() && ++*Waited < 100)
                    return;
                Log(FString::Printf(TEXT("before selection, level has unsaved changes: %s"),
                    Level && Level->GetOutermost()->IsDirty() ? TEXT("yes") : TEXT("no")));
                Log(FString::Printf(TEXT("open map %s, game map %s, %d game actors, %d classes not in the project"),
                    Level ? *FPackageName::GetShortName(Level->GetOutermost()->GetName()) : TEXT("none"), *S.World,
                    S.Mirror->Count(), S.Mirror->Missing()));
                // Select the captain's live actor, the way a click in the viewport does.
                AActor* Actor = Captain->Get();
                if (Actor && GEditor)
                {
                    GEditor->SelectNone(false, true);
                    GEditor->SelectActor(Actor, true, true);
                    GEditor->MoveViewportCamerasToActor(*Actor, false);
                }
                Log(Actor ? FString::Printf(TEXT("selected %s, a %s, at %s"), *Actor->GetActorLabel(),
                                *Actor->GetClass()->GetName(), *Actor->GetActorLocation().ToString())
                          : TEXT("no captain in the level"));
                break;
            }
            case 1:
            {
                // Stay on the landing pad. A 3 m move can be off the floor: the game then respawns
                // the captain at the pad, which is not a failed editor command.
                AActor* Actor = Captain->Get();
                if (!Actor)
                {
                    Log(TEXT("no captain to move"));
                    break;
                }
                Actor->Modify();
                Actor->SetActorLocation(Actor->GetActorLocation() + FVector(50, 0, 0));
                GEditor->BroadcastOnActorMoved(Actor);
                Log(FString::Printf(
                    TEXT("moved the captain in the editor to %s"), *Actor->GetActorLocation().ToString()));
                break;
            }
            case 2:
                S.RunConsole(
                    TEXT("local p = entity.pose(player.captain(0)) return {p.location.x, p.location.y, p.location.z}"));
                break;
            case 3:
            {
                UPackage* Map = EditorWorld() ? EditorWorld()->GetOutermost() : nullptr;
                Log(TEXT("captain in the game ") + (S.ConsoleLines.Num() ? *S.ConsoleLines.Last() : FString()));
                Log(FString::Printf(
                    TEXT("level has unsaved changes: %s"), Map && Map->IsDirty() ? TEXT("yes") : TEXT("no")));
                Log(FString::Printf(TEXT("playing toolbar: play %d, pause %d, stop %d"),
                    Actions->GetActionForCommand(Commands.RepeatLastPlay)->IsVisible() == EVisibility::Visible,
                    Actions->GetActionForCommand(Commands.PausePlaySession)->IsVisible() == EVisibility::Visible,
                    Actions->GetActionForCommand(Commands.StopPlaySession)->IsVisible() == EVisibility::Visible));
                Screen(TEXT("connector-playing.png"));
                Actions->ExecuteAction(Commands.PausePlaySession.ToSharedRef());
                break;
            }
            case 4:
                if (S.Preview != EPreview::Paused || S.bControl)
                    return;
                Log(FString::Printf(TEXT("paused: game control %d, live actors %d, resume visible %d"), S.bControl,
                    S.Mirror ? S.Mirror->Count() : 0,
                    Actions->GetActionForCommand(Commands.ResumePlaySession)->IsVisible() == EVisibility::Visible));
                Screen(TEXT("connector-paused.png"));
                Actions->ExecuteAction(Commands.ResumePlaySession.ToSharedRef());
                break;
            case 5:
                if (S.Preview != EPreview::Playing || !S.bControl)
                    return;
                Log(TEXT("resumed: game control yes"));
                Actions->ExecuteAction(Commands.StopPlaySession.ToSharedRef());
                break;
            case 6:
                if (S.Preview != EPreview::Stopped || S.bControl)
                    return;
                {
                    int32 Left = 0;
                    for (TActorIterator<AActor> It(EditorWorld()); It; ++It)
                        if (It->GetFolderPath() == TEXT("Game (live)"))
                            ++Left;
                    Log(FString::Printf(TEXT("after Stop, live actors %d"), Left));
                }
                Log(FString::Printf(TEXT("stopped: live mirror %d, play visible %d, pause visible %d, stop visible %d"),
                    S.Mirror.IsValid(),
                    Actions->GetActionForCommand(Commands.RepeatLastPlay)->IsVisible() == EVisibility::Visible,
                    Actions->GetActionForCommand(Commands.PausePlaySession)->IsVisible() == EVisibility::Visible,
                    Actions->GetActionForCommand(Commands.StopPlaySession)->IsVisible() == EVisibility::Visible));
                Screen(TEXT("connector-stopped.png"));
                break;
            case 7:
                Actions->ExecuteAction(Commands.RepeatLastPlay.ToSharedRef());
                break;
            case 8:
                if (S.Preview != EPreview::Playing || !S.bControl || !S.Mirror || S.Mirror->Count() < 2)
                    return;
                Log(FString::Printf(TEXT("second Play: %s, %d live actors"), *S.World, S.Mirror->Count()));
                S.RunConsole(TEXT(
                    "local s=reflection.find_class('GameplayStatics') "
                    "local i=reflection.call(s,'GetGameInstance',{WorldContextObject=player.captain(0)}) "
                    "local m=reflection.get(i,'GameSceneMgr') "
                    "local next=reflection.call(m,'FindGameScene',{InSceneName='Cave001_F00_P',bOpenLevel=true,bRemove_P=true}) "
                    "reflection.call(m,'SceneJump',{InNextScene=next}) return 'travel requested'"));
                break;
            default:
            {
                if (S.Preview != EPreview::Stopped || S.bControl)
                    return;
                UPackage* Map = EditorWorld() ? EditorWorld()->GetOutermost() : nullptr;
                Log(FString::Printf(TEXT("after game travel: editor %s, game %s, live mirror %d, stopped %d"),
                    Map ? *FPackageName::GetShortName(Map->GetName()) : TEXT("none"), *S.World, S.Mirror.IsValid(),
                    S.Preview == EPreview::Stopped));
                Log(FString::Printf(TEXT("after Stop, level has unsaved changes: %s"),
                    Map && Map->IsDirty() ? TEXT("yes") : TEXT("no")));
                Log(TEXT("done"));
                S.SelfTest = nullptr;
                return;
            }
        }
        ++*Step;
        *At = Now + 4.0;
    };
}
} // namespace P4GameConnection

#endif

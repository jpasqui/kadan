// Internal to the game connection: the session with the game and what its files share.
#pragma once

#include "CoreMinimal.h"
#include "P4GameLink.h"
#include "P4GameMirror.h"
#include "P4GamePreviewStart.h"
#include "P4GamePreviewUpload.h"
#include "P4GamePreviewWorld.h"
#include "Framework/Commands/InputBindingManager.h"
#include "Framework/Commands/UICommandInfo.h"
#include "Dom/JsonObject.h"

class UNavigationSystemV1;
class SDockTab;

namespace P4GameConnection
{
extern bool Registered;
inline FText Label(const FString& Value)
{
    return FText::FromString(Value);
}
constexpr int32 MaxLines = 500;
UWorld* EditorWorld();
void Toast(const FString& Text, bool bFailed = false);
// One event as a short line: its name, then its fields without handles, numbers rounded, places as x y z.
FString Describe(const FString& Name, const TSharedPtr<FJsonObject>& Data);
inline const TCHAR* const EventNames[] = {TEXT("entity.spawned"), TEXT("entity.destroyed"), TEXT("combat.damaged"),
    TEXT("combat.defeated"), TEXT("combat.pikmin_died"), TEXT("behavior.changed"), TEXT("game.stage_changed"),
    TEXT("game.control_changed")};

enum class EState
{
    Off,
    Waiting,
    Connected
};
enum class EPreview
{
    Editing,
    Preparing,
    Loading,
    Playing,
    Pausing,
    Paused,
    Resuming,
    Leaving,
    Stopping,
    Stopped,
    Failed
};

FString LuaText(const FString& Value);

// Connection and preview have separate lifetimes. Stop removes the temporary editor actors and
// pauses the owned game; the connection stays available for the next Play, events and console.
class FGameSession
{
public:
    TUniquePtr<FP4GameLink> Link;
    TUniquePtr<FP4GameMirror> Mirror;
    FString PortText = TEXT("47720"), LogPath, World;
    bool bSync = false, bControl = false, bTailLog = true;
    TMap<FString, bool> Watched;
    TArray<TSharedPtr<FString>> EventLines, ConsoleLines;
    TFunction<void()> LinesChanged; // the panel refreshes its lists
    TFunction<void(double)> SelfTest;
    EPreview Preview = EPreview::Editing;
    FString Target, LastError;
    FP4GamePreviewBaseline PreviewBaseline;

    FGameSession();

    ~FGameSession();

    // A busy PC running the game and the editor together can go a few seconds between replies.
    bool Answering() const { return Link && Link->SecondsSinceReply() >= 0 && Link->SecondsSinceReply() < 6; }
    EState State() const { return !Link ? EState::Off : Answering() ? EState::Connected : EState::Waiting; }

    FString Status() const;

    void Save(const TCHAR* Key, const FString& Value);

    void AddLine(TArray<TSharedPtr<FString>>& Lines, const FString& Line);

    void SetSync(bool bOn);

    bool Busy() const
    {
        return Preview == EPreview::Preparing || Preview == EPreview::Loading || Preview == EPreview::Stopping ||
            Preview == EPreview::Pausing || Preview == EPreview::Resuming || Preview == EPreview::Leaving;
    }
    bool Active() const { return Busy() || Preview == EPreview::Playing || Preview == EPreview::Paused; }

    void EndEditorPreview();

    // Asks the controller to hold the owner's lease through a preparation, or to end the hold.
    void HoldLease(bool bHold);

    void Failed(const FString& Why);

    FString Failure(const FP4GameReply& Reply) const;

    void TogglePlay();

    void Start();

    // The game's controller can stay "starting" when loading fails or never ends. Release it, as a
    // map change does, so the next Play, Stop or new start is not refused.
    void ReleaseController();

    void Stop();

    void Pause(bool bPause);

    void PrepareController();

    void PrepareContent();

    void ReadPreview();

    bool Tick(float);

    void SetWatch(const FString& Event, bool bOn);

    // New lines of the log file since the last look. A file that got shorter was started again.
    void TailLog();

    void SetLogPath(const FString& Path);

    void RunConsole(const FString& Typed);

private:
    FDelegateHandle Ticker;
    bool bHelloWaiting = false, bPreviewWaiting = false, bControllerReady = false, bPlayRequested = false,
         bContentReady = false;
    bool bStartOverride = false, bStartApplied = false;
    bool bScratchWorld = false, bNeutralTravel = false;
    // A preparation (cook and upload, then maybe the neutral travel) whose answer is awaited, and its number: an
    // answer of an ended preparation is dropped.
    bool bPreparing = false;
    uint32 Preparation = 0;
    FP4GamePreviewStartResult SelectedStart;
    FString StartSource, PreparedFingerprint;
    TSharedPtr<FP4GamePreviewUpload> ContentTask;
    double NextHello = 0, NextLog = 0, NextPreview = 0, Deadline = 0;
    int64 WatchedGeneration = -1, LogOffset = -1;
    FString Owner = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    int32 RequestNumber = 0;
    TWeakObjectPtr<UWorld> PreviewWorld;
    TWeakObjectPtr<UNavigationSystemV1> PreviewNavigation;
};

extern TUniquePtr<FGameSession> Session;

struct FNativePlayCommands
{
    TSharedPtr<FUICommandInfo> RepeatLastPlay, PausePlaySession, ResumePlaySession, StopPlaySession;
    FNativePlayCommands()
    {
        auto& Bindings = FInputBindingManager::Get();
        RepeatLastPlay = Bindings.FindCommandInContext(TEXT("PlayWorld"), TEXT("RepeatLastPlay"));
        PausePlaySession = Bindings.FindCommandInContext(TEXT("PlayWorld"), TEXT("PausePlaySession"));
        ResumePlaySession = Bindings.FindCommandInContext(TEXT("PlayWorld"), TEXT("ResumePlaySession"));
        StopPlaySession = Bindings.FindCommandInContext(TEXT("PlayWorld"), TEXT("StopPlaySession"));
    }
    bool Ready() const { return RepeatLastPlay && PausePlaySession && ResumePlaySession && StopPlaySession; }
};

// Saves a PNG of the editor, trying again while its window is not drawn yet.
void Screenshot(const FString& Path);

// The panel and its lamp (SKadanGameConnection.cpp).
void RegisterStyle();
void UnregisterStyle();
extern TWeakPtr<SDockTab> LiveTab;

#if P4_KADAN_TEST_SEAMS
// Bounded exercise through the toolbar's actual command bindings (P4GameConnectionSelfTest.cpp).
void StartSelfTest();
#endif
} // namespace P4GameConnection

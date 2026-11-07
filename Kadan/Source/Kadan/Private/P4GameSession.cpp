// The session with the game: the connection, the preview's steps, events, the log and the console.

#include "P4GameConnectionInternal.h"
#include "P4Common.h"
#include "P4KadanSettings.h"
#include "P4GameLink.h"
#include "P4GameMirror.h"
#include "P4GamePreviewScript.h"
#include "P4GamePreviewUpload.h"
#include "P4GamePreviewStart.h"
#include "P4GamePreviewWorld.h"
#include "AssetRegistryModule.h"
#include "Brushes/SlateImageBrush.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "EditorStyleSet.h"
#include "EngineUtils.h"
#include "Engine/Selection.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "UObject/Package.h"
#include "NavigationSystem.h"
#include "FileHelpers.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Actor.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFilemanager.h"
#include "LevelEditor.h"
#include "Kismet2/DebuggerCommands.h"
#include "ToolMenus.h"
#include "ImageUtils.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/SWindow.h"

namespace P4GameConnection
{
namespace
{
FString Short(const TSharedPtr<FJsonValue>& Value)
{
    if (!Value || Value->IsNull())
        return TEXT("nil");
    FString Text;
    if (Value->TryGetString(Text))
        return Text;
    FString Out = P4CondensedJson(TArray<TSharedPtr<FJsonValue>>{Value});
    return Out.Mid(1, Out.Len() - 2);
}
} // namespace

UWorld* EditorWorld()
{
    return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

void Toast(const FString& Text, bool bFailed)
{
    if (bFailed)
        UE_LOG(LogTemp, Warning, TEXT("Kadan preview: %s"), *Text);
    FNotificationInfo Info(Label(Text));
    Info.ExpireDuration = bFailed ? 5.f : 3.f;
    if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
        Item->SetCompletionState(bFailed ? SNotificationItem::CS_Fail : SNotificationItem::CS_None);
}

// One event as a short line: its name, then its fields without handles, numbers rounded, places as x y z.
FString Describe(const FString& Name, const TSharedPtr<FJsonObject>& Data)
{
    FString Out = Name;
    if (!Data)
        return Out;
    for (const auto& Field : Data->Values)
    {
        const TSharedPtr<FJsonValue>& Value = Field.Value;
        FString Text;
        double Number = 0;
        bool bFlag = false;
        const TSharedPtr<FJsonObject>* Place = nullptr;
        if (Value->TryGetString(Text))
        {
            if (!Text.StartsWith(TEXT("@")) && !Text.IsEmpty())
                Out += TEXT("  ") + Text;
        }
        else if (Value->TryGetObject(Place) && (*Place)->HasField(TEXT("x")))
            Out += FString::Printf(TEXT("  at %.0f %.0f %.0f"), (*Place)->GetNumberField(TEXT("x")),
                (*Place)->GetNumberField(TEXT("y")), (*Place)->GetNumberField(TEXT("z")));
        else if (Value->TryGetNumber(Number))
        {
            if (Field.Key != TEXT("placement") || Number != 0)
                Out += FString::Printf(TEXT("  %s %g"), *Field.Key, FMath::RoundToDouble(Number * 10) / 10);
        }
        else if (Value->TryGetBool(bFlag))
        {
            if (bFlag)
                Out += TEXT("  ") + Field.Key;
        }
    }
    return Out;
}

FString LuaText(const FString& Value)
{
    return TEXT("\"") + Value.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\"")) + TEXT("\"");
}

FGameSession::FGameSession()
{
    P4KadanSettings::Read(P4KadanSettings::GamePort, PortText);
    P4KadanSettings::Read(P4KadanSettings::GameLog, LogPath);
    if (LogPath.IsEmpty())
        LogPath = FPlatformMisc::GetEnvironmentVariable(TEXT("APPDATA")) /
            TEXT("eden/sdmc/atmosphere/contents/0100B7C00933A000/tsuchi.log");
    for (const TCHAR* Name : EventNames)
        Watched.Add(Name, false);
    Watched[TEXT("entity.spawned")] = Watched[TEXT("entity.destroyed")] = Watched[TEXT("combat.defeated")] = true;
    Ticker = FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FGameSession::Tick), 0.1f);
    SetSync(true);
}

FGameSession::~FGameSession()
{
    FTicker::GetCoreTicker().RemoveTicker(Ticker);
    SetSync(false);
}

FString FGameSession::Status() const
{
    switch (State())
    {
        case EState::Off:
            return TEXT("Disconnected");
        case EState::Waiting:
            return TEXT("Waiting for the game");
        default:
            break;
    }
    if (Preview == EPreview::Failed)
        return LastError;
    if (Preview == EPreview::Preparing)
        return bNeutralTravel ? TEXT("Loading preview context")
            : ContentTask     ? ContentTask->Status()
                              : TEXT("Preparing preview");
    if (Preview == EPreview::Loading)
        return TEXT("Loading ") + Target;
    if (Preview == EPreview::Stopping)
        return TEXT("Stopping preview");
    if (Preview == EPreview::Leaving)
        return TEXT("Finishing game travel before stopping preview");
    if (Preview == EPreview::Pausing)
        return TEXT("Pausing preview");
    if (Preview == EPreview::Resuming)
        return TEXT("Resuming preview");
    if (Preview == EPreview::Paused)
        return TEXT("Paused · ") + World;
    if (Preview == EPreview::Stopped)
        return TEXT("Stopped · ") + World;
    if (World.IsEmpty())
        return TEXT("Connected");
    FString Out = Mirror ? FString::Printf(TEXT("%s  %d actors"), *World, Mirror->Count())
                         : TEXT("Connected · ") + World;
    if (Mirror && Mirror->Missing())
        Out += FString::Printf(TEXT(", %d classes not in the project"), Mirror->Missing());
    return Out;
}

void FGameSession::Save(const TCHAR* Key, const FString& Value)
{
    P4KadanSettings::Write(Key, Value);
    P4KadanSettings::Flush();
}

void FGameSession::AddLine(TArray<TSharedPtr<FString>>& Lines, const FString& Line)
{
    Lines.Add(MakeShared<FString>(Line));
    if (Lines.Num() > MaxLines)
        Lines.RemoveAt(0, Lines.Num() - MaxLines);
    if (LinesChanged)
        LinesChanged();
}

void FGameSession::SetSync(bool bOn)
{
    if (bOn == bSync)
        return;
    bSync = bOn;
    if (!bOn)
    {
        EndEditorPreview();
        if (Link)
            for (const auto& Item : Watched)
                if (Item.Value)
                    Link->Unwatch(Item.Key, [](const FP4GameReply&) {});
        if (Link && bControllerReady)
        {
            Link->Eval(TEXT("return kadan_preview and kadan_preview.release(") + LuaText(Owner) + TEXT(")"),
                [](const FP4GameReply&) {});
        }
        Link.Reset();
        World.Empty();
        bHelloWaiting = bPreviewWaiting = bControllerReady = bPlayRequested = false;
        Preview = EPreview::Editing;
        return;
    }
    const int32 Port = FCString::Atoi(*PortText);
    if (Port <= 0 || Port > 65535)
    {
        Toast(TEXT("The port is a number from 1 to 65535."), true);
        bSync = false;
        return;
    }
    Link = MakeUnique<FP4GameLink>(Port);
    Link->Timeout = 5.0;
    FString Error;
    if (!Link->Open(Error))
    {
        Link.Reset();
        Toast(Error, true);
        bSync = false;
        return;
    }
    Link->OnEvent = [this](const FString& Name, const TSharedPtr<FJsonObject>& Data)
    { AddLine(EventLines, Describe(Name, Data)); };
    NextHello = 0;
    WatchedGeneration = -1;
}

void FGameSession::EndEditorPreview()
{
    // Cancelling drops the preparation's answer, which would have ended the wait for it: end the wait here, or the
    // next Play and the status reads wait for good. A late answer of this preparation is dropped by its number.
    ++Preparation;
    if (bPreparing)
    {
        bPreparing = false;
        bPreviewWaiting = false;
        HoldLease(false);
    }
    if (ContentTask)
        ContentTask->Cancel();
    ContentTask.Reset();
    bContentReady = false;
    Mirror.Reset();
    if (UNavigationSystemV1* Navigation = PreviewNavigation.Get())
        Navigation->OnPIEEnd();
    PreviewNavigation.Reset();
}

void FGameSession::Failed(const FString& Why)
{
    bPlayRequested = false;
    EndEditorPreview();
    Preview = EPreview::Failed;
    LastError = Why;
    AddLine(ConsoleLines, Why);
    Toast(Why, true);
}

FString FGameSession::Failure(const FP4GameReply& Reply) const
{
    FString Detail;
    if (Reply.Whole)
        Reply.Whole->TryGetStringField(TEXT("detail"), Detail);
    return Detail.IsEmpty() ? Reply.Error : Detail;
}

void FGameSession::TogglePlay()
{
    if (Busy())
        return;
    if (Preview == EPreview::Playing)
    {
        Stop();
        return;
    }
    UWorld* Open = EditorWorld();
    if (!Open)
    {
        Toast(TEXT("Open a level before pressing Play."), true);
        return;
    }
    bScratchWorld = P4IsGamePreviewScratchWorld(Open);
    FString StartError;
    bStartOverride = P4GamePreviewStart(Open, bScratchWorld, SelectedStart, StartSource, StartError);
    bStartApplied = false;
    if (!StartError.IsEmpty())
    {
        Toast(StartError, true);
        return;
    }
    if (bScratchWorld)
        PreviewBaseline.Reset();
    else
    {
        if (!PreviewBaseline.IsFor(Open))
            PreviewBaseline.Reset();
        bool bCleanSources = !Open->GetOutermost()->IsDirty();
        for (ULevel* Level : Open->GetLevels())
            if (IsValid(Level) && !Level->HasAnyFlags(RF_Transient) && Level->GetOutermost()->IsDirty())
                bCleanSources = false;
        if (bCleanSources && !P4CaptureGamePreviewCleanBaseline(Open, PreviewBaseline, StartError))
        {
            Toast(StartError, true);
            return;
        }
        if (!P4GamePreviewOnlyLiveChanges(Open, StartError, PreviewBaseline.IsFor(Open) ? &PreviewBaseline : nullptr))
        {
            Toast(StartError.IsEmpty() ? TEXT("Save and export your map to the preview game before Play.") : StartError,
                true);
            return;
        }
    }
    Target = bScratchWorld ? FString() : FPackageName::GetShortName(Open->GetOutermost()->GetName());
    PreviewWorld = Open;
    if (UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Open))
        if (!Navigation->IsNavigationBuildingLocked(ENavigationBuildLock::NoUpdateInPIE))
        {
            PreviewNavigation = Navigation;
            Navigation->OnPIEStart();
            Navigation->CancelBuild();
        }
    LastError.Empty();
    PreparedFingerprint.Empty();
    bNeutralTravel = false;
    bContentReady = false;
    bPlayRequested = true;
    Preview = EPreview::Preparing;
    Deadline = FPlatformTime::Seconds() + 120.0;
    SetSync(true);
    if (!Link)
        Failed(TEXT("The preview connection could not open. Check Game connection Settings."));
}

void FGameSession::Start()
{
    UE_LOG(LogTemp, Display, TEXT("Kadan preview session %s"), *Owner);
    bPlayRequested = false;
    bPreviewWaiting = true;
    Preview = EPreview::Loading;
    Deadline = FPlatformTime::Seconds() + 120.0;
    ++RequestNumber;
    Link->Eval(FString::Printf(TEXT("return kadan_preview.play(%s,%s,%d,%s)"), *LuaText(Owner), *LuaText(Target),
                   RequestNumber, *LuaText(PreparedFingerprint)),
        [this](const FP4GameReply& Reply)
        {
            bPreviewWaiting = false;
            // A timed-out travel request may still have taken effect. Status resolves it;
            // replaying a non-idempotent scene change here would start a second load.
            if (!Reply.bOk && !Reply.bTimedOut)
            {
                Failed(Failure(Reply));
                return;
            }
            NextPreview = 0;
        });
}

void FGameSession::ReleaseController()
{
    bNeutralTravel = false;
    if (Link && bControllerReady)
        Link->Eval(TEXT("return kadan_preview and kadan_preview.release(") + LuaText(Owner) + TEXT(")"),
            [](const FP4GameReply&) {});
}

void FGameSession::Stop()
{
    if (!Link || Busy())
        return;
    Mirror.Reset();
    Preview = EPreview::Stopping;
    Deadline = FPlatformTime::Seconds() + 15.0;
    Link->Eval(TEXT("return kadan_preview.stop(") + LuaText(Owner) + TEXT(")"),
        [this](const FP4GameReply& Reply)
        {
            if (!Reply.bOk && !Reply.bTimedOut)
            {
                Failed(Failure(Reply));
                return;
            }
            NextPreview = 0;
        });
}

void FGameSession::Pause(bool bPause)
{
    if (!Link || Busy())
        return;
    if (bPause ? Preview != EPreview::Playing : Preview != EPreview::Paused)
        return;
    Preview = bPause ? EPreview::Pausing : EPreview::Resuming;
    Deadline = FPlatformTime::Seconds() + 15.0;
    Link->Eval(
        FString::Printf(TEXT("return kadan_preview.%s(%s)"), bPause ? TEXT("pause") : TEXT("resume"), *LuaText(Owner)),
        [this](const FP4GameReply& Reply)
        {
            if (!Reply.bOk && !Reply.bTimedOut)
            {
                Failed(Failure(Reply));
                return;
            }
            NextPreview = 0;
        });
}

void FGameSession::PrepareController()
{
    bPreviewWaiting = true;
    Link->Eval(FString(TEXT("if not kadan_preview or kadan_preview.version ~= 12 then ")) +
            TEXT("if kadan_preview and kadan_preview.shutdown then kadan_preview.shutdown() end ") +
            TEXT("kadan_preview=(function()\n") + P4Kadan::GamePreviewScript +
            TEXT("\nend)() end return kadan_preview.version"),
        [this](const FP4GameReply& Reply)
        {
            bPreviewWaiting = false;
            if (!Reply.bOk)
            {
                bPlayRequested = false;
                Failed(Failure(Reply));
                return;
            }
            bControllerReady = true;
        });
}

void FGameSession::PrepareContent()
{
    bPreviewWaiting = bPreparing = true;
    const uint32 Number = ++Preparation;
    // A first native cook can take longer than map travel. Later Play
    // requests reuse a mounted bundle and do no cooking or uploading.
    Deadline = FPlatformTime::Seconds() + 1200.0;
    ContentTask = MakeShared<FP4GamePreviewUpload>(*Link,
        [this, Number](bool bOk, const FString& Error)
        {
            if (Number != Preparation)
                return;
            bPreviewWaiting = bPreparing = false;
            HoldLease(false);
            if (!bOk)
            {
                if (!ContentTask || !ContentTask->NeedsNeutralTravel())
                {
                    Failed(Error);
                    return;
                }
                bNeutralTravel = bPreviewWaiting = bPreparing = true;
                ++RequestNumber;
                Deadline = FPlatformTime::Seconds() + 120.0;
                // Leave the preview area for the last of the game's own worlds the controller saw.
                Link->Eval(
                    FString::Printf(TEXT("local home=kadan_preview.status(%s).home if not home then error('Travel to ")
                                        TEXT("a game map in the preview game, then press Play again.') end ")
                                            TEXT("return kadan_preview.play(%s,home,%d)"),
                        *LuaText(Owner), *LuaText(Owner), RequestNumber),
                    [this, Number](const FP4GameReply& Reply)
                    {
                        if (Number != Preparation)
                            return;
                        bPreviewWaiting = bPreparing = false;
                        if (!Reply.bOk && !Reply.bTimedOut)
                        {
                            Failed(Failure(Reply));
                            return;
                        }
                        NextPreview = 0;
                    });
                return;
            }
            PreparedFingerprint = ContentTask->Fingerprint();
            const FString PreparedWorld = ContentTask->PreviewWorldName();
            if (bScratchWorld && PreparedWorld.IsEmpty())
            {
                Failed(TEXT("The new level was not registered in the preview game."));
                return;
            }
            if (!PreparedWorld.IsEmpty())
                Target = PreparedWorld;
            bContentReady = true;
            Deadline = FPlatformTime::Seconds() + 120.0;
        });
    // The cook holds the editor and the upload can be long: the stopped preview's pause and starting state stay.
    HoldLease(true);
    ContentTask->Start(bScratchWorld ? &SelectedStart : nullptr);
}

void FGameSession::HoldLease(bool bHold)
{
    if (Link && bControllerReady)
        Link->Eval(FString::Printf(TEXT("return kadan_preview.prepare(%s,%d)"), *LuaText(Owner), bHold ? 36000 : 0),
            [](const FP4GameReply&) {});
}

void FGameSession::ReadPreview()
{
    bPreviewWaiting = true;
    Link->Eval(TEXT("return kadan_preview and kadan_preview.status(") + LuaText(Owner) + TEXT(")"),
        [this](const FP4GameReply& Reply)
        {
            bPreviewWaiting = false;
            const TSharedPtr<FJsonObject>* Status = nullptr;
            if (!Reply.bOk || !Reply.Value || !Reply.Value->TryGetObject(Status))
            {
                if (Reply.bOk)
                {
                    bControllerReady = false;
                    if (Active())
                        Failed(TEXT("The preview game restarted. Press Play to start a new preview."));
                }
                return;
            }
            FString State;
            (*Status)->TryGetStringField(TEXT("state"), State);
            if (bNeutralTravel)
            {
                FString NeutralWorld;
                FString Requested;
                (*Status)->TryGetStringField(TEXT("world"), NeutralWorld);
                (*Status)->TryGetStringField(TEXT("target"), Requested);
                if (State == TEXT("playing") && !NeutralWorld.IsEmpty() && NeutralWorld == Requested)
                {
                    bNeutralTravel = false;
                    ContentTask.Reset();
                    NextPreview = 0;
                }
                return;
            }
            bool bOwned = false;
            (*Status)->TryGetBoolField(TEXT("owned"), bOwned);
            if (Active() && Preview != EPreview::Preparing && !bOwned)
            {
                Failed(TEXT("The preview session expired. Press Play to restart."));
                return;
            }
            if (Preview == EPreview::Loading && State == TEXT("playing"))
            {
                FString Map;
                (*Status)->TryGetStringField(TEXT("world"), Map);
                if (Map != Target || PreviewWorld.Get() != EditorWorld())
                {
                    Failed(TEXT("The editor map changed during Play. Stop and try again."));
                    return;
                }
                if (bStartOverride && !bStartApplied)
                {
                    bStartApplied = true;
                    bPreviewWaiting = true;
                    const FVector At = SelectedStart.Transform.GetLocation();
                    const FRotator Turn = SelectedStart.Transform.Rotator();
                    const FString Place = FString::Printf(TEXT("{x=%.9g,y=%.9g,z=%.9g}"), At.X, At.Y, At.Z);
                    const FString Rotation = FString::Printf(TEXT("{pitch=0,yaw=%.9g,roll=0}"), Turn.Yaw);
                    Link->Eval(
                        TEXT(
                            "local a=player.captain(0) local dog=captain.riding(a) local target=dog or a local ok,why=entity.warp(target,") +
                            Place + TEXT(",") + Rotation +
                            TEXT(") if not ok then ok,why=entity.set_pose(target,{location=") + Place +
                            TEXT(",rotation=") + Rotation + TEXT("}) end if not ok then error(why) end return true"),
                        [this](const FP4GameReply& Move)
                        {
                            bPreviewWaiting = false;
                            if (!Move.bOk)
                            {
                                Failed(TEXT("The game could not use the preview start: ") + Failure(Move));
                                return;
                            }
                            AddLine(ConsoleLines, TEXT("Preview start: ") + StartSource);
                            NextPreview = 0;
                        });
                    return;
                }
                World = Map;
                Mirror = MakeUnique<FP4GameMirror>(*Link, Owner);
                if (ContentTask)
                    Mirror->SetPlayContent(ContentTask->Packages());
                Mirror->Attach(PreviewWorld.Get());
                Preview = EPreview::Playing;
                AddLine(ConsoleLines,
                    TEXT("Playing ") + Target +
                        TEXT(
                            ". Changes are temporary; select an original actor's live copy to Keep Simulation Changes."));
            }
            if (Preview == EPreview::Stopping && State == TEXT("stopped"))
            {
                Preview = EPreview::Stopped;
                EndEditorPreview();
            }
            if (Preview == EPreview::Pausing && State == TEXT("paused"))
                Preview = EPreview::Paused;
            if (Preview == EPreview::Resuming && State == TEXT("playing"))
                Preview = EPreview::Playing;
        });
}

bool FGameSession::Tick(float)
{
    const double Now = FPlatformTime::Seconds();
    if (bTailLog && Now >= NextLog)
    {
        NextLog = Now + 0.5;
        TailLog();
    }
    if (!Link)
        return true;
    Link->Poll();
    if (Active() && PreviewWorld.Get() != EditorWorld())
    {
        if (!Busy())
            Stop();
        else
        {
            Failed(TEXT("The editor map changed. Preview ended; press Play to test the new map."));
            ReleaseController();
        }
    }
    if (!bHelloWaiting && Now >= NextHello)
    {
        bHelloWaiting = true;
        NextHello = Now + 1.0;
        Link->Hello(
            [this](const FP4GameReply& Reply)
            {
                bHelloWaiting = false;
                const TSharedPtr<FJsonObject>* Hello = nullptr;
                if (!Reply.bOk || !Reply.Value || !Reply.Value->TryGetObject(Hello))
                    return;
                (*Hello)->TryGetStringField(TEXT("world"), World);
                (*Hello)->TryGetBoolField(TEXT("control"), bControl);
                if ((Preview == EPreview::Playing || Preview == EPreview::Paused) && !World.IsEmpty() &&
                    World != Target)
                {
                    AddLine(ConsoleLines,
                        TEXT("The game left ") + Target +
                            TEXT(". Stopping preview; the editor stays on its current map."));
                    Mirror.Reset();
                    Preview = EPreview::Leaving;
                    Deadline = FPlatformTime::Seconds() + 120.0;
                }
                // Let streaming and the destination's entry sequence finish before holding flow.
                if (Preview == EPreview::Leaving && bControl)
                {
                    Preview = EPreview::Playing;
                    Stop();
                }
                // The server keeps watches per client socket, so a new connection watches again.
                if (WatchedGeneration != Reply.Generation)
                {
                    WatchedGeneration = Reply.Generation;
                    for (const auto& Item : Watched)
                        if (Item.Value)
                            Link->Watch(Item.Key, [](const FP4GameReply&) {});
                }
            });
    }
    if (Busy() && Now > Deadline)
    {
        const bool bWasLoading = Preview == EPreview::Loading || bNeutralTravel;
        bPlayRequested = false;
        Failed(TEXT("The preview did not finish. Check the game connection and try again."));
        if (bWasLoading)
            ReleaseController();
    }
    if (!Answering())
        return true;
    if (bPlayRequested && !bPreviewWaiting && !bNeutralTravel)
    {
        if (!bControllerReady)
            PrepareController();
        else if (!bContentReady)
            PrepareContent();
        else
            Start();
    }
    if (bControllerReady && !bPreviewWaiting && Now >= NextPreview)
    {
        NextPreview = Now + 0.5;
        ReadPreview();
    }
    if (Mirror && (PreviewWorld.Get() != EditorWorld() || (!World.IsEmpty() && World != Target)))
        Failed(TEXT("The preview left the editor's map. Press Play to restart this map."));
    if (Mirror)
        Mirror->Tick(Now);
    if (SelfTest)
        SelfTest(Now);
    return true;
}

void FGameSession::SetWatch(const FString& Event, bool bOn)
{
    Watched.Add(Event, bOn);
    if (!Answering())
        return;
    const auto Done = [Event](const FP4GameReply& Reply)
    {
        if (!Reply.bOk && Reply.Error != TEXT("not_found"))
            Toast(FString::Printf(TEXT("%s not watched (%s)."), *Event, *Reply.Error), true);
    };
    if (bOn)
        Link->Watch(Event, Done);
    else
        Link->Unwatch(Event, Done);
}

void FGameSession::TailLog()
{
    IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
    const int64 Size = Files.FileSize(*LogPath);
    if (Size < 0)
        return;
    if (LogOffset < 0 || Size < LogOffset)
        LogOffset = LogOffset < 0 ? FMath::Max<int64>(0, Size - 4096) : 0;
    if (Size == LogOffset)
        return;
    TUniquePtr<IFileHandle> File(Files.OpenRead(*LogPath, true));
    if (!File || !File->Seek(LogOffset))
        return;
    TArray<uint8> Bytes;
    Bytes.SetNumUninitialized(int32(FMath::Min<int64>(Size - LogOffset, 1 << 20)));
    if (!File->Read(Bytes.GetData(), Bytes.Num()))
        return;
    int32 End = Bytes.Num();
    while (End > 0 && Bytes[End - 1] != '\n')
        --End;
    if (End == 0)
        return;
    LogOffset += End;
    const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), End);
    TArray<FString> Lines;
    FString(Text.Length(), Text.Get()).ParseIntoArrayLines(Lines);
    for (const FString& Line : Lines)
        AddLine(EventLines, Line);
}

void FGameSession::SetLogPath(const FString& Path)
{
    LogPath = Path;
    LogOffset = -1;
    Save(TEXT("GameLog"), LogPath);
}

void FGameSession::RunConsole(const FString& Typed)
{
    const FString Source = Typed.TrimStartAndEnd();
    if (Source.IsEmpty())
        return;
    if (!Link)
    {
        AddLine(ConsoleLines, TEXT("Turn on Sync first."));
        return;
    }
    AddLine(ConsoleLines, TEXT("> ") + Source);
    Link->Eval(Source,
        [this](const FP4GameReply& Reply)
        {
            const TArray<TSharedPtr<FJsonValue>>* Printed = nullptr;
            if (Reply.Whole && Reply.Whole->TryGetArrayField(TEXT("printed"), Printed))
                for (const auto& Line : *Printed)
                    AddLine(ConsoleLines, Line->AsString());
            FString Detail;
            if (!Reply.bOk && Reply.Whole)
                Reply.Whole->TryGetStringField(TEXT("detail"), Detail);
            AddLine(ConsoleLines, Reply.bOk ? Short(Reply.Value) : Reply.Error + TEXT("  ") + Detail);
        });
}

} // namespace P4GameConnection

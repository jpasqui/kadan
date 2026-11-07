// The game connection in the editor: its registration, the Play toolbar, Keep Simulation Changes and the
// console commands.

#include "P4GameConnection.h"
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

#define LOCTEXT_NAMESPACE "KadanGameConnection"

uint64 P4GamePlacementOf(const AActor* Actor)
{
    if (!Actor)
        return 0;
    const FStructProperty* Info = CastField<FStructProperty>(
        Actor->GetClass()->FindPropertyByName(TEXT("GenerateInfo")));
    const FNumericProperty* Id = Info
        ? CastField<FNumericProperty>(Info->Struct->FindPropertyByName(TEXT("DebugUniqueId")))
        : nullptr;
    if (!Id || !Id->IsInteger())
        return 0;
    return Id->GetUnsignedIntPropertyValue(
        Id->ContainerPtrToValuePtr<void>(Info->ContainerPtrToValuePtr<void>(const_cast<AActor*>(Actor))));
}

namespace P4GameConnection
{
bool Registered = false;
TUniquePtr<FGameSession> Session;

void Screenshot(const FString& Path);

namespace
{
// Reuse the Level Editor's existing Play/Pause/Resume/Stop entries and key bindings.
// Their visibility follows the external preview state; native PIE still delegates to UE4.
TMap<TSharedPtr<FUICommandInfo>, FUIAction> PreviousPlayActions;
FDelegateHandle ToolbarStartupHandle;
FDelegateHandle PlayInitTicker;
const FName ToolbarOwner(TEXT("KadanPreviewToolbar"));

TSharedPtr<FUICommandInfo> KeepCommand()
{
    return FInputBindingManager::Get().FindCommandInContext(TEXT("LevelEditor"), TEXT("KeepSimulationChanges"));
}

void BindPlay(const TSharedPtr<FUICommandInfo>& Command, const FUIAction& PreviewAction)
{
    const auto Actions = FPlayWorldCommands::GlobalPlayWorldActions;
    const FUIAction* Existing = Actions->GetActionForCommand(Command);
    if (!Existing)
        return;
    const FUIAction Original = *Existing;
    PreviousPlayActions.Add(Command, Original);
    Actions->UnmapAction(Command);
    Actions->MapAction(Command,
        FUIAction(FExecuteAction::CreateLambda(
                      [Original, PreviewAction]
                      {
                          if (GEditor && GEditor->PlayWorld)
                              Original.Execute();
                          else
                              PreviewAction.Execute();
                      }),
            FCanExecuteAction::CreateLambda([Original, PreviewAction]
                { return GEditor && GEditor->PlayWorld ? Original.CanExecute() : PreviewAction.CanExecute(); }),
            FIsActionChecked(),
            FIsActionButtonVisible::CreateLambda(
                [Original, PreviewAction]
                {
                    return (GEditor && GEditor->PlayWorld ? Original.IsVisible() : PreviewAction.IsVisible()) ==
                        EVisibility::Visible;
                })));
}

void RegisterPreviewToolbar()
{
    FToolMenuOwnerScoped Owner(ToolbarOwner);
    auto& Game = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.LevelEditorToolBar"))->FindOrAddSection(TEXT("Game"));
    Game.InsertPosition = FToolMenuInsert(TEXT("Settings"), EToolMenuInsertType::Before);
    const FNativePlayCommands Commands;
    Game.AddEntry(FToolMenuEntry::InitToolBarButton(Commands.RepeatLastPlay, Label(TEXT("Play")),
        Label(TEXT("Play this level in the game. Keep Simulation Changes preserves the edits you choose.")),
        FSlateIcon(FEditorStyle::GetStyleSetName(), TEXT("PlayWorld.PlayInViewport"))));
    Game.AddEntry(FToolMenuEntry::InitToolBarButton(Commands.PausePlaySession));
    Game.AddEntry(FToolMenuEntry::InitToolBarButton(Commands.ResumePlaySession, Label(TEXT("Resume")),
        Label(TEXT("Resume the paused preview game.")),
        FSlateIcon(FEditorStyle::GetStyleSetName(), TEXT("PlayWorld.ResumePlaySession"))));
    Game.AddEntry(FToolMenuEntry::InitToolBarButton(Commands.StopPlaySession, Label(TEXT("Stop")),
        Label(TEXT("Stop testing and return to editing. Unkept changes are discarded."))));
    // The editor's PIE mode picker does not describe an external game preview.
    FUIAction Hidden;
    Hidden.IsActionVisibleDelegate = FIsActionButtonVisible::CreateLambda([] { return false; });
    Game.AddEntry(FToolMenuEntry::InitComboButton(TEXT("PlayCombo"), Hidden, FNewToolMenuChoice()));
    UToolMenus::Get()->RefreshAllWidgets();
}

// Capture the editor through Unreal's own Slate screenshot API, including its menu and toolbar.
bool CaptureEditor(const FString& Path)
{
    auto Window = FGlobalTabmanager::Get()->GetRootWindow();
    if (!Window)
        Window = FSlateApplication::Get().GetActiveTopLevelWindow();
    if (Window && Window->IsWindowMinimized())
    {
        Window->Restore();
        return false; // let Slate render the restored editor before the retry
    }
    TArray<FColor> Pixels;
    FIntVector Size;
    if (!Window || !FSlateApplication::Get().TakeScreenshot(Window.ToSharedRef(), Pixels, Size))
        return false;
    TArray<uint8> Png;
    FImageUtils::CompressImageArray(Size.X, Size.Y, Pixels, Png);
    const bool Saved = FFileHelper::SaveArrayToFile(Png, *Path);
    UE_LOG(LogTemp, Display, TEXT("Kadan screenshot: %s (%dx%d, saved %d)"), *Path, Size.X, Size.Y, Saved);
    return Saved;
}
} // namespace

void Screenshot(const FString& Path)
{
    if (CaptureEditor(Path))
        return;
    const auto Tries = MakeShared<int32>(0);
    FTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateLambda(
            [Path, Tries](float)
            {
                if (CaptureEditor(Path))
                    return false;
                if (++*Tries < 3)
                    return true;
                UE_LOG(LogTemp, Error, TEXT("Kadan screenshot failed: no rendered editor window"));
                return false;
            }),
        0.5f);
}
} // namespace P4GameConnection

using namespace P4GameConnection;

namespace
{
FAutoConsoleCommand OpenCommand(TEXT("Kadan.GameConnection"),
#if P4_KADAN_TEST_SEAMS
    TEXT(
        "Opens Kadan's Game connection panel. Add connect (or sync) to connect, play to preview, and selftest to try it once."),
#else
    TEXT("Opens Kadan's Game connection panel. Add connect (or sync) to connect, and play to preview."),
#endif
    FConsoleCommandWithArgsDelegate::CreateLambda(
        [](const TArray<FString>& Args)
        {
            if (!Registered)
                return;
            P4OpenGameConnection();
            if (Args.Contains(TEXT("connect")) || Args.Contains(TEXT("sync")))
                Session->SetSync(true);
#if P4_KADAN_TEST_SEAMS
            if (Args.Contains(TEXT("play")) || Args.Contains(TEXT("selftest")))
                Session->TogglePlay();
#else
            if (Args.Contains(TEXT("play")))
                Session->TogglePlay();
#endif
            if (Args.Contains(TEXT("stop")))
                Session->Stop();
            if (Args.Contains(TEXT("pause")))
                Session->Pause(true);
            if (Args.Contains(TEXT("resume")))
                Session->Pause(false);
            if (Args.Contains(TEXT("keep")) && Session->Mirror)
                Session->Mirror->KeepSimulationChanges();
#if P4_KADAN_TEST_SEAMS
            if (Args.Contains(TEXT("selftest")))
                StartSelfTest();
#endif
        }));

FAutoConsoleCommand ScreenshotCommand(TEXT("Kadan.Screenshot"),
    TEXT("Save a PNG of the editor using Unreal's native Slate screenshot function."),
    FConsoleCommandWithArgsDelegate::CreateLambda(
        [](const TArray<FString>& Args)
        {
            if (!Args.Num() || !FSlateApplication::IsInitialized())
                return;
            const FString CommandLine = FString::Join(Args, TEXT(" "));
            const TCHAR* Cursor = *CommandLine;
            FString Path;
            if (!FParse::Token(Cursor, Path, false) || Path.IsEmpty())
                return;
            FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
                                                   [Path](float)
                                                   {
                                                       Screenshot(Path);
                                                       return false;
                                                   }),
                1.f);
        }));

TOptional<FUIAction> PreviousKeep;
FDelegateHandle KeepMenuHandle;

void KeepSimulationChanges()
{
    if (Session && Session->Mirror)
    {
        const int32 Count = Session->Mirror->KeepSimulationChanges();
        Toast(FString::Printf(TEXT("Kept %d changes. Save the map to keep them on disk."), Count));
    }
    else if (PreviousKeep.IsSet())
        PreviousKeep->Execute();
}

bool CanKeepSimulationChanges()
{
    if (Session && Session->Mirror)
        return Session->Mirror->CanKeepSimulationChanges();
    return PreviousKeep.IsSet() && PreviousKeep->CanExecute();
}
} // namespace

void P4RegisterGameConnection()
{
    if (Registered)
        return;
    Registered = true;
    Session = MakeUnique<FGameSession>();
    RegisterStyle();
    auto& LevelEditor = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
    // Use the shared binding registry. FPlayWorldCommands is not DLL-exported, so its
    // inline TCommands::Get would access a different template singleton in this plugin.
    PlayInitTicker = FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
        [](float)
        {
            if (!Registered)
                return false;
            if (!FPlayWorldCommands::GlobalPlayWorldActions)
                return true;
            const FNativePlayCommands PlayCommands;
            if (!PlayCommands.Ready())
                return true;
            BindPlay(PlayCommands.RepeatLastPlay,
                FUIAction(FExecuteAction::CreateLambda(
                              []
                              {
                                  if (Session)
                                      Session->TogglePlay();
                              }),
                    FCanExecuteAction::CreateLambda([] { return Session && !Session->Active(); }), FIsActionChecked(),
                    FIsActionButtonVisible::CreateLambda([] { return Session && !Session->Active(); })));
            BindPlay(PlayCommands.PausePlaySession,
                FUIAction(FExecuteAction::CreateLambda(
                              []
                              {
                                  if (Session)
                                      Session->Pause(true);
                              }),
                    FCanExecuteAction::CreateLambda([] { return Session && Session->Preview == EPreview::Playing; }),
                    FIsActionChecked(),
                    FIsActionButtonVisible::CreateLambda(
                        []
                        {
                            return Session && Session->Active() && Session->Preview != EPreview::Paused &&
                                Session->Preview != EPreview::Resuming;
                        })));
            BindPlay(PlayCommands.ResumePlaySession,
                FUIAction(FExecuteAction::CreateLambda(
                              []
                              {
                                  if (Session)
                                      Session->Pause(false);
                              }),
                    FCanExecuteAction::CreateLambda([] { return Session && Session->Preview == EPreview::Paused; }),
                    FIsActionChecked(),
                    FIsActionButtonVisible::CreateLambda(
                        []
                        {
                            return Session &&
                                (Session->Preview == EPreview::Paused || Session->Preview == EPreview::Resuming);
                        })));
            BindPlay(PlayCommands.StopPlaySession,
                FUIAction(FExecuteAction::CreateLambda(
                              []
                              {
                                  if (Session)
                                      Session->Stop();
                              }),
                    FCanExecuteAction::CreateLambda([] { return Session && Session->Active() && !Session->Busy(); }),
                    FIsActionChecked(),
                    FIsActionButtonVisible::CreateLambda([] { return Session && Session->Active(); })));
            ToolbarStartupHandle = UToolMenus::RegisterStartupCallback(
                FSimpleMulticastDelegate::FDelegate::CreateStatic(&RegisterPreviewToolbar));
            return false;
        }));
    const auto Actions = LevelEditor.GetGlobalLevelEditorActions();
    const auto Command = KeepCommand();
    if (const FUIAction* Existing = Actions->GetActionForCommand(Command))
        PreviousKeep = *Existing;
    Actions->UnmapAction(Command);
    Actions->MapAction(Command, FExecuteAction::CreateStatic(&KeepSimulationChanges),
        FCanExecuteAction::CreateStatic(&CanKeepSimulationChanges));
    auto Extender = FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors::CreateLambda(
        [Actions, Command](const TSharedRef<FUICommandList>, const TArray<AActor*>)
        {
            const auto Result = MakeShared<FExtender>();
            if (Session && Session->Mirror)
                Result->AddMenuExtension(TEXT("LevelViewportAttach"), EExtensionHook::Before, Actions,
                    FMenuExtensionDelegate::CreateLambda(
                        [Command](FMenuBuilder& Menu)
                        {
                            Menu.BeginSection(TEXT("KadanSimulation"), Label(TEXT("Simulation")));
                            Menu.AddMenuEntry(Command);
                            Menu.EndSection();
                        }));
            return Result;
        });
    KeepMenuHandle = Extender.GetHandle();
    LevelEditor.GetAllLevelViewportContextMenuExtenders().Add(Extender);
}

void P4UnregisterGameConnection()
{
    if (!Registered)
        return;
    Registered = false;
    if (FModuleManager::Get().IsModuleLoaded("LevelEditor"))
    {
        auto& LevelEditor = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
        LevelEditor.GetAllLevelViewportContextMenuExtenders().RemoveAll(
            [](const auto& Extender) { return Extender.GetHandle() == KeepMenuHandle; });
        const auto Actions = LevelEditor.GetGlobalLevelEditorActions();
        const auto Command = KeepCommand();
        Actions->UnmapAction(Command);
        if (PreviousKeep.IsSet())
            Actions->MapAction(Command, PreviousKeep.GetValue());
    }
    PreviousKeep.Reset();
    FTicker::GetCoreTicker().RemoveTicker(PlayInitTicker);
    UToolMenus::UnRegisterStartupCallback(ToolbarStartupHandle);
    UToolMenus::UnregisterOwner(ToolbarOwner);
    if (FPlayWorldCommands::GlobalPlayWorldActions)
        for (const auto& Original : PreviousPlayActions)
        {
            FPlayWorldCommands::GlobalPlayWorldActions->UnmapAction(Original.Key);
            FPlayWorldCommands::GlobalPlayWorldActions->MapAction(Original.Key, Original.Value);
        }
    PreviousPlayActions.Reset();
    if (const TSharedPtr<SDockTab> Tab = LiveTab.Pin())
        Tab->RequestCloseTab();
    Session.Reset();
    UnregisterStyle();
}

#undef LOCTEXT_NAMESPACE

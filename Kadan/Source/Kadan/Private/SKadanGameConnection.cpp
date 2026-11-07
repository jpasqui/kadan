// The Game connection panel: its lamp, its toolbar, events and console, and where it opens.

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

namespace P4GameConnection
{
TWeakPtr<SDockTab> LiveTab;

namespace
{
// The connection's lamp: the editor's own green and grey status lamps, the grey one tinted amber for waiting.
const FName StyleName(TEXT("KadanGameStyle"));
TSharedPtr<FSlateStyleSet> Style;

FName LampName(EState State)
{
    switch (State)
    {
        case EState::Connected:
            return TEXT("Kadan.Game.Connected");
        case EState::Waiting:
            return TEXT("Kadan.Game.Waiting");
        default:
            return TEXT("Kadan.Game.Off");
    }
}

EState CurrentState()
{
    return Session ? Session->State() : EState::Off;
}

FSlateIcon Lamp()
{
    return FSlateIcon(StyleName, LampName(CurrentState()));
}

class SKadanGameConnection : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SKadanGameConnection) {}
    SLATE_END_ARGS()

    void Construct(const FArguments&)
    {
        FGameSession& S = *Session;
        S.LinesChanged = [this] { Refresh(); };
        ChildSlot[SNew(SVerticalBox) +
            SVerticalBox::Slot().AutoHeight()
                [SNew(SBorder).BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder")).Padding(2)[Toolbar()]] +
            SVerticalBox::Slot().AutoHeight().Padding(6, 4)[SNew(SHorizontalBox) +
                SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 2, 0)[Tab(TEXT("Events"), 0)] +
                SHorizontalBox::Slot().AutoWidth()[Tab(TEXT("Console"), 1)] + SHorizontalBox::Slot().FillWidth(1) +
                SHorizontalBox::Slot().AutoWidth()[SNew(SButton)
                        .ButtonStyle(FEditorStyle::Get(), "FlatButton")
                        .ToolTipText(Label(TEXT("Clear")))
                        .OnClicked_Lambda(
                            [this]
                            {
                                (Page == 0 ? Session->EventLines : Session->ConsoleLines).Reset();
                                Refresh();
                                return FReply::Handled();
                            })[SNew(STextBlock)
                                .Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
                                .Text(Label(TEXT("\xf1f8")))]]] +
            SVerticalBox::Slot()
                .FillHeight(1)
                .Padding(6, 0, 6, 6)
                    [SNew(SBorder)
                            .BorderImage(FEditorStyle::GetBrush("ToolPanel.DarkGroupBorder"))
                            .Padding(4)
                                [SNew(SWidgetSwitcher).WidgetIndex_Lambda([this] { return Page; }) +
                                    SWidgetSwitcher::Slot()[Lines(EventList, &S.EventLines)] +
                                    SWidgetSwitcher::Slot()
                                        [SNew(SVerticalBox) +
                                            SVerticalBox::Slot().FillHeight(1)[Lines(ConsoleList, &S.ConsoleLines)] +
                                            SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
                                                [SAssignNew(ConsoleBox, SEditableTextBox)
                                                        .HintText(Label(TEXT("Lua, run in the game")))
                                                        .Font(FEditorStyle::Get().GetFontStyle("Log.Normal"))
                                                        .OnTextCommitted_Lambda(
                                                            [this](const FText& Text, ETextCommit::Type How)
                                                            {
                                                                if (How != ETextCommit::OnEnter)
                                                                    return;
                                                                ConsoleBox->SetText(FText());
                                                                Session->RunConsole(Text.ToString());
                                                            })]]]]];
    }

    virtual ~SKadanGameConnection() override
    {
        if (Session)
            Session->LinesChanged = nullptr;
    }

private:
    int32 Page = 0;
    TSharedPtr<SListView<TSharedPtr<FString>>> EventList, ConsoleList;
    TSharedPtr<SEditableTextBox> ConsoleBox;

    void Refresh()
    {
        if (EventList)
            EventList->RequestListRefresh();
        if (ConsoleList)
            ConsoleList->RequestListRefresh();
        const auto& Items = Page == 0 ? Session->EventLines : Session->ConsoleLines;
        const auto& List = Page == 0 ? EventList : ConsoleList;
        if (Items.Num() && List)
            List->RequestScrollIntoView(Items.Last());
    }

    TSharedRef<SWidget> Toolbar()
    {
        FGameSession* S = Session.Get();
        FToolBarBuilder Bar(nullptr, FMultiBoxCustomization::None);
        Bar.AddComboButton(FUIAction(), FOnGetContent::CreateSP(this, &SKadanGameConnection::SettingsMenu),
            Label(TEXT("Settings")), Label(TEXT("Port, log file and events")),
            FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.GameSettings"), false);
        Bar.AddSeparator();
        Bar.AddWidget(SNew(SBox)
                .VAlign(VAlign_Center)
                .Padding(FMargin(8, 0))
                .MinDesiredHeight(40)[SNew(STextBlock).Text_Lambda([S] { return Label(S->Status()); })]);
        return Bar.MakeWidget();
    }

    TSharedRef<SWidget> SettingsMenu()
    {
        FGameSession* S = Session.Get();
        FMenuBuilder Menu(true, nullptr);
        Menu.AddWidget(SNew(SBox).WidthOverride(90)[SNew(SEditableTextBox)
                               .Text_Lambda([S] { return Label(S->PortText); })
                               .OnTextCommitted_Lambda(
                                   [S](const FText& Text, ETextCommit::Type)
                                   {
                                       S->PortText = Text.ToString();
                                       S->Save(TEXT("GamePort"), S->PortText);
                                   })],
            Label(TEXT("Port")));
        Menu.AddWidget(SNew(SBox).WidthOverride(420)[SNew(SEditableTextBox)
                               .Text_Lambda([S] { return Label(S->LogPath); })
                               .OnTextCommitted_Lambda(
                                   [S](const FText& Text, ETextCommit::Type) { S->SetLogPath(Text.ToString()); })],
            Label(TEXT("Log file")));
        Menu.AddMenuEntry(Label(TEXT("Follow the log file")), FText(), FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([S] { S->bTailLog = !S->bTailLog; }), FCanExecuteAction(),
                FIsActionChecked::CreateLambda([S] { return S->bTailLog; })),
            NAME_None, EUserInterfaceActionType::ToggleButton);
        Menu.BeginSection(NAME_None, Label(TEXT("Events")));
        for (const TCHAR* Name : EventNames)
        {
            const FString Event = Name;
            Menu.AddMenuEntry(Label(Event), FText(), FSlateIcon(),
                FUIAction(FExecuteAction::CreateLambda([S, Event] { S->SetWatch(Event, !S->Watched.FindRef(Event)); }),
                    FCanExecuteAction(),
                    FIsActionChecked::CreateLambda([S, Event] { return S->Watched.FindRef(Event); })),
                NAME_None, EUserInterfaceActionType::ToggleButton);
        }
        Menu.EndSection();
        return Menu.MakeWidget();
    }

    TSharedRef<SWidget> Tab(const FString& Text, int32 Index)
    {
        return SNew(SCheckBox)
            .Style(FEditorStyle::Get(), "ToggleButtonCheckbox")
            .IsChecked_Lambda(
                [this, Index] { return Page == Index ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda(
                [this, Index](ECheckBoxState)
                {
                    Page = Index;
                    Refresh();
                })[SNew(SBox).Padding(FMargin(10, 2))[SNew(STextBlock).Text(Label(Text))]];
    }

    TSharedRef<SWidget> Lines(TSharedPtr<SListView<TSharedPtr<FString>>>& List, TArray<TSharedPtr<FString>>* Items)
    {
        return SAssignNew(List, SListView<TSharedPtr<FString>>)
            .ListItemsSource(Items)
            .SelectionMode(ESelectionMode::Multi)
            .OnGenerateRow_Lambda(
                [](TSharedPtr<FString> Line, const TSharedRef<STableViewBase>& Owner)
                {
                    return SNew(STableRow<TSharedPtr<FString>>,
                        Owner)[SNew(STextBlock).Font(FEditorStyle::Get().GetFontStyle("Log.Normal")).Text(Label(*Line))];
                });
    }
};
} // namespace

void RegisterStyle()
{
    Style = MakeShared<FSlateStyleSet>(StyleName);
    const FString Icons = FPaths::EngineContentDir() / TEXT("Editor/Slate/Icons/");
    struct FLamp
    {
        EState State;
        const TCHAR* File;
        FLinearColor Tint;
    };
    const FLamp Lamps[] = {{EState::Off, TEXT("icon_status_grey_16x.png"), FLinearColor::White},
        {EState::Waiting, TEXT("icon_status_grey_16x.png"), FLinearColor(2.6f, 1.6f, 0.25f)},
        {EState::Connected, TEXT("icon_status_green_16x.png"), FLinearColor::White}};
    for (const FLamp& Lamp : Lamps)
    {
        const FString Name = LampName(Lamp.State).ToString();
        Style->Set(*Name, new FSlateImageBrush(Icons + Lamp.File, FVector2D(24, 24), Lamp.Tint));
        Style->Set(*(Name + TEXT(".Small")), new FSlateImageBrush(Icons + Lamp.File, FVector2D(16, 16), Lamp.Tint));
    }
    FSlateStyleRegistry::RegisterSlateStyle(*Style);
}

void UnregisterStyle()
{
    if (!Style)
        return;
    FSlateStyleRegistry::UnRegisterSlateStyle(*Style);
    Style.Reset();
}
} // namespace P4GameConnection

using namespace P4GameConnection;

void P4OpenGameConnection()
{
    if (const TSharedPtr<SDockTab> Open = LiveTab.Pin())
    {
        Open->ActivateInParent(ETabActivationCause::SetDirectly);
        return;
    }
    const TSharedRef<SDockTab> Tab =
        SNew(SDockTab).TabRole(ETabRole::PanelTab).Label(Label(TEXT("Game connection")))[SNew(SKadanGameConnection)];
    LiveTab = Tab;
    // Beside the Content Browser at the bottom of the Level Editor, like the Output Log.
    const TSharedPtr<FTabManager> Tabs =
        FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor").GetLevelEditorTabManager();
    if (Tabs)
        Tabs->InsertNewDocumentTab("ContentBrowserTab1", FTabManager::ESearchPreference::PreferLiveTab, Tab);
    else
        FGlobalTabmanager::Get()->InsertNewDocumentTab(
            "ContentBrowserTab1", FTabManager::ESearchPreference::PreferLiveTab, Tab);
}

TSharedRef<SWidget> P4GameConnectionMenuHeader()
{
    return SNew(SHorizontalBox) +
        SHorizontalBox::Slot().AutoWidth().VAlign(
            VAlign_Center)[SNew(STextBlock).TextStyle(FEditorStyle::Get(), "Menu.Label").Text(Label(TEXT("Kadan")))] +
        SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(5, 0, 0, 0)[SNew(SButton)
                    .ButtonStyle(FEditorStyle::Get(), "NoBorder")
                    .ContentPadding(0)
                    .IsEnabled_Lambda([] { return Session && !Session->Active(); })
                    .ToolTipText_Lambda(
                        []
                        {
                            if (!Session)
                                return Label(TEXT("Disconnected"));
                            return Label(Session->Status() +
                                (Session->Active() ? TEXT(". Stop Play before disconnecting.")
                                        : Session->bSync
                                        ? TEXT(". Click to disconnect. Play connects automatically.")
                                        : TEXT(
                                              ". Click to connect for Events and Console. Play connects automatically.")));
                        })
                    .OnClicked_Lambda(
                        []
                        {
                            if (Session)
                                Session->SetSync(!Session->bSync);
                            return FReply::Handled();
                        })[SNew(SBox).WidthOverride(12).HeightOverride(12)[SNew(SImage).Image_Lambda([]
                        { return Lamp().GetSmallIcon(); })]]];
}

#include "P4KadanPanelModsPage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Input/SComboButton.h"
#include "DesktopPlatformModule.h"
#include "P4KadanPanelImportPage.h"

namespace P4KadanEditorPrivate
{

FKadanModsPage::FKadanModsPage(SKadanPanel& InHost) : Host(InHost)
{}

void FKadanModsPage::Prepare()
{
    ExportReceipt->ProjectDirectory = FPaths::ProjectDir();
    SuggestedEdenOutput = P4KadanFindEdenOutput(FPlatformMisc::GetEnvironmentVariable(TEXT("APPDATA")));
    // With Eden installed, its mod folder is where a mod goes until the creator chooses another.
    if (Host.ExportFolder.IsEmpty() && !SuggestedEdenOutput.IsEmpty())
    {
        Host.ExportFolder = SuggestedEdenOutput;
        Host.WriteSettings();
    }
}

void FKadanModsPage::Tick()
{
    // What ships is worked out by itself once the content stops changing.
    if (!ReviewWanted || !ActiveMod || !SelectionRows.Num() || FPlatformTime::Seconds() - ChangedAt < 1.5)
        return;
    ReviewWanted = false;
    TGuardValue<FString> KeepMessage(Host.Message, Host.Message);
    TGuardValue<bool> KeepError(Host.NoticeError, Host.NoticeError);
    ReviewExport(false);
}

TSharedPtr<FP4ModDescriptor> FKadanModsPage::CurrentMod() const
{
    return ActiveMod;
}

bool FKadanModsPage::ReviewOutOfDate() const
{
    return HasReview && !ReviewCurrent;
}

const FString& FKadanModsPage::Details() const
{
    return ExportDetails;
}

void FKadanModsPage::ScrollToStart()
{}

TSharedRef<SWidget> FKadanModsPage::TextField(const FString& Name, FString& Value)
{
    return Host.TextField(Name, Value, [this](FString& Changed) { FieldChanged(Changed); });
}

TSharedRef<SWidget> FKadanModsPage::PathField(const FString& Name, FString& Value, bool Folder)
{
    return Host.PathField(Name, Value, Folder, [this](FString& Changed) { FieldChanged(Changed); });
}

TSharedRef<SWidget> FKadanModsPage::Build()
{
    return SNew(SVerticalBox)
               .Tag(TEXT("KadanPages"))
               .IsEnabled_Lambda([this]() { return !Host.Busy; })
               .Visibility_Lambda([this]() { return Host.Page == 1 ? EVisibility::Visible : EVisibility::Collapsed; }) +
        SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)[ModRow()] +
        SVerticalBox::Slot().AutoHeight()[SNew(STextBlock)
                .AutoWrapText(true)
                .Text(Label(TEXT("Click New mod to start one.")))
                .Visibility_Lambda([this]() { return ActiveMod ? EVisibility::Collapsed : EVisibility::Visible; })] +
        SVerticalBox::Slot().FillHeight(1)[SNew(SVerticalBox)
                .Visibility_Lambda([this]() { return ActiveMod ? EVisibility::Visible : EVisibility::Collapsed; }) +
            SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox) +
                SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Host.Heading(TEXT("Content"))] +
                SHorizontalBox::Slot().AutoWidth()[Host.Action(TEXT("Add selected"), [this]() { AddSelected(); }, false,
                    nullptr, TEXT("Add the maps, assets or folders selected in the Content Browser. What they need comes "
                                  "along by itself, but the levels you changed don't: add each one, a map's sublevels too."))]] +
            SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 8)[RootList()] +
            SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox) +
                SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Host.Heading(TEXT("What ships"))] +
                SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock)
                        .Tag(TEXT("KadanExportReview"))
                        .ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f))
                        .Text_Lambda([this]() { return Label(ShippingSummary()); })]] +
            SVerticalBox::Slot().FillHeight(1).Padding(0, 4, 0, 8)[SAssignNew(ShippingList, SListView<TSharedPtr<FP4ExportSelectionRow>>)
                    .Tag(TEXT("KadanShippingList"))
                    .ListItemsSource(&FilteredShippingRows)
                    .SelectionMode(ESelectionMode::None)
                    .OnGenerateRow_Lambda([this](TSharedPtr<FP4ExportSelectionRow> Row, const TSharedRef<STableViewBase>& Owner)
                        { return ShippingRow(Row, Owner); })] +
            SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)[ExportTarget()] +
            SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)[SNew(SCheckBox)
                    .Tag(TEXT("KadanNativeCook"))
                    .ToolTipText(FText::FromString(TEXT(
                        "Cook the mod as game files that load without Tsuchi: copy its romfs folder over the game's. "
                        "Edited levels and tables ship whole, so a mod that edits the same level or table replaces this "
                        "one. New treasure and creature kinds and sounds need Tsuchi.")))
                    .IsChecked_Lambda([this]() { return Native ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                    .OnCheckStateChanged_Lambda(
                        [this](ECheckBoxState State)
                        {
                            Native = State == ECheckBoxState::Checked;
                            SettingsChanged();
                        })[SNew(STextBlock).Text(Label(TEXT("Cook for the game without Tsuchi")))]] +
            SVerticalBox::Slot().AutoHeight()[SNew(SButton)
                    .Tag(TEXT("KadanExport"))
                    .ButtonStyle(FEditorStyle::Get(), "Button")
                    .ButtonColorAndOpacity(PrimaryButtonTint)
                    .TextStyle(FEditorStyle::Get(), "NormalText")
                    .ForegroundColor(FLinearColor::White)
                    .HAlign(HAlign_Center)
                    .ContentPadding(FMargin(12, 5))
                    .Text(Label(TEXT("Export mod")))
                    .IsEnabled_Lambda([this]() { return ActiveMod.IsValid() && SelectionRows.Num() && !Host.ExportFolder.IsEmpty(); })
                    .ToolTipText_Lambda([this]() { return Label(ExportDisabledReason()); })
                    .OnClicked_Lambda(
                        [this]()
                        {
                            Host.QueueOperation(TEXT("Kadan is exporting the mod."),
                                [this]() { P4KadanProgressOperation([this]() { ExportMod(); }); }, true);
                            return FReply::Handled();
                        })] +
            SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[SNew(SHorizontalBox)
                    .Visibility_Lambda([this]() { return LastExportDirectory.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; }) +
                SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock)
                        .Tag(TEXT("KadanLastExport"))
                        .ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f))
                        .Text_Lambda([this]() { return Label(LastExportLine); })] +
                SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)[SNew(SHyperlink)
                        .Text(Label(TEXT("Open folder")))
                        .OnNavigate_Lambda([this]() { FPlatformProcess::ExploreFolder(*LastExportDirectory); })]]];
}

TSharedRef<SWidget> FKadanModsPage::ModRow()
{
    return SNew(SHorizontalBox) +
        SHorizontalBox::Slot().FillWidth(1)[SAssignNew(ModPicker, SComboBox<TSharedPtr<FP4ModDescriptor>>)
                .Tag(TEXT("KadanModPicker"))
                .OptionsSource(&Mods)
                .InitiallySelectedItem(ActiveMod)
                .OnGenerateWidget_Lambda(
                    [](TSharedPtr<FP4ModDescriptor> Mod)
                    { return SNew(STextBlock).Text_Lambda([Mod]() { return Label(Mod->Name + TEXT("  ") + Mod->Version); }); })
                .OnSelectionChanged_Lambda([this](TSharedPtr<FP4ModDescriptor> Mod, ESelectInfo::Type) { RequestModChange(Mod); })
                    [SNew(STextBlock).Text_Lambda([this]()
                        { return Label(ActiveMod ? ActiveMod->Name + TEXT("  ") + ActiveMod->Version : TEXT("No mod")); })]] +
        SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0)[SNew(SBox)
                .Visibility_Lambda([this]() { return ActiveMod ? EVisibility::Visible : EVisibility::Collapsed; })
                    [Host.Action(TEXT("Edit"), [this]() { EditIdentity(false); }, false, nullptr,
                        TEXT("Change the mod's name and version."))]] +
        SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0)[SNew(SBox)
                .Visibility_Lambda([this]() { return ActiveMod ? EVisibility::Visible : EVisibility::Collapsed; })
                    [Host.Action(TEXT("Content folder"), [this]() { OpenContentFolder(); }, false, nullptr,
                        TEXT("Show the mod's own folder in the Content Browser, making it first if needed."))]] +
        SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0)[Host.Action(TEXT("New mod"), [this]() { EditIdentity(true); })];
}

void FKadanModsPage::EditIdentity(bool New)
{
    if (const auto Open = IdentityWindow.Pin())
    {
        Open->BringToFront(true);
        return;
    }
    const TSharedPtr<FP4ModDescriptor> Before = ActiveMod;
    if (New)
        StartModDraft();
    const TSharedRef<SWindow> Window = SNew(SWindow)
                                           .Title(Label(New ? TEXT("New mod") : TEXT("Edit mod")))
                                           .ClientSize(FVector2D(460, 210))
                                           .SupportsMaximize(false)
                                           .SupportsMinimize(false);
    const TWeakPtr<SWindow> Weak = Window;
    Window->SetContent(SNew(SBorder)
            .BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
            .Padding(12)[SNew(SVerticalBox) +
                SVerticalBox::Slot().AutoHeight().Padding(0, 3)[TextField(TEXT("Name"), ModName)] +
                SVerticalBox::Slot().AutoHeight().Padding(0, 3)[SNew(SBox)
                        .Tag(TEXT("KadanModIdLock"))
                        .IsEnabled(New)
                        .ToolTipText(Label(TEXT("The ID names the mod's folder and files. It can't change after the mod is "
                                                "saved, so an update replaces the earlier export.")))[TextField(TEXT("ID"), ModId)]] +
                SVerticalBox::Slot().AutoHeight().Padding(0, 3)[TextField(TEXT("Version"), ModVersion)] +
                SVerticalBox::Slot().FillHeight(1) +
                SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)[SNew(SHorizontalBox) +
                    SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Host.Action(TEXT("Save"),
                        [this, Weak]()
                        {
                            if (SaveModIdentity().Saved)
                                if (const auto Open = Weak.Pin())
                                    Open->RequestDestroyWindow();
                        },
                        true)] +
                    SHorizontalBox::Slot().AutoWidth()[Host.Action(TEXT("Cancel"),
                        [Weak]()
                        {
                            if (const auto Open = Weak.Pin())
                                Open->RequestDestroyWindow();
                        })]]]);
    Window->SetOnWindowClosed(FOnWindowClosed::CreateLambda(
        [this, Before, New](const TSharedRef<SWindow>&)
        {
            // Closed without saving: the fields go back to the saved mod, and a new draft goes back to the mod before it.
            if (HasUnsavedModIdentity())
            {
                if (New)
                    ApplyModSelection(Before);
                else if (ActiveMod)
                {
                    ModId = ActiveMod->Id;
                    ModName = ActiveMod->Name;
                    ModVersion = ActiveMod->Version;
                }
                RestoreModPicker();
            }
        }));
    IdentityWindow = Window;
    FSlateApplication::Get().AddWindowAsNativeChild(Window, FSlateApplication::Get().FindWidgetWindow(Host.Self()).ToSharedRef());
}

void FKadanModsPage::AddSelected()
{
    IContentBrowserSingleton& Browser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();
    TArray<FAssetData> Assets;
    Browser.GetSelectedAssets(Assets);
    TArray<FString> Folders;
    Browser.GetSelectedFolders(Folders);
    // The folder tree and the asset view have selections of their own; the tree's counts when nothing else is.
    if (!Assets.Num() && !Folders.Num())
        Browser.GetSelectedPathViewFolders(Folders);
    if (!Assets.Num() && !Folders.Num())
    {
        Host.Message = TEXT("Select maps, assets or folders in the Content Browser first.");
        Host.NoticeError = true;
        return;
    }
    for (const auto& Asset : Assets)
        AddRoot(EP4ExportRootKind::Package, Asset.PackageName.ToString());
    for (const FString& Folder : Folders)
        AddRoot(EP4ExportRootKind::Folder, Folder);
    SaveRoots();
}

TSharedRef<SWidget> FKadanModsPage::RootList()
{
    return SNew(SBox).MaxDesiredHeight(130)[SAssignNew(SelectionList, SListView<TSharedPtr<FP4ExportRoot>>)
            .Tag(TEXT("KadanSelectionRoots"))
            .ListItemsSource(&SelectionRows)
            .SelectionMode(ESelectionMode::None)
            .OnGenerateRow_Lambda(
                [this](TSharedPtr<FP4ExportRoot> Root, const TSharedRef<STableViewBase>& Owner)
                {
                    const bool Folder = Root->Kind == EP4ExportRootKind::Folder;
                    return SNew(STableRow<TSharedPtr<FP4ExportRoot>>, Owner)
                        .ToolTipText(Label(Root->Path))[SNew(SHorizontalBox) +
                            SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(4, 3)[SNew(STextBlock)
                                    .Text(Label((Folder ? Root->Path : FPackageName::GetShortName(Root->Path)) +
                                        (Folder ? TEXT("  · folder") : FString())))] +
                            SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0)[SNew(SHyperlink)
                                    .ToolTipText(Label(TEXT("Take this out of the mod")))
                                    .Text(Label(TEXT("Remove")))
                                    .OnNavigate_Lambda(
                                        [this, Root]()
                                        {
                                            SelectionRows.Remove(Root);
                                            SaveRoots();
                                        })]];
                })];
}

TSharedRef<SWidget> FKadanModsPage::ExportTarget()
{
    return SNew(SHorizontalBox) +
        SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[SNew(STextBlock).Text(Label(TEXT("Export to")))] +
        SHorizontalBox::Slot().FillWidth(1)[SNew(SComboButton)
                .Tag(TEXT("KadanExportTarget"))
                .ToolTipText_Lambda([this]() { return Label(Host.ExportFolder); })
                .ButtonContent()[SNew(STextBlock).Text_Lambda(
                    [this]()
                    {
                        return Label(Host.ExportFolder.IsEmpty()       ? TEXT("Choose a folder")
                                : Host.ExportFolder == SuggestedEdenOutput ? TEXT("Eden's mod folder")
                                                                           : Host.ExportFolder);
                    })]
                .OnGetMenuContent_Lambda(
                    [this]()
                    {
                        FMenuBuilder Menu(true, nullptr);
                        if (!SuggestedEdenOutput.IsEmpty())
                            Menu.AddMenuEntry(Label(TEXT("Eden's mod folder")), Label(SuggestedEdenOutput), FSlateIcon(),
                                FUIAction(FExecuteAction::CreateLambda(
                                    [this]()
                                    {
                                        Host.ExportFolder = SuggestedEdenOutput;
                                        FieldChanged(Host.ExportFolder);
                                        Host.WriteSettings();
                                    })));
                        Menu.AddMenuEntry(Label(TEXT("Choose a folder…")), FText(), FSlateIcon(),
                            FUIAction(FExecuteAction::CreateLambda([this]() { ChooseExportFolder(); })));
                        return Menu.MakeWidget();
                    })];
}

void FKadanModsPage::ChooseExportFolder()
{
    IDesktopPlatform* Desktop = P4KadanPathPicker();
    if (!Desktop)
    {
        Host.Message = TEXT("The folder picker is unavailable.");
        Host.NoticeError = true;
        return;
    }
    FString Picked;
    if (!Desktop->OpenDirectoryDialog(FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
            TEXT("Export to"), Host.ExportFolder, Picked))
        return;
    Host.ExportFolder = Picked;
    FieldChanged(Host.ExportFolder);
    Host.WriteSettings();
}

FString FKadanModsPage::ShippingSummary() const
{
    if (!SelectionRows.Num())
        return TEXT("Add content to see what ships");
    if (!HasReview || !ReviewCurrent)
        return TEXT("Checking…");
    // The rows the list shows: what ships and what can't, and what the game already has.
    int32 Ships = 0, Game = 0, Fix = 0;
    for (const auto& Row : ShippingRows)
        if (Row->Disposition == EP4ExportDisposition::CreatorPackage || Row->Disposition == EP4ExportDisposition::GameEdit)
            ++Ships;
        else if (Row->Disposition == EP4ExportDisposition::UnchangedGame)
            ++Game;
        else
            ++Fix;
    FString Line = FString::Printf(TEXT("%d ship · %d from the game"), Ships, Game);
    if (Fix)
        Line += FString::Printf(TEXT(" · %d to fix"), Fix);
    else if (CreatorReview.Blockers.Num())
        Line += TEXT(" · see the Output Log");
    return Line;
}

TSharedRef<ITableRow> FKadanModsPage::ShippingRow(TSharedPtr<FP4ExportSelectionRow> Row, const TSharedRef<STableViewBase>& Owner)
{
    const bool Ships = Row->Disposition == EP4ExportDisposition::CreatorPackage || Row->Disposition == EP4ExportDisposition::GameEdit;
    const FString Name = FPackageName::GetShortName(Row->Package);
    if (Ships)
        return SNew(STableRow<TSharedPtr<FP4ExportSelectionRow>>, Owner)
            .ToolTipText(Label(Row->Package + TEXT("\n") + Row->Reason))[SNew(SHorizontalBox) +
                SHorizontalBox::Slot().FillWidth(1).Padding(4, 3)[SNew(STextBlock).Text(Label(Name))] +
                SHorizontalBox::Slot().AutoWidth().Padding(8, 3)[SNew(STextBlock)
                        .ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f))
                        .Text(Label(Row->Disposition == EP4ExportDisposition::GameEdit ? TEXT("your changes") : TEXT("yours")))]];
    const auto Issue = P4KadanExportIssue(*Row);
    const FString Map = KnownPersistentMap(*Row);
    const FString Root = Issue.SelectingRoot;
    return SNew(STableRow<TSharedPtr<FP4ExportSelectionRow>>, Owner)
        .ToolTipText(Label(Row->Package + TEXT("\n") + Issue.Detail))[SNew(SBorder)
            .BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
            .BorderBackgroundColor(FLinearColor(0.35f, 0.10f, 0.10f))
            .Padding(6)[SNew(SVerticalBox) +
                SVerticalBox::Slot().AutoHeight()[SNew(STextBlock)
                        .AutoWrapText(true)
                        .Text(Label(Name + TEXT(": ") + (Row->Reason.IsEmpty() ? Issue.Cause : Row->Reason)))] +
                SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[SNew(SWrapBox).UseAllottedWidth(true) +
                    SWrapBox::Slot().Padding(0, 0, 6, 0)[Host.Action(TEXT("Locate"), [this, Row]() { LocatePackage(Row->Package); })] +
                    SWrapBox::Slot().Padding(0, 0, 6, 0)[SNew(SBox)
                            .Visibility(Map.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)[Host.Action(TEXT("Open map"),
                                [Map]()
                                {
                                    if (FEditorFileUtils::SaveDirtyPackages(true, true, true))
                                        FEditorFileUtils::LoadMap(FPackageName::LongPackageNameToFilename(Map, FPackageName::GetMapPackageExtension()));
                                })]] +
                    SWrapBox::Slot()[SNew(SBox)
                            .Visibility(Root.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)[Host.Action(
                                TEXT("Remove ") + FPackageName::GetShortName(Root),
                                [this, Root]()
                                {
                                    SelectionRows.RemoveAll([&](const TSharedPtr<FP4ExportRoot>& Value) { return Value->Path == Root; });
                                    SaveRoots();
                                })]]]]];
}
} // namespace P4KadanEditorPrivate

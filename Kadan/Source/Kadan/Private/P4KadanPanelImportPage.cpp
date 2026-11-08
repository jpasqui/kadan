#include "P4KadanPanelImportPage.h"
#include "P4GameNames.h"
#include "P4KadanPathPicker.h"
#include "DesktopPlatformModule.h"

namespace P4KadanEditorPrivate
{

FKadanImportPage::FKadanImportPage(SKadanPanel& InHost) : Host(InHost)
{}

void FKadanImportPage::Prepare()
{
    RefreshProjectSetup();
    Kinds.Add(MakeShared<FString>(TEXT("All kinds")));
    SelectedKind = Kinds[0];
}

const FString& FKadanImportPage::Details() const
{
    return ImportDetails;
}

void FKadanImportPage::ClearDetails()
{
    ImportDetails.Empty();
}

void FKadanImportPage::ShowSearch(const FString& Value)
{
    Search = Value;
    Filter();
}

TSharedRef<SWidget> FKadanImportPage::PathField(const FString& Name, FString& Value, bool Folder)
{
    TFunction<void()> ReadFolder;
    if (&Value == &Host.GameLocation || &Value == &Host.Retoc || &Value == &Host.Repak)
        ReadFolder = [this]() { QueueGameFolder(); };
    return Host.PathField(Name, Value, Folder, [this](FString& Changed) { FieldChanged(Changed); }, ReadFolder);
}

TSharedRef<SWidget> FKadanImportPage::Build()
{
    return SNew(SVerticalBox)
               .IsEnabled_Lambda([this]() { return !Host.Busy; })
               .Visibility_Lambda([this]() { return Host.Page == 0 ? EVisibility::Visible : EVisibility::Collapsed; }) +
        SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)[PathField(TEXT("Game"), Host.GameLocation, true)] +
        SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)[SNew(STextBlock)
                .Tag(TEXT("KadanGameState"))
                .AutoWrapText(true)
                .ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f))
                .Text_Lambda(
                    [this]()
                    {
                        return Label(Assets.Num() ? FString::Printf(TEXT("%d maps and assets in the game"), Assets.Num())
                                : Host.GameLocation.IsEmpty() ? TEXT("Choose Pikmin 4's romfs or Paks folder.")
                                                              : TEXT("Kadan hasn't read this folder."));
                    })] +
        SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)[MissingTools()] +
        SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)[ProjectSetupStrip()] +
        SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)[FilterRow()] +
        SVerticalBox::Slot().FillHeight(1).Padding(0, 0, 0, 6)[SourceBrowser()] +
        SVerticalBox::Slot().AutoHeight()[ImportActions()];
}

TSharedRef<SWidget> FKadanImportPage::MissingTools()
{
    const auto Missing = [this](bool IsRepak) { return !IFileManager::Get().FileExists(IsRepak ? *Host.Repak : *Host.Retoc); };
    return SNew(SBorder)
        .Tag(TEXT("KadanMissingTools"))
        .BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
        .Padding(8)
        .Visibility_Lambda([Missing]() { return Missing(false) || Missing(true) ? EVisibility::Visible : EVisibility::Collapsed; })
            [SNew(SVerticalBox) +
             SVerticalBox::Slot().AutoHeight()[SNew(STextBlock)
                     .AutoWrapText(true)
                     .Text(Label(TEXT("Kadan reads and writes the game's files with retoc and repak, which come in its "
                                      "Tools folder. Choose the one that's missing.")))] +
             SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
                 [SNew(SWrapBox).UseAllottedWidth(true) +
                  SWrapBox::Slot().Padding(0, 0, 8, 0)[SNew(SBox).Visibility_Lambda(
                      [Missing]() { return Missing(false) ? EVisibility::Visible : EVisibility::Collapsed; })
                          [Host.Action(TEXT("Choose retoc"), [this]() { ChooseTool(false); })]] +
                  SWrapBox::Slot()[SNew(SBox).Visibility_Lambda(
                      [Missing]() { return Missing(true) ? EVisibility::Visible : EVisibility::Collapsed; })
                          [Host.Action(TEXT("Choose repak"), [this]() { ChooseTool(true); })]]]];
}

void FKadanImportPage::ChooseTool(bool IsRepak)
{
    IDesktopPlatform* Desktop = P4KadanPathPicker();
    if (!Desktop)
    {
        Host.Message = TEXT("The file picker is unavailable.");
        Host.NoticeError = true;
        return;
    }
    FString& Value = IsRepak ? Host.Repak : Host.Retoc;
    TArray<FString> Picked;
    const void* Parent = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
    if (!Desktop->OpenFileDialog(Parent, IsRepak ? TEXT("Choose repak.exe") : TEXT("Choose retoc.exe"), FPaths::GetPath(Value),
            IsRepak ? TEXT("repak.exe") : TEXT("retoc.exe"), TEXT("Programs|*.exe"), EFileDialogFlags::None, Picked) ||
        !Picked.Num())
        return;
    Value = Picked[0];
    FieldChanged(Value);
    Host.WriteSettings();
    QueueGameFolder();
}

TSharedRef<SWidget> FKadanImportPage::ProjectSetupStrip()
{
    return SNew(SBorder)
        .Tag(TEXT("KadanProjectSetup"))
        .BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
        .BorderBackgroundColor(FLinearColor(0.32f, 0.24f, 0.06f))
        .Padding(8)
        .Visibility_Lambda([this]() { return ProjectReady ? EVisibility::Collapsed : EVisibility::Visible; })
            [SNew(SHorizontalBox) +
             SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(0, 0, 8, 0)
                 [SNew(STextBlock)
                      .Tag(TEXT("KadanSetupState"))
                      .AutoWrapText(true)
                      .ToolTipText_Lambda([this]() { return Label(ProjectSetupReason); })
                      .Text_Lambda(
                          [this]()
                          {
                              return Label(RestartRequired
                                      ? TEXT("Setup is saved. Restart Unreal Editor to finish it. The first start after "
                                             "setup may take a while: Unreal compiles thousands of shaders for the "
                                             "game's renderer, and the loading screen stays near 39% until they're done.")
                                      : TEXT("This project needs the game's settings before importing. Setup replaces "
                                             "its collision and navigation settings and restarts Unreal. The first "
                                             "start after setup may take a while: Unreal compiles thousands of "
                                             "shaders for the game's renderer."));
                          })] +
             SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                 [SNew(SBox).Visibility_Lambda([this]() { return RestartRequired ? EVisibility::Collapsed : EVisibility::Visible; })
                         [Host.Action(
                             TEXT("Set up project"),
                             [this]()
                             {
                                 Host.QueueOperation(TEXT("Kadan is setting up the project from the game."),
                                                     [this]() { SetUpProject(); });
                             },
                             true,
                             [this]()
                             {
                                 return !Host.GamePaks.IsEmpty() && !Host.UseExtractedPackages &&
                                        IFileManager::Get().FileExists(*Host.Repak);
                             },
                             TEXT("Choose the game folder first."))]] +
             SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                 [SNew(SBox).Visibility_Lambda([this]() { return RestartRequired ? EVisibility::Visible : EVisibility::Collapsed; })
                         [Host.Action(TEXT("Restart editor"), []() { FUnrealEdMisc::Get().RestartEditor(true); }, true, nullptr,
                             TEXT("Unreal Editor asks you to save your work before it restarts."))]]];
}

TSharedRef<SWidget> FKadanImportPage::FilterRow()
{
    return SNew(SHorizontalBox) +
        SHorizontalBox::Slot().FillWidth(1)[SNew(SEditableTextBox)
                .Style(FEditorStyle::Get(), "SpecialEditableTextBox")
                .Tag(TEXT("KadanSearch"))
                .Text_Lambda([this]() { return Label(Search); })
                .HintText(Label(TEXT("Search")))
                .OnTextChanged_Lambda(
                    [this](const FText& Text)
                    {
                        Search = Text.ToString();
                        Filter();
                    })] +
        SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)[SAssignNew(KindPicker, SComboBox<TSharedPtr<FString>>)
                .Tag(TEXT("KadanKindPicker"))
                .OptionsSource(&Kinds)
                .OnGenerateWidget_Lambda(
                    [](TSharedPtr<FString> Kind) { return SNew(STextBlock).Text(Label(KindLabel(*Kind))); })
                .OnSelectionChanged_Lambda(
                    [this](TSharedPtr<FString> Kind, ESelectInfo::Type)
                    {
                        SelectedKind = Kind;
                        Filter();
                    })[SNew(STextBlock)
                        .Text_Lambda([this]()
                            { return Label(SelectedKind ? KindLabel(*SelectedKind) : TEXT("All content")); })]];
}

TSharedRef<SWidget> FKadanImportPage::SourceBrowser()
{
    return SNew(SSplitter) +
        SSplitter::Slot().Value(0.28f)[SNew(SBorder)
                .BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
                .Padding(3)[SAssignNew(FolderTree, STreeView<TSharedPtr<FSourceFolder>>)
                        .Tag(TEXT("KadanFolderTree"))
                        .TreeItemsSource(&FolderRoots)
                        .SelectionMode(ESelectionMode::Single)
                        .OnGenerateRow_Lambda(
                            [](TSharedPtr<FSourceFolder> Folder, const TSharedRef<STableViewBase>& Owner)
                            {
                                return SNew(STableRow<TSharedPtr<FSourceFolder>>, Owner)
                                    .ToolTipText(Label(Folder->Path))[SNew(STextBlock)
                                            .Text(Label(Folder->Name + FString::Printf(TEXT(" (%d)"), Folder->Assets)))
                                            .Margin(FMargin(2, 4))];
                            })
                        .OnGetChildren_Lambda([](TSharedPtr<FSourceFolder> Folder,
                                                  TArray<TSharedPtr<FSourceFolder>>& Out) { Out = Folder->Children; })
                        .OnSelectionChanged_Lambda(
                            [this](TSharedPtr<FSourceFolder> Folder, ESelectInfo::Type)
                            {
                                PackageFolder = Folder.IsValid() ? Folder->Path : FString();
                                Filter();
                            })]] +
        SSplitter::Slot().Value(0.72f)[SNew(SVerticalBox) +
            SVerticalBox::Slot().AutoHeight().Padding(2, 0, 0, 2)[SNew(SCheckBox)
                    .Tag(TEXT("KadanSelectShown"))
                    .ToolTipText(Label(TEXT("Tick or untick everything the search and folder show.")))
                    .IsChecked_Lambda(
                        [this]()
                        {
                            int32 Ticked = 0;
                            for (const auto& Asset : Visible)
                                Ticked += SelectedPackages.Contains(Asset->Package) ? 1 : 0;
                            return !Ticked || !Visible.Num()   ? ECheckBoxState::Unchecked
                                : Ticked == Visible.Num() ? ECheckBoxState::Checked
                                                          : ECheckBoxState::Undetermined;
                        })
                    .OnCheckStateChanged_Lambda(
                        [this](ECheckBoxState State)
                        {
                            for (const auto& Asset : Visible)
                                if (State == ECheckBoxState::Checked)
                                    SelectedPackages.Add(Asset->Package);
                                else
                                    SelectedPackages.Remove(Asset->Package);
                            InvalidateImportReview();
                        })[SNew(STextBlock).Text_Lambda([this]()
                        { return Label(FString::Printf(TEXT("%d shown"), Visible.Num())); })]] +
            SVerticalBox::Slot().FillHeight(1)[SAssignNew(SourceList, SListView<TSharedPtr<FSourceRow>>)
                .Tag(TEXT("KadanSourceList"))
                .ListItemsSource(&Visible)
                .SelectionMode(ESelectionMode::None)
                .OnGenerateRow_Lambda(
                    [this](TSharedPtr<FSourceRow> Asset, const TSharedRef<STableViewBase>& Owner)
                    {
                        FString Folder = FPackageName::GetLongPackagePath(Asset->Package);
                        Folder.RemoveFromStart(TEXT(P4_GAME_ASSET_ROOT "/"));
                        Folder.RemoveFromStart(TEXT("/Game/"));
                        Folder.ReplaceInline(TEXT("/"), TEXT(" › "));
                        const bool There = InProject(Asset->Package);
                        return SNew(STableRow<TSharedPtr<FSourceRow>>, Owner)
                            .ToolTipText(Label(Asset->Package))[SNew(SHorizontalBox) +
                                SHorizontalBox::Slot()
                                    .AutoWidth()
                                    .VAlign(VAlign_Center)
                                    .Padding(2, 0, 8, 0)[SNew(SCheckBox)
                                            .IsChecked_Lambda(
                                                [this, Asset]()
                                                {
                                                    return SelectedPackages.Contains(Asset->Package)
                                                        ? ECheckBoxState::Checked
                                                        : ECheckBoxState::Unchecked;
                                                })
                                            .OnCheckStateChanged_Lambda(
                                                [this, Asset](ECheckBoxState State)
                                                {
                                                    if (State == ECheckBoxState::Checked)
                                                        SelectedPackages.Add(Asset->Package);
                                                    else
                                                        SelectedPackages.Remove(Asset->Package);
                                                    InvalidateImportReview();
                                                })] +
                                SHorizontalBox::Slot().FillWidth(1).Padding(0, 4)[SNew(SVerticalBox) +
                                    SVerticalBox::Slot().AutoHeight()[SNew(STextBlock)
                                            .Text(Label(FPackageName::GetLongPackageAssetName(Asset->Package)))
                                            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))] +
                                    SVerticalBox::Slot().AutoHeight()[SNew(STextBlock)
                                            .Text(Label(Folder))
                                            .ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f))
                                            .AutoWrapText(true)]] +
                                SHorizontalBox::Slot()
                                    .AutoWidth()
                                    .VAlign(VAlign_Center)
                                    .Padding(8, 0)[SNew(STextBlock)
                                            .ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f))
                                            .Text(Label(KindLabel(Asset->Kind) +
                                                (Asset->Bytes >= 0 ? TEXT(" · ") + Size(Asset->Bytes) : FString()) +
                                                (There ? TEXT(" · in project") : FString())))]];
                    })]];
}

TSharedRef<SWidget> FKadanImportPage::ImportActions()
{
    return SNew(SHorizontalBox) +
        SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(0, 0, 8, 0)[SNew(STextBlock)
                .Tag(TEXT("KadanImportReview"))
                .AutoWrapText(true)
                .Text_Lambda(
                    [this]()
                    {
                        if (!SelectedPackages.Num())
                            return Label(TEXT("Tick the maps and assets to bring into the project."));
                        if (ImportSummary.IsEmpty())
                            return Label(FString::Printf(TEXT("%d selected · counting what they need…"), SelectedPackages.Num()));
                        return Label(ImportSummary);
                    })] +
        SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SButton)
                .Tag(TEXT("KadanImport"))
                .ButtonStyle(FEditorStyle::Get(), "Button")
                .ButtonColorAndOpacity(PrimaryButtonTint)
                .TextStyle(FEditorStyle::Get(), "NormalText")
                .ForegroundColor(FLinearColor::White)
                .ContentPadding(FMargin(12, 4))
                .IsEnabled_Lambda([this]() { return SelectedPackages.Num() > 0 && ProjectReady && !ProjectDiskFull; })
                .ToolTipText_Lambda(
                    [this]()
                    {
                        return Label(!ProjectReady ? TEXT("Set up the project first.")
                                : ProjectDiskFull
                                ? TEXT("The disk the project is on is almost full. Free some space: Import comes back "
                                       "on by itself.")
                                : ReimportSelected()
                                    ? TEXT("Some of the selection is already in the project. Kadan imports it again, "
                                           "with what it needs, and asks before it replaces anything.")
                                    : TEXT("Import the selection and what it needs."));
                    })
                .Text_Lambda([this]() { return Label(ReimportSelected() ? TEXT("Reimport") : TEXT("Import")); })
                .OnClicked_Lambda(
                    [this]()
                    {
                        if (ReimportSelected())
                            RequestReimport();
                        else
                            QueueImport(true);
                        return FReply::Handled();
                    })];
}

FString FKadanImportPage::KindLabel(const FString& Kind)
{
    if (Kind == TEXT("All kinds"))
        return TEXT("All content");
    if (Kind == TEXT("In project"))
        return TEXT("In this project");
    if (Kind == TEXT("World"))
        return TEXT("Maps");
    if (Kind == TEXT("Texture2D"))
        return TEXT("Textures");
    if (Kind == TEXT("Blueprint"))
        return TEXT("Blueprints");
    if (Kind == TEXT("Asset"))
        return TEXT("Other assets");
    return Kind;
}

void FKadanImportPage::RebuildFolders()
{
    FolderRoots.Empty();
    TMap<FString, TSharedPtr<FSourceFolder>> Folders;
    for (const auto& Asset : Assets)
    {
        FString Path = FPackageName::GetLongPackagePath(Asset->Package);
        TArray<FString> Parts;
        Path.ParseIntoArray(Parts, TEXT("/"), true);
        FString ParentPath;
        for (const auto& Part : Parts)
        {
            const FString CurrentPath = ParentPath + TEXT("/") + Part;
            auto* Existing = Folders.Find(CurrentPath);
            if (!Existing)
            {
                auto Folder = MakeShared<FSourceFolder>();
                Folder->Path = CurrentPath;
                Folder->Name = Part;
                if (const auto* Parent = Folders.Find(ParentPath))
                    (*Parent)->Children.Add(Folder);
                else
                    FolderRoots.Add(Folder);
                Folders.Add(CurrentPath, Folder);
                Existing = Folders.Find(CurrentPath);
            }
            ++(*Existing)->Assets;
            ParentPath = CurrentPath;
        }
    }
    const auto Sort = [](const TSharedPtr<FSourceFolder>& A, const TSharedPtr<FSourceFolder>& B)
    { return A->Name < B->Name; };
    FolderRoots.Sort(Sort);
    for (auto& Pair : Folders)
        Pair.Value->Children.Sort(Sort);
    if (FolderTree)
    {
        FolderTree->RequestTreeRefresh();
        for (const auto& Folder : FolderRoots)
            FolderTree->SetItemExpansion(Folder, true);
        if (const auto* Carrot = Folders.Find(TEXT(P4_GAME_ASSET_ROOT)))
            FolderTree->SetItemExpansion(*Carrot, true);
    }
}

void FKadanImportPage::Filter()
{
    Visible.Empty();
    FString Folder = PackageFolder.TrimStartAndEnd();
    while (Folder.EndsWith(TEXT("/")))
        Folder.LeftChopInline(1, false);
    for (const auto& Asset : Assets)
    {
        if (!Search.IsEmpty() && !Asset->Package.Contains(Search))
            continue;
        if (!Folder.IsEmpty() && Asset->Package != Folder && !Asset->Package.StartsWith(Folder + TEXT("/")))
            continue;
        if (SelectedKind.IsValid() && *SelectedKind == TEXT("In project"))
        {
            if (!InProject(Asset->Package))
                continue;
        }
        else if (SelectedKind.IsValid() && *SelectedKind != TEXT("All kinds") && Asset->Kind != *SelectedKind)
            continue;
        Visible.Add(Asset);
    }
    if (SourceList)
        SourceList->RequestListRefresh();
}
} // namespace P4KadanEditorPrivate

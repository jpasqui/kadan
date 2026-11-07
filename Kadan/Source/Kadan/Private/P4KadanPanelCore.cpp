#include "P4KadanPanel.h"
#include "P4GameNames.h"
#include "P4KadanSettings.h"
#include "P4KadanPanelImportPage.h"
#include "P4KadanPanelModsPage.h"
#include "Widgets/Input/SHyperlink.h"

namespace P4KadanEditorPrivate
{

SKadanPanel::SKadanPanel()
{}

SKadanPanel::~SKadanPanel()
{
    FCoreUObjectDelegates::OnObjectModified.Remove(ModifiedHandle);
}

void SKadanPanel::ShowPage(int32 Number)
{
    Page = FMath::Clamp(Number, 0, 1);
    // A running operation keeps writing the notice of the page that started it.
    if (!Busy)
        SettleNotice();
}

void SKadanPanel::Construct(const FArguments&)
{
    ImportPage = MakeUnique<FKadanImportPage>(*this);
    ModsPage = MakeUnique<FKadanModsPage>(*this);
    ReadSettings();
    ModsPage->Prepare();
    ImportPage->Prepare();
    ModsPage->ReloadMods();
    ModsPage->RestoreLastModSelection();
    ModifiedHandle = FCoreUObjectDelegates::OnObjectModified.AddSP(this, &SKadanPanel::AssetModified);
    RegisterActiveTimer(0.5f, FWidgetActiveTimerDelegate::CreateSP(this, &SKadanPanel::TickPages));
    ChildSlot[SNew(SVerticalBox) +
        SVerticalBox::Slot().AutoHeight().Padding(10, 8)[SNew(SHorizontalBox) +
            SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                [SNew(STextBlock).Text(Label(TEXT("Kadan"))).Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))] +
            SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(8, 0)[SNew(SThrobber).Visibility_Lambda(
                    [this]() { return Busy ? EVisibility::Visible : EVisibility::Collapsed; })] +
            SHorizontalBox::Slot().AutoWidth()[PageButton(TEXT("Import"), 0)] +
            SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)[PageButton(TEXT("Mods"), 1)]] +
        SVerticalBox::Slot().AutoHeight().Padding(10, 0, 10, 6)[NoticeArea()] +
        SVerticalBox::Slot().FillHeight(1).Padding(10, 0, 10, 8)[ImportPage->Build()] +
        SVerticalBox::Slot().FillHeight(1).Padding(10, 0, 10, 8)[ModsPage->Build()]];
    if (!GameLocation.IsEmpty() && !UseExtractedPackages)
        ImportPage->QueueGameFolder();
}

TSharedRef<SWidget> SKadanPanel::NoticeArea()
{
    return SNew(SBorder)
        .BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
        .Padding(FMargin(8, 5))
        .BorderBackgroundColor_Lambda(
            [this]() { return ShownError() ? FLinearColor(0.35f, 0.10f, 0.10f) : FLinearColor(0.10f, 0.20f, 0.28f); })
            [SNew(SHorizontalBox) +
             SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(SBox).MaxDesiredHeight(
                 40)[SNew(SScrollBox) + SScrollBox::Slot()[SNew(STextBlock)
                                                               .Tag(TEXT("KadanStatus"))
                                                               .Text_Lambda([this]() { return Label(ShownMessage()); })
                                                               .AutoWrapText(true)]]] +
             SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
                 [SNew(SHyperlink)
                      .Tag(TEXT("KadanOutputLog"))
                      .Text(Label(TEXT("Output Log")))
                      .ToolTipText(Label(TEXT("Kadan writes the full reasons and file paths there.")))
                      .Visibility_Lambda([this]()
                          { return HasNoticeDetails() ? EVisibility::Visible : EVisibility::Collapsed; })
                      .OnNavigate_Lambda([]() { FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("OutputLog"))); })]];
}

bool SKadanPanel::CreatorPackageRoot(FString& Root, FString& Error) const
{
    return ModsPage->CreatorPackageRoot(Root, Error);
}

FKadanImportPage& SKadanPanel::Import()
{
    return *ImportPage;
}

FKadanModsPage& SKadanPanel::Mods()
{
    return *ModsPage;
}

TSharedRef<SKadanPanel> SKadanPanel::Self()
{
    return SharedThis(this);
}

FString SKadanPanel::ShownMessage() const
{
    return Page == MessagePage ? Message : PageMessages[Page];
}

bool SKadanPanel::HasNoticeDetails() const
{
    return (Page == 1 && !ModsPage->Details().IsEmpty()) || (Page == 0 && !ImportPage->Details().IsEmpty());
}

FString SKadanPanel::NoticeDetails() const
{
    if (Page == 1 && !ModsPage->Details().IsEmpty())
    {
        return ModsPage->Details();
    }
    if (Page == 0 && !ImportPage->Details().IsEmpty())
    {
        return ImportPage->Details();
    }
    return ShownMessage();
}

bool SKadanPanel::ShownError() const
{
    return Page == MessagePage ? NoticeError : PageErrors[Page];
}

void SKadanPanel::SettleNotice()
{
    if (Page == MessagePage)
        return;
    PageMessages[MessagePage] = Message;
    PageErrors[MessagePage] = NoticeError;
    Message = PageMessages[Page];
    NoticeError = PageErrors[Page];
    MessagePage = Page;
}

void SKadanPanel::AssetModified(UObject* Object)
{
    ModsPage->AssetModified(Object);
}

EActiveTimerReturnType SKadanPanel::TickPages(double, float)
{
    // Work a page does by itself once its inputs settle: the import and export reviews.
    if (!Busy)
    {
        if (Page == 0)
            ImportPage->Tick();
        else
            ModsPage->Tick();
    }
    return EActiveTimerReturnType::Continue;
}

void SKadanPanel::SettingsChanged()
{
    ModsPage->SettingsChanged();
}

TSharedRef<SWidget> SKadanPanel::PageButton(const FString& Text, int32 Number)
{
    return SNew(SButton)
        .ButtonStyle(FEditorStyle::Get(), "Button")
        .ButtonColorAndOpacity(ButtonTint)
        .TextStyle(FEditorStyle::Get(), "NormalText")
        .ForegroundColor(FLinearColor::White)
        .ContentPadding(FMargin(10, 5))
        .Tag(FName(*(TEXT("KadanPage.") + Text)))
        .Text(Label(Text))
        .ButtonColorAndOpacity_Lambda([this, Number]() { return Page == Number ? PrimaryButtonTint : ButtonTint; })
        .ToolTipText(Label(Number == 0 ? TEXT("Find, review and import original game content.")
                : Number == 1          ? TEXT("Create a mod and review the exact content it will ship.")
                                       : TEXT("Inspect selected assets and apply ground types to your scenery.")))
        .OnClicked_Lambda(
            [this, Number]()
            {
                ShowPage(Number);
                return FReply::Handled();
            });
}

void SKadanPanel::QueueOperation(const FString& Status, TFunction<void()> Operation, bool BackendOwnsProgress)
{
    if (Busy)
    {
        return;
    }
    Busy = true;
    if (Page == 0)
    {
        ImportPage->ClearDetails();
    }
    NoticeError = false;
    Message = Status;
    // Paint the busy state first, then leave Slate's draw callback before
    // synchronous work. Export progress can safely tick Slate from the main-thread
    // core ticker without recursively acquiring the panel's current draw buffer.
    const TWeakPtr<SKadanPanel> WeakPanel = SharedThis(this);
    RegisterActiveTimer(0.f,
        FWidgetActiveTimerDelegate::CreateLambda(
            [WeakPanel, Operation = MoveTemp(Operation), BackendOwnsProgress, Painted = false](double, float) mutable
            {
                if (!Painted)
                {
                    Painted = true;
                    return EActiveTimerReturnType::Continue;
                }
                FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
                    [WeakPanel, Operation = MoveTemp(Operation), BackendOwnsProgress](float) mutable
                    {
                        const auto Panel = WeakPanel.Pin();
                        if (!Panel)
                        {
                            return false;
                        }
                        if (BackendOwnsProgress)
                        {
                            Operation();
                        }
                        else
                        {
                            FScopedSlowTask Progress(1.f, Label(Panel->Message));
                            Progress.MakeDialog(true);
                            Progress.EnterProgressFrame(1.f);
                            if (!Progress.ShouldCancel())
                            {
                                Operation();
                            }
                            else
                            {
                                Panel->NoticeError = false;
                                Panel->Message = TEXT("The operation was canceled. Completed content is kept.");
                            }
                        }
                        Panel->Busy = false;
                        Panel->SettleNotice();
                        return false;
                    }));
                return EActiveTimerReturnType::Stop;
            }));
}

TSharedRef<SWidget> SKadanPanel::Heading(const FString& Text)
{
    return SNew(STextBlock).Text(Label(Text)).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12));
}

TSharedRef<SWidget> SKadanPanel::Action(
    const FString& Text, TFunction<void()> Click, bool Primary, TFunction<bool()> Enabled, const FString& Tooltip)
{
    return SNew(SButton)
        .ButtonStyle(FEditorStyle::Get(), "Button")
        .ButtonColorAndOpacity(Primary ? PrimaryButtonTint : ButtonTint)
        .TextStyle(FEditorStyle::Get(), "NormalText")
        .ForegroundColor(FLinearColor::White)
        .ContentPadding(FMargin(10, 5))
        .Text(Label(Text))
        .ToolTipText(Label(Tooltip))
        .IsEnabled_Lambda([this, Enabled]() { return !Busy && (!Enabled || Enabled()); })
        .OnClicked_Lambda(
            [Click]()
            {
                Click();
                return FReply::Handled();
            });
}

TSharedRef<SWidget> SKadanPanel::TextField(const FString& Name, FString& Value, TFunction<void(FString&)> OnChanged,
    TFunction<void()> OnCommitted, TSharedPtr<SEditableTextBox>* Input)
{
    TSharedPtr<SEditableTextBox> Created;
    auto Row =
        SNew(SHorizontalBox) +
        SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0, 0, 8, 0)[SNew(SBox).WidthOverride(120)[SNew(STextBlock).Text(Label(Name)).AutoWrapText(true)]] +
        SHorizontalBox::Slot().FillWidth(
            1)[SAssignNew(Created, SEditableTextBox)
                   .Style(FEditorStyle::Get(), "SpecialEditableTextBox")
                   .Tag(FName(*(TEXT("KadanField.") + Name)))
                   .Text_Lambda([&Value]() { return Label(Value); })
                   .HintText(Label(Name == TEXT("Version")          ? TEXT("1.0.0")
                                   : Name == TEXT("Content folder") ? TEXT(P4_MODS_FOLDER "/...")
                                                                    : TEXT("")))
                   .OnTextChanged_Lambda(
                       [&Value, OnChanged](const FText &Text)
                       {
                           const FString Edited = Text.ToString();
                           if (Value != Edited)
                           {
                               Value = Edited;
                               OnChanged(Value);
                           }
                       })
                   .OnTextCommitted_Lambda(
                       [OnCommitted](const FText &, ETextCommit::Type Commit)
                       {
                           if (OnCommitted && Commit != ETextCommit::OnCleared)
                               OnCommitted();
                       })];
    if (Input)
        *Input = Created;
    return Row;
}

TSharedRef<SWidget> SKadanPanel::PathField(const FString& Name, FString& Value, bool Folder,
    TFunction<void(FString&)> OnChanged, TFunction<void()> OnCommitted, TSharedPtr<SEditableTextBox>* Input)
{
    return SNew(SHorizontalBox) +
        SHorizontalBox::Slot().FillWidth(1)[TextField(Name, Value, OnChanged, OnCommitted, Input)] +
        SHorizontalBox::Slot().AutoWidth().Padding(6, 0)[SNew(SButton)
                .Tag(FName(*(TEXT("KadanPathPicker.") + Name)))
                .ButtonStyle(FEditorStyle::Get(), "Button")
                .ButtonColorAndOpacity(ButtonTint)
                .TextStyle(FEditorStyle::Get(), "NormalText")
                .ForegroundColor(FLinearColor::White)
                .ContentPadding(FMargin(8, 4))
                .Text(Label(TEXT("Browse")))
                .OnClicked_Lambda(
                    [this, &Value, Folder, Name, OnChanged, OnCommitted]()
                    {
                        IDesktopPlatform* Desktop = P4KadanPathPicker();
                        if (!Desktop)
                        {
                            Message = TEXT("The file picker is unavailable. Type the path in the field.");
                            return FReply::Handled();
                        }
                        const void* Parent = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
                        bool Changed = false;
                        if (Folder)
                        {
                            FString Picked;
                            if (Desktop->OpenDirectoryDialog(Parent, Name, Value, Picked))
                            {
                                Value = Picked;
                                Changed = true;
                            }
                        }
                        else
                        {
                            TArray<FString> Picked;
                            if (Desktop->OpenFileDialog(Parent, Name, FPaths::GetPath(Value),
                                    FPaths::GetCleanFilename(Value), TEXT("Files|*.*"), EFileDialogFlags::None,
                                    Picked) &&
                                Picked.Num())
                            {
                                Value = Picked[0];
                                Changed = true;
                            }
                        }
                        if (Changed)
                        {
                            OnChanged(Value);
                            WriteSettings();
                            if (OnCommitted)
                                OnCommitted();
                        }
                        return FReply::Handled();
                    })];
}

void SKadanPanel::TransferSettings(bool Write)
{
    struct FSettingField
    {
        const TCHAR* Name;
        FString* Value;
    };
    const FSettingField Fields[] = {{P4KadanSettings::GameFolder, &Settings.GameFolder},
        {P4KadanSettings::GamePaks, &GamePaks}, {P4KadanSettings::Retoc, &Retoc}, {P4KadanSettings::Repak, &Repak},
        {P4KadanSettings::ExportFolder, &ExportFolder}, {P4KadanSettings::GameLocation, &GameLocation}};
    for (const auto& Field : Fields)
    {
        if (Write)
            P4KadanSettings::Write(Field.Name, *Field.Value);
        else
            P4KadanSettings::Read(Field.Name, *Field.Value);
    }
    if (Write)
        P4KadanSettings::Write(P4KadanSettings::UseExtractedPackages, UseExtractedPackages);
    else
        P4KadanSettings::Read(P4KadanSettings::UseExtractedPackages, UseExtractedPackages);
    if (Write)
        P4KadanSettings::Flush();
}

void SKadanPanel::ReadSettings()
{
    TransferSettings(false);
    FP4KadanPathDefaults Current;
    Current.GameFolder = Settings.GameFolder;
    Current.GamePaks = GamePaks;
    Current.Retoc = Retoc;
    Current.Repak = Repak;
    Current.ExportFolder = ExportFolder;
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("Kadan"));
    const auto Defaults = P4KadanDiscoverPaths(Current, Plugin ? Plugin->GetBaseDir() : FString(), FPaths::ProjectDir(),
        FPaths::ProjectSavedDir(), FPlatformMisc::GetEnvironmentVariable(TEXT("PATH")));
    Settings.GameFolder = Defaults.GameFolder;
    GamePaks = Defaults.GamePaks;
    Retoc = Defaults.Retoc;
    Repak = Defaults.Repak;
    ExportFolder = Defaults.ExportFolder;
    if (GameLocation.IsEmpty())
        GameLocation = GamePaks;
    if (Defaults.MissingTools.Num())
    {
        FString Tools = Defaults.MissingTools.Last();
        for (int32 Index = Defaults.MissingTools.Num() - 2; Index >= 0; --Index)
            Tools = Defaults.MissingTools[Index] +
                (Index == Defaults.MissingTools.Num() - 2 ? TEXT(" and ") : TEXT(", ")) + Tools;
        Message = TEXT("Kadan needs ") + Tools +
            TEXT(". Choose them on the Import page, or put them in Kadan's Tools folder.");
    }
}

void SKadanPanel::WriteSettings()
{
    TransferSettings(true);
}
} // namespace P4KadanEditorPrivate

#include "P4KadanExportReceipt.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformProcess.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/SWindow.h"

namespace
{
class SKadanExportReceipt final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SKadanExportReceipt) {}
    SLATE_END_ARGS()

    void Construct(const FArguments&, TSharedRef<FP4KadanReceiptState> InState)
    {
        State = InState;
        SetTag(FName(TEXT("KadanExportReceipt")));
        State->Changed.AddSP(this, &SKadanExportReceipt::Rebuild);
        SetVisibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateLambda([this]()
            { return State->HasReceipt || !State->Error.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })));
        Rebuild();
    }

private:
    TSharedPtr<FP4KadanReceiptState> State;
    TArray<TSharedPtr<FString>> Files;
    TArray<bool> Checks;

    TSharedRef<SWidget> Text(const FString& Value, const FName WidgetTag = NAME_None)
    {
        return SNew(STextBlock).Tag(WidgetTag).AutoWrapText(true).Text(FText::FromString(Value));
    }

    TSharedRef<SWidget> Heading(const FString& Value)
    {
        return SNew(STextBlock).Text(FText::FromString(Value)).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12));
    }

    TSharedRef<SWidget> Button(const FString& Caption, TFunction<void()> Action)
    {
        return SNew(SButton)
            .ButtonStyle(FEditorStyle::Get(), "Button")
            .ButtonColorAndOpacity(FLinearColor(0.12f, 0.14f, 0.18f))
            .TextStyle(FEditorStyle::Get(), "NormalText")
            .ForegroundColor(FLinearColor::White)
            .ContentPadding(FMargin(8, 4))
            .Text(FText::FromString(Caption))
            .OnClicked_Lambda(
                [Action]()
                {
                    Action();
                    return FReply::Handled();
                });
    }

    TSharedRef<SWidget> Check(const FString& Caption, int32 Index)
    {
        return SNew(SCheckBox)
            .Tag(FName(*(TEXT("KadanReceiptCheck.") + FString::FromInt(Index))))
            .IsChecked_Lambda(
                [this, Index]() { return Checks[Index] ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .IsEnabled_Lambda([this]() { return !State->Receipt.GameTestRecorded; })
            .OnCheckStateChanged_Lambda([this, Index](ECheckBoxState Value)
                { Checks[Index] = Value == ECheckBoxState::Checked; })[Text(Caption)];
    }

    void InstallGuide()
    {
        auto Window = SNew(SWindow)
            .Title(FText::FromString(TEXT("Install guide")))
            .ClientSize(FVector2D(700, 420))
            .SupportsMaximize(false)
            .SupportsMinimize(false)
            [SNew(SBorder)
                .BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
                .Padding(16)
                [SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
                        [Text(TEXT("Install Tsuchi before testing the export. Keep the exported romfs folder and its contents in the same layout."))]
                    + SVerticalBox::Slot().AutoHeight()[Heading(TEXT("Export folder"))]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)[Text(State->Receipt.Directory)]
                    + SVerticalBox::Slot().AutoHeight()[Heading(TEXT("Eden"))]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
                        [Text(TEXT("Place the exported mod folder beside the Tsuchi installation in Pikmin 4's mod folder. If Output folder already points there, the export is in place. Keep the romfs folder inside the mod's ID folder."))]
                    + SVerticalBox::Slot().AutoHeight()[Heading(TEXT("Console"))]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
                        [Text(TEXT("Follow Tsuchi's console installation guide for the title folder and supported setup. Keep the exported romfs layout when installing the mod."))]
                    + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
                        [Button(TEXT("Online installation guide"), []()
                        {
                            FPlatformProcess::LaunchURL(TEXT("https://tsuchi.jpasqui.dev/players/install-a-mod/"), nullptr, nullptr);
                        })]]];
        FSlateApplication::Get().AddWindow(Window);
    }

    void Rebuild()
    {
        Checks.Init(State->Receipt.GameTestRecorded, 4);
        Files.Empty();
        for (const FString& File : State->Receipt.Files)
        {
            Files.Add(MakeShared<FString>(File));
        }
        const auto& Receipt = State->Receipt;
        auto Body = SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[Heading(TEXT("Last export"))] +
            SVerticalBox::Slot().AutoHeight()[SNew(STextBlock)
                    .Tag(TEXT("KadanReceiptError"))
                    .AutoWrapText(true)
                    .Text_Lambda([this]() { return FText::FromString(State->Error); })
                    .Visibility_Lambda(
                        [this]() { return State->Error.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })];
        if (!State->HasReceipt)
        {
            ChildSlot[Body];
            return;
        }
        Body->AddSlot().AutoHeight()[Text(Receipt.Mod.Name)];
        Body->AddSlot()
            .AutoHeight()[Text(Receipt.Mod.Id + TEXT(" ") + Receipt.Mod.Version, TEXT("KadanReceiptIdentity"))];
        Body->AddSlot()
            .AutoHeight()[Text(Receipt.PublishedUtc.ToString(TEXT("%Y-%m-%d %H:%M:%S UTC")), TEXT("KadanReceiptTime"))];
        Body->AddSlot().AutoHeight()[Text(TEXT(
            "This receipt records the last successful publication and its version. Review the mod again after further edits."))];
        Body->AddSlot().AutoHeight()[Text(Receipt.Directory, TEXT("KadanReceiptDirectory"))];
        Body->AddSlot().AutoHeight().Padding(0, 4)[SNew(SWrapBox).UseAllottedWidth(true) +
            SWrapBox::Slot().Padding(0, 0, 6, 0)[Button(
                TEXT("Open output folder"), [this]() { FPlatformProcess::ExploreFolder(*State->Receipt.Directory); })] +
            SWrapBox::Slot().Padding(0, 0, 6, 0)[Button(
                TEXT("Copy path"), [this]() { FPlatformApplicationMisc::ClipboardCopy(*State->Receipt.Directory); })] +
            SWrapBox::Slot()[Button(TEXT("Install guide"), [this]() { InstallGuide(); })]];
        if (Receipt.UnbakedActors)
        {
            Body->AddSlot().AutoHeight()[Text(
                FString::Printf(
                    TEXT(
                        "%d added actor%s shipped without baked lighting. Read the export notes before testing the mod."),
                    Receipt.UnbakedActors, Receipt.UnbakedActors == 1 ? TEXT("") : TEXT("s")),
                TEXT("KadanReceiptLighting"))];
        }
        auto Notes = SNew(SVerticalBox);
        for (const FString& Note : Receipt.Notes)
        {
            Notes->AddSlot().AutoHeight().Padding(0, 3)[Text(Note)];
        }
        Notes->AddSlot().AutoHeight().HAlign(HAlign_Left)[Button(TEXT("Copy export notes"),
            [this]() { FPlatformApplicationMisc::ClipboardCopy(*FString::Join(State->Receipt.Notes, TEXT("\n"))); })];
        Body->AddSlot().AutoHeight()[SNew(SExpandableArea)
                .Tag(TEXT("KadanReceiptNotes"))
                .AreaTitle(FText::FromString(TEXT("Export notes")))
                .InitiallyCollapsed(false)
                .Visibility(Receipt.Notes.Num() ? EVisibility::Visible : EVisibility::Collapsed)
                .BodyContent()[Notes]];
        Body->AddSlot().AutoHeight()[SNew(SBox).HeightOverride(130)[SNew(SListView<TSharedPtr<FString>>)
                .Tag(TEXT("KadanPublishedFiles"))
                .ListItemsSource(&Files)
                .SelectionMode(ESelectionMode::None)
                .OnGenerateRow_Lambda(
                    [](TSharedPtr<FString> File, const TSharedRef<STableViewBase>& Owner)
                    {
                        return SNew(STableRow<TSharedPtr<FString>>,
                            Owner)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(*File))];
                    })]];
        Body->AddSlot().AutoHeight()[Heading(TEXT("Game testing"))];
        Body->AddSlot()
            .AutoHeight()[Text(Receipt.GameTestRecorded ? TEXT("The creator recorded game testing for this export.")
                                                        : TEXT("Game testing is pending for this export."),
                TEXT("KadanReceiptGameStatus"))];
        Body->AddSlot().AutoHeight()[Text(
            TEXT("Check the exported mod in the game. Revisit the area after saving when testing placement changes."))];
        Body->AddSlot().AutoHeight()[Check(TEXT("Rendering"), 0)];
        Body->AddSlot().AutoHeight()[Check(TEXT("Collision"), 1)];
        Body->AddSlot().AutoHeight()[Check(TEXT("Pikmin paths"), 2)];
        Body->AddSlot().AutoHeight()[Check(TEXT("Placement and saves"), 3)];
        Body->AddSlot().AutoHeight().HAlign(HAlign_Left)[SNew(SButton)
                .ButtonStyle(FEditorStyle::Get(), "Button")
                .ButtonColorAndOpacity(FLinearColor(0.12f, 0.14f, 0.18f))
                .TextStyle(FEditorStyle::Get(), "NormalText")
                .ForegroundColor(FLinearColor::White)
                .ContentPadding(FMargin(8, 4))
                .Text(FText::FromString(TEXT("Record game test")))
                .IsEnabled_Lambda([this]() { return !State->Receipt.GameTestRecorded && !Checks.Contains(false); })
                .OnClicked_Lambda(
                    [this]()
                    {
                        auto Saved = State->Receipt;
                        Saved.GameTestRecorded = true;
                        Saved.TestedUtc = FDateTime::UtcNow();
                        if (P4KadanWriteExportReceipt(State->ProjectDirectory, Saved, State->Error))
                        {
                            State->Receipt = MoveTemp(Saved);
                        }
                        State->Changed.Broadcast();
                        return FReply::Handled();
                    })];
        ChildSlot[SNew(SBorder).BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder")).Padding(8)[Body]];
    }
};
} //namespace

TSharedRef<SWidget> P4KadanExportReceiptWidget(TSharedRef<FP4KadanReceiptState> State)
{
    return SNew(SKadanExportReceipt, State);
}

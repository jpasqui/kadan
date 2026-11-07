#include "P4KadanShippingRow.h"
#include "EditorStyleSet.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
TSharedRef<SWidget> RowAction(const FString& Caption, TFunction<void()> Action)
{
    return SNew(SButton)
        .ButtonStyle(FEditorStyle::Get(), "Button")
        .ButtonColorAndOpacity(FLinearColor(0.12f, 0.14f, 0.18f))
        .TextStyle(FEditorStyle::Get(), "NormalText")
        .ForegroundColor(FLinearColor::White)
        .ContentPadding(FMargin(8, 4))
        .Text(FText::FromString(Caption))
        .OnClicked_Lambda([Action]() {
            Action();
            return FReply::Handled();
        });
}
} // namespace

TSharedRef<ITableRow> P4KadanShippingRow(const FP4ExportSelectionRow& Row, const FP4KadanExportIssue& Issue,
                                         const FString& Disposition, TAttribute<bool> ReviewCurrent,
                                         FP4KadanShippingActions Actions,
                                         const TSharedRef<STableViewBase>& Owner)
{
    const bool Attention = Row.Disposition == EP4ExportDisposition::Refused ||
                           Row.Disposition == EP4ExportDisposition::NeedsReplacementProof;
    auto Buttons = SNew(SWrapBox).UseAllottedWidth(true);
    Buttons->AddSlot().Padding(0, 0, 6, 0)[RowAction(TEXT("Locate asset"), Actions.LocateAsset)];
    if (Attention)
    {
        if (Actions.LocateRoot)
        {
            Buttons->AddSlot().Padding(0, 0, 6, 0)[RowAction(TEXT("Locate root"), Actions.LocateRoot)];
        }
        if (Actions.RemoveRoot)
        {
            Buttons->AddSlot().Padding(0, 0, 6, 0)[RowAction(TEXT("Remove root"), Actions.RemoveRoot)];
        }
        if (Actions.OpenMap)
        {
            Buttons->AddSlot().Padding(0, 0, 6, 0)[RowAction(TEXT("Open persistent map"), Actions.OpenMap)];
        }
        else if (Issue.NeedsPersistentMap && Actions.FindMap)
        {
            Buttons->AddSlot().Padding(0, 0, 6, 0)[RowAction(TEXT("Find map"), Actions.FindMap)];
        }
        Buttons->AddSlot()[RowAction(TEXT("Copy details"), [Issue]() {
            FPlatformApplicationMisc::ClipboardCopy(*(Issue.Asset + TEXT("\n") + Issue.Detail));
        })];
    }

    auto Body =
        SNew(SVerticalBox).IsEnabled(ReviewCurrent) +
        SVerticalBox::Slot().AutoHeight().Padding(4,
                                                  2)[SNew(STextBlock)
                                                         .Text(FText::FromString(Disposition))
                                                         .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))] +
        SVerticalBox::Slot().AutoHeight().Padding(
            4, 2)[SNew(STextBlock).Text(FText::FromString(Row.Package)).AutoWrapText(true)] +
        SVerticalBox::Slot().AutoHeight().Padding(
            4, 2)[SNew(STextBlock)
                      .Text(FText::FromString(Attention ? Issue.Cause : Row.Reason))
                      .AutoWrapText(true)] +
        SVerticalBox::Slot().AutoHeight().Padding(
            4,
            2)[SNew(STextBlock)
                   .Text(FText::FromString(Row.Explicit ? TEXT("Selected root") : TEXT("Required content")))];
    if (Attention)
    {
        Body->AddSlot().AutoHeight().Padding(
            4, 2)[SNew(STextBlock).Text(FText::FromString(Issue.NextStep)).AutoWrapText(true)];
        if (!Issue.SelectingRoot.IsEmpty())
        {
            Body->AddSlot().AutoHeight().Padding(
                4, 2)[SNew(STextBlock).Text(FText::FromString(Issue.SelectingRoot)).AutoWrapText(true)];
        }
        Body->AddSlot().AutoHeight().Padding(
            4,
            2)[SNew(SExpandableArea)
                   .Tag(FName(*(TEXT("KadanIssueDetails.") + Row.Package)))
                   .AreaTitle(FText::FromString(TEXT("Details")))
                   .InitiallyCollapsed(true)
                   .BodyContent()[SNew(STextBlock).Text(FText::FromString(Issue.Detail)).AutoWrapText(true)]];
    }
    Body->AddSlot().AutoHeight().Padding(4, 2)[Buttons];
    return SNew(STableRow<TSharedPtr<FP4ExportSelectionRow>>, Owner)[Body];
}

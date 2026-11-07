#include "P4KadanModDraftChoice.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
struct FDraftChoice
{
    FString Error;
    FString Detail;
    TWeakPtr<SWindow> Window;
    TFunction<FP4KadanDraftSaveResult()> Save;
    TFunction<void()> Continue;

    void Finish(bool Proceed)
    {
        if (const auto Dialog = Window.Pin())
        {
            Dialog->RequestDestroyWindow();
        }
        if (Proceed)
        {
            Continue();
        }
    }
};

TSharedRef<SWidget> Button(const FString& Caption, TFunction<void()> Action)
{
    return SNew(SButton)
        .ButtonStyle(FEditorStyle::Get(), "Button")
        .ButtonColorAndOpacity(FLinearColor(0.12f, 0.14f, 0.18f))
        .TextStyle(FEditorStyle::Get(), "NormalText")
        .ForegroundColor(FLinearColor::White)
        .ContentPadding(FMargin(10, 5))
        .Text(FText::FromString(Caption))
        .OnClicked_Lambda(
            [Action]()
            {
                Action();
                return FReply::Handled();
            });
}
} //namespace

TSharedRef<SWindow> P4KadanModDraftChoice(const TSharedRef<SWidget>& Parent, const FP4ModDescriptor& Draft,
    TFunction<FP4KadanDraftSaveResult()> Save, TFunction<void()> Continue)
{
    auto State = MakeShared<FDraftChoice>();
    State->Save = MoveTemp(Save);
    State->Continue = MoveTemp(Continue);
    auto Window = SNew(SWindow)
        .Title(FText::FromString(TEXT("Unsaved mod details")))
        .ClientSize(FVector2D(560, 370))
        .SupportsMaximize(false)
        .SupportsMinimize(false)
        [SNew(SBorder)
            .BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
            .Padding(16)
            [SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock)
                    .AutoWrapText(true)
                    .Text(FText::FromString(TEXT("Save the mod's name and version before changing mods, or discard the draft to continue.")))]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)[SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Name")))]
                + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock)
                    .AutoWrapText(true)
                    .Text(FText::FromString(Draft.Name))]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[SNew(STextBlock)
                    .Text(FText::FromString(TEXT("Version")))]
                + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock)
                    .Text(FText::FromString(Draft.Version))]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 10)[SNew(STextBlock)
                    .Tag(TEXT("KadanDraftError"))
                    .AutoWrapText(true)
                    .Text_Lambda([State]() { return FText::FromString(State->Error); })]
                + SVerticalBox::Slot().AutoHeight()[SNew(SExpandableArea)
                    .AreaTitle(FText::FromString(TEXT("Details")))
                    .InitiallyCollapsed(true)
                    .Visibility_Lambda([State]() { return State->Detail.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
                    .BodyContent()[SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock)
                            .AutoWrapText(true)
                            .Text_Lambda([State]() { return FText::FromString(State->Detail); })]
                        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)[Button(TEXT("Copy details"), [State]()
                        {
                            FPlatformApplicationMisc::ClipboardCopy(*State->Detail);
                        })]]]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 12).HAlign(HAlign_Right)
                    [SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Button(TEXT("Save"), [State]()
                        {
                            const auto Result = State->Save();
                            if (Result.Saved)
                            {
                                State->Finish(true);
                            }
                            else
                            {
                                State->Error = Result.Message;
                                State->Detail = Result.Detail;
                            }
                        })]
                        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Button(TEXT("Discard"), [State]()
                        {
                            State->Finish(true);
                        })]
                        + SHorizontalBox::Slot().AutoWidth()[Button(TEXT("Cancel"), [State]()
                        {
                            State->Finish(false);
                        })]]]];
    State->Window = Window;
    const auto ParentWindow = FSlateApplication::Get().FindWidgetWindow(Parent);
    FSlateApplication::Get().AddModalWindow(Window, ParentWindow, true);
    Window->ShowWindow();
    return Window;
}

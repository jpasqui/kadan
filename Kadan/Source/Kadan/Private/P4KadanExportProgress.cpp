#include "P4KadanExportProgress.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/PlatformTime.h"
#include "P4ExportProgress.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
FString StageName(EP4ExportStage Stage)
{
    switch (Stage)
    {
    case EP4ExportStage::Review:
        return TEXT("Review");
    case EP4ExportStage::Cooking:
        return TEXT("Cooking");
    case EP4ExportStage::Lighting:
        return TEXT("Lighting");
    case EP4ExportStage::Navigation:
        return TEXT("Navigation");
    case EP4ExportStage::Packing:
        return TEXT("Packing");
    case EP4ExportStage::Checking:
        return TEXT("Checking");
    default:
        return TEXT("Publishing");
    }
}
} // namespace

void P4KadanProgressOperation(TFunction<void()> Work)
{
    struct FState
    {
        double Started = FPlatformTime::Seconds(), LastPaint = 0;
        bool Canceled = false, CanCancel = true, HasStage = false, Painting = false;
        EP4ExportStage Stage = EP4ExportStage::Review;
        FString Subject, Completed;
    };
    auto State = MakeShared<FState>();
    auto Window =
        SNew(SWindow)
            .Tag(TEXT("KadanExportProgress"))
            .Title(FText::FromString(TEXT("Export progress")))
            .ClientSize(FVector2D(540, 420))
            .SupportsMaximize(false)
            .SupportsMinimize(false)
            .HasCloseButton(false)[SNew(SBorder).Padding(
                16)[SNew(SVerticalBox) +
                    SVerticalBox::Slot().AutoHeight()[SNew(STextBlock)
                                                          .Tag(TEXT("KadanExportStage"))
                                                          .Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
                                                          .Text_Lambda([State]() {
                                                              return FText::FromString(
                                                                  StageName(State->Stage));
                                                          })] +
                    SVerticalBox::Slot().AutoHeight().Padding(0, 10)[SNew(SThrobber)] +
                    SVerticalBox::Slot().AutoHeight()[SNew(STextBlock)
                                                          .Tag(TEXT("KadanExportSubject"))
                                                          .AutoWrapText(true)
                                                          .Text_Lambda([State]() {
                                                              return FText::FromString(State->Subject);
                                                          })] +
                    SVerticalBox::Slot().AutoHeight().Padding(0, 8)[SNew(STextBlock).Text_Lambda([State]() {
                        return FText::FromString(FString::Printf(TEXT("Elapsed %.0f s"),
                                                                 FPlatformTime::Seconds() - State->Started));
                    })] +
                    SVerticalBox::Slot().FillHeight(
                        1)[SNew(SScrollBox) +
                           SScrollBox::Slot()[SNew(STextBlock).AutoWrapText(true).Text_Lambda([State]() {
                               return FText::FromString(State->Completed.IsEmpty()
                                                            ? TEXT("Completed stages")
                                                            : TEXT("Completed stages\n") + State->Completed);
                           })]] +
                    SVerticalBox::Slot()
                        .AutoHeight()[SNew(STextBlock).AutoWrapText(true).Text_Lambda([State]() {
                            return FText::FromString(
                                !State->CanCancel ? TEXT("Publication is in progress. Wait for it to finish.")
                                : State->Canceled ? TEXT("Kadan will cancel before the next stage. The "
                                                         "current step must finish first.")
                                                  : TEXT("You can cancel before the next stage. The current "
                                                         "step must finish first."));
                        })] +
                    SVerticalBox::Slot()
                        .AutoHeight()
                        .HAlign(HAlign_Left)
                        .Padding(0, 10, 0, 0)[SNew(SButton)
                                                  .Text(FText::FromString(TEXT("Cancel export")))
                                                  .IsEnabled_Lambda([State]() {
                                                      return State->CanCancel && !State->Canceled;
                                                  })
                                                  .OnClicked_Lambda([State]() {
                                                      State->Canceled = true;
                                                      return FReply::Handled();
                                                  })]]];
    if (auto Parent = FGlobalTabmanager::Get()->GetRootWindow())
    {
        FSlateApplication::Get().AddModalWindow(Window, Parent, true);
        // Slow-task modal windows start hidden and do not block this call.
        Window->ShowWindow();
    }
    else
    {
        FSlateApplication::Get().AddWindow(Window);
    }
    auto Paint = [State]() {
        if (State->Painting || FPlatformTime::Seconds() - State->LastPaint < 0.1)
            return;
        State->Painting = true;
        State->LastPaint = FPlatformTime::Seconds();
        FSlateApplication::Get().PumpMessages();
        FSlateApplication::Get().Tick();
        State->Painting = false;
    };
    {
        FP4ExportProgressScope Progress(
            [State, Paint](EP4ExportStage Next, const FString& Asset, bool NextCanCancel) {
                if (State->Canceled)
                    return false;
                if (State->HasStage && Next != State->Stage)
                {
                    if (!State->Completed.IsEmpty())
                        State->Completed += TEXT("\n");
                    State->Completed += StageName(State->Stage);
                }
                State->HasStage = true;
                State->Stage = Next;
                State->Subject = Asset;
                State->CanCancel = NextCanCancel;
                State->LastPaint = 0;
                Paint();
                return !State->Canceled;
            },
            Paint);
        Work();
    }
    Window->RequestDestroyWindow();
}

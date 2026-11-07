#include "P4KadanImportRepair.h"
#include "P4Common.h"
#include "P4GamePaths.h"
#include "Dom/JsonObject.h"
#include "EditorStyleSet.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IPluginManager.h"
#include "P4KadanImport.h"
#include "P4KadanImportDependencies.h"
#include "PluginDescriptor.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
TSharedRef<SWidget> Paragraph(const FString& Text)
{
    return SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(Text));
}

TSharedRef<SWidget> Button(const FString& Text, TFunction<void()> Action)
{
    return SNew(SButton)
        .ButtonStyle(FEditorStyle::Get(), "Button")
        .ButtonColorAndOpacity(FLinearColor(0.12f, 0.14f, 0.18f))
        .ForegroundColor(FLinearColor::White)
        .ContentPadding(FMargin(10, 5))
        .Text(FText::FromString(Text))
        .OnClicked_Lambda(
            [Action]()
            {
                Action();
                return FReply::Handled();
            });
}

FString Version()
{
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("Kadan"));
    return Plugin ? Plugin->GetDescriptor().VersionName : TEXT("Unavailable");
}
} // namespace

void P4KadanUpgradeGuide(const TSharedRef<SWidget>& Parent)
{
    auto Window =
        SNew(SWindow)
            .Title(FText::FromString(TEXT("Upgrade guide")))
            .ClientSize(FVector2D(650, 330))
            .SupportsMaximize(false)
            .SupportsMinimize(
                false)[SNew(SBorder)
                           .BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
                           .Padding(16)
                               [SNew(SVerticalBox) +
                                SVerticalBox::Slot().AutoHeight()[Paragraph(TEXT("Kadan ") + Version())] +
                                SVerticalBox::Slot().AutoHeight().Padding(0, 12)[Paragraph(TEXT(
                                    "Save your project and close Unreal Editor before replacing "
                                    "the Kadan and P4Stubs folders. Reopen the project after the update."))] +
                                SVerticalBox::Slot().AutoHeight().Padding(0, 8)[Paragraph(
                                    TEXT("In Import, choose the game folder and use Select imported. This "
                                         "selects previous imports across every folder and filter. Review "
                                         "the selection before reimporting."))] +
                                SVerticalBox::Slot().AutoHeight().Padding(0, 8)[Paragraph(
                                    TEXT("Reimport rediscovers required content from the game. Save your "
                                         "edits first. Read the per-asset outcomes afterward, then review "
                                         "each mod's export and test it in the game."))] +
                                SVerticalBox::Slot().AutoHeight().Padding(0, 8)[Paragraph(
                                    TEXT("The version identifies the installed plugin. It does not "
                                         "establish compatibility or game correctness."))]]];
    FSlateApplication::Get().AddWindowAsNativeChild(
        Window, FSlateApplication::Get().FindWidgetWindow(Parent).ToSharedRef());
}

TSharedRef<SWindow> P4KadanImportRepair(const TSharedRef<SWidget>& Parent, const FP4KadanImportPlan& Plan,
                                        const TSet<FString>& Selected, TFunction<void()> Reimport)
{
    auto Packages = SNew(SVerticalBox);
    TArray<FString> Sorted = Selected.Array();
    TArray<FString> Required;
    FString ReferenceError;
    const bool ReferencesReady =
        P4KadanImportKnownRequiredReferences(Plan, Selected, Required, ReferenceError);
    for (const FString& Package : Required)
    {
        Sorted.AddUnique(Package);
    }
    for (const auto& Asset : Plan.Assets)
    {
        if (Asset)
        {
            Sorted.AddUnique(Asset->Package);
        }
    }
    TSet<FString> UnsavedLevels;
    for (TObjectIterator<UWorld> It; It; ++It)
    {
        const auto* Package = It->GetOutermost();
        if (P4IsContentPackageIgnoringCase(Package->GetName()) && Package->IsDirty())
        {
            UnsavedLevels.Add(Package->GetName());
            Sorted.AddUnique(Package->GetName());
        }
    }
    Sorted.Sort(
        [&Selected, &UnsavedLevels](const FString& A, const FString& B)
        {
            const auto Rank = [&Selected, &UnsavedLevels](const FString& Name)
            {
                return Selected.Contains(Name) ? 0 : UnsavedLevels.Contains(Name) ? 1 : 2;
            };
            return Rank(A) == Rank(B) ? A < B : Rank(A) < Rank(B);
        });
    int32 Dirty = 0;
    for (const FString& Name : Sorted)
    {
        const auto* Package = FindPackage(nullptr, *Name);
        const bool Unsaved = Package && Package->IsDirty();
        Dirty += Unsaved ? 1 : 0;
        const FString Kind = Selected.Contains(Name)        ? TEXT("Selected package")
                             : UnsavedLevels.Contains(Name) ? TEXT("Unsaved level")
                                                            : TEXT("Required reference");
        Packages->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[Paragraph(Kind)];
        Packages->AddSlot().AutoHeight()[Paragraph(Name)];
        if (Unsaved)
        {
            Packages->AddSlot()
                .AutoHeight()[Paragraph(TEXT("This package has unsaved edits. Save it before reimporting."))];
        }
    }
    if (!ReferencesReady)
    {
        Packages->AddSlot().AutoHeight()[Paragraph(
            TEXT("Known references could not be checked. Read Details before reimporting."))];
        Packages->AddSlot().AutoHeight()[Paragraph(TEXT("Details"))];
        Packages->AddSlot().AutoHeight()[Paragraph(ReferenceError)];
    }
    for (const FString& Problem : Plan.Problems)
    {
        Packages->AddSlot().AutoHeight().Padding(0, 8, 0, 0)[Paragraph(TEXT("Details"))];
        Packages->AddSlot().AutoHeight()[Paragraph(Problem)];
    }
    auto Window = SNew(SWindow)
                      .Title(FText::FromString(TEXT("Reimport summary")))
                      .ClientSize(FVector2D(650, 520))
                      .SupportsMaximize(false)
                      .SupportsMinimize(false);
    const TWeakPtr<SWindow> Weak = Window;
    Window->SetContent(
        SNew(SBorder)
            .BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
            .Padding(
                16)[SNew(SVerticalBox) +
                    SVerticalBox::Slot().AutoHeight()[Paragraph(TEXT("Kadan ") + Version())] +
                    SVerticalBox::Slot().AutoHeight().Padding(0, 8)[Paragraph(TEXT(
                        "Save your edits before reimporting. Kadan rediscovers required content and uses "
                        "the ordinary import checks to preserve creator changes or report refusals."))] +
                    SVerticalBox::Slot().AutoHeight()[Paragraph(FString::Printf(
                        TEXT("This review contains %d selected package%s and %d known required reference%s. "
                             "%d "
                             "loaded package%s %s unsaved edits."),
                        Selected.Num(), Selected.Num() == 1 ? TEXT("") : TEXT("s"), Required.Num(),
                        Required.Num() == 1 ? TEXT("") : TEXT("s"), Dirty, Dirty == 1 ? TEXT("") : TEXT("s"),
                        Dirty == 1 ? TEXT("has") : TEXT("have")))] +
                    SVerticalBox::Slot().FillHeight(1).Padding(
                        0,
                        8)[SNew(SScrollBox).Tag(TEXT("KadanRepairPackages")) + SScrollBox::Slot()[Packages]] +
                    SVerticalBox::Slot().AutoHeight().Padding(0, 8)[Paragraph(
                        TEXT("Required references come from the current source review. Reimport may discover "
                             "more. Completed assets stay "
                             "available if you cancel. Read Last import notes before retrying."))] +
                    SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
                        [SNew(SHorizontalBox) +
                         SHorizontalBox::Slot()
                             .AutoWidth()
                             .Padding(0, 0, 8, 0)[Button(
                                 TEXT("Reimport"),
                                 [Weak, Reimport]()
                                 {
                                     if (const auto Dialog = Weak.Pin())
                                     {
                                         Dialog->RequestDestroyWindow();
                                     }
                                     Reimport();
                                 })] +
                         SHorizontalBox::Slot()
                             .AutoWidth()[Button(
                                 TEXT("Cancel"),
                                 [Weak]()
                                 {
                                     if (const auto Dialog = Weak.Pin())
                                     {
                                         Dialog->RequestDestroyWindow();
                                     }
                                 })]]]);
    FSlateApplication::Get().AddModalWindow(Window, FSlateApplication::Get().FindWidgetWindow(Parent), true);
    Window->ShowWindow();
    return Window;
}

TArray<FString> P4KadanImportOutcomes(const FP4KadanImportResult& Result)
{
    TArray<FString> Rows;
    TSharedPtr<FJsonObject> Report;
    const TSharedPtr<FJsonObject>* Assets = nullptr;
    if (!P4ParseJson(Result.Report, Report) || !Report ||
        !Report->TryGetObjectField(TEXT("assets"), Assets))
    {
        return Rows;
    }
    for (const auto& Pair : (*Assets)->Values)
    {
        const auto Object =
            Pair.Value && Pair.Value->Type == EJson::Object ? Pair.Value->AsObject() : nullptr;
        if (!Object)
        {
            continue;
        }
        FString State;
        FString Error;
        bool Valid = false;
        Object->TryGetStringField(TEXT("state"), State);
        Object->TryGetStringField(TEXT("error"), Error);
        Object->TryGetBoolField(TEXT("valid"), Valid);
        const bool Kept = State == TEXT("existing asset preserved") ||
                          State == TEXT("existing creator content preserved") ||
                          State == TEXT("existing unchanged source dependency retained");
        const FString Outcome = Kept                                 ? TEXT("Kept")
                                : Result.Imported.Contains(Pair.Key) ? TEXT("Imported")
                                : Valid                              ? TEXT("Retained")
                                                                     : TEXT("Refused");
        Rows.Add(Outcome + TEXT(" · ") + Pair.Key + (Error.IsEmpty() ? FString() : TEXT("\n") + Error));
    }
    Rows.Sort();
    return Rows;
}

// The cut-down Kadan panel driven with real clicks and keys: New mod and its window, Edit, Add selected, What
// ships filling by itself, and Export mod becoming available. P4_KADAN_LOOK_ASSET is the asset Add selected adds
// (a package of the project); P4_KADAN_LOOK_EXPORT is where Export to points. Saves Saved/KadanPanelClick/mods.png.
#include "P4KadanEditor.h"
#include "AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
TSharedPtr<SWidget> FindIn(const TSharedRef<SWidget>& Root, TFunctionRef<bool(const TSharedRef<SWidget>&)> Match)
{
    if (Match(Root))
        return Root;
    FChildren* Children = Root->GetChildren();
    for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
        if (const TSharedPtr<SWidget> Found = FindIn(Children->GetChildAt(Index), Match))
            return Found;
    return nullptr;
}

FString TextOf(const TSharedRef<SWidget>& Widget)
{
    if (Widget->GetType() == FName(TEXT("STextBlock")))
        return StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString();
    return FString();
}

// The button whose own text is Caption, in Root.
TSharedPtr<SWidget> ButtonIn(const TSharedRef<SWidget>& Root, const FString& Caption)
{
    return FindIn(Root,
        [&](const TSharedRef<SWidget>& Widget)
        {
            return Widget->GetType() == FName(TEXT("SButton")) &&
                FindIn(Widget, [&](const TSharedRef<SWidget>& Inner) { return TextOf(Inner) == Caption; }).IsValid();
        });
}

TSharedPtr<SWidget> Tagged(const TSharedRef<SWidget>& Root, const TCHAR* Tag)
{
    return FindIn(Root, [&](const TSharedRef<SWidget>& Widget) { return Widget->GetTag() == FName(Tag); });
}

bool Click(const TSharedPtr<SWidget>& Widget)
{
    if (!Widget || !Widget->IsEnabled() || Widget->GetCachedGeometry().GetLocalSize().X < 1)
        return false;
    const auto Window = FSlateApplication::Get().FindWidgetWindow(Widget.ToSharedRef());
    if (!Window || !Window->GetNativeWindow())
        return false;
    const FGeometry& Geometry = Widget->GetCachedGeometry();
    const FVector2D Point = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5f);
    TSet<FKey> Down, Up;
    Down.Add(EKeys::LeftMouseButton);
    auto& Slate = FSlateApplication::Get();
    const FVector2D Previous = Slate.GetCursorPos();
    Slate.SetCursorPos(Point);
    Slate.ProcessMouseMoveEvent(FPointerEvent(0, Point, Previous, Up, EKeys::Invalid, 0, FModifierKeysState()), false);
    Slate.ProcessMouseButtonDownEvent(
        Window->GetNativeWindow(), FPointerEvent(0, Point, Point, Down, EKeys::LeftMouseButton, 0, FModifierKeysState()));
    Slate.ProcessMouseButtonUpEvent(FPointerEvent(0, Point, Point, Up, EKeys::LeftMouseButton, 0, FModifierKeysState()));
    return true;
}

// Clicks the field and types Value in place of what it holds.
bool Type(const TSharedPtr<SWidget>& Field, const FString& Value)
{
    if (!Click(Field))
        return false;
    auto& Slate = FSlateApplication::Get();
    const FModifierKeysState Control(false, false, true, false, false, false, false, false, false);
    const FKeyEvent SelectAll(EKeys::A, Control, 0, false, 0, 0);
    Slate.ProcessKeyDownEvent(SelectAll);
    Slate.ProcessKeyUpEvent(SelectAll);
    const FKeyEvent Backspace(EKeys::BackSpace, FModifierKeysState(), 0, false, 8, 0);
    Slate.ProcessKeyDownEvent(Backspace);
    Slate.ProcessKeyCharEvent(FCharacterEvent(TCHAR(8), FModifierKeysState(), 0, false));
    Slate.ProcessKeyUpEvent(Backspace);
    for (const TCHAR Character : Value)
        Slate.ProcessKeyCharEvent(FCharacterEvent(Character, FModifierKeysState(), 0, false));
    return true;
}

TSharedPtr<SWindow> WindowIn(const TSharedRef<SWindow>& Window, const FString& Title)
{
    if (Window->GetTitle().ToString() == Title)
        return Window;
    for (const TSharedRef<SWindow>& Child : Window->GetChildWindows())
        if (const TSharedPtr<SWindow> Found = WindowIn(Child, Title))
            return Found;
    return nullptr;
}

TSharedPtr<SWindow> WindowTitled(const FString& Title)
{
    TArray<TSharedRef<SWindow>> Windows;
    FSlateApplication::Get().GetAllVisibleWindowsOrdered(Windows);
    for (const TSharedRef<SWindow>& Window : Windows)
        if (const TSharedPtr<SWindow> Found = WindowIn(Window, Title))
            return Found;
    return nullptr;
}

class FKadanPanelClick : public IAutomationLatentCommand
{
  public:
    FKadanPanelClick(FAutomationTestBase* InTest, FString InName, FString InAsset, FString InFolder)
        : Test(InTest), Name(MoveTemp(InName)), Asset(MoveTemp(InAsset)), Folder(MoveTemp(InFolder))
    {}

    bool Update() override
    {
        if (FPlatformTime::Seconds() < NextAt)
            return false;
        NextAt = FPlatformTime::Seconds() + 0.5;
        if (FPlatformTime::Seconds() - StepStarted > 60)
            return Fail(TEXT("Step ") + FString::FromInt(Step) + TEXT(" did not finish within a minute. ") + Last);
        const TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(FName(TEXT("Kadan"))));
        const TSharedPtr<SWidget> Panel = Tab ? TSharedPtr<SWidget>(Tab->GetContent()) : TSharedPtr<SWidget>();
        if (Step > 0 && !Panel)
            return false;
        switch (Step)
        {
        case 0:
            P4KadanShowPanel(1);
            return Next();
        case 1: // New mod opens its window
            return Panel && Click(ButtonIn(Panel.ToSharedRef(), TEXT("New mod"))) ? Next() : false;
        case 2:
        {
            const auto Window = WindowTitled(TEXT("New mod"));
            if (!Window)
            {
                Last = TEXT("no window titled New mod among ") + FString::FromInt(FSlateApplication::Get().GetInteractiveTopLevelWindows().Num()) + TEXT(" windows");
                return false;
            }
            const auto Field = Tagged(Window.ToSharedRef(), TEXT("KadanField.Name"));
            Last = !Field ? TEXT("the window has no Name field")
                : FString::Printf(TEXT("Name field %.0fx%.0f, enabled %d"), Field->GetCachedGeometry().GetLocalSize().X,
                      Field->GetCachedGeometry().GetLocalSize().Y, Field->IsEnabled() ? 1 : 0);
            return Type(Field, Name) ? Next() : false;
        }
        case 3: // The name suggests an ID, then Save closes the window
        {
            const auto Window = WindowTitled(TEXT("New mod"));
            if (!Window)
                return Fail(TEXT("The New mod window closed before Save."));
            const auto Id = StaticCastSharedPtr<SEditableTextBox>(Tagged(Window.ToSharedRef(), TEXT("KadanField.ID")));
            if (!Id || Id->GetText().IsEmpty())
                return Fail(TEXT("The name did not suggest an ID."));
            SuggestedId = Id->GetText().ToString();
            return Click(ButtonIn(Window.ToSharedRef(), TEXT("Save"))) ? Next() : false;
        }
        case 4:
            if (WindowTitled(TEXT("New mod")))
                return false;
            return Panel && FindIn(Panel.ToSharedRef(), [&](const TSharedRef<SWidget>& W) { return TextOf(W) == Name + TEXT("  1.0.0"); })
                ? Next()
                : false;
        case 5: // Edit changes the version
            return Click(ButtonIn(Panel.ToSharedRef(), TEXT("Edit"))) ? Next() : false;
        case 6:
        {
            const auto Window = WindowTitled(TEXT("Edit mod"));
            if (!Window)
                return false;
            const auto Id = Tagged(Window.ToSharedRef(), TEXT("KadanModIdLock"));
            if (!Id || Id->IsEnabled())
                return Fail(TEXT("A saved mod's ID can be changed in Edit."));
            return Type(Tagged(Window.ToSharedRef(), TEXT("KadanField.Version")), TEXT("1.0.1")) ? Next() : false;
        }
        case 7:
        {
            const auto Window = WindowTitled(TEXT("Edit mod"));
            return Window && Click(ButtonIn(Window.ToSharedRef(), TEXT("Save"))) ? Next() : false;
        }
        case 8:
            if (WindowTitled(TEXT("Edit mod")))
                return false;
            if (!FindIn(Panel.ToSharedRef(), [&](const TSharedRef<SWidget>& W) { return TextOf(W) == Name + TEXT("  1.0.1"); }))
                return false;
            if (Asset.IsEmpty())
                return Finish();
            {
                TArray<FAssetData> Found;
                FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get().GetAssetsByPackageName(FName(*Asset), Found);
                if (!Found.Num())
                    return Fail(TEXT("The asset to add is not in the project: ") + Asset);
                FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get().SyncBrowserToAssets(Found);
            }
            return Next();
        case 9: // Add selected puts the Content Browser's selection in the mod
            return Click(ButtonIn(Panel.ToSharedRef(), TEXT("Add selected"))) ? Next() : false;
        case 10:
            return FindIn(Panel.ToSharedRef(),
                       [&](const TSharedRef<SWidget>& W) { return TextOf(W) == FPackageName::GetShortName(Asset); })
                ? Next()
                : false;
        case 11: // What ships fills by itself
        {
            const auto Summary = Tagged(Panel.ToSharedRef(), TEXT("KadanExportReview"));
            const FString Text = Summary ? TextOf(Summary.ToSharedRef()) : FString();
            if (Text.IsEmpty() || Text.StartsWith(TEXT("Checking")) || Text.StartsWith(TEXT("Add content")))
                return false;
            Test->AddInfo(TEXT("What ships: ") + Text);
            return Next();
        }
        case 12:
        {
            const auto Export = Tagged(Panel.ToSharedRef(), TEXT("KadanExport"));
            if (!Export || !Export->IsEnabled())
                return Fail(TEXT("Export mod is not available for a mod with content and a folder to export to."));
            return Finish();
        }
        }
        return true;
    }

  private:
    bool Next()
    {
        ++Step;
        StepStarted = FPlatformTime::Seconds();
        return false;
    }

    bool Fail(const FString& Why)
    {
        Picture();
        Test->AddError(Why);
        return true;
    }

    bool Finish()
    {
        Picture();
        Test->AddInfo(TEXT("Suggested ID: ") + SuggestedId);
        return true;
    }

    void Picture()
    {
        const TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(FName(TEXT("Kadan"))));
        TArray<FColor> Pixels;
        FIntVector Size;
        if (Tab && FSlateApplication::Get().TakeScreenshot(Tab->GetContent(), Pixels, Size) && Size.X > 100 && Size.Y > 100)
        {
            TArray<uint8> PNG;
            FImageUtils::CompressImageArray(Size.X, Size.Y, Pixels, PNG);
            FFileHelper::SaveArrayToFile(PNG, *(Folder / TEXT("mods.png")));
        }
    }

    FAutomationTestBase* Test;
    FString Name, Asset, Folder, SuggestedId, Last;
    int32 Step = 0;
    double NextAt = 0, StepStarted = FPlatformTime::Seconds();
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanPanelClickTest, "P4MapKit.Kadan.PanelClick",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FP4KadanPanelClickTest::RunTest(const FString&)
{
    if (!FSlateApplication::IsInitialized() || IsRunningCommandlet())
    {
        AddWarning(TEXT("PanelClick needs the editor's window."));
        return true;
    }
    const FString Export = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_LOOK_EXPORT"));
    GConfig->SetString(TEXT("Kadan"), TEXT("ExportFolder"),
        *(Export.IsEmpty() ? FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("KadanPanelClickExport")) : Export),
        GEditorPerProjectIni);
    GConfig->Flush(false, GEditorPerProjectIni);
    P4RegisterKadanEditor();
    if (const TSharedPtr<SDockTab> Restored = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(FName(TEXT("Kadan")))))
        Restored->RequestCloseTab();
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("KadanPanelClick");
    IFileManager::Get().MakeDirectory(*Folder, true);
    const FString Name = TEXT("Click ") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(6);
    ADD_LATENT_AUTOMATION_COMMAND(FKadanPanelClick(this, Name, FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_LOOK_ASSET")), Folder));
    return true;
}
#endif

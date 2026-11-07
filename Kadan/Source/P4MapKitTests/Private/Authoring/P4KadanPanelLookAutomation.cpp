// Pictures of the Kadan panel's pages as the editor draws them, for looking at the UI after a change.
// Writes Saved/KadanPanelLook/import.png and mods.png. P4_KADAN_LOOK_GAME, when set, is the game folder the
// Import page reads first, so its list is filled.
#include "P4KadanEditor.h"
#include "P4ModDescriptor.h"
#include "P4ExportSelection.h"
#include "P4KadanLastMod.h"
#include "Misc/PackageName.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWindow.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
class FKadanPanelLook : public IAutomationLatentCommand
{
  public:
    FKadanPanelLook(FAutomationTestBase* InTest, FString InFolder) : Test(InTest), Folder(MoveTemp(InFolder)) {}

    bool Update() override
    {
        static const TCHAR* const Names[] = {TEXT("import"), TEXT("mods")};
        if (!Shown)
        {
            P4KadanShowPanel(Page);
            Shown = true;
            Started = FPlatformTime::Seconds();
            return false;
        }
        // Let the panel read its settings and paint the page before taking its pixels.
        if (FPlatformTime::Seconds() - Started < (Page == 0 ? 8.0 : 30.0))
            return false;
        const TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(FName(TEXT("Kadan"))));
        const TSharedPtr<SWindow> Window = Tab ? FSlateApplication::Get().FindWidgetWindow(Tab.ToSharedRef()) : nullptr;
        if (Window)
            Window->Resize(FVector2D(720, 900));
        TArray<FColor> Pixels;
        FIntVector Size;
        if (!Tab || !FSlateApplication::Get().TakeScreenshot(Tab->GetContent(), Pixels, Size) || Size.X < 200 || Size.Y < 200)
        {
            Test->AddError(TEXT("The Kadan panel has no rendered picture."));
            return true;
        }
        TArray<uint8> PNG;
        FImageUtils::CompressImageArray(Size.X, Size.Y, Pixels, PNG);
        Test->TestTrue(TEXT("Save the panel picture"), FFileHelper::SaveArrayToFile(PNG, *(Folder / Names[Page] + TEXT(".png"))));
        if (++Page == 2)
            return true;
        Shown = false;
        return false;
    }

  private:
    FAutomationTestBase* Test;
    FString Folder;
    int32 Page = 0;
    bool Shown = false;
    double Started = 0;
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4KadanPanelLookTest, "P4MapKit.Kadan.PanelLook",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FP4KadanPanelLookTest::RunTest(const FString&)
{
    if (!FSlateApplication::IsInitialized() || IsRunningCommandlet())
    {
        AddWarning(TEXT("PanelLook needs the editor's window."));
        return true;
    }
    // P4_KADAN_LOOK_GAME, _RETOC and _REPAK: the game folder and tools the panel reads, as a creator's settings
    // hold them. P4_KADAN_LOOK_MOD: a mod with this root (a package or a /Game folder) in it, open on Mods.
    for (const TCHAR* Key : {TEXT("GameLocation"), TEXT("Retoc"), TEXT("Repak")})
    {
        const FString Value = FPlatformMisc::GetEnvironmentVariable(*(FString(TEXT("P4_KADAN_LOOK_")) +
            (FString(Key) == TEXT("GameLocation") ? TEXT("GAME") : FString(Key).ToUpper())));
        if (!Value.IsEmpty())
            GConfig->SetString(TEXT("Kadan"), Key, *Value, GEditorPerProjectIni);
    }
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_LOOK_MOD"));
    if (!Root.IsEmpty())
    {
        FP4ModDescriptor Mod;
        Mod.Id = TEXT("panel.look");
        Mod.Name = TEXT("Panel Look");
        Mod.Version = TEXT("1.0.0");
        FString Why;
        FP4ExportRoot Content;
        Content.Kind = FPackageName::IsValidLongPackageName(Root) && FPackageName::DoesPackageExist(Root)
            ? EP4ExportRootKind::Package
            : EP4ExportRootKind::Folder;
        Content.Path = Root;
        if (!TestTrue(TEXT("The look's mod is written"),
                (P4WriteModDescriptor(FPaths::ProjectDir(), Mod, nullptr, Why) || Why.Contains(TEXT("exist"))) &&
                    P4WriteExportRoots(FPaths::ProjectDir(), Mod, {Content}, Why)))
            AddInfo(Why);
        P4KadanRememberModId(GEditorPerProjectIni, Mod.Id);
    }
    GConfig->Flush(false, GEditorPerProjectIni);
    P4RegisterKadanEditor();
    // A panel the editor restored from its layout read the settings at startup: a new one reads them now.
    if (const TSharedPtr<SDockTab> Restored = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(FName(TEXT("Kadan")))))
        Restored->RequestCloseTab();
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("KadanPanelLook");
    IFileManager::Get().MakeDirectory(*Folder, true);
    ADD_LATENT_AUTOMATION_COMMAND(FKadanPanelLook(this, Folder));
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "P4KadanAssetPolicy.h"
#include "P4KadanEditor.h"
#include "P4KadanReadOnly.h"
#include "P4MaterialInstanceGameSettings.h"
#include "P4KadanImport.h"
#include "P4KadanCreatorFlowFixtures.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "IAssetTypeActions.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailsView.h"
#include "ImageUtils.h"
#include "MaterialEditor/MaterialEditorInstanceConstant.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/SWindow.h"

namespace
{
TSharedPtr<SSearchBox> Search(const TSharedRef<SWidget>& Widget, int32 Depth = 0)
{
    if (Depth > 24)
    {
        return nullptr;
    }
    if (Widget->GetType() == TEXT("SSearchBox"))
    {
        return StaticCastSharedRef<SSearchBox>(Widget);
    }
    FChildren* Children = Widget->GetChildren();
    for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
    {
        if (auto Found = Search(Children->GetChildAt(Index), Depth + 1))
        {
            return Found;
        }
    }
    return nullptr;
}
class FInstanceEditorCapture : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    TWeakObjectPtr<UMaterialInstanceConstant> Asset;
    FString File;
    double Started = FPlatformTime::Seconds();
    bool Filtered = false;

public:
    FInstanceEditorCapture(FAutomationTestBase* InTest, UMaterialInstanceConstant* InAsset, FString InFile)
        : Test(InTest), Asset(InAsset), File(MoveTemp(InFile))
    {}
    bool Update() override
    {
        if (FPlatformTime::Seconds() - Started < 3.)
        {
            return false;
        }
        auto* Instance = Asset.Get();
        auto* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
        auto* Open = Instance ? Editors->FindEditorForAsset(Instance, false) : nullptr;
        if (!Open)
        {
            Test->AddError(TEXT("The material-instance editor closed before capture."));
            return true;
        }
        const bool Editable = P4Kadan::GetAssetPolicy(Instance).AllowEdit;
        if (Editable && !Filtered)
        {
            TSharedPtr<IDetailsView> Details;
            for (TObjectIterator<UMaterialEditorInstanceConstant> It; It; ++It)
            {
                if (It->SourceInstance == Instance)
                {
                    Details = It->DetailsView.Pin();
                }
            }
            if (!Details.IsValid())
            {
                Test->AddError(TEXT("The actual instance editor has no Details panel."));
                return true;
            }
            const auto Toolkit = static_cast<FAssetEditorToolkit*>(Open);
            const auto Manager = Toolkit->GetAssociatedTabManager();
            const auto Panel = Manager
                ? Manager->FindExistingLiveTab(FTabId(TEXT("MaterialInstanceEditor_MaterialProperties")))
                : nullptr;
            const auto Window = Panel ? FSlateApplication::Get().FindWidgetWindow(Panel->GetContent()) : nullptr;
            if (Window && Panel)
            {
                P4KadanResizeDetailsSplit(Window.ToSharedRef(), Panel->GetContent(), Orient_Horizontal);
                P4KadanResizeDetailsSplit(Window.ToSharedRef(), Panel->GetContent(), Orient_Vertical);
            }
            auto Box = Search(Details.ToSharedRef());
            if (!Box.IsValid())
            {
                Test->AddError(TEXT("The actual Details search is absent."));
                return true;
            }
            const FString RequestedSearch = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_MI_UI_SEARCH"));
            Box->SetText(FText::FromString(RequestedSearch.IsEmpty() ? TEXT("C4") : RequestedSearch));
            Filtered = true;
            Started = FPlatformTime::Seconds();
            return false;
        }
        Open->FocusWindow(Instance);
        auto Window = FSlateApplication::Get().GetActiveTopLevelWindow();
        TArray<FColor> Pixels;
        FIntVector Size;
        if (!Window || !FSlateApplication::Get().TakeScreenshot(Window.ToSharedRef(), Pixels, Size) || Size.X < 400 ||
            Size.Y < 200)
        {
            Test->AddError(TEXT("The actual material-instance editor has no rendered image."));
        }
        else
        {
            TArray<uint8> PNG;
            FImageUtils::CompressImageArray(Size.X, Size.Y, Pixels, PNG);
            Test->TestTrue(TEXT("Actual instance editor pixels retained"), FFileHelper::SaveArrayToFile(PNG, *File));
        }
        Test->AddInfo(TEXT("Actual editor: ") + Open->GetEditorName().ToString() +
            (Editable ? TEXT(" (editable, property search)") : TEXT(" (read-only)")));
        if (Editable)
        {
            Test->TestTrue(
                TEXT("C4 editor settings exist"), UP4MaterialInstanceGameSettings::Find(Instance) != nullptr);
        }
        else
        {
            Test->TestEqual(TEXT("Locked family uses the visible reference editor"), Open->GetEditorName(),
                FName(TEXT("KadanReference")));
        }
        Editors->CloseAllEditorsForAsset(Instance);
        return true;
    }
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialInstanceEditorProof, "P4MapKit.Kadan.MaterialInstanceEditorProof",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialInstanceEditorProof::RunTest(const FString&)
{
    const FString Package = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_MI_EDIT_PACKAGE"));
    const FString File = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_MI_UI_FILE"));
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_MI_EDIT_PROOF")) != TEXT("1") || Package.IsEmpty() ||
        File.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. ")
                TEXT("Opt-in instance editor capture: set P4_MI_EDIT_PROOF=1, P4_MI_EDIT_PACKAGE and P4_MI_UI_FILE."));
        return true;
    }
    if (!GEditor || !FApp::CanEverRender() || !FSlateApplication::IsInitialized())
    {
        AddError(TEXT("The actual instance editor requires a rendered desktop session."));
        return false;
    }
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_MI_MERGE_IMPORT")) == TEXT("1"))
    {
        FP4KadanImportSettings Settings;
        Settings.GameFolder = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_MI_EDIT_GAMEFILES"));
        Settings.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_MI_EDIT_PAKS"));
        Settings.Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_MI_EDIT_RETOC"));
        Settings.Repak = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_MI_EDIT_REPAK"));
        Settings.AllowGameBackedLevels = true;
        const auto Imported = P4KadanImportMap(Settings, Package, true);
        if (!TestTrue(TEXT("The owned original instance imports through the current native path"), Imported.Ok))
        {
            for (const FString& Problem : Imported.Problems)
            {
                AddError(Problem);
            }
            return false;
        }
    }
    auto* Instance = LoadObject<UMaterialInstanceConstant>(
        nullptr, *(Package + TEXT(".") + FPackageName::GetShortName(Package)));
    if (!TestNotNull(TEXT("Actual imported original instance"), Instance) ||
        !TestTrue(TEXT("Recovered source"), P4Kadan::IsRecoveredGameMaterialInstance(Instance)))
    {
        return false;
    }
    P4RegisterKadanEditor();
    P4Kadan::UpdateReadOnlyAsset(Instance);
    auto Actions = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"))
                       .Get()
                       .GetAssetTypeActionsForClass(Instance->GetClass())
                       .Pin();
    if (!TestTrue(TEXT("Normal material-instance Content Browser action"), Actions.IsValid()))
    {
        return false;
    }
    Actions->OpenAssetEditor({Instance});
    ADD_LATENT_AUTOMATION_COMMAND(FInstanceEditorCapture(this, Instance, File));
    return true;
}
#endif

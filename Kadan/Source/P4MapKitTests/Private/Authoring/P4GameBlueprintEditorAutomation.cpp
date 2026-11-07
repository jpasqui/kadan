#include "P4KadanAssetPolicy.h"
#include "P4KadanEditor.h"
#include "P4KadanReadOnly.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "IAssetTypeActions.h"
#include "BlueprintEditor.h"
#include "BlueprintEditorModes.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonSerializer.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "Widgets/SWindow.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
class FGameBlueprintEditorCapture : public IAutomationLatentCommand
{
public:
    FGameBlueprintEditorCapture(FAutomationTestBase* InTest, UBlueprint* InAsset, FString InFolder, bool InEditable)
        : Test(InTest), Asset(InAsset), Folder(MoveTemp(InFolder)), Editable(InEditable),
          Started(FPlatformTime::Seconds())
    {}
    bool Update() override
    {
        // Let the normal editor paint before asking Slate for its rendered pixels.
        if (FPlatformTime::Seconds() - Started < 3.0)
            return false;
        UBlueprint* BP = Asset.Get();
        auto* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
        IAssetEditorInstance* Open = BP ? Editors->FindEditorForAsset(BP, false) : nullptr;
        if (!Open)
        {
            Test->AddError(TEXT("The actual game Blueprint editor closed before capture."));
            return true;
        }
        Open->FocusWindow(BP);
        const auto Window = FSlateApplication::Get().GetActiveTopLevelWindow();
        TArray<FColor> Pixels;
        FIntVector Size;
        if (!Window || !FSlateApplication::Get().TakeScreenshot(Window.ToSharedRef(), Pixels, Size) || Size.X < 400 ||
            Size.Y < 200)
            Test->AddError(TEXT("The actual Blueprint editor has no rendered screenshot."));
        else
        {
            TArray<uint8> PNG;
            FImageUtils::CompressImageArray(Size.X, Size.Y, Pixels, PNG);
            Test->TestTrue(
                TEXT("Retain actual editor pixels"), FFileHelper::SaveArrayToFile(PNG, *(Folder / TEXT("editor.png"))));
        }
        const TSharedRef<FJsonObject> Receipt = MakeShared<FJsonObject>();
        Receipt->SetStringField(TEXT("asset"), BP->GetPathName());
        Receipt->SetStringField(TEXT("editor"), Open->GetEditorName().ToString());
        Receipt->SetBoolField(TEXT("editable"), Editable);
        Receipt->SetBoolField(
            TEXT("defaults_editable"), P4Kadan::GetAssetPolicy(BP->GeneratedClass->GetDefaultObject()).AllowEdit);
        Receipt->SetBoolField(TEXT("has_components"), BP->SimpleConstructionScript != nullptr);
        UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(BP);
        Receipt->SetBoolField(
            TEXT("event_graph_editable"), Graph && Graph->bEditable && P4Kadan::GetAssetPolicy(BP).AllowEdit);
        FString Text;
        FJsonSerializer::Serialize(Receipt, TJsonWriterFactory<>::Create(&Text));
        Test->TestTrue(TEXT("Retain editor route receipt"),
            FFileHelper::SaveStringToFile(
                Text, *(Folder / TEXT("editor.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
        Editors->CloseAllEditorsForAsset(BP);
        return true;
    }

private:
    FAutomationTestBase* Test;
    TWeakObjectPtr<UBlueprint> Asset;
    FString Folder;
    bool Editable;
    double Started;
};
} //namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GameBlueprintEditorRoute, "P4MapKit.Kadan.GameBlueprintEditorRoute",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GameBlueprintEditorRoute::RunTest(const FString&)
{
    const FString Package = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_BP_EDIT_PACKAGE"));
    const FString Folder = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_BP_UI_OUT"));
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_BP_EDIT_PROOF")) != TEXT("1") || Folder.IsEmpty() ||
        (Package != TEXT("/Game/Carrot4/Placeables/Gimmicks/CrackPot/GCrackPotS") &&
            Package != TEXT("/Game/Carrot4/Core/Graphics/System/BP_EnvController")))
    {
        AddError(TEXT("Use the owned actual-original Blueprint qualification fixture."));
        return false;
    }
    if (IsRunningCommandlet() || !GEditor || !FSlateApplication::IsInitialized() || !FApp::CanEverRender())
    {
        AddError(TEXT("The Blueprint editor route needs a rendered Slate editor."));
        return false;
    }
    UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *(Package + TEXT(".") + FPackageName::GetShortName(Package)));
    if (!TestNotNull(TEXT("Actual imported game actor Blueprint"), BP) ||
        !TestTrue(TEXT("Complete recovered original, not a stand-in"), P4Kadan::IsRecoveredGameActorBlueprint(BP)))
        return false;
    P4RegisterKadanEditor();
    P4Kadan::UpdateReadOnlyAsset(BP);
    const bool Editable = P4Kadan::GetAssetPolicy(BP).AllowEdit;
    const auto Actions = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"))
                             .Get()
                             .GetAssetTypeActionsForClass(BP->GetClass())
                             .Pin();
    if (!TestTrue(TEXT("Normal Content Browser actions exist"), Actions.IsValid()))
        return false;
    Actions->OpenAssetEditor({BP});
    IAssetEditorInstance* Open = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(BP, false);
    if (!TestNotNull(TEXT("Normal browser action opens an editor"), Open))
        return false;
    if (!TestEqual(TEXT("Family row selects the actual editor route"), Open->GetEditorName(),
            FName(Editable ? TEXT("BlueprintEditor") : TEXT("KadanReference"))))
        return false;
    if (Editable)
    {
        auto* Editor = static_cast<FBlueprintEditor*>(Open);
        Editor->SetCurrentMode(FBlueprintEditorApplicationModes::StandardBlueprintEditorMode);
        if (UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(BP))
            FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(Graph);
        Editor->StartEditingDefaults();
    }
    IFileManager::Get().MakeDirectory(*Folder, true);
    ADD_LATENT_AUTOMATION_COMMAND(FGameBlueprintEditorCapture(this, BP, Folder, Editable));
    return true;
}
#endif

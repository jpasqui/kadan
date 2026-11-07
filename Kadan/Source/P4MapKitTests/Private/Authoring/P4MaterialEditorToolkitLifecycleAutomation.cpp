#include "P4CreatorMaterialFactories.h"
#include "P4CreatorSource.h"
#include "P4KadanAssetPolicy.h"
#include "P4NativeCustomCompilerBridge.h"
#include "P4NativeHash.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/InputBindingManager.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "IMaterialEditor.h"
#include "IAssetTools.h"
#include "Layout/Children.h"
#include "MaterialEditor/PreviewMaterial.h"
#include "MaterialEditorModule.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectHash.h"
#include "Widgets/Docking/SDockTab.h"

namespace
{
struct FMaterialToolkitLifecycle
{
    FAutomationTestBase& Test;
    TStrongObjectPtr<UPackage> Package;
    TSharedPtr<IMaterialEditor> Editor;
    TSharedPtr<const FUICommandInfo> Apply, Save;
    FString File, SavedSha;
    int64 SavedBytes = 0;
    int32 Phase = 0, Applied = 0;
    bool Saved = false, Reopened = false, Passed = false, ScriptSaveMode = false, DirtyBeforeSave = false;
    double Next = 0, Started = FPlatformTime::Seconds();
    explicit FMaterialToolkitLifecycle(FAutomationTestBase& In) : Test(In) {}
    UMaterial* Original() const
    {
        return Package.IsValid() ? FindObjectFast<UMaterial>(Package.Get(), TEXT("M_Control")) : nullptr;
    }
    UMaterial* Preview() const { return Editor ? Cast<UMaterial>(Editor->GetMaterialInterface()) : nullptr; }
    bool Open()
    {
        Editor = FModuleManager::LoadModuleChecked<IMaterialEditorModule>(TEXT("MaterialEditor"))
                     .CreateMaterialEditor(EToolkitMode::Standalone, nullptr, Original());
        Next = FPlatformTime::Seconds() + 0.7; // Allow the ordinary policy ticker to attach/reset its readback.
        return Editor.IsValid();
    }
    bool Check(const TCHAR* Stage)
    {
        const FString Prefix = FString(Stage) + TEXT(": ");
        const auto* Objects = Editor->GetObjectsCurrentlyBeingEdited();
        if (!Test.TestTrue(Prefix + TEXT("stock toolkit exposes asset, preview and preview parameters"),
                Objects && Objects->Num() == 3) ||
            !Test.TestTrue(Prefix + TEXT("real material is an edited native asset"),
                IsValid(Original()) && Original()->IsAsset() && Objects->Contains(Original())) ||
            !Test.TestTrue(Prefix + TEXT("stock preview is edited but is not an asset"),
                IsValid(Preview()) && Preview()->GetClass() == UPreviewMaterial::StaticClass() &&
                    !Preview()->IsAsset() && Objects->Contains(Preview())) ||
            !Test.TestTrue(Prefix + TEXT("real creator provenance survives"), P4HasCreatorSource(Original())) ||
            !Test.TestTrue(Prefix + TEXT("live preview graph has no false origin warning"),
                P4CreatorMaterialEditorGraphIssue(Preview()).IsEmpty()))
            return false;
        const auto Policy = P4Kadan::GetAssetPolicy(Original());
        if (!Test.TestTrue(Prefix + TEXT("real asset policy remains editable"),
                Policy.AllowEdit && Policy.Reason.IsEmpty() && Policy.Issues.Num() == 0) ||
            !Test.TestTrue(Prefix + TEXT("guarded stock Save remains executable"),
                Save.IsValid() && Editor->GetToolkitCommands()->CanExecuteAction(Save.ToSharedRef())))
            return false;
        const auto Manager = Editor->GetAssociatedTabManager();
        if (!Test.TestTrue(Prefix + TEXT("actual toolkit tab manager is alive"), Manager.IsValid()))
            return false;
        const auto Tab = Manager->FindExistingLiveTab(FTabId(FName(TEXT("MaterialEditor_GraphCanvas"))));
        if (!Test.TestTrue(Prefix + TEXT("actual graph tab is live"), Tab.IsValid()))
            return false;
        const auto Content = Tab->GetContent();
        const auto* Children = Content->GetChildren();
        return Test.TestTrue(Prefix + TEXT("actual policy wrapper body remains enabled"),
            Content->GetTypeAsString() == TEXT("SVerticalBox") && Children && Children->Num() == 2 &&
                Children->GetChildAt(1)->IsEnabled());
    }
    bool Edit(const FLinearColor& Value)
    {
        auto* Color = Preview() ? Cast<UMaterialExpressionConstant3Vector>(Preview()->BaseColor.Expression) : nullptr;
        if (!Test.TestNotNull(TEXT("The live preview retains the actual constant-color graph"), Color))
            return false;
        Color->Constant = Value;
        Editor->UpdateMaterialAfterGraphChange();
        Next = FPlatformTime::Seconds() + 0.7;
        return true;
    }
    bool ApplyColor(const FLinearColor& Expected)
    {
        if (!Test.TestTrue(TEXT("Guarded real Apply is executable after an unsaved graph edit"),
                Apply.IsValid() && Editor->GetToolkitCommands()->CanExecuteAction(Apply.ToSharedRef())) ||
            !Test.TestTrue(TEXT("Ordinary material-editor Apply executes"),
                Editor->GetToolkitCommands()->TryExecuteAction(Apply.ToSharedRef())))
            return false;
        ++Applied;
        auto* Color = Original() ? Cast<UMaterialExpressionConstant3Vector>(Original()->BaseColor.Expression) : nullptr;
        return Test.TestTrue(
            TEXT("Apply preserves the edited graph on the real material"), Color && Color->Constant == Expected);
    }
    ~FMaterialToolkitLifecycle()
    {
        if (Package.IsValid())
            Package->SetDirtyFlag(false);
        if (Editor)
        {
            Editor->CloseWindow();
            Editor.Reset();
        }
        bool Removed = true;
        if (Saved && IFileManager::Get().FileExists(*File))
            Removed = IFileManager::Get().Delete(*File, false, false);
        Test.TestTrue(TEXT("Only the controlled new material file is removed after the lifecycle gate"),
            Removed && (!Saved || !IFileManager::Get().FileExists(*File)));
        if (UMaterial* Asset = Original())
            FAssetRegistryModule::AssetDeleted(Asset);
        if (Package.IsValid())
        {
            TArray<UObject*> Objects;
            GetObjectsWithOuter(Package.Get(), Objects, true);
            for (UObject* Object : Objects)
            {
                Object->ClearFlags(RF_Public | RF_Standalone);
                Object->SetFlags(RF_Transient);
            }
            Package->SetDirtyFlag(false);
            Package->SetFlags(RF_Transient);
        }
        auto Json = MakeShared<FJsonObject>();
        Json->SetBoolField(TEXT("passed"), Passed && !Test.HasAnyErrors());
        Json->SetNumberField(TEXT("apply_count"), Applied);
        Json->SetBoolField(TEXT("actual_save"), Saved);
        Json->SetBoolField(TEXT("toolkit_reopened"), Reopened);
        Json->SetBoolField(TEXT("stock_unattended_script_save_mode"), ScriptSaveMode);
        Json->SetBoolField(TEXT("dirty_before_save"), DirtyBeforeSave);
        Json->SetStringField(TEXT("saved_sha256"), SavedSha);
        Json->SetNumberField(TEXT("saved_bytes"), SavedBytes);
        Json->SetBoolField(TEXT("owned_file_removed"), Removed);
        Json->SetStringField(TEXT("scope"),
            TEXT(
                "Actual stock toolkit open/Apply/reapply/Save/reopen; controlled creator graph only, no game acquisition/export/cold-process claim."));
        FString Text;
        const FString Out = FPaths::ProjectSavedDir() / TEXT("P4MapKit/MaterialEditorLifecycle/actual-toolkit.json");
        Test.TestTrue(TEXT("Small toolkit lifecycle proof retained"),
            FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text)) &&
                IFileManager::Get().MakeDirectory(*FPaths::GetPath(Out), true) &&
                FFileHelper::SaveStringToFile(Text, *Out, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    }
};
class FMaterialToolkitLifecycleCommand final : public IAutomationLatentCommand
{
    TSharedRef<FMaterialToolkitLifecycle> State;

public:
    explicit FMaterialToolkitLifecycleCommand(TSharedRef<FMaterialToolkitLifecycle> In) : State(In) {}
    bool Update() override
    {
        auto& S = State.Get();
        if (FPlatformTime::Seconds() - S.Started > 90)
        {
            S.Test.AddError(TEXT("Owned material toolkit lifecycle exceeded 90 seconds"));
            return true;
        }
        if (FPlatformTime::Seconds() < S.Next)
            return false;
        if (S.Phase == 0)
        {
            if (!S.Check(TEXT("Initial open")) || !S.Edit(FLinearColor(0.1f, 0.35f, 0.9f)))
                return true;
            ++S.Phase;
            return false;
        }
        if (S.Phase == 1)
        {
            if (!S.ApplyColor(FLinearColor(0.1f, 0.35f, 0.9f)) || !S.Check(TEXT("First Apply")) ||
                !S.Edit(FLinearColor(0.75f, 0.2f, 0.15f)))
                return true;
            ++S.Phase;
            return false;
        }
        if (S.Phase == 2)
        {
            if (!S.ApplyColor(FLinearColor(0.75f, 0.2f, 0.15f)) || !S.Check(TEXT("Second Apply")))
                return true;
            S.DirtyBeforeSave = S.Package->IsDirty();
            if (!S.Test.TestTrue(TEXT("Real AssetTools creation left this edited package dirty"), S.DirtyBeforeSave))
                return true;
            const bool PriorScriptMode = GIsRunningUnattendedScript;
            {
                // Stock PromptForCheckoutAndSave cancels in -unattended mode.
                // Use its supported script branch while executing the SAME
                // guarded toolkit command, never a direct SavePackage fallback.
                TGuardValue<bool> ScriptSave(GIsRunningUnattendedScript, true);
                S.ScriptSaveMode = true;
                if (!S.Test.TestTrue(TEXT("Guarded ordinary Save executes"),
                        S.Editor->GetToolkitCommands()->TryExecuteAction(S.Save.ToSharedRef())))
                    return true;
            }
            if (!S.Test.TestEqual(TEXT("Only the Save command owns unattended-script mode"), GIsRunningUnattendedScript,
                    PriorScriptMode))
                return true;
            S.Saved = IFileManager::Get().FileExists(*S.File);
            if (!S.Test.TestTrue(TEXT("The actual toolkit saved the controlled native asset"),
                    S.Saved && P4HashFile(S.File, 1024 * 1024, S.SavedBytes, S.SavedSha)) ||
                !S.Test.TestTrue(TEXT("Save clears actual package dirtiness"), !S.Package->IsDirty()) ||
                !S.Test.TestTrue(TEXT("Owned toolkit closes after Save"), S.Editor->CloseWindow()))
                return true;
            S.Editor.Reset();
            S.Reopened = S.Open();
            ++S.Phase;
            return false;
        }
        if (!S.Check(TEXT("Reopened toolkit")))
            return true;
        FString Now;
        int64 Bytes = 0;
        S.Passed = S.Test.TestTrue(TEXT("Reopening the editor changes no saved graph bytes"),
            P4HashFile(S.File, 1024 * 1024, Bytes, Now) && Bytes == S.SavedBytes &&
                P4ExactNativeString(Now, S.SavedSha));
        return true;
    }
};
} //namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialEditorToolkitLifecycleTest, "P4MapKit.Kadan.MaterialEditorToolkitLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialEditorToolkitLifecycleTest::RunTest(const FString&)
{
    if (IsRunningCommandlet() || !GEditor || !FApp::CanEverRender() || !FSlateApplication::IsInitialized())
    {
        AddError(TEXT("MaterialEditorToolkitLifecycle requires a rendering Slate editor"));
        return false;
    }
    auto State = MakeShared<FMaterialToolkitLifecycle>(*this);
    const FString Name = TEXT("/Game/Mods/__P4ToolkitLifecycle_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) +
        TEXT("/M_Control");
    State->File = FPackageName::LongPackageNameToFilename(Name, FPackageName::GetAssetPackageExtension());
    if (!TestFalse(TEXT("Controlled material destination is new"), IFileManager::Get().FileExists(*State->File)))
        return false;
    TStrongObjectPtr<UP4CreatorMaterialFactory> Factory(NewObject<UP4CreatorMaterialFactory>());
    // Match ordinary Browser creation, including registry publication and
    // package dirtiness supplied by AssetTools after the genuine constructor.
    UMaterial* Material = Cast<UMaterial>(FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"))
            .Get()
            .CreateAsset(
                TEXT("M_Control"), FPackageName::GetLongPackagePath(Name), UMaterial::StaticClass(), Factory.Get()));
    if (!TestNotNull(TEXT("Real AssetTools and Kadan factory create the new lifecycle asset"), Material))
        return false;
    State->Package.Reset(Material->GetOutermost());
    if (!TestTrue(TEXT("Normal creator publication marks the new package dirty"), State->Package->IsDirty()))
        return false;
    if (!TestTrue(TEXT("Untouched creator defaults are supported opaque Default Lit settings"),
            Material->MaterialDomain == MD_Surface && Material->BlendMode == BLEND_Opaque &&
                Material->GetShadingModels().HasOnlyShadingModel(MSM_DefaultLit) && !Material->TwoSided) ||
        !TestTrue(TEXT("Creator factory supplies supported Color Roughness decal response"),
            Material->MaterialDecalResponse == MDR_ColorRoughness))
        return false;
    auto* Color = NewObject<UMaterialExpressionConstant3Vector>(Material);
    Color->Material = Material;
    Color->Constant = FLinearColor(0.9f, 0.2f, 0.15f);
    auto* Roughness = NewObject<UMaterialExpressionConstant>(Material);
    Roughness->Material = Material;
    Roughness->R = 0.6f;
    Material->Expressions = {Color, Roughness};
    Material->BaseColor.Expression = Color;
    Material->Roughness.Expression = Roughness;
    // Exercise the policy installed by ordinary module startup, without a
    // test-owned initializer that could conceal a broken editor lifecycle.
    if (!TestTrue(TEXT("Actual stock editor opens the genuine creator graph"), State->Open()))
        return false;
    State->Apply = FInputBindingManager::Get().FindCommandInContext(TEXT("MaterialEditor"), TEXT("Apply"));
    State->Save = FInputBindingManager::Get().FindCommandInContext(TEXT("AssetEditor"), TEXT("SaveAsset"));
    ADD_LATENT_AUTOMATION_COMMAND(FMaterialToolkitLifecycleCommand(State));
    return true;
}
#endif

#include "P4KadanCreatorFlowStageList.h"
#include "P4KadanCreatorFlowShared.h"

#if P4_MAPKIT_TESTS
namespace P4KadanCreatorFlow
{

namespace
{
// MaterialBefore, Material and MaterialReopen: Game settings in the real Material Details panels of a creator
// material, its inherited instance and a protected game master.
class FMaterialStage final : public FFlowStage
{
public:
    using FFlowStage::FFlowStage;

    void AddSteps() override;

private:
    // Labels, Advanced values, search, a checkbox edit with Undo, Apply and Save.
    void AddEditSteps();

    // The uncompiled clipping setting and its help link.
    void AddClippingSteps();

    // The inherited instance and its parent link.
    void AddInstanceSteps();

    // A protected game master opens read-only and unchanged.
    void AddProtectedSteps();

    bool MaterialDetailsFixture(bool Retained);

    bool MaterialClickTagged(const FName Tag);

    UMaterial* MaterialPreview();

    bool MaterialFlag(bool Value);

    bool MaterialToolbar(const FString& Caption, bool Save);

    bool ProtectedDetailsUnchanged();

    FFlowAssets Assets{Flow};
    FMaterialDetails Details{Flow};
    TWeakObjectPtr<UMaterial> DetailsMaterial;
    TWeakObjectPtr<UMaterialInstanceConstant> DetailsInstance;
    TWeakObjectPtr<UMaterial> DetailsProtectedMaterial;
    FString ProtectedMaterialFile;
    FString ProtectedMaterialSha;
    bool ProtectedMaterialDirty = false;
    TArray<TPair<FString, FString>> ProtectedMaterialDefines;
};
} // namespace

void FMaterialStage::AddSteps()
{
    Flow.Add(
        TEXT("Prepare native creator, inherited instance and protected game material fixtures"),
        [this]() { return MaterialDetailsFixture(Flow.Stage == TEXT("MaterialReopen")); }, 180);
    Flow.Add(
        TEXT("Open the material through its production asset action"),
        [this]() { return Details.OpenMaterialDetails(DetailsMaterial.Get()); }, 120);
    Flow.Add(TEXT("Search the actual Material Details panel"), [this]()
        { return Details.MaterialSearch(Flow.Stage == TEXT("MaterialBefore") ? TEXT("C4") : TEXT("Game settings")); });
    Flow.Add(TEXT("Capture the creator Material Details panel"), [this]()
        { return Details.MaterialCapture(Flow.Stage == TEXT("MaterialReopen") ? TEXT("reopened") : TEXT("creator")); });
    if (Flow.Stage == TEXT("MaterialReopen"))
    {
        Flow.Add(
            TEXT("The cold-loaded preview retains the saved game setting"), [this]() { return MaterialFlag(true); });
        Flow.Add(TEXT("Close only this owned material editor"),
            [this]()
            {
                Details.CloseMaterialDetails(DetailsMaterial.Get());
                return true;
            });
        return;
    }
    AddEditSteps();
    AddClippingSteps();
    AddInstanceSteps();
    AddProtectedSteps();
}

void FMaterialStage::AddEditSteps()
{
    if (Flow.Stage == TEXT("Material"))
    {
        Flow.Add(TEXT("The supported label and uncompiled clipping status are visible"),
            [this]()
            {
                const auto Panel = Details.DetailsTab.Pin();
                return Flow.Expect(Panel && Text(Panel->GetContent(), TEXT("Capsule shadow scale")).IsValid() &&
                        Details.MaterialTagged(TEXT("KadanMaterialClipping")).IsValid(),
                    TEXT("Friendly labels or clipping status are missing"));
            });
        Flow.Add(TEXT("Reveal Advanced values through the actual Details search"),
            [this]() { return Details.MaterialSearch(TEXT("Unknown")); });
        Flow.Add(TEXT("Capture Advanced values with their unknown-effect guidance"),
            [this]()
            {
                return Flow.Expect(Details.MaterialTagged(TEXT("KadanMaterial.C4ShaderParam3")).IsValid() &&
                               Details.MaterialTagged(TEXT("KadanMaterialUnknown")).IsValid(),
                           TEXT("Advanced values have no visible unknown-effect guidance")) &&
                    Details.MaterialCapture(TEXT("advanced"));
            });
        Flow.Add(TEXT("Search by a preserved serialized property name"),
            [this]() { return Details.MaterialSearch(TEXT("C4ShaderParam0")); });
        Flow.Add(TEXT("Capture serialized-name search"),
            [this]()
            {
                return Flow.Expect(Details.MaterialTagged(TEXT("KadanMaterial.C4ShaderParam0")).IsValid(),
                           TEXT("Serialized property names are no longer searchable")) &&
                    Details.MaterialCapture(TEXT("search"));
            });
        Flow.Add(TEXT("Reveal the supported setting in the actual Details search"),
            [this]() { return Details.MaterialSearch(TEXT("Skip fully transparent pixels")); });
        Flow.Add(TEXT("Change the supported transparent-pixel setting through its actual checkbox"),
            [this]() { return MaterialClickTagged(TEXT("KadanMaterial.bC4MaterialParam10")); });
        Flow.Add(TEXT("The real checkbox changed the preview setting"), [this]() { return MaterialFlag(true); });
        Flow.Add(TEXT("Undo the actual property edit through the keyboard"),
            [this]()
            {
                const FModifierKeysState Control(false, false, true, false, false, false, false, false, false);
                const FKeyEvent Undo(EKeys::Z, Control, 0, false, 0, 0);
                FSlateApplication::Get().ProcessKeyDownEvent(Undo);
                FSlateApplication::Get().ProcessKeyUpEvent(Undo);
                return true;
            });
        Flow.Add(TEXT("Undo restored the preceding game setting"), [this]() { return MaterialFlag(false); });
        Flow.Add(TEXT("Edit the setting again through its checkbox"),
            [this]() { return MaterialClickTagged(TEXT("KadanMaterial.bC4MaterialParam10")); });
        Flow.Add(TEXT("The second edit changes the current preview setting"), [this]() { return MaterialFlag(true); });
        Flow.Add(
            TEXT("Apply through the stock material toolbar"),
            [this]() { return MaterialToolbar(TEXT("Apply"), false); }, 120);
        Flow.Add(TEXT("Apply transferred the setting to the source material"),
            [this]()
            {
                // Stock Apply reconstructs the source UObject in place. Resolve its
                // current identity instead of retaining the preceding weak object.
                DetailsMaterial = FindObject<UMaterial>(
                    nullptr, *(Flow.BNames.PackageRoot / TEXT("UXMaterial.UXMaterial")));
                const auto* Values = UP4MaterialGameSettings::Find(DetailsMaterial.Get());
                UE_LOG(LogTemp, Display, TEXT("Kadan material Apply source %s settings %s transparent %d"),
                    *GetPathNameSafe(DetailsMaterial.Get()), *GetPathNameSafe(Values),
                    Values ? int32(Values->bC4MaterialParam10) : -1);
                return Flow.Expect(
                    Values && Values->bC4MaterialParam10, TEXT("Apply lost the edited material game setting"));
            });
        Flow.Add(
            TEXT("Save through the stock material toolbar"), [this]() { return MaterialToolbar(TEXT("Save"), true); },
            120);
        Flow.Add(TEXT("The material's saved package is clean"),
            [this]()
            {
                return Flow.Expect(
                    !DetailsMaterial->GetOutermost()->IsDirty(), TEXT("The stock material Save did not finish"));
            });
        Flow.Add(TEXT("Capture the applied and saved game setting"),
            [this]() { return Details.MaterialCapture(TEXT("saved")); });
    }
}

void FMaterialStage::AddClippingSteps()
{
    Flow.Add(TEXT("Reveal the scene clipping setting"),
        [this]()
        {
            return Details.MaterialSearch(
                Flow.Stage == TEXT("MaterialBefore") ? TEXT("C4MaterialParam3") : TEXT("Scene clipping"));
        });
    Flow.Add(TEXT("Capture the clipping setting and its support status"),
        [this]() { return Details.MaterialCapture(TEXT("clipping")); });
    if (Flow.Stage == TEXT("Material"))
    {
        Flow.Add(TEXT("The uncompiled setting is disabled in its actual Slate hierarchy"),
            [this]()
            {
                const auto Widget = Details.MaterialTagged(TEXT("KadanMaterial.C4MaterialParam3"));
                FWidgetPath Path;
                if (!Flow.Expect(
                        Widget && FSlateApplication::Get().GeneratePathToWidgetUnchecked(Widget.ToSharedRef(), Path),
                        TEXT("The clipping setting is outside the actual Slate hierarchy")))
                {
                    return true;
                }
                bool Enabled = true;
                for (int32 PathIndex = 0; PathIndex < Path.Widgets.Num(); ++PathIndex)
                {
                    Enabled &= Path.Widgets[PathIndex].Widget->IsEnabled();
                }
                return Flow.Expect(!Enabled, TEXT("The uncompiled clipping option is editable"));
            });
        Flow.Add(TEXT("Restore the category for its help link"),
            [this]() { return Details.MaterialSearch(TEXT("Game settings")); });
        Flow.Add(TEXT("Open troubleshooting through the actual Details link"),
            [this]() { return MaterialClickTagged(TEXT("KadanMaterialHelp")); });
        Flow.Add(TEXT("Capture and close only the troubleshooting window opened by this fixture"),
            [this]()
            {
                for (const auto& Window : FSlateApplication::Get().GetInteractiveTopLevelWindows())
                {
                    if (Window->GetTitle().ToString() == TEXT("Material troubleshooting"))
                    {
                        const bool Captured = Details.MaterialCapture(TEXT("help"), Window);
                        Window->RequestDestroyWindow();
                        return Captured;
                    }
                }
                return false;
            });
    }
}

void FMaterialStage::AddInstanceSteps()
{
    Flow.Add(TEXT("Close only the owned creator material editor"),
        [this]()
        {
            Details.CloseMaterialDetails(DetailsMaterial.Get());
            return true;
        });
    Flow.Add(
        TEXT("Open the inherited material instance through its asset action"),
        [this]() { return Details.OpenMaterialDetails(DetailsInstance.Get()); }, 120);
    Flow.Add(TEXT("Search the instance's actual Details panel"), [this]()
        { return Details.MaterialSearch(Flow.Stage == TEXT("MaterialBefore") ? TEXT("C4") : TEXT("Game settings")); });
    Flow.Add(TEXT("Capture the inherited instance Details panel"),
        [this]() { return Details.MaterialCapture(TEXT("instance")); });
    if (Flow.Stage == TEXT("Material"))
    {
        Flow.Add(TEXT("Reveal the stock instance parent control"),
            [this]() { return Details.MaterialSearch(TEXT("Parent")); });
        Flow.Add(TEXT("The inherited section preserves Unreal's parent control"),
            [this]()
            {
                const auto Panel = Details.DetailsTab.Pin();
                return Flow.Expect(Panel && Text(Panel->GetContent(), TEXT("Parent")).IsValid(),
                           TEXT("The game settings extension replaced Unreal's instance controls")) &&
                    Details.MaterialCapture(TEXT("instance-parent"));
            });
        Flow.Add(TEXT("Open the parent through the instance's actual link"),
            [this]() { return MaterialClickTagged(TEXT("KadanMaterialParent")); });
        Flow.Add(TEXT("The instance link opened its actual parent"),
            [this]()
            {
                return Flow.Expect(GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(
                                       DetailsMaterial.Get(), false) != nullptr,
                    TEXT("The instance's parent link did not open its parent material"));
            });
        Flow.Add(TEXT("Close only the parent opened by this fixture"),
            [this]()
            {
                Details.CloseMaterialDetails(DetailsMaterial.Get());
                return true;
            });
    }
    Flow.Add(TEXT("Close only the owned instance editor"),
        [this]()
        {
            Details.CloseMaterialDetails(DetailsInstance.Get());
            return true;
        });
}

void FMaterialStage::AddProtectedSteps()
{
    Flow.Add(
        TEXT("Open the protected imported master through its normal reference route"),
        [this]() { return Details.OpenMaterialDetails(DetailsProtectedMaterial.Get()); }, 120);
    Flow.Add(TEXT("Search the protected material's Details panel"), [this]()
        { return Details.MaterialSearch(Flow.Stage == TEXT("MaterialBefore") ? TEXT("C4") : TEXT("Game settings")); });
    Flow.Add(TEXT("Capture the protected imported master Details panel"),
        [this]() { return Details.MaterialCapture(TEXT("protected")); });
    Flow.Add(TEXT("Reading protected Details preserves source bytes and values"),
        [this]() { return ProtectedDetailsUnchanged(); });
    Flow.Add(TEXT("Close only the reference editor opened by this fixture"),
        [this]()
        {
            Details.CloseMaterialDetails(DetailsProtectedMaterial.Get());
            return true;
        });
}

bool FMaterialStage::MaterialDetailsFixture(bool Retained)
{
    if (!Retained && !Assets.MapProgressFixture(false))
    {
        return false;
    }
    const FString Package = Flow.BNames.PackageRoot / TEXT("UXMaterial");
    UMaterial* Material = LoadObject<UMaterial>(nullptr, *(Package + TEXT(".UXMaterial")), nullptr, LOAD_NoWarn);
    if (!Material && !Retained)
    {
        auto* Factory = NewObject<UP4CreatorMaterialFactory>();
        Material = Cast<UMaterial>(Factory->FactoryCreateNew(UMaterial::StaticClass(), CreatePackage(*Package),
            TEXT("UXMaterial"), RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
    }
    if (!Flow.Expect(Material && P4HasCreatorSource(Material), TEXT("The owned creator material is unavailable")))
    {
        return false;
    }
    DetailsMaterial = Material;
    if (Retained)
    {
        const auto* Values = UP4MaterialGameSettings::Find(Material);
        return Flow.Expect(Values && Values->bC4MaterialParam10,
            TEXT("The cold-loaded material did not retain the saved game setting"));
    }
    auto* Settings = UP4MaterialGameSettings::FindOrAdd(Material);
    Settings->bC4MaterialParam10 = false;
    Settings->Publish();
    if (!Assets.SaveIssueFixture(Material))
    {
        return false;
    }
    const FString InstancePackage = Flow.BNames.PackageRoot / TEXT("UXMaterialInstance");
    UMaterialInstanceConstant* Instance = LoadObject<UMaterialInstanceConstant>(
        nullptr, *(InstancePackage + TEXT(".UXMaterialInstance")), nullptr, LOAD_NoWarn);
    if (!Instance)
    {
        auto* Factory = NewObject<UP4CreatorMaterialInstanceFactory>();
        Factory->InitialParent = Material;
        Instance = Cast<UMaterialInstanceConstant>(
            Factory->FactoryCreateNew(UMaterialInstanceConstant::StaticClass(), CreatePackage(*InstancePackage),
                TEXT("UXMaterialInstance"), RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
    }
    if (!Flow.Expect(
            Instance && Instance->Parent == Material, TEXT("The owned inherited material instance is unavailable")) ||
        !Assets.SaveIssueFixture(Instance))
    {
        return false;
    }
    DetailsInstance = Instance;
    UWorld* World = GEditor->GetEditorWorldContext().World();
    for (TActorIterator<AActor> It(World); It && !DetailsProtectedMaterial.IsValid(); ++It)
    {
        TInlineComponentArray<UStaticMeshComponent*> Parts(*It);
        for (const auto* Part : Parts)
        {
            for (int32 MaterialSlot = 0; MaterialSlot < Part->GetNumMaterials(); ++MaterialSlot)
            {
                auto* Interface = Part->GetMaterial(MaterialSlot);
                UMaterial* Candidate = Interface ? Interface->GetMaterial() : nullptr;
                FP4OriginalRecord Record;
                FString Error;
                if (Candidate && P4ReadOriginalRecord(Candidate, Record, Error) &&
                    !P4Kadan::GetAssetPolicy(Candidate).AllowEdit)
                {
                    DetailsProtectedMaterial = Candidate;
                    break;
                }
            }
        }
    }
    UMaterial* Protected = DetailsProtectedMaterial.Get();
    if (!Flow.Expect(Protected != nullptr, TEXT("The imported map has no protected recovered master material")))
    {
        return false;
    }
    ProtectedMaterialFile = FPackageName::LongPackageNameToFilename(
        Protected->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    int64 Bytes = 0;
    if (!Flow.Expect(P4HashFile(ProtectedMaterialFile, 64 * 1024 * 1024, Bytes, ProtectedMaterialSha),
            TEXT("The protected source material could not be hashed")))
    {
        return false;
    }
    ProtectedMaterialDirty = Protected->GetOutermost()->IsDirty();
    if (const auto* Values = UP4MaterialGameSettings::Find(Protected))
    {
        Values->Defines(ProtectedMaterialDefines);
    }
    return true;
}

bool FMaterialStage::MaterialClickTagged(const FName Tag)
{
    const auto Widget = Details.MaterialTagged(Tag);
    if (!Details.MaterialPrepare(Widget))
    {
        return false;
    }
    const auto Check = Widget ? Find(Widget.ToSharedRef(), [](const TSharedRef<SWidget>& Candidate)
                                    { return Candidate->GetType() == TEXT("SCheckBox"); })
                              : nullptr;
    return Flow.MouseClick(Check ? Check : Widget);
}

UMaterial* FMaterialStage::MaterialPreview()
{
    auto* Instance = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(
        DetailsMaterial.Get(), false);
    return Instance && Instance->GetEditorName() == TEXT("MaterialEditor")
        ? Cast<UMaterial>(static_cast<IMaterialEditor*>(Instance)->GetMaterialInterface())
        : nullptr;
}

bool FMaterialStage::MaterialFlag(bool Value)
{
    const auto* Settings = UP4MaterialGameSettings::Find(MaterialPreview());
    return Flow.Expect(Settings && Settings->bC4MaterialParam10 == Value,
        TEXT("The actual preview material did not retain the edited game setting"));
}

bool FMaterialStage::MaterialToolbar(const FString& Caption, bool Save)
{
    const auto Panel = Details.DetailsTab.Pin();
    const auto Window = Panel ? FSlateApplication::Get().FindWidgetWindow(Panel->GetContent()) : nullptr;
    const auto Button = Window
        ? Find(Window.ToSharedRef(), [&](const TSharedRef<SWidget>& Widget)
              { return Widget->GetType() == TEXT("SButton") && Text(Widget, Caption).IsValid(); })
        : nullptr;
    if (!Details.MaterialPrepare(Button))
    {
        return false;
    }
    // The stock Save route requires this existing script mode in unattended tests.
    TGuardValue<bool> ScriptSave(GIsRunningUnattendedScript, Save || GIsRunningUnattendedScript);
    return Flow.MouseClick(Button);
}

bool FMaterialStage::ProtectedDetailsUnchanged()
{
    UMaterial* Material = DetailsProtectedMaterial.Get();
    FString Sha;
    int64 Bytes = 0;
    TArray<TPair<FString, FString>> Defines;
    if (const auto* Values = UP4MaterialGameSettings::Find(Material))
    {
        Values->Defines(Defines);
    }
    return Flow.Expect(Material && P4HashFile(ProtectedMaterialFile, 64 * 1024 * 1024, Bytes, Sha) &&
            Sha == ProtectedMaterialSha && Material->GetOutermost()->IsDirty() == ProtectedMaterialDirty &&
            Defines == ProtectedMaterialDefines,
        TEXT("Opening protected Material Details changed saved values or source bytes"));
}

TUniquePtr<FFlowStage> MakeMaterialStage(FFlow& Flow)
{
    return MakeUnique<FMaterialStage>(Flow);
}
} // namespace P4KadanCreatorFlow
#endif

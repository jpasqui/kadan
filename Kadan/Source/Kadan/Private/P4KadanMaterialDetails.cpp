#include "P4KadanMaterialDetails.h"
#include "P4KadanAssetPolicy.h"
#include "P4KadanReadOnly.h"
#include "P4MaterialGameSettings.h"
#include "AssetToolsModule.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailCustomization.h"
#include "IDetailPropertyRow.h"
#include "IAssetTools.h"
#include "IAssetTypeActions.h"
#include "IMaterialEditor.h"
#include "MaterialEditor/MaterialEditorInstanceConstant.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
UMaterial* PolicySource(UMaterial* Material)
{
    if (Material->IsAsset() || !GEditor)
    {
        return Material;
    }
    auto* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    for (UObject* Asset : Editors->GetAllEditedAssets())
    {
        UMaterial* Original = Cast<UMaterial>(Asset);
        IAssetEditorInstance* Editor = Original ? Editors->FindEditorForAsset(Original, false) : nullptr;
        if (Editor && Editor->GetEditorName() == FName(TEXT("MaterialEditor")) &&
            static_cast<IMaterialEditor*>(Editor)->GetMaterialInterface() == Material)
        {
            return Original;
        }
    }
    return Material;
}

TSharedRef<SWidget> Message(const FString& Value, const FName Tag = NAME_None)
{
    return SNew(STextBlock).Tag(Tag).AutoWrapText(true).Text(FText::FromString(Value));
}

void Troubleshooting()
{
    auto Window =
        SNew(SWindow)
            .Title(FText::FromString(TEXT("Material troubleshooting")))
            .ClientSize(FVector2D(650, 280))
            .SupportsMaximize(false)
            .SupportsMinimize(false)[SNew(SBorder)
                    .BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
                    .Padding(16)[SNew(SVerticalBox) +
                        SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)[Message(TEXT(
                            "Check the mesh's UV channels and texture scale, then check its normals and tangents in the modeling tool."))] +
                        SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)[Message(TEXT(
                            "Check whether the material reads vertex colors, world position, or additional UV channels. Inspect the material inputs and the scene's lighting separately."))] +
                        SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)[Message(TEXT(
                            "Apply and save the material, export the mod, and verify it in the game. A dark or stretched patch does not identify its cause."))]]];
    FSlateApplication::Get().AddWindow(Window);
}

void Note(IDetailCategoryBuilder& Category, const FString& Text, const FName Tag, bool Advanced = false)
{
    FString Search = TEXT("Game settings C4 ");
    if (Advanced)
    {
        Search += TEXT("Advanced unknown ");
    }
    Search += Text;
    Category.AddCustomRow(FText::FromString(Search), Advanced).WholeRowContent()[Message(Text, Tag)];
}

class FMaterialDetails final : public IDetailCustomization
{
public:
    void CustomizeDetails(IDetailLayoutBuilder& Builder) override
    {
        TArray<TWeakObjectPtr<UObject>> Objects;
        Builder.GetObjectsBeingCustomized(Objects);
        if (!Objects.Num())
        {
            return;
        }
        for (auto& Object : Objects)
        {
            if (const auto* Proxy = Cast<UMaterialEditorInstanceConstant>(Object.Get()))
            {
                Object = Proxy->SourceInstance;
            }
            if (!Cast<UMaterialInterface>(Object.Get()))
            {
                return;
            }
        }
        IDetailCategoryBuilder& Category = Builder.EditCategory(
            TEXT("C4"), FText::FromString(TEXT("Game settings")), ECategoryPriority::Important);
        Category.InitiallyCollapsed(false);
        Category.AddCustomRow(FText::FromString(TEXT("Material troubleshooting UV normals inputs lighting game")))
            .WholeRowContent()[SNew(SHyperlink)
                    .Tag(TEXT("KadanMaterialHelp"))
                    .Text(FText::FromString(TEXT("Material troubleshooting")))
                    .OnNavigate_Static(&Troubleshooting)];
        if (Objects.Num() == 1)
        {
            if (UMaterialInstance* Instance = Cast<UMaterialInstance>(Objects[0].Get()))
            {
                Note(Category,
                    TEXT(
                        "Unchecked game settings inherit from the parent material. Enable an override to change its value."),
                    TEXT("KadanMaterialInheritance"));
                Note(Category,
                    TEXT(
                        "The effects of the values in Advanced are not known. Preserve imported values unless testing a specific change."),
                    TEXT("KadanMaterialUnknown"), true);
                const TWeakObjectPtr<UMaterialInterface> Parent(Instance->Parent);
                Category.AddCustomRow(FText::FromString(TEXT("Open parent Game settings C4")))
                    .WholeRowContent()[SNew(SHyperlink)
                            .Tag(TEXT("KadanMaterialParent"))
                            .IsEnabled(Parent.IsValid())
                            .Text(FText::FromString(TEXT("Open parent")))
                            .OnNavigate_Lambda(
                                [Parent]()
                                {
                                    if (GEditor && Parent.IsValid())
                                    {
                                        const auto Actions = FModuleManager::LoadModuleChecked<FAssetToolsModule>(
                                            TEXT("AssetTools"))
                                                                 .Get()
                                                                 .GetAssetTypeActionsForClass(Parent->GetClass())
                                                                 .Pin();
                                        if (Actions)
                                        {
                                            Actions->OpenAssetEditor({Parent.Get()});
                                        }
                                    }
                                })];
                return;
            }
        }
        bool ReadOnly = false;
        for (const auto& Object : Objects)
        {
            UMaterial* Material = Cast<UMaterial>(Object.Get());
            if (!Material || Material->HasAnyFlags(RF_ClassDefaultObject))
            {
                return;
            }
            UMaterial* Source = PolicySource(Material);
            ReadOnly |= P4Kadan::IsLockedObject(Source) || !P4Kadan::GetAssetPolicy(Source).AllowEdit;
        }
        TArray<UObject*> Settings;
        for (const auto& Object : Objects)
        {
            UMaterial* Material = CastChecked<UMaterial>(Object.Get());
            UP4MaterialGameSettings* Values = ReadOnly ? UP4MaterialGameSettings::Find(Material)
                                                       : UP4MaterialGameSettings::FindOrAdd(Material);
            if (!Values)
            {
                Note(Category,
                    TEXT(
                        "Imported game settings are unavailable for this material. Reimport the material to recover its saved values."),
                    TEXT("KadanMaterialUnavailable"));
                return;
            }
            Settings.Add(Values);
        }
        Note(Category,
            ReadOnly
                ? TEXT(
                      "This game material is read-only. Make a material instance or create a material in the mod's content folder to author changes.")
                : TEXT(
                      "Apply and save the material after changing its game settings. Verify the exported result in the game."),
            TEXT("KadanMaterialStatus"));
        Note(Category,
            TEXT("Kadan does not compile scene clipping for creator materials. The stored value is read-only."),
            TEXT("KadanMaterialClipping"));
        Note(Category,
            TEXT(
                "The effects of the values in Advanced are not known. Preserve imported values unless testing a specific change."),
            TEXT("KadanMaterialUnknown"), true);
        for (TFieldIterator<FProperty> It(UP4MaterialGameSettings::StaticClass(), EFieldIteratorFlags::ExcludeSuper);
            It; ++It)
        {
            if (!It->HasAnyPropertyFlags(CPF_Edit))
            {
                continue;
            }
            const bool Advanced = It->HasAnyPropertyFlags(CPF_AdvancedDisplay);
            IDetailPropertyRow* Row = Category.AddExternalObjectProperty(
                Settings, It->GetFName(), Advanced ? EPropertyLocation::Advanced : EPropertyLocation::Default);
            if (!Row)
            {
                continue;
            }
            const bool Compiled = It->GetFName() != GET_MEMBER_NAME_CHECKED(UP4MaterialGameSettings, C4MaterialParam3);
            Row->IsEnabled(!ReadOnly && Compiled);
            TSharedPtr<SWidget> Name;
            TSharedPtr<SWidget> Value;
            Row->GetDefaultWidgets(Name, Value, true);
            if (Name && Value)
            {
                Row->CustomWidget()
                    .NameContent()[SNew(SBox).Tag(
                        FName(*(TEXT("KadanMaterialName.") + It->GetName())))[Name.ToSharedRef()]]
                    .ValueContent()[SNew(SBox).Tag(
                        FName(*(TEXT("KadanMaterial.") + It->GetName())))[Value.ToSharedRef()]];
            }
        }
    }
};
} //namespace

TSharedRef<IDetailCustomization> P4KadanMaterialDetails()
{
    return MakeShared<FMaterialDetails>();
}

#include "P4KadanReadOnly.h"
#include "P4GamePaths.h"
#include "Misc/App.h"
#include "P4KadanAssetPolicy.h"
#include "P4CreatorMaterialFactories.h"
#include "P4GameReplacement.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/Texture2D.h"
#include "ToolMenuSection.h"
#include "ToolMenuEntry.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "IAssetTypeActions.h"
#include "AssetTypeActions_Base.h"
#include "AssetTypeCategories.h"
#include "Containers/Ticker.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/Texture.h"
#include "EditorStyleSet.h"
#include "EdGraph/EdGraph.h"
#include "Framework/Commands/InputBindingManager.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GraphEditor.h"
#include "IDetailsView.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "ContentBrowserModule.h"
#include "Widgets/SNullWidget.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/WindowsHWrapper.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace P4Kadan
{
namespace
{
const FName ReferenceTab(TEXT("Kadan_ReadOnlyReference"));

// A new toolkit owns read-only widgets. It does not alter graph bEditable flags,
// package dirty flags, properties or the caller's previously registered editor.
class FReferenceEditor final : public FAssetEditorToolkit
{
public:
    void Open(UObject* Asset, TSharedPtr<IToolkitHost> Host)
    {
        Object = Asset;
        FDetailsViewArgs Args;
        Args.bUpdatesFromSelection = false;
        Args.bLockable = false;
        Details = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(Args);
        Details->SetDisableCustomDetailLayouts(!Asset->IsA<UMaterialInterface>());
        Details->SetIsPropertyEditingEnabledDelegate(FIsPropertyEditingEnabled::CreateLambda([]() { return false; }));
        Details->SetObject(Asset);
        if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
        {
            TArray<UEdGraph*> Found;
            Blueprint->GetAllGraphs(Found);
            for (UEdGraph* Graph : Found)
                if (Graph)
                    Graphs.Add(MakeShared<TWeakObjectPtr<UEdGraph>>(Graph));
            if (Graphs.Num())
                Selected = Graphs[0];
        }
        const auto Layout =
            FTabManager::NewLayout(TEXT("KadanReference_v1"))
                ->AddArea(FTabManager::NewPrimaryArea()
                        ->SetOrientation(Orient_Vertical)
                        ->Split(
                            FTabManager::NewStack()->AddTab(ReferenceTab, ETabState::OpenedTab)->SetHideTabWell(true)));
        InitAssetEditor(Host.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone, Host,
            FName(TEXT("KadanReference")), Layout, false, false, Asset);
        // InitAssetEditor installs common Save and Save As commands even without a menu.
        // Slate treats an unmapped command as executable. Retain an explicit denied
        // action so keyboard and menu routing cannot inherit or enable that command.
        for (const TCHAR* Name : {TEXT("SaveAsset"), TEXT("SaveAssetAs"), TEXT("ReimportAsset")})
        {
            const auto Command = FInputBindingManager::Get().FindCommandInContext(TEXT("AssetEditor"), Name);
            if (Command)
            {
                GetToolkitCommands()->UnmapAction(Command);
                GetToolkitCommands()->MapAction(Command, FExecuteAction::CreateLambda([]() {}),
                    FCanExecuteAction::CreateLambda([]() { return false; }));
            }
        }
    }
    virtual FName GetToolkitFName() const override { return TEXT("KadanReference"); }
    virtual FText GetBaseToolkitName() const override { return FText::FromString(TEXT("Read-only game reference")); }
    virtual FString GetWorldCentricTabPrefix() const override { return TEXT("Game reference "); }
    virtual FLinearColor GetWorldCentricTabColorScale() const override { return FLinearColor(0.3f, 0.5f, 0.65f); }
    virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& Manager) override
    {
        FAssetEditorToolkit::RegisterTabSpawners(Manager);
        Manager->RegisterTabSpawner(ReferenceTab, FOnSpawnTab::CreateSP(this, &FReferenceEditor::Spawn))
            .SetDisplayName(GetBaseToolkitName());
    }
    virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& Manager) override
    {
        Manager->UnregisterTabSpawner(ReferenceTab);
        FAssetEditorToolkit::UnregisterTabSpawners(Manager);
    }

private:
    TWeakObjectPtr<UObject> Object;
    TSharedPtr<IDetailsView> Details;
    TArray<TSharedPtr<TWeakObjectPtr<UEdGraph>>> Graphs;
    TSharedPtr<TWeakObjectPtr<UEdGraph>> Selected;
    TSharedPtr<SBox> GraphPane;
    void RefreshGraph()
    {
        if (!GraphPane)
            return;
        UEdGraph* Graph = Selected ? Selected->Get() : nullptr;
        if (Graph)
            GraphPane->SetContent(SNew(SGraphEditor).GraphToEdit(Graph).IsEditable(false).DisplayAsReadOnly(true));
        else
            GraphPane->SetContent(
                SNew(STextBlock).Text(FText::FromString(TEXT("This asset has no Blueprint graphs."))));
    }
    TSharedRef<SDockTab> Spawn(const FSpawnTabArgs&)
    {
        TSharedRef<SDockTab> Tab = SNew(SDockTab)
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(8)
            [SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]()
            {
                const FAssetPolicy Policy = GetAssetPolicy(Object.Get());
                const FString Reason = Policy.Reason.IsEmpty() ? FString(TEXT("This game asset is a read-only reference.")) : Policy.Reason;
                return FText::FromString(Policy.PreviewNotice.IsEmpty() ? Reason : Reason + TEXT(" ") + Policy.PreviewNotice);
            })]
            + SVerticalBox::Slot().FillHeight(1)
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(Graphs.Num() ? 0.35f : 1.f)[Details.ToSharedRef()]
                + SHorizontalBox::Slot().FillWidth(0.65f)
                [SNew(SVerticalBox).Visibility(Graphs.Num() ? EVisibility::Visible : EVisibility::Collapsed)
                    + SVerticalBox::Slot().AutoHeight().Padding(4)
                    [SNew(SComboBox<TSharedPtr<TWeakObjectPtr<UEdGraph>>>).OptionsSource(&Graphs).InitiallySelectedItem(Selected)
                        .OnGenerateWidget_Lambda([](TSharedPtr<TWeakObjectPtr<UEdGraph>> Item)
                        { return SNew(STextBlock).Text(FText::FromString(Item && Item->IsValid() ? Item->Get()->GetName() : TEXT("Graph unavailable"))); })
                        .OnSelectionChanged_Lambda([this](TSharedPtr<TWeakObjectPtr<UEdGraph>> Item, ESelectInfo::Type) { Selected = Item; RefreshGraph(); })
                        [SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(Selected && Selected->IsValid() ? Selected->Get()->GetName() : TEXT("Choose a graph")); })]]
                    + SVerticalBox::Slot().FillHeight(1)[SAssignNew(GraphPane, SBox)]]]];
        RefreshGraph();
        return Tab;
    }
};
TArray<TWeakPtr<FReferenceEditor>> ReferenceEditors;

bool Protected(UObject* Asset)
{
    return Asset && (IsLockedObject(Asset) || !GetAssetPolicy(Asset).AllowEdit);
}
TArray<UObject*> Creators(const TArray<UObject*>& Objects)
{
    TArray<UObject*> Out;
    for (UObject* Object : Objects)
        if (!Protected(Object))
            Out.Add(Object);
    return Out;
}
bool HasProtected(const TArray<UObject*>& Objects)
{
    for (UObject* Object : Objects)
        if (Protected(Object))
            return true;
    return false;
}
using FWeakAssets = TArray<TWeakObjectPtr<UObject>>;
FWeakAssets WeakAssets(const TArray<UObject*>& Objects)
{
    FWeakAssets Out;
    for (UObject* Object : Objects)
        if (Object)
            Out.Add(Object);
    return Out;
}
FWeakAssets ProtectedAssets(const TArray<UObject*>& Objects)
{
    FWeakAssets Out;
    for (UObject* Object : Objects)
        if (Protected(Object))
            Out.Add(Object);
    return Out;
}
TArray<UObject*> LiveAssets(const FWeakAssets& Objects)
{
    TArray<UObject*> Out;
    for (const auto& Object : Objects)
        if (UObject* Live = Object.Get())
            Out.Add(Live);
    return Out;
}

TArray<TWeakObjectPtr<UObject>> MaterialParents(const TArray<UObject*>& Objects)
{
    TArray<TWeakObjectPtr<UObject>> Out;
    for (UObject* Object : Objects)
        if (IsValid(Object) && Object->IsA<UMaterialInterface>())
            if (!GetAssetPolicy(Object).StandIn)
                Out.Add(Object);
    return Out;
}
TArray<TWeakObjectPtr<UObject>> MaterialTextures(const TArray<UObject*>& Objects)
{
    TArray<TWeakObjectPtr<UObject>> Out;
    for (UObject* Object : Objects)
        if (IsValid(Object) && Object->IsA<UTexture>())
            if (!GetAssetPolicy(Object).StandIn)
                Out.Add(Object);
    return Out;
}
FUIAction CreatorMaterialAction(const TArray<TWeakObjectPtr<UObject>>& Textures)
{
    return FUIAction(FExecuteAction::CreateLambda(
                         [Textures]()
                         {
                             TArray<UObject*> Existing;
                             for (const auto& Texture : Textures)
                                 if (Texture.IsValid())
                                     Existing.Add(Texture.Get());
                             P4CreateCreatorMaterials(Existing);
                         }),
        FCanExecuteAction::CreateLambda(
            [Textures]()
            {
                for (const auto& Texture : Textures)
                    if (Texture.IsValid())
                        return true;
                return false;
            }));
}
FUIAction CreatorInstanceAction(const TArray<TWeakObjectPtr<UObject>>& Parents)
{
    return FUIAction(FExecuteAction::CreateLambda(
                         [Parents]()
                         {
                             TArray<UObject*> Existing;
                             for (const auto& Parent : Parents)
                                 if (Parent.IsValid())
                                     Existing.Add(Parent.Get());
                             P4CreateCreatorMaterialInstances(Existing);
                         }),
        FCanExecuteAction::CreateLambda(
            [Parents]()
            {
                for (const auto& Parent : Parents)
                    if (Parent.IsValid())
                        return true;
                return false;
            }));
}
void References(const TArray<UObject*>& Objects, TSharedPtr<IToolkitHost> Host)
{
    for (const auto& Identity : WeakAssets(Objects))
        if (UObject* Object = Identity.Get())
            if (Protected(Object))
            {
                const auto Editor = MakeShared<FReferenceEditor>();
                ReferenceEditors.Add(Editor);
                Editor->Open(Object, Host);
            }
}

// The program file an object's code is in, from its table of virtual functions. Null when it is not loaded.
void* CodeOf(const void* Object)
{
#if PLATFORM_WINDOWS
    HMODULE Module = nullptr;
    const void* Table = *static_cast<void* const*>(Object);
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            static_cast<LPCWSTR>(Table), &Module))
        return Module;
#endif
    return nullptr;
}

// Preserve every interface method rather than replacing stock creator behavior with
// FAssetTypeActions_Base defaults. Only protected object activation/actions are routed.
class FReferenceActions final : public IAssetTypeActions
{
public:
    explicit FReferenceActions(TSharedRef<IAssetTypeActions> InPrevious)
        : Previous(InPrevious), Code(CodeOf(&InPrevious.Get()))
    {}
    TSharedRef<IAssetTypeActions> Previous;
    // The program file the wrapped actions' code is in. A module unloaded during the session takes it away.
    void* Code;
    virtual FText GetName() const override { return Previous->GetName(); }
    virtual UClass* GetSupportedClass() const override { return Previous->GetSupportedClass(); }
    virtual FColor GetTypeColor() const override { return Previous->GetTypeColor(); }
    virtual bool HasActions(const TArray<UObject*>& Objects) const override
    {
        return HasProtected(Objects) || Previous->HasActions(Objects);
    }
    virtual void GetActions(const TArray<UObject*>& Objects, FMenuBuilder& Menu) override
    {
        const FWeakAssets Selected = WeakAssets(Objects);
        // A game asset's own actions are left out. Its read-only reference is in every asset's menu.
        const TArray<UObject*> Own = Creators(Objects);
        if (Own.Num())
            Previous->GetActions(Own, Menu);
        // UE4.26's material actions have no legacy entries. Keep its familiar creation
        // action, using the actual owned constructor rather than an unobserved stock factory.
        const TArray<UObject*> Surviving = LiveAssets(Selected);
        const auto Parents = MaterialParents(Surviving);
        if (Parents.Num())
            Menu.AddMenuEntry(FText::FromString(TEXT("Create Material Instance")),
                FText::FromString(TEXT("Create an instance that uses the selected material as its parent.")),
                FSlateIcon(), CreatorInstanceAction(Parents));
        const auto Textures = MaterialTextures(Surviving);
        if (Textures.Num())
            Menu.AddMenuEntry(FText::FromString(TEXT("Create Material")),
                FText::FromString(TEXT("Create a material that uses the selected texture.")), FSlateIcon(),
                CreatorMaterialAction(Textures));
    }
    virtual void GetActions(const TArray<UObject*>& Objects, FToolMenuSection& Section) override
    {
        const FWeakAssets Selected = WeakAssets(Objects);
        // Generic Edit still uses OpenAssetEditor. A protected object's type-specific
        // mutating actions are not copied into a tool-menu section.
        const TArray<UObject*> Own = Creators(Objects);
        if (Own.Num())
            Previous->GetActions(Own, Section);
        const TArray<UObject*> Surviving = LiveAssets(Selected);
        const auto Parents = MaterialParents(Surviving);
        if (Parents.Num())
        {
            // Exact native entry name from FAssetTypeActions_MaterialInterface. Keep
            // Find Parent and any other actions registered by the previous implementation.
            if (FToolMenuEntry* Entry = Section.FindEntry(TEXT("Material_NewMIC")))
                *Entry = FToolMenuEntry::InitMenuEntry(TEXT("Material_NewMIC"),
                    FText::FromString(TEXT("Create Material Instance")),
                    FText::FromString(TEXT("Create an instance that uses the selected material as its parent.")),
                    FSlateIcon(FEditorStyle::GetStyleSetName(), TEXT("ClassIcon.MaterialInstanceConstant")),
                    FToolUIActionChoice(CreatorInstanceAction(Parents)));
            else
                Section.AddMenuEntry(TEXT("Material_NewMIC"), FText::FromString(TEXT("Create Material Instance")),
                    FText::FromString(TEXT("Create an instance that uses the selected material as its parent.")),
                    FSlateIcon(), FToolUIActionChoice(CreatorInstanceAction(Parents)));
        }
        const auto Textures = MaterialTextures(Surviving);
        if (Textures.Num())
        {
            // Replace only the native texture-to-material constructor action. Conversion
            // and other stock texture actions keep their existing behavior.
            if (FToolMenuEntry* Entry = Section.FindEntry(TEXT("Texture_CreateMaterial")))
                *Entry = FToolMenuEntry::InitMenuEntry(TEXT("Texture_CreateMaterial"),
                    FText::FromString(TEXT("Create Material")),
                    FText::FromString(TEXT("Create a material that uses the selected texture.")),
                    FSlateIcon(FEditorStyle::GetStyleSetName(), TEXT("ClassIcon.Material")),
                    FToolUIActionChoice(CreatorMaterialAction(Textures)));
            else
                Section.AddMenuEntry(TEXT("Texture_CreateMaterial"), FText::FromString(TEXT("Create Material")),
                    FText::FromString(TEXT("Create a material that uses the selected texture.")), FSlateIcon(),
                    FToolUIActionChoice(CreatorMaterialAction(Textures)));
        }
    }
    virtual void OpenAssetEditor(
        const TArray<UObject*>& Objects, TSharedPtr<IToolkitHost> Host = TSharedPtr<IToolkitHost>()) override
    {
        // Stock World actions can unload and collect the input UWorld while opening the same map.
        // Classify references before dispatch, then resolve weak identities instead of inspecting
        // the old raw input array. Holding that world strongly would obstruct native map teardown.
        const FWeakAssets Game = ProtectedAssets(Objects);
        TArray<UObject*> Stock = Creators(Objects);
        if (Stock.Num())
            Previous->OpenAssetEditor(Stock, Host);
        References(LiveAssets(Game), Host);
    }
    virtual void AssetsActivated(const TArray<UObject*>& Objects, EAssetTypeActivationMethod::Type Type) override
    {
        const FWeakAssets Game = ProtectedAssets(Objects);
        if (Game.Num())
        {
            const TArray<UObject*> Own = Creators(Objects);
            if (Own.Num())
                Previous->AssetsActivated(Own, Type);
            References(LiveAssets(Game), nullptr);
        }
        else
            Previous->AssetsActivated(Objects, Type);
    }
    virtual bool AssetsActivatedOverride(
        const TArray<UObject*>& Objects, EAssetTypeActivationMethod::Type Type) override
    {
        const FWeakAssets Game = ProtectedAssets(Objects);
        if (!Game.Num())
            return Previous->AssetsActivatedOverride(Objects, Type);
        const FWeakAssets Stock = WeakAssets(Creators(Objects));
        const TArray<UObject*> Own = LiveAssets(Stock);
        if (Own.Num() && !Previous->AssetsActivatedOverride(Own, Type))
        {
            const TArray<UObject*> Remaining = LiveAssets(Stock);
            if (Remaining.Num())
                Previous->AssetsActivated(Remaining, Type);
        }
        References(LiveAssets(Game), nullptr);
        return true;
    }
    virtual TArray<FAssetData> GetValidAssetsForPreviewOrEdit(TArrayView<const FAssetData> Data, bool Preview) override
    {
        auto Valid = Previous->GetValidAssetsForPreviewOrEdit(Data, Preview);
        if (!Preview)
            Valid.RemoveAll([](const FAssetData& Asset) { return Protected(Asset.GetAsset()); });
        return Valid;
    }
    virtual bool CanFilter() override { return Previous->CanFilter(); }
    virtual bool CanLocalize() const override { return Previous->CanLocalize(); }
    virtual bool CanMerge() const override { return Previous->CanMerge(); }
    virtual void Merge(UObject* Object) override
    {
        if (!Protected(Object))
            Previous->Merge(Object);
    }
    virtual void Merge(UObject* Base, UObject* Remote, UObject* Local, const FOnMergeResolved& Done) override
    {
        if (!Protected(Local))
            Previous->Merge(Base, Remote, Local, Done);
    }
    virtual uint32 GetCategories() override { return Previous->GetCategories(); }
    virtual FString GetObjectDisplayName(UObject* Object) const override
    {
        return Previous->GetObjectDisplayName(Object);
    }
    virtual const TArray<FText>& GetSubMenus() const override { return Previous->GetSubMenus(); }
    virtual bool ShouldForceWorldCentric() override { return Previous->ShouldForceWorldCentric(); }
    virtual void PerformAssetDiff(
        UObject* Old, UObject* New, const FRevisionInfo& OldRevision, const FRevisionInfo& NewRevision) const override
    {
        Previous->PerformAssetDiff(Old, New, OldRevision, NewRevision);
    }
    virtual UThumbnailInfo* GetThumbnailInfo(UObject* Asset) const override
    {
        return Previous->GetThumbnailInfo(Asset);
    }
    virtual EThumbnailPrimType GetDefaultThumbnailPrimitiveType(UObject* Asset) const override
    {
        return Previous->GetDefaultThumbnailPrimitiveType(Asset);
    }
    virtual TSharedPtr<SWidget> GetThumbnailOverlay(const FAssetData& Data) const override
    {
        return Previous->GetThumbnailOverlay(Data);
    }
    virtual FText GetAssetDescription(const FAssetData& Data) const override
    {
        // The browser's tooltip says when the editor draws a game material only approximately.
        const FText Stock = Previous->GetAssetDescription(Data);
        UObject* Asset = Data.IsAssetLoaded() ? Data.GetAsset() : nullptr;
        return Asset ? PreviewDescription(Asset, Stock) : Stock;
    }
    virtual bool IsImportedAsset() const override { return Previous->IsImportedAsset(); }
    virtual void GetResolvedSourceFilePaths(const TArray<UObject*>& Objects, TArray<FString>& Paths) const override
    {
        Previous->GetResolvedSourceFilePaths(Objects, Paths);
    }
    virtual void GetSourceFileLabels(const TArray<UObject*>& Objects, TArray<FString>& Labels) const override
    {
        Previous->GetSourceFileLabels(Objects, Labels);
    }
    virtual void BuildBackendFilter(FARFilter& Filter) override { Previous->BuildBackendFilter(Filter); }
    virtual FText GetDisplayNameFromAssetData(const FAssetData& Data) const override
    {
        return Previous->GetDisplayNameFromAssetData(Data);
    }
    virtual void SetSupported(bool Value) override { Previous->SetSupported(Value); }
    virtual bool IsSupported() const override { return Previous->IsSupported(); }
};
TArray<TSharedPtr<FReferenceActions>> Routed;
FDelegateHandle ModulesChanged, OpenedInEditor, LockMarks, MenuExtender;

// Classes that have no asset actions of their own (the Wwise types without their editor module, the game's
// own asset classes) get these, so that they too have the lock mark and open as a read-only reference.
// Creators' assets of such a class open in the editor's plain property editor, as before.
class FPlainActions final : public FAssetTypeActions_Base
{
public:
    virtual FText GetName() const override { return FText::FromString(TEXT("Asset")); }
    virtual FColor GetTypeColor() const override { return FColor(127, 127, 127); }
    virtual UClass* GetSupportedClass() const override { return UObject::StaticClass(); }
    virtual uint32 GetCategories() override { return EAssetTypeCategories::Misc; }
    virtual bool CanFilter() override { return false; }
};
TSharedPtr<IAssetTypeActions> Plain;

bool IsRouted(const TSharedPtr<IAssetTypeActions>& Action)
{
    for (const auto& Wrapper : Routed)
        if (static_cast<IAssetTypeActions*>(Wrapper.Get()) == Action.Get())
            return true;
    return false;
}

// The engine's own asset types' actions are routed, and those registered later for them. Another
// module's actions for its own types are left as they are: some modules cast what the asset tools give
// back for their type to their own class (Niagara's script factory does), so a stand-in in their place
// breaks them. Those types are locked by the other routes: the editor refuses to open a locked asset's
// editor, an editor opened past that is closed for the reference, and the Content Browser's own menu
// gives the reference.
void RouteAll()
{
    if (!FModuleManager::Get().IsModuleLoaded("AssetTools"))
        return;
    IAssetTools& Tools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
    TArray<TWeakPtr<IAssetTypeActions>> Existing;
    Tools.GetAssetTypeActionsList(Existing);
    for (const auto& Weak : Existing)
    {
        const auto Action = Weak.Pin();
        if (!Action || IsRouted(Action) || !Action->GetSupportedClass())
            continue;
        const FString Module = Action->GetSupportedClass()->GetOutermost()->GetName();
        if (Module != TEXT("/Script/Engine") && Module != TEXT("/Script/CoreUObject") &&
            Module != TEXT("/Script/UMG") && Module != TEXT("/Script/UMGEditor"))
            continue;
        auto Wrapper = MakeShared<FReferenceActions>(Action.ToSharedRef());
        Tools.UnregisterAssetTypeActions(Action.ToSharedRef());
        Tools.RegisterAssetTypeActions(Wrapper);
        Routed.Add(Wrapper);
    }
}

// A module unloaded during the session has taken its code away. Its actions are taken out of the list and
// never released: releasing them would run code that is gone.
void ForgetUnloaded()
{
    if (!FModuleManager::Get().IsModuleLoaded("AssetTools"))
        return;
    IAssetTools& Tools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
    for (int32 Index = Routed.Num() - 1; Index >= 0; --Index)
    {
        const TSharedPtr<FReferenceActions> Wrapper = Routed[Index];
        if (!Wrapper->Code || CodeOf(&Wrapper->Previous.Get()) == Wrapper->Code)
            continue;
        Tools.UnregisterAssetTypeActions(Wrapper.ToSharedRef());
        new TSharedRef<IAssetTypeActions>(Wrapper->Previous);
        Routed.RemoveAt(Index);
    }
}

// The lock mark on a locked asset's tile in the Content Browser, whatever its type. Read from the list of
// locked packages, so that no asset is loaded to draw a tile.
TSharedRef<SWidget> LockIcon(const FAssetData& Data)
{
    if (!IsLockedPackageName(Data.PackageName))
        return SNullWidget::NullWidget;
    return SNew(SImage).Image(FEditorStyle::GetBrush(TEXT("PropertyWindow.Locked")));
}
TSharedRef<SWidget> LockTip(const FAssetData& Data)
{
    if (!IsLockedPackageName(Data.PackageName))
        return SNullWidget::NullWidget;
    FString Reason = TEXT(
        "This game asset is a read-only reference. Import your own source file to replace supported asset data.");
    if (UObject* Asset = Data.IsAssetLoaded() ? Data.GetAsset() : nullptr)
    {
        const FAssetPolicy Policy = GetAssetPolicy(Asset);
        if (!Policy.Reason.IsEmpty())
            Reason = Policy.Reason;
    }
    return SNew(STextBlock).Text(FText::FromString(Reason));
}

// "Replace with my own file" on one of the game's textures or static meshes (K9): the person's image or FBX
// is imported as the asset at the game's path, so everywhere the game uses it gets theirs.
void ReplaceWithOwnFile(const FSoftObjectPath& Path)
{
    UObject* Asset = Path.TryLoad();
    FString Why;
    if (!P4CanReplaceGameAsset(Asset, &Why))
    {
        Tell(Why);
        return;
    }
    IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
    if (!Desktop)
    {
        Tell(TEXT("Kadan cannot open a file dialog here."));
        return;
    }
    const bool Texture = Asset->IsA<UTexture2D>();
    TArray<FString> Files;
    const void* Parent = FSlateApplication::IsInitialized()
        ? FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr)
        : nullptr;
    if (!Desktop->OpenFileDialog(Parent, FString::Printf(TEXT("Replace %s with your own file"), *Asset->GetName()),
            FString(), FString(),
            Texture
                ? TEXT(
                      "Image files (*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.psd;*.exr)|*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.psd;*.exr")
                : TEXT("FBX or OBJ files (*.fbx;*.obj)|*.fbx;*.obj"),
            EFileDialogFlags::None, Files) ||
        Files.Num() != 1)
        return;
    FString Error;
    UObject* Made = P4ReplaceGameAsset(Asset, Files[0], Error);
    if (!Made)
    {
        Tell(Error);
        return;
    }
    Tell(Made->GetName() +
        (Texture
                ? TEXT(
                      " is now your own texture at the game's path. Save it, and export ships it in place of the game's.")
                : TEXT(
                      " is now your own mesh at the game's path. Its shape and collision come from your file. Save it, and export ships it in place of the game's.")));
}

// The read-only reference, from the Content Browser's menu of any locked asset, and the replacement of a game
// texture or static mesh with the person's own file.
TSharedRef<FExtender> ReferenceMenu(const TArray<FAssetData>& Selected)
{
    TSharedRef<FExtender> Extender = MakeShared<FExtender>();
    TArray<FSoftObjectPath> Locked;
    for (const FAssetData& Data : Selected)
        if (IsLockedPackageName(Data.PackageName))
            Locked.Add(Data.ToSoftObjectPath());
    // Offered on one selected texture or static mesh at a game path. Whether it can be replaced is checked when chosen.
    if (Selected.Num() == 1 &&
        (Selected[0].AssetClass == FName(TEXT("Texture2D")) || Selected[0].AssetClass == FName(TEXT("StaticMesh"))))
    {
        const FString Package = Selected[0].PackageName.ToString();
        if (P4IsContentPackage(Package) && !P4IsModFolderPackage(Package))
        {
            const FSoftObjectPath Path = Selected[0].ToSoftObjectPath();
            Extender->AddMenuExtension(TEXT("GetAssetActions"), EExtensionHook::First, nullptr,
                FMenuExtensionDelegate::CreateLambda(
                    [Path](FMenuBuilder& Menu)
                    {
                        Menu.AddMenuEntry(FText::FromString(TEXT("Replace with my own file")),
                            FText::FromString(TEXT(
                                "Imports your image or FBX file in place of this game asset. Everywhere the game uses it gets yours.")),
                            FSlateIcon(),
                            FUIAction(FExecuteAction::CreateLambda([Path]() { ReplaceWithOwnFile(Path); })));
                    }));
        }
    }
    if (!Locked.Num())
        return Extender;
    Extender->AddMenuExtension(TEXT("GetAssetActions"), EExtensionHook::First, nullptr,
        FMenuExtensionDelegate::CreateLambda(
            [Locked](FMenuBuilder& Menu)
            {
                Menu.AddMenuEntry(FText::FromString(TEXT("Open read-only reference")),
                    FText::FromString(TEXT("Shows the game asset without letting it change.")),
                    FSlateIcon(FEditorStyle::GetStyleSetName(), TEXT("PropertyWindow.Locked")),
                    FUIAction(FExecuteAction::CreateLambda(
                        [Locked]()
                        {
                            TArray<UObject*> Objects;
                            for (const FSoftObjectPath& Path : Locked)
                                if (UObject* Object = Path.TryLoad())
                                    Objects.Add(Object);
                            References(Objects, nullptr);
                        })));
            }));
    return Extender;
}

// An editor opened for a locked asset some other way than through its actions is closed, and the
// read-only reference opens in its place.
void Reroute(UObject* Asset, IAssetEditorInstance* Editor)
{
    if (!Asset || !Editor || !Protected(Asset))
        return;
    const FName Name = Editor->GetEditorName();
    if (Name == FName(TEXT("KadanReference")) || Name == FName(TEXT("PropertyEditor")))
        return;
    const TWeakObjectPtr<UObject> Weak(Asset);
    FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
        [Weak](float)
        {
            UObject* Locked = Weak.Get();
            auto* Editors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
            if (!Locked || !Editors)
                return false;
            bool Closed = false;
            for (IAssetEditorInstance* Open : Editors->FindEditorsForAsset(Locked))
            {
                const FName Kind = Open->GetEditorName();
                if (Kind != FName(TEXT("KadanReference")) && Kind != FName(TEXT("PropertyEditor")))
                {
                    Open->CloseWindow();
                    Closed = true;
                }
            }
            Locked = Weak.Get();
            if (Closed && Locked && !Editors->FindEditorForAsset(Locked, false))
                References({Locked}, nullptr);
            return false;
        }));
}
} //namespace

TSharedRef<SWidget> LockMark(const FAssetData& Data)
{
    return LockIcon(Data);
}

void InitializeReadOnlyRouting()
{
    if (Routed.Num() || IsRunningCommandlet() || !IsInGameThread() || !FApp::CanEverRender())
        return;
    FModuleManager::LoadModuleChecked<IModuleInterface>("UMGEditor");
    InitializeMutationGuards();
    IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
    Plain = MakeShared<FPlainActions>();
    Tools.RegisterAssetTypeActions(Plain.ToSharedRef());
    RouteAll();
    FContentBrowserModule& Browser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
    LockMarks = Browser.AddAssetViewExtraStateGenerator(
        FAssetViewExtraStateGenerator(FOnGenerateAssetViewExtraStateIndicators::CreateStatic(&LockIcon),
            FOnGenerateAssetViewExtraStateIndicators::CreateStatic(&LockTip)));
    auto Extender = FContentBrowserMenuExtender_SelectedAssets::CreateStatic(&ReferenceMenu);
    MenuExtender = Extender.GetHandle();
    Browser.GetAllAssetViewContextMenuExtenders().Add(Extender);
    ModulesChanged = FModuleManager::Get().OnModulesChanged().AddLambda(
        [](FName, EModuleChangeReason Reason)
        {
            if (Reason == EModuleChangeReason::ModuleUnloaded)
                ForgetUnloaded();
            else if (Reason == EModuleChangeReason::ModuleLoaded)
                RouteAll();
        });
    if (GEditor)
        if (auto* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
            OpenedInEditor = Editors->OnAssetOpenedInEditor().AddStatic(&Reroute);
}
void ShutdownReadOnlyRouting()
{
    FModuleManager::Get().OnModulesChanged().Remove(ModulesChanged);
    if (FModuleManager::Get().IsModuleLoaded(TEXT("ContentBrowser")))
    {
        FContentBrowserModule& Browser = FModuleManager::GetModuleChecked<FContentBrowserModule>(
            TEXT("ContentBrowser"));
        Browser.RemoveAssetViewExtraStateGenerator(LockMarks);
        const FDelegateHandle Handle = MenuExtender;
        Browser.GetAllAssetViewContextMenuExtenders().RemoveAll(
            [Handle](const FContentBrowserMenuExtender_SelectedAssets& One) { return One.GetHandle() == Handle; });
    }
    if (GEditor)
        if (auto* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
            Editors->OnAssetOpenedInEditor().Remove(OpenedInEditor);
    LockMarks.Reset();
    MenuExtender.Reset();
    ModulesChanged.Reset();
    OpenedInEditor.Reset();
    for (const auto& Weak : ReferenceEditors)
        if (const auto Editor = Weak.Pin())
            Editor->CloseWindow();
    ReferenceEditors.Empty();
    if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
    {
        IAssetTools& Tools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
        for (const auto& Wrapper : Routed)
        {
            const auto Current = Tools.GetAssetTypeActionsForClass(Wrapper->GetSupportedClass()).Pin();
            Tools.UnregisterAssetTypeActions(Wrapper.ToSharedRef());
            Tools.RegisterAssetTypeActions(Wrapper->Previous);
            // A later action registration must retain its priority after restoration.
            if (Current && Current != Wrapper)
            {
                Tools.UnregisterAssetTypeActions(Current.ToSharedRef());
                Tools.RegisterAssetTypeActions(Current.ToSharedRef());
            }
        }
        if (Plain)
            Tools.UnregisterAssetTypeActions(Plain.ToSharedRef());
    }
    Routed.Empty();
    Plain.Reset();
}
} //namespace P4Kadan

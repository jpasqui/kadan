#include "P4KadanReadOnly.h"
#include "P4KadanAssetPolicy.h"
#include "P4OriginalRecord.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Engine/DataTable.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/InputBindingManager.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Docking/TabManager.h"
#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBox.h"

namespace P4Kadan
{
namespace
{
FDelegateHandle Tick, Opened;
TWeakObjectPtr<UAssetEditorSubsystem> Editors;
struct FDetailGuard
{
    TWeakPtr<IDetailsView> View;
    FIsPropertyEditingEnabled Enabled;
    FIsPropertyReadOnly ReadOnly;
    FDelegateHandle OurEnabled, OurReadOnly;
    TSharedPtr<bool> GameRows;
};
struct FTableGuard
{
    TWeakPtr<SDockTab> Tab;
    TSharedPtr<SWidget> Original, Wrapper;
};
struct FCommandGuard
{
    TWeakPtr<FUICommandList> List;
    TSharedPtr<const FUICommandInfo> Command;
    FUIAction Original;
};
TArray<FDetailGuard> Details;
TArray<FTableGuard> Tables;
TArray<FCommandGuard> Commands;

// An object of a locked game asset, or a locked asset itself, is shown and never edited in a details panel.
bool Locked(UObject* Object)
{
    return Object && (IsLockedObject(Object) || (Object->IsAsset() && !GetAssetPolicy(Object).AllowEdit));
}
bool AllowedProperty(UObject* Object, const FProperty&, const TArray<const FProperty*>&)
{
    if (!Object)
        return false;
    if (Object->IsA<UDataTable>())
    {
        // A game table's rows are edited in the table's row editor. The table's own settings (its row type,
        // how it was imported) are the game's.
        FP4OriginalRecord Record;
        FString Error;
        if (P4ReadOriginalRecord(Object, Record, Error))
            return false;
    }
    // A change Tsuchi cannot apply to a game actor is put back when it is made, with the reason.
    return !Locked(Object);
}
bool WritableTableField(const FProperty& Property)
{
    if (CastField<FBoolProperty>(&Property) || CastField<FNumericProperty>(&Property) ||
        CastField<FEnumProperty>(&Property) || CastField<FNameProperty>(&Property) ||
        CastField<FStrProperty>(&Property) || CastField<FObjectPropertyBase>(&Property))
        return true;
    if (const auto* Struct = CastField<FStructProperty>(&Property))
    {
        // Compound values are edited through their leaf fields. A struct with an
        // unsupported leaf remains inspectable, and that leaf stays read-only.
        return Struct->Struct != nullptr;
    }
    // An array of values: Tsuchi writes its values and its length.
    if (const auto* Array = CastField<FArrayProperty>(&Property))
        return Array->Inner && WritableTableField(*Array->Inner);
    return false; // FText, maps, sets, delegates and interfaces cannot be written as a row value.
}
void Install(FDetailGuard& G, const TSharedRef<IDetailsView>& View)
{
    G.Enabled = View->GetIsPropertyEditingEnabledDelegate();
    G.ReadOnly = View->GetIsPropertyReadOnlyDelegate();
    const TWeakPtr<IDetailsView> Weak(View);
    const auto Enabled = G.Enabled;
    const auto ReadOnly = G.ReadOnly;
    const auto Rows = G.GameRows;
    View->SetIsPropertyEditingEnabledDelegate(FIsPropertyEditingEnabled::CreateLambda(
        [Weak, Enabled]()
        {
            const auto V = Weak.Pin();
            if (!V || (Enabled.IsBound() && !Enabled.Execute()))
                return false;
            for (const auto& Object : V->GetSelectedObjects())
                if (Object.IsValid() && Locked(Object.Get()))
                    return false;
            return true;
        }));
    View->SetIsPropertyReadOnlyDelegate(FIsPropertyReadOnly::CreateLambda(
        [Weak, ReadOnly, Rows](const FPropertyAndParent& P)
        {
            if (ReadOnly.IsBound() && ReadOnly.Execute(P))
                return true;
            const auto V = Weak.Pin();
            if (!V)
                return true;
            if (*Rows && !CanEditTableProperty(P.Property, P.ParentProperties))
                return true;
            for (const auto& Object : V->GetSelectedObjects())
                if (Object.IsValid() && !AllowedProperty(Object.Get(), P.Property, P.ParentProperties))
                    return true;
            return false;
        }));
    G.OurEnabled = View->GetIsPropertyEditingEnabledDelegate().GetHandle();
    G.OurReadOnly = View->GetIsPropertyReadOnlyDelegate().GetHandle();
}
void Inspect(const TSharedRef<SWidget>& Widget, bool GameRows = false)
{
    if (Widget->GetType() == FName(TEXT("SDetailsView")) || Widget->GetType() == FName(TEXT("SStructureDetailsView")))
    {
        const auto View = StaticCastSharedRef<IDetailsView>(Widget);
        FDetailGuard* Known = nullptr;
        for (auto& G : Details)
            if (G.View.Pin() == View)
            {
                Known = &G;
                break;
            }
        if (Known)
        {
            *Known->GameRows |= GameRows;
            // The view's owner set its own delegates again: they are wrapped again.
            if (View->GetIsPropertyEditingEnabledDelegate().GetHandle() != Known->OurEnabled ||
                View->GetIsPropertyReadOnlyDelegate().GetHandle() != Known->OurReadOnly)
                Install(*Known, View);
        }
        else
        {
            FDetailGuard G;
            G.View = View;
            G.GameRows = MakeShared<bool>(GameRows);
            Install(G, View);
            Details.Add(MoveTemp(G));
        }
    }
    FChildren* Children = Widget->GetChildren();
    for (int32 I = 0; Children && I < Children->Num(); ++I)
        Inspect(Children->GetChildAt(I), GameRows);
}
void GuardMatrix(UObject* Asset, IAssetEditorInstance* Editor)
{
    if (Asset && Asset->IsA<UDataTable>() && Editor && Editor->GetEditorName() == FName(TEXT("DataTableEditor")))
    {
        FP4OriginalRecord Record;
        FString Error;
        const auto Manager = Editor->GetAssociatedTabManager();
        if (Manager && P4ReadOriginalRecord(Asset, Record, Error))
            if (const auto Tab = Manager->FindExistingLiveTab(FTabId(TEXT("DataTableEditor_RowEditor"))))
                Inspect(Tab->GetContent(), true);
    }
    if (!Asset || !Locked(Asset) || !Editor || Editor->GetEditorName() != FName(TEXT("PropertyEditor")))
        return;
    const auto Manager = Editor->GetAssociatedTabManager();
    if (!Manager)
        return;
    for (const TCHAR* Id : {TEXT("PropertyEditorToolkit_PropertyTable"), TEXT("PropertyEditorToolkit_PropertyTree")})
    {
        const auto Tab = Manager->FindExistingLiveTab(FTabId(FName(Id)));
        if (!Tab)
            continue;
        bool Known = false;
        for (const auto& G : Tables)
            if (G.Tab.Pin() == Tab)
            {
                Known = true;
                break;
            }
        if (Known)
            continue;
        FTableGuard G;
        G.Tab = Tab;
        G.Original = Tab->GetContent();
        // Matrix selection is one edit operation. A mixed selection cannot edit
        // a game row indirectly; creators can reopen their own rows separately.
        G.Wrapper = SNew(SBox).IsEnabled(false)[G.Original.ToSharedRef()];
        Tab->SetContent(G.Wrapper.ToSharedRef());
        Tables.Add(MoveTemp(G));
    }
    auto* Toolkit = static_cast<FAssetEditorToolkit*>(Editor);
    for (const TCHAR* Name : {TEXT("SaveAsset"), TEXT("SaveAssetAs"), TEXT("ReimportAsset")})
    {
        const auto Command = FInputBindingManager::Get().FindCommandInContext(TEXT("AssetEditor"), Name);
        if (!Command)
            continue;
        const auto List = Toolkit->GetToolkitCommands();
        bool Known = false;
        for (const auto& G : Commands)
            if (G.List.Pin() == List && G.Command == Command)
            {
                Known = true;
                break;
            }
        if (Known)
            continue;
        if (const auto* Existing = List->GetActionForCommand(Command))
        {
            FCommandGuard G;
            G.List = List;
            G.Command = Command;
            G.Original = *Existing;
            auto Filtered = *Existing;
            Filtered.CanExecuteAction = FCanExecuteAction::CreateLambda([]() { return false; });
            List->MapAction(Command, Filtered);
            Commands.Add(MoveTemp(G));
        }
    }
}
bool Refresh(float)
{
    if (!GEditor || !FSlateApplication::IsInitialized())
        return true;
    for (const auto& G : Details)
        *G.GameRows = false;
    if (!Editors.IsValid())
    {
        Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
        if (Editors.IsValid())
            Opened = Editors->OnAssetOpenedInEditor().AddStatic(&GuardMatrix);
    }
    if (Editors.IsValid())
        for (UObject* Asset : Editors->GetAllEditedAssets())
            for (IAssetEditorInstance* Editor : Editors->FindEditorsForAsset(Asset))
                GuardMatrix(Asset, Editor);
    for (const auto& Window : FSlateApplication::Get().GetTopLevelWindows())
        Inspect(Window);
    Details.RemoveAll([](const FDetailGuard& G) { return !G.View.IsValid(); });
    Tables.RemoveAll([](const FTableGuard& G) { return !G.Tab.IsValid(); });
    Commands.RemoveAll([](const FCommandGuard& G) { return !G.List.IsValid(); });
    return true;
}
} //namespace
bool CanEditTableProperty(const FProperty& Property, const TArray<const FProperty*>& Parents)
{
    bool Row = false;
    for (const FProperty* P : Parents)
    {
        const auto* Owner = P->GetOwnerStruct();
        Row |= Owner && Owner->IsChildOf(FTableRowBase::StaticStruct());
    }
    const auto* Owner = Property.GetOwnerStruct();
    Row |= Owner && Owner->IsChildOf(FTableRowBase::StaticStruct());
    if (!Row)
        return true;
    // Tsuchi can address struct and array leaves and an array's length, but not map/set values or
    // values reached through another UObject. Those stay read-only.
    for (const FProperty* P : Parents)
        if (!CastField<FStructProperty>(P) && !CastField<FArrayProperty>(P))
            return false;
    return WritableTableField(Property);
}
void InitializePropertyGuards()
{
    if (Tick.IsValid())
        return;
    Tick = FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Refresh), 0.1f);
    Refresh(0);
}
void ShutdownPropertyGuards()
{
    if (Tick.IsValid())
        FTicker::GetCoreTicker().RemoveTicker(Tick);
    if (Editors.IsValid())
        Editors->OnAssetOpenedInEditor().Remove(Opened);
    for (const auto& G : Details)
        if (const auto View = G.View.Pin())
        {
            View->SetIsPropertyEditingEnabledDelegate(G.Enabled);
            View->SetIsPropertyReadOnlyDelegate(G.ReadOnly);
        }
    for (const auto& G : Tables)
        if (const auto Tab = G.Tab.Pin())
            if (Tab->GetContent() == G.Wrapper)
                Tab->SetContent(G.Original.ToSharedRef());
    for (const auto& G : Commands)
        if (const auto List = G.List.Pin())
            List->MapAction(G.Command, G.Original);
    Details.Reset();
    Tables.Reset();
    Commands.Reset();
    Editors.Reset();
    Opened.Reset();
    Tick.Reset();
}
} //namespace P4Kadan

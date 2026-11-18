// The Details panel's Game Options: the options a gameplay object's placement row holds, at the top.
#include "P4GameOptionsDetails.h"
#include "P4PlacementExport.h"
#include "P4PlacementInternal.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "IDetailCustomization.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"

using namespace P4PlacementDetail;

namespace
{
const FName LayoutClass(TEXT("GActor"));
bool bRegistered = false;

// The property of the owner a field's path starts with: its first step, or the second after the name of one of
// the owner's classes ("VarGateBaseActor.AddWallNum"). None for a path through the actor's components.
FProperty* TopProperty(UObject* Owner, const std::string& Path)
{
    TArray<FString> Steps;
    Wide(Path).ParseIntoArray(Steps, TEXT("."));
    for (int32 Step = 0; Step < FMath::Min(2, Steps.Num()); ++Step)
    {
        if (Steps[Step] == TEXT("PlacedActor"))
            return nullptr;
        if (FProperty* Property = FindFProperty<FProperty>(Owner->GetClass(), *Steps[Step]))
            return Property->HasAnyPropertyFlags(CPF_Edit) ? Property : nullptr;
    }
    return nullptr;
}

class FP4GameOptionsDetails final : public IDetailCustomization
{
public:
    void CustomizeDetails(IDetailLayoutBuilder& Builder) override
    {
        TArray<TWeakObjectPtr<UObject>> Selected;
        Builder.GetObjectsBeingCustomized(Selected);
        AActor* Actor = Selected.Num() == 1 ? Cast<AActor>(Selected[0].Get()) : nullptr;
        if (!Actor || Actor->IsTemplate() || !P4IsGameplayActor(Actor))
            return;
        IDetailCategoryBuilder* Category = nullptr;
        for (const TPair<UObject*, FProperty*>& Option : P4GameOptionsOf(Actor))
        {
            if (!Category)
                Category = &Builder.EditCategory(
                    TEXT("Game Options"), FText::FromString(TEXT("Game Options")), ECategoryPriority::Important);
            // The actor's own option moves here from its other category; a component's is shown here as well.
            if (Option.Key == Actor)
            {
                TSharedRef<IPropertyHandle> Handle =
                    Builder.GetProperty(Option.Value->GetFName(), Option.Value->GetOwnerClass());
                if (Handle->IsValidHandle())
                    Category->AddProperty(Handle);
            }
            else
                Category->AddExternalObjectProperty(TArray<UObject*>{Option.Key}, Option.Value->GetFName());
        }
    }
};

TSharedRef<IDetailCustomization> Make()
{
    return MakeShared<FP4GameOptionsDetails>();
}
} // namespace

TArray<TPair<UObject*, FProperty*>> P4GameOptionsOf(AActor* Actor)
{
    TArray<TPair<UObject*, FProperty*>> Out;
    if (!Actor || !P4IsGameplayActor(Actor))
        return Out;
    const P::Context Context = ContextOf(Actor);
    for (const P::SlotName& Slot : P::Slots())
    {
        UObject* Owner = OwnerOf(Actor, Slot);
        if (!Owner || NotAnOption(SlotKey(Slot)))
            continue;
        for (const P::Field& Field : LayoutFields(Slot.S, Context))
            if (!NotAProperty(Field))
                if (FProperty* Property = TopProperty(Owner, Field.Name))
                    Out.AddUnique(TPair<UObject*, FProperty*>(Owner, Property));
    }
    return Out;
}

void P4RegisterGameOptionsDetails()
{
    if (bRegistered)
        return;
    FPropertyEditorModule& Editor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
    Editor.RegisterCustomClassLayout(LayoutClass, FOnGetDetailCustomizationInstance::CreateStatic(&Make));
    Editor.NotifyCustomizationModuleChanged();
    bRegistered = true;
}

void P4UnregisterGameOptionsDetails()
{
    if (!bRegistered)
        return;
    if (FPropertyEditorModule* Editor = FModuleManager::GetModulePtr<FPropertyEditorModule>(TEXT("PropertyEditor")))
        Editor->UnregisterCustomClassLayout(LayoutClass);
    bRegistered = false;
}

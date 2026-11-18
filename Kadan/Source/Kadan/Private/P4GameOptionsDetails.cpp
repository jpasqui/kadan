// The Details panel's Game Options: the options a gameplay object's placement row holds, at the top.
#include "P4GameOptionsDetails.h"
#include "P4LevelContent.h"
#include "P4PlacementExport.h"
#include "P4PlacementInternal.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "IDetailCustomization.h"
#include "IDetailPropertyRow.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"

using namespace P4PlacementDetail;

namespace
{
const FName LayoutClass(TEXT("GActor"));
bool bRegistered = false;

const FText NotHeld = FText::FromString(TEXT(
    "The game makes this object from its placement list, which does not hold this value, so a change here does not reach the game."));
const FText NotWritten = FText::FromString(TEXT(
    "Kadan cannot write this option into the game's placement list yet, so a change here does not reach the game."));
const FText NotWrittenForNewPlacement = FText::FromString(TEXT(
    "Kadan cannot write this option for a new placement yet. The game uses the class's default value."));

bool IsNewPlacement(const AActor* Actor)
{
    const ULevel* Level = Actor->GetLevel();
    UWorld* Source = Level ? Cast<UWorld>(Level->GetOuter()) : nullptr;
    return !Source || !P4ImportedLevelActors(Source).Contains(Actor->GetFName());
}

bool ReachesGame(AActor* Actor, UObject* Object, FProperty* Property, const P::Context& Context, bool bNewPlacement)
{
    if (!GameProperty(Property) || (Object == Actor && RowSetting(Property)))
        return true;
    for (const TPair<FProperty*, bool>& Held : HeldOptions(Actor, Object, Context, bNewPlacement))
        if (Held.Key == Property)
            return Held.Value;
    return false;
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
        const P::Context Context = ContextOf(Actor);
        const bool bNewPlacement = IsNewPlacement(Actor);
        const FText& CannotWrite = bNewPlacement ? NotWrittenForNewPlacement : NotWritten;
        IDetailCategoryBuilder* Category = nullptr;
        const auto Mark = [](IDetailPropertyRow* Row, bool Reaches, const FText& Why)
        {
            if (Row && !Reaches)
            {
                Row->IsEnabled(false);
                Row->ToolTip(Why);
            }
        };
        for (const TPair<UObject*, FProperty*>& Option : P4GameOptionsOf(Actor))
        {
            if (!Category)
                Category = &Builder.EditCategory(
                    TEXT("Game Options"), FText::FromString(TEXT("Game Options")), ECategoryPriority::Important);
            const bool Reaches = ReachesGame(Actor, Option.Key, Option.Value, Context, bNewPlacement);
            // The actor's own option moves here from its other category; a component's is shown here as well.
            if (Option.Key == Actor)
            {
                TSharedRef<IPropertyHandle> Handle =
                    Builder.GetProperty(Option.Value->GetFName(), Option.Value->GetOwnerClass());
                if (Handle->IsValidHandle())
                    Mark(&Category->AddProperty(Handle), Reaches, CannotWrite);
            }
            else
                Mark(Category->AddExternalObjectProperty(TArray<UObject*>{Option.Key}, Option.Value->GetFName()), Reaches,
                    CannotWrite);
        }
        // The game's other values of the object do not reach the game: they are shown, not edited.
        const TArray<TPair<FProperty*, bool>> Held = HeldOptions(Actor, Actor, Context);
        for (TFieldIterator<FProperty> It(Actor->GetClass()); It; ++It)
        {
            FProperty* Property = *It;
            if (!Property->HasAnyPropertyFlags(CPF_Edit) || !GameProperty(Property) || RowSetting(Property) ||
                Held.ContainsByPredicate([&](const TPair<FProperty*, bool>& One) { return One.Key == Property; }))
                continue;
            TSharedRef<IPropertyHandle> Handle = Builder.GetProperty(Property->GetFName(), Property->GetOwnerClass());
            if (Handle->IsValidHandle())
                Mark(Builder.EditDefaultProperty(Handle), false, NotHeld);
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
    TInlineComponentArray<UActorComponent*> Parts(Actor);
    TArray<UObject*> Objects{Actor};
    for (UActorComponent* Part : Parts)
        if (Part)
            Objects.Add(Part);
    for (UObject* Object : Objects)
        for (const TPair<FProperty*, bool>& Held : HeldOptions(Actor, Object, Context))
            if (Held.Key->HasAnyPropertyFlags(CPF_Edit))
                Out.Add(TPair<UObject*, FProperty*>(Object, Held.Key));
    return Out;
}

bool P4GameOptionReachesGame(AActor* Actor, UObject* Object, FProperty* Property)
{
    if (!Actor || !Object || !Property || !P4IsGameplayActor(Actor))
        return true;
    return ReachesGame(Actor, Object, Property, ContextOf(Actor), IsNewPlacement(Actor));
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

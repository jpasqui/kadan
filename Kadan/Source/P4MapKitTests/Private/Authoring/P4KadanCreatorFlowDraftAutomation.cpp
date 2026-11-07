#include "P4KadanCreatorFlowStageList.h"

#if P4_MAPKIT_TESTS
namespace P4KadanCreatorFlow
{

namespace
{
// Draft: unsaved identity edits ask Save, Discard or Cancel before the picker or New mod replaces them.
class FDraftStage final : public FFlowStage
{
public:
    using FFlowStage::FFlowStage;

    void AddSteps() override;

private:
    TSharedPtr<SWindow> DraftDialog();

    bool DraftSwitch(const FP4ModDescriptor& Mod);

    bool DraftChoice(const FString& Caption);

    bool DraftRootBytes(bool Remember);

    bool DraftCapture(const FString& Name);

    TMap<FString, TArray<uint8>> DraftRoots;
    FP4ModDescriptor DraftSavedB;
    FString DraftTargetVersion;
};
} // namespace

void FDraftStage::AddSteps()
{
    Flow.Add(TEXT("Read the retained identities and snapshot both root files"),
        [this]()
        {
            FString Error;
            if (!Flow.Expect(P4ReadModDescriptor(FPaths::ProjectDir(), Flow.A.Id, Flow.A, Error) &&
                        P4ReadModDescriptor(FPaths::ProjectDir(), Flow.B.Id, Flow.B, Error),
                    Error))
            {
                return true;
            }
            DraftTargetVersion = Flow.B.Version == TEXT("1.0.2") ? TEXT("1.0.3") : TEXT("1.0.2");
            return DraftRootBytes(true);
        });
    Flow.Add(TEXT("Open Mods through the actual menu"),
        [this]() { return Flow.Menu(TEXT("Mods and export"), TEXT("Mods")); });
    Flow.Add(TEXT("Use the narrow panel"), [this]() { return Flow.NarrowPanel(); });
    Flow.Add(TEXT("Choose the second mod"), [this]() { return Flow.Choose(Flow.B); });
    Flow.Add(TEXT("Type an unsaved name"), [this]() { return Flow.SetField(TEXT("Name"), TEXT("Draft name kept")); });
    Flow.Add(TEXT("Type an invalid unsaved version"), [this]() { return Flow.SetField(TEXT("Version"), TEXT("bad")); });
    Flow.Add(TEXT("Request the first mod through the actual picker"), [this]() { return DraftSwitch(Flow.A); });
    Flow.Add(TEXT("Capture the dirty identity choice"), [this]() { return DraftCapture(TEXT("choice")); });
    Flow.Add(TEXT("Choose Save with an invalid draft"), [this]() { return DraftChoice(TEXT("Save")); });
    Flow.Add(TEXT("Validation keeps the dialog and unchanged second-mod draft"),
        [this]()
        {
            const auto Window = DraftDialog();
            return Flow.Expect(
                       Window && Text(Window.ToSharedRef(), TEXT("Enter a version with three numbers, such as 1.0.0.")),
                       TEXT("The invalid draft did not show an actionable validation message")) &&
                Flow.ModFormMatches(Flow.B.Id, TEXT("Draft name kept"), TEXT("bad"));
        });
    Flow.Add(TEXT("Capture validation failure"), [this]() { return DraftCapture(TEXT("validation")); });
    Flow.Add(TEXT("Cancel the switch"), [this]() { return DraftChoice(TEXT("Cancel")); });
    Flow.Add(TEXT("Cancel preserves the picker and every draft field"),
        [this]() { return !DraftDialog() && Flow.ModFormMatches(Flow.B.Id, TEXT("Draft name kept"), TEXT("bad")); });
    Flow.Add(TEXT("Reveal the retained version field"), [this]() { return Flow.Prepare(Flow.Field(TEXT("Version"))); });
    Flow.Add(TEXT("Capture the unchanged canceled draft"), [this]() { return Flow.Screenshot(TEXT("canceled")); });
    Flow.Add(TEXT("Request the first mod again"), [this]() { return DraftSwitch(Flow.A); });
    Flow.Add(TEXT("Discard the invalid draft explicitly"), [this]() { return DraftChoice(TEXT("Discard")); });
    Flow.Add(TEXT("Discard changes only the selected identity"),
        [this]() { return Flow.ModFormMatches(Flow.A.Id, Flow.A.Name, Flow.A.Version) && DraftRootBytes(false); });
    Flow.Add(TEXT("Return to the second mod"), [this]() { return Flow.Choose(Flow.B); });
    Flow.Add(TEXT("Edit a valid name"), [this]() { return Flow.SetField(TEXT("Name"), TEXT("Saved draft identity")); });
    Flow.Add(TEXT("Edit a valid version"), [this]() { return Flow.SetField(TEXT("Version"), DraftTargetVersion); });
    Flow.Add(TEXT("Request another mod with the valid draft"), [this]() { return DraftSwitch(Flow.A); });
    Flow.Add(TEXT("Save before switching"), [this]() { return DraftChoice(TEXT("Save")); });
    Flow.Add(TEXT("The saved draft and both independent root files read back"),
        [this]()
        {
            FString Error;
            return Flow.Expect(P4ReadModDescriptor(FPaths::ProjectDir(), Flow.B.Id, DraftSavedB, Error) &&
                           DraftSavedB.Name == TEXT("Saved draft identity") &&
                           DraftSavedB.Version == DraftTargetVersion,
                       TEXT("Saving the draft did not retain its identity")) &&
                Flow.ModFormMatches(Flow.A.Id, Flow.A.Name, Flow.A.Version) && DraftRootBytes(false);
        });
    Flow.Add(TEXT("Choose the renamed saved mod"), [this]() { return Flow.Choose(DraftSavedB); });
    Flow.Add(TEXT("Capture the saved name and immutable ID"),
        [this]()
        {
            const auto Lock = Flow.Tagged(TEXT("KadanModIdLock"));
            return Flow.Expect(Lock && !Lock->IsEnabled(), TEXT("Saving a draft unlocked its permanent ID")) &&
                Flow.Screenshot(TEXT("saved"));
        });
    Flow.Add(TEXT("Start another unsaved edit"),
        [this]() { return Flow.SetField(TEXT("Name"), TEXT("Draft before new mod")); });
    Flow.Add(TEXT("Request New mod through its actual button"), [this]() { return Flow.Click(TEXT("New mod")); });
    Flow.Add(TEXT("Cancel starting a new draft"), [this]() { return DraftChoice(TEXT("Cancel")); });
    Flow.Add(TEXT("New mod cancellation keeps the current draft"),
        [this]() { return Flow.ModFormMatches(Flow.B.Id, TEXT("Draft before new mod"), DraftTargetVersion); });
    Flow.Add(TEXT("Restore the saved name without writing"),
        [this]() { return Flow.SetField(TEXT("Name"), DraftSavedB.Name); });
    Flow.Add(TEXT("Start a clean new mod"), [this]() { return Flow.Click(TEXT("New mod")); });
    Flow.Add(TEXT("The clean new draft has no selected saved identity"),
        [this]() { return Flow.ModFormMatches(FString(), FString(), TEXT("1.0.0")); });
    Flow.Add(TEXT("Name the unsaved new mod"),
        [this]() { return Flow.SetField(TEXT("Name"), TEXT("Unsaved new identity")); });
    Flow.Add(TEXT("A new unsaved identity also prompts on switching"), [this]() { return DraftSwitch(Flow.A); });
    Flow.Add(TEXT("Cancel switching away from the new identity"), [this]() { return DraftChoice(TEXT("Cancel")); });
    Flow.Add(TEXT("The unsaved new name and suggested ID survive"),
        [this]()
        {
            const auto Picker = StaticCastSharedPtr<FPicker>(Flow.Tagged(TEXT("KadanModPicker")));
            return Flow.Expect(Picker && !Picker->GetSelectedItem() &&
                    Flow.Field(TEXT("ID"))->GetText().ToString() == TEXT("unsaved_new_identity") &&
                    Flow.Field(TEXT("Name"))->GetText().ToString() == TEXT("Unsaved new identity"),
                TEXT("Cancel discarded the unsaved new mod"));
        });
    Flow.Add(TEXT("Capture the retained new draft"), [this]() { return Flow.Screenshot(TEXT("new-canceled")); });
    Flow.Add(TEXT("Request the first mod again from the new draft"), [this]() { return DraftSwitch(Flow.A); });
    Flow.Add(TEXT("Discard the new draft explicitly"), [this]() { return DraftChoice(TEXT("Discard")); });
    Flow.Add(TEXT("Discard does not create an identity file"),
        [this]()
        {
            return Flow.Expect(
                       !FPaths::FileExists(FPaths::ProjectDir() / TEXT("Config/KadanMods/unsaved_new_identity.json")),
                       TEXT("Discard unexpectedly saved the new identity")) &&
                Flow.ModFormMatches(Flow.A.Id, Flow.A.Name, Flow.A.Version);
        });
    Flow.Add(TEXT("Close only the owned Kadan tab"),
        [this]()
        {
            if (const auto Panel = Flow.Tab())
            {
                Panel->RequestCloseTab();
            }
            return !Flow.Tab();
        });
    Flow.Add(
        TEXT("Reopen Kadan through its menu"), [this]() { return Flow.Menu(TEXT("Mods and export"), TEXT("Mods")); });
    Flow.Add(TEXT("Choose the first saved identity after reopen"), [this]() { return Flow.Choose(Flow.A); });
    Flow.Add(TEXT("Capture the first identity and retained roots"),
        [this]()
        {
            return Flow.ModFormMatches(Flow.A.Id, Flow.A.Name, Flow.A.Version) && DraftRootBytes(false) &&
                Flow.Screenshot(TEXT("reopened-first"));
        });
    Flow.Add(TEXT("Choose the second saved identity after reopen"), [this]() { return Flow.Choose(DraftSavedB); });
    Flow.Add(TEXT("Capture the second identity and retained roots"),
        [this]()
        {
            return Flow.ModFormMatches(Flow.B.Id, DraftSavedB.Name, DraftSavedB.Version) && DraftRootBytes(false) &&
                Flow.Screenshot(TEXT("reopened-second"));
        });
}

TSharedPtr<SWindow> FDraftStage::DraftDialog()
{
    const auto Window = FSlateApplication::Get().GetActiveModalWindow();
    return Window && Window->GetTitle().ToString() == TEXT("Unsaved mod details") ? Window : nullptr;
}

bool FDraftStage::DraftSwitch(const FP4ModDescriptor& Mod)
{
    if (Flow.Phase == 0)
    {
        const auto Picker = Flow.Tagged(TEXT("KadanModPicker"));
        if (!Flow.Prepare(Picker) || !Flow.MouseClick(Picker))
        {
            return false;
        }
        Flow.Phase = 1;
        Flow.ReadyFrame = Flow.Frame + 2;
        return false;
    }
    if (Flow.Phase == 1)
    {
        if (!Flow.MouseClick(Flow.WindowText(Mod.Name + TEXT(" · ") + Mod.Id)))
        {
            return false;
        }
        Flow.Phase = 2;
        Flow.ReadyFrame = Flow.Frame + 2;
        return false;
    }
    return DraftDialog().IsValid();
}

bool FDraftStage::DraftChoice(const FString& Caption)
{
    const auto Window = DraftDialog();
    if (!Window)
    {
        return false;
    }
    const auto Button = Find(Window.ToSharedRef(), [&](const TSharedRef<SWidget>& Widget)
        { return Widget->GetType() == FName(TEXT("SButton")) && Text(Widget, Caption).IsValid(); });
    if (!Button)
    {
        return false;
    }
    if (Flow.Prepared.Pin() != Button)
    {
        Window->BringToFront(true);
        Flow.Prepared = Button;
        Flow.ReadyFrame = Flow.Frame + 2;
        return false;
    }
    return Flow.Frame >= Flow.ReadyFrame && Flow.MouseClick(Button);
}

bool FDraftStage::DraftRootBytes(bool Remember)
{
    for (const auto& Mod : {Flow.A, Flow.B})
    {
        TArray<uint8> Bytes;
        const FString File = FPaths::ProjectDir() / TEXT("Config/KadanSelections") / (Mod.Id + TEXT(".json"));
        if (!FFileHelper::LoadFileToArray(Bytes, *File))
        {
            return Flow.Expect(false, TEXT("The retained mod roots are unavailable"));
        }
        if (Remember)
        {
            DraftRoots.Add(Mod.Id, MoveTemp(Bytes));
        }
        else if (!DraftRoots.Contains(Mod.Id) || DraftRoots[Mod.Id] != Bytes)
        {
            return Flow.Expect(false, TEXT("Identity editing changed a saved mod's roots"));
        }
    }
    return true;
}

bool FDraftStage::DraftCapture(const FString& Name)
{
    const auto Window = DraftDialog();
    return Window && Flow.CaptureWidget(Name, Window);
}

TUniquePtr<FFlowStage> MakeDraftStage(FFlow& Flow)
{
    return MakeUnique<FDraftStage>(Flow);
}
} // namespace P4KadanCreatorFlow
#endif

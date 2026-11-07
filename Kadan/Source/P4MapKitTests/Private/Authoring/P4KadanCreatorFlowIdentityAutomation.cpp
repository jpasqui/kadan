#include "P4KadanCreatorFlowStageList.h"

#if P4_MAPKIT_TESTS
namespace P4KadanCreatorFlow
{

namespace
{
// IdentityBefore and Identity: a suggested ID follows the name until it is edited, then a numeric ID is saved.
class FIdentityStage final : public FFlowStage
{
public:
    using FFlowStage::FFlowStage;

    void AddSteps() override;
};
} // namespace

void FIdentityStage::AddSteps()
{
    if (Flow.Stage == TEXT("Identity"))
    {
        Flow.Add(TEXT("Reset only the known owned identity fixture"),
            [this]()
            {
                const FString File = FPaths::ProjectDir() / TEXT("Config/KadanMods/99.json");
                if (!FPaths::FileExists(File))
                {
                    return true;
                }
                FP4ModDescriptor Saved;
                FString Error;
                if (!P4ReadModDescriptor(FPaths::ProjectDir(), TEXT("99"), Saved, Error) ||
                    (Saved.Name != TEXT("Pikmix_Kadan updated") && Saved.Name != TEXT("Pikmix_Kadan final")))
                {
                    return Flow.Expect(
                        false, TEXT("The owned project has an unexpected numeric identity. Preserve it."));
                }
                return Flow.Expect(
                    IFileManager::Get().Delete(*File), TEXT("The known identity fixture could not reset"));
            });
    }
    Flow.Add(TEXT("Open Mods through the Kadan menu"),
        [this]() { return Flow.Menu(TEXT("Mods and export"), TEXT("Mods")); });
    Flow.Add(TEXT("Use the actual narrow panel"), [this]() { return Flow.NarrowPanel(); });
    Flow.Add(TEXT("Start a new mod through its button"), [this]() { return Flow.Click(TEXT("New mod")); });
    Flow.Add(TEXT("Type the creator's mod name through Slate input"),
        [this]() { return Flow.SetField(TEXT("Name"), TEXT("Pikmix_Kadan")); });
    if (Flow.Stage == TEXT("IdentityBefore"))
    {
        Flow.Add(TEXT("The original form leaves the ID empty after naming a mod"),
            [this]()
            {
                const auto Id = Flow.Field(TEXT("ID"));
                return Flow.Expect(Id && Id->GetText().IsEmpty(), TEXT("The baseline identity form already changed"));
            });
        Flow.Add(TEXT("Capture the original new-mod form"), [this]() { return Flow.Screenshot(TEXT("name")); });
        return;
    }
    Flow.Add(TEXT("The name suggests an ID and explains the folder"),
        [this]()
        {
            const auto Id = Flow.Field(TEXT("ID"));
            return Flow.Expect(Id && Id->GetText().ToString() == TEXT("pikmix_kadan") &&
                    Flow.VisibleText(TEXT("The ID names the output folder"), true),
                TEXT("The new mod does not suggest its ID or explain the output folder"));
        });
    Flow.Add(TEXT("Capture the suggested ID"), [this]() { return Flow.Screenshot(TEXT("suggested")); });
    Flow.Add(TEXT("Continue editing a new name without choosing an ID"),
        [this]() { return Flow.SetField(TEXT("Name"), TEXT("Pikmix_Kadan next")); });
    Flow.Add(TEXT("An unedited suggestion follows the new name"),
        [this]()
        {
            const auto Id = Flow.Field(TEXT("ID"));
            return Flow.Expect(Id && Id->GetText().ToString() == TEXT("pikmix_kadan_next"),
                TEXT("An automatic suggestion stopped following the new mod name"));
        });
    Flow.Add(TEXT("Choose a permanent numeric ID through the field"),
        [this]() { return Flow.SetField(TEXT("ID"), TEXT("99")); });
    Flow.Add(TEXT("Edit the display name after choosing an ID"),
        [this]() { return Flow.SetField(TEXT("Name"), TEXT("Pikmix_Kadan updated")); });
    Flow.Add(TEXT("An explicit ID remains unchanged"),
        [this]()
        {
            return Flow.Expect(Flow.Field(TEXT("ID"))->GetText().ToString() == TEXT("99"),
                TEXT("Typing a display name replaced a manually chosen ID"));
        });
    Flow.Add(TEXT("Save the real mod through its button"), [this]() { return Flow.Click(TEXT("Save mod")); });
    Flow.Add(TEXT("Saved identity stays immutable"),
        [this]()
        {
            FP4ModDescriptor Saved;
            FString Error;
            const auto Lock = Flow.Tagged(TEXT("KadanModIdLock"));
            return Flow.Expect(P4ReadModDescriptor(FPaths::ProjectDir(), TEXT("99"), Saved, Error) &&
                    Saved.Name == TEXT("Pikmix_Kadan updated") && Lock && !Lock->IsEnabled(),
                TEXT("The numeric identity did not save or remained editable"));
        });
    Flow.Add(TEXT("Edit the saved display name"),
        [this]() { return Flow.SetField(TEXT("Name"), TEXT("Pikmix_Kadan final")); });
    Flow.Add(TEXT("Save the updated display name"), [this]() { return Flow.Click(TEXT("Save mod")); });
    Flow.Add(TEXT("Show the permanent identity guidance"), [this]() { return Flow.Click(TEXT("Mods")); });
    Flow.Add(TEXT("Capture the retained numeric identity"), [this]() { return Flow.Screenshot(TEXT("saved")); });
    Flow.Add(TEXT("Reveal the exact published mod folder"),
        [this]() { return Flow.Prepare(Flow.Tagged(TEXT("KadanModOutput"))); });
    Flow.Add(TEXT("The output folder uses the ID rather than the display name"),
        [this]()
        {
            return Flow.Expect(Flow.TaggedText(TEXT("KadanModOutput")) ==
                    FPaths::ConvertRelativePathToFull(Flow.ExportFolder / TEXT("99")),
                TEXT("The displayed mod folder does not use its numeric identity"));
        });
    Flow.Add(TEXT("Capture the exact numeric mod folder"), [this]() { return Flow.Screenshot(TEXT("folder")); });
    Flow.Add(TEXT("Close the owned Kadan tab"),
        [this]()
        {
            if (auto Panel = Flow.Tab())
            {
                Panel->RequestCloseTab();
            }
            return !Flow.Tab();
        });
    Flow.Add(TEXT("Reopen the panel through the same menu"),
        [this]() { return Flow.Menu(TEXT("Mods and export"), TEXT("Mods")); });
    Flow.Add(TEXT("Choose the saved numeric mod through its picker"),
        [this]()
        {
            FP4ModDescriptor Saved;
            Saved.Id = TEXT("99");
            Saved.Name = TEXT("Pikmix_Kadan final");
            Saved.Version = TEXT("1.0.0");
            return Flow.Choose(Saved);
        });
    Flow.Add(TEXT("The ID and renamed display name survive reopen"),
        [this]()
        {
            const auto Id = Flow.Field(TEXT("ID"));
            const auto Name = Flow.Field(TEXT("Name"));
            const auto Lock = Flow.Tagged(TEXT("KadanModIdLock"));
            return Flow.Expect(Id && Name && Lock && Id->GetText().ToString() == TEXT("99") &&
                    Name->GetText().ToString() == TEXT("Pikmix_Kadan final") && !Lock->IsEnabled(),
                TEXT("The saved identity changed after reopening"));
        });
    Flow.Add(TEXT("Capture the reopened identity"), [this]() { return Flow.Screenshot(TEXT("reopened")); });
}

TUniquePtr<FFlowStage> MakeIdentityStage(FFlow& Flow)
{
    return MakeUnique<FIdentityStage>(Flow);
}
} // namespace P4KadanCreatorFlow
#endif

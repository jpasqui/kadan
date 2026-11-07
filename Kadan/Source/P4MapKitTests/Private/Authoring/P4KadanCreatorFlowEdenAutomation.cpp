#include "P4KadanCreatorFlowStageList.h"
#include "P4KadanCreatorFlowShared.h"

#if P4_MAPKIT_TESTS
namespace P4KadanCreatorFlow
{

namespace
{
// EdenBefore and Eden: the optional Eden output suggestion keeps the saved output until it is chosen.
class FEdenStage final : public FFlowStage
{
public:
    using FFlowStage::FFlowStage;

    void AddSteps() override;

private:
    struct FScopedEdenEnvironment
    {
        FString Previous;
        explicit FScopedEdenEnvironment(const FString& ProfileDirectory)
        {
            Previous = FPlatformMisc::GetEnvironmentVariable(TEXT("APPDATA"));
            FPlatformMisc::SetEnvironmentVar(TEXT("APPDATA"), *ProfileDirectory);
        }
        ~FScopedEdenEnvironment() { FPlatformMisc::SetEnvironmentVar(TEXT("APPDATA"), *Previous); }
    };

    bool PrepareEdenProfile();

    FFlowAssets Assets{Flow};
    FFlowModSteps ModSteps{Flow};
    TUniquePtr<FScopedEdenEnvironment> EdenEnvironment;
    FString EdenOutputDirectory;
};
} // namespace

void FEdenStage::AddSteps()
{
    Flow.Add(TEXT("Close only a restored tab in this owned editor"),
        [this]()
        {
            if (auto Panel = Flow.Tab())
            {
                Panel->RequestCloseTab();
            }
            return !Flow.Tab();
        });
    Flow.Add(
        TEXT("Prepare an owned emulator profile and saved output choice"), [this]() { return PrepareEdenProfile(); });
    Flow.Add(TEXT("Open Mods through the actual Kadan menu"),
        [this]() { return Flow.Menu(TEXT("Mods and export"), TEXT("Mods")); });
    Flow.Add(TEXT("Use the narrow panel"), [this]() { return Flow.NarrowPanel(); });
    Flow.Add(TEXT("Choose the retained second mod"), [this]() { return Flow.Choose(Flow.B); });
    Flow.Add(TEXT("Opening the panel retains the saved output folder"),
        [this]()
        {
            const auto Output = Flow.Field(TEXT("Output folder"));
            return Flow.Expect(Output && Output->GetText().ToString() == Flow.ExportFolder,
                TEXT("Discovery replaced the creator's selected output folder"));
        });
    Flow.Add(TEXT("Reveal the output controls"), [this]() { return Flow.Prepare(Flow.Field(TEXT("Output folder"))); });
    if (Flow.Stage == TEXT("EdenBefore"))
    {
        Flow.Add(TEXT("Capture the original output controls"), [this]() { return Flow.Screenshot(TEXT("output")); });
        return;
    }
    Flow.Add(TEXT("The existing title folder is offered without changing output"),
        [this]()
        {
            return Flow.Expect(Flow.TaggedText(TEXT("KadanEdenFolder")) == EdenOutputDirectory,
                TEXT("The actual output suggestion does not match the owned profile's title directory"));
        });
    Flow.Add(TEXT("Reveal the available Eden suggestion"),
        [this]()
        {
            auto Panel = Flow.Tab();
            return Panel && Flow.Prepare(Text(Panel->GetContent(), TEXT("Use Eden folder")));
        });
    Flow.Add(TEXT("Capture the optional output suggestion"), [this]() { return Flow.Screenshot(TEXT("suggestion")); });
    Flow.Add(TEXT("Choose Eden through its actual button"), [this]() { return Flow.Click(TEXT("Use Eden folder")); });
    Flow.Add(TEXT("The chosen output keeps the per-mod directory"),
        [this]()
        {
            const auto Output = Flow.Field(TEXT("Output folder"));
            return Flow.Expect(Output && Output->GetText().ToString() == EdenOutputDirectory &&
                    Flow.TaggedText(TEXT("KadanModOutput")) == EdenOutputDirectory / Flow.B.Id,
                TEXT("The chosen title output lost the per-mod identity directory"));
        });
    Flow.Add(TEXT("Capture the chosen title output and mod directory"),
        [this]() { return Flow.Screenshot(TEXT("chosen")); });
    Flow.Add(TEXT("Remove only this fixture mod's previous roots"), [this]() { return Flow.RemoveEveryRoot(); });
    Flow.Add(
        TEXT("Select the real retained creator texture"), [this]() { return Assets.SelectFixture(Flow.BPackage); });
    Flow.Add(TEXT("Add the selected texture through its control"),
        [this]() { return Flow.Click(TEXT("Add selected assets")); });
    Flow.Add(
        TEXT("Review export into the owned emulator profile"),
        [this]() { return Flow.ClickQueuedExport(TEXT("Review export")); }, 240);
    Flow.Add(
        TEXT("Export through the actual panel"), [this]() { return Flow.ClickQueuedExport(TEXT("Export mod")); }, 600);
    Flow.Add(
        TEXT("Independently inspect the exact per-mod title layout and container"),
        [this]()
        {
            TGuardValue<FString> ExpectedOutput(Flow.ExportFolder, EdenOutputDirectory);
            return ModSteps.VerifyExport();
        },
        240);
    Flow.Add(TEXT("Reveal the actual exported mod directory"),
        [this]() { return Flow.Prepare(Flow.Tagged(TEXT("KadanModOutput"))); });
    Flow.Add(TEXT("Capture the published result in the owned profile"),
        [this]() { return Flow.Screenshot(TEXT("published")); });
    Flow.Add(TEXT("Restore the fixture's ordinary output through the field"),
        [this]() { return Flow.SetField(TEXT("Output folder"), Flow.ExportFolder); });
    Flow.Add(
        TEXT("Persist the restored output through ordinary review"),
        [this]() { return Flow.ClickQueuedExport(TEXT("Review export")); }, 240);
}

bool FEdenStage::PrepareEdenProfile()
{
    FString ProjectDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    FPaths::NormalizeDirectoryName(ProjectDirectory);
    const FString TaskRoot = FPaths::GetPath(ProjectDirectory);
    const FString ProfileDirectory = TaskRoot / TEXT("eden-user");
    if (!Flow.Expect(FPaths::IsUnderDirectory(ProjectDirectory, TaskRoot) &&
                FPaths::IsUnderDirectory(ProfileDirectory, TaskRoot),
            TEXT("The emulator profile fixture is outside the owned task")))
    {
        return false;
    }
    EdenOutputDirectory = ProfileDirectory / TEXT("eden/load/0100B7C00933A000");
    if (!Flow.Expect(IFileManager::Get().MakeDirectory(*EdenOutputDirectory, true),
            TEXT("The owned emulator profile fixture could not be created")))
    {
        return false;
    }
    EdenEnvironment = MakeUnique<FScopedEdenEnvironment>(ProfileDirectory);
    // A saved creator choice is the precondition. Opening the panel must retain it.
    GConfig->SetString(TEXT("Kadan"), TEXT("ExportFolder"), *Flow.ExportFolder, GEditorPerProjectIni);
    return true;
}

TUniquePtr<FFlowStage> MakeEdenStage(FFlow& Flow)
{
    return MakeUnique<FEdenStage>(Flow);
}
} // namespace P4KadanCreatorFlow
#endif

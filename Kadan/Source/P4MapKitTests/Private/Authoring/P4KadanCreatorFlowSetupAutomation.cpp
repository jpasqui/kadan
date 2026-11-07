#include "P4KadanCreatorFlowStageList.h"

#if P4_MAPKIT_TESTS
namespace P4KadanCreatorFlow
{

namespace
{
// Setup: the project setup strip, its missing-input feedback and its restart and missing-tool states.
class FSetupStage final : public FFlowStage
{
public:
    using FFlowStage::FFlowStage;

    void AddSteps() override;

private:
    FString SetupConfigBefore;
};

// SetupReady: a reopened project uses its saved setup.
class FSetupReadyStage final : public FFlowStage
{
public:
    using FFlowStage::FFlowStage;

    void AddSteps() override;
};

// Browser: automatic folder preparation, the source catalog, folder tree and search.
class FBrowserStage final : public FFlowStage
{
public:
    using FFlowStage::FFlowStage;

    void AddSteps() override;

    FString QualificationScope() const override;
};

// Reimport: Select imported across hidden rows, then a bounded texture reimport.
class FReimportStage final : public FFlowStage
{
public:
    using FFlowStage::FFlowStage;

    void AddSteps() override;
};
} // namespace

void FSetupStage::AddSteps()
{
    Flow.Add(TEXT("Record config before the folder is chosen"),
        [this]
        {
            FFileHelper::LoadFileToString(SetupConfigBefore, *(FPaths::ProjectConfigDir() / TEXT("DefaultEngine.ini")));
            return true;
        });
    Flow.Add(TEXT("Open Import through the Kadan menu"),
        [this] { return Flow.Menu(TEXT("Import from game"), TEXT("Choose Pikmin 4's romfs or Paks folder.")); });
    Flow.Add(TEXT("Capture fresh project with Advanced closed"), [this] { return Flow.Screenshot(TEXT("fresh")); });
    Flow.Add(TEXT("Use a missing original Paks folder"),
        [this] { return Flow.SetField(TEXT("Game folder"), Flow.Out / TEXT("missing-paks")); });
    Flow.Add(TEXT("Commit missing game input for automatic preparation"),
        [this] { return Flow.CommitField(TEXT("Game folder")); });
    Flow.Add(TEXT("Missing setup input has actionable feedback"),
        [this]
        {
            return Flow.Expect((Flow.TaggedText(TEXT("KadanStatus")).Contains(TEXT("romfs folder")) ||
                                   Flow.TaggedText(TEXT("KadanStatus")).Contains(TEXT("Carrot4-Switch.pak"))),
                TEXT("Project setup failure does not identify required Paks input"));
        });
    Flow.Add(TEXT("Capture missing game folder guidance"), [this] { return Flow.Screenshot(TEXT("missing-input")); });
    Flow.Add(TEXT("Set real original Paks through the form"),
        [this]
        {
            return Flow.SetField(
                TEXT("Game folder"), FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS")));
        });
    Flow.Add(TEXT("Expand Advanced for setup tool"),
        [this] { return Flow.Expand(TEXT("KadanAdvanced"), TEXT("Advanced"), true); });
    Flow.Add(TEXT("Set repak through the form"), [this]
        { return Flow.SetField(TEXT("repak"), Flow.Tool(TEXT("P4_NATIVE_REPAK"), TEXT("private/repak/repak.exe"))); });
    Flow.Add(
        TEXT("Commit the setup tool to read the game folder again"), [this] { return Flow.CommitField(TEXT("repak")); },
        180);
    Flow.Add(TEXT("Reading the game folder leaves project config untouched"),
        [this]
        {
            FString Current;
            FFileHelper::LoadFileToString(Current, *(FPaths::ProjectConfigDir() / TEXT("DefaultEngine.ini")));
            return Flow.Expect(Current == SetupConfigBefore,
                TEXT("Choosing a folder changed project setup without its explicit button"));
        });
    Flow.Add(TEXT("Close Advanced before explicit setup"),
        [this] { return Flow.Expand(TEXT("KadanAdvanced"), TEXT("Advanced"), false); });
    Flow.Add(
        TEXT("Capture visible setup before its action"), [this] { return Flow.Screenshot(TEXT("before-action")); });
    Flow.Add(TEXT("Open legacy setup only for the baseline capture"),
        [this]
        {
            return FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_UX_BASELINE")) != TEXT("1") ||
                Flow.Expand(TEXT("KadanAdvanced"), TEXT("Advanced"), true);
        });
    // Project setup rewrites project config, so it runs only from its own button.
    Flow.Add(TEXT("Set up the project through its button"), [this] { return Flow.Click(TEXT("Set up project")); }, 180);
    Flow.Add(TEXT("Setup reports saved configuration or existing ready project"),
        [this]
        {
            const FString Status = Flow.TaggedText(TEXT("KadanStatus"));
            return Flow.Expect(Status.Contains(TEXT("Restart Unreal Editor")) ||
                    Status.Contains(TEXT("already uses the game collision channels")) ||
                    Status.Contains(TEXT("ready to browse")),
                TEXT("Project setup retry did not report a completed setup"));
        });
    Flow.Add(TEXT("Collapse setup Advanced tools"),
        [this] { return Flow.Expand(TEXT("KadanAdvanced"), TEXT("Advanced"), false); });
    Flow.Add(TEXT("Capture setup UI"), [this] { return Flow.Screenshot(TEXT("setup")); });
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_UX_BASELINE")) != TEXT("1"))
    {
        Flow.Add(TEXT("Setup exposes a separate restart state"),
            [this]
            {
                return Flow.Expect(Flow.TaggedText(TEXT("KadanSetupState")) == TEXT("Restart required") &&
                        !Flow.ButtonEnabled(TEXT("Import selection")),
                    TEXT("Setup does not expose its restart requirement"));
            });
        Flow.Add(TEXT("Expand tools for the missing-tool case"),
            [this] { return Flow.Expand(TEXT("KadanAdvanced"), TEXT("Advanced"), true); });
        Flow.Add(TEXT("Set a missing repak executable"),
            [this] { return Flow.SetField(TEXT("repak"), Flow.Out / TEXT("missing-repak.exe")); });
        Flow.Add(TEXT("Close tools before the missing-tool action"),
            [this] { return Flow.Expand(TEXT("KadanAdvanced"), TEXT("Advanced"), false); });
        Flow.Add(TEXT("Capture the missing-tool action"), [this] { return Flow.Screenshot(TEXT("missing-tool")); });
        Flow.Add(TEXT("Open the exact missing tool field"), [this] { return Flow.Click(TEXT("Choose repak")); });
        Flow.Add(TEXT("Missing-tool action expands Advanced and focuses repak"),
            [this]
            {
                auto Area = StaticCastSharedPtr<SExpandableArea>(Flow.Tagged(TEXT("KadanAdvanced")));
                auto Input = Flow.Field(TEXT("repak"));
                return Flow.Expect(Area && Area->IsExpanded() && Input &&
                        (Input->HasKeyboardFocus() || Input->HasFocusedDescendants()),
                    TEXT("Missing-tool action did not focus repak"));
            });
        Flow.Add(TEXT("Capture the focused tool field"), [this] { return Flow.Screenshot(TEXT("tool-field")); });
        Flow.Add(TEXT("Restore repak after the missing-tool case"),
            [this]
            {
                return Flow.SetField(
                    TEXT("repak"), Flow.Tool(TEXT("P4_NATIVE_REPAK"), TEXT("private/repak/repak.exe")));
            });
        Flow.Add(TEXT("Close tools after recovery"),
            [this] { return Flow.Expand(TEXT("KadanAdvanced"), TEXT("Advanced"), false); });
    }
}

void FSetupReadyStage::AddSteps()
{
    Flow.Add(TEXT("Open the ready Import panel"),
        [this] { return Flow.Menu(TEXT("Import from game"), TEXT("Choose Pikmin 4's romfs or Paks folder.")); });
    Flow.Add(TEXT("Reopened project uses saved setup"),
        [this]
        {
            FString Why;
            return Flow.Expect(
                P4KadanProjectReady(Why) && Flow.TaggedText(TEXT("KadanSetupState")) == TEXT("Project ready"),
                TEXT("Saved setup is not ready after reopening. ") + Why);
        });
    Flow.Add(TEXT("Capture reopened ready state"), [this] { return Flow.Screenshot(TEXT("ready")); });
}

void FBrowserStage::AddSteps()
{
    Flow.Add(TEXT("Open the actual Import panel"),
        [this] { return Flow.Menu(TEXT("Import from game"), TEXT("Choose Pikmin 4's romfs or Paks folder.")); });
    Flow.Add(TEXT("Open Advanced to establish actual source tools"),
        [this] { return Flow.Expand(TEXT("KadanAdvanced"), TEXT("Advanced"), true); });
    Flow.Add(TEXT("Set the actual retained retoc executable"), [this]
        { return Flow.SetField(TEXT("retoc"), Flow.Tool(TEXT("P4_NATIVE_RETOC"), TEXT("private/retoc/retoc.exe"))); });
    Flow.Add(TEXT("Set the actual retained repak executable"), [this]
        { return Flow.SetField(TEXT("repak"), Flow.Tool(TEXT("P4_NATIVE_REPAK"), TEXT("private/repak/repak.exe"))); });
    Flow.Add(TEXT("Use the normal catalog instead of any retained extracted override"),
        [this] { return Flow.CheckTag(TEXT("KadanUseExtracted"), false); });
    Flow.Add(TEXT("Close Advanced before automatic folder preparation"),
        [this] { return Flow.Expand(TEXT("KadanAdvanced"), TEXT("Advanced"), false); });
    Flow.Add(TEXT("Choose missing original source in the form"),
        [this] { return Flow.SetField(TEXT("Game folder"), Flow.Out / TEXT("missing-paks")); });
    Flow.Add(TEXT("Commit missing input through a real Enter event"),
        [this] { return Flow.CommitField(TEXT("Game folder")); });
    Flow.Add(TEXT("Automatic source failure is actionable and leaves import disabled"),
        [this]
        {
            return Flow.Expect(Flow.TaggedText(TEXT("KadanStatus")).Contains(TEXT("romfs folder")) &&
                    !Flow.ButtonEnabled(TEXT("Import selection")),
                TEXT("Missing committed game folder did not report an actionable refusal"));
        });
    Flow.Add(TEXT("Enter the actual retained game source"),
        [this]
        {
            return Flow.SetField(
                TEXT("Game folder"), FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS")));
        });
    Flow.Add(
        TEXT("Commit source for automatic catalog and setup"), [this] { return Flow.CommitField(TEXT("Game folder")); },
        180);
    Flow.Add(TEXT("Automatic preparation populates the actual browser"),
        [this]
        {
            return Flow.Expect(
                Flow.SourceCount() > 4 && Flow.TaggedText(TEXT("KadanStatus")).Contains(TEXT("ready to browse")),
                TEXT("Committed real source did not populate the catalog"));
        });
    Flow.Add(TEXT("Choose the real Carrot4 folder tree row"), [this] { return Flow.SelectTopFolder(); });
    Flow.Add(TEXT("Actual folder selection updates its breadcrumb and results"),
        [this]
        {
            return Flow.Expect(
                Flow.TaggedText(TEXT("KadanFolderPath")) == TEXT("/Game/Carrot4") && Flow.SourceCount() > 4,
                TEXT("Folder-tree selection did not filter the actual browser"));
        });
    Flow.Add(TEXT("Search alongside the selected folder"),
        [this] { return Flow.SetField(TEXT("Search"), TEXT("this-package-does-not-exist-ui-test")); });
    Flow.Add(TEXT("Search and folder selection combine without losing the folder"),
        [this]
        {
            return Flow.Expect(Flow.SourceCount() == 0 &&
                    Flow.TaggedText(TEXT("KadanFolderPath")) == TEXT("/Game/Carrot4") &&
                    Flow.VisibleText(TEXT("No assets match these filters."), true),
                TEXT("Search did not compose with folder-tree selection"));
        });
    Flow.Add(TEXT("Clear the search and selected tree folder"), [this] { return Flow.Click(TEXT("Clear filters")); });
    Flow.Add(TEXT("Clear filters restores all source results and folder path"),
        [this]
        {
            return Flow.Expect(Flow.SourceCount() > 4 &&
                    Flow.TaggedText(TEXT("KadanFolderPath")) == TEXT("All folders") && Flow.BusyObservations > 0,
                TEXT("Clear filters or deferred busy indication was not observed"));
        });
    Flow.Add(TEXT("Capture the actual populated source browser"), [this] { return Flow.Screenshot(TEXT("browser")); });
}

void FReimportStage::AddSteps()
{
    Flow.Add(TEXT("Open Import in the retained upgrade project"),
        [this] { return Flow.Menu(TEXT("Import from game"), TEXT("Choose Pikmin 4's romfs or Paks folder.")); });
    Flow.Add(TEXT("Open Advanced for retained game tools"),
        [this] { return Flow.Expand(TEXT("KadanAdvanced"), TEXT("Advanced"), true); });
    Flow.Add(TEXT("Use actual retoc"), [this]
        { return Flow.SetField(TEXT("retoc"), Flow.Tool(TEXT("P4_NATIVE_RETOC"), TEXT("private/retoc/retoc.exe"))); });
    Flow.Add(TEXT("Use actual repak"), [this]
        { return Flow.SetField(TEXT("repak"), Flow.Tool(TEXT("P4_NATIVE_REPAK"), TEXT("private/repak/repak.exe"))); });
    Flow.Add(TEXT("Use the normal game catalog"), [this] { return Flow.CheckTag(TEXT("KadanUseExtracted"), false); });
    Flow.Add(TEXT("Close Advanced"), [this] { return Flow.Expand(TEXT("KadanAdvanced"), TEXT("Advanced"), false); });
    Flow.Add(TEXT("Set retained original containers"),
        [this]
        {
            return Flow.SetField(
                TEXT("Game folder"), FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS")));
        });
    Flow.Add(TEXT("Read catalog through Enter"), [this] { return Flow.CommitField(TEXT("Game folder")); }, 180);
    Flow.Add(TEXT("Clear any previous selection"), [this] { return Flow.Click(TEXT("Clear selection")); });
    Flow.Add(TEXT("Reimport is disabled without a selection"), [this]
        { return Flow.Expect(!Flow.ButtonEnabled(TEXT("Reimport")), TEXT("Empty selection enables reimport")); });
    Flow.Add(TEXT("Hide every row with search"),
        [this] { return Flow.SetField(TEXT("Search"), TEXT("no-such-source-kadan-upgrade-control")); });
    Flow.Add(
        TEXT("Select all previous imports through the actual button"),
        [this] { return Flow.Click(TEXT("Select imported")); }, 240);
    Flow.Add(TEXT("Hidden previous imports were selected"),
        [this]
        {
            const FString Counts = Flow.TaggedText(TEXT("KadanImportCounts"));
            TArray<FString> CountWords;
            Counts.ParseIntoArray(CountWords, TEXT(" "), true);
            const int32 Selected = CountWords.Num() > 1 ? FCString::Atoi(*CountWords[CountWords.Num() - 2]) : 0;
            return Flow.Expect(Counts.StartsWith(TEXT("0 of ")) && Selected >= 1500 &&
                    Flow.ButtonEnabled(TEXT("Reimport")) &&
                    Flow.TaggedText(TEXT("KadanStatus")).Contains(TEXT("across all folders and filters")),
                TEXT("Select imported depends on visible rows or selected no retained imports: ") + Counts);
        });
    Flow.Add(TEXT("Capture imported selection with all rows hidden"),
        [this] { return Flow.Screenshot(TEXT("imported-selection")); });
    Flow.Add(TEXT("Clear imported selection before bounded reimport"),
        [this] { return Flow.Click(TEXT("Clear selection")); });
    Flow.Add(TEXT("Find one actual retained game texture"), [this]
        { return Flow.SetField(TEXT("Search"), TEXT("/Game/Carrot4/Characters/Buddy/Happy/T_chr_Happy_00_T00_D")); });
    Flow.Add(TEXT("Select only this visible texture"), [this] { return Flow.Click(TEXT("Select visible")); });
    Flow.Add(TEXT("One bounded target is selected"),
        [this]
        {
            return Flow.Expect(Flow.TaggedText(TEXT("KadanImportCounts")).EndsWith(TEXT("1 selected")),
                TEXT("Bounded texture reimport selected other content"));
        });
    Flow.Add(TEXT("Reimport through the production button"), [this] { return Flow.Click(TEXT("Reimport")); }, 180);
    Flow.Add(TEXT("Reimport completed using ordinary import results"),
        [this]
        {
            const FString Status = Flow.TaggedText(TEXT("KadanStatus"));
            return Flow.Expect(
                Status.StartsWith(TEXT("Kadan imported 1 package")) && !Status.Contains(TEXT("could not")),
                TEXT("Reimport did not refresh the retained texture: ") + Status);
        });
    Flow.Add(TEXT("Capture completed reimport"), [this] { return Flow.Screenshot(TEXT("reimport")); });
}

FString FBrowserStage::QualificationScope() const
{
    return TEXT("Catalog, automatic setup, folder/search UI only; no asset construction, cook, "
                "export or game acceptance");
}

TUniquePtr<FFlowStage> MakeSetupStage(FFlow& Flow)
{
    return MakeUnique<FSetupStage>(Flow);
}

TUniquePtr<FFlowStage> MakeSetupReadyStage(FFlow& Flow)
{
    return MakeUnique<FSetupReadyStage>(Flow);
}

TUniquePtr<FFlowStage> MakeBrowserStage(FFlow& Flow)
{
    return MakeUnique<FBrowserStage>(Flow);
}

TUniquePtr<FFlowStage> MakeReimportStage(FFlow& Flow)
{
    return MakeUnique<FReimportStage>(Flow);
}
} // namespace P4KadanCreatorFlow
#endif

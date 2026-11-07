#include "P4KadanCreatorFlowStageList.h"
#include "P4KadanCreatorFlowShared.h"

#if P4_MAPKIT_TESTS
namespace P4KadanCreatorFlow
{

namespace
{
// MapProgress: real lighting and navigation exports of an edited game map, with their stage windows.
class FMapProgressStage final : public FFlowStage
{
public:
    using FFlowStage::FFlowStage;

    void AddSteps() override;

    void Stop() override;

private:
    bool MapProgressCollision();

    FFlowAssets Assets{Flow};
    FExportProgressCapture Progress{Flow};
};
} // namespace

void FMapProgressStage::AddSteps()
{
    Flow.Add(
        TEXT("Open and prepare independent map and cube fixtures"), [this]() { return Assets.MapProgressFixture(); },
        180);
    Flow.Add(TEXT("Open the actual Mods panel"), [this]() { return Flow.Menu(TEXT("Mods and export"), TEXT("Mods")); });
    Flow.Add(TEXT("Choose the retained second mod"), [this]() { return Flow.Choose(Flow.B); });
    Flow.Add(TEXT("Remove the previous orphan-level root"),
        [this]()
        {
            const auto List = Flow.Tagged(TEXT("KadanSelectionRoots"));
            if (List &&
                StaticCastSharedPtr<SListView<TSharedPtr<FP4ExportRoot>>>(List)->GetNumItemsBeingObserved() == 0)
            {
                return true;
            }
            return Flow.Click(TEXT("Remove"));
        });
    Flow.Add(TEXT("Open shared source settings"), [this]() { return Flow.Click(TEXT("Import")); });
    Flow.Add(TEXT("Open Advanced"), [this]() { return Flow.Expand(TEXT("KadanAdvanced"), TEXT("Advanced"), true); });
    Flow.Add(TEXT("Enable the extracted-folder field through its checkbox"),
        [this]() { return Flow.CheckTag(TEXT("KadanUseExtracted"), true); });
    Flow.Add(TEXT("Use the actual converted original packages for rebuilding"),
        [this]()
        {
            return Flow.SetField(
                TEXT("Extracted packages"), FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_UX_GAME")));
        });
    Flow.Add(TEXT("Restore the normal game catalog after selecting rebuild inputs"),
        [this]() { return Flow.CheckTag(TEXT("KadanUseExtracted"), false); });
    Flow.Add(TEXT("Return to Mods"), [this]() { return Flow.Click(TEXT("Mods")); });
    Flow.Add(TEXT("Select the copied original map in Content Browser"),
        [this]() { return Assets.SelectFixture(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_UX_MAP"))); });
    Flow.Add(TEXT("Add the original map through its production control"),
        [this]() { return Flow.Click(TEXT("Add selected assets")); });
    Flow.Add(TEXT("Show all derived content"), [this]() { return Flow.Click(TEXT("All")); });
    Flow.Add(
        TEXT("Review the actual map edit"), [this]() { return Flow.ClickQueuedExport(TEXT("Review export")); }, 180);
    Flow.Add(TEXT("A real map edit passes review"),
        [this]()
        {
            return Flow.Expect(Flow.ButtonEnabled(TEXT("Export mod")),
                TEXT("The actual map edit did not pass ordinary export review"));
        });
    Flow.Add(TEXT("Capture the reviewed map edit"), [this]() { return Flow.Screenshot(TEXT("review")); });
    Flow.Add(TEXT("Observe actual lighting progress"),
        [this]() { return Progress.ObserveExportProgress(false, TEXT("map-lighting")); });
    Flow.Add(
        TEXT("Export the added static cube through the panel"),
        [this]() { return Flow.ClickQueuedExport(TEXT("Export mod")); }, 600);
    Flow.Add(TEXT("Record the actual lighting timeline"),
        [this]()
        {
            if (!Progress.SaveProgressTimeline(TEXT("map-lighting")))
            {
                return false;
            }
            return Flow.Expect(Progress.ProgressStages.Contains(TEXT("Lighting")) &&
                    Progress.ProgressStages.Contains(TEXT("Publishing")) &&
                    Flow.TaggedText(TEXT("KadanStatus")).StartsWith(TEXT("Kadan exported the mod to ")),
                TEXT("The actual lighting export did not reach publication"));
        });
    Flow.Add(TEXT("Capture the lighting export result"), [this]() { return Flow.Screenshot(TEXT("lighting-result")); });
    Flow.Add(TEXT("Add real terrain collision to the owned cube"), [this]() { return MapProgressCollision(); });
    Flow.Add(
        TEXT("Review the navigation-changing map edit"),
        [this]() { return Flow.ClickQueuedExport(TEXT("Review export")); }, 180);
    Flow.Add(TEXT("The actual navigation edit passes review"),
        [this]()
        {
            return Flow.Expect(Flow.ButtonEnabled(TEXT("Export mod")),
                TEXT("The actual navigation edit did not pass ordinary export review"));
        });
    Flow.Add(TEXT("Observe actual navigation progress"),
        [this]() { return Progress.ObserveExportProgress(false, TEXT("map-navigation")); });
    Flow.Add(
        TEXT("Export the actual navigation-changing edit"),
        [this]() { return Flow.ClickQueuedExport(TEXT("Export mod")); }, 600);
    Flow.Add(TEXT("Record the actual navigation timeline"),
        [this]()
        {
            if (!Progress.SaveProgressTimeline(TEXT("map-navigation")))
            {
                return false;
            }
            return Flow.Expect(Progress.ProgressStages.Contains(TEXT("Navigation")) &&
                    Progress.ProgressStages.Contains(TEXT("Publishing")) &&
                    Flow.TaggedText(TEXT("KadanStatus")).StartsWith(TEXT("Kadan exported the mod to ")),
                TEXT("The actual navigation export did not reach publication"));
        });
    Flow.Add(
        TEXT("Capture the navigation export result"), [this]() { return Flow.Screenshot(TEXT("navigation-result")); });
}

void FMapProgressStage::Stop()
{
    Progress.StopProgressCapture();
}

bool FMapProgressStage::MapProgressCollision()
{
    UWorld* World = GEditor->GetEditorWorldContext().World();
    AActor* Actor = World ? FindObject<AActor>(World->PersistentLevel, TEXT("KadanProgressCube")) : nullptr;
    const auto Types = P4ImportedGroundTypes();
    if (!Actor || Types.Num() == 0)
    {
        return Flow.Expect(false, TEXT("The map's imported ground types or the owned cube are missing"));
    }
    FString Error;
    if (!P4SetCreatorGroundType(Actor, Types[0], Error))
    {
        return Flow.Expect(false, Error);
    }
    return Flow.Expect(UEditorLoadingAndSavingUtils::SavePackages({World->GetOutermost()}, true),
        TEXT("The collision change could not save"));
}

TUniquePtr<FFlowStage> MakeMapProgressStage(FFlow& Flow)
{
    return MakeUnique<FMapProgressStage>(Flow);
}
} // namespace P4KadanCreatorFlow
#endif

#include "P4KadanCreatorFlowStageList.h"
#include "P4KadanCreatorFlowShared.h"

#if P4_MAPKIT_TESTS
namespace P4KadanCreatorFlow
{

namespace
{
// Create: the full creator flow from import through a published export, in one editor.
class FCreateStage final : public FFlowStage
{
public:
    using FFlowStage::FFlowStage;

    void AddSteps() override;

    void Finish() override;

private:
    // Recover the owned fixture and open Import.
    void AddFixtureSteps();

    // A missing extracted folder, its retained failure, then a canceled and retried scan.
    void AddSourceFailureSteps();

    // Search, Clear filters and the kind picker.
    void AddFilterSteps();

    // Review, cancel and retry a real texture import from the extracted folder.
    void AddExtractedImportSteps();

    // Browse, review and import from the game's own catalog, then invalidate it.
    void AddCatalogImportSteps();

    // The page buttons keep each page and the Details Output Log action.
    void AddPageNavigationSteps();

    // The Asset page inspects creator and original textures and ground types.
    void AddAssetInspectionSteps();

    // An invalid identity, then the two fixture mods and the first one's texture root.
    void AddIdentitySteps();

    // Folder roots, a malformed roots file and independent selections.
    void AddRootSteps();

    // Unsaved identity, a missing tool and a blocked output each stop the export.
    void AddExportFailureSteps();

    // A real export and its file list, then root removal.
    void AddExportSteps();

    // Close and reopen the panel.
    void AddReopenSteps();

    FFlowAssets Assets{Flow};
    FFlowModSteps ModSteps{Flow};
    FString StoredImportFailure, StoredExportFailure;
    FString SelectionBackup, SelectionError;
};

// Reopen: a second editor process keeps what Create saved and published.
class FReopenStage final : public FFlowStage
{
public:
    using FFlowStage::FFlowStage;

    void AddSteps() override;

private:
    FFlowModSteps ModSteps{Flow};
};
} // namespace

void FCreateStage::AddSteps()
{
    AddFixtureSteps();
    AddSourceFailureSteps();
    AddFilterSteps();
    AddExtractedImportSteps();
    AddCatalogImportSteps();
    AddPageNavigationSteps();
    AddAssetInspectionSteps();
    AddIdentitySteps();
    AddRootSteps();
    AddExportFailureSteps();
    AddExportSteps();
    AddReopenSteps();
}

void FCreateStage::AddFixtureSteps()
{
    Flow.Add(TEXT("Close any restored panel before owned fixture recovery"),
        [this]
        {
            auto Panel = Flow.Tab();
            return !Panel || Panel->RequestCloseTab();
        });
    Flow.Add(TEXT("Observe restored panel closed before fixture recovery"), [this] { return !Flow.Tab(); });
    Flow.Add(TEXT("Construct native fixtures in the owned project"), [this] { return Assets.Fixture(); });
    Flow.Add(TEXT("Use setup configuration in a fresh editor"),
        [this]
        {
            FString Error;
            return Flow.Expect(P4KadanProjectReady(Error),
                TEXT("Creator UI import needs completed Setup and editor restart: ") + Error);
        });
    Flow.Add(TEXT("Menu Import from game"),
        [this] { return Flow.Menu(TEXT("Import from game"), TEXT("Choose Pikmin 4's romfs or Paks folder.")); });
}

void FCreateStage::AddSourceFailureSteps()
{
    Flow.Add(TEXT("Empty import has disabled selection/review actions"),
        [this]
        {
            return Flow.Expect(
                !Flow.ButtonEnabled(TEXT("Import selection")) && !Flow.ButtonEnabled(TEXT("Review required content")),
                TEXT("Empty import enables actions"));
        });
    Flow.Add(TEXT("Expand Advanced through its actual header"),
        [this] { return Flow.Expand(TEXT("KadanAdvanced"), TEXT("Advanced"), true); });
    Flow.Add(TEXT("Configure actual retoc for shared source"), [this]
        { return Flow.SetField(TEXT("retoc"), Flow.Tool(TEXT("P4_NATIVE_RETOC"), TEXT("private/retoc/retoc.exe"))); });
    Flow.Add(TEXT("Configure actual repak for shared source"), [this]
        { return Flow.SetField(TEXT("repak"), Flow.Tool(TEXT("P4_NATIVE_REPAK"), TEXT("private/repak/repak.exe"))); });
    Flow.Add(TEXT("Choose the Advanced extracted-folder override"),
        [this] { return Flow.CheckTag(TEXT("KadanUseExtracted"), true); });
    Flow.Add(TEXT("Enter missing game folder"),
        [this] { return Flow.SetField(TEXT("Extracted packages"), Flow.Out / TEXT("missing-source")); });
    Flow.Add(TEXT("Read missing folder through UI"), [this] { return Flow.Click(TEXT("Refresh game folder")); });
    Flow.Add(TEXT("Scan failure explains the folder problem in the panel"),
        [this]
        {
            return Flow.Expect(
                Flow.TaggedText(TEXT("KadanStatus")).StartsWith(TEXT("The game folder could not be read.")),
                TEXT("Scan failure hides its reason"));
        });
    Flow.Add(TEXT("Retain the exact source failure before navigation"),
        [this]
        {
            StoredImportFailure = Flow.TaggedText(TEXT("KadanStatus"));
            return Flow.Expect(!StoredImportFailure.IsEmpty(), TEXT("Missing source failure is empty"));
        });
    Flow.Add(TEXT("Visit Mods after an Import failure"), [this] { return Flow.Click(TEXT("Mods")); });
    Flow.Add(TEXT("Mods has its own contextual status"),
        [this]
        {
            return Flow.Expect(Flow.TaggedText(TEXT("KadanStatus")).Contains(TEXT("Create or choose a mod")) &&
                    Flow.TaggedText(TEXT("KadanStatus")) != StoredImportFailure,
                TEXT("Import failure leaked into the Mods status"));
        });
    Flow.Add(TEXT("Return to the failed Import page"), [this] { return Flow.Click(TEXT("Import")); });
    Flow.Add(TEXT("Import navigation preserves its exact failure details"),
        [this]
        {
            return Flow.Expect(Flow.TaggedText(TEXT("KadanStatus")) == StoredImportFailure,
                TEXT("Page navigation cleared the prior Import failure"));
        });
    Flow.Add(TEXT("Use existing real game texture folder"),
        [this] { return Flow.SetField(TEXT("Extracted packages"), FPaths::GetPath(Flow.TextureHeader)); });
    Flow.Add(TEXT("Arm real scan Cancel button"), [this] { return Flow.ArmCancellation(); });
    Flow.Add(TEXT("Cancel actual game scan"), [this] { return Flow.Click(TEXT("Refresh game folder")); });
    Flow.Add(TEXT("Scan cancellation is visible and retry stays available"),
        [this]
        {
            Flow.StopCancellation();
            const FString Status = Flow.TaggedText(TEXT("KadanStatus"));
            const bool BeforeRead = Status == TEXT("The operation was canceled. Completed content is kept.");
            const bool DuringRead = Status.StartsWith(TEXT("The folder scan was canceled."));
            return Flow.Expect(
                Flow.CancelClicked && (BeforeRead || DuringRead) && Flow.ButtonEnabled(TEXT("Refresh game folder")),
                TEXT("Native scan cancel/retry feedback failed"));
        });
    Flow.Add(TEXT("Retry actual game scan"), [this] { return Flow.Click(TEXT("Refresh game folder")); });
    Flow.Add(TEXT("Real game source list is populated after retry"),
        [this]
        {
            auto List = Flow.Tagged(TEXT("KadanSourceList"));
            return Flow.Expect(List &&
                    StaticCastSharedPtr<SListView<TSharedPtr<FP4KadanBrowserRow>>>(List)->GetNumItemsBeingObserved() >
                        0,
                TEXT("Scan retry did not populate actual source packages"));
        });
}

void FCreateStage::AddFilterSteps()
{
    Flow.Add(TEXT("Type a search with no matches"),
        [this] { return Flow.SetField(TEXT("Search"), TEXT("this-package-does-not-exist-ui-test")); });
    Flow.Add(TEXT("No-match filter explains empty results"),
        [this]
        {
            return Flow.Expect(Flow.VisibleText(TEXT("No assets match these filters."), true) &&
                    !Flow.ButtonEnabled(TEXT("Select visible")),
                TEXT("No-match filter lacks feedback or still selects assets"));
        });
    Flow.Add(TEXT("Clear all filters through UI"), [this] { return Flow.Click(TEXT("Clear filters")); });
    Flow.Add(TEXT("Clear filters clears painted search and restores results"),
        [this]
        {
            auto Search = StaticCastSharedPtr<SEditableTextBox>(Flow.Tagged(TEXT("KadanSearch")));
            auto List = Flow.Tagged(TEXT("KadanSourceList"));
            return Flow.Expect(Search && Search->GetText().IsEmpty() && List &&
                    StaticCastSharedPtr<SListView<TSharedPtr<FP4KadanBrowserRow>>>(List)->GetNumItemsBeingObserved() >
                        0,
                TEXT("Cleared filter leaves stale search input/results"));
        });
    Flow.Add(
        TEXT("Choose texture kind through the native dropdown"), [this] { return Flow.ChooseKind(TEXT("Texture2D")); });
    Flow.Add(TEXT("Clear filters also resets the kind picker"), [this] { return Flow.Click(TEXT("Clear filters")); });
    Flow.Add(TEXT("Kind picker paints the cleared kind"),
        [this]
        {
            auto Picker = StaticCastSharedPtr<SComboBox<TSharedPtr<FString>>>(Flow.Tagged(TEXT("KadanKindPicker")));
            return Flow.Expect(Picker && Picker->GetSelectedItem() && *Picker->GetSelectedItem() == TEXT("All kinds"),
                TEXT("Clear filters did not reset kind selection"));
        });
}

void FCreateStage::AddExtractedImportSteps()
{
    Flow.Add(TEXT("Search the exact source path without an Apply step"),
        [this] { return Flow.SetField(TEXT("Search"), Flow.TexturePackage); });
    Flow.Add(TEXT("Live source-path search lists only the target texture"),
        [this]
        {
            auto List = Flow.Tagged(TEXT("KadanSourceList"));
            return Flow.Expect(List &&
                    StaticCastSharedPtr<SListView<TSharedPtr<FP4KadanBrowserRow>>>(List)->GetNumItemsBeingObserved() ==
                        1,
                TEXT("Typing a source-path search did not immediately filter the source list"));
        });
    Flow.Add(TEXT("Select the actual source-row checkbox"), [this] { return Flow.CheckSource(true); });
    Flow.Add(TEXT("Review the checked source row"), [this] { return Flow.Click(TEXT("Review required content")); });
    Flow.Add(TEXT("Uncheck source row after review"), [this] { return Flow.CheckSource(false); });
    Flow.Add(TEXT("Row checkbox changes invalidate reviewed selection"),
        [this]
        {
            return Flow.Expect(
                Flow.TaggedText(TEXT("KadanImportReview")).IsEmpty() && !Flow.ButtonEnabled(TEXT("Import selection")),
                TEXT("Source-row deselection leaves a stale import review"));
        });
    Flow.Add(TEXT("Select visible texture"), [this] { return Flow.Click(TEXT("Select visible")); });
    Flow.Add(TEXT("Review selected texture"), [this] { return Flow.Click(TEXT("Review required content")); });
    Flow.Add(TEXT("Selected source and review agree"),
        [this]
        {
            return Flow.Expect(Flow.TaggedText(TEXT("KadanImportReview")).StartsWith(TEXT("1 selected")),
                TEXT("Review does not match source selection"));
        });
    Flow.Add(TEXT("Clear selection after a review"), [this] { return Flow.Click(TEXT("Clear selection")); });
    Flow.Add(TEXT("Selection change clears stale review"),
        [this]
        {
            return Flow.Expect(
                Flow.TaggedText(TEXT("KadanImportReview")).IsEmpty() && !Flow.ButtonEnabled(TEXT("Import selection")),
                TEXT("Clearing selection leaves stale review or import enabled"));
        });
    Flow.Add(TEXT("Reselect texture for actual import"), [this] { return Flow.Click(TEXT("Select visible")); });
    Flow.Add(TEXT("Disable resume for cancellation on a repeat run"), [this] { return Flow.ResumeCheck(false); });
    Flow.Add(TEXT("Arm actual import Cancel button"), [this] { return Flow.ArmCancellation(); });
    Flow.Add(
        TEXT("Cancel native texture import through UI"), [this] { return Flow.Click(TEXT("Import selection")); }, 180);
    Flow.Add(TEXT("Import cancellation explains preserved work and resume"),
        [this]
        {
            Flow.StopCancellation();
            return Flow.Expect(Flow.CancelClicked &&
                    (Flow.TaggedText(TEXT("KadanStatus"))
                            .Contains(TEXT("The import was canceled. Completed assets are kept. Use Import "
                                           "selection to continue.")) ||
                        Flow.TaggedText(TEXT("KadanStatus"))
                            .Contains(TEXT("The operation was canceled. Completed content is kept."))),
                TEXT("Native import cancel does not explain preserved work/resume"));
        });
    Flow.Add(TEXT("Enable resume after cancellation"), [this] { return Flow.ResumeCheck(true); });
    Flow.Add(TEXT("Retry texture import through UI"), [this] { return Flow.Click(TEXT("Import selection")); }, 180);
    Flow.Add(TEXT("Actual selected game texture was imported faithfully"),
        [this] { return ModSteps.VerifyImportedTexture(); });
}

void FCreateStage::AddCatalogImportSteps()
{
    Flow.Add(TEXT("Enter the real original game folder"),
        [this]
        {
            return Flow.SetField(
                TEXT("Game folder"), FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS")));
        });
    Flow.Add(TEXT("Switch from Advanced override to normal game catalog"),
        [this] { return Flow.CheckTag(TEXT("KadanUseExtracted"), false); });
    Flow.Add(TEXT("Collapse Advanced through its actual header"),
        [this] { return Flow.Expand(TEXT("KadanAdvanced"), TEXT("Advanced"), false); });
    Flow.Add(
        TEXT("Clear filters before original-container browsing"), [this] { return Flow.Click(TEXT("Clear filters")); });
    Flow.Add(
        TEXT("Commit the original folder to browse it automatically"),
        [this] { return Flow.CommitField(TEXT("Game folder")); }, 180);
    Flow.Add(TEXT("Original catalog populates the native list"),
        [this]
        {
            return Flow.Expect(
                Flow.SourceCount() > 4 && Flow.TaggedText(TEXT("KadanStatus")).Contains(TEXT("ready to browse")),
                TEXT("Original-container catalog did not populate the browser"));
        });
    Flow.Add(
        TEXT("Select a real top-level folder through its native tree row"), [this] { return Flow.SelectTopFolder(); });
    Flow.Add(TEXT("Folder tree selection filters and paints its actual path"),
        [this]
        {
            return Flow.Expect(
                Flow.TaggedText(TEXT("KadanFolderPath")) == TEXT("/Game/Carrot4") && Flow.SourceCount() > 0,
                TEXT("The folder tree did not update its breadcrumb and source list"));
        });
    Flow.Add(TEXT("Clear filters also clears the folder tree selection"),
        [this] { return Flow.Click(TEXT("Clear filters")); });
    Flow.Add(TEXT("Clearing the folder tree returns to all folders"),
        [this]
        {
            return Flow.Expect(
                Flow.TaggedText(TEXT("KadanFolderPath")) == TEXT("All folders") && Flow.SourceCount() > 4,
                TEXT("Clear filters retained a hidden folder restriction"));
        });
    Flow.Add(TEXT("Sticky import actions remain visible in the compact native panel"),
        [this] { return Flow.StickyImportActions(); });
    Flow.Add(TEXT("Capture full original-container browser"), [this] { return Flow.Screenshot(TEXT("catalog")); });
    Flow.Add(TEXT("Filter catalog to the qualified real game texture"),
        [this] { return Flow.SetField(TEXT("Search"), Flow.TexturePackage); });
    Flow.Add(TEXT("Catalog row shows a friendly asset name"),
        [this]
        {
            return Flow.Expect(
                Flow.SourceCount() == 1 && Flow.VisibleText(FPackageName::GetLongPackageAssetName(Flow.TexturePackage)),
                TEXT("Catalog filter or friendly asset row differs from selected source"));
        });
    Flow.Add(TEXT("Select the original catalog row through its checkbox"), [this] { return Flow.CheckSource(true); });
    Flow.Add(
        TEXT("Review acquires only the selected real original source"),
        [this] { return Flow.Click(TEXT("Review required content")); }, 180);
    Flow.Add(TEXT("Catalog review has a real selected-source plan"),
        [this]
        {
            const FString Base = FPaths::ProjectSavedDir() / TEXT("Kadan/RequestedOriginals/Carrot4/Content") /
                Flow.TexturePackage.Mid(6);
            return Flow.Expect(Flow.TaggedText(TEXT("KadanImportReview")).StartsWith(TEXT("1 selected")) &&
                    (IFileManager::Get().FileSize(*(Base + TEXT(".uasset"))) > 0 ||
                        IFileManager::Get().FileSize(
                            *(P4KadanCatalogGameFolder() / TEXT("Carrot4/Content") / Flow.TexturePackage.Mid(6) +
                                TEXT(".uasset"))) > 0),
                TEXT("Catalog review did not acquire a real converted selected source"));
        });
    Flow.Add(
        TEXT("Import selection from the original-container workflow"),
        [this] { return Flow.Click(TEXT("Import selection")); }, 180);
    Flow.Add(TEXT("Original-container workflow preserves faithful imported texture"),
        [this] { return ModSteps.VerifyImportedTexture(); });
    Flow.Add(TEXT("Changing the game source invalidates selected catalog rows"),
        [this] { return Flow.SetField(TEXT("Game folder"), Flow.Out / TEXT("missing-source")); });
    Flow.Add(TEXT("Changed source cannot import a stale selection"),
        [this]
        {
            return Flow.Expect(Flow.SourceCount() == 0 && !Flow.ButtonEnabled(TEXT("Import selection")) &&
                    Flow.TaggedText(TEXT("KadanImportReview")).IsEmpty(),
                TEXT("Changed game folder retained stale reviewed selection"));
        });
    Flow.Add(TEXT("Restore actual game source after invalidation"),
        [this]
        {
            return Flow.SetField(
                TEXT("Game folder"), FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS")));
        });
    Flow.Add(
        TEXT("Commit the restored game folder for automatic catalog reading"),
        [this] { return Flow.CommitField(TEXT("Game folder")); }, 180);
    Flow.Add(TEXT("Reselect retained catalog texture"), [this] { return Flow.CheckSource(true); });
    Flow.Add(TEXT("Capture import UI with successful result"), [this] { return Flow.Screenshot(TEXT("import")); });
}

void FCreateStage::AddPageNavigationSteps()
{
    Flow.Add(TEXT("Open Asset using the in-panel page button"), [this] { return Flow.Click(TEXT("Asset")); });
    Flow.Add(TEXT("Asset page navigation shows the expected heading"),
        [this]
        {
            return Flow.Expect(Flow.VisibleText(TEXT("What you can change")) &&
                    Flow.TaggedText(TEXT("KadanStatus")).Contains(TEXT("Select assets in the Content Browser")),
                TEXT("Asset page navigation does not show its own context"));
        });
    Flow.Add(TEXT("Open Mods using the in-panel page button"), [this] { return Flow.Click(TEXT("Mods")); });
    Flow.Add(TEXT("Mods page navigation shows the expected heading"),
        [this] { return Flow.Expect(Flow.VisibleText(TEXT("Mods")), TEXT("Mods page button did not navigate")); });
    Flow.Add(TEXT("Return to Import using the in-panel page button"), [this] { return Flow.Click(TEXT("Import")); });
    Flow.Add(TEXT("Import page retains selection across navigation"),
        [this]
        {
            return Flow.Expect(Flow.VisibleText(TEXT("Choose Pikmin 4's romfs or Paks folder."), true) &&
                    Flow.TaggedText(TEXT("KadanImportCounts")).Contains(TEXT("1 selected")),
                TEXT("Page navigation lost the import selection"));
        });
    Flow.Add(TEXT("Expand actual persistent Details header"),
        [this] { return Flow.Expand(TEXT("KadanDetails"), TEXT("Details"), true); });
    Flow.Add(TEXT("Open full details with the persistent Output Log action"),
        [this] { return Flow.Click(TEXT("Open Output Log")); });
    Flow.Add(TEXT("Output Log action invokes the actual editor log tab"),
        [this]
        {
            return Flow.Expect(
                FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(FName(TEXT("OutputLog")))).IsValid(),
                TEXT("Output Log action did not open the editor tab"));
        });
    Flow.Add(TEXT("Close the test-opened Output Log after its navigation proof"),
        [this]
        {
            auto Log = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(FName(TEXT("OutputLog"))));
            return !Log || Log->RequestCloseTab();
        });
    Flow.Add(TEXT("Collapse Details after log navigation"),
        [this] { return Flow.Expand(TEXT("KadanDetails"), TEXT("Details"), false); });
}

void FCreateStage::AddAssetInspectionSteps()
{
    Flow.Add(
        TEXT("Menu Selected asset"), [this] { return Flow.Menu(TEXT("Selected asset"), TEXT("What you can change")); });
    Flow.Add(TEXT("Select a creator texture for live asset inspection"),
        [this]
        {
            UObject* Texture = LoadObject<UTexture2D>(nullptr, *(Flow.BPackage + TEXT(".Texture")));
            if (!Texture)
                return Flow.Expect(false, TEXT("Creator texture fixture missing"));
            Flow.Browser().SyncBrowserToAssets(TArray<UObject*>{Texture});
            return true;
        });
    Flow.Add(TEXT("Inspect Content Browser selection through UI"),
        [this] { return Flow.Click(TEXT("Inspect assets selected in Content Browser")); });
    Flow.Add(TEXT("Asset UI agrees with native edit policy"),
        [this]
        {
            auto Policy = P4Kadan::GetAssetPolicy(LoadObject<UTexture2D>(nullptr, *(Flow.BPackage + TEXT(".Texture"))));
            return Flow.Expect(
                Flow.VisibleText(
                    Policy.AllowEdit ? TEXT("Texture is editable.") : TEXT("Texture is read-only."), true) &&
                    (Policy.Reason.IsEmpty() || Flow.VisibleText(Policy.Reason, true)),
                TEXT("Asset inspector does not display the native edit policy"));
        });
    Flow.Add(TEXT("Select the actual imported original for read-only inspection"),
        [this]
        {
            UObject* Texture = LoadObject<UTexture2D>(nullptr,
                *(Flow.TexturePackage + TEXT(".") + FPackageName::GetLongPackageAssetName(Flow.TexturePackage)));
            if (!Texture)
                return Flow.Expect(false, TEXT("Original texture missing from UI fixture"));
            Flow.Browser().SyncBrowserToAssets(TArray<UObject*>{Texture});
            return true;
        });
    Flow.Add(TEXT("Inspect actual original through UI"),
        [this] { return Flow.Click(TEXT("Inspect assets selected in Content Browser")); });
    Flow.Add(TEXT("Original inspection explains its real read-only policy"),
        [this]
        {
            auto Policy = P4Kadan::GetAssetPolicy(LoadObject<UTexture2D>(nullptr,
                *(Flow.TexturePackage + TEXT(".") + FPackageName::GetLongPackageAssetName(Flow.TexturePackage))));
            return Flow.Expect(!Policy.AllowEdit && !Policy.Reason.IsEmpty() && Flow.VisibleText(Policy.Reason, true) &&
                    Flow.VisibleText(
                        FPackageName::GetLongPackageAssetName(Flow.TexturePackage) + TEXT(" is read-only."), true),
                TEXT("Original inspector omits the protected asset policy"));
        });
    Flow.Add(TEXT("Capture actual original asset inspection"), [this] { return Flow.Screenshot(TEXT("asset")); });
    Flow.Add(
        TEXT("Refresh ground types through UI"), [this] { return Flow.Click(TEXT("Refresh imported ground types")); });
    Flow.Add(TEXT("No imported collision explains how to enable ground types"),
        [this]
        {
            return Flow.Expect(
                Flow.TaggedText(TEXT("KadanStatus")).Contains(TEXT("Import game scenery with collision")) &&
                    !Flow.ButtonEnabled(TEXT("Use this ground type on selected actors")),
                TEXT("Empty ground types enable application or hide required import"));
        });
}

void FCreateStage::AddIdentitySteps()
{
    Flow.Add(TEXT("Menu Mods and export"), [this] { return Flow.Menu(TEXT("Mods and export"), TEXT("Mods")); });
    Flow.Add(TEXT("Capture compact Mods form"), [this] { return Flow.Screenshot(TEXT("mods")); });
    Flow.Add(TEXT("Save an invalid new mod identity"), [this] { return Flow.Click(TEXT("New mod")); });
    Flow.Add(TEXT("Enter an invalid ID"), [this] { return Flow.SetField(TEXT("ID"), TEXT("Invalid ID")); });
    Flow.Add(TEXT("Reject invalid mod identity through UI"), [this] { return Flow.Click(TEXT("Save mod")); });
    Flow.Add(TEXT("Identity failure is visible and does not enable export"),
        [this]
        {
            return Flow.Expect(Flow.TaggedText(TEXT("KadanStatus")).Contains(TEXT("Invalid format or mod ID")) &&
                    !Flow.ButtonEnabled(TEXT("Review export")),
                TEXT("Invalid ID has no clear feedback or enables review"));
        });
    ModSteps.AddNewMod(Flow.A);
    Flow.Add(TEXT("Select first texture in Content Browser"),
        [this]
        {
            UObject* Texture = LoadObject<UTexture2D>(nullptr, *(Flow.APackage + TEXT(".Texture")));
            if (!Texture)
            {
                Flow.Fail(TEXT("First fixture is missing"));
                return true;
            }
            Flow.Browser().SyncBrowserToAssets(TArray<UObject*>{Texture});
            return true;
        });
    Flow.Add(TEXT("Confirm Content Browser package selection"),
        [this]
        {
            TArray<FAssetData> Selected;
            Flow.Browser().GetSelectedAssets(Selected);
            return Selected.Num() == 1 && Selected[0].PackageName == FName(*Flow.APackage);
        });
    Flow.Add(
        TEXT("Add selected package through the panel"), [this] { return Flow.Click(TEXT("Add selected assets")); });
    Flow.Add(TEXT("Verify first mod package root"), [this] { return Flow.CheckRoots(Flow.A, {Flow.APackage}); });
    Flow.Add(TEXT("Adding the same package twice gives useful feedback"),
        [this] { return Flow.Click(TEXT("Add selected assets")); });
    Flow.Add(TEXT("Duplicate package keeps one root"),
        [this]
        {
            if (!Flow.CheckRoots(Flow.A, {Flow.APackage}))
                return false;
            return Flow.Expect(Flow.TaggedText(TEXT("KadanStatus")).Contains(TEXT(" is already included.")),
                TEXT("Duplicate selection does not report its existing root"));
        });
    ModSteps.AddNewMod(Flow.B);
}

void FCreateStage::AddRootSteps()
{
    Flow.Add(TEXT("Enter invalid folder path"),
        [this] { return Flow.SetField(TEXT("Content folder"), TEXT("C:/not-a-package")); });
    Flow.Add(TEXT("Reject invalid folder through UI"), [this] { return Flow.Click(TEXT("Add folder")); });
    Flow.Add(TEXT("Folder failure is visible and selection stays empty"),
        [this]
        {
            return Flow.Expect(Flow.TaggedText(TEXT("KadanStatus")).Contains(TEXT("canonical /Game")),
                TEXT("Invalid folder input has no actionable reason"));
        });
    Flow.Add(TEXT("Select second folder in Content Browser"),
        [this]
        {
            Flow.Browser().SyncBrowserToFolders({Flow.BFolder});
            return true;
        });
    Flow.Add(TEXT("Confirm Content Browser folder selection"),
        [this]
        {
            TArray<FString> Selected;
            Flow.Browser().GetSelectedPathViewFolders(Selected);
            const bool Match = Selected.Num() == 1 && Selected[0] == Flow.BFolder;
            if (Match)
                Flow.FolderRoute = TEXT("path_view");
            return Match;
        });
    Flow.Add(
        TEXT("Add selected folder through the panel"), [this] { return Flow.Click(TEXT("Add selected folders")); });
    Flow.Add(TEXT("Verify second mod folder root"), [this] { return Flow.CheckRoots(Flow.B, {Flow.BFolder}); });
    Flow.Add(TEXT("Open selected mod content folder"), [this] { return Flow.Click(TEXT("Open content folder")); });
    Flow.Add(TEXT("Verify the Content Browser opens the selected mod's namespace"),
        [this]
        {
            TArray<FString> Paths;
            Flow.Browser().GetSelectedPathViewFolders(Paths);
            return Flow.Expect(
                Paths.Contains(Flow.BNames.PackageRoot), TEXT("Open mod content folder targets a different namespace"));
        });
    Flow.Add(TEXT("Back up and corrupt only the owned first mod's roots"),
        [this]
        {
            const FString File = FPaths::ProjectDir() / TEXT("Config/KadanSelections") / (Flow.A.Id + TEXT(".json"));
            if (!FFileHelper::LoadFileToString(SelectionBackup, *File) ||
                !FFileHelper::SaveStringToFile(TEXT("{}"), *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
                return Flow.Expect(false, TEXT("Cannot prepare owned malformed-root UI fixture"));
            TArray<FP4ExportRoot> Roots;
            return Flow.Expect(!P4ReadExportRoots(FPaths::ProjectDir(), Flow.A, Roots, SelectionError),
                TEXT("Malformed-root fixture did not fail independently"));
        });
    Flow.Add(TEXT("Switch to mod whose saved roots cannot be read"), [this] { return Flow.Choose(Flow.A); });
    Flow.Add(TEXT("Selection failure survives the mod switch"),
        [this]
        {
            return Flow.Expect(
                Flow.TaggedText(TEXT("KadanStatus")) == SelectionError && !Flow.ButtonEnabled(TEXT("Review export")),
                TEXT("Mod switch hid the saved-root error or enabled review"));
        });
    Flow.Add(TEXT("Restore exact owned roots after failure"),
        [this]
        {
            const bool Saved = FFileHelper::SaveStringToFile(SelectionBackup,
                *(FPaths::ProjectDir() / TEXT("Config/KadanSelections") / (Flow.A.Id + TEXT(".json"))),
                FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            if (Saved)
                SelectionBackup.Empty();
            return Flow.Expect(Saved, TEXT("Cannot restore owned selection fixture"));
        });
    Flow.Add(TEXT("Switch away before retrying saved roots"), [this] { return Flow.Choose(Flow.B); });
    Flow.Add(TEXT("Switch to first mod"), [this] { return Flow.Choose(Flow.A); });
    Flow.Add(TEXT("First selection is independent"), [this] { return Flow.CheckRoots(Flow.A, {Flow.APackage}); });
    Flow.Add(TEXT("Capture first mod"), [this] { return Flow.Screenshot(TEXT("first-selected")); });
    Flow.Add(TEXT("Switch to second mod"), [this] { return Flow.Choose(Flow.B); });
    Flow.Add(TEXT("Second selection is independent"), [this] { return Flow.CheckRoots(Flow.B, {Flow.BFolder}); });
}

void FCreateStage::AddExportFailureSteps()
{
    Flow.Add(TEXT("Set export output through the form"),
        [this] { return Flow.SetField(TEXT("Output folder"), Flow.ExportFolder); });
    Flow.Add(
        TEXT("Edit saved version without saving"), [this] { return Flow.SetField(TEXT("Version"), TEXT("2.0.0")); });
    Flow.Add(TEXT("Review unsaved identity"), [this] { return Flow.ClickQueuedExport(TEXT("Review export")); });
    Flow.Add(TEXT("Unsaved identity prevents stale-version export"),
        [this]
        {
            return Flow.Expect(
                Flow.TaggedText(TEXT("KadanExportReview")).Contains(TEXT("Save the mod before reviewing its export")) &&
                    !Flow.ButtonEnabled(TEXT("Export mod")),
                TEXT("Unsaved form can export the old identity"));
        });
    Flow.Add(TEXT("Restore version before export"), [this] { return Flow.SetField(TEXT("Version"), Flow.B.Version); });
    Flow.Add(TEXT("Persist restored mod identity through UI"), [this] { return Flow.Click(TEXT("Save mod")); });
    Flow.Add(TEXT("Navigate to shared tools in Import"), [this] { return Flow.Click(TEXT("Import")); });
    Flow.Add(TEXT("Expand Advanced shared tools for failure check"),
        [this] { return Flow.Expand(TEXT("KadanAdvanced"), TEXT("Advanced"), true); });
    Flow.Add(TEXT("Set invalid retoc executable"),
        [this] { return Flow.SetField(TEXT("retoc"), Flow.Out / TEXT("missing-retoc.exe")); });
    Flow.Add(TEXT("Return to Mods after changing shared tool"), [this] { return Flow.Click(TEXT("Mods")); });
    Flow.Add(
        TEXT("Review export with invalid executable"), [this] { return Flow.ClickQueuedExport(TEXT("Review export")); },
        180);
    Flow.Add(TEXT("Export review failure keeps publication disabled"),
        [this]
        {
            return Flow.Expect(
                !Flow.TaggedText(TEXT("KadanExportReview")).IsEmpty() && !Flow.ButtonEnabled(TEXT("Export mod")),
                TEXT("Invalid tool did not block export with visible reason"));
        });
    Flow.Add(TEXT("Return to shared source tools for retry"), [this] { return Flow.Click(TEXT("Import")); });
    Flow.Add(TEXT("Restore valid retoc executable"), [this]
        { return Flow.SetField(TEXT("retoc"), Flow.Tool(TEXT("P4_NATIVE_RETOC"), TEXT("private/retoc/retoc.exe"))); });
    Flow.Add(TEXT("Collapse shared tools after valid retry"),
        [this] { return Flow.Expand(TEXT("KadanAdvanced"), TEXT("Advanced"), false); });
    Flow.Add(TEXT("Return to Mods after tool retry"), [this] { return Flow.Click(TEXT("Mods")); });
    Flow.Add(TEXT("Create only an owned blocking output file"),
        [this]
        {
            return Flow.Expect(
                FFileHelper::SaveStringToFile(TEXT("UI blocked output fixture"), *(Flow.Out / TEXT("blocked-output")),
                    FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM),
                TEXT("Cannot make owned export-failure fixture"));
        });
    Flow.Add(TEXT("Choose the blocked output path"),
        [this] { return Flow.SetField(TEXT("Output folder"), Flow.Out / TEXT("blocked-output")); });
    Flow.Add(
        TEXT("Review before an actual publication failure"),
        [this] { return Flow.ClickQueuedExport(TEXT("Review export")); }, 600);
    Flow.Add(
        TEXT("Attempt export into the blocked output"), [this] { return Flow.ClickQueuedExport(TEXT("Export mod")); },
        600);
    Flow.Add(TEXT("Export failure displays the actual reason and preserves the blocking file"),
        [this]
        {
            FString Contents;
            FFileHelper::LoadFileToString(Contents, *(Flow.Out / TEXT("blocked-output")));
            return Flow.Expect(Flow.TaggedText(TEXT("KadanStatus")).Contains(TEXT("The export stopped.")) &&
                    Flow.TaggedText(TEXT("KadanDetailsText"))
                        .Contains(TEXT("Kadan could not prepare the export work folders.")) &&
                    Contents == TEXT("UI blocked output fixture"),
                TEXT("Export failure hides its actual reason or overwrites the existing output file"));
        });
    Flow.Add(TEXT("Retain actual export failure before leaving Mods"),
        [this]
        {
            StoredExportFailure = Flow.TaggedText(TEXT("KadanStatus"));
            return Flow.Expect(
                StoredExportFailure.Contains(TEXT("Export stopped.")), TEXT("Export failure was not retained"));
        });
    Flow.Add(TEXT("Visit Import after an export failure"), [this] { return Flow.Click(TEXT("Import")); });
    Flow.Add(TEXT("Import does not display an unrelated export failure"),
        [this]
        {
            return Flow.Expect(Flow.TaggedText(TEXT("KadanStatus")) != StoredExportFailure,
                TEXT("Mods failure leaked into Import status"));
        });
    Flow.Add(TEXT("Return to Mods after the failed publication"), [this] { return Flow.Click(TEXT("Mods")); });
    Flow.Add(TEXT("Mods restores the exact actionable export failure"),
        [this]
        {
            return Flow.Expect(Flow.TaggedText(TEXT("KadanStatus")) == StoredExportFailure,
                TEXT("Page navigation cleared prior export failure details"));
        });
}

void FCreateStage::AddExportSteps()
{
    Flow.Add(TEXT("Choose valid output after publication failure"),
        [this] { return Flow.SetField(TEXT("Output folder"), Flow.ExportFolder); });
    Flow.Add(
        TEXT("Review export through the production button"),
        [this] { return Flow.ClickQueuedExport(TEXT("Review export")); }, 600);
    Flow.Add(
        TEXT("Export selected mod through the production button"),
        [this] { return Flow.ClickQueuedExport(TEXT("Export mod")); }, 1200);
    Flow.Add(
        TEXT("Verify manifest and independent complete container selection"),
        [this] { return ModSteps.VerifyExport(true); }, 600);
    Flow.Add(TEXT("Show the saved mod's file list"),
        [this]
        {
            auto List = Flow.Tagged(TEXT("KadanPublishedFiles"));
            auto Scroll = Flow.Tagged(TEXT("KadanPages"));
            if (!List || !Scroll)
                return false;
            StaticCastSharedPtr<SScrollBox>(Scroll)->ScrollDescendantIntoView(List, false);
            return true;
        });
    Flow.Add(TEXT("Compare every displayed filename with the saved mod"),
        [this]
        {
            const FString Published = Flow.ExportFolder / Flow.B.Id;
            TArray<FString> Files;
            IFileManager::Get().FindFilesRecursive(Files, *Published, TEXT("*"), true, false);
            if (Files.Num() != 4)
            {
                Flow.Fail(TEXT("The independently enumerated saved mod has unexpected files."));
                return true;
            }
            for (FString File : Files)
            {
                if (!FPaths::MakePathRelativeTo(File, *(Published + TEXT("/"))))
                {
                    Flow.Fail(TEXT("The saved mod file is outside its output folder."));
                    return true;
                }
                if (!Flow.VisibleText(File))
                    return false;
            }
            return true;
        });
    Flow.Add(TEXT("Capture successful UI export"), [this] { return Flow.Screenshot(TEXT("export")); });
    Flow.Add(TEXT("Choose first mod to clear its last root"), [this] { return Flow.Choose(Flow.A); });
    Flow.Add(
        TEXT("Remove first mod's last root through its row button"), [this] { return Flow.Click(TEXT("Remove")); });
    Flow.Add(TEXT("First mod remains empty"), [this] { return Flow.CheckRoots(Flow.A, {}); });
    Flow.Add(TEXT("Choose second mod after removing first root"), [this] { return Flow.Choose(Flow.B); });
    Flow.Add(TEXT("Removing first root preserves second selection"),
        [this] { return Flow.CheckRoots(Flow.B, {Flow.BFolder}); });
}

void FCreateStage::AddReopenSteps()
{
    Flow.Add(TEXT("Close the Kadan tab"),
        [this]
        {
            auto Panel = Flow.Tab();
            return Panel && Panel->RequestCloseTab();
        });
    Flow.Add(TEXT("Observe Kadan tab closed"), [this] { return !Flow.Tab(); });
    Flow.Add(
        TEXT("Reopen Kadan through its menu"), [this] { return Flow.Menu(TEXT("Mods and export"), TEXT("Mods")); });
    ModSteps.ReopenChecks();
}

void FCreateStage::Finish()
{
    if (!SelectionBackup.IsEmpty())
        if (!FFileHelper::SaveStringToFile(SelectionBackup,
                *(FPaths::ProjectDir() / TEXT("Config/KadanSelections") / (Flow.A.Id + TEXT(".json"))),
                FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            Flow.Test.AddError(TEXT("Could not restore exact owned roots after interrupted UI test"));
}

void FReopenStage::AddSteps()
{
    Flow.Add(TEXT("Require a successful Create in a different editor process"),
        [this]
        {
            FString Json, Project;
            TSharedPtr<FJsonObject> Previous;
            double Pid = 0;
            bool Passed = false;
            if (!FFileHelper::LoadFileToString(Json, *(FPaths::GetPath(Flow.Out) / TEXT("create/create.json"))) ||
                !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Previous) || !Previous ||
                !Previous->TryGetBoolField(TEXT("passed"), Passed) || !Passed ||
                !Previous->TryGetNumberField(TEXT("pid"), Pid) ||
                uint32(Pid) == FPlatformProcess::GetCurrentProcessId() ||
                !Previous->TryGetStringField(TEXT("project"), Project) ||
                Project != FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()))
                Flow.Fail(TEXT("Reopen needs successful Create evidence from this project and another editor PID"));
            FString Owned;
            if (!FFileHelper::LoadFileToString(Owned, *Flow.Marker) || Owned != Owner)
                Flow.Fail(TEXT("Reopen project is not the owned fixture"));
            return true;
        });
    Flow.Add(TEXT("Open Kadan through its menu in the second editor"),
        [this] { return Flow.Menu(TEXT("Mods and export"), TEXT("Mods")); });
    ModSteps.ReopenChecks();
    Flow.Add(TEXT("Published mod survives the second editor"), [this] { return ModSteps.VerifyExport(); }, 600);
    Flow.Add(TEXT("UI-imported original texture survives a fresh editor unchanged"),
        [this] { return ModSteps.VerifyImportedTexture(); });
}

TUniquePtr<FFlowStage> MakeCreateStage(FFlow& Flow)
{
    return MakeUnique<FCreateStage>(Flow);
}

TUniquePtr<FFlowStage> MakeReopenStage(FFlow& Flow)
{
    return MakeUnique<FReopenStage>(Flow);
}
} // namespace P4KadanCreatorFlow
#endif

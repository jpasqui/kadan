#include "P4KadanCreatorFlowStageList.h"
#include "P4KadanCreatorFlowShared.h"

#if P4_MAPKIT_TESTS
namespace P4KadanCreatorFlow
{

namespace
{
// IssuesBefore, Issues and Progress: export refusals with their next steps, a real publication and,
// for Progress, its stage windows and a safe cancellation.
class FIssuesStage final : public FFlowStage
{
public:
    using FFlowStage::FFlowStage;

    void AddSteps() override;

    void ObserveBusy() override;

    void Stop() override;

    FString QualificationScope() const override;

private:
    // The previous panel's refusals, captured for comparison.
    void AddBeforeSteps();

    // Identity, inventory and required-asset refusals, then their repair.
    void AddRefusalSteps();

    // The real export, its stage windows and a canceled newer export.
    void AddPublicationSteps();

    // A level without a persistent game map.
    void AddMissingMapSteps();

    bool RecordPublication(bool Compare = false);

    FFlowAssets Assets{Flow};
    FFlowModSteps ModSteps{Flow};
    FExportProgressCapture Progress{Flow};
    TMap<FString, FString> PublishedHashes;
    bool CapturedBusyPanel = false;
};

// ShippingBefore and Shipping: the shipping list of a long required-content closure in a narrow panel.
class FShippingStage final : public FFlowStage
{
public:
    using FFlowStage::FFlowStage;

    void AddSteps() override;

    FString QualificationScope() const override;

private:
    // Two mods, a material root with 24 required textures and the reviewed list.
    void AddReviewSteps();

    // Content and identity edits that keep or invalidate the review, and separate mod roots.
    void AddInvalidationSteps();

    bool ShippingFixture();

    FFlowAssets Assets{Flow};
    FFlowModSteps ModSteps{Flow};
};
} // namespace

void FIssuesStage::AddSteps()
{
    if (Flow.Stage == TEXT("IssuesBefore"))
    {
        AddBeforeSteps();
        return;
    }
    AddRefusalSteps();
    AddPublicationSteps();
    AddMissingMapSteps();
}

void FIssuesStage::ObserveBusy()
{
    if (Flow.Stage == TEXT("IssuesBefore") && !CapturedBusyPanel)
    {
        CapturedBusyPanel = true;
        Flow.Screenshot(TEXT("busy-review"));
    }
}

void FIssuesStage::Stop()
{
    Progress.StopProgressCapture();
}

FString FIssuesStage::QualificationScope() const
{
    if (Flow.Stage == TEXT("Issues") || Flow.Stage == TEXT("Progress"))
        return TEXT("Actual identity, inventory, dependency and map refusals and creator texture publication; Progress "
                    "also checks stages and safe cancellation; no game acceptance");
    return FFlowStage::QualificationScope();
}

void FIssuesStage::AddBeforeSteps()
{
    Flow.Add(TEXT("Create real owned issue assets"), [this] { return Assets.IssueFixture(); });
    Flow.Add(TEXT("Open the previous Mods panel"), [this] { return Flow.Menu(TEXT("Mods and export"), TEXT("Mods")); });
    Flow.Add(TEXT("Use a narrow panel"), [this] { return Flow.NarrowPanel(); });
    Flow.Add(TEXT("Choose the retained second mod"), [this] { return Flow.Choose(Flow.B); });
    Flow.Add(TEXT("Edit an unsaved version"), [this] { return Flow.SetField(TEXT("Version"), TEXT("2.0.0")); });
    Flow.Add(TEXT("Review the unsaved identity"), [this] { return Flow.ClickQueuedExport(TEXT("Review export")); });
    Flow.Add(
        TEXT("Capture the previous unsaved identity refusal"), [this] { return Flow.Screenshot(TEXT("identity")); });
    Flow.Add(TEXT("Restore the saved version"), [this] { return Flow.SetField(TEXT("Version"), Flow.B.Version); });
    Flow.Add(TEXT("Open Import"), [this] { return Flow.Click(TEXT("Import")); });
    Flow.Add(TEXT("Open shared tools"), [this] { return Flow.Expand(TEXT("KadanAdvanced"), TEXT("Advanced"), true); });
    Flow.Add(TEXT("Choose an absent retoc executable"),
        [this] { return Flow.SetField(TEXT("retoc"), Flow.Out / TEXT("missing-retoc.exe")); });
    Flow.Add(TEXT("Return to Mods"), [this] { return Flow.Click(TEXT("Mods")); });
    Flow.Add(TEXT("Review with an absent tool"), [this] { return Flow.ClickQueuedExport(TEXT("Review export")); }, 180);
    Flow.Add(TEXT("Capture the previous inventory refusal"), [this] { return Flow.Screenshot(TEXT("inventory")); });
    Flow.Add(TEXT("Return to shared tools"), [this] { return Flow.Click(TEXT("Import")); });
    Flow.Add(TEXT("Restore retoc"), [this]
        { return Flow.SetField(TEXT("retoc"), Flow.Tool(TEXT("P4_NATIVE_RETOC"), TEXT("private/retoc/retoc.exe"))); });
    Flow.Add(TEXT("Return to Mods after repair"), [this] { return Flow.Click(TEXT("Mods")); });
    Flow.Add(TEXT("Remove the material root"), [this] { return Flow.Click(TEXT("Remove")); });
    Flow.Add(TEXT("Select the unsupported dependency fixture"),
        [this] { return Assets.SelectFixture(Flow.BNames.PackageRoot / TEXT("IssueMaterial")); });
    Flow.Add(TEXT("Add the material root"), [this] { return Flow.Click(TEXT("Add selected assets")); });
    Flow.Add(
        TEXT("Review the unsupported dependency"), [this] { return Flow.ClickQueuedExport(TEXT("Review export")); },
        240);
    Flow.Add(TEXT("Filter attention"), [this] { return Flow.Click(TEXT("Issues")); });
    Flow.Add(
        TEXT("Capture the previous dependency refusal"), [this] { return Flow.Screenshot(TEXT("required-asset")); });
    Flow.Add(TEXT("Remove the unsupported material root"), [this] { return Flow.Click(TEXT("Remove")); });
    Flow.Add(TEXT("Select the orphan level"),
        [this] { return Assets.SelectFixture(Flow.BNames.PackageRoot / TEXT("OrphanLevel")); });
    Flow.Add(TEXT("Add the orphan level"), [this] { return Flow.Click(TEXT("Add selected assets")); });
    Flow.Add(
        TEXT("Review the level without its persistent map"),
        [this] { return Flow.ClickQueuedExport(TEXT("Review export")); }, 240);
    Flow.Add(
        TEXT("Attempt the missing-map export when enabled"),
        [this] { return !Flow.ButtonEnabled(TEXT("Export mod")) || Flow.ClickQueuedExport(TEXT("Export mod")); }, 600);
    Flow.Add(TEXT("Capture the previous map refusal"), [this] { return Flow.Screenshot(TEXT("map")); });
    Flow.Add(TEXT("Remove the orphan level root"), [this] { return Flow.Click(TEXT("Remove")); });
    Flow.Add(TEXT("Select the retained material root"),
        [this] { return Assets.SelectFixture(Flow.BNames.PackageRoot / TEXT("DependencyMaterial")); });
    Flow.Add(TEXT("Restore the retained material root"), [this] { return Flow.Click(TEXT("Add selected assets")); });
}

void FIssuesStage::AddRefusalSteps()
{
    Flow.Add(
        TEXT("Create real owned assets for refusal and missing-map checks"), [this] { return Assets.IssueFixture(); });
    Flow.Add(TEXT("Open the actual Mods panel"), [this] { return Flow.Menu(TEXT("Mods and export"), TEXT("Mods")); });
    Flow.Add(TEXT("Use the narrow panel"), [this] { return Flow.NarrowPanel(); });
    Flow.Add(TEXT("Choose the retained second mod"), [this] { return Flow.Choose(Flow.B); });
    Flow.Add(TEXT("Edit the version without saving"), [this] { return Flow.SetField(TEXT("Version"), TEXT("2.0.0")); });
    Flow.Add(TEXT("Review the unsaved identity"), [this] { return Flow.ClickQueuedExport(TEXT("Review export")); });
    Flow.Add(TEXT("Unsaved identity has a visible next step"),
        [this]
        {
            return Flow.Expect(
                !Flow.TaggedText(TEXT("KadanExportNextStep")).IsEmpty() && !Flow.ButtonEnabled(TEXT("Export mod")),
                TEXT("Unsaved identity has no actionable issue"));
        });
    Flow.Add(TEXT("Capture the unsaved identity issue"), [this] { return Flow.Screenshot(TEXT("identity")); });
    Flow.Add(TEXT("Restore the saved version"), [this] { return Flow.SetField(TEXT("Version"), Flow.B.Version); });
    Flow.Add(TEXT("Open Import for the inventory failure"), [this] { return Flow.Click(TEXT("Import")); });
    Flow.Add(
        TEXT("Expand shared tools"), [this] { return Flow.Expand(TEXT("KadanAdvanced"), TEXT("Advanced"), true); });
    Flow.Add(TEXT("Choose an absent retoc executable"),
        [this] { return Flow.SetField(TEXT("retoc"), Flow.Out / TEXT("missing-retoc.exe")); });
    Flow.Add(TEXT("Return to Mods for the early failure"), [this] { return Flow.Click(TEXT("Mods")); });
    Flow.Add(
        TEXT("Run the actual failing inventory review"),
        [this] { return Flow.ClickQueuedExport(TEXT("Review export")); }, 180);
    Flow.Add(TEXT("Open exact inventory diagnostic"),
        [this] { return Flow.Expand(TEXT("KadanDetails"), TEXT("Details"), true); });
    Flow.Add(
        TEXT("Capture inventory failure and its next step"), [this] { return Flow.Screenshot(TEXT("inventory")); });
    Flow.Add(TEXT("Close inventory diagnostic"),
        [this] { return Flow.Expand(TEXT("KadanDetails"), TEXT("Details"), false); });
    Flow.Add(TEXT("Return to the shared tools"), [this] { return Flow.Click(TEXT("Import")); });
    Flow.Add(TEXT("Restore the retained retoc executable"), [this]
        { return Flow.SetField(TEXT("retoc"), Flow.Tool(TEXT("P4_NATIVE_RETOC"), TEXT("private/retoc/retoc.exe"))); });
    Flow.Add(TEXT("Return to Mods after tool repair"), [this] { return Flow.Click(TEXT("Mods")); });
    Flow.Add(TEXT("Remove the former material root"), [this] { return Flow.Click(TEXT("Remove")); });
    Flow.Add(TEXT("Select the material that requires an unsupported cube texture"),
        [this] { return Assets.SelectFixture(Flow.BNames.PackageRoot / TEXT("IssueMaterial")); });
    Flow.Add(TEXT("Add the material through the selected-assets control"),
        [this] { return Flow.Click(TEXT("Add selected assets")); });
    Flow.Add(
        TEXT("Review the actual unsupported dependency"),
        [this] { return Flow.ClickQueuedExport(TEXT("Review export")); }, 240);
    Flow.Add(TEXT("Show attention rows"), [this] { return Flow.Click(TEXT("Issues")); });
    Flow.Add(TEXT("The dependency names its selecting root and a safe root removal"),
        [this]
        {
            return Flow.Expect(Flow.VisibleText(Flow.BNames.PackageRoot / TEXT("IssueCube")) &&
                    Flow.VisibleText(Flow.BNames.PackageRoot / TEXT("IssueMaterial")) &&
                    Flow.VisibleText(TEXT("Remove root")),
                TEXT("The unsupported dependency has no selecting-root action"));
        });
    Flow.Add(
        TEXT("Capture the unsupported required asset"), [this] { return Flow.Screenshot(TEXT("required-asset")); });
    Flow.Add(TEXT("Locate the required asset"), [this] { return Flow.Click(TEXT("Locate asset")); });
    Flow.Add(TEXT("Locate the selecting root"), [this] { return Flow.Click(TEXT("Locate root")); });
    Flow.Add(
        TEXT("Remove the selecting root through its safe action"), [this] { return Flow.Click(TEXT("Remove root")); });
    Flow.Add(TEXT("Root removal persists an empty selection"), [this] { return Flow.CheckRoots(Flow.B, {}); });
    Flow.Add(TEXT("Select a supported creator texture"), [this] { return Assets.SelectFixture(Flow.BPackage); });
    Flow.Add(TEXT("Add the supported replacement root"), [this] { return Flow.Click(TEXT("Add selected assets")); });
    Flow.Add(TEXT("Restore all shipping rows"), [this] { return Flow.Click(TEXT("All")); });
    Flow.Add(
        TEXT("Re-review after root removal"), [this] { return Flow.ClickQueuedExport(TEXT("Review export")); }, 240);
    Flow.Add(TEXT("The supported root enables actual export"),
        [this]
        {
            return Flow.Expect(
                Flow.ButtonEnabled(TEXT("Export mod")), TEXT("Root removal and re-review did not recover readiness"));
        });
    Flow.Add(TEXT("Capture repaired review"), [this] { return Flow.Screenshot(TEXT("repaired")); });
}

void FIssuesStage::AddPublicationSteps()
{
    if (Flow.Stage == TEXT("Progress"))
    {
        Flow.Add(TEXT("Observe real stage windows"), [this] { return Progress.ObserveExportProgress(); });
    }
    Flow.Add(
        TEXT("Export the real creator texture"), [this] { return Flow.ClickQueuedExport(TEXT("Export mod")); }, 600);
    if (Flow.Stage == TEXT("Progress"))
    {
        Flow.Add(TEXT("Retain actual stage timestamps"),
            [this]
            {
                Progress.StopProgressCapture();
                auto Json = MakeShared<FJsonObject>();
                Json->SetArrayField(TEXT("captures"), Progress.ProgressCaptures);
                FString Value;
                FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Value));
                FFileHelper::SaveStringToFile(Value,
                    *(Flow.Out / (Flow.Stage.ToLower() + TEXT("-export-timeline.json"))),
                    FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
                return Flow.Expect(Progress.ProgressStages.Contains(TEXT("Cooking")) &&
                        Progress.ProgressStages.Contains(TEXT("Packing")) &&
                        Progress.ProgressStages.Contains(TEXT("Checking")) &&
                        Progress.ProgressStages.Contains(TEXT("Publishing")),
                    TEXT("The actual texture export did not expose every applicable stage"));
            });
    }
    Flow.Add(TEXT("Independently inspect the actual publication"), [this] { return ModSteps.VerifyExport(); }, 240);
    Flow.Add(TEXT("Hash the previous publication"), [this] { return RecordPublication(); });
    Flow.Add(TEXT("Capture the published texture result"), [this] { return Flow.Screenshot(TEXT("published")); });
    if (Flow.Stage == TEXT("Progress"))
    {
        Flow.Add(
            TEXT("Re-review before safe cancellation"),
            [this] { return Flow.ClickQueuedExport(TEXT("Review export")); }, 240);
        Flow.Add(TEXT("Arm a real cancel click at the cooking boundary"),
            [this] { return Progress.ObserveExportProgress(true); });
        Flow.Add(
            TEXT("Cancel an actual newer export"), [this] { return Flow.ClickQueuedExport(TEXT("Export mod")); }, 600);
        Flow.Add(TEXT("Cancellation preserves the existing publication"),
            [this]
            {
                Progress.StopProgressCapture();
                if (!Progress.ExportCancelClicked)
                    return Flow.Expect(false, TEXT("The actual progress Cancel button was not clicked"));
                return RecordPublication(true);
            });
        Flow.Add(TEXT("Capture the canceled export result"), [this] { return Flow.Screenshot(TEXT("canceled")); });
    }
}

void FIssuesStage::AddMissingMapSteps()
{
    Flow.Add(
        TEXT("Remove the texture root before the missing-map check"), [this] { return Flow.Click(TEXT("Remove")); });
    Flow.Add(TEXT("Select the own level without a persistent game map"),
        [this] { return Assets.SelectFixture(Flow.BNames.PackageRoot / TEXT("OrphanLevel")); });
    Flow.Add(TEXT("Add the own level through its control"), [this] { return Flow.Click(TEXT("Add selected assets")); });
    Flow.Add(
        TEXT("Review the level before the map refusal"),
        [this] { return Flow.ClickQueuedExport(TEXT("Review export")); }, 240);
    Flow.Add(TEXT("Review names the missing map before export"),
        [this]
        {
            return Flow.Expect(Flow.VisibleText(TEXT("1 need attention"), true) &&
                    !Flow.ButtonEnabled(TEXT("Export mod")) && Flow.VisibleText(TEXT("Find map")),
                TEXT("Review did not identify the missing persistent map"));
        });
    Flow.Add(
        TEXT("Attempt the level export without a game map"),
        [this] { return !Flow.ButtonEnabled(TEXT("Export mod")) || Flow.ClickQueuedExport(TEXT("Export mod")); }, 600);
    Flow.Add(TEXT("A missing map has its actual next action"),
        [this]
        {
            return Flow.Expect(Flow.VisibleText(TEXT("Find map")) || Flow.VisibleText(TEXT("Open persistent map")),
                TEXT("The missing-map issue has no map action"));
        });
    Flow.Add(TEXT("Missing map leaves the publication unchanged"), [this] { return RecordPublication(true); });
    Flow.Add(TEXT("Capture the actual missing-map issue"), [this] { return Flow.Screenshot(TEXT("map")); });
}

bool FIssuesStage::RecordPublication(bool Compare)
{
    TArray<FString> Files;
    IFileManager::Get().FindFilesRecursive(Files, *(Flow.ExportFolder / Flow.B.Id), TEXT("*"), true, false);
    TMap<FString, FString> Current;
    for (const FString& File : Files)
    {
        FString Sha;
        int64 Bytes = 0;
        if (!P4HashFile(File, 64 * 1024 * 1024, Bytes, Sha))
            return Flow.Expect(false, TEXT("Cannot hash the actual published export"));
        Current.Add(File, Sha);
    }
    if (!Compare)
    {
        PublishedHashes = MoveTemp(Current);
        return Flow.Expect(PublishedHashes.Num() == 4, TEXT("The tiny published export does not have four files"));
    }
    bool Same = Current.Num() == PublishedHashes.Num();
    for (const auto& Pair : PublishedHashes)
        Same &= Current.FindRef(Pair.Key) == Pair.Value;
    return Flow.Expect(Same, TEXT("A canceled or failed export changed the previous publication"));
}

void FShippingStage::AddSteps()
{
    if (Flow.Stage == TEXT("ShippingBefore"))
    {
        Flow.Add(
            TEXT("Open the previous Mods layout"), [this] { return Flow.Menu(TEXT("Mods and export"), TEXT("Mods")); });
        Flow.Add(TEXT("Resize the actual old panel for narrow-dock proof"), [this] { return Flow.NarrowPanel(); });
        Flow.Add(TEXT("Choose the retained second mod"), [this] { return Flow.Choose(Flow.B); });
        Flow.Add(TEXT("Capture the previous top of the Mods page"), [this] { return Flow.Screenshot(TEXT("top")); });
        Flow.Add(
            TEXT("Review the retained material and its required content"),
            [this] { return Flow.ClickQueuedExport(TEXT("Review export")); }, 240);
        Flow.Add(TEXT("Capture the previous review and export placement"),
            [this] { return Flow.Screenshot(TEXT("review")); });
        return;
    }
    AddReviewSteps();
    AddInvalidationSteps();
}

FString FShippingStage::QualificationScope() const
{
    return TEXT("Native narrow-panel shipping review, filtering, stale identity and separate mod roots; no export "
                "or game acceptance");
}

void FShippingStage::AddReviewSteps()
{
    Flow.Add(TEXT("Close any restored panel before building owned fixtures"),
        [this]
        {
            auto Panel = Flow.Tab();
            return !Panel || Panel->RequestCloseTab();
        });
    Flow.Add(TEXT("Create the two owned texture fixtures"), [this] { return Assets.Fixture(); });
    Flow.Add(TEXT("Create a material with 24 required creator textures"), [this] { return ShippingFixture(); });
    Flow.Add(TEXT("Open the actual Mods panel"), [this] { return Flow.Menu(TEXT("Mods and export"), TEXT("Mods")); });
    Flow.Add(TEXT("Resize the actual panel for narrow-dock proof"), [this] { return Flow.NarrowPanel(); });
    ModSteps.AddNewMod(Flow.A);
    Flow.Add(TEXT("Set the first mod's root"),
        [this] { return Flow.SetField(TEXT("Content folder"), Flow.ANames.PackageRoot); });
    Flow.Add(TEXT("Add the first mod's folder through its control"), [this] { return Flow.Click(TEXT("Add folder")); });
    ModSteps.AddNewMod(Flow.B);
    Flow.Add(TEXT("Select only the material root in the Content Browser"),
        [this]
        {
            const FString Package = Flow.BNames.PackageRoot / TEXT("DependencyMaterial");
            UObject* Object = LoadObject<UObject>(nullptr, *(Package + TEXT(".DependencyMaterial")));
            if (!Object)
                return false;
            Flow.Browser().SyncBrowserToAssets({FAssetData(Object)});
            return true;
        });
    Flow.Add(TEXT("Add only the selected root through its control"),
        [this] { return Flow.Click(TEXT("Add selected assets")); });
    Flow.Add(TEXT("Set the output folder"), [this] { return Flow.SetField(TEXT("Output folder"), Flow.ExportFolder); });
    Flow.Add(TEXT("Open Import to set shared tools"), [this] { return Flow.Click(TEXT("Import")); });
    Flow.Add(TEXT("Expand tools"), [this] { return Flow.Expand(TEXT("KadanAdvanced"), TEXT("Advanced"), true); });
    Flow.Add(TEXT("Set retoc"), [this]
        { return Flow.SetField(TEXT("retoc"), Flow.Tool(TEXT("P4_NATIVE_RETOC"), TEXT("private/retoc/retoc.exe"))); });
    Flow.Add(TEXT("Return to Mods"), [this] { return Flow.Click(TEXT("Mods")); });
    Flow.Add(
        TEXT("Review actual required content"), [this] { return Flow.ClickQueuedExport(TEXT("Review export")); }, 240);
    Flow.Add(TEXT("Review derives the required textures from one root"),
        [this]
        {
            auto List = Flow.Tagged(TEXT("KadanShippingList"));
            return Flow.Expect(List &&
                    StaticCastSharedPtr<SListView<TSharedPtr<FP4ExportSelectionRow>>>(List)
                            ->GetNumItemsBeingObserved() >= 25 &&
                    Flow.TaggedText(TEXT("KadanShippingState")) == TEXT("Current review"),
                TEXT("The shipping list did not derive the long dependency closure"));
        });
    Flow.Add(TEXT("Export controls remain visible with a long list"), [this] { return Flow.StickyModsActions(); });
    Flow.Add(TEXT("Capture the actual long shipping list"), [this] { return Flow.Screenshot(TEXT("long-list")); });
    Flow.Add(TEXT("Filter shipping assets"), [this] { return Flow.Click(TEXT("Included")); });
    Flow.Add(TEXT("Capture shipping filter"), [this] { return Flow.Screenshot(TEXT("ships")); });
    Flow.Add(TEXT("Filter attention"), [this] { return Flow.Click(TEXT("Issues")); });
    Flow.Add(TEXT("Capture attention filter"), [this] { return Flow.Screenshot(TEXT("attention")); });
    Flow.Add(TEXT("Use-game filter is independent of the shipping roots"), [this] { return Flow.Click(TEXT("Game")); });
    Flow.Add(TEXT("Capture use-game filter"), [this] { return Flow.Screenshot(TEXT("uses-game")); });
    Flow.Add(TEXT("Restore all rows"), [this] { return Flow.Click(TEXT("All")); });
}

void FShippingStage::AddInvalidationSteps()
{
    Flow.Add(TEXT("Change an unrelated mod texture"),
        [this]()
        {
            auto* Texture = LoadObject<UTexture2D>(nullptr, *(Flow.APackage + TEXT(".Texture")));
            if (!Texture)
            {
                return Flow.Expect(false, TEXT("The unrelated fixture texture is missing"));
            }
            Texture->Modify();
            Texture->AddressX = Texture->AddressX == TA_Clamp ? TA_Wrap : TA_Clamp;
            Texture->PostEditChange();
            return true;
        });
    Flow.Add(TEXT("Unrelated content preserves the current review"),
        [this]()
        {
            return Flow.Expect(Flow.TaggedText(TEXT("KadanShippingState")) == TEXT("Current review") &&
                    Flow.ButtonEnabled(TEXT("Export mod")),
                TEXT("An unrelated package invalidated this mod's review"));
        });
    Flow.Add(TEXT("Change a required texture"),
        [this]()
        {
            const FString Package = Flow.BFolder / TEXT("Required_00");
            auto* Texture = LoadObject<UTexture2D>(nullptr, *(Package + TEXT(".Required_00")));
            if (!Texture)
            {
                return Flow.Expect(false, TEXT("The required fixture texture is missing"));
            }
            Texture->Modify();
            Texture->AddressX = Texture->AddressX == TA_Clamp ? TA_Wrap : TA_Clamp;
            Texture->PostEditChange();
            return true;
        });
    Flow.Add(TEXT("Required content invalidates the current review"),
        [this]()
        {
            return Flow.Expect(Flow.TaggedText(TEXT("KadanShippingState")) == TEXT("Review out of date") &&
                    !Flow.ButtonEnabled(TEXT("Export mod")),
                TEXT("A required texture edit retained export readiness"));
        });
    Flow.Add(TEXT("Capture content-driven stale review"), [this]() { return Flow.Screenshot(TEXT("content-stale")); });
    Flow.Add(
        TEXT("Re-review the changed required texture"),
        [this]() { return Flow.ClickQueuedExport(TEXT("Review export")); }, 240);
    Flow.Add(TEXT("Re-review restores readiness after the required edit"),
        [this]()
        {
            return Flow.Expect(
                Flow.ButtonEnabled(TEXT("Export mod")), TEXT("The changed required texture did not pass re-review"));
        });
    Flow.Add(TEXT("Edit the name without saving"),
        [this] { return Flow.SetField(TEXT("Name"), Flow.B.Name + TEXT(" draft")); });
    Flow.Add(TEXT("Review becomes stale and export disables"),
        [this]
        {
            return Flow.Expect(Flow.TaggedText(TEXT("KadanShippingState")) == TEXT("Review out of date") &&
                    !Flow.ButtonEnabled(TEXT("Export mod")) && Flow.StickyModsActions(),
                TEXT("A changed identity retained current export approval"));
        });
    Flow.Add(TEXT("Capture stale review without scrolling to the footer"),
        [this] { return Flow.Screenshot(TEXT("stale")); });
    Flow.Add(TEXT("Restore the unchanged saved name"), [this] { return Flow.SetField(TEXT("Name"), Flow.B.Name); });
    Flow.Add(TEXT("Switch to the first mod"), [this] { return Flow.Choose(Flow.A); });
    Flow.Add(
        TEXT("First mod retains its own roots"), [this] { return Flow.CheckRoots(Flow.A, {Flow.ANames.PackageRoot}); });
    Flow.Add(TEXT("Capture the first mod's roots"), [this] { return Flow.Screenshot(TEXT("first-mod")); });
    Flow.Add(TEXT("Switch back to the second mod"), [this] { return Flow.Choose(Flow.B); });
    Flow.Add(TEXT("Second mod retains only its material root"),
        [this] { return Flow.CheckRoots(Flow.B, {Flow.BNames.PackageRoot / TEXT("DependencyMaterial")}); });
    Flow.Add(TEXT("Capture the second mod's roots"), [this] { return Flow.Screenshot(TEXT("second-mod")); });
}

bool FShippingStage::ShippingFixture()
{
    const FString Root = Flow.BNames.PackageRoot / TEXT("DependencyMaterial");
    UMaterial* Material = NewObject<UMaterial>(
        CreatePackage(*Root), TEXT("DependencyMaterial"), RF_Public | RF_Standalone);
    for (int32 TextureIndex = 0; TextureIndex < 24; ++TextureIndex)
    {
        const FString PackageName = Flow.BFolder / FString::Printf(TEXT("Required_%02d"), TextureIndex);
        const FString Name = FPackageName::GetLongPackageAssetName(PackageName);
        UTexture2D* Texture = NewObject<UTexture2D>(CreatePackage(*PackageName), *Name, RF_Public | RF_Standalone);
        TArray<uint8> Pixels;
        Pixels.SetNumZeroed(4 * 4 * 4);
        for (int32 I = 0; I < 16; ++I)
        {
            Pixels[I * 4] = 16 + TextureIndex * 7;
            Pixels[I * 4 + 1] = 90;
            Pixels[I * 4 + 3] = 255;
        }
        Texture->Source.Init(4, 4, 1, 1, TSF_BGRA8, Pixels.GetData());
        Texture->MipGenSettings = TMGS_NoMipmaps;
        auto* Expression = NewObject<UMaterialExpressionTextureSample>(Material);
        Expression->Texture = Texture;
        Material->Expressions.Add(Expression);
        if (TextureIndex == 0)
            Material->BaseColor.Expression = Expression;
        const FString File = FPackageName::LongPackageNameToFilename(
            PackageName, FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
        if (!UPackage::SavePackage(Texture->GetOutermost(), Texture, RF_Public | RF_Standalone, *File, GError, nullptr,
                false, true, SAVE_NoError))
            return Flow.Expect(false, TEXT("Cannot save the dependency fixture"));
        FAssetRegistryModule::AssetCreated(Texture);
    }
    const FString File = FPackageName::LongPackageNameToFilename(Root, FPackageName::GetAssetPackageExtension());
    if (!UPackage::SavePackage(Material->GetOutermost(), Material, RF_Public | RF_Standalone, *File, GError, nullptr,
            false, true, SAVE_NoError))
        return Flow.Expect(false, TEXT("Cannot save the material root"));
    FAssetRegistryModule::AssetCreated(Material);
    auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.ScanPathsSynchronous({Flow.BNames.PackageRoot}, true);
    return true;
}

TUniquePtr<FFlowStage> MakeIssuesStage(FFlow& Flow)
{
    return MakeUnique<FIssuesStage>(Flow);
}

TUniquePtr<FFlowStage> MakeShippingStage(FFlow& Flow)
{
    return MakeUnique<FShippingStage>(Flow);
}
} // namespace P4KadanCreatorFlow
#endif

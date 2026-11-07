#include "P4KadanCreatorFlowStageList.h"
#include "P4KadanCreatorFlowShared.h"

#if P4_MAPKIT_TESTS
namespace P4KadanCreatorFlow
{

namespace
{
// Copy and CopyRead: each changed panel message is captured on the Slate tick that shows it, across source
// reads, imports, menus, review cards and exports.
class FCopyStage final : public FFlowStage
{
public:
    using FFlowStage::FFlowStage;

    void AddSteps() override;

    void Stop() override;

private:
    // The full menu, the Asset page and the Game changes and Game reference cards.
    void AddCardSteps();

    // The Replacement and Export issue cards of owned negative fixtures.
    void AddRefusalCardSteps();

    bool CopyFixture();

    bool ObserveCopy();

    void StopProgressCapture();

    bool SaveCopyMessages();

    bool CopyMenu();

    bool CopyMenuCapture();

    bool CopyClearRoots();

    FFlowAssets Assets{Flow};
    FUpgradeSnapshot Snapshot{Flow};
    FString CopyHeader;
    TSet<FString> CopySeen;
    TArray<TSharedPtr<FJsonValue>> CopyMessages;
    TUniquePtr<FP4KadanReadOnlyTextureProbe> CopyTextureProbe;
    FDelegateHandle ProgressTickHandle;
    bool CapturingProgress = false;
};
} // namespace

void FCopyStage::AddSteps()
{
    Flow.Add(TEXT("Resolve the real game texture source"), [this]() { return CopyFixture(); }, 180);
    Flow.Add(TEXT("Observe changed messages on displayed Slate ticks"), [this]() { return ObserveCopy(); });
    Flow.Add(TEXT("Open Import through the menu"),
        [this]() { return Flow.Menu(TEXT("Import from game"), TEXT("Choose Pikmin 4")); });
    Flow.Add(TEXT("Use the narrow panel"), [this]() { return Flow.NarrowPanel(); });
    if (Flow.Stage == TEXT("CopyRead"))
    {
        Flow.Add(
            TEXT("Open Advanced"), [this]() { return Flow.Expand(TEXT("KadanAdvanced"), TEXT("Advanced"), true); });
        Flow.Add(
            TEXT("Use the extracted source mode"), [this]() { return Flow.CheckTag(TEXT("KadanUseExtracted"), true); });
        Flow.Add(TEXT("Choose a missing source folder"),
            [this]() { return Flow.SetField(TEXT("Extracted packages"), Flow.Out / TEXT("missing-source")); });
        Flow.Add(TEXT("Trigger the source read failure"), [this]() { return Flow.Click(TEXT("Refresh game folder")); });
        Flow.Add(TEXT("Expand the retained source diagnostic"),
            [this]() { return Flow.Expand(TEXT("KadanDetails"), TEXT("Details"), true); });
        Flow.Add(TEXT("Capture the sentence and exact diagnostic separately"),
            [this]() { return Flow.Screenshot(TEXT("source-error")); });
        Flow.Add(TEXT("Collapse the source diagnostic"),
            [this]() { return Flow.Expand(TEXT("KadanDetails"), TEXT("Details"), false); });
        Flow.Add(TEXT("Choose the real retained texture folder"),
            [this]() { return Flow.SetField(TEXT("Extracted packages"), FPaths::GetPath(CopyHeader)); });
        Flow.Add(TEXT("Arm the existing native Cancel driver"), [this]() { return Flow.ArmCancellation(); });
        Flow.Add(
            TEXT("Cancel the real folder scan"), [this]() { return Flow.Click(TEXT("Refresh game folder")); }, 180);
        Flow.Add(TEXT("The scan uses its own cancellation message"),
            [this]()
            {
                Flow.StopCancellation();
                return Flow.Expect(Flow.CancelClicked &&
                        Flow.TaggedText(TEXT("KadanStatus")).StartsWith(TEXT("The folder scan was canceled.")),
                    TEXT("The actual folder scan did not reach its cancellation message"));
            });
        Flow.Add(TEXT("Capture the canceled scan"), [this]() { return Flow.Screenshot(TEXT("scan-canceled")); });
        Flow.Add(TEXT("Retry the real scan"), [this]() { return Flow.Click(TEXT("Refresh game folder")); }, 180);
        Flow.Add(TEXT("Capture the completed source read"), [this]() { return Flow.Screenshot(TEXT("source-ready")); });
        Flow.Add(TEXT("Find the real texture by package path"),
            [this]() { return Flow.SetField(TEXT("Search"), Snapshot.UpgradeTexture); });
        Flow.Add(TEXT("Select the visible texture"), [this]() { return Flow.Click(TEXT("Select visible")); });
        Flow.Add(
            TEXT("Review required content"), [this]() { return Flow.Click(TEXT("Review required content")); }, 180);
        Flow.Add(
            TEXT("Capture the source review sentence"), [this]() { return Flow.Screenshot(TEXT("source-review")); });
        Flow.Add(TEXT("Disable reuse for an actual texture import"), [this]() { return Flow.ResumeCheck(false); });
        Flow.Add(
            TEXT("Import through the actual control"), [this]() { return Flow.Click(TEXT("Import selection")); }, 240);
        Flow.Add(TEXT("The completed import names its subject"),
            [this]()
            {
                return Flow.Expect(Flow.TaggedText(TEXT("KadanStatus")).StartsWith(TEXT("Kadan imported 1 package.")),
                    TEXT("The texture import did not show the new completion sentence"));
            });
        Flow.Add(TEXT("Capture the completed import"), [this]() { return Flow.Screenshot(TEXT("imported")); });
        Flow.Add(TEXT("Enable reuse"), [this]() { return Flow.ResumeCheck(true); });
        Flow.Add(
            TEXT("Run the completed import with reuse"), [this]() { return Flow.Click(TEXT("Import selection")); },
            180);
        Flow.Add(
            TEXT("Return to the game catalog"), [this]() { return Flow.CheckTag(TEXT("KadanUseExtracted"), false); });
        Flow.Add(TEXT("Choose the real game folder"),
            [this]()
            {
                return Flow.SetField(
                    TEXT("Game folder"), FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS")));
            });
        Flow.Add(
            TEXT("Read the original game catalog"), [this]() { return Flow.CommitField(TEXT("Game folder")); }, 180);
        Flow.Add(TEXT("Hide the catalog rows"),
            [this]() { return Flow.SetField(TEXT("Search"), TEXT("no-such-copy-check-source")); });
        Flow.Add(TEXT("Arm the existing native Cancel driver for imported selection"),
            [this]() { return Flow.ArmCancellation(); });
        Flow.Add(
            TEXT("Cancel the imported-content selection"), [this]() { return Flow.Click(TEXT("Select imported")); },
            240);
        Flow.Add(TEXT("The selection cancellation keeps the prior selection"),
            [this]()
            {
                Flow.StopCancellation();
                return Flow.Expect(Flow.CancelClicked &&
                        Flow.TaggedText(TEXT("KadanStatus")).StartsWith(TEXT("The selection was canceled.")),
                    TEXT("The actual imported-content selection did not reach its cancellation message"));
            });
        Flow.Add(
            TEXT("Capture the canceled selection"), [this]() { return Flow.Screenshot(TEXT("selection-canceled")); });
        Flow.Add(
            TEXT("Retry the imported-content selection"), [this]() { return Flow.Click(TEXT("Select imported")); },
            240);
        Flow.Add(TEXT("Capture the cross-filter selection sentence"),
            [this]() { return Flow.Screenshot(TEXT("selection")); });
        Flow.Add(TEXT("Retain the observed message captures"), [this]() { return SaveCopyMessages(); });
        return;
    }
    AddCardSteps();
    AddRefusalCardSteps();
}

void FCopyStage::AddCardSteps()
{
    Flow.Add(
        TEXT("Reopen the retained map for real game-change review"), [this]() { return Snapshot.UpgradeContent(true); },
        180);
    Flow.Add(TEXT("Open the complete Kadan menu with its header"), [this]() { return CopyMenu(); });
    Flow.Add(TEXT("Capture every menu entry and its Help section"), [this]() { return CopyMenuCapture(); });
    Flow.Add(TEXT("Choose Mods from that menu"),
        [this]() { return Flow.MouseClick(Flow.WindowText(TEXT("Mods and export"), true)); });
    Flow.Add(TEXT("Choose the owned second mod"), [this]() { return Flow.Choose(Flow.B); });
    Flow.Add(TEXT("Select the creator mesh in the Content Browser"),
        [this]() { return Assets.SelectFixture(Flow.BNames.PackageRoot / TEXT("ProgressCube")); });
    Flow.Add(TEXT("Open Asset with its page control"), [this]() { return Flow.Click(TEXT("Asset")); });
    Flow.Add(TEXT("Refresh the selected asset through the actual button"),
        [this]() { return Flow.Click(TEXT("Inspect assets selected in Content Browser")); });
    Flow.Add(TEXT("Capture the Editing heading and separate message rows"),
        [this]() { return Flow.Screenshot(TEXT("asset")); });
    Flow.Add(TEXT("Return to Mods"), [this]() { return Flow.Click(TEXT("Mods")); });
    Flow.Add(TEXT("Remove only the owned mod's prior roots"), [this]() { return CopyClearRoots(); });
    Flow.Add(TEXT("Select the actual edited game map"),
        [this]()
        {
            auto* Stock = LoadObject<UTexture2D>(
                nullptr, *(Snapshot.UpgradeTexture + TEXT(".") + FPackageName::GetShortName(Snapshot.UpgradeTexture)));
            if (!Flow.Expect(Stock != nullptr, TEXT("The unchanged game texture is unavailable")))
            {
                return false;
            }
            Flow.Browser().SyncBrowserToAssets(
                {FAssetData(GEditor->GetEditorWorldContext().World()), FAssetData(Stock)});
            return true;
        });
    Flow.Add(TEXT("Add the game map root through its control"),
        [this]() { return Flow.Click(TEXT("Add selected assets")); });
    Flow.Add(
        TEXT("Review actual game changes"), [this]() { return Flow.ClickQueuedExport(TEXT("Review export")); }, 240);
    Flow.Add(TEXT("Filter included content"), [this]() { return Flow.Click(TEXT("Included")); });
    Flow.Add(TEXT("Capture the Game changes card"), [this]() { return Flow.Screenshot(TEXT("game-changes")); });
    Flow.Add(TEXT("Filter game references"), [this]() { return Flow.Click(TEXT("Game")); });
    Flow.Add(TEXT("Capture the Game reference card"), [this]() { return Flow.Screenshot(TEXT("game-reference")); });
    Flow.Add(TEXT("Remove the map root before the bounded texture export"), [this]() { return CopyClearRoots(); });
    Flow.Add(TEXT("Select the owned texture"), [this]() { return Assets.SelectFixture(Flow.BPackage); });
    Flow.Add(TEXT("Add the owned texture root"), [this]() { return Flow.Click(TEXT("Add selected assets")); });
    Flow.Add(TEXT("Set the owned output folder"),
        [this]() { return Flow.SetField(TEXT("Output folder"), Flow.ExportFolder); });
    Flow.Add(
        TEXT("Review the bounded texture export"), [this]() { return Flow.ClickQueuedExport(TEXT("Review export")); },
        240);
    Flow.Add(TEXT("Show all reviewed rows"), [this]() { return Flow.Click(TEXT("All")); });
    Flow.Add(TEXT("Capture the Packed asset card"), [this]() { return Flow.Screenshot(TEXT("packed")); });
    Flow.Add(
        TEXT("Export the texture through the actual control"),
        [this]() { return Flow.ClickQueuedExport(TEXT("Export mod")); }, 240);
    Flow.Add(TEXT("The export completion names its subject"),
        [this]()
        {
            return Flow.Expect(Flow.TaggedText(TEXT("KadanStatus")).StartsWith(TEXT("Kadan exported the mod to ")),
                TEXT("The completed export did not show its new sentence"));
        });
    Flow.Add(TEXT("Capture the completed export sentence"), [this]() { return Flow.Screenshot(TEXT("published")); });
    Flow.Add(TEXT("Remove the owned texture root"), [this]() { return CopyClearRoots(); });
}

void FCopyStage::AddRefusalCardSteps()
{
    Flow.Add(TEXT("Prepare the real in-memory read-only negative fixture"),
        [this]()
        {
            auto* Stock = LoadObject<UTexture2D>(
                nullptr, *(Snapshot.UpgradeTexture + TEXT(".") + FPackageName::GetShortName(Snapshot.UpgradeTexture)));
            if (!Flow.Expect(Stock != nullptr, TEXT("The imported original texture is unavailable")))
            {
                return false;
            }
            CopyTextureProbe = MakeUnique<FP4KadanReadOnlyTextureProbe>(Stock);
            Flow.Browser().SyncBrowserToAssets({FAssetData(Stock)});
            return true;
        });
    Flow.Add(TEXT("Add the changed original texture as a negative root"),
        [this]() { return Flow.Click(TEXT("Add selected assets")); });
    Flow.Add(
        TEXT("Review the forbidden original-data change"),
        [this]() { return Flow.ClickQueuedExport(TEXT("Review export")); }, 240);
    Flow.Add(TEXT("Capture the Replacement card"), [this]() { return Flow.Screenshot(TEXT("replacement")); });
    Flow.Add(TEXT("Restore only the fixture's in-memory original values"),
        [this]()
        {
            CopyTextureProbe.Reset();
            return true;
        });
    Flow.Add(TEXT("Remove the negative root"), [this]() { return CopyClearRoots(); });
    Flow.Add(TEXT("Prepare the owned unsupported texture fixture"), [this]() { return Assets.IssueFixture(); });
    Flow.Add(TEXT("Select the unsupported source cube"),
        [this]() { return Assets.SelectFixture(Flow.BNames.PackageRoot / TEXT("IssueCube")); });
    Flow.Add(TEXT("Add the unsupported root"), [this]() { return Flow.Click(TEXT("Add selected assets")); });
    Flow.Add(
        TEXT("Review the unsupported root"), [this]() { return Flow.ClickQueuedExport(TEXT("Review export")); }, 240);
    Flow.Add(TEXT("Filter Issues"), [this]() { return Flow.Click(TEXT("Issues")); });
    Flow.Add(TEXT("Capture the Export issue card"), [this]() { return Flow.Screenshot(TEXT("issue")); });
    Flow.Add(TEXT("Remove the unsupported fixture root"), [this]() { return CopyClearRoots(); });
    Flow.Add(TEXT("Retain the observed message captures"), [this]() { return SaveCopyMessages(); });
}

void FCopyStage::Stop()
{
    StopProgressCapture();
}

void FCopyStage::StopProgressCapture()
{
    if (ProgressTickHandle.IsValid() && FSlateApplication::IsInitialized())
        FSlateApplication::Get().OnPostTick().Remove(ProgressTickHandle);
    ProgressTickHandle.Reset();
}

bool FCopyStage::CopyFixture()
{
    FString Error;
    FP4KadanSourceCatalog Catalog;
    const FString Game = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    if (!Flow.Expect(P4KadanReadSourceCatalog(
                         Game, Flow.Tool(TEXT("P4_NATIVE_RETOC"), TEXT("private/retoc/retoc.exe")), Catalog, Error),
            Error))
    {
        return false;
    }
    Catalog.ImportSettings.Repak = Flow.Tool(TEXT("P4_NATIVE_REPAK"), TEXT("private/repak/repak.exe"));
    const auto Plan = P4KadanPlanImport(Catalog.ImportSettings, {Snapshot.UpgradeTexture}, true);
    for (const auto& Asset : Plan.Assets)
    {
        if (Asset && FName(*Asset->Package) == FName(*Snapshot.UpgradeTexture))
        {
            CopyHeader = Asset->Header;
            break;
        }
    }
    return Flow.Expect(FPaths::FileExists(CopyHeader), TEXT("The real texture source is unavailable for copy proof"));
}

bool FCopyStage::ObserveCopy()
{
    StopProgressCapture();
    CopySeen.Empty();
    CopyMessages.Empty();
    ProgressTickHandle = FSlateApplication::Get().OnPostTick().AddLambda(
        [this](float)
        {
            const auto Panel = Flow.Tab();
            const FString Message = Flow.TaggedText(TEXT("KadanStatus"));
            const bool Changed = Message.StartsWith(TEXT("Kadan is ")) || Message.StartsWith(TEXT("Kadan selected ")) ||
                Message.StartsWith(TEXT("Kadan imported ")) || Message.StartsWith(TEXT("Kadan exported ")) ||
                Message.StartsWith(TEXT("The game folder could not be read.")) ||
                Message.StartsWith(TEXT("The folder scan was canceled.")) ||
                Message.StartsWith(TEXT("The selection was canceled."));
            if (!Panel || CapturingProgress || !Changed || CopySeen.Contains(Message) ||
                Panel->GetContent()->GetCachedGeometry().GetLocalSize().X < 400)
            {
                return;
            }
            TGuardValue<bool> Guard(CapturingProgress, true);
            TArray<FColor> Pixels;
            FIntVector Size;
            if (!FSlateApplication::Get().TakeScreenshot(Panel->GetContent(), Pixels, Size))
            {
                Flow.Fail(TEXT("The changed message could not be captured from Slate"));
                return;
            }
            TArray<uint8> Png;
            FImageUtils::CompressImageArray(Size.X, Size.Y, Pixels, Png);
            const FString File = Flow.Stage.ToLower() + FString::Printf(TEXT("-status-%02d.png"), CopySeen.Num() + 1);
            if (!FFileHelper::SaveArrayToFile(Png, *(Flow.Out / File)))
            {
                Flow.Fail(TEXT("The changed-message screenshot could not be saved"));
                return;
            }
            CopySeen.Add(Message);
            Flow.CapturedScreens.Add(MakeShared<FJsonValueString>(File));
            auto Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("message"), Message);
            Entry->SetStringField(TEXT("file"), File);
            Entry->SetStringField(TEXT("utc"), FDateTime::UtcNow().ToIso8601());
            CopyMessages.Add(MakeShared<FJsonValueObject>(Entry));
        });
    return true;
}

bool FCopyStage::SaveCopyMessages()
{
    StopProgressCapture();
    auto Record = MakeShared<FJsonObject>();
    Record->SetArrayField(TEXT("messages"), CopyMessages);
    FString Json;
    FJsonSerializer::Serialize(Record, TJsonWriterFactory<>::Create(&Json));
    return Flow.Expect(
        FFileHelper::SaveStringToFile(Json, *(Flow.Out / (Flow.Stage.ToLower() + TEXT("-messages.json"))),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM),
        TEXT("The changed-message record could not save"));
}

bool FCopyStage::CopyMenu()
{
    if (Flow.Phase == 0)
    {
        const auto Window = FGlobalTabmanager::Get()->GetRootWindow();
        if (!Window)
        {
            return false;
        }
        Window->BringToFront(true);
        const auto Bar = Find(Window.ToSharedRef(),
            [](const TSharedRef<SWidget>& Widget) { return Widget->GetType() == FName(TEXT("SKadanMenuBar")); });
        if (!Bar || !Flow.MouseClick(Text(Bar.ToSharedRef(), TEXT("Kadan"))))
        {
            return false;
        }
        Flow.Phase = 1;
        Flow.ReadyFrame = Flow.Frame + 2;
        return false;
    }
    return Flow.Frame >= Flow.ReadyFrame && Flow.WindowText(TEXT("Panel guide"), true) &&
        Flow.WindowText(TEXT("Map guide"), true);
}

bool FCopyStage::CopyMenuCapture()
{
    TArray<TSharedRef<SWindow>> Windows;
    FSlateApplication::Get().GetAllVisibleWindowsOrdered(Windows);
    for (const auto& Window : Windows)
    {
        if (Find(Window, [](const TSharedRef<SWidget>& Widget)
                { return Widget->GetType() == FName(TEXT("SMenuEntryBlock")) && Text(Widget, TEXT("Panel guide")); }))
        {
            return Flow.CaptureWidget(TEXT("menu"), Window);
        }
    }
    return false;
}

bool FCopyStage::CopyClearRoots()
{
    const auto Widget = Flow.Tagged(TEXT("KadanSelectionRoots"));
    const auto List = Widget ? StaticCastSharedPtr<SListView<TSharedPtr<FP4ExportRoot>>>(Widget) : nullptr;
    if (!List)
    {
        return false;
    }
    if (List->GetNumItemsBeingObserved() == 0)
    {
        return true;
    }
    Flow.Click(TEXT("Remove"));
    return false;
}

TUniquePtr<FFlowStage> MakeCopyStage(FFlow& Flow)
{
    return MakeUnique<FCopyStage>(Flow);
}
} // namespace P4KadanCreatorFlow
#endif

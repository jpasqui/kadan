#include "P4KadanCreatorFlowStageList.h"
#include "P4KadanCreatorFlowShared.h"

#if P4_MAPKIT_TESTS
namespace P4KadanCreatorFlow
{

namespace
{
// Receipt and ReceiptReopen: the last successful export's receipt survives a failed newer export and a restart.
class FReceiptStage final : public FFlowStage
{
public:
    using FFlowStage::FFlowStage;

    void AddSteps() override;

private:
    bool ReceiptFixture();

    bool ReceiptOutputSnapshot(bool Compare);

    bool InspectReceiptExport();

    bool ReceiptSaved();

    bool ReceiptKept();

    FFlowAssets Assets{Flow};
    FString ReceiptFile;
    FString ReceiptDirectory;
    FString ReceiptTime;
    TArray<uint8> ReceiptBefore;
    TMap<FString, FString> ReceiptPublication;
#if PLATFORM_WINDOWS
    TUniquePtr<FP4KadanReceiptPublicationLock> ReceiptLock;
#endif
};
} // namespace

void FReceiptStage::AddSteps()
{
    Flow.Add(TEXT("Prepare owned receipt and publication fixtures"), [this]() { return ReceiptFixture(); }, 180);
    Flow.Add(TEXT("Open the actual Mods panel"), [this]() { return Flow.Menu(TEXT("Mods and export"), TEXT("Mods")); });
    Flow.Add(TEXT("Use the narrow panel"), [this]() { return Flow.NarrowPanel(); });
    Flow.Add(TEXT("Choose the retained fixture mod"), [this]() { return Flow.Choose(Flow.B); });
    Flow.Add(TEXT("Expand the actual authoring split for legible receipt evidence"),
        [this]()
        {
            const auto Panel = Flow.Tab();
            const auto Authoring = Flow.Tagged(TEXT("KadanModDetails"));
            return Panel && Authoring &&
                P4KadanResizeDetailsSplit(Panel->GetContent(), Authoring.ToSharedRef(), Orient_Vertical);
        });
    if (Flow.Stage == TEXT("ReceiptReopen"))
    {
        Flow.Add(TEXT("Reveal the cold-loaded publication receipt"),
            [this]() { return Flow.Prepare(Flow.Tagged(TEXT("KadanReceiptIdentity"))); });
        Flow.Add(TEXT("Capture the receipt after reopening the editor"),
            [this]() { return Flow.Screenshot(TEXT("retained")); });
        return;
    }
    Flow.Add(TEXT("Set the controlled successful version"),
        [this]() { return Flow.SetField(TEXT("Version"), TEXT("1.0.0")); });
    Flow.Add(TEXT("Save the controlled successful identity"), [this]() { return Flow.Click(TEXT("Save mod")); });
    Flow.Add(TEXT("Remove only the fixture mod's previous roots"), [this]() { return Flow.RemoveEveryRoot(); });
    Flow.Add(TEXT("Select the actual edited persistent map"),
        [this]() { return Assets.SelectFixture(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_UX_MAP"))); });
    Flow.Add(TEXT("Add the actual map root through its button"),
        [this]() { return Flow.Click(TEXT("Add selected assets")); });
    Flow.Add(TEXT("Choose the controlled long output path"),
        [this]() { return Flow.SetField(TEXT("Output folder"), Flow.ExportFolder); });
    Flow.Add(
        TEXT("Open Import for the retained original rebuild inputs"), [this]() { return Flow.Click(TEXT("Import")); });
    Flow.Add(TEXT("Open Advanced"), [this]() { return Flow.Expand(TEXT("KadanAdvanced"), TEXT("Advanced"), true); });
    Flow.Add(
        TEXT("Reveal the extracted input field"), [this]() { return Flow.CheckTag(TEXT("KadanUseExtracted"), true); });
    Flow.Add(TEXT("Use the retained converted originals"),
        [this]()
        {
            return Flow.SetField(
                TEXT("Extracted packages"), FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_UX_GAME")));
        });
    Flow.Add(
        TEXT("Restore the normal game catalog"), [this]() { return Flow.CheckTag(TEXT("KadanUseExtracted"), false); });
    Flow.Add(TEXT("Return to Mods"), [this]() { return Flow.Click(TEXT("Mods")); });
    Flow.Add(
        TEXT("Review the actual omission-note fixture"),
        [this]() { return Flow.ClickQueuedExport(TEXT("Review export")); }, 240);
    Flow.Add(
        TEXT("Publish the actual map export"), [this]() { return Flow.ClickQueuedExport(TEXT("Export mod")); }, 600);
    Flow.Add(TEXT("Independently inspect publication and receipt contents"), [this]() { return ReceiptSaved(); }, 240);
    Flow.Add(TEXT("Reveal the successful identity and output directory"),
        [this]() { return Flow.Prepare(Flow.Tagged(TEXT("KadanReceiptDirectory"))); });
    Flow.Add(TEXT("Capture the successful receipt and long output path"),
        [this]() { return Flow.Screenshot(TEXT("published")); });
    Flow.Add(TEXT("Copy the actual receipt directory through its button"),
        [this]() { return Flow.Click(TEXT("Copy path")); });
    Flow.Add(TEXT("The clipboard contains the intended publication directory"),
        [this]()
        {
            FString Value;
            FPlatformApplicationMisc::ClipboardPaste(Value);
            return Flow.Expect(
                Value == ReceiptDirectory, TEXT("Copy path used a draft output or another mod's directory"));
        });
    Flow.Add(TEXT("Open the intended output folder through its actual button"),
        [this]() { return Flow.Click(TEXT("Open output folder")); });
    Flow.Add(TEXT("Reveal the actual omission note"),
        [this]() { return Flow.Prepare(Flow.Tagged(TEXT("KadanReceiptLighting"))); });
    Flow.Add(TEXT("Capture the persistent omission note"), [this]() { return Flow.Screenshot(TEXT("notes")); });
    Flow.Add(TEXT("Reveal pending game testing"),
        [this]() { return Flow.Prepare(Flow.Tagged(TEXT("KadanReceiptGameStatus"))); });
    Flow.Add(TEXT("Capture the pending game checklist"), [this]() { return Flow.Screenshot(TEXT("game-pending")); });
    Flow.Add(TEXT("Open the actual install guide"), [this]() { return Flow.Click(TEXT("Install guide")); });
    Flow.Add(TEXT("Capture and close only the guide opened by this fixture"),
        [this]()
        {
            for (const auto& Window : FSlateApplication::Get().GetInteractiveTopLevelWindows())
            {
                if (Window->GetTitle().ToString() == TEXT("Install guide"))
                {
                    const bool Captured = Flow.CaptureWidget(TEXT("guide"), Window);
                    Window->RequestDestroyWindow();
                    return Captured;
                }
            }
            return false;
        });
    Flow.Add(TEXT("Edit a newer mod version"), [this]() { return Flow.SetField(TEXT("Version"), TEXT("1.0.1")); });
    Flow.Add(TEXT("Save the newer version through its control"), [this]() { return Flow.Click(TEXT("Save mod")); });
    Flow.Add(TEXT("Review the newer export"), [this]() { return Flow.ClickQueuedExport(TEXT("Review export")); }, 240);
    Flow.Add(TEXT("Hold an owned Windows handle that prevents target renaming"),
        [this]()
        {
#if PLATFORM_WINDOWS
            ReceiptLock = MakeUnique<FP4KadanReceiptPublicationLock>();
            return Flow.Expect(
                ReceiptLock->Open(ReceiptDirectory), TEXT("The owned publication directory could not be locked"));
#else
        return Flow.Expect(false, TEXT("The native receipt publication-lock fixture requires Windows"));
#endif
        });
    Flow.Add(
        TEXT("Attempt the newer export through its actual control"),
        [this]() { return Flow.ClickQueuedExport(TEXT("Export mod")); }, 600);
    Flow.Add(TEXT("Release only the fixture's own directory handle"),
        [this]()
        {
#if PLATFORM_WINDOWS
            ReceiptLock.Reset();
#endif
            return true;
        });
    Flow.Add(TEXT("The failed newer attempt preserves the successful receipt and output"),
        [this]() { return ReceiptKept(); });
    Flow.Add(TEXT("Reveal the retained receipt under the newer failure notice"),
        [this]() { return Flow.Prepare(Flow.Tagged(TEXT("KadanReceiptIdentity"))); });
    Flow.Add(TEXT("Capture the identifiable earlier success after a newer failure"),
        [this]() { return Flow.Screenshot(TEXT("failed-newer")); });
}

bool FReceiptStage::ReceiptFixture()
{
    FString Error;
    FP4ModDescriptor Stored;
    if (!Flow.Expect(P4ReadModDescriptor(FPaths::ProjectDir(), Flow.B.Id, Stored, Error), Error))
    {
        return false;
    }
    Flow.B = Stored;
    ReceiptFile = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("Kadan/ExportReceipts") / (Flow.B.Id + TEXT(".json")));
    if (Flow.Stage == TEXT("ReceiptReopen"))
    {
        FP4KadanExportReceipt Receipt;
        return Flow.Expect(P4KadanReadExportReceipt(FPaths::ProjectDir(), Flow.B.Id, Receipt, Error) &&
                Receipt.Mod.Version == TEXT("1.0.0") && !Receipt.GameTestRecorded,
            TEXT("The cold receipt did not retain the earlier successful version and pending game test"));
    }
    if (!Assets.MapProgressFixture(false))
    {
        return false;
    }
    UWorld* World = GEditor->GetEditorWorldContext().World();
    auto* Actor = FindObject<AStaticMeshActor>(World->PersistentLevel, TEXT("KadanProgressCube"));
    if (!Flow.Expect(Actor != nullptr, TEXT("The owned movable actor is unavailable")))
    {
        return false;
    }
    Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
    Flow.ExportFolder = FPaths::GetPath(Flow.Out) / TEXT("export/Receipt output with a longer folder name");
    ReceiptDirectory = FPaths::ConvertRelativePathToFull(Flow.ExportFolder / Flow.B.Id);
    return Flow.Expect(UEditorLoadingAndSavingUtils::SavePackages({World->GetOutermost()}, true),
        TEXT("The independent omission-note fixture could not save"));
}

bool FReceiptStage::ReceiptOutputSnapshot(bool Compare)
{
    TArray<FString> Paths;
    IFileManager::Get().FindFilesRecursive(Paths, *ReceiptDirectory, TEXT("*"), true, false);
    TMap<FString, FString> Current;
    for (const FString& File : Paths)
    {
        FString Sha;
        int64 Bytes = 0;
        if (!Flow.Expect(P4HashFile(File, 128 * 1024 * 1024, Bytes, Sha),
                TEXT("A published receipt fixture file could not be hashed")))
        {
            return false;
        }
        Current.Add(File, Sha);
    }
    if (!Compare)
    {
        ReceiptPublication = MoveTemp(Current);
        return Flow.Expect(
            ReceiptPublication.Num() >= 4, TEXT("The receipt fixture has no complete published container"));
    }
    bool Same = ReceiptPublication.Num() == Current.Num();
    for (const auto& Pair : ReceiptPublication)
    {
        Same &= Current.FindRef(Pair.Key) == Pair.Value;
    }
    return Flow.Expect(Same, TEXT("The failed newer export changed the previous published files"));
}

bool FReceiptStage::InspectReceiptExport()
{
    const FString Manifest = ReceiptDirectory / TEXT("romfs/tsuchi/mods") / Flow.B.Id / TEXT("mod.json");
    FString Text;
    TSharedPtr<FJsonObject> Json;
    if (!Flow.Expect(FFileHelper::LoadFileToString(Text, *Manifest) &&
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) && Json &&
                Json->GetStringField(TEXT("id")) == Flow.B.Id && Json->GetStringField(TEXT("version")) == TEXT("1.0.0"),
            TEXT("The receipt fixture did not publish its successful manifest identity")))
    {
        return false;
    }
    FP4ExportContainerTools Tools;
    Tools.Retoc = Flow.Tool(TEXT("P4_NATIVE_RETOC"), TEXT("private/retoc/retoc.exe"));
    Tools.Repak = Flow.Tool(TEXT("P4_NATIVE_REPAK"), TEXT("private/repak/repak.exe"));
    FP4ExportGameInventory Inventory;
    FP4ExportContainerCensus Census;
    FString Error;
    const FString Base = FPaths::GetPath(Manifest) / (Flow.BNames.Container + TEXT("_P"));
    return Flow.Expect(
        P4ReadExportGameInventory(Tools, FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS")),
            Flow.Out / TEXT("inspection"), Inventory, Error) &&
            P4InspectExportContainer(
                Tools, Base + TEXT(".utoc"), Flow.Out / TEXT("inspection"), Inventory, Census, Error) &&
            Census.PackageIds.Num() > 0,
        TEXT("Independent receipt container inspection failed. ") + Error);
}

bool FReceiptStage::ReceiptSaved()
{
    FP4KadanExportReceipt Receipt;
    FString Error;
    if (!Flow.Expect(P4KadanReadExportReceipt(FPaths::ProjectDir(), Flow.B.Id, Receipt, Error) &&
                Receipt.Mod.Version == TEXT("1.0.0") && Receipt.Directory == ReceiptDirectory &&
                Receipt.UnbakedActors > 0 && Receipt.Notes.Num() > 0 && !Receipt.GameTestRecorded,
            TEXT("The actual successful receipt lost its identity, omission note or pending game status")))
    {
        return false;
    }
    ReceiptTime = Flow.TaggedText(TEXT("KadanReceiptTime"));
    return Flow.Expect(FFileHelper::LoadFileToArray(ReceiptBefore, *ReceiptFile),
               TEXT("The successful receipt bytes could not be read")) &&
        ReceiptOutputSnapshot(false) && InspectReceiptExport();
}

bool FReceiptStage::ReceiptKept()
{
    TArray<uint8> Current;
    return Flow.Expect(FFileHelper::LoadFileToArray(Current, *ReceiptFile) && Current == ReceiptBefore &&
                   Flow.TaggedText(TEXT("KadanReceiptIdentity")) == Flow.B.Id + TEXT(" 1.0.0") &&
                   Flow.TaggedText(TEXT("KadanReceiptTime")) == ReceiptTime &&
                   Flow.TaggedText(TEXT("KadanStatus")).Contains(TEXT("export stopped"), ESearchCase::IgnoreCase),
               TEXT("A failed newer export replaced the successful receipt or lost its failure notice")) &&
        ReceiptOutputSnapshot(true);
}

TUniquePtr<FFlowStage> MakeReceiptStage(FFlow& Flow)
{
    return MakeUnique<FReceiptStage>(Flow);
}
} // namespace P4KadanCreatorFlow
#endif

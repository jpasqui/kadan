#include "P4KadanCreatorFlowStageList.h"

#if P4_MAPKIT_TESTS
namespace P4KadanCreatorFlow
{

namespace
{
// HistoryExport: a release-history project exports its maps while another unsaved map stays open,
// and a blocked output keeps the previous publication.
class FHistoryStage final : public FFlowStage
{
public:
    using FFlowStage::FFlowStage;

    void AddSteps() override;

private:
    // The owned project's identity and roots, and another open map with an unsaved edit.
    void AddFixtureSteps();

    // The review and real export through the panel.
    void AddExportSteps();

    // A blocked output fails once and keeps the previous mod.
    void AddFailureSteps();

    bool HistoryPublication(bool Compare);

    bool HistoryOpenPreserved();

    TWeakObjectPtr<UWorld> HistoryOpen;
    TWeakObjectPtr<AActor> HistoryUnsaved;
    TMap<FString, FString> HistoryPublished;
    FDateTime HistoryExportStarted = FDateTime(0);
};
} // namespace

void FHistoryStage::AddSteps()
{
    AddFixtureSteps();
    AddExportSteps();
    AddFailureSteps();
}

void FHistoryStage::AddFixtureSteps()
{
    Flow.Add(TEXT("Verify the owned release-history project and saved identity"),
        [this]()
        {
            FString MarkerText;
            TSharedPtr<FJsonObject> MarkerJson;
            const bool Owned = FFileHelper::LoadFileToString(
                                   MarkerText, *(FPaths::ProjectDir() / TEXT("history-owner.json"))) &&
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(MarkerText), MarkerJson) && MarkerJson &&
                MarkerJson->GetStringField(TEXT("owner")) == TEXT("kadan-ux-release-history-1025");
            FString Error;
            if (!Flow.Expect(Owned, TEXT("The release-history project ownership receipt is unavailable")) ||
                !Flow.Expect(P4ReadModDescriptor(FPaths::ProjectDir(), TEXT("kadan.history"), Flow.A, Error), Error))
            {
                return true;
            }
            TArray<FP4ExportRoot> Roots;
            TArray<FString> Packages;
            if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NAVIGATION_ROOT_ONLY")) != TEXT("1"))
            {
                Packages.Add(TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave033/Cave033_F00/Cave033_F00_P"));
            }
            Packages.Add(TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Cave000_F00_P"));
            for (const FString& Package : Packages)
            {
                FP4ExportRoot Root;
                Root.Kind = EP4ExportRootKind::Package;
                Root.Path = Package;
                Roots.Add(Root);
            }
            IFileManager::Get().MakeDirectory(*Flow.Out, true);
            return Flow.Expect(P4WriteExportRoots(FPaths::ProjectDir(), Flow.A, Roots, Error), Error);
        });
    Flow.Add(TEXT("Leave another map open with an unsaved edit"),
        [this]()
        {
            UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
            if (!Flow.Expect(World != nullptr, TEXT("The owned scratch map could not open")))
            {
                return true;
            }
            HistoryOpen = World;
            HistoryUnsaved = World->SpawnActor<AStaticMeshActor>(FVector(12, 34, 56), FRotator::ZeroRotator);
            World->GetOutermost()->SetDirtyFlag(true);
            return HistoryOpenPreserved();
        });
}

void FHistoryStage::AddExportSteps()
{
    Flow.Add(
        TEXT("Open the actual Kadan Mods page"), [this]() { return Flow.Menu(TEXT("Mods and export"), TEXT("Mods")); });
    Flow.Add(TEXT("Use the qualified panel size"), [this]() { return Flow.NarrowPanel(); });
    Flow.Add(TEXT("Choose the released-history mod"), [this]() { return Flow.Choose(Flow.A); });
    Flow.Add(TEXT("Open shared source settings"), [this]() { return Flow.Click(TEXT("Import")); });
    Flow.Add(TEXT("Set the game's Paks directory"),
        [this]()
        {
            return Flow.SetField(
                TEXT("Game folder"), FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS")));
        });
    Flow.Add(TEXT("Open Advanced source settings"),
        [this]() { return Flow.Expand(TEXT("KadanAdvanced"), TEXT("Advanced"), true); });
    Flow.Add(TEXT("Set the current retoc tool"), [this]()
        { return Flow.SetField(TEXT("retoc"), FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"))); });
    Flow.Add(TEXT("Set the current repak tool"), [this]()
        { return Flow.SetField(TEXT("repak"), FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_REPAK"))); });
    Flow.Add(
        TEXT("Enable the extracted source field"), [this]() { return Flow.CheckTag(TEXT("KadanUseExtracted"), true); });
    Flow.Add(TEXT("Choose the release history's converted source"),
        [this]()
        {
            return Flow.SetField(TEXT("Extracted packages"),
                FPaths::ProjectSavedDir() / TEXT("Kadan/ConvertedOriginals/Carrot4/Content"));
        });
    Flow.Add(TEXT("Return to Mods with the selected map closed"), [this]() { return Flow.Click(TEXT("Mods")); });
    Flow.Add(TEXT("Set the reusable output directory"),
        [this]() { return Flow.SetField(TEXT("Output folder"), Flow.ExportFolder); });
    Flow.Add(
        TEXT("Review the imported selection through the production button"),
        [this]() { return Flow.ClickQueuedExport(TEXT("Review export")); }, 240);
    Flow.Add(TEXT("Capture the ordinary export review"),
        [this]()
        {
            return HistoryOpenPreserved() &&
                Flow.Expect(
                    Flow.ButtonEnabled(TEXT("Export mod")), TEXT("The release-history mod did not pass review")) &&
                Flow.Screenshot(TEXT("review"));
        });
    Flow.Add(
        TEXT("Export while the creator's unsaved map stays open"),
        [this]()
        {
            if (HistoryExportStarted.GetTicks() == 0)
            {
                HistoryExportStarted = FDateTime::UtcNow();
            }
            return Flow.ClickQueuedExport(TEXT("Export mod"));
        },
        1200);
    Flow.Add(TEXT("Capture the real export result and retained open map"),
        [this]()
        {
            FString Manifest;
            FString Error;
            FP4KadanExportReceipt Receipt;
            const FString File = Flow.ExportFolder / Flow.A.Id / TEXT("romfs/tsuchi/mods") / Flow.A.Id /
                TEXT("mod.json");
            return HistoryOpenPreserved() &&
                Flow.Expect(Flow.TaggedText(TEXT("KadanStatus")).StartsWith(TEXT("Kadan exported the mod to ")) &&
                        P4KadanReadExportReceipt(FPaths::ProjectDir(), Flow.A.Id, Receipt, Error) &&
                        Receipt.PublishedUtc >= HistoryExportStarted,
                    TEXT("The actual export did not produce a fresh successful publication receipt")) &&
                Flow.Expect(
                    Receipt.Files.ContainsByPredicate([](const FString& Name) { return Name.EndsWith(TEXT(".nav")); }),
                    TEXT("The published export did not contain rebuilt navigation")) &&
                Flow.Expect(
                    FFileHelper::LoadFileToString(Manifest, *File), TEXT("The mod did not publish a manifest")) &&
                HistoryPublication(false) && Flow.Screenshot(TEXT("published"));
        });
}

void FHistoryStage::AddFailureSteps()
{
    Flow.Add(TEXT("Make one owned blocking output file"),
        [this]()
        {
            return Flow.Expect(
                FFileHelper::SaveStringToFile(TEXT("Owned blocked output fixture"),
                    *(Flow.Out / TEXT("blocked-output")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM),
                TEXT("The owned blocking file could not be written"));
        });
    Flow.Add(TEXT("Choose the blocked output path"),
        [this]() { return Flow.SetField(TEXT("Output folder"), Flow.Out / TEXT("blocked-output")); });
    Flow.Add(
        TEXT("Review the same map selection before the controlled failure"),
        [this]() { return Flow.ClickQueuedExport(TEXT("Review export")); }, 240);
    Flow.Add(
        TEXT("Attempt export through its production button"),
        [this]() { return Flow.ClickQueuedExport(TEXT("Export mod")); }, 600);
    Flow.Add(TEXT("Open the exact failure details"),
        [this]() { return Flow.Expand(TEXT("KadanDetails"), TEXT("Details"), true); });
    Flow.Add(TEXT("Copy the actual failure details"), [this]() { return Flow.Click(TEXT("Copy details")); });
    Flow.Add(TEXT("Capture one failure cause and preserve the previous mod"),
        [this]()
        {
            const FString Details = Flow.TaggedText(TEXT("KadanDetailsText"));
            const FString Cause = TEXT("Kadan could not prepare the export work folders.");
            FString Copied;
            FPlatformApplicationMisc::ClipboardPaste(Copied);
            const int32 First = Details.Find(Cause);
            return Flow.Expect(Copied == Details && First != INDEX_NONE &&
                           Details.Find(Cause, ESearchCase::CaseSensitive, ESearchDir::FromStart,
                               First + Cause.Len()) == INDEX_NONE &&
                           !Flow.TaggedText(TEXT("KadanStatus")).Contains(Cause),
                       TEXT("The same failure cause was duplicated in the summary or Details")) &&
                HistoryOpenPreserved() && HistoryPublication(true) && Flow.Screenshot(TEXT("failure-details"));
        });
    Flow.Add(TEXT("Restore the ordinary output folder"),
        [this]() { return Flow.SetField(TEXT("Output folder"), Flow.ExportFolder); });
}

bool FHistoryStage::HistoryPublication(bool Compare)
{
    TArray<FString> Files;
    IFileManager::Get().FindFilesRecursive(Files, *(Flow.ExportFolder / Flow.A.Id), TEXT("*"), true, false);
    TMap<FString, FString> Current;
    for (const FString& File : Files)
    {
        FString Sha;
        int64 Bytes = 0;
        if (!P4HashFile(File, 64 * 1024 * 1024, Bytes, Sha))
        {
            return Flow.Expect(false, TEXT("The published history mod could not be hashed"));
        }
        Current.Add(File, Sha);
    }
    if (!Compare)
    {
        HistoryPublished = MoveTemp(Current);
        return Flow.Expect(HistoryPublished.Num() > 0, TEXT("The published history mod has no files"));
    }
    bool Same = Current.Num() == HistoryPublished.Num();
    for (const auto& Pair : HistoryPublished)
    {
        Same &= Current.FindRef(Pair.Key) == Pair.Value;
    }
    return Flow.Expect(Same, TEXT("The failed export changed the previous published mod"));
}

bool FHistoryStage::HistoryOpenPreserved()
{
    return Flow.Expect(HistoryOpen.IsValid() && HistoryUnsaved.IsValid() &&
            GEditor->GetEditorWorldContext().World() == HistoryOpen.Get() && HistoryOpen->GetOutermost()->IsDirty() &&
            HistoryUnsaved->GetActorLocation().Equals(FVector(12, 34, 56)),
        TEXT("Export did not preserve the creator's open map and unsaved edit"));
}

TUniquePtr<FFlowStage> MakeHistoryStage(FFlow& Flow)
{
    return MakeUnique<FHistoryStage>(Flow);
}
} // namespace P4KadanCreatorFlow
#endif

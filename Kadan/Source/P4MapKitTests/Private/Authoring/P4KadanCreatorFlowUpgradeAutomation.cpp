#include "P4KadanCreatorFlowStageList.h"
#include "P4KadanCreatorFlowShared.h"

#if P4_MAPKIT_TESTS
namespace P4KadanCreatorFlow
{

namespace
{
// Upgrade and UpgradeReopen: Reimport keeps saved actor and creator content through refusals, a canceled
// batch and its retry, and after a restart.
class FUpgradeStage final : public FFlowStage
{
public:
    using FFlowStage::FFlowStage;

    void AddSteps() override;

private:
    // Imported content selected across filters and the upgrade guide.
    void AddGuideSteps();

    // A dependency summary, the unsaved-level refusal and a bounded texture retry.
    void AddRetrySteps();

    // A canceled batch keeps completed packages, then completes on retry.
    void AddBatchSteps();

    TSharedPtr<SWindow> UpgradeWindow(const FString& Title);

    bool UpgradeClick(const FString& Caption);

    bool UpgradeCapture(const FString& Name, const FString& Title = TEXT("Reimport summary"));

    bool UpgradeFixture();

    bool UpgradeSaveOwnedWorlds();

    bool UpgradePartialCancel();

    bool UpgradeImportNotes();

    FFlowAssets Assets{Flow};
    FUpgradeSnapshot Snapshot{Flow};
    FString UpgradeDependencyRoot;
    FDateTime UpgradePriorReport;
    TUniquePtr<TGuardValue<double>> UpgradeReportInterval;
    int32 UpgradeCompletedAtCancel = 0;
};
} // namespace

void FUpgradeStage::AddSteps()
{
    if (Flow.Stage == TEXT("UpgradeReopen"))
    {
        Flow.Add(
            TEXT("Reopen the actual map and compare saved actor and creator mesh content"),
            [this]() { return Snapshot.UpgradeContent(true); }, 180);
        Flow.Add(TEXT("Open Import after cold content readback"),
            [this]() { return Flow.Menu(TEXT("Import from game"), TEXT("Choose Pikmin 4")); });
        Flow.Add(TEXT("Capture the reopened project and installed upgrade guide control"),
            [this]() { return Flow.Screenshot(TEXT("retained")); });
        return;
    }
    AddGuideSteps();
    AddRetrySteps();
    AddBatchSteps();
}

void FUpgradeStage::AddGuideSteps()
{
    Flow.Add(
        TEXT("Prepare old imported worlds with saved actor and creator content"), [this]() { return UpgradeFixture(); },
        180);
    Flow.Add(TEXT("Open Import through its menu"),
        [this]() { return Flow.Menu(TEXT("Import from game"), TEXT("Choose Pikmin 4")); });
    Flow.Add(TEXT("Use the narrow panel"), [this]() { return Flow.NarrowPanel(); });
    Flow.Add(TEXT("Open Advanced"), [this]() { return Flow.Expand(TEXT("KadanAdvanced"), TEXT("Advanced"), true); });
    Flow.Add(TEXT("Choose actual retoc"), [this]()
        { return Flow.SetField(TEXT("retoc"), Flow.Tool(TEXT("P4_NATIVE_RETOC"), TEXT("private/retoc/retoc.exe"))); });
    Flow.Add(TEXT("Choose actual repak"), [this]()
        { return Flow.SetField(TEXT("repak"), Flow.Tool(TEXT("P4_NATIVE_REPAK"), TEXT("private/repak/repak.exe"))); });
    Flow.Add(TEXT("Use the game catalog"), [this]() { return Flow.CheckTag(TEXT("KadanUseExtracted"), false); });
    Flow.Add(TEXT("Close Advanced"), [this]() { return Flow.Expand(TEXT("KadanAdvanced"), TEXT("Advanced"), false); });
    Flow.Add(TEXT("Choose the original game containers"),
        [this]()
        {
            return Flow.SetField(
                TEXT("Game folder"), FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS")));
        });
    Flow.Add(TEXT("Read the real catalog"), [this]() { return Flow.CommitField(TEXT("Game folder")); }, 180);
    Flow.Add(TEXT("Hide the browser rows with a filter"),
        [this]() { return Flow.SetField(TEXT("Search"), TEXT("no-such-source-kadan-upgrade-control")); });
    Flow.Add(
        TEXT("Select all imported packages across filters"), [this]() { return Flow.Click(TEXT("Select imported")); },
        240);
    Flow.Add(TEXT("Verify and capture the cross-filter count"),
        [this]()
        {
            return Flow.Expect(Flow.TaggedText(TEXT("KadanImportCounts")).StartsWith(TEXT("0 of ")) &&
                           Flow.TaggedText(TEXT("KadanStatus")).Contains(TEXT("across all folders and filters")),
                       TEXT("The imported selection did not cross the active filter")) &&
                Flow.Screenshot(TEXT("selection"));
        });
    Flow.Add(TEXT("Open the actual upgrade guide"), [this]() { return Flow.Click(TEXT("Upgrade guide")); });
    Flow.Add(TEXT("Inspect the version as information and capture the guide"),
        [this]()
        {
            const auto Window = UpgradeWindow(TEXT("Upgrade guide"));
            if (!Window)
            {
                return false;
            }
            const bool Captured = UpgradeCapture(TEXT("guide"), TEXT("Upgrade guide"));
            Window->RequestDestroyWindow();
            return Captured;
        });
}

void FUpgradeStage::AddRetrySteps()
{
    Flow.Add(TEXT("Clear the broad selection"), [this]() { return Flow.Click(TEXT("Clear selection")); });
    Flow.Add(TEXT("Find one imported mesh and its required content"),
        [this]() { return Flow.SetField(TEXT("Search"), UpgradeDependencyRoot); });
    Flow.Add(TEXT("Select the game mesh"), [this]() { return Flow.Click(TEXT("Select visible")); });
    Flow.Add(TEXT("Request its dependency summary"), [this]() { return Flow.Click(TEXT("Reimport")); }, 180);
    Flow.Add(TEXT("Capture real selected and required package rows"),
        [this]()
        {
            const auto Window = UpgradeWindow(TEXT("Reimport summary"));
            if (!Window)
            {
                return false;
            }
            UpgradeCapture(TEXT("required"));
            return Flow.Expect(Text(Window.ToSharedRef(), TEXT("Required reference")).IsValid(),
                TEXT("The actual game mesh summary contains no required references"));
        });
    Flow.Add(TEXT("Cancel only the dependency summary"), [this]() { return UpgradeClick(TEXT("Cancel")); });
    Flow.Add(TEXT("Clear the mesh selection before the bounded retry"),
        [this]() { return Flow.Click(TEXT("Clear selection")); });
    Flow.Add(TEXT("Find the bounded retained source texture"),
        [this]() { return Flow.SetField(TEXT("Search"), Snapshot.UpgradeTexture); });
    Flow.Add(TEXT("Select only the actual texture"), [this]() { return Flow.Click(TEXT("Select visible")); });
    Flow.Add(TEXT("Mark the owned map as having unsaved work for the refusal check"),
        [this]()
        {
            GEditor->GetEditorWorldContext().World()->MarkPackageDirty();
            return true;
        });
    Flow.Add(TEXT("Request the reimport summary"), [this]() { return Flow.Click(TEXT("Reimport")); }, 180);
    Flow.Add(TEXT("Capture selected content and the unsaved level in the summary"),
        [this]() { return UpgradeCapture(TEXT("summary")); });
    Flow.Add(TEXT("Confirm reimport with the dirty level"), [this]() { return UpgradeClick(TEXT("Reimport")); }, 180);
    Flow.Add(TEXT("The normal backend refuses unsaved level work"),
        [this]()
        {
            return Flow.Expect(
                Flow.TaggedText(TEXT("KadanStatus")).StartsWith(TEXT("Save your level changes before reimporting.")),
                TEXT("The normal dirty-level refusal was not preserved"));
        });
    Flow.Add(TEXT("Reveal the exact dirty-level refusal"), [this]() { return UpgradeImportNotes(); });
    Flow.Add(
        TEXT("Capture the actual dirty-level refusal"), [this]() { return Flow.Screenshot(TEXT("dirty-refusal")); });
    Flow.Add(TEXT("Save only the owned fixture map before retrying"), [this]() { return UpgradeSaveOwnedWorlds(); });
    Flow.Add(TEXT("Request the ordinary retry summary"), [this]() { return Flow.Click(TEXT("Reimport")); }, 180);
    Flow.Add(TEXT("Confirm the bounded texture reimport"), [this]() { return UpgradeClick(TEXT("Reimport")); }, 180);
    Flow.Add(TEXT("The texture actually refreshes and saved creator content stays intact"),
        [this]()
        {
            return Flow.Expect(Flow.TaggedText(TEXT("KadanStatus")).StartsWith(TEXT("Kadan imported 1 package.")),
                       TEXT("The bounded texture reimport did not complete")) &&
                Snapshot.UpgradeContent(false);
        });
    Flow.Add(TEXT("Reveal retained per-package outcomes"), [this]() { return UpgradeImportNotes(); });
    Flow.Add(TEXT("Capture the successful retry outcome"), [this]() { return Flow.Screenshot(TEXT("retry")); });
}

void FUpgradeStage::AddBatchSteps()
{
    Flow.Add(TEXT("Clear the single texture selection"), [this]() { return Flow.Click(TEXT("Clear selection")); });
    Flow.Add(TEXT("Find a bounded batch of source textures"),
        [this]() { return Flow.SetField(TEXT("Search"), TEXT("/Game/Carrot4/Characters/Buddy/Happy/T_chr_Happy")); });
    Flow.Add(TEXT("Select the visible source batch"), [this]() { return Flow.Click(TEXT("Select visible")); });
    Flow.Add(TEXT("Save the owned map before the next reimport"), [this]() { return UpgradeSaveOwnedWorlds(); });
    Flow.Add(TEXT("Request the batch repair summary"), [this]() { return Flow.Click(TEXT("Reimport")); }, 180);
    Flow.Add(TEXT("Arm actual cancellation after a completed package"), [this]() { return UpgradePartialCancel(); });
    Flow.Add(TEXT("Start the actual batch reimport"), [this]() { return UpgradeClick(TEXT("Reimport")); }, 240);
    Flow.Add(TEXT("The actual Cancel control preserves completed imports"),
        [this]()
        {
            Flow.StopCancellation();
            UpgradeReportInterval.Reset();
            FString Report;
            if (!Flow.Expect(FFileHelper::LoadFileToString(
                                 Report, *(FPaths::ProjectSavedDir() / TEXT("Kadan/import-result.json"))) &&
                        FFileHelper::SaveStringToFile(Report, *(Flow.Out / TEXT("upgrade-partial-result.json")),
                            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM),
                    TEXT("The actual partial import report could not be retained")))
            {
                return true;
            }
            return Flow.Expect(Flow.CancelClicked && UpgradeCompletedAtCancel > 0 &&
                           Flow.TaggedText(TEXT("KadanStatus")).StartsWith(TEXT("The import was canceled.")),
                       TEXT("The bounded batch did not produce actual partial cancellation")) &&
                Snapshot.UpgradeContent(false, false);
        });
    Flow.Add(TEXT("Reveal partial per-package outcomes"), [this]() { return UpgradeImportNotes(); });
    Flow.Add(TEXT("Capture partial cancellation and the real retry control"),
        [this]() { return Flow.Screenshot(TEXT("canceled")); });
    Flow.Add(TEXT("Save the owned map before retrying the interrupted batch"),
        [this]() { return UpgradeSaveOwnedWorlds(); });
    Flow.Add(TEXT("Retry through the actual reimport control"), [this]() { return Flow.Click(TEXT("Reimport")); }, 180);
    Flow.Add(TEXT("Confirm the batch retry"), [this]() { return UpgradeClick(TEXT("Reimport")); }, 240);
    Flow.Add(TEXT("The batch retry completes with retained content"),
        [this]()
        {
            return Flow.Expect(Flow.TaggedText(TEXT("KadanStatus")).StartsWith(TEXT("Kadan imported ")) &&
                           !Flow.TaggedText(TEXT("KadanStatus")).Contains(TEXT("with issues")),
                       TEXT("The batch retry failed")) &&
                Snapshot.UpgradeContent(false);
        });
    Flow.Add(TEXT("Reveal the final batch outcomes"), [this]() { return UpgradeImportNotes(); });
    Flow.Add(TEXT("Capture the final retry"), [this]() { return Flow.Screenshot(TEXT("batch-retry")); });
}

TSharedPtr<SWindow> FUpgradeStage::UpgradeWindow(const FString& Title)
{
    TArray<TSharedRef<SWindow>> Windows;
    FSlateApplication::Get().GetAllVisibleWindowsOrdered(Windows);
    if (const auto Modal = FSlateApplication::Get().GetActiveModalWindow())
    {
        Windows.AddUnique(Modal.ToSharedRef());
    }
    for (const auto& Window : Windows)
    {
        if (Window->IsVisible() && Window->GetTitle().ToString() == Title)
        {
            return Window;
        }
    }
    return nullptr;
}

bool FUpgradeStage::UpgradeClick(const FString& Caption)
{
    const auto Window = UpgradeWindow(TEXT("Reimport summary"));
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

bool FUpgradeStage::UpgradeCapture(const FString& Name, const FString& Title)
{
    const auto Window = UpgradeWindow(Title);
    return Window && Flow.CaptureWidget(Name, Window);
}

bool FUpgradeStage::UpgradeFixture()
{
    if (!Assets.MapProgressFixture(false))
    {
        return false;
    }
    UWorld* World = GEditor->GetEditorWorldContext().World();
    AActor* Original = nullptr;
    int32 Checked = 0;
    for (AActor* Actor : World->PersistentLevel->Actors)
    {
        if (!Actor || !Cast<UArrowComponent>(Actor->GetRootComponent()) || Actor->IsEditorOnly() ||
            Actor->IsA<ABrush>() || Actor == World->GetWorldSettings() ||
            Actor->GetFName() == TEXT("KadanProgressCube"))
        {
            continue;
        }
        if (++Checked > 16)
        {
            break;
        }
        const FTransform Before = Actor->GetActorTransform();
        Actor->Modify();
        Actor->SetActorLocation(Before.GetLocation() + FVector(35, 0, 0));
        const auto Changes = P4AnalyzeOriginalLevelChanges(World);
        if (Changes.IsValid() &&
            Changes.Moved.ContainsByPredicate(
                [Actor](const FP4LevelActorMove& Move) { return Move.Actor == Actor->GetName(); }))
        {
            Original = Actor;
            break;
        }
        Actor->SetActorTransform(Before);
    }
    if (!Flow.Expect(
            Original != nullptr, TEXT("The retained persistent map has no supported original actor move fixture")))
    {
        return false;
    }
    Snapshot.UpgradeActorPath = Original->GetPathName();
    Snapshot.UpgradeActorTransform = Original->GetActorTransform();
    World->MarkPackageDirty();
    if (!Flow.Expect(UEditorLoadingAndSavingUtils::SavePackages({World->GetOutermost()}, true),
            TEXT("The owned saved actor change could not be saved")))
    {
        return false;
    }
    for (TActorIterator<AActor> It(World); It && UpgradeDependencyRoot.IsEmpty(); ++It)
    {
        TInlineComponentArray<UStaticMeshComponent*> Components(*It);
        for (const auto* Component : Components)
        {
            UStaticMesh* Mesh = Component->GetStaticMesh();
            FP4OriginalRecord Record;
            FP4OriginalRecord MaterialRecord;
            FString Error;
            if (Mesh && Mesh->StaticMaterials.Num() > 0 && Mesh->GetMaterial(0) &&
                P4ReadOriginalRecord(Mesh->GetMaterial(0), MaterialRecord, Error) &&
                P4ReadOriginalRecord(Mesh, Record, Error))
            {
                UpgradeDependencyRoot = Record.Package;
                break;
            }
        }
    }
    if (!Flow.Expect(
            !UpgradeDependencyRoot.IsEmpty(), TEXT("The imported fixture has no game mesh for required-content proof")))
    {
        return false;
    }
    Snapshot.UpgradeMeshFile = FPackageName::LongPackageNameToFilename(
        Flow.BNames.PackageRoot / TEXT("ProgressCube"), FPackageName::GetAssetPackageExtension());
    int64 Bytes = 0;
    if (!Flow.Expect(P4HashFile(Snapshot.UpgradeMeshFile, 64 * 1024 * 1024, Bytes, Snapshot.UpgradeMeshHash),
            TEXT("The owned creator mesh could not be hashed")))
    {
        return false;
    }
    auto Record = MakeShared<FJsonObject>();
    Record->SetStringField(TEXT("map"), World->GetOutermost()->GetName());
    Record->SetStringField(TEXT("actor"), Snapshot.UpgradeActorPath);
    Record->SetStringField(TEXT("transform"), Snapshot.UpgradeActorTransform.ToString());
    Record->SetStringField(TEXT("mesh_file"), Snapshot.UpgradeMeshFile);
    Record->SetStringField(TEXT("mesh_sha256"), Snapshot.UpgradeMeshHash);
    FString Json;
    FJsonSerializer::Serialize(Record, TJsonWriterFactory<>::Create(&Json));
    return Flow.Expect(FFileHelper::SaveStringToFile(Json, *(Flow.Out / TEXT("upgrade-content.json")),
                           FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM),
        TEXT("The saved-content snapshot could not be written"));
}

bool FUpgradeStage::UpgradeSaveOwnedWorlds()
{
    const FString Map = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_UX_MAP"));
    const FString Prefix = FPackageName::GetLongPackagePath(FPackageName::GetLongPackagePath(Map)) + TEXT("/");
    TArray<UPackage*> Packages;
    for (TObjectIterator<UWorld> It; It; ++It)
    {
        UPackage* Package = It->GetOutermost();
        const FString Name = Package->GetName();
        if (!Name.StartsWith(TEXT("/Game/")) || !Package->IsDirty())
        {
            continue;
        }
        const FString File = FPaths::ConvertRelativePathToFull(
            FPackageName::LongPackageNameToFilename(Name, FPackageName::GetMapPackageExtension()));
        if (!Flow.Expect(Name.StartsWith(Prefix) && FPaths::IsUnderDirectory(File, FPaths::ProjectContentDir()) &&
                    IFileManager::Get().FileExists(*File),
                TEXT("Preserve an unexpected dirty world outside the independently copied fixture: ") + Name))
        {
            return false;
        }
        Packages.AddUnique(Package);
    }
    return Flow.Expect(Packages.Num() == 0 || UEditorLoadingAndSavingUtils::SavePackages(Packages, true),
        TEXT("The owned fixture's dirty worlds could not save"));
}

bool FUpgradeStage::UpgradePartialCancel()
{
    Flow.StopCancellation();
    Flow.CancelClicked = false;
    UpgradeCompletedAtCancel = 0;
    UpgradePriorReport = IFileManager::Get().GetTimeStamp(
        *(FPaths::ProjectSavedDir() / TEXT("Kadan/import-result.json")));
    UpgradeReportInterval = MakeUnique<TGuardValue<double>>(P4KadanReportInterval, 0.0);
    Flow.CancelTickHandle = FSlateApplication::Get().OnPostTick().AddLambda(
        [this](float)
        {
            if (Flow.CancelClicked)
            {
                return;
            }
            const auto Window = FSlateApplication::Get().GetActiveModalWindow();
            if (!Window)
            {
                return;
            }
            const auto Task = Find(Window.ToSharedRef(),
                [](const TSharedRef<SWidget>& Widget) { return Widget->GetType() == FName(TEXT("SSlowTaskWidget")); });
            if (!Task ||
                IFileManager::Get().GetTimeStamp(*(FPaths::ProjectSavedDir() / TEXT("Kadan/import-result.json"))) <=
                    UpgradePriorReport)
            {
                return;
            }
            FString Json;
            TSharedPtr<FJsonObject> Report;
            const TSharedPtr<FJsonObject>* Assets = nullptr;
            if (!FFileHelper::LoadFileToString(Json, *(FPaths::ProjectSavedDir() / TEXT("Kadan/import-result.json"))) ||
                !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Report) || !Report ||
                !Report->TryGetObjectField(TEXT("assets"), Assets) || (*Assets)->Values.Num() == 0)
            {
                return;
            }
            const auto Button = Find(Task.ToSharedRef(), [](const TSharedRef<SWidget>& Widget)
                { return Widget->GetType() == FName(TEXT("SButton")) && Text(Widget, TEXT("Cancel")); });
            if (Button && Flow.MouseClick(Button))
            {
                Flow.CancelClicked = true;
                UpgradeCompletedAtCancel = (*Assets)->Values.Num();
            }
        });
    return true;
}

bool FUpgradeStage::UpgradeImportNotes()
{
    return Flow.Expand(TEXT("KadanImportNotesArea"), TEXT("Last import notes"), true);
}

TUniquePtr<FFlowStage> MakeUpgradeStage(FFlow& Flow)
{
    return MakeUnique<FUpgradeStage>(Flow);
}
} // namespace P4KadanCreatorFlow
#endif

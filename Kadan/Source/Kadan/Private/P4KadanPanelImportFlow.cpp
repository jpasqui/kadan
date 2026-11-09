#include "P4KadanPanelImportPage.h"
#include "P4ExportContainer.h"
#include "Misc/MessageDialog.h"

namespace P4KadanEditorPrivate
{

void FKadanImportPage::RefreshProjectSetup()
{
    ProjectReady = P4KadanProjectReady(ProjectSetupReason);
    RestartRequired = P4KadanProjectRestartRequired();
}

void FKadanImportPage::InvalidateImportReview()
{
    ImportPlan = FP4KadanImportPlan();
    ImportSummary.Empty();
    // The page counts what the selection needs by itself once the boxes stop changing (Tick).
    ReviewWanted = SelectedPackages.Num() > 0;
    SelectionChangedAt = FPlatformTime::Seconds();
}

void FKadanImportPage::Tick()
{
    // Once space is freed, the selection is counted again and Import comes back on.
    if (ProjectDiskFull && FPlatformTime::Seconds() - DiskCheckedAt > 5.0)
    {
        DiskCheckedAt = FPlatformTime::Seconds();
        if (P4DiskFullMessage(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir())).IsEmpty())
        {
            ProjectDiskFull = false;
            Host.Message.Empty();
            Host.NoticeError = false;
            InvalidateImportReview();
        }
    }
    if (!ReviewWanted || FPlatformTime::Seconds() - SelectionChangedAt < 0.8 || !SelectedPackages.Num())
        return;
    ReviewWanted = false;
    ReviewImport(!ReimportSelected(), true);
}

bool FKadanImportPage::InProject(const FString& Package)
{
    if (const bool* Known = InProjectCache.Find(Package))
        return *Known;
    const bool There = FPackageName::DoesPackageExist(Package);
    InProjectCache.Add(Package, There);
    return There;
}

bool FKadanImportPage::ReimportSelected()
{
    for (const FString& Package : SelectedPackages)
        if (InProject(Package))
            return true;
    return false;
}

void FKadanImportPage::PrepareGameFolder()
{
    RefreshProjectSetup();
    if (!ReadSource() || Host.UseExtractedPackages)
        return;
    PreparedGameLocation = Host.GameLocation;
    AutoSetUpProject();
}

// Setup is done once per project, so Kadan runs it by itself the first time it has the game folder in a project that
// needs it, then asks to restart. Set up project stays for when that could not finish.
void FKadanImportPage::AutoSetUpProject()
{
    static bool Tried = false;
    if (Tried || ProjectReady || RestartRequired || FApp::IsUnattended() || Host.UseExtractedPackages ||
        Host.GamePaks.IsEmpty() || !IFileManager::Get().FileExists(*Host.Repak) || !P4KadanSetupCanFinish())
        return;
    Tried = true;
    SetUpProject(true);
}

// Rewrites the project's collision, navigation and world settings from the game: they are what the game's own
// editor used, and the creator's other settings stay.
void FKadanImportPage::SetUpProject(bool Automatic)
{
    if (Host.UseExtractedPackages)
    {
        Host.NoticeError = true;
        Host.Message = TEXT("Project setup reads the game catalog. Turn off UseExtractedPackages in Kadan's settings first.");
        return;
    }
    if (Host.GamePaks.IsEmpty())
    {
        Host.NoticeError = true;
        Host.Message = TEXT("Choose your game folder first.");
        return;
    }
    const auto Result = P4KadanSetupProject(Host.GamePaks, Host.Repak);
    Host.NoticeError = !Result.Ok;
    Host.Message = Result.Message;
    RefreshProjectSetup();
    // The settings take effect in a new editor session. Unreal asks to save open work before it restarts.
    if (!Result.Ok || !RestartRequired)
        return;
    const FText Title = FText::FromString(TEXT("Kadan setup"));
    if (Automatic &&
        FMessageDialog::Open(EAppMsgType::YesNo,
            FText::FromString(TEXT("Kadan set this project up for the game: ") + Result.Message +
                TEXT("\n\nRestart Unreal Editor now? Unreal asks you to save your work first. The first start after "
                     "setup may take a while: Unreal compiles thousands of shaders for the game's renderer.")),
            &Title) != EAppReturnType::Yes)
        return;
    FUnrealEdMisc::Get().RestartEditor(true);
}

void FKadanImportPage::QueueGameFolder()
{
    if (Host.GameLocation.TrimStartAndEnd().IsEmpty())
        return;
    if (PreparedGameLocation == Host.GameLocation && Assets.Num() && P4KadanSourceCatalogCurrent(SourceCatalog))
        return;
    Host.QueueOperation(TEXT("Kadan is reading the game folder."), [this]() { PrepareGameFolder(); });
}

void FKadanImportPage::FieldChanged(FString& Value)
{
    if (&Value == &Host.Repak)
        PreparedGameLocation.Empty();
    Host.SettingsChanged();
    if (&Value == &PackageFolder)
        Filter();
    if (&Value == &Host.GameLocation)
    {
        FString Why, Normalized;
        Host.GamePaks = P4KadanNormalizeGamePaks(Host.GameLocation, Normalized, Why) ? Normalized : FString();
        Host.Settings.GamePaks = Host.GamePaks;
    }
    if (&Value == &Host.GamePaks || &Value == &Host.Retoc)
        InvalidateImportReview();
    if (&Value == &Host.Settings.GameFolder || &Value == &Host.GameLocation || &Value == &Host.Retoc)
    {
        Assets.Empty();
        Visible.Empty();
        SelectedPackages.Empty();
        InvalidateImportReview();
        PreparedGameLocation.Empty();
        FolderRoots.Empty();
        if (FolderTree)
            FolderTree->RequestTreeRefresh();
        if (SourceList)
            SourceList->RequestListRefresh();
    }
}

FP4KadanImportSettings FKadanImportPage::EffectiveSettings() const
{
    FP4KadanImportSettings Out = Host.UseExtractedPackages ? Host.Settings : SourceCatalog.ImportSettings;
    Out.GamePaks = Host.GamePaks;
    Out.Retoc = Host.Retoc;
    Out.Repak = Host.Repak;
    // Any map imports: what cannot be rebuilt stays the game's and the import says so.
    Out.AllowGameBackedLevels = true;
    return Out;
}

bool FKadanImportPage::ReadSource()
{
    Assets.Empty();
    Visible.Empty();
    SelectedPackages.Empty();
    InvalidateImportReview();
    TArray<FString> Problems;
    bool Complete = false;
    if (Host.UseExtractedPackages)
    {
        Complete = P4KadanScanGame(Host.Settings.GameFolder, ExtractedAssets, Problems);
        for (const auto& Asset : ExtractedAssets)
        {
            auto Row = MakeShared<FSourceRow>();
            Row->Package = Asset->Package;
            Row->Kind = Asset->Kind;
            Row->Bytes = Asset->SourceBytes;
            Assets.Add(Row);
        }
    }
    else
    {
        FString Why;
        FP4KadanSourceCatalog Catalog;
        Complete = P4KadanReadSourceCatalog(Host.GameLocation, Host.Retoc, Catalog, Why);
        if (!Complete)
            Problems.Add(Why);
        else
        {
            SourceCatalog = MoveTemp(Catalog);
            Host.Settings = SourceCatalog.ImportSettings;
            Host.GamePaks = Host.Settings.GamePaks;
            Host.Retoc = Host.Settings.Retoc;
            for (const auto& Asset : SourceCatalog.Rows)
            {
                auto Row = MakeShared<FSourceRow>();
                Row->Package = Asset->Package;
                Row->Kind = Asset->Kind;
                Row->Bytes = Asset->SourceBytes;
                Assets.Add(Row);
            }
        }
    }
    Kinds.Empty();
    Kinds.Add(MakeShared<FString>(TEXT("All kinds")));
    TSet<FString> Found;
    for (const auto& Asset : Assets)
        Found.Add(Asset->Kind);
    TArray<FString> Names = Found.Array();
    Names.Sort();
    for (const FString& Name : Names)
        Kinds.Add(MakeShared<FString>(Name));
    Kinds.Add(MakeShared<FString>(TEXT("In project")));
    InProjectCache.Empty();
    // The list opens on the maps: most people come for one.
    SelectedKind = Kinds[0];
    for (const auto& Kind : Kinds)
        if (*Kind == TEXT("World"))
            SelectedKind = Kind;
    if (KindPicker)
        KindPicker->SetSelectedItem(SelectedKind);
    RebuildFolders();
    Filter();
    if (KindPicker)
    {
        KindPicker->RefreshOptions();
        KindPicker->SetSelectedItem(SelectedKind);
    }
    const bool Cancelled = !Complete &&
        Problems.ContainsByPredicate(
            [](const FString& Problem) { return Problem.Contains(TEXT("scan was cancelled")); });
    Host.NoticeError = !Complete && !Cancelled;
    ImportDetails = FString::Join(Problems, TEXT("\n"));
    Host.Message = Complete
        ? FString::Printf(
              TEXT("Kadan is ready to browse %d maps and assets. Select the content to import."), Assets.Num())
        : Cancelled      ? TEXT("The folder scan was canceled. Completed rows are kept. Choose the game folder "
                                "again to retry.")
        : Problems.Num() ? Problems[0]
                         : TEXT("The game folder could not be read. The Output Log says why.");
    for (const FString& Problem : Problems)
        UE_LOG(LogTemp, Warning, TEXT("Kadan source: %s"), *Problem);
    Host.WriteSettings();
    return Complete;
}

void FKadanImportPage::RequestReimport()
{
    Host.QueueOperation(
        TEXT("Kadan is reviewing the reimport selection."),
        [this]()
        {
            if (!ReviewImport(false))
            {
                return;
            }
            const TWeakPtr<SKadanPanel> Weak = Host.Self();
            RepairWindow = P4KadanImportRepair(Host.Self(), ImportPlan, SelectedPackages,
                [Weak]()
                {
                    if (const auto Panel = Weak.Pin())
                    {
                        Panel->Import().QueueImport(false);
                    }
                });
        },
        true);
}

void FKadanImportPage::QueueImport(bool KeepExisting)
{
    Host.QueueOperation(
        KeepExisting ? TEXT("Kadan is importing selected game content.")
                     : TEXT("Kadan is reimporting selected game content."),
        [this, KeepExisting]()
        {
            {
                FScopedSlowTask ReviewProgress(
                    1.f, Label(TEXT("Kadan is reviewing selected content and required packages.")));
                ReviewProgress.MakeDialog(true);
                ReviewProgress.EnterProgressFrame(1.f);
                if (ReviewProgress.ShouldCancel())
                {
                    Host.NoticeError = false;
                    Host.Message = TEXT("The import was canceled. Completed content is kept. Click Import "
                                        "to continue.");
                    return;
                }
                if (!P4KadanProjectReady(Host.Message))
                {
                    Host.NoticeError = true;
                    return;
                }
                // Always rebuild through the normal planner, including references added in a new Kadan
                // version.
                if (!ReviewImport(KeepExisting))
                    return;
            }
            Host.Message = KeepExisting ? TEXT("Kadan is importing selected game content.")
                                        : TEXT("Kadan is reimporting selected game content.");
            ShowImportResult(P4KadanRunImport(EffectiveSettings(), ImportPlan, KeepExisting));
        },
        true);
}

bool FKadanImportPage::ReviewImport(bool KeepExisting, bool Quiet)
{
    if (!Host.UseExtractedPackages && !P4KadanSourceCatalogCurrent(SourceCatalog))
    {
        ImportSummary = TEXT("The game files or tools changed. Choose the game folder again before importing.");
        Host.Message = ImportSummary;
        Host.NoticeError = true;
        return false;
    }
    Host.WriteSettings();
    // Counting reads the selection's sources from the game's containers into the project's folder, so a nearly
    // full disk stops it here, before anything is read. Import stays off until there is room (Tick).
    const FString DiskFull = P4DiskFullMessage(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
    ProjectDiskFull = !DiskFull.IsEmpty();
    DiskCheckedAt = FPlatformTime::Seconds();
    if (ProjectDiskFull)
    {
        ImportPlan = FP4KadanImportPlan();
        ImportSummary = FString::Printf(TEXT("%d selected · not counted: the project's disk is almost full"),
            SelectedPackages.Num());
        Host.Message = DiskFull;
        Host.NoticeError = true;
        return false;
    }
    // An extracted folder was already read with real headers. Reuse that scan for the legacy
    // workflow; original-container rows require acquisition and planning
    // through the settings-aware backend before any import can run.
    ImportPlan = Host.UseExtractedPackages ? P4KadanPlanImport(ExtractedAssets, SelectedPackages, KeepExisting)
                                           : P4KadanPlanImport(EffectiveSettings(), SelectedPackages, KeepExisting);
    if (ImportPlan.Problems.ContainsByPredicate(
            [](const FString& Problem) { return Problem.Contains(TEXT("scan was cancelled")); }))
    {
        ImportSummary = TEXT("The count was canceled. Completed original sources are kept.");
        Host.Message = ImportSummary;
        Host.NoticeError = false;
        return false;
    }
    uint64 Total = 0, Free = 0;
    const bool Disk = FPlatformMisc::GetDiskTotalAndFreeSpace(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()), Total, Free);
    // What the selection's sources name and Kadan has not read yet is found during the import, with what it needs.
    const FString Later = ImportPlan.NotYetRead
        ? FString::Printf(TEXT(" so far, %d more found in the game's files on import"), ImportPlan.NotYetRead)
        : FString();
    ImportSummary = FString::Printf(TEXT("%d selected · %d more they need%s · %d already in the project · %s to read from the game%s"),
        ImportPlan.Roots, ImportPlan.Dependencies, *Later, ImportPlan.AlreadyPresent,
        *Size(KeepExisting ? ImportPlan.RemainingSourceBytes : ImportPlan.SourceBytes),
        Disk ? *(TEXT(" · ") + Size(int64(Free)) + TEXT(" free")) : TEXT(""));
    for (const FString& Problem : ImportPlan.Problems)
        UE_LOG(LogTemp, Warning, TEXT("Kadan import review: %s"), *Problem);
    for (const FString& Warning : ImportPlan.Warnings)
        UE_LOG(LogTemp, Log, TEXT("Kadan import review note: %s"), *Warning);
    ImportDetails = FString::Join(ImportPlan.Problems, TEXT("\n"));
    if (ImportPlan.Problems.Num() || !Quiet)
    {
        Host.Message = ImportPlan.Problems.Num()
            ? TEXT("Some of the selection can't be imported. The Output Log says which and why.")
            : TEXT("Ready to import.");
        Host.NoticeError = ImportPlan.Problems.Num() > 0;
    }
    return true;
}

void FKadanImportPage::ShowImportResult(const FP4KadanImportResult& Result)
{
    Host.Message = P4KadanImportCompletionMessage(Result);
    ImportDetails = FString::Join(Result.Problems, TEXT("\n"));
    for (const FString& Warning : Result.Warnings)
    {
        if (!ImportDetails.IsEmpty())
        {
            ImportDetails += TEXT("\n");
        }
        ImportDetails += Warning;
    }
    Host.NoticeError = !Result.Ok && !Result.Cancelled;
    if (Result.Problems.Contains(TEXT("Save your level changes before reimporting.")))
    {
        Host.Message = TEXT("Save your level changes before reimporting, then choose Reimport again.");
    }
    // A selected asset that stayed in the game comes first, with what kept it out.
    TArray<FString> Stayed;
    for (const FString& Package : SelectedPackages)
        if (!Result.Imported.Contains(Package) &&
            (Result.KeptAsTheGames.Contains(Package) || Result.NotImported.Contains(Package)))
        {
            const FString Why = P4KadanStayedInGameReason(Result, Package);
            Stayed.Add(FPackageName::GetShortName(Package) + TEXT(" was not imported and stays in the game.") +
                (Why.IsEmpty() ? FString() : TEXT(" ") + Why));
        }
    Stayed.Sort();
    // Game objects the editor does not show come right after: they are in the game after an export all the same.
    int32 NotShown = 0;
    for (const FString& Warning : Result.Warnings)
        if (Warning.StartsWith(P4KadanNotInEditorPrefix))
        {
            Stayed.Add(Warning);
            ++NotShown;
        }
    if (NotShown)
        Host.Message += FString::Printf(TEXT(" %d level%s keep%s some of the game's objects out of the editor; they "
                                             "stay in the game. The Output Log names them."),
            NotShown, NotShown == 1 ? TEXT("") : TEXT("s"), NotShown == 1 ? TEXT("s") : TEXT(""));
    if (Stayed.Num() > NotShown)
    {
        Host.Message += TEXT(" The Output Log lists the selected packages that stayed in the game.");
        Host.NoticeError = true;
    }
    ImportNotes.Empty();
    for (const FString& Note : Stayed)
    {
        ImportNotes.Add(MakeShared<FString>(Note));
    }
    for (const FString& Outcome : P4KadanImportOutcomes(Result))
    {
        ImportNotes.Add(MakeShared<FString>(Outcome));
    }
    TSet<FString> Noted;
    for (const FString& Asset : Result.KeptAsTheGames)
    {
        const FString Package = P4KadanNotePackage(Asset);
        if (Noted.Contains(Package))
            continue;
        Noted.Add(Package);
        ImportNotes.Add(MakeShared<FString>(TEXT("Uses the game's original asset · ") + Package +
            TEXT("\nThis asset stays in the game and cannot be edited here. "
                 "A map can keep this reference when it is exported.")));
    }
    for (const FString& Asset : Result.ReferenceOnly)
    {
        const FString Package = P4KadanNotePackage(Asset);
        if (Noted.Contains(Package))
            continue;
        Noted.Add(Package);
        ImportNotes.Add(MakeShared<FString>(TEXT("Read-only reference · ") + Package +
            TEXT(" It can't be edited here.")));
    }
    for (const FString& Asset : Result.NotImported)
    {
        const FString Package = P4KadanNotePackage(Asset);
        if (Noted.Contains(Package))
            continue;
        Noted.Add(Package);
        ImportNotes.Add(MakeShared<FString>(TEXT("Not imported · ") + Package));
    }
    for (const FString& Problem : Result.Problems)
        ImportNotes.Add(MakeShared<FString>(TEXT("Issue · ") + Problem));
    for (const FString& Warning : Result.Warnings)
        if (!Warning.StartsWith(P4KadanNotInEditorPrefix))
            ImportNotes.Add(MakeShared<FString>(TEXT("Note · ") + Warning));
    // The notes go to the Output Log, which the notice links when there are any.
    for (const auto& Note : ImportNotes)
        UE_LOG(LogTemp, Display, TEXT("Kadan import: %s"), **Note);
    InProjectCache.Empty();
    if (SourceList)
        SourceList->RequestListRefresh();
    if (ImportNotes.Num() && ImportDetails.IsEmpty())
        ImportDetails = FString::Printf(TEXT("%d import notes are in the Output Log."), ImportNotes.Num());
}
} // namespace P4KadanEditorPrivate

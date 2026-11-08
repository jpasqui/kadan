#include "P4KadanPanelModsPage.h"
#include "AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "HAL/FileManager.h"
#include "IContentBrowserSingleton.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"

namespace P4KadanEditorPrivate
{

void FKadanModsPage::FieldChanged(FString& Value)
{
    if (&Value == &ModId)
    {
        SuggestedModId.Empty();
    }
    if (&Value == &ModName && !ActiveMod && (ModId.IsEmpty() || ModId == SuggestedModId))
    {
        SuggestedModId = P4KadanSuggestedModId(ModName);
        ModId = SuggestedModId;
    }
    SettingsChanged();
}

void FKadanModsPage::SettingsChanged()
{
    ExportReady = false;
    ReviewCurrent = false;
    WantReview();
}

void FKadanModsPage::WantReview()
{
    ReviewWanted = true;
    ChangedAt = FPlatformTime::Seconds();
}

void FKadanModsPage::AssetModified(UObject* Object)
{
    if (!Object || Host.Busy || !HasReview || Object->GetOutermost() == GetTransientPackage())
    {
        return;
    }
    if (!P4KadanExportPackageIsSelected(Object->GetOutermost()->GetName(), Roots(), CreatorReview.Rows))
    {
        return;
    }
    ExportReady = false;
    ReviewCurrent = false;
    WantReview();
}

bool FKadanModsPage::CreatorPackageRoot(FString& Root, FString& Error) const
{
    if (!ActiveMod)
    {
        Error = TEXT("Choose a mod in Kadan before creating content from a game asset.");
        return false;
    }
    FP4ModNames Names;
    if (!P4ModDerivedNames(*ActiveMod, Names, Error))
        return false;
    Root = Names.PackageRoot;
    return true;
}

bool FKadanModsPage::OpenContentFolder()
{
    FString Root, Error, Directory;
    if (!CreatorPackageRoot(Root, Error))
    {
        Host.Message = Error;
        Host.NoticeError = true;
        return false;
    }
    if (!FPackageName::TryConvertLongPackageNameToFilename(Root + TEXT("/"), Directory) ||
        !IFileManager::Get().MakeDirectory(*Directory, true))
    {
        Host.Message = TEXT("Kadan could not make the mod's content folder ") + Root + TEXT(".");
        Host.NoticeError = true;
        return false;
    }
    FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().AddPath(Root);
    FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().SyncBrowserToFolders({Root});
    Host.NoticeError = false;
    Host.Message = TEXT("Your mod's content folder is ") + Root +
        TEXT(". Keep what you make for this mod in it.");
    return true;
}

FString FKadanModsPage::ExportDisabledReason() const
{
    if (!ActiveMod)
        return TEXT("Create or choose a mod before reviewing its export.");
    if (!SelectionRows.Num())
        return TEXT("Add an asset or folder to this mod before reviewing its export.");
    if (ModName != ActiveMod->Name || ModVersion != ActiveMod->Version)
        return TEXT("Save the mod's name and version before reviewing its export.");
    if (Host.ExportFolder.IsEmpty())
        return TEXT("Choose where to export to first.");
    if (ReviewCurrent && !ExportReady)
        return TEXT("Fix what What ships lists first.");
    return TEXT("Export the mod. Kadan checks it again first.");
}

void FKadanModsPage::LocatePackage(const FString& Package)
{
    TArray<FAssetData> Found;
    FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
        .Get()
        .GetAssetsByPackageName(FName(*Package), Found);
    if (Found.Num())
        FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"))
            .Get()
            .SyncBrowserToAssets(Found);
    else
    {
        Host.Message = TEXT("Kadan could not find this asset in the project. Check its path in Details.");
        ExportDetails = Package;
    }
}

FString FKadanModsPage::KnownPersistentMap(const FP4ExportSelectionRow& Row)
{
    if (Row.AssetClass != TEXT("World") || !Inventory.IsValid())
        return FString();
    if (const FString* Cached = IssueMapCache.Find(Row.Package))
        return *Cached;
    TArray<FP4ExportMapGroup> Groups;
    FString Why;
    UWorld* Open = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (P4GroupExportMaps({Row.Package}, Inventory, Open ? Open->GetOutermost()->GetName() : FString(), Groups, Why) &&
        Groups.Num() == 1 && FPackageName::DoesPackageExist(Groups[0].Map))
    {
        IssueMapCache.Add(Row.Package, Groups[0].Map);
        return Groups[0].Map;
    }
    IssueMapCache.Add(Row.Package, FString());
    return FString();
}

void FKadanModsPage::FilterShipping()
{
    // What needs fixing first, then what ships. What the game already has is only counted.
    FilteredShippingRows.Empty();
    for (int32 Pass = 0; Pass < 2; ++Pass)
        for (const auto& Row : ShippingRows)
        {
            const bool Ships = Row->Disposition == EP4ExportDisposition::CreatorPackage ||
                Row->Disposition == EP4ExportDisposition::GameEdit;
            const bool UsesGame = Row->Disposition == EP4ExportDisposition::UnchangedGame;
            if (!UsesGame && (Pass == 0) != Ships)
                FilteredShippingRows.Add(Row);
        }
    if (ShippingList)
        ShippingList->RequestListRefresh();
}

void FKadanModsPage::ReloadMods()
{
    TArray<FP4ModDescriptor> Found;
    FString Why;
    Mods.Empty();
    if (!P4ListModDescriptors(FPaths::ProjectDir(), Found, Why))
    {
        if (Host.MessagePage == 1)
        {
            Host.Message = Why;
            Host.NoticeError = true;
        }
        else
        {
            Host.PageMessages[1] = Why;
            Host.PageErrors[1] = true;
        }
    }
    for (const auto& Mod : Found)
        Mods.Add(MakeShared<FP4ModDescriptor>(Mod));
    if (ModPicker)
        ModPicker->RefreshOptions();
}

void FKadanModsPage::ApplyModSelection(TSharedPtr<FP4ModDescriptor> Mod)
{
    ActiveMod = Mod;
    if (Mod)
    {
        P4KadanRememberModId(GEditorPerProjectIni, Mod->Id);
    }
    ExportReceipt->Select(Mod ? Mod->Id : FString());
    SuggestedModId.Empty();
    ExportDetails.Empty();
    ExportIssueNext.Empty();
    IssueMapCache.Empty();
    IdentityIssue = false;
    MapIssue = false;
    ExportReady = false;
    ReviewCurrent = false;
    HasReview = false;
    ExportSummary.Empty();
    SelectionRows.Empty();
    ShippingRows.Empty();
    FilterShipping();
    if (Mod)
    {
        ModId = Mod->Id;
        ModName = Mod->Name;
        ModVersion = Mod->Version;
        TArray<FP4ExportRoot> Roots;
        FString Why;
        const bool Read = P4ReadExportRoots(FPaths::ProjectDir(), *Mod, Roots, Why);
        Host.NoticeError = !Read;
        if (!Read)
            Host.Message = Why;
        for (const auto& Root : Roots)
            SelectionRows.Add(MakeShared<FP4ExportRoot>(Root));
        FP4ModNames Names;
        if (P4ModDerivedNames(*Mod, Names, Why))
            SelectionFolder = Names.PackageRoot;
        if (Read)
            Host.Message = Mod->Name + TEXT(" is open.");
    }
    LastExportDirectory.Empty();
    RefreshRoots();
}

void FKadanModsPage::RestoreLastModSelection()
{
    const TSharedPtr<FP4ModDescriptor> Mod = P4KadanFindRememberedMod(P4KadanLastModId(GEditorPerProjectIni), Mods);
    if (Mod.IsValid())
    {
        // Restore the Mods state without replacing Import's setup notice.
        TGuardValue<FString> PreserveMessage(Host.Message, Host.Message);
        TGuardValue<bool> PreserveError(Host.NoticeError, Host.NoticeError);
        ApplyModSelection(Mod);
        Host.PageMessages[1] = Host.Message;
        Host.PageErrors[1] = Host.NoticeError;
    }
}

void FKadanModsPage::AddRoot(EP4ExportRootKind Kind, const FString& Path)
{
    Host.NoticeError = false;
    if (!ActiveMod)
    {
        Host.Message = TEXT("Create or choose a mod first.");
        Host.NoticeError = true;
        return;
    }
    for (const auto& Root : SelectionRows)
        if (Root->Kind == Kind && Root->Path == Path)
        {
            Host.Message = Path + TEXT(" is already included.");
            return;
        }
    FP4ExportRoot Value;
    Value.Kind = Kind;
    Value.Path = Path;
    FString Why;
    if (!P4ValidateExportRoots({Value}, Why))
    {
        Host.Message = Why;
        Host.NoticeError = true;
        return;
    }
    SelectionRows.Add(MakeShared<FP4ExportRoot>(Value));
    Host.Message = FPackageName::GetShortName(Path) + TEXT(" is in the mod.");
}

TArray<FP4ExportRoot> FKadanModsPage::Roots() const
{
    TArray<FP4ExportRoot> Out;
    for (const auto& Root : SelectionRows)
        Out.Add(*Root);
    return Out;
}

void FKadanModsPage::RefreshRoots()
{
    ExportReady = false;
    ReviewCurrent = false;
    ExportSummary.Empty();
    if (SelectionList)
        SelectionList->RequestListRefresh();
    FilterShipping();
    WantReview();
}

void FKadanModsPage::SaveRoots()
{
    FString Why;
    if (ActiveMod && !P4WriteExportRoots(FPaths::ProjectDir(), *ActiveMod, Roots(), Why))
    {
        Host.Message = Why;
        Host.NoticeError = true;
    }
    RefreshRoots();
}

FP4ExportModRequest FKadanModsPage::ExportRequest() const
{
    FP4ExportModRequest Request;
    if (ActiveMod)
        Request.Mod = *ActiveMod;
    Request.Roots = Roots();
    Request.Tools.Retoc = Host.Retoc;
    Request.Tools.Repak = Host.Repak;
    Request.ProjectDirectory = FPaths::ProjectDir();
    Request.GamePaksDirectory = Host.GamePaks;
    Request.OutputDirectory = Host.ExportFolder;
    Request.WorkDirectory = FPaths::ProjectIntermediateDir() / TEXT("Kadan/ExportWork");
    Request.ExtractedGameDirectory = Host.Settings.GameFolder;
    Request.Native = Native;
    return Request;
}

void FKadanModsPage::ReviewExport(bool PublishReadyStatus)
{
    Host.WriteSettings();
    ExportReady = false;
    Host.NoticeError = true;
    ExportDetails.Empty();
    ExportIssueNext.Empty();
    IssueMapCache.Empty();
    IdentityIssue = false;
    MapIssue = false;
    ShippingRows.Empty();
    HasReview = true;
    ReviewCurrent = true;
    FilterShipping();
    if (!ActiveMod)
    {
        ExportSummary = TEXT("Choose a mod first.");
        Host.Message = ExportSummary;
        return;
    }
    if (ModName != ActiveMod->Name || ModVersion != ActiveMod->Version)
    {
        ExportSummary = TEXT("Save the mod before reviewing its export.");
        Host.Message = ExportSummary;
        ExportDetails = TEXT("The current name or version differs from the saved mod descriptor.");
        ExportIssueNext = TEXT("Open Mod details and save the name and version, then review the export again.");
        IdentityIssue = true;
        return;
    }
    FP4ExportModRequest Request = ExportRequest();
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    FP4KadanModReview Review;
    FString Why;
    if (!P4KadanReviewMod(Request, World, Review, Why))
    {
        UE_LOG(LogTemp, Warning, TEXT("Kadan export review: %s"), *Why);
        ExportDetails = Why;
        ExportIssueNext = TEXT("Check the exact diagnostic in Details. Check the game folder and tools in "
                               "Import, then review the export again.");
        ExportSummary = TEXT("The export review could not finish.");
        Host.Message = ExportSummary;
        return;
    }
    Inventory = MoveTemp(Review.Inventory);
    ExportPreview = MoveTemp(Review.Selection);
    CreatorReview = MoveTemp(Review.Creator);
    OtherPackageClaims = MoveTemp(Request.OtherPackageClaims);
    ExportSummary = FString::Printf(TEXT("%d items ship · %d use the game · %d need attention"), CreatorReview.Ships,
        CreatorReview.UsesGame, CreatorReview.Blockers.Num());
    for (const auto& Row : CreatorReview.Rows)
        ShippingRows.Add(MakeShared<FP4ExportSelectionRow>(Row));
    MapIssue = CreatorReview.Rows.ContainsByPredicate(
        [](const FP4ExportSelectionRow& Row) { return Row.Issue == EP4ExportIssueKind::PersistentMapRequired; });
    ExportDetails = FString::Join(CreatorReview.Blockers, TEXT("\n"));
    for (const FString& Blocker : CreatorReview.Blockers)
        UE_LOG(LogTemp, Warning, TEXT("Kadan export: %s"), *Blocker);
    if (CreatorReview.Blockers.Num() &&
        !CreatorReview.Rows.ContainsByPredicate(
            [](const FP4ExportSelectionRow& Row)
            {
                return Row.Disposition == EP4ExportDisposition::Refused ||
                    Row.Disposition == EP4ExportDisposition::NeedsReplacementProof;
            }))
        ExportIssueNext = TEXT(
            "Check the listed assets and the exact diagnostic in Details, then review the export again.");
    if (MapIssue)
    {
        ExportIssueNext = TEXT(
            "Open an imported persistent game map before exporting this level. Use Import to find a map if it is missing.");
    }
    if (Host.ExportFolder.IsEmpty())
        ExportSummary += TEXT("\nChoose an output folder.");
    ExportReady = CreatorReview.CanExport() && !Host.ExportFolder.IsEmpty();
    Host.NoticeError = !ExportReady;
    if (!ExportReady)
        Host.Message = TEXT("Some of the mod can't ship yet. What ships lists it.");
    else if (PublishReadyStatus)
        Host.Message = TEXT("The mod is ready to export.");
    FilterShipping();
}

void FKadanModsPage::ExportMod()
{
    if (!ActiveMod)
    {
        Host.Message = TEXT("Choose a mod first.");
        return;
    }
    // Revalidate normally without replacing the active export's status
    // with the standalone review's success message before publishing.
    ReviewExport(false);
    if (!ExportReady)
    {
        Host.Message = TEXT("Some of the mod can't ship yet. What ships lists it.");
        return;
    }
    FP4ExportModResult Result;
    FString Why;
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    // A changed level of the open map that the mod leaves out ships nothing. Selection stays the creator's.
    const TArray<FString> Left = P4ChangedLevelsOutsideSelection(World, Inventory, CreatorReview);
    if (Left.Num())
    {
        TArray<FString> Names;
        for (const FString& Package : Left)
            Names.Add(FPackageName::GetShortName(Package));
        UE_LOG(LogTemp, Warning, TEXT("Kadan export: changed levels left out of the mod: %s"),
            *FString::Join(Names, TEXT(", ")));
        const FString Question = TEXT("These levels of the open map have changes, but they are not in this mod, so "
                                      "their changes won't ship:\n\n") +
            FString::Join(Names, TEXT("\n")) +
            TEXT("\n\nTo ship a level's changes, select it in the Content Browser and click Add selected. Export "
                 "without them?");
        if (!FApp::IsUnattended() &&
            FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(Question)) != EAppReturnType::Yes)
        {
            Host.NoticeError = true;
            Host.Message = TEXT("Export stopped. Changed levels are not in the mod: ") +
                FString::Join(Names, TEXT(", ")) + TEXT(".");
            return;
        }
    }
    const bool Finished = P4KadanExportMod(ExportRequest(), World, Result, Why);
    if (Finished && Result.Published)
    {
        ExportReceipt->Published(*ActiveMod, Result);
        LastExportDirectory = Result.Directory;
        LastExportLine = FString::Printf(TEXT("Exported %s to %s"), *FDateTime::Now().ToString(TEXT("%H:%M")),
            Host.ExportFolder == SuggestedEdenOutput ? TEXT("Eden's mod folder") : *Host.ExportFolder);
        int32 Generated = 0;
        for (const FString& Package : Result.Packages)
        {
            if (ShippingRows.ContainsByPredicate(
                    [&](const TSharedPtr<FP4ExportSelectionRow>& Row) { return Row->Package == Package; }))
                continue;
            auto Row = MakeShared<FP4ExportSelectionRow>();
            Row->Package = Package;
            Row->Disposition = Result.RebuiltLighting.Contains(Package) ? EP4ExportDisposition::GameEdit
                                                                        : EP4ExportDisposition::CreatorPackage;
            Row->Reason = Result.RebuiltLighting.Contains(Package)
                ? TEXT("The map's newly rebuilt lighting is included at the game's lighting path.")
                : TEXT("This package was generated for your added actors or placement lists and is "
                       "included in the mod.");
            ShippingRows.Add(Row);
            ++Generated;
        }
        ExportSummary = FString::Printf(TEXT("%d items ship · %d use the game · %d need attention"),
            CreatorReview.Ships + Generated, CreatorReview.UsesGame, CreatorReview.Blockers.Num());
        FilterShipping();
    }
    Host.NoticeError = !Finished || !Result.Published;
    Host.Message = Finished && Result.Published ? TEXT("Exported ") + ActiveMod->Name + TEXT(".")
                                                : TEXT("Export stopped. ") +
            (Why.IsEmpty() ? TEXT("Check the export setup and unavailable assets.") : Why.Left(800));
    if (!Finished || !Result.Published)
    {
        ExportReady = false;
        ExportDetails = P4KadanExportDetails(Why, Result.Refused);
        MapIssue = !World ||
            CreatorReview.Rows.ContainsByPredicate([](const FP4ExportSelectionRow& Row)
                { return Row.Issue == EP4ExportIssueKind::PersistentMapRequired; });
        Host.Message = TEXT("The export stopped. The Output Log says why.");
        ExportIssueNext = MapIssue ? TEXT("Open an imported persistent game map before exporting this level. "
                                          "Use Import to find a map if it is missing.")
                                   : TEXT("Check the exact diagnostic in Details before exporting again.");
        if (P4ExportWasCancelled())
        {
            Host.Message = TEXT("The export was canceled. The earlier export is unchanged.");
            ExportIssueNext = TEXT("Review the export to try again when you are ready.");
        }
    }
    if (Finished && Result.Published && Result.GameRowEdits.Num())
    {
        // The game's own gameplay objects moved or removed, each named with the row the mod changes.
        for (const FP4GameRowEdit& Row : Result.GameRowEdits)
            Host.Message += FString::Printf(TEXT("\n%s is %s through row %llu of the game's placement list %s."),
                *Row.Actor, Row.Removed ? TEXT("removed") : TEXT("moved"), Row.Id,
                *FPackageName::GetShortName(Row.List));
    }
    TSet<FString> LoggedRefusals;
    if (!Why.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("Kadan export: %s"), *Why);
        LoggedRefusals.Add(Why);
    }
    for (const FString& Refusal : Result.Refused)
    {
        if (LoggedRefusals.Contains(Refusal))
        {
            continue;
        }
        UE_LOG(LogTemp, Warning, TEXT("Kadan export: %s"), *Refusal);
        LoggedRefusals.Add(Refusal);
    }
    for (const FString& Note : Result.Told)
        UE_LOG(LogTemp, Display, TEXT("Kadan export: %s"), *Note);
}
} // namespace P4KadanEditorPrivate

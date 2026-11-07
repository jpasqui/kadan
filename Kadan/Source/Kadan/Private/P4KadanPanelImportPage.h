#pragma once
#include "P4KadanPanel.h"

namespace P4KadanEditorPrivate
{
// The Import page: the game folder and project setup, the source browser and the import of selected content.
// It reads and writes the panel's saved paths and reports through the panel's notice.
class FKadanImportPage
{
  public:
    explicit FKadanImportPage(SKadanPanel &InHost);

    // Read the project setup state and start with every kind of content.
    void Prepare();

    TSharedRef<SWidget> Build();

    // Read the game folder unless it is already read and current.
    void QueueGameFolder();

    void QueueImport(bool KeepExisting);

    // Counts what the selection needs once the boxes stop changing. The panel calls it twice a second.
    void Tick();

    // Show the source rows under a search, as Find map does.
    void ShowSearch(const FString &Value);

    const FString &Details() const;

    void ClearDetails();

  private:
    SKadanPanel &Host;
    FString PreparedGameLocation;
    bool ProjectReady = false, RestartRequired = false;
    FString ProjectSetupReason;
    FString Search, PackageFolder, ImportSummary, ImportDetails;
    bool ReviewWanted = false;
    double SelectionChangedAt = 0;
    TMap<FString, bool> InProjectCache;
    TArray<TSharedPtr<FP4KadanSourceAsset>> ExtractedAssets;
    TArray<TSharedPtr<FSourceRow>> Assets, Visible;
    TSet<FString> SelectedPackages;
    FP4KadanImportPlan ImportPlan;
    FP4KadanSourceCatalog SourceCatalog;
    TSharedPtr<SListView<TSharedPtr<FSourceRow>>> SourceList;
    TArray<TSharedPtr<FSourceFolder>> FolderRoots;
    TSharedPtr<STreeView<TSharedPtr<FSourceFolder>>> FolderTree;
    TArray<TSharedPtr<FString>> ImportNotes;
    TArray<TSharedPtr<FString>> Kinds;
    TSharedPtr<FString> SelectedKind;
    TSharedPtr<SComboBox<TSharedPtr<FString>>> KindPicker;
    TWeakPtr<SWindow> RepairWindow;

    void RefreshProjectSetup();

    void ChooseTool(bool IsRepak);

    bool InProject(const FString &Package);

    // Whether any selected package is already in the project, which makes Import a reimport.
    bool ReimportSelected();

    void InvalidateImportReview();

    void PrepareGameFolder();

    // Rewrites the project's collision, navigation and world settings from the game. Only an
    // explicit click runs it, so a creator's own config edits are never replaced unasked.
    void SetUpProject();

    void FieldChanged(FString &Value);

    static FString KindLabel(const FString &Kind);

    FP4KadanImportSettings EffectiveSettings() const;

    bool ReadSource();

    TSharedRef<SWidget> PathField(const FString &Name, FString &Value, bool Folder);

    TSharedRef<SWidget> ProjectSetupStrip();


    TSharedRef<SWidget> MissingTools();

    TSharedRef<SWidget> FilterRow();

    TSharedRef<SWidget> SourceBrowser();

    TSharedRef<SWidget> ImportActions();

    void RebuildFolders();

    void Filter();

    void RequestReimport();

    bool ReviewImport(bool KeepExisting, bool Quiet = false);

    void ShowImportResult(const FP4KadanImportResult &Result);
};
} // namespace P4KadanEditorPrivate

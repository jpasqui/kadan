#pragma once
#include "P4KadanPanel.h"

namespace P4KadanEditorPrivate
{
// The Mods page: the mod picker and identity form, the selected roots, the export setup, the reviewed
// shipping list and the review and export actions. It reports through the panel's notice.
class FKadanModsPage
{
  public:
    explicit FKadanModsPage(SKadanPanel &InHost);

    // Point the receipt at this project and look for Eden's mod folder.
    void Prepare();

    void ReloadMods();

    void RestoreLastModSelection();

    TSharedRef<SWidget> Build();

    // Reviews what ships once the content settles. The panel calls it twice a second.
    void Tick();

    // A saved setting changed, so the current export review no longer holds.
    void SettingsChanged();

    void AssetModified(UObject *Object);

    bool CreatorPackageRoot(FString &Root, FString &Error) const;

    TSharedPtr<FP4ModDescriptor> CurrentMod() const;

    bool ReviewOutOfDate() const;

    const FString &Details() const;

    void ScrollToStart();

    FP4KadanDraftSaveResult SaveModIdentity();

    void ContinueModChange(const FString &Id, bool NewDraft);

  private:
    SKadanPanel &Host;
    FString SuggestedEdenOutput;
    FString ExportSummary, ExportDetails, ExportIssueNext;
    TMap<FString, FString> IssueMapCache;
    bool IdentityIssue = false, MapIssue = false;
    FString ModId, ModName, ModVersion = TEXT("1.0.0"), SelectionFolder;
    FString SuggestedModId;
    TArray<TSharedPtr<FP4ModDescriptor>> Mods;
    TSharedPtr<FP4ModDescriptor> ActiveMod;
    TSharedPtr<SComboBox<TSharedPtr<FP4ModDescriptor>>> ModPicker;
    TArray<TSharedPtr<FP4ExportRoot>> SelectionRows;
    TSharedPtr<SListView<TSharedPtr<FP4ExportRoot>>> SelectionList;
    FP4ExportGameInventory Inventory;
    FP4ExportSelectionPreview ExportPreview;
    FP4KadanCreatorReview CreatorReview;
    TArray<TSharedPtr<FP4ExportSelectionRow>> ShippingRows;
    TSharedPtr<SListView<TSharedPtr<FP4ExportSelectionRow>>> ShippingList;
    TSharedRef<FP4KadanReceiptState> ExportReceipt = MakeShared<FP4KadanReceiptState>();
    TMap<FName, FString> OtherPackageClaims;
    bool ExportReady = false, ReviewCurrent = false, HasReview = false;
    // Ship edited game levels whole instead of as lists of their changes.
    bool Native = false;
    bool ReviewWanted = false;
    double ChangedAt = 0;
    FString LastExportDirectory, LastExportLine;
    TWeakPtr<SWindow> IdentityWindow;
    TArray<TSharedPtr<FP4ExportSelectionRow>> FilteredShippingRows;
    bool RestoringModPicker = false;
    TWeakPtr<SWindow> ModDraftWindow;

    void FieldChanged(FString &Value);

    TSharedRef<SWidget> TextField(const FString &Name, FString &Value);

    TSharedRef<SWidget> PathField(const FString &Name, FString &Value, bool Folder);

    // The identity form, selected roots and export setup above the shipping list.
    TSharedRef<SWidget> ModRow();

    void EditIdentity(bool New);

    void AddSelected();

    TSharedRef<SWidget> RootList();

    TSharedRef<SWidget> ExportTarget();

    void ChooseExportFolder();

    FString ShippingSummary() const;

    void WantReview();

    FString ExportDisabledReason() const;


    void LocatePackage(const FString &Package);

    FString KnownPersistentMap(const FP4ExportSelectionRow &Row);

    TSharedRef<ITableRow> ShippingRow(TSharedPtr<FP4ExportSelectionRow> Row, const TSharedRef<STableViewBase> &Owner);


    void FilterShipping();

    void ApplyModSelection(TSharedPtr<FP4ModDescriptor> Mod);

    void AddRoot(EP4ExportRootKind Kind, const FString &Path);

    TArray<FP4ExportRoot> Roots() const;

    void RefreshRoots();

    void SaveRoots();

    FP4ExportModRequest ExportRequest() const;

    void ReviewExport(bool PublishReadyStatus = true);

    void ExportMod();

    bool HasUnsavedModIdentity() const;

    void RestoreModPicker();

    void StartModDraft();

    void RequestModChange(TSharedPtr<FP4ModDescriptor> Mod, bool NewDraft = false);
};
} // namespace P4KadanEditorPrivate

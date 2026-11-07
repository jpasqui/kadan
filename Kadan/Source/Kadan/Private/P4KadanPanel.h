#pragma once
#include "P4KadanEditor.h"
#include "P4KadanImport.h"
#include "P4KadanProjectSetup.h"
#include "P4KadanAssetPolicy.h"
#include "P4KadanReadOnly.h"
#include "P4KadanCreatorReview.h"
#include "P4ModDescriptor.h"
#include "P4ExportSelection.h"
#include "P4ExportContainer.h"
#include "P4ExportDriver.h"
#include "P4KadanExportIssues.h"
#include "P4KadanExportInvalidation.h"
#include "P4KadanModIdentity.h"
#include "P4KadanLastMod.h"
#include "P4KadanEdenOutput.h"
#include "P4KadanShippingRow.h"
#include "P4KadanExportProgress.h"
#include "P4ExportProgress.h"
#include "P4KadanExportReceipt.h"
#include "P4KadanModDraftChoice.h"
#include "P4KadanImportRepair.h"
#include "P4KadanHelp.h"
#include "P4KadanPathPicker.h"
#include "P4ExportMaps.h"
#include "FileHelpers.h"
#include "HAL/PlatformApplicationMisc.h"
#include "P4KadanSaveMod.h"
#include "P4KadanPathDefaults.h"
#include "P4KadanSourceCatalog.h"
#include "P4KadanGroundPanel.h"
#include "P4CreatorSurfaceAdvice.h"
#include "Engine/StaticMesh.h"
#include "P4GameConnection.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Editor.h"
#include "Containers/Ticker.h"
#include "UnrealEdMisc.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBox.h"
#include "HAL/FileManager.h"
#include "LevelEditor.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/Images/SThrobber.h"
#include "UObject/UObjectGlobals.h"

namespace P4KadanEditorPrivate
{
using FSourceRow = FP4KadanBrowserRow;
struct FSourceFolder
{
    FString Path, Name;
    int32 Assets = 0;
    TArray<TSharedPtr<FSourceFolder>> Children;
};
extern const FName TabName;
extern const FLinearColor ButtonTint;
extern const FLinearColor PrimaryButtonTint;
extern TSharedPtr<FExtender> MenuExtender;
extern bool Registered;
FText Label(const FString &Value);
FString Size(int64 Bytes);
FString Disposition(EP4ExportDisposition Value);
class FKadanImportPage;
class FKadanModsPage;

// The Kadan panel: page buttons, the notice with its Details, the map layer controls and the shown page.
// Each page is its own class. The panel holds what pages share: the notice, the busy state that a queued
// operation sets, the saved game and tool paths, and the button and field widgets.
class SKadanPanel : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SKadanPanel)
    {
    }
    SLATE_END_ARGS()

    SKadanPanel();

    ~SKadanPanel();

    void ShowPage(int32 Number);

    void Construct(const FArguments &);

    bool CreatorPackageRoot(FString &Root, FString &Error) const;

    FKadanImportPage &Import();

    FKadanModsPage &Mods();

    TSharedRef<SKadanPanel> Self();

    // Run Operation after the busy state is painted, behind a progress dialog unless the backend owns one.
    void QueueOperation(const FString &Status, TFunction<void()> Operation, bool BackendOwnsProgress = false);

    TSharedRef<SWidget> Heading(const FString &Text);

    TSharedRef<SWidget> Action(const FString &Text,
                               TFunction<void()> Click,
                               bool Primary = false,
                               TFunction<bool()> Enabled = nullptr,
                               const FString &Tooltip = FString());

    // A labeled text field bound to Value. OnChanged runs after each edit. OnCommitted, when set, runs when
    // the field is committed other than by clearing. Input, when set, receives the editable box.
    TSharedRef<SWidget> TextField(const FString &Name,
                                  FString &Value,
                                  TFunction<void(FString &)> OnChanged,
                                  TFunction<void()> OnCommitted = nullptr,
                                  TSharedPtr<SEditableTextBox> *Input = nullptr);

    // A text field with a Browse button. A browsed path runs OnChanged, saves the settings, then OnCommitted.
    TSharedRef<SWidget> PathField(const FString &Name,
                                  FString &Value,
                                  bool Folder,
                                  TFunction<void(FString &)> OnChanged,
                                  TFunction<void()> OnCommitted = nullptr,
                                  TSharedPtr<SEditableTextBox> *Input = nullptr);

    // A saved setting changed, so the current export review no longer holds.
    void SettingsChanged();

    void WriteSettings();

    int32 Page = 0;
    // The page whose notice Message and NoticeError hold.
    int32 MessagePage = 0;
    FString Message = TEXT("Import brings in the game's maps and assets. Mods is where you export your changes.");
    bool NoticeError = false, Busy = false;
    FString PageMessages[3] = {TEXT("Choose your game folder to browse maps and assets."),
                               TEXT("A mod is what Kadan exports for the game."), FString()};
    bool PageErrors[3] = {};

    // Saved per project: the game and tool paths, the output folder and the source mode.
    FP4KadanImportSettings Settings;
    FString GamePaks, Retoc, Repak, ExportFolder;
    FString GameLocation;
    bool UseExtractedPackages = false;

  private:
    TUniquePtr<FKadanImportPage> ImportPage;
    TUniquePtr<FKadanModsPage> ModsPage;
    FDelegateHandle ModifiedHandle;

    FString ShownMessage() const;

    FString NoticeDetails() const;

    bool ShownError() const;

    void SettleNotice();

    TSharedRef<SWidget> NoticeArea();

    TSharedRef<SWidget> PageButton(const FString &Text, int32 Number);

    void AssetModified(UObject *Object);

    EActiveTimerReturnType TickPages(double, float);

    bool HasNoticeDetails() const;

    void TransferSettings(bool Write);

    void ReadSettings();
};
extern TWeakPtr<SKadanPanel> CurrentPanel;
} // namespace P4KadanEditorPrivate

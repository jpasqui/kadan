#pragma once
#include "P4KadanCreatorFlow.h"

#if P4_MAPKIT_TESTS
namespace P4KadanCreatorFlow
{
// Owned assets that several stages build, save or select.
class FFlowAssets
{
  public:
    explicit FFlowAssets(FFlow &InFlow);

    // The two fixture mods' creator textures, after recovering only their own metadata.
    bool Fixture();

    bool SaveIssueFixture(UObject *Asset);

    // A cube texture, a material that needs it and a level without a persistent game map.
    bool IssueFixture();

    bool SelectFixture(const FString &Package);

    // Open the copied game map, place an owned cube with an imported material and save the map.
    bool MapProgressFixture(bool ExpectLightingWarning = true);

  private:
    FFlow &Flow;
};

// Steps and checks on the two fixture mods' identity and publication.
class FFlowModSteps
{
  public:
    explicit FFlowModSteps(FFlow &InFlow);

    void AddNewMod(const FP4ModDescriptor &Mod);

    void ReopenChecks();

    // Independently inspect the second mod's publication. With CheckPanelLists the panel's shipping
    // and published-file lists must also match it.
    bool VerifyExport(bool CheckPanelLists = false);

    bool VerifyImportedTexture();

  private:
    FFlow &Flow;
};

// Watches the export progress window on Slate ticks and captures each stage it shows once.
class FExportProgressCapture
{
  public:
    explicit FExportProgressCapture(FFlow &InFlow);

    ~FExportProgressCapture();

    bool ObserveExportProgress(bool CancelAtCooking = false, const FString &Prefix = FString());

    void StopProgressCapture();

    bool SaveProgressTimeline(const FString &Name);

    TSet<FString> ProgressStages;
    TArray<TSharedPtr<FJsonValue>> ProgressCaptures;
    bool ExportCancelClicked = false;

  private:
    FFlow &Flow;
    FDelegateHandle ProgressTickHandle;
    bool CancelExportAtCooking = false;
    bool CapturingProgress = false;
};

// The real Material Details panel of the material whose editor is open.
class FMaterialDetails
{
  public:
    explicit FMaterialDetails(FFlow &InFlow);

    bool OpenMaterialDetails(UMaterialInterface *Material);

    void CloseMaterialDetails(UMaterialInterface *Material);

    TSharedPtr<SWidget> MaterialTagged(const FName Name);

    bool MaterialPrepare(const TSharedPtr<SWidget> &Widget);

    bool MaterialSearch(const FString &Value);

    // Capture the Details panel, or the given widget.
    bool MaterialCapture(const FString &Name, TSharedPtr<SWidget> Content = nullptr);

    TWeakPtr<SDockTab> DetailsTab;

  private:
    FFlow &Flow;
};

// The saved original actor move and creator mesh that a reimport must keep.
class FUpgradeSnapshot
{
  public:
    explicit FUpgradeSnapshot(FFlow &InFlow);

    // Compare the open map with the snapshot. With Reopen, read the snapshot and open its map first.
    bool UpgradeContent(bool Reopen, bool AnalyzeMove = true);

    const FString UpgradeTexture = TEXT("/Game/Carrot4/Characters/Buddy/Happy/T_chr_Happy_00_T00_D");
    FString UpgradeActorPath;
    FString UpgradeMeshFile;
    FString UpgradeMeshHash;
    FTransform UpgradeActorTransform;

  private:
    FFlow &Flow;
};
} // namespace P4KadanCreatorFlow
#endif

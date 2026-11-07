#pragma once
#include "P4KadanCreatorFlowStageList.h"

#if P4_MAPKIT_TESTS
namespace P4KadanCreatorFlow
{
// Pages, MapLayersBefore, MapLayers and MapLayersDistinct: page switching inside one panel, and the Day,
// Night and Olimar layer controls of an imported game map. The map fixture is in the Layers file; the
// genuine source streaming graph of MapLayersDistinct is in the LayerSource file.
class FLayersStage final : public FFlowStage
{
  public:
    using FFlowStage::FFlowStage;

    void AddSteps() override;

  private:
    // Map fixture
    bool SamePanelPage();

    bool LayerMapFixture();

    bool LayerDataPreserved();

    // Owned test fixture. Every world comes from an actual converted game package.
    // Actors stay in the game so this proves editor layer visibility only.
    bool RequireLayer(bool Value, const FString &Why);

    bool LinkLayerReference(const FString &Package);

    bool LayerReferencesPreserved();

    bool RebindCopiedLayerSources();

    bool PrepareDistinctLayerFixture();

    // Import one game-backed world, first importing each missing required reference it names.
    bool ImportLayerWorld(FP4KadanSourceCatalog &Catalog,
                          FP4KadanContainerIndex &Containers,
                          const FString &Sources,
                          const FString &Header,
                          FP4OriginalLevelImportResult &Result,
                          FString &Error);

    bool CheckDistinctLayerVisibility(const FString &Caption);

    TWeakPtr<SDockTab> SwitcherTab;
    FString LayerOriginalState;
    TMap<TWeakObjectPtr<ULevelStreaming>, TPair<bool, bool>> LayerRuntimeFlags;
    TMap<FString, FString> LayerReferenceHashes;
};
} // namespace P4KadanCreatorFlow
#endif

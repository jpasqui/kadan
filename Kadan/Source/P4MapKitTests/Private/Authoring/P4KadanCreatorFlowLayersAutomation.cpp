#include "P4KadanCreatorFlowLayers.h"

#if P4_MAPKIT_TESTS
namespace P4KadanCreatorFlow
{

bool FLayersStage::SamePanelPage()
{
    return Flow.Expect(Flow.Tab() == SwitcherTab.Pin() && !FSlateApplication::Get().AnyMenusVisible(),
                       TEXT("A page change replaced the Kadan panel or left a drop-down menu open"));
}

bool FLayersStage::LayerMapFixture()
{
    const FString Map = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_UX_MAP"));
    if (!FEditorFileUtils::LoadMap(
            FPackageName::LongPackageNameToFilename(Map, FPackageName::GetMapPackageExtension())))
    {
        return Flow.Expect(false, TEXT("The retained map could not open for the layer controls"));
    }
    UWorld *World = GEditor->GetEditorWorldContext().World();
    const auto Changes = P4AnalyzeOriginalLevelChanges(World);
    if (!Flow.Expect(Changes.IsValid(), TEXT("The actual imported map comparison is unavailable")))
    {
        return false;
    }
    LayerOriginalState = Changes.State;
    LayerRuntimeFlags.Empty();
    TArray<UWorld *> Pending = {World};
    TSet<UWorld *> Seen;
    while (Pending.Num())
    {
        UWorld *LayerWorld = Pending.Pop(false);
        if (!LayerWorld || Seen.Contains(LayerWorld))
        {
            continue;
        }
        Seen.Add(LayerWorld);
        for (ULevelStreaming *Streaming : LayerWorld->GetStreamingLevels())
        {
            if (!Streaming)
            {
                continue;
            }
            LayerRuntimeFlags.Add(TWeakObjectPtr<ULevelStreaming>(Streaming),
                                  TPair<bool, bool>(Streaming->ShouldBeLoaded(), Streaming->GetShouldBeVisibleFlag()));
            const FString Package = Streaming->GetWorldAssetPackageName();
            Pending.Add(LoadObject<UWorld>(nullptr, *(Package + TEXT(".") + FPackageName::GetShortName(Package))));
        }
    }
    return true;
}

bool FLayersStage::LayerDataPreserved()
{
    for (const auto &Pair : LayerRuntimeFlags)
    {
        const auto *Streaming = Pair.Key.Get();
        if (!Flow.Expect(Streaming && Streaming->ShouldBeLoaded() == Pair.Value.Key &&
                             Streaming->GetShouldBeVisibleFlag() == Pair.Value.Value,
                         TEXT("Changing editor layer visibility changed a saved runtime streaming flag")))
        {
            return false;
        }
    }
    const auto Changes = P4AnalyzeOriginalLevelChanges(GEditor->GetEditorWorldContext().World());
    return Flow.Expect(Changes.IsValid() && Changes.State == LayerOriginalState,
                       TEXT("Changing the editor layer set changed the map's export state"));
}

TUniquePtr<FFlowStage> MakeLayersStage(FFlow &Flow)
{
    return MakeUnique<FLayersStage>(Flow);
}
} // namespace P4KadanCreatorFlow
#endif

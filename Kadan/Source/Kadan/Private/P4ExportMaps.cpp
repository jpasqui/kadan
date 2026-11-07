#include "P4ExportMaps.h"
#include "P4GameNames.h"
#include "P4GamePaths.h"
#include "P4OriginalRecord.h"
#include "P4WorldExportGraph.h"
#include "AssetRegistryModule.h"
#include "Engine/World.h"
#include "Engine/LevelStreaming.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/StrongObjectPtr.h"

bool P4GroupExportMaps(const TArray<FString>& EditedLevels, const FP4ExportGameInventory& Inventory,
    const FString& OpenMap, TArray<FP4ExportMapGroup>& Out, FString& Error, bool* MissingPersistentMap)
{
    if (MissingPersistentMap)
    {
        *MissingPersistentMap = false;
    }
    if (!EditedLevels.Num())
    {
        Out.Reset();
        return true;
    }
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    TArray<FString> Pending = EditedLevels;
    if (!OpenMap.IsEmpty())
        Pending.AddUnique(OpenMap);
    TSet<FName> Seen;
    TArray<FP4WorldStreamingGraphResult> Graphs;
    TArray<TStrongObjectPtr<UWorld>> Held;
    for (int32 At = 0; At < Pending.Num(); ++At)
    {
        const FString Package = Pending[At];
        if (Seen.Contains(FName(*Package)))
            continue;
        if (Seen.Num() >= 4096)
        {
            Error = TEXT("The selected maps have too many streaming references.");
            return false;
        }
        Seen.Add(FName(*Package));
        if (!Inventory.Contains(FName(*Package)))
            continue;
        TArray<FAssetData> Assets;
        Registry.GetAssetsByPackageName(FName(*Package), Assets);
        if (!Assets.ContainsByPredicate(
                [](const FAssetData& Asset) { return Asset.AssetClass == FName(TEXT("World")); }))
            continue;
        const FString Path = P4ObjectPathOf(Package);
        UWorld* World = LoadObject<UWorld>(nullptr, *Path);
        if (!World || !P4QueryOriginalAsset(World).IsOriginal())
            continue;
        Held.Emplace(World);
        TArray<FName> Referencers;
        Registry.GetReferencers(FName(*Package), Referencers, UE::AssetRegistry::EDependencyCategory::Package);
        for (FName Referencer : Referencers)
            if (!Seen.Contains(Referencer))
                Pending.AddUnique(Referencer.ToString());
        if (World->GetStreamingLevels().Num() || Package.Equals(OpenMap, ESearchCase::IgnoreCase))
        {
            auto Graph = P4InspectWorldStreamingGraph(World, TEXT(P4_MODS_FOLDER "/ExportMapInspection"));
            if (!Graph.IsValid())
            {
                Error = Package + TEXT(" cannot be inspected. ") + Graph.ErrorDetail;
                return false;
            }
            Graphs.Add(MoveTemp(Graph));
        }
    }
    return P4GroupExportMapGraphs(EditedLevels, Graphs, Out, Error, MissingPersistentMap);
}

bool P4GroupExportMapGraphs(const TArray<FString>& EditedLevels, const TArray<FP4WorldStreamingGraphResult>& Graphs,
    TArray<FP4ExportMapGroup>& Out, FString& Error, bool* MissingPersistentMap)
{
    if (MissingPersistentMap)
    {
        *MissingPersistentMap = false;
    }
    TArray<FP4ExportMapGroup> Groups;
    TSet<FName> Sources;
    for (const auto& Graph : Graphs)
    {
        if (!Graph.IsValid() || !Graph.Nodes.Contains(Graph.SourcePackage) ||
            Sources.Contains(FName(*Graph.SourcePackage)))
        {
            Error = TEXT("The original map streaming graphs are incomplete or duplicated.");
            return false;
        }
        Sources.Add(FName(*Graph.SourcePackage));
    }
    for (const FString& Level : EditedLevels)
    {
        TArray<FString> Owners;
        for (const auto& Graph : Graphs)
        {
            if (!Graph.Nodes.Contains(Level))
                continue;
            // Nested streaming worlds belong to their outermost persistent map.
            if (Graphs.ContainsByPredicate([&](const FP4WorldStreamingGraphResult& Other)
                    { return Other.SourcePackage != Graph.SourcePackage && Other.Nodes.Contains(Graph.SourcePackage); }))
                continue;
            Owners.Add(Graph.SourcePackage);
        }
        if (Owners.Num() != 1)
        {
            if (MissingPersistentMap)
            {
                *MissingPersistentMap = Owners.Num() == 0;
            }
            Error = Owners.Num() ? Level + TEXT(" belongs to several game maps. Select one map's levels separately.")
                                 : Level + TEXT(" has no imported persistent map. Import its map before exporting.");
            return false;
        }
        FP4ExportMapGroup* Group = Groups.FindByPredicate(
            [&](const FP4ExportMapGroup& One) { return One.Map == Owners[0]; });
        if (!Group)
        {
            FP4ExportMapGroup Made;
            Made.Map = Owners[0];
            Groups.Add(MoveTemp(Made));
            Group = &Groups.Last();
        }
        Group->Levels.AddUnique(Level);
    }
    Groups.Sort([](const FP4ExportMapGroup& A, const FP4ExportMapGroup& B) { return A.Map < B.Map; });
    for (auto& Group : Groups)
        Group.Levels.Sort();
    Out = MoveTemp(Groups);
    Error.Reset();
    return true;
}

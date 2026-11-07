#include "P4KadanCreatorReview.h"
#include "P4Common.h"
#include "P4GamePaths.h"
#include "P4ExportDriver.h"
#include "P4GameReplacement.h"
#include "P4AddedContent.h"
#include "P4CreatorReferencePreflight.h"
#include "P4NativeCustomCompilerBridge.h"
#include "P4ExportMaps.h"
#include "P4LevelChanges.h"
#include "P4AuthoringRows.h"
#include "P4PlacementExport.h"
#include "P4ExportNavigation.h"
#include "P4TableExport.h"
#include "Engine/DataTable.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonSerializer.h"

FP4KadanCreatorReview P4ReviewCreatorSelection(const FP4ModDescriptor& Mod, const FP4ExportSelectionPreview& Preview,
    UWorld* OpenMap, const FP4ExportGameInventory* Inventory, const TMap<FName, FString>* Claims,
    const FString& GameFolder)
{
    FP4KadanCreatorReview Result;
    Result.Rows = Preview.Rows;
    Result.Blockers = Preview.Refused;
    const TMap<FName, FString> EmptyClaims;
    const TMap<FName, FString>& PackageClaims = Claims ? *Claims : EmptyClaims;
    FP4CreatorReferenceInputs CreatorReferences;
    for (const FP4ExportSelectionRow& Row : Preview.Rows)
        if (Row.Disposition == EP4ExportDisposition::CreatorPackage)
        {
            const UP4AddedContent* Definition = Cast<UP4AddedContent>(
                LoadObject<UObject>(nullptr, *P4ObjectPathOf(Row.Package)));
            if (!Definition)
                CreatorReferences.Packages.AddUnique(Row.Package);
        }
    TArray<FP4ExportMapGroup> Groups;
    FString MapError;
    bool MissingPersistentMap = false;
    if (Inventory && Preview.GameEditCandidates.Num())
        P4GroupExportMaps(Preview.GameEditCandidates, *Inventory,
            OpenMap ? OpenMap->GetOutermost()->GetName() : FString(), Groups, MapError, &MissingPersistentMap);
    TSharedPtr<FJsonObject> MapGraph;
    if (!Inventory && OpenMap && Preview.GameEditCandidates.Num())
    {
        FP4ModNames Names;
        FString Error;
        if (P4ModDerivedNames(Mod, Names, Error))
            P4ParseJson(
                UP4AuthoringTools::NativeWorldStreamingGraph(OpenMap, Names.PackageRoot + TEXT("/ExportValidation")),
                MapGraph);
    }
    for (FP4ExportSelectionRow& Row : Result.Rows)
    {
        if (Row.Disposition == EP4ExportDisposition::UnchangedGame)
        {
            ++Result.UsesGame;
            Row.Reason = TEXT("This is already in the game, so no game data is included.");
            continue;
        }
        if (Row.Disposition != EP4ExportDisposition::CreatorPackage &&
            Row.Disposition != EP4ExportDisposition::GameEdit)
            continue;
        FString Why;
        UObject* Asset = LoadObject<UObject>(nullptr, *P4ObjectPathOf(Row.Package));
        if (Row.Disposition == EP4ExportDisposition::CreatorPackage)
        {
            const FP4ExportAvailability Availability = P4ExportAssetAvailability(Asset, Mod);
            if (Availability.IsAvailable() && P4IsGameReplacement(Asset))
                Row.Reason = Availability.Reason;
            else if (Availability.Route == EP4ExportAssetRoute::CreatorTexture)
                Row.Reason = TEXT("Your texture is included.");
            else if (Availability.Route == EP4ExportAssetRoute::CreatorStaticMesh)
                Row.Reason = TEXT("Your mesh and its collision are included.");
            else if (Availability.Route == EP4ExportAssetRoute::CreatorMaterialInstance)
                Row.Reason = TEXT("Your material instance is included.");
            else if (Availability.Route == EP4ExportAssetRoute::CreatorMaterial)
                Row.Reason = TEXT("Your material and its shader archive are included.");
            else if (Availability.Route == EP4ExportAssetRoute::CreatorLevel)
            {
                if (Inventory && (!OpenMap || !Inventory->Contains(FName(*OpenMap->GetOutermost()->GetName()))))
                {
                    Row.Issue = EP4ExportIssueKind::PersistentMapRequired;
                    Why = TEXT("Open an imported persistent game map to load your level and its script.");
                }
                else
                {
                    Row.Reason = TEXT("Your level and its script are included.");
                }
            }
            else if (Availability.Route == EP4ExportAssetRoute::CreatorLighting)
                Row.Reason = Availability.Reason;
            else if (Availability.Route == EP4ExportAssetRoute::CreatorSound)
                Row.Reason = TEXT("Your sound is included in the mod's sound bank.");
            else if (Availability.Route == EP4ExportAssetRoute::CreatorSkinned)
                Row.Reason = TEXT("Your skeletal asset is included.");
            else if (Availability.Route == EP4ExportAssetRoute::CreatorBlueprint)
                Row.Reason = TEXT("Your new Blueprint child class is included.");
            else if (Availability.Route == EP4ExportAssetRoute::CreatorContent)
                Row.Reason = TEXT("Your new treasure or creature is declared in the manifest.");
            else
                Why = Availability.Reason;
        }
        else if (Preview.TableEditCandidates.Contains(Row.Package))
        {
            TSharedPtr<FJsonObject> Changes;
            if (P4ExportTableChanges(Cast<UDataTable>(Asset), Mod.Id, Changes, Why))
            {
                if (Changes->Values.Num())
                    Row.Reason = TEXT("Your table changes are written in the mod's manifest.");
                else
                {
                    Row.Disposition = EP4ExportDisposition::UnchangedGame;
                    ++Result.UsesGame;
                    continue;
                }
            }
        }
        else
        {
            UWorld* World = Cast<UWorld>(Asset);
            const TSharedPtr<FJsonObject>* Graph = nullptr;
            if (Inventory)
            {
                if (!MapError.IsEmpty())
                {
                    if (MissingPersistentMap)
                    {
                        Row.Issue = EP4ExportIssueKind::PersistentMapRequired;
                    }
                    Why = MapError;
                }
                else if (!Groups.ContainsByPredicate(
                             [&](const FP4ExportMapGroup& Group) { return Group.Levels.Contains(Row.Package); }))
                {
                    Row.Issue = EP4ExportIssueKind::PersistentMapRequired;
                    Why = TEXT("Import this level's persistent game map before exporting.");
                }
            }
            else if (!OpenMap)
            {
                Row.Issue = EP4ExportIssueKind::PersistentMapRequired;
                Why = TEXT("Open this edit's original persistent map before exporting.");
            }
            else if (!MapGraph.IsValid() || !MapGraph->TryGetObjectField(TEXT("graph"), Graph) ||
                !(*Graph)->HasField(Row.Package))
            {
                Row.Issue = EP4ExportIssueKind::PersistentMapRequired;
                Why = TEXT("This level is outside the open map. Open its original persistent map.");
            }
            if (Why.IsEmpty() && !World)
                Why = TEXT("The selected level cannot be opened.");
            if (Why.IsEmpty())
            {
                const FP4LevelChangesResult Changes = P4AnalyzeOriginalLevelChanges(World);
                FP4CreatorReferenceInputs LevelReferences;
                if (P4InspectLevelCreatorReferences(World, Changes, LevelReferences, Why))
                {
                    TArray<FString> LevelPackages;
                    const bool Ready = P4PreflightCreatorReferences(
                        Mod, LevelReferences, Inventory, PackageClaims, true, LevelPackages, Why);
                    CreatorReferences.Packages.Append(LevelPackages);
                    CreatorReferences.AddedActors.Append(LevelReferences.AddedActors);
                    CreatorReferences.AssetValues.Append(LevelReferences.AssetValues);
                    if (Ready)
                        Why.Reset();
                }
                int32 Placed = 0;
                for (const FString& Name : Changes.Added)
                    Placed += P4IsGameplayActor(FindObject<AActor>(World->PersistentLevel, *Name)) ? 1 : 0;
                TArray<FP4GameRowEdit> RowEdits;
                bool Relight = false;
                if (Why.IsEmpty())
                    P4ExportLevelRowEdits(World, Changes, GameFolder, RowEdits, Relight, Why);
                if (Why.IsEmpty() && Relight && Inventory)
                {
                    const FP4ExportMapGroup* LightingGroup = Groups.FindByPredicate(
                        [&](const FP4ExportMapGroup& Candidate) { return Candidate.Levels.Contains(Row.Package); });
                    if (LightingGroup && (!OpenMap || OpenMap->GetOutermost()->GetName() != LightingGroup->Map))
                    {
                        Why = TEXT("Open the original map in the editor before exporting its lighting changes.");
                    }
                }
                // An edit that changes where Pikmin walk has its navigation built during export. What would
                // stop that build is shown here, before export.
                FString NavigationWhy;
                bool Navigation = P4EditChangesNavigation(Changes, NavigationWhy);
                for (const FString& Name : Changes.Added)
                {
                    if (P4IsNavigationOnlyActor(FindObject<AActor>(World->PersistentLevel, *Name)))
                    {
                        Navigation = true;
                        if (NavigationWhy.IsEmpty())
                        {
                            NavigationWhy = Name;
                        }
                    }
                }
                if (Why.IsEmpty() && Navigation)
                {
                    const FP4ExportMapGroup* Group = Groups.FindByPredicate(
                        [&](const FP4ExportMapGroup& Candidate) { return Candidate.Levels.Contains(Row.Package); });
                    if (Group)
                    {
                        P4MapNavigationPackageReady(Group->Map, GameFolder, Why);
                    }
                    else if (OpenMap)
                    {
                        P4MapNavigationReady(OpenMap, GameFolder, Why);
                    }
                }
                if (Why.IsEmpty() && Placed)
                    Row.Reason = FString::Printf(
                        TEXT(
                            "%d gameplay objects you placed go into the map's placement list, so the game saves them. Other added actors go into this mod's own level, and moves, removals and changed values apply in all play modes."),
                        Placed);
                else if (Why.IsEmpty() && Changes.Added.Num())
                    Row.Reason = TEXT(
                        "Added actors go into this mod's own level, and moves, removals and changed values apply in all play modes. Added static meshes that do not move get baked lighting from the game's lights when this map is open.");
                else if (Why.IsEmpty())
                    Row.Reason = TEXT("Includes actor moves, removals and changed values in all play modes.");
                if (Why.IsEmpty() && Navigation)
                {
                    Row.Reason += TEXT(" The actor ") + NavigationWhy + TEXT(" in ") + Changes.Level +
                        TEXT(" changes where Pikmin can walk, so export rebuilds this map's navigation.");
                }
                if (Why.IsEmpty() && RowEdits.Num())
                {
                    int32 Removed = 0;
                    for (const FP4GameRowEdit& Edit : RowEdits)
                        Removed += Edit.Removed ? 1 : 0;
                    Row.Reason += FString::Printf(
                        TEXT(
                            " The game builds its own gameplay objects from its placement lists, so %d you moved and %d you removed are written as changes to their rows in those lists."),
                        RowEdits.Num() - Removed, Removed);
                }
                if (Why.IsEmpty() && Relight)
                    Row.Reason += TEXT(
                        " The map's lighting is rebuilt during export and the new lighting data is included at the game's paths.");
            }
        }
        if (!Why.IsEmpty())
        {
            Row.Disposition = EP4ExportDisposition::Refused;
            Row.Reason = Why;
            Result.Blockers.AddUnique(Row.Package + TEXT(" cannot be shipped. ") + Why);
        }
        else
            ++Result.Ships;
    }
    // Registry edges may not exist until an asset is saved. Show the same live
    // instance parents, mesh defaults and added-actor references the exporter
    // validates, without staging or compiling anything.
    TArray<FString> Packages;
    FString PreflightError;
    const bool ReferencesReady = P4PreflightCreatorReferences(
        Mod, CreatorReferences, Inventory, PackageClaims, true, Packages, PreflightError);
    if (!ReferencesReady)
        Result.Blockers.AddUnique(PreflightError);
    for (const FString& Package : Packages)
    {
        if (Result.Rows.ContainsByPredicate([&](const FP4ExportSelectionRow& Row) { return Row.Package == Package; }))
            continue;
        FP4ExportSelectionRow Row;
        Row.Package = Package;
        Row.Disposition = EP4ExportDisposition::CreatorPackage;
        UObject* Asset = LoadObject<UObject>(nullptr, *P4ObjectPathOf(Package));
        if (Asset)
            Row.AssetClass = Asset->GetClass()->GetName();
        const FP4ExportAvailability Availability = P4ExportAssetAvailability(Asset, Mod);
        if (Availability.IsAvailable())
        {
            Row.Reason = Availability.Reason;
            ++Result.Ships;
        }
        else
        {
            Row.Disposition = EP4ExportDisposition::Refused;
            Row.Reason = Availability.Reason;
            Result.Blockers.AddUnique(Package + TEXT(" cannot be shipped. ") + Row.Reason);
        }
        Result.Rows.Add(MoveTemp(Row));
    }
    if (!Result.Ships && !Result.Blockers.Num())
    {
        FString Why;
        Result.ManifestOnly = P4ValidateUnchangedMapSelection(Mod, Preview, OpenMap, Why);
        if (!Result.ManifestOnly)
            Result.Blockers.Add(Why);
        else
            for (FP4ExportSelectionRow& Row : Result.Rows)
                if (Preview.UnchangedLevelCandidates.Contains(Row.Package))
                    Row.Reason = TEXT(
                        "This original level is unchanged. Export writes the mod's manifest and includes no game packages.");
    }
    return Result;
}

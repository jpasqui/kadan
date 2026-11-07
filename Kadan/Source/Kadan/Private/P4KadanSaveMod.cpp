#include "P4KadanSaveMod.h"
#include "P4ExportProgress.h"
#include "AssetRegistryModule.h"
#include "Engine/World.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"

namespace
{
bool HoldsAssets(const FP4ExportRoot& Root)
{
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    TArray<FAssetData> Assets;
    if (Root.Kind == EP4ExportRootKind::Folder)
        Registry.GetAssetsByPath(FName(*Root.Path), Assets, true);
    else
        Registry.GetAssetsByPackageName(FName(*Root.Path), Assets);
    return Assets.Num() > 0;
}
} //namespace

bool P4KadanReviewMod(FP4ExportModRequest& Request, UWorld* OpenMap, FP4KadanModReview& Out, FString& Error)
{
    Out = FP4KadanModReview();
    if (!P4ReportExportStage(EP4ExportStage::Review, Request.Mod.Id, Error))
        return false;
    const FString Check = FPaths::ProjectSavedDir() / TEXT("Kadan/ExportCheck");
    if (!P4ReadExportGameInventory(Request.Tools, Request.GamePaksDirectory, Check, Out.Inventory, Error))
        return false;
    TArray<FP4ModDescriptor> Mods;
    if (!P4ListModDescriptors(Request.ProjectDirectory, Mods, Error))
        return false;
    Request.OtherPackageClaims.Empty();
    for (const FP4ModDescriptor& Mod : Mods)
    {
        if (Mod.Id == Request.Mod.Id)
            continue;
        TArray<FP4ExportRoot> Roots;
        FP4ExportSelectionPreview Other;
        if (!P4ReadExportRoots(Request.ProjectDirectory, Mod, Roots, Error))
        {
            Error = TEXT("The list of the mod ") + Mod.Id + TEXT(" cannot be read. ") + Error;
            return false;
        }
        // A line of another mod whose assets were moved or deleted claims nothing, and its own review says so.
        Roots.RemoveAll([&](const FP4ExportRoot& Root) { return !HoldsAssets(Root); });
        if (!Roots.Num())
            continue;
        if (!P4PreviewExportSelection(Mod, Roots, Out.Inventory, {}, Other, Error))
        {
            Error = TEXT("The list of the mod ") + Mod.Id + TEXT(" cannot be checked. ") + Error;
            return false;
        }
        for (const auto& Row : Other.Rows)
        {
            if (Row.Disposition != EP4ExportDisposition::CreatorPackage &&
                Row.Disposition != EP4ExportDisposition::GameEdit)
                continue;
            const FName Package(*Row.Package);
            if (FString* Owners = Request.OtherPackageClaims.Find(Package))
                *Owners += TEXT(", ") + Mod.Id;
            else
                Request.OtherPackageClaims.Add(Package, Mod.Id);
        }
    }
    if (!P4PreviewExportSelection(
            Request.Mod, Request.Roots, Out.Inventory, Request.OtherPackageClaims, Out.Selection, Error))
        return false;
    Out.Creator = P4ReviewCreatorSelection(Request.Mod, Out.Selection, OpenMap, &Out.Inventory,
        &Request.OtherPackageClaims, Request.ExtractedGameDirectory);
    // What only Tsuchi does, told at Review when the mod is cooked for the game without it.
    if (Request.Native)
        for (const FP4ExportSelectionRow& Row : Out.Creator.Rows)
        {
            if (Row.AssetClass == TEXT("P4AddedContent"))
                Out.Creator.Blockers.Add(Row.Package + TEXT(": a new treasure or creature kind needs Tsuchi, which gives "
                                                            "it its identity. Export this mod for Tsuchi, or remove it."));
            else if (Row.AssetClass == TEXT("SoundWave"))
                Out.Creator.Blockers.Add(Row.Package + TEXT(": your sounds play through the sound bank Tsuchi loads. "
                                                            "Export this mod for Tsuchi, or remove the sound."));
        }
    const bool CreatorLevel = Out.Creator.Rows.ContainsByPredicate([](const FP4ExportSelectionRow& Row)
        { return Row.Disposition == EP4ExportDisposition::CreatorPackage && Row.AssetClass == TEXT("World"); });
    if (Out.Selection.GameEditCandidates.Num() || Out.Creator.ManifestOnly || CreatorLevel)
    {
        // Edited levels resolve to their own maps through streaming references, so the open map is
        // only a hint for them. Creator levels and manifest-only exports name the open game map.
        const bool OpenGameMap = OpenMap && Out.Inventory.Contains(FName(*OpenMap->GetOutermost()->GetName()));
        if (!OpenGameMap && !Out.Creator.ManifestOnly)
        {
            Error.Reset();
            return true;
        }
        if (!OpenGameMap)
        {
            Error = TEXT("Open the original game's persistent map before exporting its changes.");
            return false;
        }
        const FString Package = OpenMap->GetOutermost()->GetName();
        if (!Request.MapPackage.IsEmpty() && Request.MapPackage != Package)
        {
            Error = TEXT("Open the selected game's persistent map before exporting its changes.");
            return false;
        }
        Request.MapPackage = Package;
    }
    Error.Reset();
    return true;
}

bool P4KadanExportMod(FP4ExportModRequest Request, UWorld* OpenMap, FP4ExportModResult& Out, FString& Error)
{
    Out = FP4ExportModResult();
    FP4KadanModReview Review;
    if (!P4KadanReviewMod(Request, OpenMap, Review, Error))
        return false;
    if (!Review.Creator.CanExport())
    {
        Out.Refused = Review.Creator.Blockers;
        Error = Review.Creator.Blockers.Num() ? Review.Creator.Blockers[0]
                                              : TEXT("Resolve the export review before continuing.");
        return false;
    }
    return P4ExportMod(Request, Out, Error);
}

#pragma once
#include "CoreMinimal.h"
#include "P4ModDescriptor.h"

enum class EP4ExportRootKind : uint8
{
    Package,
    Folder
};
struct FP4ExportRoot
{
    EP4ExportRootKind Kind = EP4ExportRootKind::Package;
    FString Path;
};
enum class EP4ExportDisposition : uint8
{
    CreatorPackage,
    UnchangedGame,
    GameEdit,
    NeedsReplacementProof,
    Refused
};
enum class EP4ExportIssueKind : uint8
{
    None,
    PersistentMapRequired
};
struct FP4ExportSelectionRow
{
    FString Package, AssetClass, Reason;
    // The selected package or folder that first brought this row into the closure.
    FString SelectingRoot;
    EP4ExportDisposition Disposition = EP4ExportDisposition::Refused;
    EP4ExportIssueKind Issue = EP4ExportIssueKind::None;
    bool Explicit = false;
};
struct FP4ExportSelectionPreview
{
    TArray<FP4ExportSelectionRow> Rows;
    TArray<FString> Refused;
    // Census only: this never asserts that cooked/container bytes passed export admission.
    TArray<FString> CookCandidates;
    TArray<FString> GameEditCandidates;
    TArray<FString> TableEditCandidates;
    TArray<FString> UnchangedLevelCandidates;
};
class FP4ExportGameInventory;
class UWorld;
// Validates descriptor-only export against original provenance and the open map's graph.
KADAN_API bool P4ValidateUnchangedMapSelection(
    const FP4ModDescriptor& Mod, const FP4ExportSelectionPreview& Preview, UWorld* OpenMap, FString& Error);
KADAN_API bool P4ValidateExportRoots(const TArray<FP4ExportRoot>& Roots, FString& Error);
// Selection persists separately from the exact format-1 identity, Config/KadanSelections/<id>.json.
KADAN_API bool P4ReadExportRoots(
    const FString& ProjectDirectory, const FP4ModDescriptor& Mod, TArray<FP4ExportRoot>& Out, FString& Error);
KADAN_API bool P4WriteExportRoots(
    const FString& ProjectDirectory, const FP4ModDescriptor& Mod, const TArray<FP4ExportRoot>& Roots, FString& Error);
// PackageClaims maps package -> another mod's ID; conflicting packages are refused by name.
// Inventory comes from actual game containers. Inspecting the closure never saves or cooks assets.
KADAN_API bool P4PreviewExportSelection(const FP4ModDescriptor& Mod, const TArray<FP4ExportRoot>& Roots,
    const FP4ExportGameInventory& Inventory, const TMap<FName, FString>& PackageClaims, FP4ExportSelectionPreview& Out,
    FString& Error);

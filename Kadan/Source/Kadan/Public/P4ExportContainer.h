#pragma once
#include "CoreMinimal.h"

struct FP4ExportContainerTools
{
    FString Retoc, Repak;
    double TimeoutSeconds = 120;
};
// Only the reader can establish validity. Project JSON cannot manufacture a verified inventory.
class KADAN_API FP4ExportGameInventory
{
public:
    bool IsValid() const { return Valid; }
    bool Contains(FName Package) const { return Names.Contains(Package); }
    const TMap<uint64, FName>& PackageIds() const { return Ids; }
    // Identity of bytes actually inspected; these describe the inventory, not every package payload.
    const FString& RegistrySha256() const { return RegistryDigest; }
    const FString& ContainerHeaderSha256() const { return HeaderDigest; }
    const FString& UtocSha256() const { return UtocDigest; }
    const FString& InventorySha256() const { return InventoryDigest; }
    bool IsFromDirectory(const FString& Directory) const;

private:
    bool Valid = false;
    TSet<FName> Names;
    TMap<uint64, FName> Ids;
    FString SourceDirectory, RegistryDigest, HeaderDigest, UtocDigest, InventoryDigest;
    friend KADAN_API bool P4ReadExportGameInventory(
        const FP4ExportContainerTools&, const FString&, const FString&, FP4ExportGameInventory&, FString&);
};
struct FP4ExportContainerCensus
{
    TArray<uint64> PackageIds;
    TMap<uint64, uint64> Redirects;
    TArray<TPair<uint64, uint64>> CultureRedirects;
    TArray<uint64> GameClaims;
};
// Reads the actual game pak registry and actual IoStore header, checking their package identities.
// WorkDirectory is private scratch; small temporary files are removed after the read.
// Whether a container tool's error says the computer had no memory left for it, after its retries.
KADAN_API bool P4ToolRanOutOfMemory(const FString& Error);
// How much more memory programs on this computer can be given, page file included.
KADAN_API int64 P4AvailableCommitBytes();
// What to tell the creator when the disk holding Folder has under 1 GB free (P4_KADAN_DISK_FLOOR_GB changes it); empty otherwise.
KADAN_API FString P4DiskFullMessage(const FString& Folder);
// Replaces To with a copy of From, written beside it first so a reader never finds half a file. A failure is the
// caller's to report: nothing is logged as an error and no part file stays.
KADAN_API bool P4CopyFileWhole(const FString& From, const FString& To);
KADAN_API bool P4ReadExportGameInventory(const FP4ExportContainerTools& Tools, const FString& GamePaksDirectory,
    const FString& WorkDirectory, FP4ExportGameInventory& Out, FString& Error);
// Every package plus redirect source is checked; an unavailable game inventory refuses inspection.
KADAN_API bool P4InspectExportContainer(const FP4ExportContainerTools& Tools, const FString& Utoc,
    const FString& WorkDirectory, const FP4ExportGameInventory& Game, FP4ExportContainerCensus& Out, FString& Error);
// Exact package selection; permits only retained identity redirects into that set.
KADAN_API bool P4ValidateExportCensusSelection(
    const FP4ExportContainerCensus& Census, const TArray<FString>& SelectedPackages, FString& Error);

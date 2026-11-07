#pragma once
#include "CoreMinimal.h"
#include "P4KadanImport.h"

struct FP4KadanSourceCatalogRow
{
    FString Package, Kind, ContainerPath;
    // retoc directory rows do not report package payload bytes.
    int64 SourceBytes=-1;
};
struct FP4KadanSourceCatalog
{
    FString GameFolder, SourceIdentity;
    FP4KadanImportSettings ImportSettings;
    TArray<TSharedPtr<FP4KadanSourceCatalogRow>> Rows;
    bool IndexFromCache=false;
};

// Supports a Paks folder and the usual romfs/Carrot4/Content ancestors. The
// original pak and matching IoStore pair must be present; no payload is read.
KADAN_API bool P4KadanNormalizeGamePaks(const FString& Folder,FString& Out,FString& Error);
// Actual retoc list --path inventory, with no game extraction or fake headers.
// .umap is World; .uasset type is unknown until its selected source is acquired.
KADAN_API bool P4KadanReadSourceCatalog(const FString& GameFolder,const FString& Retoc,
    FP4KadanSourceCatalog& Out,FString& Error);
KADAN_API bool P4KadanSourceCatalogCurrent(const FP4KadanSourceCatalog& Catalog);
// Controlled source cache used by normal catalog selection. Advanced extracted
// folders continue through the legacy scanner and never become owned caches.
KADAN_API FString P4KadanCatalogGameFolder();
KADAN_API bool P4KadanIsCatalogGameFolder(const FString& Folder);
KADAN_API bool P4KadanPrepareCatalogImport(const FP4KadanImportSettings& Settings,FString& Error);

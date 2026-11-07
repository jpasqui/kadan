#pragma once
#include "CoreMinimal.h"
struct FP4KadanImportSettings;
// Only the exact mounted header from a checked container index. No cache lookup,
// invalidation, staging-link deletion or publication. The caller owns Into.
KADAN_API bool P4KadanConvertMountedOriginal(const FP4KadanImportSettings&,
    const FString& MountedHeader,const FString& Into,FString& Error);
// The same conversion for several exact mounted headers in one retoc run per chunk of
// filters (retoc ORs its filters; each package's files are the same as from its own run).
KADAN_API bool P4KadanConvertMountedOriginals(const FP4KadanImportSettings&,
    const TArray<FString>& MountedHeaders,const FString& Into,FString& Error);

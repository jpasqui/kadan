#pragma once
#include "CoreMinimal.h"

// Browse metadata only. A listed header path is not a converted/importable
// FP4KadanSourceAsset and never supplies source payload bytes.
struct FP4KadanContainerPackage
{
    FString Package, Path;
    bool IsMap=false;
};

// Observed retoc list --path rows. This cache belongs to one ordinary import;
// names retain their exact container spelling while lookup follows UE identity.
class KADAN_API FP4KadanContainerIndex
{
public:
    bool Read(const FString& Listing, FString& Error);
    bool Resolve(const FString& Package, FString& Path, FString& Error) const;
    bool Enumerate(TArray<FP4KadanContainerPackage>& Out, FString& Error) const;
private:
    TMap<FName, FString> Paths;
    TSet<FName> Ambiguous;
    bool Ready=false;
};

KADAN_API bool P4KadanExtractedPackageReady(const FString& Header, const FString& Package, FString& Error);

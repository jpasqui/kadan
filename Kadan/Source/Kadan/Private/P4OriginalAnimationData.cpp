#include "P4OriginalAnimationData.h"
#include "DerivedDataCacheInterface.h"

bool UP4OriginalAnimationData::Seed() const
{
#if WITH_EDITORONLY_DATA
    if (CacheSuffix.IsEmpty() || CacheBytes.Num() == 0) return false;
    // UE4.26 FDerivedDataAnimationCompression's compression format version, not the game's id.
    // A different engine format must refuse this retained cache rather than deserialize it.
    const FString Key = FDerivedDataCacheInterface::BuildCacheKey(TEXT("AnimSeq"), TEXT("D8E26604610C43A4A2DCB8A690657705"), *CacheSuffix);
    // A prior ordinary recompression may already occupy the reconstructed source key.
    // Replace that one entry with the validated original data, including on reimport.
    GetDerivedDataCacheRef().Put(*Key, CacheBytes, GetPathName(), true);
    return true;
#else
    return false;
#endif
}

void UP4OriginalAnimationData::PostLoad()
{
    Super::PostLoad();
    Seed();
}

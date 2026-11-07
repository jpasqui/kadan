#pragma once
#include "CoreMinimal.h"

struct FP4KadanImportSettings;
struct FP4KadanSourceInputs
{
    FString Root, ShaderArchive;
    TArray<FString> Packages;
};

// Acquire the real source closure of material resources into one reused project cache.
// Existing game files are reused. This does not create editor assets or substitute proxies.
KADAN_API bool P4KadanAcquireSourceClosure(const FP4KadanImportSettings& Configured,
    const TArray<FString>& Roots, FP4KadanSourceInputs& Out, FString& Error);

// Material import already acquired its package closure. Reuse/acquire only the
// exact Switch shader library in the selected containers' fingerprinted cache.
// An explicit archive remains authoritative; an invalid external choice refuses.
KADAN_API bool P4KadanAcquireShaderArchive(const FP4KadanImportSettings& Configured,
    FString& Archive, FString& Error);

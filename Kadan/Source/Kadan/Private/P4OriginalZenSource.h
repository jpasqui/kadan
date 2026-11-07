#pragma once
#include "CoreMinimal.h"
#include "P4CurrentGameRegistry.h"

class FP4OriginalZenSource;
using FP4OriginalZenSourcePtr = TSharedPtr<const FP4OriginalZenSource, ESPMode::ThreadSafe>;
// Fresh mounted raw output only. This does not establish converted-header or
// reference-role equivalence; the portable verifier must consume all four buffers.
KADAN_API bool P4AcquireOriginalZenSource(const FP4CurrentGameRegistryPtr& Reader, const FP4ExportContainerTools& Tools,
    const FString& Paks, const FString& Package, const FString& WorkParent, FP4OriginalZenSourcePtr& Out,
    FString& Error, const TFunction<bool()>& Cancel = {});

class KADAN_API FP4OriginalZenSource
{
public:
    ~FP4OriginalZenSource();
    bool Current(FString& Error) const;
    // The immutable selected inputs and exact output files remain held before,
    // during and after the consumer. No caller-supplied JSON can mint this owner.
    bool Read(
        const TFunction<bool(const TArray<uint8>&, const TArray<uint8>&, FString&)>& Consumer, FString& Error) const;
    const FString& Package() const;
    uint64 PackageId() const;
    const FString& RawSha256() const;
    const FString& MetadataSha256() const;
    const FString& LeaseEpoch() const;
    const FString& RetocSha256() const;
    // Exact total lengths of every held output, including bounded diagnostics.
    uint64 HeldOutputBytes() const;
    // Diagnostic custody location; not a durable source path or authority.
    const FString& CustodyDirectory() const;

private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
    explicit FP4OriginalZenSource(TUniquePtr<FImpl>&&);
    friend KADAN_API bool P4AcquireOriginalZenSource(const FP4CurrentGameRegistryPtr&, const FP4ExportContainerTools&,
        const FString&, const FString&, const FString&, FP4OriginalZenSourcePtr&, FString&, const TFunction<bool()>&);
};

// Explicit, synchronous Blueprint batch lifetime only. No plans, failures or
// reflection are retained. Overflow remains a normal uncached acquisition.
class KADAN_API FP4OriginalZenSourceBatch
{
public:
    explicit FP4OriginalZenSourceBatch(uint32 MaxEntries = 64, uint64 MaxOutputBytes = 64ull * 1024 * 1024);
    ~FP4OriginalZenSourceBatch();
    bool Acquire(const FP4CurrentGameRegistryPtr& Reader, const FP4ExportContainerTools& Tools, const FString& Paks,
        const FString& Package, const FString& WorkParent, FP4OriginalZenSourcePtr& Out, FString& Error);
    uint32 Entries() const;
    uint64 HeldOutputBytes() const;
    FP4OriginalZenSourceBatch(const FP4OriginalZenSourceBatch&) = delete;
    FP4OriginalZenSourceBatch& operator=(const FP4OriginalZenSourceBatch&) = delete;

private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
};

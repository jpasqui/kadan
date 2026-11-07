#pragma once
#include "CoreMinimal.h"
#include "P4MaterialFamilySources.h"
struct FP4KadanImportSettings;
class FP4KadanContainerIndex;
struct FP4MaterialUsedResources;
struct FP4MaterialUsedResourceRequest;
class FP4MaterialFamilyAcquireContext;
class FP4MaterialFamilyPreparedStage;
using FP4MaterialFamilyAcquireContextPtr = TSharedPtr<FP4MaterialFamilyAcquireContext>;
using FP4MaterialFamilyPreparedStagePtr = TSharedPtr<const FP4MaterialFamilyPreparedStage>;
enum class EP4MaterialFamilyStageResult
{
    Ready,
    Cancelled,
    Refused
};
// Read-only discovery of anonymous-uniform metadata absences; creates no acquisition context/cache.
KADAN_API bool P4MaterialFamilyBindingMetadataRequired(const FP4KadanImportSettings&, const FP4MaterialFamilySources&,
    const TArray<TSharedPtr<FP4KadanSourceAsset>>&, bool& Required, FString& Error);
// Read-only context admission. Requires an already current owned cache; never
// creates/invalidates its stamp. Its fresh index is read lazily after veto/cancel.
KADAN_API bool P4CreateMaterialFamilyAcquireContext(const FP4KadanImportSettings&, const FP4CurrentGameRegistryPtr&,
    FP4MaterialFamilyAcquireContextPtr&, FString& Error);
// Optional ExistingLookup is a locator for read-only cache hits, NOT index authority.
// Missing conversion paths are re-resolved from one fresh list under the lease.
// Cancellation is checked BETWEEN stages; existing tool runner is not cancellable in-flight.
KADAN_API EP4MaterialFamilyStageResult P4PrepareMissingMaterialFamilyStage(const FP4MaterialFamilyAcquireContextPtr&,
    const FString& Selection, const TArray<TSharedPtr<FP4KadanSourceAsset>>& Registered,
    const FP4KadanContainerIndex* ExistingLookup, const TFunction<bool()>& Cancel, FP4MaterialFamilyPreparedStagePtr&,
    FString& Error);
// Preserve existing checked-locator callers; an absent locator asks the context
// to obtain its fresh current list before it reads cache paths.
inline EP4MaterialFamilyStageResult P4PrepareMissingMaterialFamilyStage(
    const FP4MaterialFamilyAcquireContextPtr& Context, const FString& Selection,
    const TArray<TSharedPtr<FP4KadanSourceAsset>>& Registered, const FP4KadanContainerIndex& ExistingLookup,
    const TFunction<bool()>& Cancel, FP4MaterialFamilyPreparedStagePtr& Out, FString& Error)
{
    return P4PrepareMissingMaterialFamilyStage(Context, Selection, Registered, &ExistingLookup, Cancel, Out, Error);
}

class KADAN_API FP4MaterialFamilyAcquireContext
{
public:
    ~FP4MaterialFamilyAcquireContext();
    bool Current(FString& Error) const;

private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
    explicit FP4MaterialFamilyAcquireContext(TUniquePtr<FImpl>&&);
    friend class FP4MaterialFamilyPreparedStage;
    friend KADAN_API bool P4CreateMaterialFamilyAcquireContext(
        const FP4KadanImportSettings&, const FP4CurrentGameRegistryPtr&, FP4MaterialFamilyAcquireContextPtr&, FString&);
    friend KADAN_API EP4MaterialFamilyStageResult P4PrepareMissingMaterialFamilyStage(
        const FP4MaterialFamilyAcquireContextPtr&, const FString&, const TArray<TSharedPtr<FP4KadanSourceAsset>>&,
        const FP4KadanContainerIndex*, const TFunction<bool()>&, FP4MaterialFamilyPreparedStagePtr&, FString&);
};
// Preparation witness. No cache publication/commit method exists.
// Only this explicit lifetime owns the fresh marked stage. Exact unchanged own
// outputs are removed on release; unexpected/changed artifacts retain custody.
class KADAN_API FP4MaterialFamilyPreparedStage
{
public:
    ~FP4MaterialFamilyPreparedStage();
    bool Current(FString& Error) const;
    const FP4MaterialFamilySourcesPtr& Sources() const;
    const FString& StageDirectory() const;
    int32 ConvertedPackages() const;
    // Only actual complete all-owner typed discovery can request resources.
    // Complete registered/cache bundles are bound read-only; wholly absent
    // bundles convert into this SAME scoped stage, never into the cache.
    // Returned executable resource descriptors remain local to the caller.
    // Acquires only source-typed collection layouts and anonymous texture metadata needed for graph discovery.
    // Returned rows remain recipe metadata; they must never enter Plan.Assets or ImportOne.
    EP4MaterialFamilyStageResult PrepareBindingMetadata(const TArray<TSharedPtr<FP4KadanSourceAsset>>& Registered,
        const TFunction<bool()>& Cancel, TArray<TSharedPtr<FP4KadanSourceAsset>>& Out, FString& Error) const;
    EP4MaterialFamilyStageResult PrepareUsedResources(const FP4MaterialUsedResources&,
        const TArray<TSharedPtr<FP4KadanSourceAsset>>& Registered, const TFunction<bool()>& Cancel,
        TArray<TSharedPtr<FP4KadanSourceAsset>>& Out, FString& Error) const;

private:
    EP4MaterialFamilyStageResult PrepareResourceInputs(const TArray<FP4MaterialUsedResourceRequest>&,
        const TArray<TSharedPtr<FP4KadanSourceAsset>>&, const TFunction<bool()>&,
        TArray<TSharedPtr<FP4KadanSourceAsset>>&, FString&) const;
    struct FImpl;
    TUniquePtr<FImpl> Impl;
    explicit FP4MaterialFamilyPreparedStage(TUniquePtr<FImpl>&&);
    friend KADAN_API EP4MaterialFamilyStageResult P4PrepareMissingMaterialFamilyStage(
        const FP4MaterialFamilyAcquireContextPtr&, const FString&, const TArray<TSharedPtr<FP4KadanSourceAsset>>&,
        const FP4KadanContainerIndex*, const TFunction<bool()>&, FP4MaterialFamilyPreparedStagePtr&, FString&);
};

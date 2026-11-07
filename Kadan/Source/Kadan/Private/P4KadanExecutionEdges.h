#pragma once
#include "CoreMinimal.h"
#include "P4MaterialUsedResources.h"
struct FP4KadanSourceAsset;
// One synchronous planner/run's source witnesses. Raw scanned dependency arrays
// remain inventory; only verified Material/MIC sources get execution edges.
class KADAN_API FP4KadanExecutionEdges
{
public:
    bool Read(const FP4KadanSourceAsset&,FString& Error);
    bool Dependencies(const FP4KadanSourceAsset&,TArray<FString>& Out,FString& Error)const;
    bool Current(FString& Error)const;
    bool Has(const FString& Package)const;
    // A parameter collection a verified material names. Its source is read before the material's
    // graph exists, so it is needed even when the project already has the collection.
    bool SourceNeeded(const FString& Package)const;
private:
    struct FWitness
    {
        FString Kind;
        TArray<FString> RawDependencies,SoftDependencies;
        FP4MaterialExecutionSource Source;
    };
    TMap<FName,TSharedPtr<const FWitness>> Sources;
    TMap<const FP4KadanSourceAsset*,FName> DescriptorIdentities;
    const FWitness* Find(const FP4KadanSourceAsset&)const;
    bool Matches(const FP4KadanSourceAsset&,const FWitness&,FString& Error)const;
};
// Bounded actual header/body identity for requested roots, including .umap.
// This is current read evidence, never an original-asset historical baseline.
KADAN_API bool P4KadanRequestedSourceDigest(const FP4KadanSourceAsset&,FString& Digest,FString& Error,FString* RootClass=nullptr);

// Current typed UObject/source equality; no import, migration or save. Existing
// resources are reused only when their real native source/content proof holds.
KADAN_API bool P4KadanMaterialResourceReady(const FP4MaterialUsedResourceRequest&,
    const FP4KadanSourceAsset&,FString& Error);

// After new genuine source metadata is available, graph sampler metadata may
// change. Existing source/program/consumer identities and exact request union
// must remain bound; every final request must have actual source metadata.
KADAN_API bool P4KadanMaterialDiscoveryAfterHydration(const FP4MaterialUsedResources& Before,
    const FP4MaterialUsedResources& After,FString& Error);

// Typed capability boundary before acquisition/publication. Collections and
// texture classes lacking a native texture-source proof refuse per asset.
KADAN_API bool P4KadanMaterialResourceCanExecute(const FP4MaterialUsedResourceRequest&,FString& Error);

// Occupied destinations are ONLY reused after exact native/source acceptance,
// regardless of Resume. No creator/edited/stand-in original is replaced here.
KADAN_API bool P4KadanMaterialResourceNeedsImport(const FP4MaterialUsedResourceRequest&,
    const FP4KadanSourceAsset&,bool& NeedsImport,FString& Error);

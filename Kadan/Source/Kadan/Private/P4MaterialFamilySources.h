#pragma once
#include "CoreMinimal.h"
#include "P4CurrentGameRegistry.h"
#include "P4KadanImportSources.h"

struct FP4KadanImportSettings;
struct FP4KadanSourceAsset;
class FP4KadanContainerIndex;
class FP4MaterialFamilySources;
using FP4MaterialFamilySourcesPtr=TSharedPtr<const FP4MaterialFamilySources>;
enum class EP4MaterialFamilySourceRead { Ready,MissingSources,Refused };

// Read-only first layer. Registered sources win, including failed/conflicting
// bindings. Optional catalog paths locate an ALREADY current conversion cache;
// neither this call nor Current creates, acquires, converts, stages or deletes.
// MissingSources lists the complete unbound family request, not executable assets.
// Optional AvailableSourceBytes reports only successfully validated available
// pairs on Ready/MissingSources, for acquisition budgeting. Zero on Refused;
// it is not a partial family witness or permission to publish an asset.
// A partial/invalid existing pair or stale supplied cache is Refused, never
// permission to overwrite/invalidate it. No CachePaths means registered-only.
KADAN_API EP4MaterialFamilySourceRead P4ReadMaterialFamilySources(
    const FP4KadanImportSettings&,const FP4CurrentGameRegistryPtr&,const FString& Selection,
    const TArray<TSharedPtr<FP4KadanSourceAsset>>& RegisteredSources,const FP4KadanContainerIndex* CachePaths,
    FP4MaterialFamilySourcesPtr& Out,TArray<FString>& MissingSources,FString& Error,int64* AvailableSourceBytes=nullptr);

// Owns the current registry lease and exact source snapshots. No project JSON or
// caller-supplied checksum can manufacture this witness. It establishes current
// read identity and source owner coverage, not bytes-against-game/render fidelity.
class KADAN_API FP4MaterialFamilySources
{
public:
    ~FP4MaterialFamilySources();
    bool Current(const FP4KadanImportSettings&,FString& Error)const;
    const FString& Master()const;
    const FString& SourceSetSha256()const;
    const FString& RegistrySha256()const;
    const FString& RegistryRowsSha256()const;
    const FString& RegistryLeaseEpoch()const;
    int32 ShaderOwnerCount()const;
    // One entry per family member, pointing to its actual nearest shader owner.
    const TMap<FString,FString>& ShaderOwners()const;
    const TArray<FP4KadanSourceBinding>& Bindings()const;
    // Creates fresh metadata descriptors for recipe resolution/source scope.
    // Caller must first Current-check; must NOT append these to executable
    // Plan.Assets/BringIn roots or treat the snapshots as a graph qualification.
    void AppendMetadataSources(TArray<TSharedPtr<FP4KadanSourceAsset>>& Out)const;
private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
    explicit FP4MaterialFamilySources(TUniquePtr<FImpl>&& In);
    friend KADAN_API EP4MaterialFamilySourceRead P4ReadMaterialFamilySources(
        const FP4KadanImportSettings&,const FP4CurrentGameRegistryPtr&,const FString&,
        const TArray<TSharedPtr<FP4KadanSourceAsset>>&,const FP4KadanContainerIndex*,
        FP4MaterialFamilySourcesPtr&,TArray<FString>&,FString&,int64*);
};

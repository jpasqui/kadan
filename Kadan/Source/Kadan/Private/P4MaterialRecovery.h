#pragma once
#include "CoreMinimal.h"
#include "P4MaterialUsedResources.h"

struct FP4KadanSourceAsset;
struct FP4KadanImportSettings;
class UWorld;

// Read-only lookup of an ALREADY recovered complete merged parent recipe. Every
// raw read/absence is rechecked in the caller's current source scope, including
// the exact parent's SHA256. Never recovers a graph, invents a seal, or updates a
// record. The recipe MD5 must equal the existing recovered parent's identity.
KADAN_API bool P4CurrentRecoveredMaterialParentRecipe(const FString& Package,const FString& ParentSourceSha256,
    const FString& ExpectedRecipeMd5,FString& Error);

struct FP4MaterialRecoveryResult
{
    bool bOk = false;
    // What is refused and why, in words, when bOk is false. RefusalName is the kind of refusal.
    FString Reason, RefusalName;
    // Material or MaterialInstanceConstant.
    FString Kind;
    // What is built: the master's graph or the instance, in the JSON P4BuildMaterialFromGraph and
    // P4BuildMaterialInstance read (P4MaterialGraph.h, P4MaterialInstance.h).
    FString Source;
    // The build's report (P4BuildMaterialGraphReport, P4BuildMaterialInstanceReport) after Import.
    FString Report;
    // Coverage of the caller-supplied source universe, not a claim that it
    // contains every instance in the game. Owners/users passed the merge proof.
    TArray<FString> FamilyMembers, FamilyOwners, FamilyUsers;
    // The material, or its master, has Kadan game nodes: the editor draws it with stand-in values
    // where the game reads the scene (P4KadanMaterialExpressions.h).
    bool bPreviewDiffers = false;
};

// The game's materials recovered at import from the creator's own game files, with no records made
// beforehand. A master's node graph is recovered from its compiled shaders (the shader library in the
// game folder) together with the graphs of its instances that have shaders of their own, merged into one
// graph with its static switches, and rebuilt as a native material graph for display/reference.
// Original masters retain their read-only/reference-only policy. An instance is built from what it
// sets itself, once its master has a graph. What cannot be recovered is refused by name: a master whose
// shaders are not read, a master whose graphs do not merge, and the instances of either.
//
// One parsed shader library serves the import; short-lived complete-family recipes share that owner.
class KADAN_API FP4MaterialRecovery
{
public:
    // GameFolder: the extracted game the packages were scanned from. Assets: the scanned packages
    // (P4KadanScanGame, or an import plan's assets). Their material instances are the instances a master
    // is merged from; every package is read from the files the scan found.
    // ShaderArchive optionally reuses an existing game library outside that folder.
    FP4MaterialRecovery(const FString& GameFolder, const TArray<TSharedPtr<FP4KadanSourceAsset>>& Assets, const FString& ShaderArchive = FString());
    // Fresh source/recipe scope with the same explicitly held parsed archive.
    FP4MaterialRecovery(const FP4MaterialRecovery& ArchiveOwner, const TArray<TSharedPtr<FP4KadanSourceAsset>>& Assets);
    ~FP4MaterialRecovery();

    // Recovers what a Material or MaterialInstanceConstant package would be built from, without building.
    FP4MaterialRecoveryResult Recover(const FString& Package);
    // All unique owners from the sealed current family, using this object's
    // held archive/raw resolver. Traces owners separately from Family recovery;
    // reported counts are actual work, not a once-per-master cache claim.
    // The required guard protects selected registry/stage/source lifetime.
    FP4MaterialUsedResources DiscoverUsedResources(const FP4MaterialFamilySources& Family,
        const TFunction<bool(FString&)>& SourceCurrent);
    // Lazy SHA of immutable held archive bytes, shared across recipe scopes.
    int32 ArchiveDigestComputes()const;
    // Owner graphs a family recovery took from a used-resource trace instead of recovering them again.
    int32 OwnerGraphReuses()const;
    // Recovers and builds the package's material or material instance at the same path, saving it when
    // asked. A real renderer compiles a master before it is reported valid.
    // SourceCurrent is a read-only lifetime guard, held through construction and native save validation.
    // Without bWaitForShaders a master is saved while its shaders still compile (P4BuildMaterialGraphReport).
    FP4MaterialRecoveryResult Import(const FString& Package, bool bSaveToDisk,
        const TFunction<bool(FString&)>& SourceCurrent = TFunction<bool(FString&)>(), bool bWaitForShaders = true);
    // Source-derived base-color drawing cache for inline Landscape shader owners.
    // Does not change original material parents, values, or exporter admission.
    int32 CacheLandscapeColorPreviews(UWorld*,const FP4KadanSourceAsset&,const FP4KadanImportSettings&,TArray<FString>& Refusals);
    void ReportLandscapeColorPreviews(const FString& AssetPath, const FP4KadanSourceAsset& Source,
        const FP4KadanImportSettings& Settings, const TSharedPtr<FJsonObject>& Reply, TArray<FString>& Warnings);

    // Masters recovered so far with a graph, and without one.
    int32 MastersWithGraph() const;
    int32 MastersWithoutGraph() const;
    // Actual work on this explicit shared archive owner, not an admission proof.
    int32 ShaderArchiveReadAttempts()const;
    int32 FamilyRecoveryBuilds()const;
    // One sentence for the import report naming every material recovered so far that the editor draws
    // a little differently from the game. Empty when there is none.
    FString PreviewNote() const;

private:
    friend KADAN_API bool P4CurrentRecoveredMaterialParentRecipe(const FString&,const FString&,const FString&,FString&);
    struct FState;
    TUniquePtr<FState> State;
};

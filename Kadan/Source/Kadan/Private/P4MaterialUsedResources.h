#pragma once
#include "CoreMinimal.h"
#include "P4KadanImportSources.h"
class FP4MaterialFamilySources;
enum class EP4MaterialUsedResourceStatus
{
    Complete,
    Incomplete,
    Refused
};
enum class EP4MaterialUsedResourceKind
{
    Texture,
    Collection
};
struct FP4MaterialResourceSource
{
    FString Package, HeaderSha256, BodySha256, SourceSha256;
};
struct FP4MaterialResourceProgram
{
    uint32 ArchiveIndex = 0;
    uint8 Frequency = 0;
    FString PayloadSha256;
};
struct FP4MaterialResourceReference
{
    FString SourcePackage, SourceSha256, Property, Object, NativeClass;
    int32 ImportIndex = 0;
};
struct FP4MaterialResourceConsumer
{
    FString Node, NodeType, Parameter;
    TArray<uint64> CollectionUniforms;
    TArray<FString> CollectionStateIds;
};
struct FP4MaterialResourceOwnerUse
{
    FString Owner, OwnerSourceSha256, ResourceHash, GraphSha256;
    TArray<FP4MaterialResourceReference> References;
    TArray<FP4MaterialResourceConsumer> Consumers;
};
struct FP4MaterialUsedResourceRequest
{
    EP4MaterialUsedResourceKind Kind = EP4MaterialUsedResourceKind::Texture;
    FString Package, Object, NativeClass, SourceSha256, SamplerType;
    bool bSourceAvailable = false;
    TArray<FP4MaterialResourceOwnerUse> Owners;
};
struct FP4MaterialResourceOwnerProof
{
    FString Owner, OwnerSourceSha256, ResourceHash, GraphSha256;
    uint64 GraphNodes = 0, ReachableNodes = 0;
    FString ShaderTypeHash, VertexFactoryHash;
    int32 ShaderResourceIndex = -1, ShaderPermutation = 0;
    uint8 ShaderFrequency = 0;
    uint64 ShaderFrozenOffset = 0;
    TArray<FP4MaterialResourceSource> Sources;
    TArray<FP4MaterialResourceProgram> Programs;
    TArray<FString> CachedMissingNotRequested;
};
struct FP4MaterialUsedResources
{
    EP4MaterialUsedResourceStatus Status = EP4MaterialUsedResourceStatus::Refused;
    FString Reason, RefusalCategory, Master, FamilySourceSetSha256, RegistryLeaseEpoch, ArchiveSha256;
    TArray<FP4MaterialUsedResourceRequest> Requests;
    TArray<FP4MaterialResourceOwnerProof> Owners;
    // Actual work of THIS discovery, not existing Family recovery or a claimed
    // once-per-master cache. One portable graph producer call per unique owner.
    // SourceCurrentReads counts direct bundle rechecks, not filesystem opens or
    // work performed inside the caller's SourceCurrent guard.
    int32 OwnerTraceCalls = 0, RawSourceReads = 0, SourceCurrentReads = 0, SourceGuardChecks = 0;
    // Owner traces that ran on worker threads (no reusable trace was held for them).
    int32 ParallelOwnerTraces = 0;
    bool bRendererProven = false, bProductionAdmitted = false;
};
enum class EP4MaterialExecutionResourceKind
{
    Parent,
    TextureOverride,
    ParameterCollection
};
enum class EP4MaterialExecutionSourceRead
{
    Verified,
    Unsupported,
    InvalidSource
};
struct FP4MaterialExecutionResource
{
    EP4MaterialExecutionResourceKind Kind = EP4MaterialExecutionResourceKind::Parent;
    FString Package, Object, NativeClass, Parameter, Property;
    int32 ImportIndex = 0;
};
struct FP4MaterialExecutionSource
{
    EP4MaterialExecutionSourceRead Status = EP4MaterialExecutionSourceRead::InvalidSource;
    FString RefusalCategory;
    FP4KadanSourceBinding Binding;
    FString Object, NativeClass, HeaderSha256, BodySha256, SourceSha256;
    TArray<FP4MaterialExecutionResource> Resources;
};
// No shader trace, acquisition, source mutation or UObject access. Requires an
// actual current source binding. Typed Unsupported is valid root/source with
// an unsupported execution value, never corruption or authorization to load it.
// InvalidSource includes changed/invalid H+B/root. No error-string dispatch.
// Masters bootstrap actual typed collection imports before all-owner discovery.
// MICs also retain actual hard Parent and explicitly authored texture values.
KADAN_API bool P4ReadMaterialExecutionSource(
    const FP4KadanSourceBinding&, FP4MaterialExecutionSource& Out, FString& Error);
KADAN_API bool P4MaterialExecutionSourceCurrent(const FP4MaterialExecutionSource&, FString& Error);

#include "P4MaterialUsedResourcesInternal.h"
#include "P4MaterialFamilySources.h"
#include "P4MaterialUsedTraceCache.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/PlatformMisc.h"
#include "Misc/ScopeLock.h"
#include <atomic>
#include <exception>
THIRD_PARTY_INCLUDES_START
#include "P4MapKitCore/material_execution_resources.h"
THIRD_PARTY_INCLUDES_END

namespace
{
namespace UsedM = P4MapKit::Material;
namespace UsedC = P4MapKit::CustomMaterial;
std::string UsedUtf8(const FString& Value)
{
    FTCHARToUTF8 Text(*Value);
    return std::string(Text.Get(), Text.Length());
}
FString UsedWide(const std::string& Value)
{
    return FString(UTF8_TO_TCHAR(Value.c_str()));
}
FString UsedHash64(uint64 Value)
{
    std::ostringstream Text;
    Text << std::hex << std::setfill('0') << std::setw(16) << Value;
    return UsedWide(Text.str());
}
UsedM::MaterialOriginalImportRawAsset UsedRaw(const FP4KadanSourceBinding& Source)
{
    if (!Source.Header.Num() || !Source.Body.Num())
        throw UsedM::Refused("current source binding has no complete bytes", "material used native binding");
    return {
        std::make_shared<const UsedC::Bytes>(Source.Header.GetData(), Source.Header.GetData() + Source.Header.Num()),
        std::make_shared<const UsedC::Bytes>(Source.Body.GetData(), Source.Body.GetData() + Source.Body.Num())};
}
// One owner's trace is a function of the bytes its reads return (or that a read found nothing) and of
// the held archive. A material import traces every owner of a family for each material of the family,
// twice, so a family of many instances traced the same owners over and over. A trace is reused only
// when the same reads, made again in the same order through the caller's resolver, return the same
// bytes and absences; the reads keep their snapshots, counters and bounds as the real trace would.
FP4MaterialUsedTraceCache& UsedTraceCache()
{
    static FP4MaterialUsedTraceCache Cache;
    return Cache;
}
// Runs Work(0..Count-1) on worker threads and waits for all of them. Each worker has a large stack:
// shader recovery recurses through expression trees deeper than a task graph thread allows.
void UsedRunParallel(std::size_t Count, const TFunction<void(std::size_t)>& Work)
{
    const int32 Cores = FMath::Max(1, FPlatformMisc::NumberOfCoresIncludingHyperthreads() - 2);
    const int32 Threads = int32(FMath::Min<std::size_t>(Count, std::size_t(FMath::Min(Cores, 16))));
    if (Threads <= 1)
    {
        for (std::size_t i = 0; i < Count; ++i)
            Work(i);
        return;
    }
    struct FWorker final : FRunnable
    {
        std::atomic<std::size_t>& Next;
        std::size_t Count;
        const TFunction<void(std::size_t)>& Work;
        FWorker(std::atomic<std::size_t>& InNext, std::size_t InCount, const TFunction<void(std::size_t)>& InWork)
            : Next(InNext), Count(InCount), Work(InWork)
        {}
        uint32 Run() override
        {
            for (std::size_t i = Next++; i < Count; i = Next++)
                Work(i);
            return 0;
        }
    };
    std::atomic<std::size_t> Next{0};
    TArray<TUniquePtr<FWorker>> Workers;
    TArray<FRunnableThread*> Running;
    for (int32 i = 0; i < Threads; ++i)
    {
        Workers.Add(MakeUnique<FWorker>(Next, Count, Work));
        if (FRunnableThread* Thread = FRunnableThread::Create(
                Workers.Last().Get(), *FString::Printf(TEXT("KadanMaterialTrace%d"), i), 64 * 1024 * 1024))
            Running.Add(Thread);
    }
    // A thread that could not start leaves its share to the others; the game thread takes what is left.
    for (std::size_t i = Next++; i < Count; i = Next++)
        Work(i);
    for (FRunnableThread* Thread : Running)
    {
        Thread->WaitForCompletion();
        delete Thread;
    }
}
UsedM::FMaterialUsedResourceDiscovery UsedTrace(const std::string& Owner,
    const UsedM::MaterialOriginalImportRawResolver& Read, const UsedM::MaterialOriginalArchiveAccess& Archive,
    const FString& HeldArchiveSha256)
{
    return UsedTraceCache().Discover(
        Owner, Read, Archive, UsedUtf8(HeldArchiveSha256), UsedM::DiscoverOriginalMaterialUsedResources);
}
FP4MaterialResourceOwnerProof UsedOwner(const UsedM::FMaterialUsedResourceDiscovery& Discovery)
{
    FP4MaterialResourceOwnerProof Out;
    Out.Owner = UsedWide(Discovery.Owner);
    Out.OwnerSourceSha256 = UsedWide(Discovery.OwnerSourceSha256);
    Out.ResourceHash = UsedWide(Discovery.ResourceHash);
    Out.GraphSha256 = UsedWide(Discovery.GraphSha256);
    Out.GraphNodes = Discovery.GraphNodes;
    Out.ReachableNodes = Discovery.ReachableNodes;
    const auto& Ref = Discovery.PrimaryReference;
    Out.ShaderTypeHash = UsedHash64(Ref.TypeHash);
    Out.VertexFactoryHash = UsedHash64(Ref.VertexFactoryHash);
    Out.ShaderResourceIndex = Ref.ResourceIndex;
    Out.ShaderPermutation = Ref.Permutation;
    Out.ShaderFrequency = Ref.Frequency;
    Out.ShaderFrozenOffset = Ref.FrozenOffset;
    for (const auto& Source : Discovery.Sources)
        Out.Sources.Add({UsedWide(Source.Package), UsedWide(Source.HeaderSha256), UsedWide(Source.BodySha256),
            UsedWide(Source.SourceSha256)});
    for (const auto& Program : Discovery.ArchiveReads)
        Out.Programs.Add({Program.ArchiveIndex, Program.Frequency, UsedWide(Program.PayloadSha256)});
    for (const auto& Missing : Discovery.CachedMissingNotRequested)
        Out.CachedMissingNotRequested.Add(UsedWide(Missing));
    return Out;
}
FP4MaterialResourceOwnerUse UsedOccurrence(const UsedM::FMaterialUsedResourceRequest& Request)
{
    FP4MaterialResourceOwnerUse Out;
    Out.Owner = UsedWide(Request.Owner);
    Out.OwnerSourceSha256 = UsedWide(Request.OwnerSourceSha256);
    Out.ResourceHash = UsedWide(Request.ResourceHash);
    Out.GraphSha256 = UsedWide(Request.GraphSha256);
    for (const auto& Ref : Request.References)
        Out.References.Add({UsedWide(Ref.SourcePackage), UsedWide(Ref.SourceSha256), UsedWide(Ref.Property),
            UsedWide(Ref.Object), UsedWide(Ref.NativeClass), Ref.ImportIndex});
    for (const auto& Use : Request.Uses)
    {
        FP4MaterialResourceConsumer Consumer;
        Consumer.Node = UsedWide(Use.Node);
        Consumer.NodeType = UsedWide(Use.NodeType);
        Consumer.Parameter = UsedWide(Use.Parameter);
        for (const auto Index : Use.CollectionUniforms)
            Consumer.CollectionUniforms.Add(Index);
        for (const auto& Guid : Use.CollectionStateIds)
            Consumer.CollectionStateIds.Add(UsedWide(Guid));
        Out.Consumers.Add(MoveTemp(Consumer));
    }
    return Out;
}
void UsedExecution(const FP4KadanSourceBinding& Binding, FP4MaterialExecutionSource& Out)
{
    const auto Read = UsedM::ReadOriginalMaterialExecutionResources(UsedRaw(Binding), UsedUtf8(Binding.Package));
    Out.Binding = Binding;
    Out.Status = EP4MaterialExecutionSourceRead::Verified;
    Out.Object = UsedWide(Read.Object);
    Out.NativeClass = UsedWide(Read.NativeClass);
    Out.HeaderSha256 = UsedWide(Read.HeaderSha256);
    Out.BodySha256 = UsedWide(Read.BodySha256);
    Out.SourceSha256 = UsedWide(Read.SourceSha256);
    for (const auto& Edge : Read.Resources)
    {
        FP4MaterialExecutionResource Native;
        Native.Kind = Edge.Kind == UsedM::EMaterialExecutionResourceKind::Parent
            ? EP4MaterialExecutionResourceKind::Parent
            : Edge.Kind == UsedM::EMaterialExecutionResourceKind::ParameterCollection
            ? EP4MaterialExecutionResourceKind::ParameterCollection
            : EP4MaterialExecutionResourceKind::TextureOverride;
        Native.Package = UsedWide(Edge.Package);
        Native.Object = UsedWide(Edge.Object);
        Native.NativeClass = UsedWide(Edge.NativeClass);
        Native.Parameter = UsedWide(Edge.Parameter);
        Native.Property = UsedWide(Edge.Property);
        Native.ImportIndex = Edge.ImportIndex;
        Out.Resources.Add(MoveTemp(Native));
    }
}
bool UsedExecutionEqual(const FP4MaterialExecutionSource& A, const FP4MaterialExecutionSource& B)
{
    const auto Equal = [](const FString& X, const FString& Y) { return X.Equals(Y, ESearchCase::CaseSensitive); };
    if (A.Status != B.Status || !Equal(A.Object, B.Object) || !Equal(A.NativeClass, B.NativeClass) ||
        !Equal(A.HeaderSha256, B.HeaderSha256) || !Equal(A.BodySha256, B.BodySha256) ||
        !Equal(A.SourceSha256, B.SourceSha256) || A.Resources.Num() != B.Resources.Num())
        return false;
    for (int32 Index = 0; Index < A.Resources.Num(); ++Index)
    {
        const auto& X = A.Resources[Index];
        const auto& Y = B.Resources[Index];
        if (X.Kind != Y.Kind || X.ImportIndex != Y.ImportIndex || !Equal(X.Package, Y.Package) ||
            !Equal(X.Object, Y.Object) || !Equal(X.NativeClass, Y.NativeClass) || !Equal(X.Parameter, Y.Parameter) ||
            !Equal(X.Property, Y.Property))
            return false;
    }
    return true;
}
} //namespace

bool P4ReadMaterialExecutionSource(
    const FP4KadanSourceBinding& Binding, FP4MaterialExecutionSource& Out, FString& Error)
{
    Out = FP4MaterialExecutionSource();
    Error.Reset();
    if (!P4KadanSourceBindingCurrent(Binding, Error))
        return false;
    try
    {
        FP4MaterialExecutionSource Read;
        UsedExecution(Binding, Read);
        if (!P4KadanSourceBindingCurrent(Binding, Error))
            return false;
        Out = MoveTemp(Read);
        return true;
    }
    catch (const UsedM::MaterialExecutionUnsupported& Failure)
    {
        if (!P4KadanSourceBindingCurrent(Binding, Error))
            return false;
        Out.Status = EP4MaterialExecutionSourceRead::Unsupported;
        Out.RefusalCategory = UsedWide(Failure.name);
        Out.Binding = Binding;
        Error = UsedWide(Failure.what());
        return false;
    }
    catch (const UsedM::Refused& Failure)
    {
        Out.RefusalCategory = UsedWide(Failure.name);
        Error = UsedWide(Failure.what());
        return false;
    }
}
bool P4MaterialExecutionSourceCurrent(const FP4MaterialExecutionSource& Source, FString& Error)
{
    if (Source.Status != EP4MaterialExecutionSourceRead::Verified)
    {
        Error = TEXT("Material execution source is not verified");
        return false;
    }
    if (!P4KadanSourceBindingCurrent(Source.Binding, Error))
        return false;
    try
    {
        FP4MaterialExecutionSource Read;
        UsedExecution(Source.Binding, Read);
        if (!UsedExecutionEqual(Source, Read))
        {
            Error = TEXT("Material execution source witness changed");
            return false;
        }
        return P4KadanSourceBindingCurrent(Source.Binding, Error);
    }
    catch (const UsedM::Refused& Failure)
    {
        Error = UsedWide(Failure.what());
        return false;
    }
}

bool P4FindTracedMaterialOwnerGraph(const std::string& Owner, const UsedM::MaterialOriginalImportRawResolver& Read,
    const FString& HeldArchiveSha256, const std::vector<std::string>& Collections, UsedM::Json& Graph)
{
    return UsedTraceCache().FindGraph(Owner, Read, UsedUtf8(HeldArchiveSha256), Collections, Graph);
}
int32 P4TracedMaterialOwnerGraphHits()
{
    return UsedTraceCache().GraphHits;
}

FP4MaterialUsedResources P4DiscoverMaterialUsedResourcesNative(const FP4MaterialFamilySources& Family,
    const UsedM::MaterialOriginalImportRawResolver& Raw, const UsedM::MaterialOriginalArchiveAccess& Archive,
    const FString& HeldArchiveSha256, const TFunction<bool(FString&)>& SourceCurrent)
{
    FP4MaterialUsedResources Result;
    Result.Master = Family.Master();
    Result.FamilySourceSetSha256 = Family.SourceSetSha256();
    Result.RegistryLeaseEpoch = Family.RegistryLeaseEpoch();
    const auto Guard = [&]()
    {
        ++Result.SourceGuardChecks;
        FString Error;
        if (!SourceCurrent || !SourceCurrent(Error))
            throw UsedM::Refused(Error.IsEmpty() ? "current family/source guard is unavailable" : UsedUtf8(Error),
                "material used native current");
    };
    try
    {
        Guard();
        if (HeldArchiveSha256.Len() != 64)
            throw UsedM::Refused("held shader archive digest is unavailable", "material used native archive");
        Result.ArchiveSha256 = HeldArchiveSha256;
        std::map<std::string, UsedM::MaterialOriginalImportRawAsset> Snapshots;
        std::map<std::string, const FP4KadanSourceBinding*> FamilyBindings;
        uint64 SnapshotBytes = 0;
        const auto Account = [&](const UsedM::MaterialOriginalImportRawAsset& Input)
        {
            const uint64 Bytes = uint64(Input.Header->size()) + uint64(Input.Body->size());
            if (Bytes > 128ull * 1024 * 1024 - SnapshotBytes || Snapshots.size() >= 4096)
                throw UsedM::Refused("family resource snapshots exceed their bound", "material used native bound");
            SnapshotBytes += Bytes;
        };
        for (const auto& Binding : Family.Bindings())
        {
            const auto Package = UsedUtf8(Binding.Package);
            auto Input = UsedRaw(Binding);
            Account(Input);
            if (!Snapshots.emplace(Package, std::move(Input)).second)
                throw UsedM::Refused("validated family has duplicate source packages", "material used native family");
            FamilyBindings.emplace(Package, &Binding);
        }
        std::set<std::string> Owners, Missing;
        bool bInvalidRawSource = false;
        for (const auto& Pair : Family.ShaderOwners())
        {
            if (!Snapshots.count(UsedUtf8(Pair.Key)) || !Snapshots.count(UsedUtf8(Pair.Value)))
                throw UsedM::Refused("validated member/owner lacks its exact binding", "material used native family");
            Owners.insert(UsedUtf8(Pair.Value));
        }
        if (Owners.empty() || Owners.size() != std::size_t(Family.ShaderOwnerCount()) ||
            Family.ShaderOwners().Num() != Family.Bindings().Num() || !Owners.count(UsedUtf8(Family.Master())))
            throw UsedM::Refused("validated shader owner coverage differs", "material used native family");
        const UsedM::MaterialOriginalImportRawResolver Read =
            [&](const std::string& Package, UsedM::MaterialOriginalImportRawAsset& Out)
        {
            const auto Prior = Snapshots.find(Package);
            if (Prior != Snapshots.end())
            {
                Out = Prior->second;
                return true;
            }
            if (Missing.count(Package))
                return false;
            if (Missing.size() >= 4096)
                throw UsedM::Refused("missing resource set exceeds its bound", "material used native bound");
            ++Result.RawSourceReads;
            UsedM::MaterialOriginalImportRawAsset Current;
            if (!Raw || !Raw(Package, Current))
            {
                Missing.insert(Package);
                return false;
            }
            try
            {
                const auto Checked = UsedM::ReadOriginalMaterialImportCustody(Current, Package);
                if (Checked.Asset.Package.Package != Package)
                    throw UsedM::Refused("raw resource source is not case-exact", "material used native source");
            }
            catch (const UsedM::Refused&)
            {
                bInvalidRawSource = true;
                throw;
            }
            Account(Current);
            Snapshots.emplace(Package, Current);
            Out = std::move(Current);
            return true;
        };
        // Owners with no reusable trace are traced on worker threads; the shared resolver is locked, so
        // reads and their bounds stay as one trace at a time keeps them. Results are then taken in owner
        // order, exactly as the one-at-a-time loop took them, and a trace that threw rethrows here.
        const std::vector<std::string> OwnerList(Owners.begin(), Owners.end());
        const std::string ArchiveKey = UsedUtf8(HeldArchiveSha256);
        auto& Cache = UsedTraceCache();
        std::vector<UsedM::FMaterialUsedResourceDiscovery> Traced(OwnerList.size());
        std::vector<FP4MaterialUsedTraceCache::FReads> Witness(OwnerList.size());
        std::vector<std::exception_ptr> Thrown(OwnerList.size());
        std::unique_ptr<bool[]> Cacheable(new bool[OwnerList.size()]);
        std::unique_ptr<bool[]> Invalid(new bool[OwnerList.size()]);
        std::vector<std::size_t> Pending;
        for (std::size_t i = 0; i < OwnerList.size(); ++i)
        {
            Cacheable[i] = true;
            Invalid[i] = false;
            if (!Cache.Find(OwnerList[i], Read, ArchiveKey, Traced[i]))
                Pending.push_back(i);
        }
        FCriticalSection ReadLock;
        UsedRunParallel(Pending.size(),
            [&](std::size_t n)
            {
                const std::size_t i = Pending[n];
                // Which trace read an invalid raw source, as the in-order loop below needs to know.
                const UsedM::MaterialOriginalImportRawResolver Own =
                    [&, i](const std::string& Package, UsedM::MaterialOriginalImportRawAsset& Out)
                {
                    FScopeLock Hold(&ReadLock);
                    const bool Was = bInvalidRawSource;
                    bInvalidRawSource = false;
                    try
                    {
                        const bool Found = Read(Package, Out);
                        bInvalidRawSource = Was;
                        return Found;
                    }
                    catch (...)
                    {
                        if (bInvalidRawSource)
                            Invalid[i] = true;
                        bInvalidRawSource = bInvalidRawSource || Was;
                        throw;
                    }
                };
                try
                {
                    Traced[i] = UsedM::DiscoverOriginalMaterialUsedResources(
                        OwnerList[i], FP4MaterialUsedTraceCache::Logging(Own, Witness[i], Cacheable[i]), Archive);
                }
                catch (...)
                {
                    Thrown[i] = std::current_exception();
                }
            });
        for (const std::size_t i : Pending)
            if (!Thrown[i] && Cacheable[i])
                Cache.Keep(OwnerList[i], ArchiveKey, std::move(Witness[i]), Traced[i]);
        Result.ParallelOwnerTraces = int32(Pending.size());
        bool InvalidSoFar = false;
        for (std::size_t OwnerIndex = 0; OwnerIndex < OwnerList.size(); ++OwnerIndex)
        {
            if (Thrown[OwnerIndex])
                std::rethrow_exception(Thrown[OwnerIndex]);
            InvalidSoFar |= Invalid[OwnerIndex];
            ++Result.OwnerTraceCalls;
            const auto& Discovery = Traced[OwnerIndex];
            if (Discovery.Status != UsedM::EMaterialUsedResourceStatus::Complete)
            {
                Guard();
                if (InvalidSoFar)
                    throw UsedM::Refused(Discovery.Refusal, Discovery.RefusalCategory);
                Result.Status = EP4MaterialUsedResourceStatus::Incomplete;
                Result.Reason = UsedWide(Discovery.Refusal);
                Result.RefusalCategory = UsedWide(Discovery.RefusalCategory);
                Result.Requests.Reset();
                return Result;
            }
            Result.Owners.Add(UsedOwner(Discovery));
            for (const auto& Request : Discovery.Requests)
            {
                const auto Kind = Request.Kind == UsedM::EMaterialUsedResourceKind::Texture
                    ? EP4MaterialUsedResourceKind::Texture
                    : EP4MaterialUsedResourceKind::Collection;
                const auto Package = UsedWide(Request.Package), Object = UsedWide(Request.Object),
                           Class = UsedWide(Request.NativeClass), Digest = UsedWide(Request.SourceSha256),
                           Sampler = UsedWide(Request.SamplerType);
                auto* Existing = Result.Requests.FindByPredicate([&](const FP4MaterialUsedResourceRequest& Item)
                    { return Item.Package.Equals(Package, ESearchCase::CaseSensitive); });
                if (!Existing)
                {
                    FP4MaterialUsedResourceRequest Item;
                    Item.Kind = Kind;
                    Item.Package = Package;
                    Item.Object = Object;
                    Item.NativeClass = Class;
                    Item.SourceSha256 = Digest;
                    Item.SamplerType = Sampler;
                    Item.bSourceAvailable = Request.SourceAvailable;
                    const auto Index = Result.Requests.Add(MoveTemp(Item));
                    Existing = &Result.Requests[Index];
                }
                if (Existing->Kind != Kind || Existing->bSourceAvailable != Request.SourceAvailable ||
                    !Existing->Object.Equals(Object, ESearchCase::CaseSensitive) ||
                    !Existing->NativeClass.Equals(Class, ESearchCase::CaseSensitive) ||
                    !Existing->SourceSha256.Equals(Digest, ESearchCase::CaseSensitive) ||
                    !Existing->SamplerType.Equals(Sampler, ESearchCase::CaseSensitive))
                    throw UsedM::Refused(
                        "shader owners disagree about one resource identity/source", "material used native union");
                Existing->Owners.Add(UsedOccurrence(Request));
            }
        }
        // Family buffers come from the sealed witness; resources come from the
        // held raw resolver. Recheck both, including previously absent sources.
        for (const auto& Pair : Snapshots)
        {
            ++Result.SourceCurrentReads;
            const auto Bound = FamilyBindings.find(Pair.first);
            if (Bound != FamilyBindings.end())
            {
                FString Error;
                if (!P4KadanSourceBindingCurrent(*Bound->second, Error))
                    throw UsedM::Refused(UsedUtf8(Error), "material used native current");
                continue;
            }
            UsedM::MaterialOriginalImportRawAsset Current;
            if (!Raw || !Raw(Pair.first, Current))
                throw UsedM::Refused("used resource source disappeared", "material used native current");
            const auto Before = UsedM::ReadOriginalMaterialImportCustody(Pair.second, Pair.first),
                       After = UsedM::ReadOriginalMaterialImportCustody(Current, Pair.first);
            if (After.Asset.Package.Package != Pair.first || Before.SourceSha256 != After.SourceSha256)
                throw UsedM::Refused("used resource source changed during discovery", "material used native current");
        }
        for (const auto& Package : Missing)
        {
            ++Result.SourceCurrentReads;
            UsedM::MaterialOriginalImportRawAsset Current;
            if (Raw && Raw(Package, Current))
                throw UsedM::Refused(
                    "absent resource source appeared during discovery", "material used native current");
        }
        Guard();
        Result.Requests.Sort([](const FP4MaterialUsedResourceRequest& A, const FP4MaterialUsedResourceRequest& B)
            { return A.Package < B.Package; });
        Result.Status = EP4MaterialUsedResourceStatus::Complete;
        return Result;
    }
    catch (const UsedM::Refused& Failure)
    {
        Result.Status = EP4MaterialUsedResourceStatus::Refused;
        Result.Reason = UsedWide(Failure.what());
        Result.RefusalCategory = UsedWide(Failure.name);
        Result.Requests.Reset();
        return Result;
    }
}

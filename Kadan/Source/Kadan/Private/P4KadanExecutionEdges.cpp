#include "P4KadanExecutionEdges.h"
#include "P4KadanImport.h"
#include "P4KadanContainerIndex.h"
#include "P4BoundedSourceRead.h"
#include "P4NativeHash.h"
#include "P4KadanSourceStamp.h"
#include "P4KadanImportSources.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Misc/PackageName.h"
#include "P4TextureContent.h"
#include "P4TextureInventory.h"
#include "P4MaterialCollectionSource.h"
THIRD_PARTY_INCLUDES_START
#include "P4MapKitCore/package_reader.h"
THIRD_PARTY_INCLUDES_END
#include "P4OriginalRecord.h"
#include "Engine/Texture.h"
#include "Materials/MaterialParameterCollection.h"
#include "UObject/Package.h"

const FP4KadanExecutionEdges::FWitness* FP4KadanExecutionEdges::Find(const FP4KadanSourceAsset& Asset) const
{
    const FName* Original = DescriptorIdentities.Find(&Asset);
    const auto* Found = Sources.Find(Original ? *Original : FName(*Asset.Package));
    return Found ? Found->Get() : nullptr;
}
bool FP4KadanExecutionEdges::Matches(const FP4KadanSourceAsset& Asset, const FWitness& Witness, FString& Error) const
{
    const auto ArraysEqual = [](const TArray<FString>& A, const TArray<FString>& B)
    {
        if (A.Num() != B.Num())
            return false;
        for (int32 At = 0; At < A.Num(); ++At)
            if (!P4ExactNativeString(A[At], B[At]))
                return false;
        return true;
    };
    const auto& Read = Witness.Source;
    if (!P4ExactNativeString(Read.Binding.Package, Asset.Package) || !P4ExactNativeString(Witness.Kind, Asset.Kind) ||
        !FPaths::IsSamePath(Read.Binding.HeaderPath, Asset.Header) ||
        !FPaths::IsSamePath(Read.Binding.BodyPath, Asset.Body) ||
        !ArraysEqual(Witness.RawDependencies, Asset.Dependencies) ||
        !ArraysEqual(Witness.SoftDependencies, Asset.SoftDependencies))
    {
        Error = TEXT("Material execution descriptor package/kind/paths/raw inventory changed");
        return false;
    }
    if (Read.Status == EP4MaterialExecutionSourceRead::Verified)
    {
        const FString Expected = Asset.Kind == TEXT("Material") ? TEXT("/Script/Engine.Material")
                                                                : TEXT("/Script/Engine.MaterialInstanceConstant");
        if (!P4ExactNativeString(Expected, Read.NativeClass))
        {
            Error = TEXT("Material execution descriptor differs from its witnessed native class");
            return false;
        }
        return P4MaterialExecutionSourceCurrent(Read, Error);
    }
    // Only this valid unsupported Material/MIC input is witnessed. Its RAW
    // traversal remains legacy behavior; this never grants deferred edges.
    return Read.Status == EP4MaterialExecutionSourceRead::Unsupported &&
        P4KadanSourceBindingCurrent(Read.Binding, Error);
}
bool FP4KadanExecutionEdges::Read(const FP4KadanSourceAsset& Asset, FString& Error)
{
    Error.Reset();
    if (const auto* Prior = Find(Asset))
        return Matches(Asset, *Prior, Error);
    if (Asset.Kind != TEXT("Material") && Asset.Kind != TEXT("MaterialInstanceConstant"))
        return true;
    FP4KadanSourceBinding Binding;
    if (!P4KadanReadBoundSource(Asset.Package, Binding, Error))
        return false;
    FP4MaterialExecutionSource Read;
    if (!P4ReadMaterialExecutionSource(Binding, Read, Error) &&
        Read.Status != EP4MaterialExecutionSourceRead::Unsupported)
        return false;
    if (Read.Status == EP4MaterialExecutionSourceRead::Verified)
    {
        const FString Expected = Asset.Kind == TEXT("Material") ? TEXT("/Script/Engine.Material")
                                                                : TEXT("/Script/Engine.MaterialInstanceConstant");
        if (!P4ExactNativeString(Read.NativeClass, Expected) ||
            !P4ExactNativeString(Read.Binding.Package, Asset.Package))
        {
            Error = TEXT("Material execution source class or canonical identity differs from the scanned row");
            return false;
        }
    }
    auto Witness = MakeShared<FWitness>();
    Witness->Kind = Asset.Kind;
    Witness->RawDependencies = Asset.Dependencies;
    Witness->SoftDependencies = Asset.SoftDependencies;
    Witness->Source = MoveTemp(Read);
    Sources.Add(FName(*Asset.Package), Witness);
    DescriptorIdentities.Add(&Asset, FName(*Asset.Package));
    Error.Reset();
    return true;
}
bool FP4KadanExecutionEdges::Dependencies(const FP4KadanSourceAsset& Asset, TArray<FString>& Out, FString& Error) const
{
    Error.Reset();
    Out.Reset();
    const auto* Witness = Find(Asset);
    if (!Witness)
    {
        Out = Asset.Dependencies;
        return true;
    }
    if (!Matches(Asset, *Witness, Error))
        return false;
    if (Witness->Source.Status == EP4MaterialExecutionSourceRead::Unsupported)
    {
        Out = Asset.Dependencies;
        return true;
    }
    for (const auto& Resource : Witness->Source.Resources)
        Out.AddUnique(Resource.Package);
    return true;
}
bool FP4KadanExecutionEdges::Has(const FString& Package) const
{
    const auto* Found = Sources.Find(FName(*Package));
    return Found && (*Found)->Source.Status == EP4MaterialExecutionSourceRead::Verified;
}
bool FP4KadanExecutionEdges::SourceNeeded(const FString& Package) const
{
    const FName Identity(*Package);
    for (const auto& Pair : Sources)
    {
        const auto& Source = Pair.Value->Source;
        if (Source.Status != EP4MaterialExecutionSourceRead::Verified)
            continue;
        for (const auto& Resource : Source.Resources)
            if (Resource.Kind == EP4MaterialExecutionResourceKind::ParameterCollection &&
                FName(*Resource.Package) == Identity)
                return true;
    }
    return false;
}
bool FP4KadanExecutionEdges::Current(FString& Error) const
{
    Error.Reset();
    for (const auto& Pair : Sources)
    {
        const auto& Source = Pair.Value->Source;
        if (Source.Status == EP4MaterialExecutionSourceRead::Verified)
        {
            if (!P4MaterialExecutionSourceCurrent(Source, Error))
                return false;
        }
        else if (!P4KadanSourceBindingCurrent(Source.Binding, Error))
            return false;
    }
    return true;
}
namespace
{
// A requested source read whole, by its header and body paths. The import asks for the same sources many times; a
// pair whose files keep their stamps still has the digest and root class read from it.
struct FRequestedSource
{
    FString Package;
    FP4KadanSourceStamp Header, Body;
    FString Digest, RootClass;
    int64 Checked = 0;
};
TMap<FString, FRequestedSource> RequestedSources;
FCriticalSection RequestedSourcesLock;
} // namespace

bool P4KadanRequestedSourceDigest(const FP4KadanSourceAsset& Asset, FString& Digest, FString& Error, FString* RootClass)
{
    Digest.Reset();
    Error.Reset();
    if (RootClass)
        RootClass->Reset();
    const FString Key = Asset.Header + TEXT("|") + Asset.Body;
    const int64 Import = IsInGameThread() ? P4KadanImportGeneration() : 0;
    const auto Reuse = [&](FRequestedSource* Prior, bool bStampsMatch)
    {
        if (!Prior || !P4ExactNativeString(Prior->Package, Asset.Package) || (RootClass && Prior->RootClass.IsEmpty()) ||
            !(bStampsMatch || (Import && Prior->Checked == Import)))
            return false;
        Prior->Checked = Import;
        Digest = Prior->Digest;
        if (RootClass)
            *RootClass = Prior->RootClass;
        return true;
    };
    if (Import)
    {
        FScopeLock Lock(&RequestedSourcesLock);
        if (Reuse(RequestedSources.Find(Key), false))
            return true;
    }
    FP4KadanSourceStamp HeaderStamp, BodyStamp;
    const bool Known = P4KadanSourceStampOf(Asset.Header, HeaderStamp) && P4KadanSourceStampOf(Asset.Body, BodyStamp);
    if (Known)
    {
        FScopeLock Lock(&RequestedSourcesLock);
        FRequestedSource* Prior = RequestedSources.Find(Key);
        if (Prior && Prior->Header == HeaderStamp && Prior->Body == BodyStamp && Reuse(Prior, true))
            return true;
    }
    if (!P4KadanExtractedPackageReady(Asset.Header, Asset.Package, Error))
        return false;
    if (!FPaths::IsSamePath(FPaths::ChangeExtension(Asset.Header, TEXT("uexp")), Asset.Body))
    {
        Error = TEXT("Requested root body is not the canonical header companion");
        return false;
    }
    TArray<uint8> Header, Body;
    if (!P4ReadBoundedSourceFile(Asset.Header, 64 * 1024 * 1024, Header, Error) ||
        !P4ReadBoundedSourceFile(Asset.Body, 64 * 1024 * 1024, Body, Error) ||
        !P4HashSourceBuffers(Header.GetData(), Header.Num(), Body.GetData(), Body.Num(), Digest))
    {
        if (Error.IsEmpty())
            Error = TEXT("Requested source snapshot cannot be read or hashed");
        return false;
    }
    const auto Read = P4MapKit::ReadPackage(std::vector<uint8>(Header.GetData(), Header.GetData() + Header.Num()),
        std::vector<uint8>(Body.GetData(), Body.GetData() + Body.Num()));
    if (!Read.Error.Code.empty() || !P4ExactNativeString(FString(UTF8_TO_TCHAR(Read.Package.c_str())), Asset.Package))
    {
        Digest.Reset();
        Error = TEXT("Requested root canonical source identity or export bounds differ");
        return false;
    }
    if (RootClass)
    {
        int32 Found = 0;
        const FString Leaf = FPackageName::GetShortName(Asset.Package);
        for (const auto& Export : Read.Exports)
            if (Export.Outer == 0 && P4ExactNativeString(FString(UTF8_TO_TCHAR(Export.Name.c_str())), Leaf))
            {
                std::string Class;
                if (Read.Reference(Export.Class, Class))
                {
                    *RootClass = UTF8_TO_TCHAR(Class.c_str());
                    ++Found;
                }
            }
        if (Found != 1 || RootClass->IsEmpty())
        {
            Digest.Reset();
            Error = TEXT("Requested resource source has no unique canonical typed root");
            return false;
        }
    }
    // Kept only when neither file changed while it was read.
    FP4KadanSourceStamp HeaderAfter, BodyAfter;
    if (Known && P4KadanSourceStampOf(Asset.Header, HeaderAfter) && P4KadanSourceStampOf(Asset.Body, BodyAfter) &&
        HeaderAfter == HeaderStamp && BodyAfter == BodyStamp)
    {
        FScopeLock Lock(&RequestedSourcesLock);
        FRequestedSource& Kept = RequestedSources.FindOrAdd(Key);
        if (!(P4ExactNativeString(Kept.Package, Asset.Package) && Kept.Header == HeaderStamp &&
                Kept.Body == BodyStamp && Kept.Digest == Digest))
            Kept = FRequestedSource{Asset.Package, HeaderStamp, BodyStamp, Digest, FString()};
        Kept.Checked = Import;
        if (RootClass)
            Kept.RootClass = *RootClass;
    }
    return true;
}

bool P4KadanMaterialResourceReady(
    const FP4MaterialUsedResourceRequest& Request, const FP4KadanSourceAsset& Source, FString& Error)
{
    Error.Reset();
    if (!P4KadanMaterialResourceCanExecute(Request, Error))
        return false;
    FString CurrentDigest, RootClass;
    if (!Request.bSourceAvailable || !P4ExactNativeString(Request.Package, Source.Package) ||
        !P4KadanRequestedSourceDigest(Source, CurrentDigest, Error, &RootClass) ||
        !P4ExactNativeString(CurrentDigest, Request.SourceSha256) ||
        !P4ExactNativeString(RootClass, Request.NativeClass))
    {
        if (Error.IsEmpty())
            Error = TEXT("Shader-used resource lacks its exact final available source/class proof");
        return false;
    }
    UObject* Asset = StaticLoadObject(
        UObject::StaticClass(), nullptr, *Request.Object, nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (!IsValid(Asset) || !P4ExactNativeString(Asset->GetPathName(), Request.Object) ||
        !P4ExactNativeString(Asset->GetClass()->GetPathName(), Request.NativeClass))
    {
        Error = TEXT("Shader-used typed native resource is absent or has another identity");
        return false;
    }
    if (Request.Kind == EP4MaterialUsedResourceKind::Texture)
        return P4OriginalTextureMatchesSource(Cast<UTexture>(Asset), Source.Header, Source.Body, Error);
    auto Row = MakeShared<FP4KadanSourceAsset>(Source);
    FP4KadanImportSourceScope Scope({Row});
    FP4KadanSourceBinding Binding;
    if (!P4KadanReadBoundSource(Source.Package, Binding, Error))
        return false;
    return P4OriginalMaterialCollectionMatchesSource(
        Cast<UMaterialParameterCollection>(Asset), Request, Binding, Error);
}

bool P4KadanMaterialDiscoveryAfterHydration(
    const FP4MaterialUsedResources& Before, const FP4MaterialUsedResources& After, FString& Error)
{
    Error.Reset();
    const auto Fail = [&](const TCHAR* Why)
    {
        Error = Why;
        return false;
    };
    const auto Equal = [](const FString& A, const FString& B) { return P4ExactNativeString(A, B); };
    if (Before.Status != EP4MaterialUsedResourceStatus::Complete ||
        After.Status != EP4MaterialUsedResourceStatus::Complete || Before.Owners.Num() == 0 ||
        Before.Owners.Num() != After.Owners.Num() || Before.Requests.Num() != After.Requests.Num() ||
        !Equal(Before.Master, After.Master) || !Equal(Before.FamilySourceSetSha256, After.FamilySourceSetSha256) ||
        !Equal(Before.RegistryLeaseEpoch, After.RegistryLeaseEpoch) || Before.ArchiveSha256.Len() != 64 ||
        !Equal(Before.ArchiveSha256, After.ArchiveSha256))
        return Fail(TEXT("All-owner shader discovery/source/archive coverage changed after hydration"));
    const auto Linked = [&](const FP4MaterialUsedResources& Result)
    {
        TSet<FName> UniqueOwners, UniqueRequests;
        for (const auto& Owner : Result.Owners)
        {
            if (UniqueOwners.Contains(FName(*Owner.Owner)) || Owner.GraphSha256.Len() != 64)
                return false;
            UniqueOwners.Add(FName(*Owner.Owner));
        }
        for (const auto& Request : Result.Requests)
        {
            if (UniqueRequests.Contains(FName(*Request.Package)) || !Request.Owners.Num())
                return false;
            UniqueRequests.Add(FName(*Request.Package));
            TSet<FName> Uses;
            for (const auto& Use : Request.Owners)
            {
                const auto* Owner = Result.Owners.FindByPredicate(
                    [&](const FP4MaterialResourceOwnerProof& One) { return Equal(One.Owner, Use.Owner); });
                if (!Owner || Uses.Contains(FName(*Use.Owner)) || !Equal(Use.GraphSha256, Owner->GraphSha256) ||
                    !Equal(Use.OwnerSourceSha256, Owner->OwnerSourceSha256) ||
                    !Equal(Use.ResourceHash, Owner->ResourceHash))
                    return false;
                Uses.Add(FName(*Use.Owner));
            }
        }
        return true;
    };
    if (!Linked(Before) || !Linked(After))
        return Fail(TEXT("Shader-used per-request graph/owner source linkage is inconsistent"));
    TSet<FName> Requests, Owners;
    for (const auto& Request : After.Requests)
    {
        const auto* Prior = Before.Requests.FindByPredicate(
            [&](const FP4MaterialUsedResourceRequest& One) { return Equal(One.Package, Request.Package); });
        const FName Key(*Request.Package);
        if (!Prior || Requests.Contains(Key) || Prior->Kind != Request.Kind || !Equal(Prior->Object, Request.Object) ||
            !Equal(Prior->NativeClass, Request.NativeClass) || !Request.bSourceAvailable ||
            Request.SourceSha256.Len() != 64 ||
            (Request.Kind == EP4MaterialUsedResourceKind::Texture && Request.SamplerType.IsEmpty()) ||
            (Prior->bSourceAvailable &&
                (!Equal(Prior->SourceSha256, Request.SourceSha256) ||
                    !Equal(Prior->SamplerType, Request.SamplerType))) ||
            Prior->Owners.Num() != Request.Owners.Num())
            return Fail(TEXT("Shader-used typed resource union/source identity changed or remains unavailable"));
        Requests.Add(Key);
        for (const auto& Use : Request.Owners)
        {
            const auto* Old = Prior->Owners.FindByPredicate(
                [&](const FP4MaterialResourceOwnerUse& One) { return Equal(One.Owner, Use.Owner); });
            if (!Old || !Equal(Old->OwnerSourceSha256, Use.OwnerSourceSha256) ||
                !Equal(Old->ResourceHash, Use.ResourceHash) || Old->References.Num() != Use.References.Num() ||
                Old->Consumers.Num() != Use.Consumers.Num())
                return Fail(TEXT("Shader-used resource owner/program references changed"));
            for (int32 Index = 0; Index < Use.References.Num(); ++Index)
            {
                const auto& A = Old->References[Index];
                const auto& B = Use.References[Index];
                if (A.ImportIndex != B.ImportIndex || !Equal(A.SourcePackage, B.SourcePackage) ||
                    !Equal(A.SourceSha256, B.SourceSha256) || !Equal(A.Property, B.Property) ||
                    !Equal(A.Object, B.Object) || !Equal(A.NativeClass, B.NativeClass))
                    return Fail(TEXT("Shader-used original hard-import/property witness changed"));
            }
            for (int32 Index = 0; Index < Use.Consumers.Num(); ++Index)
            {
                const auto& A = Old->Consumers[Index];
                const auto& B = Use.Consumers[Index];
                if (!Equal(A.Node, B.Node) || !Equal(A.NodeType, B.NodeType) || !Equal(A.Parameter, B.Parameter) ||
                    A.CollectionUniforms != B.CollectionUniforms || A.CollectionStateIds != B.CollectionStateIds)
                    return Fail(TEXT("Shader-used reachable node/parameter witness changed"));
            }
        }
    }
    for (const auto& Owner : After.Owners)
    {
        const auto* Prior = Before.Owners.FindByPredicate(
            [&](const FP4MaterialResourceOwnerProof& One) { return Equal(One.Owner, Owner.Owner); });
        const FName Key(*Owner.Owner);
        if (!Prior || Owners.Contains(Key) || !Equal(Prior->OwnerSourceSha256, Owner.OwnerSourceSha256) ||
            !Equal(Prior->ResourceHash, Owner.ResourceHash) || Prior->Programs.Num() != Owner.Programs.Num() ||
            Prior->ShaderResourceIndex != Owner.ShaderResourceIndex ||
            Prior->ShaderPermutation != Owner.ShaderPermutation || Prior->ShaderFrequency != Owner.ShaderFrequency ||
            Prior->ShaderFrozenOffset != Owner.ShaderFrozenOffset ||
            !Equal(Prior->ShaderTypeHash, Owner.ShaderTypeHash) ||
            !Equal(Prior->VertexFactoryHash, Owner.VertexFactoryHash))
            return Fail(TEXT("Shader owner/source/program binding changed after hydration"));
        Owners.Add(Key);
        for (int32 Index = 0; Index < Owner.Programs.Num(); ++Index)
        {
            const auto& A = Prior->Programs[Index];
            const auto& B = Owner.Programs[Index];
            if (A.ArchiveIndex != B.ArchiveIndex || A.Frequency != B.Frequency ||
                !Equal(A.PayloadSha256, B.PayloadSha256))
                return Fail(TEXT("Shader archive payload identity changed after hydration"));
        }
        for (const auto& Source : Prior->Sources)
        {
            const auto* Now = Owner.Sources.FindByPredicate(
                [&](const FP4MaterialResourceSource& One) { return Equal(One.Package, Source.Package); });
            if (!Now || !Equal(Now->HeaderSha256, Source.HeaderSha256) || !Equal(Now->BodySha256, Source.BodySha256) ||
                !Equal(Now->SourceSha256, Source.SourceSha256))
                return Fail(TEXT("Previously consumed material/resource source changed after hydration"));
        }
        for (const auto& Source : Owner.Sources)
        {
            const bool Existing = Prior->Sources.ContainsByPredicate(
                [&](const FP4MaterialResourceSource& One) { return Equal(One.Package, Source.Package); });
            if (!Existing && !Requests.Contains(FName(*Source.Package)))
                return Fail(TEXT("New source was consumed without a typed shader-used request"));
        }
    }
    return true;
}

bool P4KadanMaterialResourceCanExecute(const FP4MaterialUsedResourceRequest& Request, FString& Error)
{
    Error.Reset();
    if (Request.Kind == EP4MaterialUsedResourceKind::Collection)
    {
        if (!P4ExactNativeString(Request.NativeClass, TEXT("/Script/Engine.MaterialParameterCollection")))
        {
            Error = TEXT("Shader-used collection has another native class");
            return false;
        }
        return true;
    }
    if (Request.Kind != EP4MaterialUsedResourceKind::Texture)
    {
        Error = TEXT("Shader-used resource kind is unsupported");
        return false;
    }
    P4MapKit::ETextureShape Shape;
    bool Light = false;
    FTCHARToUTF8 Class(*Request.NativeClass);
    if (!P4TextureShape(std::string(Class.Get(), Class.Length()), Shape, Light))
    {
        Error = TEXT("Shader-used texture class has no qualified native texture-source import: ") + Request.NativeClass;
        return false;
    }
    return true;
}

bool P4KadanMaterialResourceNeedsImport(
    const FP4MaterialUsedResourceRequest& Request, const FP4KadanSourceAsset& Source, bool& NeedsImport, FString& Error)
{
    NeedsImport = false;
    Error.Reset();
    if (!P4KadanMaterialResourceCanExecute(Request, Error))
        return false;
    const bool SavedPackage = FPackageName::DoesPackageExist(Request.Package);
    const bool LoadedPackage = FindPackage(nullptr, *Request.Package) != nullptr;
    const bool LoadedObject = FindObject<UObject>(nullptr, *Request.Object) != nullptr;
    const bool Occupied = SavedPackage || LoadedPackage || LoadedObject;
    if (Occupied)
    {
        UObject* Existing = StaticLoadObject(
            UObject::StaticClass(), nullptr, *Request.Object, nullptr, LOAD_NoWarn | LOAD_Quiet);
        FP4OriginalRecord Record;
        const FString Kind = Request.Kind == EP4MaterialUsedResourceKind::Collection ? TEXT("object") : TEXT("texture");
        FString Eligibility;
        if (!Existing)
            Eligibility = TEXT("requested-object-unreadable");
        else if (!P4ReadOriginalRecord(Existing, Record, Error))
            Eligibility = TEXT("original-record-unavailable") + (Error.IsEmpty() ? FString() : TEXT(": ") + Error);
        else if (Record.ReferenceOnly)
            Eligibility = TEXT("existing-original-is-reference-only");
        else if (!P4ExactNativeString(Record.Kind, Kind))
            Eligibility = FString::Printf(
                TEXT("existing-record-kind %s differs from required %s"), *Record.Kind, *Kind);
        if (!Eligibility.IsEmpty())
        {
            Error = FString::Printf(
                TEXT(
                    "Shader-used resource %s (%s) destination is occupied without an eligible unchanged native source; it is preserved: %s [saved_package=%d, loaded_package=%d, loaded_object=%d]"),
                *Request.Package, *Request.Object, *Eligibility, int32(SavedPackage), int32(LoadedPackage),
                int32(LoadedObject));
            return false;
        }
        return P4KadanMaterialResourceReady(Request, Source, Error);
    }
    FString Digest, Class;
    if (!Request.bSourceAvailable || !P4ExactNativeString(Request.Package, Source.Package) ||
        !P4KadanRequestedSourceDigest(Source, Digest, Error, &Class) ||
        !P4ExactNativeString(Digest, Request.SourceSha256) || !P4ExactNativeString(Class, Request.NativeClass))
    {
        if (Error.IsEmpty())
            Error = TEXT("Fresh shader-used resource lacks an exact current source/class proof");
        return false;
    }
    NeedsImport = true;
    return true;
}

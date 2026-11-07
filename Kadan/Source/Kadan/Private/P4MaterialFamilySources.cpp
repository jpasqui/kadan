#include "P4MaterialFamilySources.h"
#include "P4KadanImport.h"
#include "P4KadanContainerIndex.h"
#include "P4NativeHash.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
THIRD_PARTY_INCLUDES_START
#include "P4MapKitCore/material_family_source_validation.h"
THIRD_PARTY_INCLUDES_END

namespace
{
namespace FamilySource = P4MapKit::Material;
FString FamilySourceWide(const std::string& Text)
{
    return FString(UTF8_TO_TCHAR(Text.c_str()));
}
std::string FamilySourceUtf8(const FString& Text)
{
    FTCHARToUTF8 Value(*Text);
    return std::string(Value.Get(), Value.Length());
}
FP4ExportContainerTools FamilySourceTools(const FP4KadanImportSettings& Settings)
{
    FP4ExportContainerTools Tools;
    Tools.Retoc = Settings.Retoc;
    Tools.Repak = Settings.Repak;
    return Tools;
}
bool FamilySourceInside(const FString& Root, const FString& Path)
{
    FString Base = FPaths::ConvertRelativePathToFull(Root), File = FPaths::ConvertRelativePathToFull(Path);
    FPaths::NormalizeDirectoryName(Base);
    FPaths::NormalizeFilename(File);
    if (!FPaths::CollapseRelativeDirectories(Base) || !FPaths::CollapseRelativeDirectories(File))
        return false;
    return File.StartsWith(Base + TEXT("/"), ESearchCase::IgnoreCase);
}
} //namespace
struct FP4MaterialFamilySources::FImpl
{
    FP4CurrentGameRegistryPtr Registry;
    FString Master, SourceDigest, UsedCache;
    int32 OwnerCount = 0;
    TArray<FP4KadanSourceBinding> Sources;
    TMap<FString, FString> Classes, Owners;
};
FP4MaterialFamilySources::FP4MaterialFamilySources(TUniquePtr<FImpl>&& In) : Impl(MoveTemp(In))
{}
FP4MaterialFamilySources::~FP4MaterialFamilySources() = default;
const FString& FP4MaterialFamilySources::Master() const
{
    return Impl->Master;
}
const FString& FP4MaterialFamilySources::SourceSetSha256() const
{
    return Impl->SourceDigest;
}
const FString& FP4MaterialFamilySources::RegistrySha256() const
{
    return Impl->Registry->RegistrySha256();
}
const FString& FP4MaterialFamilySources::RegistryRowsSha256() const
{
    return Impl->Registry->MaterialRowsSha256();
}
const FString& FP4MaterialFamilySources::RegistryLeaseEpoch() const
{
    return Impl->Registry->LeaseEpoch();
}
int32 FP4MaterialFamilySources::ShaderOwnerCount() const
{
    return Impl->OwnerCount;
}
const TMap<FString, FString>& FP4MaterialFamilySources::ShaderOwners() const
{
    return Impl->Owners;
}
const TArray<FP4KadanSourceBinding>& FP4MaterialFamilySources::Bindings() const
{
    return Impl->Sources;
}
bool FP4MaterialFamilySources::Current(const FP4KadanImportSettings& Settings, FString& Error) const
{
    Error.Reset();
    if (!IsInGameThread())
    {
        Error = TEXT("Material family sources require the import game thread");
        return false;
    }
    if (!Impl->Registry.IsValid() || !Impl->Registry->IsCurrent(FamilySourceTools(Settings), Settings.GamePaks, Error))
        return false;
    if (!Impl->UsedCache.IsEmpty())
    {
        FString Now;
        if (!P4KadanRetainedConversionCacheCurrent(Settings, Now, Error))
            return false;
        if (!FPaths::IsSamePath(Now, Impl->UsedCache))
        {
            Error = TEXT("Material family conversion cache changed");
            return false;
        }
    }
    for (const auto& Source : Impl->Sources)
        if (!P4KadanSourceBindingCurrent(Source, Error))
            return false;
    return Impl->Registry->IsCurrent(FamilySourceTools(Settings), Settings.GamePaks, Error);
}
void FP4MaterialFamilySources::AppendMetadataSources(TArray<TSharedPtr<FP4KadanSourceAsset>>& Out) const
{
    for (const auto& Binding : Impl->Sources)
    {
        auto Source = MakeShared<FP4KadanSourceAsset>();
        Source->Package = Binding.Package;
        Source->Header = Binding.HeaderPath;
        Source->Body = Binding.BodyPath;
        Source->Kind = Impl->Classes.FindChecked(Binding.Package);
        Source->SourceBytes = int64(Binding.Header.Num()) + Binding.Body.Num();
        Out.Add(MoveTemp(Source));
    }
}
EP4MaterialFamilySourceRead P4ReadMaterialFamilySources(const FP4KadanImportSettings& Settings,
    const FP4CurrentGameRegistryPtr& Registry, const FString& Selection,
    const TArray<TSharedPtr<FP4KadanSourceAsset>>& RegisteredSources, const FP4KadanContainerIndex* CachePaths,
    FP4MaterialFamilySourcesPtr& Out, TArray<FString>& MissingSources, FString& Error, int64* AvailableSourceBytes)
{
    Out.Reset();
    MissingSources.Reset();
    Error.Reset();
    if (AvailableSourceBytes)
        *AvailableSourceBytes = 0;
    const auto Refused = [&](const FString& Why)
    {
        Error = Why;
        MissingSources.Reset();
        return EP4MaterialFamilySourceRead::Refused;
    };
    if (!IsInGameThread())
        return Refused(TEXT("Material family sources require the import game thread"));
    if (!Registry.IsValid())
        return Refused(TEXT("Material family sources need the current game registry lease"));
    if (!Registry->IsCurrent(FamilySourceTools(Settings), Settings.GamePaks, Error))
        return EP4MaterialFamilySourceRead::Refused;
    FamilySource::FMaterialRegistryFamily Family;
    if (!Registry->Family(Selection, Family, Error))
        return EP4MaterialFamilySourceRead::Refused;
    TMap<FName, TArray<TSharedPtr<FP4KadanSourceAsset>>> RegisteredByPackage;
    for (const auto& Source : RegisteredSources)
        if (Source.IsValid() && Source->Package.Len() > 0 && Source->Package.Len() <= 1023)
            RegisteredByPackage.FindOrAdd(FName(*Source->Package)).Add(Source);
    TArray<TSharedPtr<FP4KadanSourceAsset>> Rows;
    FString Cache;
    bool CacheChecked = false, CacheReady = false, UsedCache = false;
    for (const auto& Member : Family.Members)
    {
        const FString Package = FamilySourceWide(Member.Package);
        bool Registered = false;
        if (Package.Len() > 1023)
            return Refused(TEXT("Material family package exceeds the native name bound"));
        if (const auto* Found = RegisteredByPackage.Find(FName(*Package)))
        {
            // Scope registration deliberately skips noncanonical input. Refuse
            // matching aliases here rather than accepting another valid row
            // after the bad supplied binding was silently skipped.
            for (const auto& Source : *Found)
                if (!P4ExactNativeString(Source->Package, Package))
                    return Refused(TEXT("Material family supplied source canonical spelling differs: ") + Package);
            Rows.Append(*Found);
            Registered = true;
        }
        if (Registered)
            continue; // An invalid explicit binding must never silently fall back.
        if (!CacheChecked)
        {
            CacheChecked = true;
            if (CachePaths)
            {
                if (!P4KadanRetainedConversionCacheCurrent(Settings, Cache, Error))
                    return Refused(Error);
                CacheReady = true;
            }
        }
        if (!CacheReady)
        {
            MissingSources.Add(Package);
            continue;
        }
        FString Relative;
        if (!CachePaths->Resolve(Package, Relative, Error))
            return Refused(Error);
        const FString Header = Cache / Relative, Body = FPaths::ChangeExtension(Header, TEXT("uexp"));
        if (!FamilySourceInside(Cache, Header) || !FamilySourceInside(Cache, Body))
            return Refused(TEXT("Material family cache path escaped its owned root"));
        for (const TCHAR* Extension : {TEXT("uasset"), TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl")})
            if (IFileManager::Get().DirectoryExists(*FPaths::ChangeExtension(Header, Extension)))
                return Refused(TEXT("Material family cache bundle path names a directory: ") + Package);
        const bool HasHeader = IFileManager::Get().FileExists(*Header), HasBody = IFileManager::Get().FileExists(*Body);
        if (!HasHeader && !HasBody)
        {
            if (IFileManager::Get().FileExists(*FPaths::ChangeExtension(Header, TEXT("ubulk"))) ||
                IFileManager::Get().FileExists(*FPaths::ChangeExtension(Header, TEXT("uptnl"))))
                return Refused(TEXT("Material family cache has an incomplete existing bundle: ") + Package);
            MissingSources.Add(Package);
            continue;
        }
        if (!HasHeader || !HasBody)
            return Refused(TEXT("Material family cache has an incomplete existing pair: ") + Package);
        auto Source = MakeShared<FP4KadanSourceAsset>();
        Source->Package = Package;
        Source->Header = Header;
        Source->Body = Body;
        Source->Kind = FamilySourceWide(Member.Class);
        Rows.Add(MoveTemp(Source));
        UsedCache = true;
    }
    // Scope detects duplicate/case-conflicting explicit bindings and keeps missing
    // paths authoritative. It does not modify the caller's active source registry.
    FP4KadanImportSourceScope SourceScope(Rows);
    auto State = MakeUnique<FP4MaterialFamilySources::FImpl>();
    State->Registry = Registry;
    State->Master = FamilySourceWide(Family.Master.Package);
    if (UsedCache)
        State->UsedCache = Cache;
    int64 Total = 0;
    for (const auto& Member : Family.Members)
    {
        const FString Package = FamilySourceWide(Member.Package);
        if (MissingSources.Contains(Package))
            continue;
        // Budget before reading, including explicit paths, then budget exact bytes.
        for (const auto& Source : Rows)
            if (Source.IsValid() && Source->Package == Package)
            {
                const int64 HeaderSize = IFileManager::Get().FileSize(*Source->Header),
                            BodySize = IFileManager::Get().FileSize(*Source->Body);
                if (HeaderSize <= 0 || BodySize <= 0 || HeaderSize > 64 * 1024 * 1024 || BodySize > 64 * 1024 * 1024 ||
                    HeaderSize + BodySize > 128 * 1024 * 1024 - Total)
                    return Refused(
                        TEXT("Material family source bytes exceed their bound or are unavailable: ") + Package);
                break;
            }
        FP4KadanSourceBinding Binding;
        if (!P4KadanReadBoundSource(Package, Binding, Error))
            return Refused(Error);
        Total += int64(Binding.Header.Num()) + Binding.Body.Num();
        if (Total > 128 * 1024 * 1024)
            return Refused(TEXT("Material family source set exceeds its byte bound"));
        // Validate available actual edges/owners independently of the missing
        // request list. A malformed supplied member never triggers acquisition.
        const std::vector<uint8> HeaderBytes(Binding.Header.GetData(), Binding.Header.GetData() + Binding.Header.Num());
        const std::vector<uint8> BodyBytes(Binding.Body.GetData(), Binding.Body.GetData() + Binding.Body.Num());
        FamilySource::FMaterialFamilySourceMember Checked;
        P4MapKit::FReadError SourceError;
        if (!FamilySource::MaterialFamilySourceDetail::SourceMember(
                Member, P4MapKit::ReadPackage(HeaderBytes, BodyBytes), BodyBytes, Checked, SourceError))
            return Refused(FamilySourceWide(SourceError.Code));
        State->Sources.Add(MoveTemp(Binding));
        State->Classes.Add(Package, FamilySourceWide(Member.Class));
    }
    if (UsedCache)
    {
        FString Now;
        if (!P4KadanRetainedConversionCacheCurrent(Settings, Now, Error))
            return Refused(Error);
        if (!FPaths::IsSamePath(Cache, Now))
            return Refused(TEXT("Material family cache changed during its read"));
    }
    for (const auto& Source : State->Sources)
        if (!P4KadanSourceBindingCurrent(Source, Error))
            return Refused(Error);
    if (!Registry->IsCurrent(FamilySourceTools(Settings), Settings.GamePaks, Error))
        return Refused(Error);
    if (MissingSources.Num())
    {
        MissingSources.Sort();
        Error = TEXT("The complete current material family needs additional read-only sources");
        if (AvailableSourceBytes)
            *AvailableSourceBytes = Total;
        return EP4MaterialFamilySourceRead::MissingSources;
    }
    FamilySource::FMaterialFamilySourceWitness Witness;
    P4MapKit::FReadError ReadError;
    const auto Resolve = [&](const std::string& Package, FamilySource::FMaterialFamilyRawSource& Raw)
    {
        for (const auto& Source : State->Sources)
            if (FamilySourceUtf8(Source.Package) == Package)
            {
                Raw.Header = std::make_shared<const std::vector<uint8>>(
                    Source.Header.GetData(), Source.Header.GetData() + Source.Header.Num());
                Raw.Body = std::make_shared<const std::vector<uint8>>(
                    Source.Body.GetData(), Source.Body.GetData() + Source.Body.Num());
                return true;
            }
        return false;
    };
    if (!FamilySource::ValidateMaterialFamilySources(Family, Resolve, Witness, ReadError))
        return Refused(FamilySourceWide(ReadError.Code));
    State->SourceDigest = FamilySourceWide(Witness.SourceSetSha256);
    State->OwnerCount = int32(Witness.Owners.size());
    for (const auto& Pair : Witness.Members)
        State->Owners.Add(FamilySourceWide(Pair.first), FamilySourceWide(Pair.second.Owner));
    TSharedPtr<FP4MaterialFamilySources> Ready(new FP4MaterialFamilySources(MoveTemp(State)));
    if (!Ready->Current(Settings, Error))
        return Refused(Error);
    Out = Ready;
    if (AvailableSourceBytes)
        *AvailableSourceBytes = Total;
    return EP4MaterialFamilySourceRead::Ready;
}

// What the import reads before it plans: the game folder's packages, the game's containers and the
// packages converted from them, and what the project already holds.

#include "P4KadanImportInternal.h"
#include "P4GameBuild.h"
#include "P4GamePaths.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

using namespace P4KadanImport;

namespace P4KadanImport
{
bool RetainAcquiredWorldSource(const FP4KadanImportSettings& Settings, const FP4KadanContainerIndex& Index,
    FP4KadanSourceAsset& Asset, FString& Error)
{
    if (Asset.Kind != TEXT("World"))
        return true;
    // Both RequestedOriginals (selected roots) and OriginalPackages (BringIn)
    // are replaceable staging. Resolve the actual durable mounted cache row.
    FString Cache, Mounted;
    FP4OriginalWorldSourcePtr Staged, Retained;
    if (!P4KadanRetainedConversionCacheCurrent(Settings, Cache, Error) ||
        !Index.Resolve(Asset.Package, Mounted, Error) ||
        !P4ReadOriginalWorldSource(Asset.Header, Asset.Body, Asset.Package, Staged, Error) ||
        !P4ReadOriginalWorldSource(
            Cache / Mounted, FPaths::ChangeExtension(Cache / Mounted, TEXT("uexp")), Asset.Package, Retained, Error) ||
        !Staged->Current(Error) || !Retained->Current(Error))
        return false;
    if (!P4ExactNativeString(Staged->SourceDigest(), Retained->SourceDigest()))
    {
        Error = TEXT("acquired-world-retained-source-bytes-differ");
        return false;
    }
    Asset.Header = Retained->HeaderPath();
    Asset.Body = Retained->BodyPath();
    return true;
}

bool ExistingReal(const FString& Package)
{
    const FName StandInTag(TEXT("P4BlueprintStandIn"));
    // Protect actual live assets, including an unsaved user asset that has
    // not been registered yet. Inspect only loaded metadata; presence checks
    // must not load packages or create metadata objects.
    if (UPackage* Loaded = FindPackage(nullptr, *Package))
    {
        TArray<UObject*> Inside;
        GetObjectsWithOuter(Loaded, Inside, false);
        UMetaData* Metadata = nullptr;
        for (UObject* Object : Inside)
            if (!Object->IsPendingKill())
                if (UMetaData* Found = Cast<UMetaData>(Object))
                {
                    Metadata = Found;
                    break;
                }
        bool LiveAssets = false;
        for (UObject* Object : Inside)
            if (!Object->IsPendingKill() && Object->IsAsset())
            {
                LiveAssets = true;
                if (P4BlueprintReconstructionIncomplete(Object))
                    continue;
                if (!Metadata || FString(Metadata->GetValue(Object, StandInTag)) != TEXT("1"))
                    return true;
            }
        // Live roles are authoritative over older registry tags.
        if (LiveAssets)
            return false;
    }
    // Registry records can outlive deleted/unloaded files. They describe
    // identity, not current existence; they alone cannot finish a resume.
    // A registry can also name an optional plugin that is not mounted here.
    // Keep that source refusal without calling the engine's error-logging
    // disk probe; loaded user assets above remain authoritative.
    FString Filename;
    if (!FPackageName::TryConvertLongPackageNameToFilename(Package, Filename))
        return false;
    if (!FPackageName::DoesPackageExist(Package))
        return false;
    TArray<FAssetData> Assets;
    FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry")
        .Get()
        .GetAssetsByPackageName(FName(*Package), Assets, false);
    for (const FAssetData& Asset : Assets)
    {
        FString Marker;
        if (Asset.GetTagValue(TEXT("P4BlueprintReconstructionIncomplete"), Marker) && Marker == TEXT("1"))
            continue;
        if (!Asset.GetTagValue(StandInTag, Marker) || Marker != TEXT("1"))
            return true;
    }
    // Preserve an existing user package even before the registry scans it.
    // A known on-disk stand-in remains eligible for its guarded fill.
    return Assets.Num() == 0;
}

bool ExistingRetainedNativeKind(const FString& Package)
{
    // Class identity only: keep the same no-load presence-query contract.
    const auto StorageClass = [](const UClass* Class)
    {
        if (!Class)
            return false;
        if (RetainedNativeKind(Class->GetFName()))
            return true;
        // This one qualified editor storage class cooks back to ParticleSystem.
        // Keep canonical source kinds separate and refuse arbitrary subclasses.
        return Class->GetPathName() == TEXT("/Script/Engine.P4RecoveredParticleSystem") && Class->GetSuperClass() &&
            Class->GetSuperClass()->GetPathName() == TEXT("/Script/Engine.ParticleSystem");
    };
    if (UPackage* Loaded = FindPackage(nullptr, *Package))
    {
        TArray<UObject*> Inside;
        GetObjectsWithOuter(Loaded, Inside, false);
        for (UObject* Object : Inside)
            if (!Object->IsPendingKill() && Object->IsAsset())
                return StorageClass(Object->GetClass());
    }
    TArray<FAssetData> Assets;
    FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry")
        .Get()
        .GetAssetsByPackageName(FName(*Package), Assets, false);
    for (const FAssetData& Asset : Assets)
    {
        if (RetainedNativeKind(Asset.AssetClass))
            return true;
        if (Asset.AssetClass == TEXT("P4RecoveredParticleSystem") &&
            StorageClass(FindObject<UClass>(nullptr, TEXT("/Script/Engine.P4RecoveredParticleSystem"))))
            return true;
    }
    return false;
}

bool ExistingLevelPackage(const FString& Package)
{
    // A retained level is a dependency boundary, not the end of the source walk.
    // Reading its current original source can repair classes absent from an earlier
    // import without replacing that level or the creator's saved edits. No asset loads.
    if (UPackage* Loaded = FindPackage(nullptr, *Package))
        if (UWorld* World = UWorld::FindWorldInPackage(Loaded))
            if (!World->IsPendingKill())
                return true;
    FString File;
    return FPackageName::TryConvertLongPackageNameToFilename(Package, File, FPackageName::GetMapPackageExtension()) &&
        IFileManager::Get().FileExists(*File);
}

// A Blueprint class the project already has, from an earlier import into it. Its game source is read
// again, as a placed thing's class is compared with the game's file of it and the level is made with
// what that file says; the project's class is kept.
bool ExistingBlueprintClass(const FString& Package, bool IncludeWidget)
{
    if (UPackage* Loaded = FindPackage(nullptr, *Package))
    {
        TArray<UObject*> Inside;
        GetObjectsWithOuter(Loaded, Inside, false);
        for (UObject* Object : Inside)
            if (!Object->IsPendingKill() && Object->IsAsset())
                return Object->GetClass()->GetFName() == TEXT("Blueprint") ||
                    Object->GetClass()->GetFName() == TEXT("AnimBlueprint") ||
                    (IncludeWidget && Object->GetClass()->GetFName() == TEXT("WidgetBlueprint"));
    }
    TArray<FAssetData> Assets;
    FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry")
        .Get()
        .GetAssetsByPackageName(FName(*Package), Assets, false);
    for (const FAssetData& Asset : Assets)
        if (Asset.AssetClass == TEXT("Blueprint") || Asset.AssetClass == TEXT("AnimBlueprint") ||
            (IncludeWidget && Asset.AssetClass == TEXT("WidgetBlueprint")))
            return true;
    return false;
}

bool ExtractFromContainers(const FP4KadanImportSettings& Settings, const FP4KadanContainerIndex& Index,
    const FString& Package, const FString& Into, FString& Error)
{
    return P4KadanConvertOriginal(Settings, Index, Package, Into, Error);
}

// What the game's containers and retoc are now: every container file and retoc, by name, size and time.
FString ContainerKey(const FP4KadanImportSettings& Settings)
{
    FString Key = FPaths::ConvertRelativePathToFull(Settings.GamePaks) + TEXT("\n") +
        FPaths::ConvertRelativePathToFull(Settings.Retoc) + TEXT("\n");
    const auto Stamp = [&](const FString& File)
    {
        const FFileStatData Data = IFileManager::Get().GetStatData(*File);
        Key += FPaths::GetCleanFilename(File) + FString::Printf(TEXT(" %lld "), Data.FileSize) +
            Data.ModificationTime.ToString() + TEXT("\n");
    };
    Stamp(FPaths::ConvertRelativePathToFull(Settings.Retoc));
    TArray<FString> Containers;
    for (const TCHAR* Pattern : {TEXT("*.utoc"), TEXT("*.ucas"), TEXT("*.pak")})
    {
        TArray<FString> Found;
        IFileManager::Get().FindFiles(Found, *(Settings.GamePaks / Pattern), true, false);
        Containers.Append(Found);
    }
    Containers.Sort();
    for (const FString& Container : Containers)
        Stamp(Settings.GamePaks / Container);
    return Key;
}

} // namespace P4KadanImport

bool P4KadanReadContainerIndex(
    const FP4KadanImportSettings& Settings, FP4KadanContainerIndex& Index, FString& Error, bool* FromCache)
{
    // retoc lists the containers once per editor session and set of containers, not once per import.
    if (FromCache)
        *FromCache = false;
    static FCriticalSection Guard;
    static TMap<FString, FP4KadanContainerIndex> Read;
    const FString Key = ContainerKey(Settings);
    {
        FScopeLock Lock(&Guard);
        if (const FP4KadanContainerIndex* Known = Read.Find(Key))
        {
            Index = *Known;
            Error.Reset();
            if (FromCache)
                *FromCache = true;
            return true;
        }
    }
    TArray<uint8> Listing;
    if (!P4RunExportTool(FPaths::ConvertRelativePathToFull(Settings.Retoc),
            {TEXT("list"), FPaths::ConvertRelativePathToFull(Settings.GamePaks), TEXT("--path")}, 600, Listing, Error))
        return false;
    if (Listing.Contains(uint8(0)))
    {
        Error = TEXT("The container index contains a null byte.");
        return false;
    }
    Listing.Add(0);
    if (!Index.Read(UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(Listing.GetData())), Error))
        return false;
    FScopeLock Lock(&Guard);
    Read.Add(Key, Index);
    return true;
}

namespace P4KadanImport
{
FString ConfiguredConversionCache(const FP4KadanImportSettings& Settings)
{
    const FString Cache = Settings.ConvertedCache.IsEmpty()
        ? FPaths::ProjectSavedDir() / TEXT("Kadan/ConvertedOriginals")
        : Settings.ConvertedCache;
    // Only a project-owned Saved subfolder may be invalidated; an external folder is never removed.
    const FString Saved = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()).Replace(TEXT("\\"), TEXT("/"));
    const FString AbsoluteCache = FPaths::ConvertRelativePathToFull(Cache).Replace(TEXT("\\"), TEXT("/"));
    const FString Scope = Saved.EndsWith(TEXT("/")) ? Saved : Saved + TEXT("/");
    if (!AbsoluteCache.StartsWith(Scope, ESearchCase::IgnoreCase) || AbsoluteCache == Scope)
        return FString();
    return Cache;
}

FString CurrentConversionCacheIdentity(const FP4KadanImportSettings& Settings)
{
    // Same conversion options and same source paths, sizes and times; other inputs cannot serve this cache.
    return TEXT("retoc to-legacy --no-shaders --no-script-objects --version ") + P4GameEngineFor(Settings.GamePaks) +
        TEXT("\n") + ContainerKey(Settings);
}

} // namespace P4KadanImport

bool P4KadanRetainedConversionCacheCurrent(const FP4KadanImportSettings& Settings, FString& Cache, FString& Error)
{
    Cache = ConfiguredConversionCache(Settings);
    Error.Reset();
    if (Cache.IsEmpty())
    {
        Error = TEXT("The retained conversion cache is outside this project's Saved folder.");
        return false;
    }
    const FString Stamp = Cache / TEXT("converted-from.txt");
    const int64 Bytes = IFileManager::Get().FileSize(*Stamp);
    FString Prior;
    if (!IFileManager::Get().DirectoryExists(*Cache) || Bytes < 0 || Bytes > 65536 ||
        !FFileHelper::LoadFileToString(Prior, *Stamp) || Prior != CurrentConversionCacheIdentity(Settings))
    {
        Cache.Reset();
        Error = TEXT("The retained conversion cache does not match the current selected containers and retoc.");
        return false;
    }
    return true;
}

FString P4KadanConversionCache(const FP4KadanImportSettings& Settings)
{
    const FString Cache = ConfiguredConversionCache(Settings);
    if (Cache.IsEmpty())
        return FString();
    const FString Key = CurrentConversionCacheIdentity(Settings);
    const FString KeyFile = Cache / TEXT("converted-from.txt");
    FString Prior;
    if (!FFileHelper::LoadFileToString(Prior, *KeyFile) || Prior != Key)
    {
        // Other containers or another retoc: nothing cached from before can be used. A file still in use survives a
        // partial delete, so the new identity is stamped only on a folder that is gone; else nothing is cached.
        IFileManager::Get().DeleteDirectory(*Cache, false, true);
        if (IFileManager::Get().DirectoryExists(*Cache))
            return FString();
        if (!IFileManager::Get().MakeDirectory(*Cache, true) ||
            !FFileHelper::SaveStringToFile(Key, *KeyFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            return FString();
    }
    return Cache;
}

namespace P4KadanImport
{
bool ConvertOriginalInCache(const FP4KadanImportSettings& Settings, const FP4KadanContainerIndex& Index,
    const FString& Package, const FString& Into, const FString& Cache, FString& Error, bool* FromCache)
{
    if (FromCache)
        *FromCache = false;
    FString Path;
    if (!Index.Resolve(Package, Path, Error))
        return false;
    // A package is converted from the containers once per project and kept (Saved/Kadan/ConvertedOriginals),
    // so the next map, or the next import, that needs it does not run retoc for it again.
    const FString Converted = (Cache.IsEmpty() ? Into : Cache) / Path;
    FString CacheError;
    if (!Cache.IsEmpty() && P4KadanExtractedPackageReady(Converted, Package, CacheError))
    {
        if (FromCache)
            *FromCache = true;
    }
    else
    {
        // retoc's filter uses exact case-sensitive containment. Use the actual
        // observed mounted header path, never a guessed spelling of the request.
        if (!P4KadanConvertMountedOriginal(Settings, Path, Cache.IsEmpty() ? Into : Cache, Error))
            return false;
        if (Cache.IsEmpty())
            return P4KadanExtractedPackageReady(Into / Path, Package, Error);
        if (!P4KadanExtractedPackageReady(Converted, Package, Error))
            return false;
    }
    // The package's files go into Into as hard links to the converted copy (a copy where a link cannot be made).
    for (const FString& Extension :
        {FPaths::GetExtension(Path, true), FString(TEXT(".uexp")), FString(TEXT(".ubulk")), FString(TEXT(".uptnl"))})
    {
        const FString From = FPaths::ChangeExtension(Converted, Extension),
                      To = FPaths::ChangeExtension(Into / Path, Extension);
        if (!IFileManager::Get().FileExists(*From))
            continue;
        IFileManager::Get().Delete(*To, false, true, true);
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(To), true);
        bool Linked = false;
#if PLATFORM_WINDOWS
        Linked = ::CreateHardLinkW(
                     *FPaths::ConvertRelativePathToFull(To), *FPaths::ConvertRelativePathToFull(From), nullptr) != 0;
#endif
        if (!Linked && IFileManager::Get().Copy(*To, *From) != COPY_OK)
        {
            Error = TEXT("The converted package could not be placed for import: ") + To;
            return false;
        }
    }
    return P4KadanExtractedPackageReady(Into / Path, Package, Error);
}

} // namespace P4KadanImport

bool P4KadanConvertOriginal(const FP4KadanImportSettings& Settings, const FP4KadanContainerIndex& Index,
    const FString& Package, const FString& Into, FString& Error, bool* FromCache)
{
    return ConvertOriginalInCache(Settings, Index, Package, Into, P4KadanConversionCache(Settings), Error, FromCache);
}

bool P4KadanScanGame(const FString& Folder, TArray<TSharedPtr<FP4KadanSourceAsset>>& Assets, TArray<FString>& Problems)
{
    Assets.Empty();
    Problems.Empty();
    if (Folder.IsEmpty() || !IFileManager::Get().DirectoryExists(*Folder))
    {
        Problems.Add(TEXT("Choose the folder containing your extracted game packages."));
        return false;
    }
    TArray<FString> Files;
    IFileManager::Get().FindFilesRecursive(Files, *Folder, TEXT("*.uasset"), true, false);
    IFileManager::Get().FindFilesRecursive(Files, *Folder, TEXT("*.umap"), true, false, false);
    Files.Sort();
    if (Files.Num() == 0)
    {
        Problems.Add(TEXT("This folder contains no game package files (.uasset or .umap)."));
        return false;
    }
    FScopedSlowTask Progress(Files.Num(), FText::FromString(TEXT("Reading game package names and references")));
    if (!IsRunningCommandlet())
        Progress.MakeDialog(true);
    TSet<FName> Seen;
    for (const FString& File : Files)
    {
        Progress.EnterProgressFrame(1.f, FText::FromString(FPaths::GetCleanFilename(File)));
        if (Progress.ShouldCancel())
        {
            Problems.Add(TEXT("The package scan was cancelled. The packages read so far are still listed."));
            return false;
        }
        const int64 HeaderSize = IFileManager::Get().FileSize(*File);
        const FString Body = FPaths::ChangeExtension(File, TEXT("uexp"));
        const int64 BodySize = IFileManager::Get().FileSize(*Body);
        TArray<uint8> Header;
        if (HeaderSize < 0 || HeaderSize > 64 * 1024 * 1024 ||
            !FFileHelper::LoadFileToArray(Header, *File, FILEREAD_Silent))
        {
            Problems.Add(FPaths::GetCleanFilename(File) + TEXT(" cannot be read."));
            continue;
        }
        const auto Read = P4MapKit::ReadPackageHeader(
            std::vector<uint8_t>(Header.GetData(), Header.GetData() + Header.Num()),
            std::size_t(FMath::Max<int64>(0, BodySize)));
        if (!Read.Error.Code.empty())
        {
            UE_LOG(LogTemp, Display, TEXT("Kadan package scan %s %s"), *File, UTF8_TO_TCHAR(Read.Error.Code.c_str()));
            Problems.Add(FPaths::GetCleanFilename(File) + TEXT(" is not a game package this import reads."));
            continue;
        }
        auto Asset = MakeShared<FP4KadanSourceAsset>();
        Asset->Package = UTF8_TO_TCHAR(Read.Package.c_str());
        if (!P4IsContentPackageIgnoringCase(Asset->Package))
            continue;
        if (!FPackageName::IsValidLongPackageName(Asset->Package))
        {
            Problems.Add(Asset->Package + TEXT(" is not a valid package name."));
            continue;
        }
        if (Seen.Contains(FName(*Asset->Package)))
        {
            Problems.Add(Asset->Package + TEXT(" is in the folder twice. Choose a folder with one copy of the game."));
            continue;
        }
        Seen.Add(FName(*Asset->Package));
        Asset->Header = File;
        Asset->Body = Body;
        Asset->SourceBytes = HeaderSize + FMath::Max<int64>(0, BodySize);
        for (const TCHAR* Extension : {TEXT("ubulk"), TEXT("uptnl")})
            Asset->SourceBytes += FMath::Max<int64>(
                0, IFileManager::Get().FileSize(*FPaths::ChangeExtension(File, Extension)));
        // The asset named like its package decides the kind. A map also holds its level Blueprint's class
        // (<map>_C), so a class named that way only decides when no asset has the package's name.
        const FString Leaf = FPackageName::GetLongPackageAssetName(Asset->Package);
        FString FirstKind, ClassKind;
        for (const auto& Export : Read.Exports)
        {
            if (Export.Outer != 0)
                continue;
            std::string Class;
            if (!Read.Reference(Export.Class, Class))
                continue;
            FString Kind = UTF8_TO_TCHAR(Class.c_str());
            int32 Dot = INDEX_NONE;
            if (Kind.FindLastChar(TEXT('.'), Dot))
                Kind = Kind.Mid(Dot + 1);
            const FString Name = UTF8_TO_TCHAR(Export.Name.c_str());
            if (FirstKind.IsEmpty())
                FirstKind = KindName(Kind);
            if (Name == Leaf)
            {
                Asset->Kind = KindName(Kind);
                break;
            }
            if (ClassKind.IsEmpty() && Name == Leaf + TEXT("_C"))
                ClassKind = KindName(Kind);
        }
        if (Asset->Kind.IsEmpty())
            Asset->Kind = ClassKind.IsEmpty() ? FirstKind : ClassKind;
        TSet<FName> HardIdentities, SoftIdentities;
        for (const auto& Import : Read.Imports)
        {
            const FString Name = UTF8_TO_TCHAR(Import.Name.c_str());
            const FName Identity(*Name);
            if (Import.Outer == 0 && P4IsContentPackageIgnoringCase(Name) && Identity != FName(*Asset->Package) &&
                !HardIdentities.Contains(Identity))
            {
                HardIdentities.Add(Identity);
                Asset->Dependencies.Add(Name);
            }
        }
        for (const std::string& Soft : Read.SoftPackageReferences)
        {
            const FString Name = UTF8_TO_TCHAR(Soft.c_str());
            const FName Identity(*Name);
            if (!P4IsValidContentPackageIgnoringCase(Name) || Identity == FName(*Asset->Package) ||
                HardIdentities.Contains(Identity) || SoftIdentities.Contains(Identity))
                continue;
            SoftIdentities.Add(Identity);
            Asset->Dependencies.Add(Name);
            Asset->SoftDependencies.Add(Name);
        }
        Asset->Dependencies.Sort();
        Asset->SoftDependencies.Sort();
        if (Asset->Kind.IsEmpty())
            Asset->Kind = TEXT("Unknown");
        Assets.Add(Asset);
    }
    return true;
}

bool P4KadanFindImportedPackages(const TSet<FString>& Catalog, TSet<FString>& Imported)
{
    // Query what the project actually holds, not all packages in the game's catalog. Old releases
    // kept provenance in package metadata, so those assets must be loaded to read their records.
    auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.SearchAllAssets(true);
    TArray<FAssetData> Assets;
    Registry.GetAllAssets(Assets);
    TArray<FAssetData> Candidates;
    for (const FAssetData& Asset : Assets)
        if (Catalog.Contains(Asset.PackageName.ToString()))
            Candidates.Add(Asset);
    TSet<FString> Found;
    FScopedSlowTask Progress(Candidates.Num(), FText::FromString(TEXT("Finding previously imported game content…")));
    Progress.MakeDialog(true);
    for (const FAssetData& Data : Candidates)
    {
        Progress.EnterProgressFrame(1.f, FText::FromName(Data.PackageName));
        if (Progress.ShouldCancel())
            return false;
        if (Found.Contains(Data.PackageName.ToString()) || !FPackageName::DoesPackageExist(Data.PackageName.ToString()))
            continue;
        UObject* Asset = Data.GetAsset();
        if (!Asset || P4HasCreatorSource(Asset))
            continue;
        const FString Package = Asset->GetOutermost()->GetName();
        FString Source, Error;
        FP4OriginalRecord Record;
        if ((P4HasIntrinsicGameSource(Asset, &Source) && Source == Package) ||
            (P4ReadOriginalRecord(Asset, Record, Error) && Record.Package == Package))
            Found.Add(Package);
    }
    Imported = MoveTemp(Found);
    return true;
}

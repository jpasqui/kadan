#include "P4KadanLibrary.h"
#include "P4KadanImport.h"
#include "P4KadanImportInternal.h"
#include "P4ExportContainer.h"
#include "P4NativeHash.h"
#include "AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

namespace
{
// The files a package can have. A level (.umap) never goes in: an import always builds its levels.
const TCHAR* const PackageFiles[] = {TEXT(".uasset"), TEXT(".uexp"), TEXT(".ubulk"), TEXT(".uptnl")};
// Written last: an entry without its list of what it needs is not finished and is never used. A hard reference
// is one line; a soft one starts with Soft. A Blueprint names its sounds and effects by soft reference, and a
// level needs them in the project to place the Blueprint, so they come along when the library has them.
const TCHAR* const Needs = TEXT(".needs");
const TCHAR* const Soft = TEXT("~");

bool Wanted()
{
    const FString Setting = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_LIBRARY"));
    if (Setting == TEXT("0"))
        return false;
    return !GIsAutomationTesting || Setting == TEXT("1");
}

// The Kadan build: the bytes of every module of Kadan and of the game's classes (the P4Stubs plugin), so that
// every build, released or not, has its own library. A Blueprint is built by several of them.
FString BuildDigest()
{
    static const FString Digest = []()
    {
        FString Combined;
        for (const TCHAR* Name : {TEXT("Kadan"), TEXT("P4Stubs")})
        {
            const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(Name);
            if (!Plugin.IsValid())
                return FString();
            const FString Binaries = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Binaries/Win64"));
            TArray<FString> Modules;
            IFileManager::Get().FindFiles(Modules, *(Binaries / TEXT("UE4Editor-*.dll")), true, false);
            Modules.Sort();
            if (!Modules.Num())
                return FString();
            for (const FString& Module : Modules)
            {
                int64 Size = 0;
                FString Out;
                if (!P4HashFile(Binaries / Module, 512ll * 1024 * 1024, Size, Out))
                    return FString();
                Combined += Name + FString(TEXT("/")) + Module + TEXT(" ") + Out + TEXT("\n");
            }
        }
        FString Out;
        return P4HashUtf8(Combined, Out) ? Out : FString();
    }();
    return Digest;
}

FString ProjectBase(const FString& Package)
{
    FString Filename;
    return FPackageName::TryConvertLongPackageNameToFilename(Package, Filename)
        ? FPaths::ConvertRelativePathToFull(Filename)
        : FString();
}

bool GamePackage(const FString& Package)
{
    return Package.StartsWith(TEXT("/Game/"));
}

// Every build has its own library, so earlier builds' libraries are let go: the two used last are kept, for going
// back a build, and older ones are deleted. A library's folder time is the last time an import opened it.
void KeepRecentLibraries(const FString& Current)
{
    IFileManager& Files = IFileManager::Get();
    Files.SetTimeStamp(*Current, FDateTime::UtcNow());
    TArray<FString> Others;
    Files.FindFiles(Others, *(FPaths::GetPath(Current) / TEXT("*")), false, true);
    TArray<TPair<FDateTime, FString>> Older;
    for (const FString& Name : Others)
    {
        const FString Folder = FPaths::GetPath(Current) / Name;
        if (!FPaths::IsSamePath(Folder, Current))
            Older.Emplace(Files.GetTimeStamp(*Folder), Folder);
    }
    Older.Sort([](const TPair<FDateTime, FString>& A, const TPair<FDateTime, FString>& B) { return A.Key > B.Key; });
    for (int32 Index = 2; Index < Older.Num(); ++Index)
        Files.DeleteDirectory(*Older[Index].Value, false, true);
}
} // namespace

FP4KadanLibrary::FP4KadanLibrary(const FP4KadanImportSettings& Settings)
{
    if (!Wanted())
        return;
    // For Kadan's own development: a build that changes only how the library is read keeps using one it filled.
    const FString Pinned = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_KADAN_LIBRARY_FOLDER"));
    if (!Pinned.IsEmpty())
    {
        Root = FPaths::ConvertRelativePathToFull(Pinned);
        return;
    }
    const FString Build = BuildDigest();
    const FString Game = P4KadanImport::CurrentConversionCacheIdentity(Settings);
    FString Key;
    if (Build.IsEmpty() || Game.IsEmpty() || !P4HashUtf8(TEXT("kadan-library/3\n") + Build + TEXT("\n") + Game, Key))
        return;
    Root = FPaths::ConvertRelativePathToFull(
        FPaths::Combine(FPlatformProcess::UserSettingsDir(), TEXT("Kadan/Library"), Key.Left(16)));
    IFileManager::Get().MakeDirectory(*Root, true);
    KeepRecentLibraries(Root);
}

FString FP4KadanLibrary::Entry(const FString& Package) const
{
    return Root / Package.Mid(1);
}

bool FP4KadanLibrary::InProject(const FString& Package) const
{
    return FindPackage(nullptr, *Package) || FPackageName::DoesPackageExist(Package);
}

bool FP4KadanLibrary::ReadDependencies(const FString& Package, TArray<FString>& Out) const
{
    Out.Reset();
    return FFileHelper::LoadFileToStringArray(Out, *(Entry(Package) + Needs));
}

bool FP4KadanLibrary::Closure(const FString& Package, TArray<FString>& Missing, TSet<FString>& Visiting)
{
    if (Seeded.Contains(Package) || Visiting.Contains(Package) || InProject(Package))
        return true;
    if (Refused.Contains(Package))
        return false;
    TArray<FString> Dependencies;
    if (!ReadDependencies(Package, Dependencies))
        return false;
    Visiting.Add(Package);
    for (const FString& Dependency : Dependencies)
    {
        if (Dependency.IsEmpty())
            continue;
        if (!Dependency.StartsWith(Soft))
        {
            if (!Closure(Dependency, Missing, Visiting))
                return false;
            continue;
        }
        // A soft reference the library cannot complete is left to the import, as without a library.
        TArray<FString> WithSoft = Missing;
        TSet<FString> SoftVisiting = Visiting;
        if (Closure(Dependency.Mid(1), WithSoft, SoftVisiting))
        {
            Missing = MoveTemp(WithSoft);
            Visiting = MoveTemp(SoftVisiting);
        }
    }
    Missing.AddUnique(Package);
    return true;
}

bool FP4KadanLibrary::Seed(const FString& Package)
{
    if (!Enabled() || !GamePackage(Package) || Refused.Contains(Package))
        return false;
    if (Seeded.Contains(Package) || InProject(Package))
        return true;
    TArray<FString> Missing;
    TSet<FString> Visiting;
    if (!Closure(Package, Missing, Visiting))
    {
        Refused.Add(Package);
        return false;
    }
    TArray<FString> Copied;
    for (const FString& One : Missing)
    {
        const FString To = ProjectBase(One);
        bool Whole = !To.IsEmpty();
        for (const TCHAR* Extension : PackageFiles)
        {
            const FString From = Entry(One) + Extension;
            if (!Whole || !IFileManager::Get().FileExists(*From))
                continue;
            Whole = P4CopyFileWhole(From, To + Extension);
            if (Whole)
                Copied.Add(To + Extension);
        }
        if (!Whole)
        {
            for (const FString& File : Copied)
                IFileManager::Get().Delete(*File, false, true, true);
            Refused.Add(Package);
            return false;
        }
    }
    for (const FString& File : Copied)
        if (File.EndsWith(TEXT(".uasset")))
            SeededFiles.Add(File);
    Seeded.Append(Missing);
    return true;
}

TArray<FString> FP4KadanLibrary::TakeSeededFiles()
{
    return MoveTemp(SeededFiles);
}

int32 FP4KadanLibrary::Publish(const TMap<FString, FString>& Imported, TArray<FString>& Warnings)
{
    if (!Enabled())
        return 0;
    TArray<FString> Packages, Headers;
    for (const auto& Pair : Imported)
    {
        const FString Header = ProjectBase(Pair.Key) + TEXT(".uasset");
        if (Pair.Value != TEXT("World") && GamePackage(Pair.Key) &&
            !IFileManager::Get().FileExists(*(Entry(Pair.Key) + Needs)) && IFileManager::Get().FileExists(*Header))
        {
            Packages.Add(Pair.Key);
            Headers.Add(Header);
        }
    }
    if (!Packages.Num())
        return 0;
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.ScanFilesSynchronous(Headers, true);
    int32 Kept = 0, Failed = 0;
    for (const FString& Package : Packages)
    {
        // A stand-in or an unfinished Blueprint is not the game's asset yet.
        TArray<FAssetData> Assets;
        Registry.GetAssetsByPackageName(FName(*Package), Assets, false);
        bool Finished = Assets.Num() > 0;
        for (const FAssetData& Asset : Assets)
        {
            FString Marker;
            Finished &= !(Asset.GetTagValue(TEXT("P4BlueprintStandIn"), Marker) && Marker == TEXT("1")) &&
                !(Asset.GetTagValue(TEXT("P4BlueprintReconstructionIncomplete"), Marker) && Marker == TEXT("1"));
        }
        if (!Finished)
            continue;
        TArray<FName> Hard, Softly;
        Registry.GetDependencies(FName(*Package), Hard, UE::AssetRegistry::EDependencyCategory::Package,
            UE::AssetRegistry::EDependencyQuery::Hard);
        Registry.GetDependencies(FName(*Package), Softly, UE::AssetRegistry::EDependencyCategory::Package,
            UE::AssetRegistry::EDependencyQuery::Soft);
        // Only what the project has: a package the game does not ship is missing from every import, and the
        // import does without it, so the library must not wait for it either.
        const auto Listed = [&](const FName& Dependency)
        { return GamePackage(Dependency.ToString()) && Dependency.ToString() != Package && InProject(Dependency.ToString()); };
        TArray<FString> NeedsList;
        for (const FName& Dependency : Hard)
            if (Listed(Dependency))
                NeedsList.Add(Dependency.ToString());
        for (const FName& Dependency : Softly)
            if (Listed(Dependency) && !Hard.Contains(Dependency))
                NeedsList.Add(Soft + Dependency.ToString());
        const FString From = ProjectBase(Package), To = Entry(Package);
        // Another editor on this computer can be keeping the same package, or reading it the moment it is finished.
        // One keeps at a time, and an entry finished in the meantime stays: replacing it could fail on a reader.
        FSystemWideCriticalSection OneAtATime(TEXT("KadanLibrary_") + FPaths::GetCleanFilename(Root), FTimespan::FromMinutes(1));
        if (!OneAtATime.IsValid())
        {
            ++Failed;
            continue;
        }
        if (IFileManager::Get().FileExists(*(To + Needs)))
            continue;
        bool Whole = true;
        for (const TCHAR* Extension : PackageFiles)
            if (Whole && IFileManager::Get().FileExists(*(From + Extension)))
                Whole = P4CopyFileWhole(From + Extension, To + Extension);
        const FString List = To + Needs + TEXT(".part");
        Whole = Whole && FFileHelper::SaveStringArrayToFile(NeedsList, *List, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) &&
            IFileManager::Get().Move(*(To + Needs), *List, true, true, false, true);
        Whole ? ++Kept : ++Failed;
        // The project already has the asset, so the import goes on; the rest would not fit in the library either.
        if (!Whole && !P4DiskFullMessage(Root).IsEmpty())
            break;
    }
    if (Failed)
    {
        const FString Full = P4DiskFullMessage(Root);
        Warnings.AddUnique(FString::Printf(TEXT("%d imported game assets could not be kept for other projects in %s.%s%s"),
            Failed, *Root, Full.IsEmpty() ? TEXT("") : TEXT(" "), *Full));
    }
    return Kept;
}

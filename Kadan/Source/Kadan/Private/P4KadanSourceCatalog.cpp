#include "P4KadanSourceCatalog.h"
#include "P4GameNames.h"
#include "P4KadanContainerIndex.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "P4GameBuild.h"

namespace
{
FString FullPath(const FString& Path)
{
    if (Path.IsEmpty())
        return FString();
    FString Full = FPaths::ConvertRelativePathToFull(Path);
    FPaths::NormalizeFilename(Full);
    if (!FPaths::CollapseRelativeDirectories(Full))
        return FString();
    FPaths::NormalizeDirectoryName(Full);
    return Full;
}
bool HasContainers(const FString& Folder)
{
    return P4HasKnownGameContainer(Folder);
}
bool ToolReady(const FString& Retoc, FString& Error)
{
    if (Retoc.IsEmpty())
    {
        Error = TEXT("Choose retoc to read the game's Paks inventory.");
        return false;
    }
    if (IFileManager::Get().FileSize(*FullPath(Retoc)) <= 0)
    {
        Error = TEXT("The selected retoc executable is missing or unreadable.");
        return false;
    }
    return true;
}
FString SourceIdentity(const FP4KadanImportSettings& Settings)
{
    FString Key = FullPath(Settings.GamePaks) + TEXT("\n") + FullPath(Settings.Retoc) + TEXT("\n");
    const auto Stamp = [&](const FString& File)
    {
        const FFileStatData Data = IFileManager::Get().GetStatData(*File);
        Key += FullPath(File) + FString::Printf(TEXT(" %lld "), Data.FileSize) + Data.ModificationTime.ToIso8601() +
            TEXT("\n");
    };
    Stamp(Settings.Retoc);
    TArray<FString> Names;
    for (const TCHAR* Pattern : {TEXT("*.pak"), TEXT("*.utoc"), TEXT("*.ucas")})
    {
        TArray<FString> Found;
        IFileManager::Get().FindFiles(Found, *(Settings.GamePaks / Pattern), true, false);
        Names.Append(Found);
    }
    Names.Sort();
    for (const FString& Name : Names)
        Stamp(Settings.GamePaks / Name);
    return Key;
}
} //namespace

bool P4KadanNormalizeGamePaks(const FString& Folder, FString& Out, FString& Error)
{
    Out.Reset();
    Error.Reset();
    const FString Root = FullPath(Folder);
    if (Root.IsEmpty() || !IFileManager::Get().DirectoryExists(*Root))
    {
        Error = TEXT("Choose your dumped game's romfs folder or its Paks folder.");
        return false;
    }
    TArray<FString> Found;
    for (const FString& Candidate : {Root, Root / TEXT("Paks"), Root / TEXT("Content/Paks"),
             Root / TEXT(P4_GAME_CONTENT_FOLDER "/Paks"), Root / TEXT("romfs/" P4_GAME_CONTENT_FOLDER "/Paks")})
        if (HasContainers(Candidate))
            Found.AddUnique(FullPath(Candidate));
    if (Found.Num() != 1)
    {
        Error = Found.Num() > 1
            ? TEXT("This folder contains more than one complete game Paks directory. Choose the one to use.")
            : TEXT("This folder must contain the game's container files together: Carrot4-Switch.pak, .utoc and .ucas "
                   "(Nintendo Switch) or pakchunk0-Sage.pak, .utoc and .ucas (Nintendo Switch 2 Edition).");
        return false;
    }
    Out = Found[0];
    return true;
}

FString P4KadanCatalogGameFolder()
{
    return FullPath(FPaths::ProjectSavedDir() / TEXT("Kadan/ConvertedOriginals"));
}
bool P4KadanIsCatalogGameFolder(const FString& Folder)
{
    return !Folder.IsEmpty() && FullPath(Folder).Equals(P4KadanCatalogGameFolder(), ESearchCase::IgnoreCase);
}

bool P4KadanPrepareCatalogImport(const FP4KadanImportSettings& Settings, FString& Error)
{
    Error.Reset();
    if (!P4KadanIsCatalogGameFolder(Settings.GameFolder) ||
        (!Settings.ConvertedCache.IsEmpty() && !P4KadanIsCatalogGameFolder(Settings.ConvertedCache)))
    {
        Error = TEXT("Catalog imports must use this project's owned Saved/Kadan/ConvertedOriginals cache.");
        return false;
    }
    FString Paks;
    if (!P4KadanNormalizeGamePaks(Settings.GamePaks, Paks, Error) || !ToolReady(Settings.Retoc, Error))
        return false;
    if (!Paks.Equals(FullPath(Settings.GamePaks), ESearchCase::IgnoreCase))
    {
        Error = TEXT("The catalog's original source must be its normalized Paks directory.");
        return false;
    }
    const FString Cache = P4KadanCatalogGameFolder();
    if (IFileManager::Get().DirectoryExists(*Cache) &&
        !IFileManager::Get().FileExists(*(Cache / TEXT("converted-from.txt"))))
    {
        TArray<FString> Entries;
        IFileManager::Get().FindFiles(Entries, *(Cache / TEXT("*")), true, true);
        if (Entries.Num())
        {
            Error = TEXT(
                "The converted source cache contains files without its ownership receipt. Choose a clean project cache.");
            return false;
        }
    }
    if (P4KadanConversionCache(Settings).IsEmpty())
    {
        Error = TEXT("The project's converted source cache could not be prepared.");
        return false;
    }
    return true;
}

bool P4KadanReadSourceCatalog(
    const FString& GameFolder, const FString& Retoc, FP4KadanSourceCatalog& Out, FString& Error)
{
    Out = {};
    Error.Reset();
    FString Paks;
    if (!P4KadanNormalizeGamePaks(GameFolder, Paks, Error) || !ToolReady(Retoc, Error))
        return false;
    FP4KadanImportSettings Settings;
    Settings.GamePaks = Paks;
    Settings.Retoc = FullPath(Retoc);
    Settings.GameFolder = P4KadanCatalogGameFolder();
    Settings.ConvertedCache = Settings.GameFolder;
    FP4KadanContainerIndex Index;
    bool FromCache = false;
    if (!P4KadanReadContainerIndex(Settings, Index, Error, &FromCache))
        return false;
    TArray<FP4KadanContainerPackage> Packages;
    if (!Index.Enumerate(Packages, Error))
        return false;
    FP4KadanSourceCatalog Catalog;
    Catalog.GameFolder = FullPath(GameFolder);
    Catalog.ImportSettings = Settings;
    Catalog.IndexFromCache = FromCache;
    Catalog.SourceIdentity = SourceIdentity(Settings);
    for (const auto& Package : Packages)
    {
        auto Row = MakeShared<FP4KadanSourceCatalogRow>();
        Row->Package = Package.Package;
        Row->ContainerPath = Package.Path;
        Row->Kind = Package.IsMap ? TEXT("World") : TEXT("Asset");
        Catalog.Rows.Add(Row);
    }
    Out = MoveTemp(Catalog);
    return true;
}

bool P4KadanSourceCatalogCurrent(const FP4KadanSourceCatalog& Catalog)
{
    if (!Catalog.Rows.Num() || Catalog.SourceIdentity.IsEmpty())
        return false;
    FString Paks, Error;
    return P4KadanNormalizeGamePaks(Catalog.GameFolder, Paks, Error) &&
        ToolReady(Catalog.ImportSettings.Retoc, Error) &&
        Paks.Equals(Catalog.ImportSettings.GamePaks, ESearchCase::IgnoreCase) &&
        SourceIdentity(Catalog.ImportSettings) == Catalog.SourceIdentity;
}

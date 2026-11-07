#pragma once
#include "P4GamePreviewNavigation.h"
#include "P4KadanImport.h"
#include "P4MapKitCore/export_package.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

// The game's own files for the preview tests that compare a rule over the game: the converted packages
// Play reads, read in place.
namespace P4GamePreviewTestGame
{
inline bool GameSettings(FP4KadanImportSettings& Out, FString& Error)
{
    const FString NativeRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    Out.GamePaks = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS"));
    Out.Retoc = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_RETOC"));
    if (Out.Retoc.IsEmpty() && !NativeRoot.IsEmpty())
        Out.Retoc = FPaths::GetPath(FPaths::GetPath(NativeRoot)) / TEXT("private/retoc/retoc.exe");
    if (Out.GamePaks.IsEmpty() || !IFileManager::Get().FileExists(*Out.Retoc))
    {
        Error = TEXT("The game's containers (P4_NATIVE_ORIGINAL_CONTAINERS) and retoc are required.");
        return false;
    }
    return true;
}

struct FPackage
{
    std::vector<uint8> Header;
    std::vector<uint8> Body;
    P4MapKit::FPackageInventory Inventory;
    int32 Asset = INDEX_NONE;
};

// One converted package already on disk.
inline bool ReadConverted(const FString& HeaderFile, const FString& Package, FPackage& Out, FString& Error)
{
    TArray<uint8> Header;
    TArray<uint8> Body;
    if (!FFileHelper::LoadFileToArray(Header, *HeaderFile) ||
        !FFileHelper::LoadFileToArray(Body, *FPaths::ChangeExtension(HeaderFile, TEXT("uexp"))))
    {
        Error = TEXT("The converted package could not be read: ") + Package;
        return false;
    }
    Out.Header.assign(Header.GetData(), Header.GetData() + Header.Num());
    Out.Body.assign(Body.GetData(), Body.GetData() + Body.Num());
    Out.Inventory = P4MapKit::ReadPackageHeader(Out.Header, Out.Body.size());
    const std::string Leaf = TCHAR_TO_UTF8(*FPackageName::GetShortName(Package));
    for (std::size_t Index = 0; Index < Out.Inventory.Exports.size(); ++Index)
        if (Out.Inventory.Exports[Index].Name == Leaf)
            Out.Asset = int32(Index);
    if (!Out.Inventory.Error.Code.empty() || Out.Asset == INDEX_NONE)
    {
        Error = TEXT("The converted package has no readable asset: ") + Package;
        return false;
    }
    return true;
}

inline bool Read(const FP4KadanImportSettings& Settings, const FString& Package, FPackage& Out, FString& Error)
{
    FString HeaderFile;
    return P4AcquireGamePreviewSource(Settings, Package, HeaderFile, Error) &&
        ReadConverted(HeaderFile, Package, Out, Error);
}

inline P4MapKit::FPropertyInventory Properties(const FPackage& Package, int32 Index)
{
    const auto& Export = Package.Inventory.Exports[Index];
    return P4MapKit::ReadPropertyEnvelopes(
        Package.Body, Package.Inventory.Names, Export.Offset, Export.Offset + Export.Size);
}

// A string or soft-object property of the package's asset, as text. Empty when it has none.
inline FString Value(const FPackage& Package, const char* Name)
{
    const auto List = Properties(Package, Package.Asset);
    for (const auto& Property : List.Properties)
        if (Property.Name == Name)
        {
            P4MapKit::FByteReader Reader(Package.Body, Property.PayloadOffset, Property.PayloadOffset + Property.Size);
            std::string Text;
            if (Property.Type == "StrProperty" && Reader.String(Text))
                return FString(UTF8_TO_TCHAR(Text.c_str()));
            if (Property.Type == "SoftObjectProperty" && Reader.Name(Package.Inventory.Names, Text))
                return FString(UTF8_TO_TCHAR(Text.c_str()));
        }
    return FString();
}
} // namespace P4GamePreviewTestGame

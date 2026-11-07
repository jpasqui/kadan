#include "P4GamePaths.h"
#include "P4GameNames.h"
#include "Misc/PackageName.h"

namespace
{
const TCHAR ContentRoot[] = TEXT("/Game/");
}

bool P4IsContentPackage(const FString& Package)
{
    return Package.StartsWith(ContentRoot, ESearchCase::CaseSensitive);
}

bool P4IsValidContentPackage(const FString& Package)
{
    return P4IsContentPackage(Package) && FPackageName::IsValidLongPackageName(Package);
}

bool P4IsContentPackageIgnoringCase(const FString& Package)
{
    return Package.StartsWith(ContentRoot, ESearchCase::IgnoreCase);
}

bool P4IsValidContentPackageIgnoringCase(const FString& Package)
{
    return P4IsContentPackageIgnoringCase(Package) && FPackageName::IsValidLongPackageName(Package);
}

FString P4ContentRelativePath(const FString& Package)
{
    return Package.Mid(UE_ARRAY_COUNT(ContentRoot) - 1);
}

FString P4ContainerRelativePath(const FString& Package)
{
    return FString(TEXT(P4_GAME_CONTENT_FOLDER "/")) + P4ContentRelativePath(Package);
}

FString P4ObjectPathOf(const FString& Package)
{
    return Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package);
}

FString P4ClassPathOf(const FString& Package)
{
    return P4ObjectPathOf(Package) + TEXT("_C");
}

bool P4IsModFolderPackage(const FString& Package)
{
    return Package.StartsWith(TEXT(P4_MODS_FOLDER "/"), ESearchCase::IgnoreCase) ||
        Package.StartsWith(TEXT(P4_PREVIEW_FOLDER "/"), ESearchCase::IgnoreCase);
}

FString P4ModFolderRootOf(const FString& Package)
{
    for (const TCHAR* Folder : {TEXT(P4_MODS_FOLDER "/"), TEXT(P4_PREVIEW_FOLDER "/")})
    {
        if (!Package.StartsWith(Folder, ESearchCase::IgnoreCase))
            continue;
        const int32 End = Package.Find(
            TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, FCString::Strlen(Folder));
        return End == INDEX_NONE ? FString() : Package.Left(End);
    }
    return FString();
}

#include "P4NavigationLayer.h"

#include "Misc/PackageName.h"

bool P4IsOlimarNavigationLevel(const FString& Package)
{
    const FString Leaf = FPackageName::GetShortName(Package);
    return Leaf.Contains(TEXT("_Hero"), ESearchCase::IgnoreCase);
}

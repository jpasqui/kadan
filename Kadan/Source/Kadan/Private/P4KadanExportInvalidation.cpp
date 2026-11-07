#include "P4KadanExportInvalidation.h"

bool P4KadanExportPackageIsSelected(
    const FString& Package,
    const TArray<FP4ExportRoot>& Roots,
    const TArray<FP4ExportSelectionRow>& ReviewedRows)
{
    if (ReviewedRows.ContainsByPredicate([&](const FP4ExportSelectionRow& Row)
    {
        return Row.Package.Equals(Package, ESearchCase::IgnoreCase);
    }))
    {
        return true;
    }
    for (const FP4ExportRoot& Root : Roots)
    {
        if (Package.Equals(Root.Path, ESearchCase::IgnoreCase))
        {
            return true;
        }
        if (Root.Kind == EP4ExportRootKind::Folder &&
            Package.StartsWith(Root.Path + TEXT("/"), ESearchCase::IgnoreCase))
        {
            return true;
        }
    }
    return false;
}

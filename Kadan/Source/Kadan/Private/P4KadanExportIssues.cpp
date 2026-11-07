#include "P4KadanExportIssues.h"

FString P4KadanExportDetails(const FString& Error, const TArray<FString>& Refused)
{
    TArray<FString> Parts;
    if (!Error.IsEmpty())
    {
        Parts.Add(Error);
    }
    const FString AllRefusals = FString::Join(Refused, TEXT("\n"));
    if (Error == AllRefusals)
    {
        return Error;
    }
    for (const FString& Detail : Refused)
    {
        if (!Detail.IsEmpty())
        {
            Parts.AddUnique(Detail);
        }
    }
    return FString::Join(Parts, TEXT("\n"));
}

FP4KadanExportIssue P4KadanExportIssue(const FP4ExportSelectionRow& Row)
{
    FP4KadanExportIssue Issue;
    Issue.Asset = Row.Package;
    Issue.Detail = Row.Reason;
    Issue.SelectingRoot = Row.SelectingRoot;
    Issue.NeedsPersistentMap = Row.Issue == EP4ExportIssueKind::PersistentMapRequired;
    Issue.Cause = Issue.NeedsPersistentMap
                      ? TEXT("This level needs its persistent game map before it can export.")
                  : Row.Disposition == EP4ExportDisposition::NeedsReplacementProof
                      ? TEXT("This game asset needs an approved export route before it can ship.")
                      : TEXT("This asset needs attention before it can ship.");
    Issue.NextStep =
        Issue.NeedsPersistentMap
            ? TEXT("Open the persistent map if it is available, or import it from the game.")
        : Row.Explicit
            ? TEXT("Locate the asset and check its status. Remove its selected root if it is not needed.")
            : TEXT("Check the selected root that requires this asset. Change its references or remove that "
                   "root if it is not needed.");
    return Issue;
}

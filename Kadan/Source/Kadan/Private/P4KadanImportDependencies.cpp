#include "P4KadanImportDependencies.h"
#include "P4KadanImport.h"

bool P4KadanImportKnownRequiredReferences(const FP4KadanImportPlan& Plan, const TSet<FString>& Selected,
                                          TArray<FString>& Required, FString& Error)
{
    Required.Empty();
    Error.Empty();
    TSet<FName> Seen;
    for (const FString& Package : Selected)
    {
        Seen.Add(FName(*Package));
    }
    for (const auto& Asset : Plan.Assets)
    {
        if (!Asset)
        {
            continue;
        }
        TArray<FString> Dependencies;
        if (!Plan.ExecutionEdges.Dependencies(*Asset, Dependencies, Error))
        {
            Required.Empty();
            return false;
        }
        for (const FString& Package : Dependencies)
        {
            const FName Identity(*Package);
            const bool Soft = Asset->SoftDependencies.ContainsByPredicate(
                [&](const FString& Candidate)
                {
                    return FName(*Candidate) == Identity;
                });
            if (Seen.Contains(Identity) || (Soft && !P4KadanImportFollowsSoftReferences(Asset->Kind)))
            {
                continue;
            }
            Seen.Add(Identity);
            Required.Add(Package);
        }
    }
    Required.Sort();
    return true;
}

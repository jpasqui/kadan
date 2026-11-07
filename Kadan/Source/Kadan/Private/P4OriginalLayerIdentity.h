#pragma once

#include "CoreMinimal.h"

inline bool P4OriginalLayerRecorded(const FString& ObjectPath, const TArray<FString>& Imported)
{
    const FString Prefix = ObjectPath.Replace(TEXT(":"), TEXT(".")) + TEXT(" | ");
    return Imported.ContainsByPredicate(
        [&Prefix](const FString& Line)
        {
            // Cook paths use dots for subobjects and can fold Unreal object names.
            return Line.StartsWith(Prefix, ESearchCase::IgnoreCase);
        });
}

#pragma once
#include "P4OriginalClassComparison.h"

namespace P4MapKit { struct FDecodedValue; }

// Called immediately before the existing Unheld decision erases a value. Bounded
// to256 rows and512 preview characters, with no change to the unsupported count.
KADAN_API void P4AppendOriginalClassUnheldDetail(TArray<FP4OriginalClassValueNotHeld>* Details,
    const FString& Object, const FString& Property, const FString& Schema,
    const P4MapKit::FDecodedValue& Value, const FString& Reason);

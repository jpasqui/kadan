#pragma once
#include "P4OriginalClassComparison.h"

// A native diagnostic comparison. Source authority belongs to the opaque
// original package token and its factory, never to these supplied buffers.
FP4OriginalClassComparisonResult P4CompareOriginalClassSourceNative(
    const TArray<uint8>& Header, const TArray<uint8>& Body, const FString& CanonicalPackage);

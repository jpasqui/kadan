#pragma once
#include "P4OriginalClassComparison.h"

enum class EP4OriginalClassDefaultObservation : uint8
{
    NativeStoredDefaults,
    TemporarilyBorrowedForCook
};

// A strict stored-class proof. Temporary source lending can qualify a separate cook
// adapter, but cannot qualify persistent native CDO/component defaults through this API.
// Source snapshot authority and the eventual cold/ordinary-construction gate remain
// the caller's responsibilities; this validates the observed native comparison coverage.
KADAN_API bool P4StrictOriginalClassDefaultsQualified(const FP4OriginalClassComparisonResult& Result,
    EP4OriginalClassDefaultObservation Observation, FString& Reason);

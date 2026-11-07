#pragma once
#include "P4OriginalClassCoverage.h"

struct FP4KadanSourceBinding;

// One current, source-bound inventory per selected world, including resumed worlds.
// Empty RequiredClassPackages is meaningful only when Known and WorldSourceDigest are set.
struct FP4StrictWorldClassInventory
{
    FString WorldPackage, WorldSourceDigest, Error;
    bool Known = false, Resumed = false;
    TArray<FString> RequiredClassPackages;
};

struct FP4StrictRequiredClassProof
{
    FString Package, SourceDigest, DefaultFactsDigest, Reason;
    bool Qualified = false;
    int32 Compared = 0, ValuesNotHeld = 0, Differences = 0;
};

struct FP4StrictWorldClassProof
{
    FString WorldPackage, WorldSourceDigest;
    bool Qualified = false, Resumed = false;
    int32 RequiredClasses = 0;
    TArray<FString> Problems;
};

struct FP4StrictRequiredClassPostflight
{
    bool Qualified = false;
    TArray<FP4StrictRequiredClassProof> Classes;
    TArray<FP4StrictWorldClassProof> Worlds;
    TArray<FString> Problems;
};

// Call once after ALL Blueprint writes, world construction and FLentTemplates scopes
// have ended. ReadCurrent resolves the exact registered source and its actual loaded
// generated class without mutating either. ReadCurrent MUST also verify the source
// generated-class export's Super identity against the live GetSuperClass; loaded
// ancestry alone is not source evidence. This helper revalidates the source bytes
// itself and checks class/CDO identities before and after comparing and at consumption.
// CompareNative must observe persistent defaults, never create a temporary source loan.
// Unique classes are compared once; all state is local to this synchronous call.
KADAN_API FP4StrictRequiredClassPostflight P4EvaluateStrictRequiredClasses(
    const TArray<FString>& SelectedWorlds,
    const TArray<FP4StrictWorldClassInventory>& Inventories,
    TFunctionRef<bool(const FString&, FP4KadanSourceBinding&, UClass*&, FString&)> ReadCurrent,
    TFunctionRef<FP4OriginalClassComparisonResult(const FP4KadanSourceBinding&)> CompareNative,
    EP4OriginalClassDefaultObservation Observation);

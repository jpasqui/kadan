#pragma once
#include "CoreMinimal.h"

class UWorld;

enum class EP4WorldStreamingGraphError : uint8
{
    None, EditorThreadRequired, WorldRequired, InvalidPackages, MissingWorld,
    InvalidWorldIdentity, WorldLimit, DestinationIdentityFailed, DestinationCollision,
    NullEntry, InvalidEntryOwner, EntryLimit, InvalidReference, PackageOverride,
    Cycle, DestinationSourceCollision
};

struct KADAN_API FP4WorldStreamingEntry
{
    FString StreamName, Package, LoadedPackage, PackageToLoad;
    bool Loaded = false, Visible = false, EditorVisible = false;
};

struct KADAN_API FP4WorldStreamingNode
{
    FString Target;
    TArray<FP4WorldStreamingEntry> Streams;
};

struct KADAN_API FP4WorldStreamingGraphResult
{
    EP4WorldStreamingGraphError Error = EP4WorldStreamingGraphError::None;
    FString ErrorDetail, SourcePackage, TargetPackage;
    // Full declared membership, keyed by the exact source package identity.
    TMap<FString, FP4WorldStreamingNode> Nodes;
    // Streaming entries of imported game levels that name a level the game does not ship.
    TArray<FString> UnshippedStreams;
    bool IsValid() const { return Error == EP4WorldStreamingGraphError::None; }
};

// Inspect all descendants without flushing streaming, dirtying settings or saving.
KADAN_API FP4WorldStreamingGraphResult P4InspectWorldStreamingGraph(UWorld* World, const FString& TargetPackage);
KADAN_API FString P4WorldStreamingGraphJson(const FP4WorldStreamingGraphResult& Result);

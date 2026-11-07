#pragma once
#include "CoreMinimal.h"

struct FP4OccupiedMapSlot
{
    uint32 Area = 0;
    FString Map;
};
enum class EP4MapOccupancyState : uint8
{
    Empty,
    Valid,
    Recovered,
    Legacy
};

// A read-only snapshot of the chosen actual final-Tsuchi save root. It never reserves or frees slots.
class KADAN_API FP4NewMapOccupancy
{
public:
    bool IsValid() const { return Valid; }
    EP4MapOccupancyState State() const { return SnapshotState; }
    uint64 Counter() const { return Generation; }
    const TArray<FP4OccupiedMapSlot>& Occupied() const { return Slots; }
    const FString& SnapshotSha256() const { return Digest; }

private:
    struct FSnapshot;
    TSharedPtr<const FSnapshot> Snapshot;
    bool Valid = false;
    EP4MapOccupancyState SnapshotState = EP4MapOccupancyState::Empty;
    uint64 Generation = 0;
    TArray<FP4OccupiedMapSlot> Slots;
    FString Digest;
    friend KADAN_API bool P4ReadNewMapOccupancy(const FString&, FP4NewMapOccupancy&, FString&);
    friend KADAN_API bool P4RevalidateNewMapOccupancy(const FP4NewMapOccupancy&, FString&);
};

// SaveDirectory is the explicitly selected mounted save root, containing tsuchi/tsuchi.content.
// Both canonical copies are read first; legacy is read only when both canonical copies are absent.
// Missing, corrupt and unreadable are different states. Failed reads preserve Out.
KADAN_API bool P4ReadNewMapOccupancy(const FString& SaveDirectory, FP4NewMapOccupancy& Out, FString& Error);
// Call immediately before publication. A changed, newly present or missing record refuses reuse.
KADAN_API bool P4RevalidateNewMapOccupancy(const FP4NewMapOccupancy& Snapshot, FString& Error);

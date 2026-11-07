#pragma once
#include "CoreMinimal.h"
#include "P4MapKitCore/property_decoder.h"

class UDataTable;

enum class EP4CookedTableRowObservationError : uint8
{
    None, WrongThread, InvalidTable, CookFailed, RowsUnreadable, TableNotFound
};

struct FP4CookedTableRowObservation
{
    EP4CookedTableRowObservationError Error = EP4CookedTableRowObservationError::None;
    FString ErrorDetail, Package, AssetPath, RowStruct;
    TArray<FString> RowNames;
    TArray<P4MapKit::FDecodedValue> Rows;
    bool IsValid() const { return Error == EP4CookedTableRowObservationError::None; }
};

// Observes freshly cooked row tags without folding field or row spelling. No original
// source, donor, schema alias or runtime behavior admission follows from this result.
// The diagnostic cook preserves the source package and never saves editor Content.
KADAN_API FP4CookedTableRowObservation P4ObserveCookedTableRowsNative(UDataTable* Table);

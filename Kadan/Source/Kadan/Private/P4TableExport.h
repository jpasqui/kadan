#pragma once
#include "CoreMinimal.h"

class UDataTable;
class FJsonObject;

// Project-only source rows are compared with current rows. Only changed fields and clone donors enter
// the manifest. Original rows must retain their names and order.
KADAN_API bool P4TableChanges(const FString& ModId, const FString& Baseline, const FString& Current,
    TSharedPtr<FJsonObject>& Out, FString& Error);
KADAN_API bool P4ExportTableChanges(UDataTable* Table, const FString& ModId, TSharedPtr<FJsonObject>& Out, FString& Error);
// UE4's standard JSON writer converts every number to double. Manifest integers must retain their
// exact stored spelling so IDs and integer table fields never lose low bits.
KADAN_API FString P4TableManifestJson(const TSharedPtr<FJsonObject>& Manifest);

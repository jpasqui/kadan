#pragma once
#include "CoreMinimal.h"

// Replaces complete export payloads in an ordinary, versioned UE4.26 legacy
// package. Keys are zero based export-table indices. The table order, names,
// imports, custom versions and unknown header bytes retain their identities.
// Only contiguous export bodies ending in the package tag are admitted; native
// inline bulk data is deliberately outside this helper's scope.
bool P4ReplaceGamePreviewExports(TArray<uint8>& Header, TArray<uint8>& Body,
    const TMap<int32, TArray<uint8>>& Replacements, FString& Error);

// Changes the summary's package/folder FString, relocates its header tables and
// absolute body offsets, and gives the private copy its own package GUID. The
// name table is separately relabelled by the caller; all body bytes stay exact.
bool P4SetGamePreviewPackageName(TArray<uint8>& Header, const TArray<uint8>& Body,
    const FString& Package, FString& Error);

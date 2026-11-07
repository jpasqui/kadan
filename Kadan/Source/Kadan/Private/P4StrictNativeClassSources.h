#pragma once
#include "P4StrictRequiredClasses.h"
#include "P4KadanImportSources.h"

// Fresh inventory from the selected world's exact original header/body pair.
// Does not import, replace, save or lend class defaults.
KADAN_API FP4StrictWorldClassInventory P4ReadStrictWorldClassInventory(
    const FString& Package, const FString& Header, const FString& Body, bool Resumed);

// Resolve the current import registry and prove source-generated class ancestry
// against its actual live class before any persistent-default comparison.
KADAN_API bool P4ReadStrictNativeClassSource(const FString& Package,
    FP4KadanSourceBinding& Source, UClass*& Class, FString& Error);

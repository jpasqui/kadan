#pragma once
#include "CoreMinimal.h"
#include "P4MapKitCore/property_decoder.h"
#include "P4StockEngineFields.h"

KADAN_API bool P4StoreDecodedStockEngineField(UObject*, const FString&, const P4MapKit::FDecodedValue&, FString&);
KADAN_API P4MapKit::FDecodedValue P4DecodeStockEngineField(const FP4StockEngineField&);
KADAN_API bool P4TranslateStockEngineFieldCook(UPackage*, std::vector<uint8>&, std::vector<uint8>&, FString&);
KADAN_API bool P4PackageHasStockEngineFields(UPackage*);
KADAN_API bool P4ValidateStockEngineCookRelocation(const std::vector<uint8>&, const std::vector<uint8>&, FString&);
// Appends imports to a cooked package's import table (every name already in its name table); existing
// import and export indices are kept.
KADAN_API bool P4AppendCookedImports(std::vector<uint8>& Header, const std::vector<uint8>& Body, const std::vector<P4MapKit::FPackageImport>& Imports, FString& Error);

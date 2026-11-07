#pragma once
#include "P4ExportContainer.h"

// Decode the selected packed container beside only the supported game's global
// script metadata. No original asset container can supply a fallback package.
KADAN_API bool P4ExtractExportReadback(const FP4ExportContainerTools& Tools,const FString& Utoc,
    const FString& GamePaks,const FString& Work,const FString& Destination,
    const FString& Filter,FString& Error);
KADAN_API bool P4ExtractExportReadback(const FP4ExportContainerTools& Tools,const FString& Utoc,
    const FString& GamePaks,const FString& Work,const FString& Destination,
    const TArray<FString>& Filters,FString& Error);

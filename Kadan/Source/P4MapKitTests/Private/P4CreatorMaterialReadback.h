#pragma once
#include "P4ExportContainer.h"
bool P4ExtractCreatorMaterialReadback(const FP4ExportContainerTools& Tools,const FString& Utoc,
    const FString& GamePaks,const FString& Work,const FString& Destination,
    const FString& OwnRoot,const TArray<FString>& ExpectedPackages,FString& Error);

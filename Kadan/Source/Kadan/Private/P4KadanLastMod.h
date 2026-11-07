#pragma once

#include "CoreMinimal.h"
#include "P4ModDescriptor.h"

KADAN_API FString P4KadanLastModId(const FString& SettingsFile);
KADAN_API void P4KadanRememberModId(const FString& SettingsFile, const FString& Id);
KADAN_API TSharedPtr<FP4ModDescriptor> P4KadanFindRememberedMod(
    const FString& Id, const TArray<TSharedPtr<FP4ModDescriptor>>& Mods);

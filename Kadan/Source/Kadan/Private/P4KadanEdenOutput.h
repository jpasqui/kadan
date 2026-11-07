#pragma once

#include "CoreMinimal.h"

class SWidget;

// Read-only discovery of an existing title load folder in an Eden user profile.
KADAN_API FString P4KadanFindEdenOutput(const FString& RoamingDirectory);
TSharedRef<SWidget> P4KadanEdenOutputSuggestion(const FString& Directory, TFunction<void()> Choose);

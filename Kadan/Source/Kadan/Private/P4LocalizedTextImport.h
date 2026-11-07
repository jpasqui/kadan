#pragma once
#include "CoreMinimal.h"
KADAN_API bool P4MakePersistentLocalizedText(uint32 Flags, const FString& Namespace, const FString& Key,
                                             const FString& Source, FText& Out);

#pragma once
#include "CoreMinimal.h"

// Generic exact fields on disposable editor-thread scratch only. This does not
// admit an original asset, resolve its complete defaults, or publish Content.
// Failure may leave scratch partially written: the owner discards it.
KADAN_API bool P4PrepareExactPrivateProperties(UObject* Scratch, const FString& Values, FString& Canonical, FString& Error);

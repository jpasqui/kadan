#pragma once
#include "CoreMinimal.h"
class UWorld;

// Native game component aliases are serialized map values. Editor SCS reconstruction replaces
// their components; keep those aliases pointed at the engine's actual replacement objects.
KADAN_API void P4StartOriginalComponentReferences();
KADAN_API void P4StopOriginalComponentReferences();
class KADAN_API FP4OriginalComponentReferenceScope
{
public:
    explicit FP4OriginalComponentReferenceScope(UWorld* World);
    ~FP4OriginalComponentReferenceScope();
    FP4OriginalComponentReferenceScope(const FP4OriginalComponentReferenceScope&) = delete;
    FP4OriginalComponentReferenceScope& operator=(const FP4OriginalComponentReferenceScope&) = delete;
private:
    FDelegateHandle Handle;
};

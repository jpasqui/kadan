#pragma once
#include "CoreMinimal.h"

// Editor-thread-only, lexically scoped resolver for private graph prototypes.
// Logical destination paths never require placeholder objects in Content.
class KADAN_API FP4ImportReferenceScope
{
public:
    explicit FP4ImportReferenceScope(const TMap<FString, UObject*>& References);
    ~FP4ImportReferenceScope();
    FP4ImportReferenceScope(const FP4ImportReferenceScope&) = delete;
    FP4ImportReferenceScope& operator=(const FP4ImportReferenceScope&) = delete;
    UObject* Find(const FString& Path) const;
    FString Path(UObject* Object) const;
private:
    const FP4ImportReferenceScope* Previous;
    TMap<FString, UObject*> Forward;
    TMap<UObject*, FString> Reverse;
};
UObject* P4ImportFindObject(const FString& Path);
FString P4ImportObjectPath(UObject* Object);
FString P4ImportStagedPath(UObject* Object);
FString P4ImportTextPath(const FString& Logical);

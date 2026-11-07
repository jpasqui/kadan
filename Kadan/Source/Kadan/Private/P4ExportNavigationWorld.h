#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

class ULevel;
class UWorld;
class AActor;

// An isolated collision snapshot for a map that is not the creator's open map.
// No source world is initialized, saved, or made current in the editor.
class KADAN_API FP4ExportNavigationWorld
{
public:
    FP4ExportNavigationWorld();
    ~FP4ExportNavigationWorld();

    UWorld* Load(const FString& Package, FString& Error);
    const TMap<ULevel*, bool>& Variants() const { return HeroLevels; }

private:
    bool Gather(UWorld* Source, const FTransform& Transform, bool Hero, FString& Error);

    FString Namespace;
    TArray<TStrongObjectPtr<UWorld>> Copies;
    TSet<UWorld*> Sources;
    TMap<ULevel*, bool> HeroLevels;
    TMap<AActor*, AActor*> CopiedActors;
    bool Initialized = false;
    bool MadeContext = false;
};

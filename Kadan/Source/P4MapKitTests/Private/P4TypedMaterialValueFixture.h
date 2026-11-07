#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "P4TypedMaterialValueFixture.generated.h"
class UMaterial;
class UMaterialInterface;
// Real reflected negative slots; detached FProperty constructors are not linked fields.
UCLASS(Transient)
class UP4TypedMaterialValueFixture : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY() UMaterial* ConcreteMaterial = nullptr;
    UPROPERTY() TSoftObjectPtr<UObject> BroadSoft;
    UPROPERTY() TSoftObjectPtr<UMaterialInterface> MaterialSoft;
};

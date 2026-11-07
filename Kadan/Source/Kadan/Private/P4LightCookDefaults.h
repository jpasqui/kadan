#pragma once
#include "CoreMinimal.h"

class UPointLightComponent;

// The game defaults point/spot components to non-inverse-square falloff. Stock
// UE defaults to inverse square; omitting that true value also makes the game
// downgrade an authored photometric light to Unitless while deserializing it.
class KADAN_API FP4LightCookDefaults
{
public:
    explicit FP4LightCookDefaults(UObject* Asset);
    ~FP4LightCookDefaults();
    FP4LightCookDefaults(const FP4LightCookDefaults&) = delete;
    FP4LightCookDefaults& operator=(const FP4LightCookDefaults&) = delete;

private:
    struct FKept
    {
        TWeakObjectPtr<UPointLightComponent> Light;
        bool InverseSquared;
    };
    TArray<FKept> Kept;
};

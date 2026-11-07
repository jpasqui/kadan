#include "P4LightCookDefaults.h"
#include "Components/PointLightComponent.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"

FP4LightCookDefaults::FP4LightCookDefaults(UObject* Asset)
{
    if (!IsValid(Asset))
        return;
    bool HasLight = false;
    ForEachObjectWithPackage(
        Asset->GetOutermost(),
        [&](UObject* Object)
        {
            HasLight |= Object->IsA<UPointLightComponent>();
            return true;
        },
        true);
    if (!HasLight)
        return;
    for (TObjectIterator<UPointLightComponent> It(RF_NoFlags); It; ++It)
    {
        UPointLightComponent* Light = *It;
        if (!Light->IsTemplate() || Light->GetOutermost()->GetName() != TEXT("/Script/Engine"))
            continue;
        Kept.Add({Light, Light->bUseInverseSquaredFalloff != 0});
        Light->bUseInverseSquaredFalloff = false;
    }
}

FP4LightCookDefaults::~FP4LightCookDefaults()
{
    for (const FKept& Each : Kept)
        if (UPointLightComponent* Light = Each.Light.Get())
            Light->bUseInverseSquaredFalloff = Each.InverseSquared;
}

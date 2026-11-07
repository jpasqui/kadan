#pragma once
#include "CoreMinimal.h"
#include "P4MapKitCore/property_decoder.h"

class FProperty;

// Only an explicit null Distribution in the exact native raw-distribution structs can
// equal its omitted, natively initialized null default. Every other value is untouched.
KADAN_API bool P4CanonicalOriginalParticleDistribution(P4MapKit::FDecodedValue& Value, const FProperty* Property);
KADAN_API bool P4OriginalParticleDistributionNullsHeld(UObject* Object, const P4MapKit::FDecodedValue& Source,
    int32& Checked, FString& Error);

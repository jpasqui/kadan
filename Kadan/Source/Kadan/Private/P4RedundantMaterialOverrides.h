#pragma once
#include "CoreMinimal.h"
THIRD_PARTY_INCLUDES_START
#include "P4MapKitCore/property_decoder.h"
THIRD_PARTY_INCLUDES_END

// Null mesh overrides fall through to the mesh's own materials. Qualify a
// stripped source tag only against the actual component and its cooked fields.
KADAN_API void P4WithoutRedundantNullMaterialOverrides(UObject* Live, P4MapKit::FDecodedValue& Source,
                                                       const P4MapKit::FDecodedValue& Cooked);

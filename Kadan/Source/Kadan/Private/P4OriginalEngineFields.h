#pragma once
#include "CoreMinimal.h"
#include "P4MapKitCore/property_decoder.h"
class UActorComponent;

// The declaration SDK stores engine-fork fields on its native game child classes.
// Use that real storage while retaining the original game's member-owner identity.
KADAN_API bool P4PrepareOriginalEngineTracking(const UClass*, P4MapKit::FDecodedValue&);
KADAN_API bool P4CanonicalOriginalEngineTracking(const UClass*, P4MapKit::FDecodedValue&);
// Editor-generated tracking only; the actual transform values remain compared.
KADAN_API bool P4NativeTransformConstructionTracking(const UClass*, const P4MapKit::FDecodedValue&);
KADAN_API void P4RestoreOriginalEngineFields(UActorComponent* Old, UActorComponent* New);

#pragma once
#include "CoreMinimal.h"
#include "P4MapKitCore/physics_asset_native.h"
class UObject;
KADAN_API bool P4FillPhysicsNative(UObject* Object, const P4MapKit::FPhysicsAssetNative& Data, FString& Error);
KADAN_API bool P4FillPrimitivePhysicsBodyNative(UObject* Object, const P4MapKit::FPhysicsBodyNative& Data, FString& Error);

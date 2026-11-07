#pragma once
#include "CoreMinimal.h"

class UWorld;

// The bake setup of a map (its sun, sky light, importance volumes, static lights and Lightmass settings,
// as JSON) put into a level for a lighting build. Returns JSON: valid, error, placed (the labels of what
// was put in), sky_source (the sky light's cubemap, or "captured"), sky_material, values_not_set.
FString P4LightingBakeSetup(UWorld* World, const FString& Setup);
// After the build: the setup's lights taken out of the level, with what the build made for the level as a
// whole (its volumetric lightmap), which a level loaded into a map would otherwise give the whole map.
FString P4LightingBakeClear(UWorld* World);

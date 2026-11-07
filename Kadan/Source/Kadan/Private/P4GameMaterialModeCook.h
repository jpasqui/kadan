#pragma once
#include "CoreMinimal.h"
#include <vector>

// The game's own modes in a cooked material: settings stock Unreal's material cannot hold, which Kadan keeps in the
// material's C4 settings (P4MaterialGameSettings.h) while the material holds a stock stand-in. The cook writes the
// stand-in; these write the game's value in its place, as the game's cooked masters hold it: the decal mode
// DBM_DBuffer_C4_WaterBox (C4DecalBlendMode), a byte property of EDecalBlendMode by name, by which the game's renderer
// picks the decal's pass and blending; and the game's boolean UMaterial fields the settings mark C4MaterialField
// (reverse depth test, translucent depth write, the separate translucency passes), set ones as true bool tags.
bool P4PackageHasGameMaterialModes(UPackage* Package);
bool P4TranslateGameMaterialModeCook(UPackage* Package, std::vector<uint8>& Header, std::vector<uint8>& Body, FString& Error);

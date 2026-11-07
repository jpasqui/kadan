#pragma once
#include "CoreMinimal.h"
class UMapBuildDataRegistry;
class UWorld;

// A level's built lighting from the game: the cooked MapBuildDataRegistry package (HeaderFile, its .uexp in BodyFile
// or next to it, its .ubulk next to it) made again at its own path, with its lightmap and shadow map textures as the
// registry's own objects, as the game's project held it. The level keeps referring to it by that path, so the editor
// draws the level with the game's lighting. The asset is reference-only and read-only: it never goes into a mod; an
// edited map's lighting is built again at export. Null with Error when it cannot be made.
struct FP4OriginalBuiltLightingResult
{
    UMapBuildDataRegistry* Registry = nullptr;
    FString Package, Error;
    int32 Textures = 0;
    bool Saved = false;
};
KADAN_API FP4OriginalBuiltLightingResult P4ImportOriginalBuiltLighting(
    const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk);
// The game's built lighting as the game's editor held a level after loading it: each mesh component names its part of
// the level's registry by the ids of its levels of detail, each light keeps its own id, and the level refers to its
// registry when the call above made it. A save and a cook then write them as the game's file has them. LevelWorld is
// the world of one level's own package, with the lighting its import read from the game's file. Returns whether the
// level refers to its registry.
KADAN_API bool P4HoldOriginalBuiltLighting(UWorld* LevelWorld);
// The level a registry made by the call above belongs to, when it is already in the project and does not refer to it
// yet: given its lighting as above and saved.
KADAN_API void P4HoldOriginalBuiltLightingOfRegistry(UMapBuildDataRegistry* Registry);
// The game's built lighting drawn on an opened map, as the game's editor drew it: each game level without lighting of
// its own whose registry was made by the call above refers to it, and its meshes and lights get back the lighting ids
// the import kept. These are previews (P4NativeConstructionPreview.h): a save or an export sees the level as imported.
// Returns how many levels were given their lighting.
KADAN_API int32 P4ApplyOriginalBuiltLightingPreviews(UWorld* World);

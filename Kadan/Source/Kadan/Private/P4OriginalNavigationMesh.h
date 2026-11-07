#pragma once

#include "CoreMinimal.h"
#include <vector>

class UPackage;
class UWorld;
namespace P4LevelContent
{
struct FFile;
}

// The game's navigation as the game's editor held a level after loading it: each navigation mesh actor of LevelWorld
// that Game (the level's file) gives a navigation build's mesh loads that mesh's tiles, as the engine's load of the
// map the game's editor saved does: the tiles inside the level's navigation bounds, in their order. Returns how many
// actors hold tiles.
int32 P4HoldOriginalNavigationMeshes(UWorld* LevelWorld, const P4LevelContent::FFile& Game);

// The editor's save of a navigation mesh keeps the tiles inside its level's navigation bounds, which its world's
// navigation system knows, and none while that system is off, as the game's maps have it (World Settings). The game's
// editor saved its maps with it running. While World is saved (Saving) and its navigation system is off, the world has
// one that knows its bounds and runs nothing else; after the save (not Saving) it has none again.
void P4GiveNavigationBoundsForSave(UWorld* World, bool Saving);

// A cook runs as a commandlet, where a navigation mesh saves every tile it holds. The editor's save keeps only the
// tiles inside the level's navigation bounds, and none while no navigation system runs. Each navigation mesh in a
// package saved by the editor for a cook is written here as the cook writes it, from what its actor holds now.
bool P4CookNavigationMeshesAsCommandlet(
    UPackage* Package, std::vector<uint8>& Header, std::vector<uint8>& Body, FString& Error);

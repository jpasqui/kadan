#pragma once
#include "CoreMinimal.h"

class UWorld;
struct FP4KadanImportSettings;

// Reuses the native importer's converted-source cache; never imports the game's
// map into the project or copies a game installation.
KADAN_API bool P4AcquireGamePreviewSource(
    const FP4KadanImportSettings& Settings, const FString& Package, FString& HeaderFile, FString& Error);

struct FP4GamePreviewNavigation
{
    TMap<FString, TArray<uint8>> NativePayloads;
    TArray<FString> Told;
};

// World must be a disposable private geometry clone. Its floor/collision and
// navigation modifiers are baked using the game's three distinct agent sizes;
// all six normal/Hero native meshes get this scratch geometry, with no links
// or terrain copied from the template map. ProfileWorld is the game world whose navigation meshes give the
// agent sizes: the preview template's world.
KADAN_API bool P4BuildGamePreviewNavigation(UWorld* World, const FP4KadanImportSettings& Settings,
    const FString& ProfileWorld, FP4GamePreviewNavigation& Out, FString& Error);

// The raw bootstrap retains the game's exact nav actor names/classes/properties.
// Replace only their native tails before retoc makes the immutable container.
bool P4PatchGamePreviewNavigation(
    TArray<uint8>& Header, TArray<uint8>& Body, const FP4GamePreviewNavigation& Navigation, FString& Error);

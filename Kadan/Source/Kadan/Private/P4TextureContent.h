#pragma once
#include "CoreMinimal.h"

class UTexture;

// A digest of everything a texture saves that a cook reads: every setting and every pixel of its
// source image, mip by mip. Two textures with the same digest cook to the same thing. Left out are the
// things the engine itself rewrites without an edit: whether the source is stored as a PNG, the
// imported size it recomputes on load, and the record of the file the texture was imported from.
// The exact editor-only intrinsic creator/game origin subobject is also excluded; all other
// asset user data retains its native array serialization and participates in this comparison.
// Empty when the texture has no source image or it cannot be read.
KADAN_API FString P4TextureState(UTexture* Texture);
// Read-only reuse proof: exact native texture baseline, all five current game
// input files and unchanged live content. Never rebuilds, migrates or saves.
KADAN_API bool P4OriginalTextureMatchesSource(UTexture* Texture,const FString& HeaderFile,
    const FString& BodyFile,FString& Error);

// Imports one of the game's cooked textures as an ordinary texture at its game path in Content and
// records what it was imported as, so an export can tell a texture nobody touched from an edited one.
// A texture already there is left alone when it came from the same game file, replaced in place when
// it came from another version of it and was not edited since, and never replaced when it was edited.
// HeaderFile is the .uasset; the .uexp and any .ubulk are found next to it. Returns JSON.
FString P4ImportOriginalTexture(const FString& HeaderFile, const FString& BodyFile, bool SaveToDisk);

// Whether a texture is one the kit imported from the game, and whether it is still as imported.
FString P4OriginalTextureStatus(UTexture* Texture);

// A texture's references the plugin's save has to follow. False with Error set when the texture holds
// something the save cannot carry.
bool P4TextureSaveProperty(const UTexture* Texture, const class FProperty* Property, bool& Skip, FString& Error);

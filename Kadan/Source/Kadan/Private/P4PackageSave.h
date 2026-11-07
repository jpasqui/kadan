#pragma once
#include "CoreMinimal.h"

// Saves the package of an asset or of a level to its own file in Content, through the engine's own save,
// as the editor's Save does. A save that fails is tried again a few times, a moment apart: another
// program (a virus scanner, a file indexer) can hold a file for an instant just after it was written,
// and the engine then cannot put the new file in its place.
//
// A mesh, a material instance or a material is saved after what it refers to that has no file yet (a
// mesh's materials, an instance's parent and textures): a file that names a package without a file does
// not load whole in another editor. What already has a file is left as it is.
//
// StillRight, when given, is asked after the save. When it says no the save is taken back: the file that
// was there before is put back, or the new file is removed, and the package is left as not saved.
//
// Returns whether the asset is saved. Why says why not.
KADAN_API bool P4SaveAssetPackage(UObject* Asset, const TFunction<bool()>& StillRight, FString& Why);

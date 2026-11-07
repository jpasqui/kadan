#pragma once
#include "CoreMinimal.h"

class UObject;

// A whole package at a game asset's own path: a K9 creator texture/mesh, or an edited recovered actor
// Blueprint/material instance once its family is game-qualified. These retain their original recovery provenance;
// it is not relabelled as a creator-file import. Tsuchi mounts the package over the game's package ID.
struct FP4GameReplacement
{
    // The game asset's package and class, the creator's file (as it was named when it was imported) and the
    // digest of the game asset's content (P4GameContentDigest; empty when the game's could not be read).
    FString Package, Class, File, GameDigest;
    // creator-file (K9), edited-game-material-instance, or edited-game-actor-blueprint. Edited originals have no source
    // file: its generated class and native ancestor retain their original identities.
    FString Kind = TEXT("creator-file"), GeneratedClass, NativeParent;
};

// Whether this asset is an admitted whole replacement at its original path. A copy elsewhere is not
// a replacement. Edited originals require both the family gate and persistent authored edit intent.
KADAN_API bool P4IsGameReplacement(const UObject* Asset, FP4GameReplacement* Out = nullptr);
// Whether export ships this replacement: a creator file always, an edited original only while it still
// differs from its import (edit intent stays after an undo) or its kind has no comparison.
KADAN_API bool P4GameReplacementShips(UObject* Asset, FP4GameReplacement* Out = nullptr);
// P4GameReplacementShips for a package's asset, loaded when it is not loaded yet.
KADAN_API bool P4IsGameReplacementPackage(const FString& Package, FP4GameReplacement* Out = nullptr);
// Called at an authored edit notification, outside import/load/compile-on-load. Keeps a
// persistent edit-intent marker so a saved Blueprint is still followed as a replacement.
// Does nothing while its family is locked; preserves all original recovery provenance.
KADAN_API void P4RecordGameBlueprintEdit(UObject* Object);
KADAN_API void P4RecordGameMaterialInstanceEdit(UObject* Object);
// Whether "Replace with my own file" can be used on the asset: one of the game's own textures or plain static
// meshes, or a replacement made earlier. Why says why not in a sentence for the person.
KADAN_API bool P4CanReplaceGameAsset(UObject* Asset, FString* Why = nullptr);
// Imports the file (an image for a texture, an FBX or OBJ file for a static mesh) as the asset at the game's
// path, with the editor's own factory. A texture keeps the game texture's settings. A mesh keeps the game mesh's
// materials, sockets and collision settings, and its shape and collision come from the file. References in the
// editor move to the replacement. The package is left unsaved, as any import leaves it. Returns the replacement,
// or null with Error saying why; on an error the game's asset is left as it was.
KADAN_API UObject* P4ReplaceGameAsset(UObject* Asset, const FString& File, FString& Error);
// True while P4ReplaceGameAsset is replacing an asset of this package, so the read-only guards let it write.
bool P4IsReplacingGameAsset(const UObject* Asset);

#pragma once
#include "CoreMinimal.h"

class UMaterialInstanceConstant;

KADAN_API bool P4MaterialInstanceHasStructuralPermutation(const UMaterialInstanceConstant* Instance);
// Whether a parameter name is a member the game's View uniform buffer adds to the engine's. A recovered
// game master stands such a member in with a vector parameter of that name; the game's shader reads the View.
KADAN_API bool P4IsGameViewParameter(FName Name);
KADAN_API bool P4MaterialInstanceOverridesRecoveredView(const UMaterialInstanceConstant* Instance);
// Whether export compiles a shader map for this instance. Export availability and material staging
// both ask this, so a panel that says exportable matches what the export compiles.
KADAN_API bool P4MaterialInstanceNeedsOwnShaderMap(const UMaterialInstanceConstant* Instance);

// A View-uniform stand-in in a recovered graph is not a parameter in the
// original game's shader map. Give a private compile/cook copy a native static
// identity so its authored vector can be evaluated by the newly compiled map.
// Source is read-only; Copy must be a separate object owned by this export.
KADAN_API void P4MaterialInstancePrepareRecoveredViewPermutation(const UMaterialInstanceConstant* Source,
    UMaterialInstanceConstant* Copy);

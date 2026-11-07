#pragma once
#include "CoreMinimal.h"

class UMaterialInstanceConstant;

// A material instance as JSON, the form the kit's material tools write for one of the game's:
//   {"parent": "/Game/.../M_x.M_x",
//    "scalars": {"Roughness": 0.5}, "vectors": {"Tint": [1, 0.5, 0.25, 1]}, "textures": {"Diffuse_01": "/Game/.../T_x.T_x"},
//    "switches": {"Use_MaskMap": false},
//    "masks": {"Channels": [true, false, false, false]},
//    "overrides": {"two_sided": true, "blend_mode": "BLEND_Masked", "shading_model": "MSM_DefaultLit", "clip": 0.3333}}
// Overrides also accepts the boolean dithered_lod and shadow_as_masked settings.
// Every part but the parent may be left out. A value is given only where the instance sets it itself.

// Makes a material instance at Package. The parent and every texture have to be in the project. A
// parameter or switch the parent does not have is kept on the instance as the game's instance has it,
// and named in NotInParent. Null with Error set when it could not be made. Nothing is saved.
// This explicit builder records creator-instance origin outside game import publication and the
// original Carrot4 destination branch. That origin is independent of supported game parameters.
KADAN_API UMaterialInstanceConstant* P4BuildMaterialInstance(const FString& InstanceJson, const FString& Package,
    TArray<FString>& NotInParent, FString& Error);

// The instance as it is in the editor, in the same JSON, with keys in a fixed order.
KADAN_API FString P4MaterialInstanceJson(UMaterialInstanceConstant* Instance, FString& Error);

// Builds, optionally saves, and reports as JSON: valid, error, asset, not_in_parent, saved, and the
// instance read back.
// Optional source custody is checked before construction, before save and after its completed write.
KADAN_API FString P4BuildMaterialInstanceReport(const FString& InstanceJson, const FString& Package, bool SaveToDisk,
    const TFunction<bool(FString&)>& SourceCurrent = TFunction<bool(FString&)>());

// The assets an instance refers to: its parent and the textures it sets.
void P4MaterialInstanceSaveReferences(UMaterialInstanceConstant* Instance, TArray<UObject*>& Out);

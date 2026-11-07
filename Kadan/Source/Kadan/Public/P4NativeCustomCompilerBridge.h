#pragma once
#include "CoreMinimal.h"
#include <cstdint>
#include <vector>
class UMaterial;
class UMaterialInstanceConstant;
// Native export service for live graphs and creator instances. A child of a recovered game
// material gets its own compiled permutation while retaining the game parent reference.
// No recipe JSON substitutes for the live graph and instance state.
struct KADAN_API FP4CreatorMaterialCompileRequest {
    UMaterial* CreatorMaterial = nullptr; // master graph; a recovered game master requires CreatorInstance
    // Optional permutation owner. Its entire parent chain supplies static parameters and base properties.
    UMaterialInstanceConstant* CreatorInstance = nullptr;
    FString RuntimeObjectPath;
    bool Instanced = false;     // the material is also drawn by instanced static meshes
    // Through UE's shaders (P4UseSwitchShaderRoute): the vertex factories the mod draws it with (the hashed names
    // P4ValidateMaterialFactoryConsumers reports), each compiled with its usage (P4ApplyMaterialFactoryUsage).
    TArray<uint64> Factories;
};
struct KADAN_API FP4CreatorMaterialCompileResult {
    bool Succeeded = false;
    FString Error;
    // The inline shader map block for the cooked material, the map's hash and its programs in library order.
    std::vector<std::uint8_t> Block, MapHash;
    // The id the game finds the map by (through UE's shaders), empty otherwise.
    std::vector<std::uint8_t> MapId;
    std::vector<std::vector<std::uint8_t>> Programs;
    int32 Versions = 0;
};
// Read-only graph eligibility. Export compiles the exact graph again.
KADAN_API FString P4CreatorMaterialGraphIssue(UMaterial* Material);
// UI only: reads transient MaterialEditor previews, without origin admission.
KADAN_API FString P4CreatorMaterialEditorGraphIssue(UMaterial* Material);
KADAN_API FP4CreatorMaterialCompileResult P4CompileCreatorMaterial(const FP4CreatorMaterialCompileRequest& Request);
// Why an instance's own map cannot be compiled from this game master, or empty when it can (or the master
// is not the game's). Export availability and the compiler ask the same check.
KADAN_API FString P4GameParentCompileIssue(UMaterial* Master);
// True when a creator material is compiled through UE's own shaders for the game (P4CompileCreatorMaterialSwitch):
// whenever Kadan's GLSL_SWITCH format (the KadanShaderFormat module) is loaded, unless KADAN_SWITCH_ROUTE=0.
// Otherwise Kadan's own maps (own_export.h).
KADAN_API bool P4UseSwitchShaderRoute();

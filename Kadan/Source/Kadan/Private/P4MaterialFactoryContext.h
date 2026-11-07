#pragma once
#include "CoreMinimal.h"
class UMaterialInterface;
// The lookup checks actual compiled payload/resource references for this graph.
using FP4MaterialFactoryLookup = TFunction<bool(UMaterialInterface*, uint64, const FString&, FString&)>;
KADAN_API bool P4ValidateMaterialFactoryConsumers(UObject* Root, const TSet<UMaterialInterface*>& Graphs,
    const FP4MaterialFactoryLookup& Lookup, FString& Error);
// Static, instanced and spline mesh components: their OverrideMaterials and StaticMesh are material slots.
KADAN_API bool P4StaticMaterialFactoryComponent(const UObject* Object);
// The engine class a component draws as: its own class when that is the engine's, else the nearest engine class above
// it when everything between is a Blueprint (which cannot change how it draws) or the game's own C++ class (the game's
// shaders for its characters and scenery are those of the engine class: its Carrot mesh components draw as their
// engine parents). Null for any other class, whose drawing is never inferred.
KADAN_API const UClass* P4DrawnAsEngineClass(const UClass* Type);
class UMaterial;
// The usage flags a material needs so the engine compiles it for, and draws it with, these vertex factories (the hashed
// names P4ValidateMaterialFactoryConsumers reports): instanced, spline, skeletal and morph target meshes, mesh and
// sprite particles, beams and trails.
KADAN_API void P4ApplyMaterialFactoryUsage(UMaterial* Material, const TArray<uint64>& Factories);
// True when the material's usage flags cover this factory (as P4ApplyMaterialFactoryUsage sets them).
KADAN_API bool P4MaterialUsageCovers(const UMaterial* Material, uint64 Factory);
// True for a factory P4ApplyMaterialFactoryUsage gives a usage (or the plain static mesh's, which needs none).
KADAN_API bool P4KnownMaterialFactory(uint64 Factory);

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
// What the game's renderer cannot draw as set, changed to what it draws closest, on a copy compiled for the game and
// on the runtime material that ships (both get the same settings). UE 4.26 draws a Modulate material after depth of
// field only with dual-source blending, which its RHI never gives the Switch (RHISupportsDualSourceBlending), so the
// game's own engine refuses that combination: such a material draws in the ordinary translucency pass, before depth of
// field, as a Modulate material with Render After DOF off does.
KADAN_API void P4ApplyGameRenderLimits(UMaterial* Material);
// Refraction without the game's distortion pass: the game keeps it off (r.RefractionQuality 0), so a translucent
// material with a Refraction input composites the scene behind it at UE's own distortion offset in its translucent
// base pass (KadanSwitchSourcePatches.inl). On the copy compiled for the game only: it makes that copy read the
// scene colour, so the game copies the scene for it.
KADAN_API void P4ApplyGameRefraction(UMaterial* Material);
// True when the material's usage flags cover this factory (as P4ApplyMaterialFactoryUsage sets them).
KADAN_API bool P4MaterialUsageCovers(const UMaterial* Material, uint64 Factory);
// True for a factory P4ApplyMaterialFactoryUsage gives a usage (or the plain static mesh's, which needs none).
KADAN_API bool P4KnownMaterialFactory(uint64 Factory);

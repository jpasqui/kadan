#pragma once
#include "CoreMinimal.h"
#include "P4NativeCustomCompilerBridge.h"
#include "RHIDefinitions.h"

class FMaterialShaderMap;
class FMaterialResource;
class UMaterial;
class UMaterialInstance;

// What UMaterial::CacheResourceShadersForCooking does for a shader platform (it is private and not exported): a
// resource for the platform's feature level, its shaders cached through UE's compile jobs and Derived Data Cache.
// The default quality level only, for now. The caller frees the resource with P4ReleaseMaterialResource.
// Instance, when present, supplies effective base properties and the composited static parameter set.
// Switch resources apply per-material SSR policy after the shared layout lookup.
// A non-stock UMaterial subclass is refused rather than bypassing its allocator.
KADAN_API FMaterialResource* P4CacheMaterialForPlatform(UMaterial* Material, EShaderPlatform Platform, UMaterialInstance* Instance = nullptr);
// Frees a resource P4CacheMaterialForPlatform made, on the rendering thread after the render command that
// sets its compiled map for rendering.
KADAN_API void P4ReleaseMaterialResource(FMaterialResource* Resource);

// Legacy compile-context handshake. Layout selection does not read this state:
// the owned Switch resource's ShouldCache applies the material's SSR/C4 policy.
struct FP4SwitchMaterialContext
{
    explicit FP4SwitchMaterialContext(const UMaterial* Material);
    ~FP4SwitchMaterialContext();
    FP4SwitchMaterialContext(const FP4SwitchMaterialContext&) = delete;
    FP4SwitchMaterialContext& operator=(const FP4SwitchMaterialContext&) = delete;
};

// A material's shader map compiled for SP_SWITCH (Kadan's GLSL_SWITCH format), as the game's cooked material carries
// it: FMaterialResource::SerializeInlineShaderMap's resource (cooked, valid, the shader map id's quality, feature level,
// id hash and layout parameters, then FShaderMapBase::Serialize's frozen image, its patches, the pointer table and the
// type dependencies, and the code kept in the shader library by its hash). UE freezes the map with the game's memory
// layout (64-bit, no editor-only data, no ray tracing, MaxFieldAlignment 0xffffffff: the layout flags every game map
// records), the same writer UE's cook uses for a target platform. IdHash and LibraryHash are the map's in the game's
// terms (own_map_writer.h computes them). False with Error when the map is not compiled.
// Names are written as UE's material resource writer writes them: an index into the block's name table (Names, which
// gets each new name) and the name's number.
KADAN_API bool P4FreezeSwitchShaderMap(const FMaterialResource& Resource, const TArray<uint8>& IdHash, const TArray<uint8>& LibraryHash,
                             TArray<FName>& Names, TArray<uint8>& Out, FString& Error);

// Stage 8 of tools/navigation/OWN_SHADER_MAPS.md: a creator material through UE's own shaders. A transient copy with
// the runtime material's usage (static lighting, and instanced meshes when Request.Instanced) is compiled for
// SP_SWITCH by Kadan's format, frozen in the game's layout, and returned as the export takes Kadan's own maps: the
// inline block (name table, one location, the resource), the map's hash and its programs in library order.
FP4CreatorMaterialCompileResult P4CompileCreatorMaterialSwitch(const FP4CreatorMaterialCompileRequest& Request);
// P4UseSwitchShaderRoute (P4NativeCustomCompilerBridge.h) says when the export takes this route.

// While one is alive, UE's shader compiles run in the editor's process: UE's shader compile workers load only the
// engine's own formats and fail a GLSL_SWITCH job (an empty result the editor asserts on). The export holds one for as
// long as it compiles or cooks anything for the game. Nested ones restore what the outer one set.
struct KADAN_API FP4SwitchShadersInEditorProcess
{
    FP4SwitchShadersInEditorProcess();
    ~FP4SwitchShadersInEditorProcess();
    FP4SwitchShadersInEditorProcess(const FP4SwitchShadersInEditorProcess&) = delete;
    FP4SwitchShadersInEditorProcess& operator=(const FP4SwitchShadersInEditorProcess&) = delete;
private:
    bool bWas = false, bHeld = false;
};

#pragma once
#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "P4NativeCustomCompilerBridge.h"

class UMaterial;
class UMaterialInterface;
class UMaterialInstanceConstant;
class FProperty;
class UPackage;
class ITargetPlatform;

// Export-owned runtime representations. Graphs and statically overridden instances get private
// runtime copies (UMaterial or UMaterialInstanceConstant) with compiled inline shader maps and
// one shared shader library. A game parent remains a reference. Copies live only for this export.
class KADAN_API FP4TranslucentMaterialStaging
{
public:
    FP4TranslucentMaterialStaging() = default;
    ~FP4TranslucentMaterialStaging();
    FP4TranslucentMaterialStaging(const FP4TranslucentMaterialStaging&) = delete;
    FP4TranslucentMaterialStaging& operator=(const FP4TranslucentMaterialStaging&) = delete;
    // The game build family's shader format the maps are for (core/game_builds.h): GLSL_SWITCH, or SF_GLSL_SAGE, for
    // which each compiled map is converted (core/sage_own_map.h). Set before Prepare.
    void SetShaderFormat(const FString& Format) { ShaderFormat = Format; }
    bool Prepare(const FString& Root, const TArray<UObject*>& Assets, FString& Error);
    // A private object/package closure only. Concrete UMaterial references are
    // refused before changing anything; only MaterialInterface slots are remapped.
    bool Remap(UObject* Asset, FString& Error, bool KeepEditorState = true);
    bool Restore(FString& Error);
    bool Release(FString& Error);
    UObject* CookAsset(UObject* Authored) const;
    TArray<FString> RuntimePackages() const;
    bool IsRuntimeAsset(const UObject* Asset) const;
    FString RuntimePath(const FString& AuthoredPath) const;
    bool HasMaterials() const { return !Compiled.empty(); }
    bool HasDirectionalLightFunctions() const { return DirectionalLightFunctions; }
    // Before a runtime copy is cooked: the cook writes no shader map of the editor's for it (Kadan's takes its place).
    void BeginCook(UObject* Asset, const ITargetPlatform* Target) const;
    // After it is cooked to Base (.uasset/.uexp): Kadan's block goes in as its inline shader maps.
    bool WriteCookedBlock(UObject* Asset, const FString& Base, FString& Error) const;
    // The library holding every runtime copy's map.
    bool ShaderLibrary(TArray<uint8>& Out, FString& Error) const;
    static FString RuntimePackage(const FString& Root, const FString& GraphPackage);
    // Whether Kadan's own map for a graph covers the renderer a consumer draws it with (static meshes, and instanced
    // static meshes for opaque materials); else what to do.
    static FString RendererIssue(const UMaterialInterface* Graph, uint64 Factory, const FString& Consumer);
    // The same bounded slot inspection used by Remap, without compiling,
    // allocating runtime materials, or changing fields.
    static bool PreflightReferences(UObject* Asset, const TSet<UMaterialInterface*>& Graphs, const FString& ModRoot,
        FString& Error, bool* DirectionalFunctions = nullptr);

private:
    struct FKeptField
    {
        TStrongObjectPtr<UObject> Object;
        FProperty* Field = nullptr;
        void* Value = nullptr;
    };
    struct FKeptPackage
    {
        TStrongObjectPtr<UPackage> Package;
        bool Dirty = false;
    };
    FString ModRoot;
    FString ShaderFormat = TEXT("GLSL_SWITCH");
    bool DirectionalLightFunctions = false;
    TMap<UMaterialInterface*, UMaterialInterface*> Replacements;
    TArray<TStrongObjectPtr<UMaterialInterface>> Materials;
    struct FOriginalInstance
    {
        TStrongObjectPtr<UMaterialInstanceConstant> Instance;
        bool HadPermutation = false;
        mutable TSet<const ITargetPlatform*> AddedTargets;
    };
    TArray<FOriginalInstance> OriginalInstances;
    TArray<TStrongObjectPtr<UMaterial>> Graphs;
    TArray<FKeptField> Fields;
    TArray<FKeptPackage> Packages;
    std::vector<FP4CreatorMaterialCompileResult> Compiled;
    TMap<const UMaterialInterface*, int32> Positions; // authored material/instance or runtime copy -> compiled map
};

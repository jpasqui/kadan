#pragma once
#include "CoreMinimal.h"
#include "P4CurrentGameRegistry.h"

struct FP4KadanSourceAsset;
struct FP4KadanImportSettings;
class FP4OriginalZenSourceBatch;
class FJsonObject;

struct FP4KadanBlueprintPlans
{
    // The plans folder the Blueprint import reads: index.json, import/ and evidence/.
    FString Folder;
    // Packages with a plan, the ones asked for and the ones they need.
    TArray<FString> Planned;
    // Objects the Blueprints name that retoc could not name in the converted packages, read from the
    // original Zen packages. The import brings in the ones the game ships; the others stay empty references.
    TArray<FString> ReferenceOnlyPaths;
    int32 OriginalIdentityPackages = 0;
    // Packages without a plan, and why, in words: what the planner refused or what the plan could not hold.
    TMap<FString, FString> Refused;
    // Valid original packages whose editable reconstruction is unsupported.
    // Map callers keep these identities in the game; no class is synthesized.
    TMap<FString, FString> ReferenceOnlyReasons;
    // Missing/corrupt inputs and planner infrastructure failures are not an
    // unsupported-asset fallback. Callers must retain them as actual errors.
    TMap<FString, FString> SourceProblems;
    // The native modules whose declarations the plans were made with, and the ones that were not loaded.
    TArray<FString> Modules, ModulesNotLoaded;
    // The Blueprint packages that name each native module.
    TMap<FString, TArray<FString>> ModuleUsers;
    // Rebuilds every claimed generated UMG field from the retained immutable
    // native source, including verified original-import identities. JSON alone
    // cannot authorize omission of an authored variable.
    TFunction<bool(const FString&, const TSharedPtr<FJsonObject>&, FString&)> ValidateWidgetSource;
};

// What a native module the imported Blueprints name and this editor has not loaded means for them, as a sentence
// for the person: a plugin of this project to enable, or a module of a later engine than this one.
KADAN_API FString P4KadanUnloadedModuleNote(const FString& Module, const TArray<FString>& Users);

// Excludes editable plans transitively requiring an already-refused Blueprint;
// preserves valid original identities separately from missing/corrupt sources.
KADAN_API TSet<FString> P4KadanBlueprintReferenceClosure(
    const TMap<FString, TArray<FString>>& Required, FP4KadanBlueprintPlans& InOut);

// Plans the Blueprints of an import in C++, from the game's own files: the declarations of the native
// modules the Blueprints name, as this editor has them loaded, and every scanned package a plan reads.
// A plan whose admission issues are not empty is refused, by package. The plans are written under
// Folder, which is emptied first, in the layout decomp/blueprints/plan.py writes.
KADAN_API bool P4KadanPlanBlueprints(const TArray<TSharedPtr<FP4KadanSourceAsset>>& Scanned,
    const TArray<FString>& Packages, const FString& Folder, FP4KadanBlueprintPlans& Out, FString& Error);
// Unresolved original imports alone use the selected tools/containers. The
// explicit reader owner is reused across this call and the enclosing import;
// resolved packages cause no original-package acquisition.
KADAN_API bool P4KadanPlanBlueprints(const TArray<TSharedPtr<FP4KadanSourceAsset>>& Scanned,
    const TArray<FString>& Packages, const FString& Folder, FP4KadanBlueprintPlans& Out, FString& Error,
    const FP4KadanImportSettings& Settings, FP4CurrentGameRegistryPtr& SharedReader,
    FP4OriginalZenSourceBatch* OriginalBatch = nullptr);

#include "P4KadanTestSeams.h"
#if P4_KADAN_TEST_SEAMS && !(defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS)
// Development seam. Forces one explicitly selected dependency through the real loader and
// wraps only a genuine source-read exception. It cannot fabricate a successful read.
class FP4KadanBlueprintWrappedSourceControl
{
public:
    FP4KadanBlueprintWrappedSourceControl(const FString& InRoot, const FString& InDependency);
    ~FP4KadanBlueprintWrappedSourceControl();
    const FString& Root() const { return RootPackage; }
    const FString& Dependency() const { return DependencyPackage; }
    void RecordWrap() { ++Wrapped; }
    int32 WrappedFailures() const { return Wrapped; }
    FP4KadanBlueprintWrappedSourceControl(const FP4KadanBlueprintWrappedSourceControl&) = delete;
    FP4KadanBlueprintWrappedSourceControl& operator=(const FP4KadanBlueprintWrappedSourceControl&) = delete;

private:
    FString RootPackage, DependencyPackage;
    int32 Wrapped = 0;
    FP4KadanBlueprintWrappedSourceControl* Previous = nullptr;
};
#endif

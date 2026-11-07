#pragma once
#include "CoreMinimal.h"
#include "Exporters/Exporter.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "P4BlenderExport.generated.h"

class AActor;
class UMaterialInterface;
class UWorld;

// Unreal to Blender. File > Export Selected and File > Export All offer "Blender scene (*.json)": the
// actors' static meshes (of the selected actors, or of every actor in the level when none is selected) as
// the folder the TransMat add-on (tools/blender/transmat) imports.
//
//     <scene>.json                   a row for each placed mesh (mesh, transform, the material on each slot)
//                                    and the lists below
//     meshes/<name>.fbx              each mesh once, written by Unreal's own FBX exporter
//     textures/<name>.png            each texture the materials use
//     materials/<name>.json          each material: the node graph of its root material (material_t3d's form,
//                                    with each node's property values under their Python names), the root's
//                                    settings, and for a material instance its parameter values and textures
//
// The folders sit next to the scene file. Unreal lengths are centimeters and its Y axis is mirrored
// against Blender's; the add-on converts. A material that came from Blender carries its Blender nodes
// (the TransMat metadata tag of the asset), which the add-on uses when the material was not changed here.
// The files are the ones the old Python kit's Export for Blender wrote (ue4kit/p4blender_ue.py). That
// export took Static Mesh Actors only, and a Static Mesh Actor is still one row as it wrote it. A level
// of the game places its meshes with Blueprint actors, so any other actor gives a row for each of its
// static mesh components (and for each instance of an instanced one), with the component's transform
// and bounds and the extra keys actor, component and instance.
UCLASS()
class KADAN_API UP4BlenderSceneExporter : public UExporter
{
    GENERATED_BODY()
public:
    UP4BlenderSceneExporter();
    virtual bool ExportText(const FExportObjectInnerContext* Context, UObject* Object, const TCHAR* Type,
        FOutputDevice& Ar, FFeedbackContext* Warn, uint32 PortFlags = 0) override;
};

// Unreal to Blender for materials alone. Asset Actions > Export (or Bulk Export) of materials and
// material instances in the Content Browser offers "Blender materials (*.json)": a scene with no actors
// and no meshes, the material's JSON in materials/ and its textures in textures/ next to it. The add-on
// imports a folder of these and makes the Blender materials with no objects.
UCLASS()
class KADAN_API UP4BlenderMaterialExporter : public UExporter
{
    GENERATED_BODY()
public:
    UP4BlenderMaterialExporter();
    virtual bool ExportText(const FExportObjectInnerContext* Context, UObject* Object, const TCHAR* Type,
        FOutputDevice& Ar, FFeedbackContext* Warn, uint32 PortFlags = 0) override;
};

// The same exports for scripts and tests.
UCLASS()
class KADAN_API UKadanBlender : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Write the scene file for Blender, with its meshes, textures and materials in folders next to it. Actors are the actors whose static meshes are written; with none, the selected ones, or every one in the editor's level when none is selected. Returns the scene as JSON, or an empty string when nothing was written. */
    UFUNCTION(BlueprintCallable, Category = "Kadan|Blender")
    static FString ExportForBlender(const FString& SceneFile, const TArray<AActor*>& Actors);
    /** Write a scene file for Blender with these materials and instances alone (no actors, no meshes), their JSON and textures in folders next to it. Returns the scene as JSON, or an empty string when nothing was written. */
    UFUNCTION(BlueprintCallable, Category = "Kadan|Blender")
    static FString ExportMaterialsForBlender(const FString& SceneFile, const TArray<UMaterialInterface*>& Materials);
    /** Bring a game material the project does not hold into it through Kadan's importer (the Import page's route, so it is a read-only reference), from the game folder set in the Kadan panel. Returns an empty string when the material is in the project afterwards, or else what stopped it in whole sentences. */
    UFUNCTION(BlueprintCallable, Category = "Kadan|Blender")
    static FString ImportGameMaterial(const FString& Path);
    /** The game's packages as the Kadan panel's Import page reads them (Location is its Game folder, read through retoc, or with Extracted an extracted folder), as JSON {ok, error, packages: {package: kind}}. */
    UFUNCTION(BlueprintCallable, Category = "Kadan|Blender")
    static FString GameCatalogJson(const FString& Location, const FString& Retoc, bool Extracted = false);
    /** Import these game packages and what they need through the Import page's route, keeping what is already in the project. Returns the result as JSON (imported packages with their kind, not imported, reference only, problems). Repak is the Import page's repak, which game materials need. Reimport reruns the normal planner and importer instead of reusing existing imports. */
    UFUNCTION(BlueprintCallable, Category = "Kadan|Blender")
    static FString ImportGamePackages(const FString& Location, const FString& Retoc, const TArray<FString>& Packages,
        bool Extracted = false, const FString& Repak = TEXT(""), bool Reimport = false);
    /** One material as the JSON the export writes to its materials folder, or an empty string when the material has no material at the root of its parents. */
    UFUNCTION(BlueprintCallable, Category = "Kadan|Blender")
    static FString BlenderMaterialJson(UMaterialInterface* Material);
};

struct FP4BlenderScene
{
    int32 Actors = 0, Meshes = 0, Materials = 0, Textures = 0;
    TArray<FString> Notes;
    FString Json;
    FString Error;
};

// One placed mesh of the scene: a Static Mesh Actor, another actor's static mesh component, or one
// instance of an instanced component.
struct FP4BlenderPart
{
    class UStaticMeshComponent* Component = nullptr;
    int32 Instance = INDEX_NONE;
    FString Label;
    FTransform Transform;
    FVector Origin = FVector::ZeroVector, Extent = FVector::ZeroVector;
};
KADAN_API TArray<FP4BlenderPart> P4BlenderParts(AActor* Actor);

// Writes the folders next to SceneFile and returns the scene's JSON in Out.Json; the caller writes SceneFile.
KADAN_API bool P4ExportForBlender(
    UWorld* World, const FString& SceneFile, const TArray<AActor*>& Actors, FP4BlenderScene& Out);
KADAN_API bool P4ExportMaterialsForBlender(
    const FString& SceneFile, const TArray<UMaterialInterface*>& Materials, FP4BlenderScene& Out);
// The actors with static meshes the export takes when it is given none.
KADAN_API TArray<AActor*> P4BlenderSceneActors(UWorld* World, bool SelectedOnly);

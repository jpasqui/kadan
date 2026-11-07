#pragma once
#include "CoreMinimal.h"

struct FP4GamePreviewStartResult;
class UWorld;

// Inspection only: it neither saves authored assets nor cooks the current map.
// The plan holds the creator content the open level uses, and nothing else of the project.
struct FP4GamePreviewContentPlan
{
    FString Fingerprint;
    TArray<FString> SourcePackages;
    // A scratch world's reserved world and scene names are filled after the game reserves its preview
    // area, before Cook. Source worlds may be unsaved /Temp objects.
    FString WorldSourceObjectPath, WorldRuntimeName, SceneRuntimeName;
    FTransform StartTransform = FTransform::Identity;
    FVector StartFloorPoint = FVector::ZeroVector, StartFloorNormal = FVector::UpVector;
    bool HasStartFloor = false;
    TArray<FString> EnginePackages;
    // Actual build-data registry packages of the authored scratch level. These
    // require a scoped source->copy proof; they are not creator import assets.
    TArray<FString> DerivedPackages;
    TArray<FString> Warnings;
};

struct FP4GamePreviewContentResult
{
    FString Fingerprint, ContainerStem, PackageRoot;
    FString PakFile, UtocFile, UcasFile, ShaderFile, ShaderMetadataJson;
    FString WorldObjectPath, SceneObjectPath;
    // Object paths, including generated Blueprint classes and staged material MICs.
    TMap<FString, FString> RuntimePaths;
    bool FromCache = false;
};

// Both operations run on the editor thread. An empty plan succeeds without any
// game-file/tool settings. Cook reuses the native exporter on disposable package
// copies, with a fresh runtime namespace whenever creator content changes.
// The creator packages the world's own actors use, followed through their dependencies. Actors the
// game imported, and the game's content, end the walk. Fails naming an asset that can't go into Play.
KADAN_API bool P4GatherGamePreviewCreatorClosure(UWorld* World, TArray<FString>& Out, FString& Error);
bool P4GatherGamePreviewContent(
    FP4GamePreviewContentPlan& Out, FString& Error, const FP4GamePreviewStartResult* FrozenStart = nullptr);
bool P4CookGamePreviewContent(const FP4GamePreviewContentPlan& Plan, FP4GamePreviewContentResult& Out, FString& Error);

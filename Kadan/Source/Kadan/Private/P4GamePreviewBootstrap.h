#pragma once
#include "CoreMinimal.h"

struct FP4KadanImportSettings;
struct FP4GamePreviewTemplate;

struct FP4GamePreviewBootstrapPackage
{
    FString Package;
    FString Extension = TEXT(".umap");
    TArray<uint8> Header, Body;
};

// Private connector preview only. Opaque original exports remain in the cooked
// buffer to preserve package indices, but the runtime actor and streaming lists
// contain only the game bootstrap, the authored geometry and one captain.
struct FP4GamePreviewBootstrapResult
{
    FString WorldPackage, PlayerPackage;
    TArray<FP4GamePreviewBootstrapPackage> Packages;
    TArray<FString> RuntimeActors;
    // Scene.LoadAssets must preload the conventionally named arrival list.
    TArray<FString> LoadAssets;
};

// Template is the stock content to copy, from P4ReadGamePreviewTemplate.
bool P4BuildGamePreviewBootstrap(const FP4KadanImportSettings& Settings, const FP4GamePreviewTemplate& Template,
    const FString& PreviewRoot, const FString& WorldName, const FString& AreaName, const FString& AuthoredWorldPackage,
    const FTransform& Start, const FVector& FloorPoint, const FVector& FloorNormal, FP4GamePreviewBootstrapResult& Out,
    FString& Error);

// Bases use the validated ground contact, while the captain keeps the selected
// actor-centre transform. Ships stay upright and retain the selected yaw.
KADAN_API bool P4GamePreviewBaseAnchor(
    const FTransform& Start, const FVector& FloorPoint, const FVector& FloorNormal, FTransform& Out, FString& Error);

bool P4WriteGamePreviewBootstrap(const FP4GamePreviewBootstrapResult& Bootstrap, const FString& Stage,
    TArray<FString>& ExtraPackages, FString& Error);

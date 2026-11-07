#pragma once
#include "CoreMinimal.h"

enum class EP4ExportLightingPolicy : uint8 { RetainGame, BakeAddedMeshes };
enum class EP4ExportLightingQuality : uint8 { Preview, Medium, High, Production };
enum class EP4ExportNavigationPolicy : uint8 { RetainGame, RebuildEditedGeometry };

struct FP4ExportLightingRequest
{
    EP4ExportLightingPolicy Policy = EP4ExportLightingPolicy::RetainGame;
    EP4ExportLightingQuality Quality = EP4ExportLightingQuality::Production;
    // Explicit recovered-property transport input. It cannot manufacture build
    // success, source custody or permission to publish a lighting package.
    FString RecoveredSetupJson;
    FString EditorExecutable, ProjectFile;
    double TimeoutSeconds = 900;
};
struct FP4ExportNavigationRequest
{
    EP4ExportNavigationPolicy Policy = EP4ExportNavigationPolicy::RetainGame;
    // Actual changes and engine collision determine what needs rebuilding;
    // there is no caller-provided successful-build or verified-output flag.
};

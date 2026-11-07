#pragma once
#include "CoreMinimal.h"

class UWorld;

enum class EP4LightingBakeError : uint8
{
    None, EditorThreadRequired, LevelRequired, SetupUnreadable, SetupAlreadyPresent,
    SunSetupFailed, GameSunSetupFailed, StaticLightSetupFailed, ImportanceVolumeMalformed,
    ImportanceVolumeCreationFailed, SkySetupFailed, WorldSettingsMissing,
    ActorRemovalFailed, VolumeLightingRemovalFailed
};

struct KADAN_API FP4LightingBakeResult
{
    EP4LightingBakeError Error = EP4LightingBakeError::None;
    FString ErrorDetail, SkySource, SkyMaterial;
    TArray<FString> Placed, ValuesNotSet, TakenOut, Left;
    bool SetupCompleted = false, RemovalAttempted = false;
    bool LightingDataKnown = false, LightingData = false;
    bool BuiltLightingKnown = false, VolumeLightingTakenOut = false;
    int32 MeshesWithBuiltLighting = 0;
    bool IsValid() const { return Error == EP4LightingBakeError::None; }
};

// The recovered setup document remains a transport input; the result is native.
// Dynamic setup property decoding is an explicit legacy seam until its typed port.
KADAN_API FP4LightingBakeResult P4SetupLightingBakeFromJsonNative(UWorld* World, const FString& SetupJson);
KADAN_API FP4LightingBakeResult P4ClearLightingBakeNative(UWorld* World);
KADAN_API FString P4LightingBakeJson(const FP4LightingBakeResult& Result);

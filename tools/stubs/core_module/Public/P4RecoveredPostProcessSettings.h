// Real native storage for the observed game additions to logical Settings.
// Common renderer settings remain in UPostProcessComponent::Settings.
#pragma once
#include "CoreMinimal.h"
#include "P4RecoveredPostProcessSettings.generated.h"

class UTexture;

// Original meanings; this is deliberately distinct from stock Bokeh/Gaussian.
UENUM()
enum EP4RecoveredDepthOfFieldMethod
{
    P4DOFM_DefaultDOF=0,
    P4DOFM_GatherDOF=1,
    P4DOFM_MAX=2 UMETA(Hidden)
};

USTRUCT(BlueprintType)
struct P4STUBSCORE_API FP4RecoveredPostProcessForkSettings
{
    GENERATED_BODY()
    FP4RecoveredPostProcessForkSettings();

    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Game")
    uint8 bOverride_C4IndirectLightingVolumetricLightmapIntensity:1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Game")
    uint8 bOverride_C4IndirectLightingSpecularColor:1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Game")
    uint8 bOverride_C4IndirectLightingSpecularIntensity:1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Game")
    uint8 bOverride_DepthOfFieldTiltAngles:1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Game")
    uint8 bOverride_C4DepthOfFieldMask:1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Game")
    uint8 bOverride_C4DepthOfFieldParam0:1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Game")
    uint8 bOverride_C4DepthOfFieldParam1:1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Game")
    uint8 bOverride_C4DepthOfFieldParam2:1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Game")
    uint8 bOverride_C4DepthOfFieldParam3:1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Game")
    uint8 bOverride_C4PostProcessParam0:1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Game")
    uint8 bOverride_DepthOfFieldMethod:1;

    UPROPERTY(EditAnywhere,BlueprintReadWrite,Interp,Category="Game")
    float C4IndirectLightingVolumetricLightmapIntensity;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Interp,Category="Game")
    FLinearColor C4IndirectLightingSpecularColor;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Interp,Category="Game")
    float C4IndirectLightingSpecularIntensity;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Interp,Category="Game")
    FVector2D DepthOfFieldTiltAngles;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Game")
    UTexture* C4DepthOfFieldMask;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Interp,Category="Game")
    FVector4 C4DepthOfFieldParam0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Interp,Category="Game")
    FVector4 C4DepthOfFieldParam1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Interp,Category="Game")
    FVector4 C4DepthOfFieldParam2;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Interp,Category="Game")
    FVector4 C4DepthOfFieldParam3;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Interp,Category="Game")
    FVector4 C4PostProcessParam0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,AdvancedDisplay,Category="Game")
    TEnumAsByte<EP4RecoveredDepthOfFieldMethod> DepthOfFieldMethod;
};

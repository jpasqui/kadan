#include "P4RecoveredPostProcessComponent.h"
#include "Engine/World.h"

FP4RecoveredPostProcessForkSettings::FP4RecoveredPostProcessForkSettings()
{
    bOverride_C4IndirectLightingVolumetricLightmapIntensity=false;
    bOverride_C4IndirectLightingSpecularColor=false;
    bOverride_C4IndirectLightingSpecularIntensity=false;
    bOverride_DepthOfFieldTiltAngles=false;
    bOverride_C4DepthOfFieldMask=false;
    bOverride_C4DepthOfFieldParam0=false;
    bOverride_C4DepthOfFieldParam1=false;
    bOverride_C4DepthOfFieldParam2=false;
    bOverride_C4DepthOfFieldParam3=false;
    bOverride_C4PostProcessParam0=false;
    bOverride_DepthOfFieldMethod=false;
    C4IndirectLightingVolumetricLightmapIntensity=1.f;
    C4IndirectLightingSpecularColor=FLinearColor(1.f,1.f,1.f,1.f);
    C4IndirectLightingSpecularIntensity=1.f;
    DepthOfFieldTiltAngles=FVector2D(0.f,0.f);
    C4DepthOfFieldMask=nullptr;
    C4DepthOfFieldParam0=FVector4(0.f,0.f,0.f,0.f);
    C4DepthOfFieldParam1=FVector4(1.f,1.f,0.f,0.f);
    C4DepthOfFieldParam2=FVector4(1.f,1.f,0.f,0.f);
    C4DepthOfFieldParam3=FVector4(0.f,0.f,0.f,0.f);
    C4PostProcessParam0=FVector4(0.f,0.f,0.f,0.f);
    DepthOfFieldMethod=P4DOFM_DefaultDOF;
}
UP4RecoveredPostProcessComponent::UP4RecoveredPostProcessComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}
void UP4RecoveredPostProcessComponent::OnRegister()
{
    USceneComponent::OnRegister();
    GetWorld()->InsertPostProcessVolume(this);
}
void UP4RecoveredPostProcessComponent::OnUnregister()
{
    USceneComponent::OnUnregister();
    GetWorld()->RemovePostProcessVolume(this);
}
void UP4RecoveredPostProcessComponent::Serialize(FArchive& Ar)
{
    USceneComponent::Serialize(Ar);
    if(Ar.IsLoading())
    {
#if WITH_EDITORONLY_DATA
        Settings.OnAfterLoad();
#endif
    }
}

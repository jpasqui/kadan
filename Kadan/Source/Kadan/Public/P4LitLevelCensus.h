#pragma once
#include "CoreMinimal.h"

class UWorld;

struct FP4LitTextureIdentity
{
    FString Object, Class;
};
struct FP4LitMeshCensus
{
    FString Actor, Component;
    int32 Lod = 0;
    FGuid MapBuildDataId;
    bool HasLightMap = false, HasShadowMap = false;
    TArray<FP4LitTextureIdentity> LightMapTextures; // nonnull basis, sky-occlusion and AO references
    FP4LitTextureIdentity ShadowMapTexture;
    FVector2D LightMapScale = FVector2D::ZeroVector, LightMapBias = FVector2D::ZeroVector;
    FVector2D ShadowMapScale = FVector2D::ZeroVector, ShadowMapBias = FVector2D::ZeroVector;
    TArray<FGuid> IrrelevantLights;
};
enum class EP4LitLevelCensusError : uint8
{
    None,
    EditorThreadRequired,
    LevelRequired,
    ActorsRequired,
    RegistryRequired,
    RegistryOwnershipDiffers,
    ActorUnsupported,
    MeshUnsupported,
    LodUnsupported,
    RegistryRecordMissing,
    OverrideDataUnsupported,
    LightingTypeUnsupported,
    TextureOwnershipDiffers,
    NonfiniteCoordinates
};
struct FP4LitLevelCensusResult
{
    EP4LitLevelCensusError Error = EP4LitLevelCensusError::None;
    FString ErrorDetail, MapPackage, RegistryPackage;
    TArray<FP4LitMeshCensus> Meshes;
    bool IsValid() const { return Error == EP4LitLevelCensusError::None; }
};

// Live ownership census only. It does not establish a lighting build, source
// actor invariance, full registry semantics, texture resource content or export admission.
KADAN_API FP4LitLevelCensusResult P4InspectLitLevelCensusNative(UWorld* World, const TArray<FString>& ActorNames);

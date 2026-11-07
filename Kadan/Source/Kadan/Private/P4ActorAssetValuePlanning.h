#pragma once
#include "CoreMinimal.h"
#include "P4MapKitCore/export_contract.h"
class FProperty;
using FP4ActorValues = std::map<std::string, P4MapKit::Export::SetValue>;
struct FP4ActorAssetReference
{
    FString Actor, Path, Target;
    bool MaterialInterface = false;
    bool MaterialSlot = false, StaticMesh = false, MeshSlot = false;
    TWeakObjectPtr<const UObject> Owner;
    FName Property;
    bool operator==(const FP4ActorAssetReference& Other) const
    { return Actor == Other.Actor && Path == Other.Path && Target == Other.Target && MaterialInterface == Other.MaterialInterface && MaterialSlot == Other.MaterialSlot && StaticMesh == Other.StaticMesh && MeshSlot == Other.MeshSlot && Owner == Other.Owner && Property == Other.Property; }
};
// Native-only planning metadata. The neutral map file schema stays unchanged.
KADAN_API bool P4PlanActorValue(const FProperty* Property, const void* Data, const FString& Path,
    const FString& Actor, const UObject* Within, const UObject* Owner, FP4ActorValues& Out, TArray<FString>& References,
    TArray<FP4ActorAssetReference>& AssetValues, FString& Error);
KADAN_API bool P4RemapActorAssetValue(FP4ActorValues& Values, const FP4ActorAssetReference& Reference,
    const FString& Runtime, FString& Error);

KADAN_API bool P4ValidateActorShaderContext(const FP4ActorAssetReference& Reference,bool UsesNewShader,FString& Error);

#include "P4MaterialFactoryContext.h"
#include "P4GameNames.h"
#include "P4MapKitCore/material_engine_hashes.h"
#include "P4MaterialReferenceContext.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionGetMaterialAttributes.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionSetMaterialAttributes.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModuleRequired.h"
#include "Particles/TypeData/ParticleModuleTypeDataMesh.h"
#include "Particles/TypeData/ParticleModuleTypeDataBeam2.h"
#include "Particles/TypeData/ParticleModuleTypeDataRibbon.h"
#include "Particles/TypeData/ParticleModuleTypeDataGpu.h"
namespace
{
// Exact engine hashed factory names already recorded in material_factory.h.
constexpr uint64 Local = P4MapKit::EngineHash::LocalVertexFactory,
                 Instanced = P4MapKit::EngineHash::InstancedStaticMeshVertexFactory, Spline = 0x872b1640e244ca06ull,
                 Skin = P4MapKit::EngineHash::GpuSkinVertexFactory, Morph = 0xdd651790dd308138ull,
                 MeshParticle = 0x6d728306d56d412full, Sprite = 0x1adefad780cd16c5ull,
                 GPUSprite = 0x00080c76633abf12ull, BeamTrail = 0x2f7fb09897dda57cull;
UMaterialInterface* FindGraph(UMaterialInterface* Material, const TSet<UMaterialInterface*>& Graphs, FString& Error)
{
    TSet<UMaterialInterface*> Seen;
    for (int32 Depth = 0; Material; ++Depth)
    {
        if (Depth >= 64 || Seen.Contains(Material))
        {
            Error = TEXT("Material factory inspection found a cyclic or excessive parent chain.");
            return nullptr;
        }
        Seen.Add(Material);
        if (Graphs.Contains(Material))
            return Material;
        UMaterialInstance* Instance = Cast<UMaterialInstance>(Material);
        Material = Instance ? Instance->Parent : nullptr;
    }
    return nullptr;
}
uint64 ComponentFactory(const UPrimitiveComponent* Component)
{
    const UClass* Type = P4DrawnAsEngineClass(Component->GetClass());
    if (Type == UStaticMeshComponent::StaticClass())
        return Local;
    if (Type == UInstancedStaticMeshComponent::StaticClass() ||
        Type == UHierarchicalInstancedStaticMeshComponent::StaticClass())
        return Instanced;
    if (Type == USplineMeshComponent::StaticClass())
        return Spline;
    if (Type == USkeletalMeshComponent::StaticClass())
        return Skin;
    return 0; // A subclass may replace its scene proxy/factory; never infer it.
}
} //namespace
void P4ApplyMaterialFactoryUsage(UMaterial* Material, const TArray<uint64>& Factories)
{
    if (!Material)
        return;
    for (const uint64 Factory : Factories)
    {
        if (Factory == Instanced)
            Material->bUsedWithInstancedStaticMeshes = true;
        else if (Factory == Spline)
            Material->bUsedWithSplineMeshes = true;
        else if (Factory == Skin)
            Material->bUsedWithSkeletalMesh = true;
        else if (Factory == Morph)
        {
            Material->bUsedWithSkeletalMesh = true;
            Material->bUsedWithMorphTargets = true;
        }
        else if (Factory == MeshParticle)
            Material->bUsedWithMeshParticles = true;
        else if (Factory == Sprite || Factory == GPUSprite)
            Material->bUsedWithParticleSprites = true;
        else if (Factory == BeamTrail)
            Material->bUsedWithBeamTrails = true;
    }
}
void P4ApplyGameRenderLimits(UMaterial* Material)
{
    if (Material && Material->GetBlendMode() == BLEND_Modulate && Material->bEnableSeparateTranslucency)
        Material->bEnableSeparateTranslucency = false;
    // An ambient occlusion decal: the game's forward renderer has no ambient occlusion decal stage (UE draws one only
    // into deferred SSAO), so it draws as a DBuffer colour decal of black at opacity (1 - AO) x opacity, which leaves
    // base colour x lerp(1, AO, opacity): the same darkening, through the colour buffer the game's base pass reads.
    if (Material && Material->MaterialDomain == MD_DeferredDecal && Material->DecalBlendMode == DBM_AmbientOcclusion &&
        Material->AmbientOcclusion.Expression)
    {
        const FExpressionInput Opacity = Material->Opacity;
        auto* Covered = NewObject<UMaterialExpressionMultiply>(Material); // AO x opacity
        Covered->A = Material->AmbientOcclusion;
        if (Opacity.Expression)
            Covered->B = Opacity;
        else
            Covered->ConstB = 1.f;
        auto* Negated = NewObject<UMaterialExpressionMultiply>(Material);
        Negated->A.Expression = Covered;
        Negated->ConstB = -1.f;
        auto* Left = NewObject<UMaterialExpressionAdd>(Material); // opacity - AO x opacity
        if (Opacity.Expression)
            Left->A = Opacity;
        else
            Left->ConstA = 1.f;
        Left->B.Expression = Negated;
        auto* Black = NewObject<UMaterialExpressionConstant3Vector>(Material);
        Black->Constant = FLinearColor::Black;
        Material->Expressions.Append({Covered, Negated, Left, Black});
        Material->Opacity.Expression = Left;
        Material->Opacity.OutputIndex = 0;
        Material->Opacity.Mask = 0;
        Material->BaseColor.Expression = Black;
        Material->BaseColor.OutputIndex = 0;
        Material->BaseColor.Mask = 0;
        // An AO decal draws nothing else (stock returns after its AO): no emissive pass of its own.
        Material->EmissiveColor.Expression = nullptr;
        Material->DecalBlendMode = DBM_DBuffer_Color;
    }
}
void P4ApplyGameRefraction(UMaterial* Material)
{
    if (!Material || !Material->bUsesDistortion || !IsTranslucentBlendMode(Material->GetBlendMode()))
        return;
    if (Material->bUseMaterialAttributes && !Material->MaterialAttributes.Expression)
        return;
    // Scene Color x 0 in the emissive: compiles to nothing, but marks the material as reading the scene colour, so
    // the game copies the scene before its translucency for it (the copy the refraction patch samples).
    // The engine does not export the Scene Color expression's class: found by name.
    UClass* SceneClass = FindObject<UClass>(ANY_PACKAGE, TEXT("MaterialExpressionSceneColor"));
    if (!SceneClass)
        return;
    auto* Scene = NewObject<UMaterialExpression>(Material, SceneClass);
    auto* Zero = NewObject<UMaterialExpressionMultiply>(Material);
    Zero->A.Expression = Scene;
    Zero->ConstB = 0.f;
    Material->Expressions.Add(Scene);
    Material->Expressions.Add(Zero);
    if (Material->bUseMaterialAttributes)
    {
        // The attributes, with their emissive plus the marker: Set Material Attributes over them, its emissive the
        // attributes' own (Get Material Attributes) plus Scene Color x 0.
        const FGuid Emissive = FMaterialAttributeDefinitionMap::GetID(MP_EmissiveColor);
        auto* Get = NewObject<UMaterialExpressionGetMaterialAttributes>(Material);
        Get->MaterialAttributes.Expression = Material->MaterialAttributes.Expression;
        Get->MaterialAttributes.OutputIndex = Material->MaterialAttributes.OutputIndex;
        Get->AttributeGetTypes.Add(Emissive);
        Get->Outputs.Add(FExpressionOutput(TEXT("Emissive")));
        auto* Sum = NewObject<UMaterialExpressionAdd>(Material);
        Sum->A.Expression = Get;
        Sum->A.OutputIndex = 1;
        Sum->B.Expression = Zero;
        auto* Set = NewObject<UMaterialExpressionSetMaterialAttributes>(Material);
        if (Set->Inputs.Num() == 0)
            Set->Inputs.Add(FMaterialAttributesInput());
        Set->Inputs[0].Expression = Material->MaterialAttributes.Expression;
        Set->Inputs[0].OutputIndex = Material->MaterialAttributes.OutputIndex;
        Set->AttributeSetTypes.Add(Emissive);
        FExpressionInput Value;
        Value.Expression = Sum;
        Set->Inputs.Add(Value);
        Material->Expressions.Append({Get, Sum, Set});
        Material->MaterialAttributes.Expression = Set;
        Material->MaterialAttributes.OutputIndex = 0;
        return;
    }
    UMaterialExpression* Result = Zero;
    if (Material->EmissiveColor.Expression)
    {
        auto* Sum = NewObject<UMaterialExpressionAdd>(Material);
        Sum->A = Material->EmissiveColor;
        Sum->B.Expression = Zero;
        Material->Expressions.Add(Sum);
        Result = Sum;
    }
    Material->EmissiveColor.Expression = Result;
    Material->EmissiveColor.OutputIndex = 0;
    Material->EmissiveColor.Mask = 0;
}
bool P4MaterialUsageCovers(const UMaterial* Material, uint64 Factory)
{
    if (!Material)
        return false;
    if (Factory == Local)
        return true;
    if (Factory == Instanced)
        return Material->bUsedWithInstancedStaticMeshes;
    if (Factory == Spline)
        return Material->bUsedWithSplineMeshes;
    if (Factory == Skin)
        return Material->bUsedWithSkeletalMesh;
    if (Factory == Morph)
        return Material->bUsedWithSkeletalMesh && Material->bUsedWithMorphTargets;
    if (Factory == MeshParticle)
        return Material->bUsedWithMeshParticles;
    if (Factory == Sprite || Factory == GPUSprite)
        return Material->bUsedWithParticleSprites;
    if (Factory == BeamTrail)
        return Material->bUsedWithBeamTrails;
    return false;
}
bool P4KnownMaterialFactory(uint64 Factory)
{
    return Factory == Local || Factory == Instanced || Factory == Spline || Factory == Skin || Factory == Morph ||
        Factory == MeshParticle || Factory == Sprite || Factory == GPUSprite || Factory == BeamTrail;
}
bool P4StaticMaterialFactoryComponent(const UObject* Object)
{
    const UClass* Type = Object ? P4DrawnAsEngineClass(Object->GetClass()) : nullptr;
    return Type &&
        (Type == UStaticMeshComponent::StaticClass() || Type == UInstancedStaticMeshComponent::StaticClass() ||
            Type == UHierarchicalInstancedStaticMeshComponent::StaticClass() ||
            Type == USplineMeshComponent::StaticClass());
}
const UClass* P4DrawnAsEngineClass(const UClass* Type)
{
    for (; Type; Type = Type->GetSuperClass())
    {
        const FString Package = Type->GetOutermost()->GetName();
        if (Package == TEXT("/Script/Engine"))
            return Type;
        if (Type->HasAnyClassFlags(CLASS_Native) && Package != TEXT(P4_GAME_SCRIPT_PACKAGE))
            return nullptr;
    }
    return nullptr;
}
bool P4ValidateMaterialFactoryConsumers(
    UObject* Root, const TSet<UMaterialInterface*>& Graphs, const FP4MaterialFactoryLookup& Lookup, FString& Error)
{
    Error.Reset();
    if (Graphs.Num() == 0)
        return true;
    TArray<UObject*> Objects;
    if (!P4GatherCreatorReferenceObjects(Root, Objects, Error))
        return false;
    const auto Check = [&](UMaterialInterface* Material, uint64 Factory, const FString& Consumer) -> bool
    {
        UMaterialInterface* Graph = FindGraph(Material, Graphs, Error);
        if (!Error.IsEmpty())
            return false;
        if (!Graph)
            return true;
        if (!Factory)
        {
            Error = Consumer + TEXT(" uses a creator material through an unsupported renderer.");
            return false;
        }
        if (Lookup(Graph, Factory, Consumer, Error))
            return true;
        // Keep the compiler/lookup's specific explanation; UE's route supports the skin factories too.
        if (Error.IsEmpty())
            Error = Consumer + TEXT(" uses ") + Graph->GetName() +
                TEXT(
                    ", whose compiled shaders do not cover this renderer. Recompile the material for this renderer before exporting.");
        return false;
    };
    for (UObject* Object : Objects)
    {
        if (UStaticMesh* Mesh = Cast<UStaticMesh>(Object))
        {
            if (Mesh->StaticMaterials.Num() > 256)
            {
                Error = TEXT("Static material factory inspection exceeds its bounded slot limit.");
                return false;
            }
            for (const FStaticMaterial& Slot : Mesh->StaticMaterials)
                if (!Check(Slot.MaterialInterface, Local, Mesh->GetPathName()))
                    return false;
        }
        if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(Object))
        {
            if (Mesh->Materials.Num() > 256)
            {
                Error = TEXT("Skeletal material factory inspection exceeds its bounded slot limit.");
                return false;
            }
            for (const FSkeletalMaterial& Slot : Mesh->Materials)
                if (!Check(Slot.MaterialInterface, Skin, Mesh->GetPathName()) ||
                    (Mesh->MorphTargets.Num() && !Check(Slot.MaterialInterface, Morph, Mesh->GetPathName())))
                    return false;
        }
        if (UPrimitiveComponent* Component = Cast<UPrimitiveComponent>(Object))
        {
            const int32 Count = Component->GetNumMaterials();
            if (Count < 0 || Count > 256)
            {
                Error = TEXT("Component material factory inspection exceeds its bounded slot limit.");
                return false;
            }
            for (int32 Slot = 0; Slot < Count; ++Slot)
                if (!Check(Component->GetMaterial(Slot), ComponentFactory(Component), Component->GetPathName()))
                    return false;
            if (USkeletalMeshComponent* SkinComponent = Cast<USkeletalMeshComponent>(Component))
                if (SkinComponent->SkeletalMesh && SkinComponent->SkeletalMesh->MorphTargets.Num())
                    for (int32 Slot = 0; Slot < Count; ++Slot)
                        if (!Check(Component->GetMaterial(Slot), Morph, Component->GetPathName()))
                            return false;
        }
        if (UParticleSystem* System = Cast<UParticleSystem>(Object))
        {
            if (System->Emitters.Num() > 4096)
            {
                Error = TEXT("Particle material inspection exceeds its bounded emitter limit.");
                return false;
            }
            for (UParticleEmitter* Emitter : System->Emitters)
                if (Emitter)
                {
                    if (Emitter->LODLevels.Num() > 16)
                    {
                        Error = TEXT("Particle material inspection exceeds its bounded LOD limit.");
                        return false;
                    }
                    for (UParticleLODLevel* LOD : Emitter->LODLevels)
                        if (LOD && LOD->RequiredModule)
                        {
                            uint64 Factory = Sprite;
                            if (LOD->TypeDataModule)
                            {
                                const UClass* Type = LOD->TypeDataModule->GetClass();
                                Factory = Type == UParticleModuleTypeDataMesh::StaticClass() ? MeshParticle
                                    : Type == UParticleModuleTypeDataGpu::StaticClass()      ? GPUSprite
                                    : (Type == UParticleModuleTypeDataBeam2::StaticClass() ||
                                          Type == UParticleModuleTypeDataRibbon::StaticClass())
                                    ? BeamTrail
                                    : 0;
                            }
                            if (!Check(LOD->RequiredModule->Material, Factory, LOD->GetPathName()))
                                return false;
                        }
                }
        }
    }
    return true;
}

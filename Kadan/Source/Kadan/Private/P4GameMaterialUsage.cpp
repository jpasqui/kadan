#include "P4GameMaterialUsage.h"
#include "P4Common.h"
#include "P4GamePreviewNavigation.h"
#include "P4OriginalRecord.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModuleRequired.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Particles/TypeData/ParticleModuleTypeDataAnimTrail.h"
#include "Particles/TypeData/ParticleModuleTypeDataBeam2.h"
#include "Particles/TypeData/ParticleModuleTypeDataMesh.h"
#include "Particles/TypeData/ParticleModuleTypeDataRibbon.h"
#include "Misc/Paths.h"
#include "P4MapKitCore/package_reader.h"
#include "P4MapKitCore/material_cooked.h"

namespace
{
const char* const UsageFlags[] = {"bUsedWithSkeletalMesh", "bUsedWithMorphTargets", "bUsedWithParticleSprites",
    "bUsedWithBeamTrails", "bUsedWithMeshParticles", "bUsedWithNiagaraSprites", "bUsedWithNiagaraRibbons",
    "bUsedWithNiagaraMeshParticles", "bUsedWithInstancedStaticMeshes", "bUsedWithSplineMeshes", "bUsedWithClothing",
    "bUsedWithGeometryCache", "bUsedWithStaticLighting"};

bool ReadUsage(const FP4KadanImportSettings& Containers, const FString& Package, TSet<FName>& Out, FString& Error)
{
    FString Header;
    std::vector<std::uint8_t> Head, Body;
    if (!P4AcquireGamePreviewSource(Containers, Package, Header, Error))
        return false;
    if (!P4LoadFileBytes(Header, Head) || !P4LoadFileBytes(FPaths::ChangeExtension(Header, TEXT("uexp")), Body))
    {
        Error = TEXT("Its files could not be read.");
        return false;
    }
    const P4MapKit::FPackageInventory Inventory = P4MapKit::ReadPackageHeader(Head, Body.size());
    P4MapKit::FMaterialCookedRecord Record;
    P4MapKit::FReadError Problem;
    if (!P4MapKit::ReadMaterialCookedRecord(Inventory, Body, Record, Problem))
    {
        Error = UTF8_TO_TCHAR(Problem.Code.c_str());
        return false;
    }
    for (const char* Flag : UsageFlags)
    {
        const auto* Field = Record.Properties.Field(Flag);
        bool Value = false;
        // A flag the file does not hold is at its default, off.
        if (Field && P4MapKit::MaterialBool(*Field, Value) && Value)
            Out.Add(FName(Flag));
    }
    return true;
}

struct FUse
{
    UMaterialInterface* Material;
    FName Flag;
};

void AddMaterials(UPrimitiveComponent* Part, FName Flag, TArray<FUse>& Out)
{
    TArray<UMaterialInterface*> Materials;
    Part->GetUsedMaterials(Materials);
    for (UMaterialInterface* Material : Materials)
        Out.Add({Material, Flag});
}

void AddEmitters(const UParticleSystem* System, TArray<FUse>& Out)
{
    for (const UParticleEmitter* Emitter : System->Emitters)
    {
        const UParticleLODLevel* Level = Emitter && Emitter->LODLevels.Num() ? Emitter->LODLevels[0] : nullptr;
        if (!Level || !Level->RequiredModule)
            continue;
        const UParticleModule* Type = Level->TypeDataModule;
        if (const auto* Meshes = Cast<UParticleModuleTypeDataMesh>(Type))
        {
            if (Meshes->bOverrideMaterial || !Meshes->Mesh)
                Out.Add({Level->RequiredModule->Material, TEXT("bUsedWithMeshParticles")});
            else
                for (const FStaticMaterial& Slot : Meshes->Mesh->StaticMaterials)
                    Out.Add({Slot.MaterialInterface, TEXT("bUsedWithMeshParticles")});
        }
        else
            Out.Add({Level->RequiredModule->Material,
                Type && (Type->IsA<UParticleModuleTypeDataBeam2>() || Type->IsA<UParticleModuleTypeDataRibbon>() ||
                            Type->IsA<UParticleModuleTypeDataAnimTrail>())
                    ? TEXT("bUsedWithBeamTrails")
                    : TEXT("bUsedWithParticleSprites")});
    }
}

// The usage each material of a part needs. A plain static mesh needs one only with baked lighting.
void Uses(UPrimitiveComponent* Part, bool Baked, TArray<FUse>& Out)
{
    if (Part->IsA<UInstancedStaticMeshComponent>())
        AddMaterials(Part, TEXT("bUsedWithInstancedStaticMeshes"), Out);
    else if (Part->IsA<USplineMeshComponent>())
        AddMaterials(Part, TEXT("bUsedWithSplineMeshes"), Out);
    else if (const auto* Skinned = Cast<USkeletalMeshComponent>(Part))
    {
        AddMaterials(Part, TEXT("bUsedWithSkeletalMesh"), Out);
        if (Skinned->SkeletalMesh && Skinned->SkeletalMesh->MorphTargets.Num())
            AddMaterials(Part, TEXT("bUsedWithMorphTargets"), Out);
    }
    else if (const auto* Particles = Cast<UParticleSystemComponent>(Part))
    {
        // The game's own effects draw as the game made them.
        if (Particles->Template && !P4QueryOriginalAsset(Particles->Template).IsOriginal())
            AddEmitters(Particles->Template, Out);
    }
    else if (Baked && Part->IsA<UStaticMeshComponent>() && Part->Mobility == EComponentMobility::Static)
        AddMaterials(Part, TEXT("bUsedWithStaticLighting"), Out);
}
} // namespace

void FP4GameMaterialUsage::WhyDefaultMaterial(const AActor* Actor, bool Baked, TArray<FString>& Out)
{
    if (!Actor)
        return;
    TInlineComponentArray<UPrimitiveComponent*> Parts(Actor);
    for (UPrimitiveComponent* Part : Parts)
    {
        TArray<FUse> Needed;
        Uses(Part, Baked, Needed);
        TSet<UMaterialInterface*> Said;
        for (const FUse& Use : Needed)
        {
            FString Problem;
            if (!Use.Material || Said.Contains(Use.Material) || Allows(Use.Material, Use.Flag, Problem))
                continue;
            Said.Add(Use.Material);
            Out.Add(FString::Printf(
                TEXT("%s draws with the game's default material in the game, because the game's master of %s does not have %s.%s"),
                *Actor->GetActorLabel(), *Use.Material->GetName(), *Use.Flag.ToString(),
                Problem.IsEmpty() ? TEXT("") : *(TEXT(" ") + Problem)));
        }
    }
}

bool FP4GameMaterialUsage::Allows(UMaterialInterface* Material, FName Flag, FString& Error)
{
    Error.Reset();
    const UMaterial* Master = Material ? Material->GetMaterial() : nullptr;
    if (!Master || !P4QueryOriginalAsset(const_cast<UMaterial*>(Master)).IsOriginal())
        return true;
    const FName Package = Master->GetOutermost()->GetFName();
    if (const FString* Why = Unread.Find(Package))
    {
        Error = *Why;
        return false;
    }
    if (const TSet<FName>* Known = Read.Find(Package))
        return Known->Contains(Flag);
    TSet<FName> Usage;
    FString Why;
    if (!ReadUsage(Containers, Package.ToString(), Usage, Why))
    {
        Error = Why.IsEmpty() ? FString(TEXT("It could not be read.")) : Why;
        Unread.Add(Package, Error);
        return false;
    }
    const bool Allowed = Usage.Contains(Flag);
    Read.Add(Package, MoveTemp(Usage));
    return Allowed;
}

#include "P4CollisionHelperDisplay.h"
#include "P4OriginalRecord.h"
#include "P4LevelContent.h"
#include "P4StaticMeshContent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ShapeComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/BillboardComponent.h"
#include "Components/DecalComponent.h"
#include "Components/LightComponent.h"
#include "Components/ChildActorComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "UObject/Package.h"

int32 P4HideOriginalCollisionHelpers(UWorld* World)
{
    if (!World || World->WorldType != EWorldType::Editor)
        return 0;
    TMap<UStaticMesh*, bool> Qualified;
    int32 Hidden = 0;
    const auto SourceCollision = [&](UStaticMesh* Mesh)
    {
        if (const bool* Cached = Qualified.Find(Mesh))
            return *Cached;
        // An original mesh whose every material slot is empty in the game draws nothing of its own. The mesh
        // import refuses a material missing from the project, so an empty slot is empty in the game too.
        FP4OriginalRecord Record;
        FString Error;
        bool Good = Mesh && Mesh->BodySetup && P4ReadOriginalRecord(Mesh, Record, Error) &&
            Record.Kind == TEXT("static-mesh") && Record.Package == Mesh->GetOutermost()->GetName() &&
            Record.SourceDigest.Len() == 64;
        if (Good)
            for (const FStaticMaterial& Slot : Mesh->StaticMaterials)
                Good &= Slot.MaterialInterface == nullptr;
        if (Good)
            Good = P4OriginalMeshBaselineUnchanged(Mesh);
        Qualified.Add(Mesh, Good);
        return Good;
    };
    for (ULevel* Level : World->GetLevels())
        if (Level)
        {
            UWorld* SourceWorld = Cast<UWorld>(Level->GetOuter());
            FP4OriginalRecord Record;
            FString Error;
            if (!SourceWorld || !P4ReadOriginalRecord(SourceWorld, Record, Error) || Record.Kind != TEXT("level"))
                continue;
            const TSet<FName> Originals = P4ImportedLevelActors(SourceWorld);
            for (AActor* Actor : Level->Actors)
                if (IsValid(Actor) && Originals.Contains(Actor->GetFName()))
                {
                    TInlineComponentArray<UStaticMeshComponent*> Meshes(Actor);
                    bool Collision = false, OnlyHelpers = true;
                    TInlineComponentArray<UPrimitiveComponent*> Primitives(Actor);
                    for (UPrimitiveComponent* Part : Primitives)
                        if (!Part->IsA<UStaticMeshComponent>() && !Part->IsA<UShapeComponent>() &&
                            !Part->IsA<UArrowComponent>() && !Part->IsA<UBillboardComponent>() && !Part->IsEditorOnly())
                            OnlyHelpers = false;
                    TInlineComponentArray<UDecalComponent*> Decals(Actor);
                    TInlineComponentArray<ULightComponent*> Lights(Actor);
                    TInlineComponentArray<UChildActorComponent*> Children(Actor);
                    OnlyHelpers &= Decals.Num() == 0 && Lights.Num() == 0 && Children.Num() == 0;
                    // A rendered object with any real mesh or material remains visible.
                    for (UStaticMeshComponent* Part : Meshes)
                        if (Part && Part->GetStaticMesh())
                        {
                            if (!SourceCollision(Part->GetStaticMesh()))
                            {
                                OnlyHelpers = false;
                                break;
                            }
                            for (int32 Slot = 0; Slot < Part->GetNumMaterials(); ++Slot)
                                if (Part->GetMaterial(Slot))
                                    OnlyHelpers = false;
                            Collision |= Part->GetCollisionEnabled() != ECollisionEnabled::NoCollision;
                        }
                    if (Collision && OnlyHelpers && !Actor->IsTemporarilyHiddenInEditor())
                    {
                        Actor->SetIsTemporarilyHiddenInEditor(true);
                        ++Hidden;
                    }
                }
        }
    return Hidden;
}

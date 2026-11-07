#include "P4MaterialReferenceContext.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Components/ActorComponent.h"
#include "UObject/UObjectHash.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
namespace
{
bool MaterialUses(UMaterialInterface* Material, const TSet<UMaterialInterface*>& Graphs, bool& Uses, FString& Error)
{
    Uses = false;
    TSet<UMaterialInterface*> Seen;
    for (int32 Depth = 0; Material; ++Depth)
    {
        if (Depth >= 64 || Seen.Contains(Material))
        {
            Error = TEXT("The material parent chain is cyclic or exceeds the supported inspection limit.");
            return false;
        }
        Seen.Add(Material);
        if (Graphs.Contains(Material))
        {
            Uses = true;
            return true;
        }
        UMaterialInstance* Instance = Cast<UMaterialInstance>(Material);
        Material = Instance ? Instance->Parent : nullptr;
    }
    return true;
}
} //namespace
bool P4MaterialReferenceUsesGraphs(UObject* Target, const TSet<UMaterialInterface*>& Graphs, bool& Uses, FString& Error)
{
    Uses = false;
    if (Graphs.Num() == 0)
        return true;
    // Authoring graph subobjects are also absent from the runtime MIC package.
    for (UObject* Outer = Target ? Target->GetOuter() : nullptr; Outer; Outer = Outer->GetOuter())
        if (UMaterial* Graph = Cast<UMaterial>(Outer))
            if (Graphs.Contains(Graph))
            {
                Uses = true;
                return true;
            }
    if (UMaterialInterface* Material = Cast<UMaterialInterface>(Target))
        return MaterialUses(Material, Graphs, Uses, Error);
    if (UStaticMesh* Mesh = Cast<UStaticMesh>(Target))
    {
        if (Mesh->StaticMaterials.Num() > 256)
        {
            Error = TEXT("Mesh material context inspection exceeds its bounded slot limit.");
            return false;
        }
        for (const FStaticMaterial& Slot : Mesh->StaticMaterials)
        {
            bool SlotUses = false;
            if (!MaterialUses(Slot.MaterialInterface, Graphs, SlotUses, Error))
                return false;
            Uses = Uses || SlotUses;
        }
    }
    if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(Target))
    {
        if (Mesh->Materials.Num() > 256)
        {
            Error = TEXT("Skeletal mesh material context inspection exceeds its bounded slot limit.");
            return false;
        }
        for (const FSkeletalMaterial& Slot : Mesh->Materials)
        {
            bool SlotUses = false;
            if (!MaterialUses(Slot.MaterialInterface, Graphs, SlotUses, Error))
                return false;
            Uses = Uses || SlotUses;
        }
    }
    return true;
}

bool P4GatherCreatorReferenceObjects(UObject* Asset, TArray<UObject*>& Objects, FString& Error)
{
    Objects.Reset();
    if (!IsValid(Asset))
    {
        Error = TEXT("A creator object disappeared during reference inspection.");
        return false;
    }
    TSet<UObject*> Seen;
    const auto AddTree = [&](UObject* Root) -> bool
    {
        if (!Root)
            return true;
        TArray<UObject*> Nested;
        GetObjectsWithOuter(Root, Nested, true);
        Nested.Insert(Root, 0);
        for (UObject* Object : Nested)
            if (IsValid(Object) && !Seen.Contains(Object))
            {
                if (Objects.Num() >= 262144)
                {
                    Error = TEXT("Creator object inspection exceeds its bounded object limit.");
                    return false;
                }
                Seen.Add(Object);
                Objects.Add(Object);
            }
        return true;
    };
    if (!AddTree(Asset))
        return false;
    if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
    {
        if (Blueprint->GeneratedClass &&
            (!AddTree(Blueprint->GeneratedClass) || !AddTree(Blueprint->GeneratedClass->GetDefaultObject(false))))
            return false;
        for (UActorComponent* Component : Blueprint->ComponentTemplates)
            if (!AddTree(Component))
                return false;
        if (Blueprint->SimpleConstructionScript)
        {
            const TArray<USCS_Node*>& Nodes = Blueprint->SimpleConstructionScript->GetAllNodes();
            if (Nodes.Num() > 4096)
            {
                Error = TEXT("Blueprint component inspection exceeds its bounded template limit.");
                return false;
            }
            for (USCS_Node* Node : Nodes)
                if (Node && !AddTree(Node->ComponentTemplate))
                    return false;
        }
    }
    return true;
}

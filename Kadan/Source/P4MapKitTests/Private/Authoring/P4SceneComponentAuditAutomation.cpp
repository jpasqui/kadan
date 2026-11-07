#if WITH_DEV_AUTOMATION_TESTS
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "P4OriginalRecord.h"

namespace P4SweepCapture
{
void RecordComponentAudit(UWorld* World, const TSharedRef<FJsonObject>& Report)
{
    TArray<TSharedPtr<FJsonValue>> Candidates;
    int32 CandidateCount = 0;
    for (ULevel* Level : World->GetLevels())
    {
        if (!Level)
            continue;
        for (AActor* Actor : Level->Actors)
        {
            if (!IsValid(Actor))
                continue;
            const FString Class = Actor->GetClass()->GetPathName();
            // This is a diagnostic fixture search, never an import or rendering rule.
            bool Construction = false;
            AActor* Ancestor = Actor;
            for (int32 Depth = 0; Ancestor && Depth < 64; ++Depth)
            {
                const FString AncestorClass = Ancestor->GetClass()->GetPathName();
                Construction |= AncestorClass.Contains(TEXT("Bridge")) || AncestorClass.Contains(TEXT("Wall")) ||
                    AncestorClass.Contains(TEXT("Climb"));
                Ancestor = Ancestor->GetParentActor();
            }
            bool Water = Class.Contains(TEXT("Water"));
            TArray<TSharedPtr<FJsonValue>> Components;
            TInlineComponentArray<UMeshComponent*> Meshes;
            Actor->GetComponents(Meshes);
            for (UMeshComponent* Mesh : Meshes)
            {
                auto Part = MakeShared<FJsonObject>();
                Part->SetStringField(TEXT("component"), Mesh->GetPathName());
                Part->SetBoolField(TEXT("registered"), Mesh->IsRegistered());
                Part->SetBoolField(TEXT("visible"), Mesh->IsVisible());
                FString Asset;
                if (const auto* Static = Cast<UStaticMeshComponent>(Mesh))
                    if (Static->GetStaticMesh())
                        Asset = Static->GetStaticMesh()->GetPathName();
                if (const auto* Skeletal = Cast<USkeletalMeshComponent>(Mesh))
                    if (Skeletal->SkeletalMesh)
                        Asset = Skeletal->SkeletalMesh->GetPathName();
                Part->SetStringField(TEXT("mesh"), Asset);
                TArray<TSharedPtr<FJsonValue>> Materials;
                for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
                {
                    UMaterialInterface* Material = Mesh->GetMaterial(Slot);
                    const FString Path = Material ? Material->GetPathName() : FString();
                    Water |= Path.Contains(TEXT("Water"));
                    Materials.Add(MakeShared<FJsonValueString>(Path));
                }
                Part->SetArrayField(TEXT("materials"), Materials);
                Components.Add(MakeShared<FJsonValueObject>(Part));
            }
            if (!Construction && !Water)
                continue;
            ++CandidateCount;
            if (Candidates.Num() >= 256)
                continue;
            auto Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("actor"), Actor->GetPathName());
            Row->SetStringField(TEXT("class"), Class);
            Row->SetStringField(
                TEXT("parent_actor"), Actor->GetParentActor() ? Actor->GetParentActor()->GetPathName() : FString());
            Row->SetBoolField(TEXT("construction_candidate"), Construction);
            Row->SetBoolField(TEXT("water_candidate"), Water);
            Row->SetBoolField(TEXT("level_visible"), Level->bIsVisible);
            Row->SetBoolField(TEXT("editor_hidden"), Actor->IsHiddenEd());
            FP4OriginalRecord Record;
            FString Error;
            UObject* Blueprint = Actor->GetClass()->ClassGeneratedBy;
            const bool Original = Blueprint && P4ReadOriginalRecord(Blueprint, Record, Error);
            Row->SetStringField(TEXT("class_import"),
                Original ? (Record.ReferenceOnly ? TEXT("reference only") : TEXT("imported"))
                         : TEXT("native or unrecorded"));
            Row->SetArrayField(TEXT("mesh_components"), Components);
            Candidates.Add(MakeShared<FJsonValueObject>(Row));
        }
    }
    Report->SetNumberField(TEXT("component_audit_candidate_count"), CandidateCount);
    Report->SetBoolField(TEXT("component_audit_truncated"), CandidateCount > Candidates.Num());
    Report->SetArrayField(TEXT("component_audit"), Candidates);
}
} // namespace P4SweepCapture
#endif

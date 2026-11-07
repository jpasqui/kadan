#if P4_MAPKIT_TESTS
// Lightmass keeps one copy of each static mesh by its lighting id. Two meshes of a map with the same id, or one
// without an id, make the lighting build read one mesh's sections for another (a Lightmass assertion). Once
// P4GiveMeshesLightingIds has run, as every export lighting build runs it, each mesh has its own. P4_AUDIT_MAP
// names the map.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "P4GameLighting.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "UObject/UObjectIterator.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FP4LightmassMeshAudit, "P4MapKit.Kadan.LightmassMeshAudit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
void FP4LightmassMeshAudit::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
    const FString Map = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_AUDIT_MAP"));
    if (Map.StartsWith(TEXT("/Game/")) && FPackageName::IsValidLongPackageName(Map))
    {
        Names.Add(FPackageName::GetLongPackageAssetName(Map));
        Commands.Add(Map);
    }
}
bool FP4LightmassMeshAudit::RunTest(const FString& Map)
{
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Map);
    if (!TestNotNull(TEXT("The map opens"), World))
        return false;
    P4GiveMeshesLightingIds(World);
    TMap<FGuid, TSet<UStaticMesh*>> ById;
    int32 Components = 0;
    for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
    {
        UStaticMeshComponent* Component = *It;
        if (!IsValid(Component) || Component->GetWorld() != World || !Component->GetStaticMesh())
            continue;
        ++Components;
        ById.FindOrAdd(Component->GetStaticMesh()->GetLightingGuid()).Add(Component->GetStaticMesh());
    }
    int32 Shared = 0;
    for (const auto& Pair : ById)
    {
        if (Pair.Value.Num() < 2 && Pair.Key.IsValid())
            continue;
        ++Shared;
        TArray<FString> Meshes;
        for (UStaticMesh* Mesh : Pair.Value)
        {
            const FStaticMeshRenderData* Render = Mesh->RenderData.Get();
            FString Sections;
            for (int32 Lod = 0; Render && Lod < Render->LODResources.Num(); ++Lod)
                Sections += FString::Printf(TEXT("%s%d"), Lod ? TEXT(",") : TEXT(""), Render->LODResources[Lod].Sections.Num());
            Meshes.Add(Mesh->GetPathName() + TEXT(" (sections per LOD ") + Sections + TEXT(")"));
        }
        Meshes.Sort();
        AddError(FString::Printf(TEXT("Lighting id %s is held by %d mesh(es): %s"), *Pair.Key.ToString(),
            Pair.Value.Num(), *FString::Join(Meshes, TEXT("; ")).Left(3000)));
        if (Shared >= 40)
            break;
    }
    AddInfo(FString::Printf(TEXT("%d static mesh components, %d lighting ids, %d shared or missing"), Components,
        ById.Num(), Shared));
    return Shared == 0;
}
#endif

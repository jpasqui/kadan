#if P4_MAPKIT_TESTS
#include "P4MaterialFactoryContext.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Blueprint.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSpriteEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModuleRequired.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4MaterialFactoryConsumersTest, "P4MapKit.Export.MaterialFactoryConsumers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4MaterialFactoryConsumersTest::RunTest(const FString&)
{
    TStrongObjectPtr<UMaterial> Graph(NewObject<UMaterial>());
    TStrongObjectPtr<UMaterialInstanceConstant> Child(NewObject<UMaterialInstanceConstant>());
    Child->Parent = Graph.Get();
    TSet<UMaterialInterface*> Graphs;
    Graphs.Add(Graph.Get());
    FString Error;
    uint64 Seen = 0;
    UMaterialInterface* SeenGraph = nullptr;
    // Traversal tests deliberately refuse in the callback; portable tests join
    // actual original records to payloads. No synthetic callback admits export.
    const FP4MaterialFactoryLookup Refuse = [&](UMaterialInterface* Found, uint64 Factory, const FString&, FString& Why)
    {
        Seen = Factory;
        SeenGraph = Found;
        Why = TEXT("No matching compiled stages in this traversal fixture.");
        return false;
    };
    TStrongObjectPtr<UStaticMesh> Mesh(NewObject<UStaticMesh>());
    Mesh->StaticMaterials.Add(FStaticMaterial(Child.Get()));
    TestFalse(TEXT("Static mesh requests its actual factory before admission"),
        P4ValidateMaterialFactoryConsumers(Mesh.Get(), Graphs, Refuse, Error));
    TestTrue(TEXT("Static slot resolves the authored parent graph"),
        Seen == 0x9f41d10d68bb52d8ull && SeenGraph == Graph.Get());
    TStrongObjectPtr<UInstancedStaticMeshComponent> Instanced(NewObject<UInstancedStaticMeshComponent>());
    Instanced->SetStaticMesh(Mesh.Get());
    Seen = 0;
    TestFalse(TEXT("Instanced fallback slots require instanced stages"),
        P4ValidateMaterialFactoryConsumers(Instanced.Get(), Graphs, Refuse, Error));
    TestTrue(TEXT("Real instanced component requests instanced factory"), Seen == 0x7e6253dcc60d6ebbull);
    TStrongObjectPtr<USkeletalMesh> Skinned(NewObject<USkeletalMesh>());
    Skinned->Materials.Add(FSkeletalMaterial(Child.Get()));
    Seen = 0;
    TestFalse(TEXT("Skeletal asset refuses without skin stages"),
        P4ValidateMaterialFactoryConsumers(Skinned.Get(), Graphs, Refuse, Error));
    TestTrue(TEXT("Real skeletal slot requests GPU skin factory"), Seen == 0x391c2b78c893a52aull);
    TStrongObjectPtr<UParticleSystem> System(NewObject<UParticleSystem>());
    UParticleSpriteEmitter* Emitter = NewObject<UParticleSpriteEmitter>(System.Get());
    System->Emitters.Add(Emitter);
    UParticleLODLevel* LOD = NewObject<UParticleLODLevel>(Emitter);
    Emitter->LODLevels.Add(LOD);
    LOD->RequiredModule = NewObject<UParticleModuleRequired>(LOD);
    LOD->RequiredModule->Material = Child.Get();
    Seen = 0;
    TestFalse(TEXT("Actual particle LOD material refuses without sprite stages"),
        P4ValidateMaterialFactoryConsumers(System.Get(), Graphs, Refuse, Error));
    TestTrue(TEXT("Real default sprite module requests sprite factory"), Seen == 0x1adefad780cd16c5ull);
    TStrongObjectPtr<UBlueprint> Blueprint(NewObject<UBlueprint>());
    UInstancedStaticMeshComponent* Template = NewObject<UInstancedStaticMeshComponent>(GetTransientPackage());
    Template->SetStaticMesh(Mesh.Get());
    Blueprint->ComponentTemplates.Add(Template);
    Seen = 0;
    TestFalse(TEXT("Unsaved Blueprint component template is inspected"),
        P4ValidateMaterialFactoryConsumers(Blueprint.Get(), Graphs, Refuse, Error));
    TestTrue(TEXT("Blueprint template resolves actual instanced consumer"), Seen == 0x7e6253dcc60d6ebbull);
    TStrongObjectPtr<UMaterial> Other(NewObject<UMaterial>());
    Instanced->SetMaterial(0, Other.Get());
    Seen = 0;
    TestTrue(TEXT("An actual unrelated override does not consume creator graph"),
        P4ValidateMaterialFactoryConsumers(Instanced.Get(), Graphs, Refuse, Error));
    TestTrue(TEXT("Unrelated renderer never asks for shader coverage"), Seen == 0);
    TestTrue(TEXT("Guard preserves actual mesh and child parent references"),
        Mesh->StaticMaterials[0].MaterialInterface == Child.Get() && Child->Parent == Graph.Get());
    return !HasAnyErrors();
}
#endif

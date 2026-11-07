#include "P4MaterialFactoryContext.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Components/SplineMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// A spline mesh's creator material is restaged on export like a static mesh's (its OverrideMaterials and
// StaticMesh are material slots), and the material gets the spline usage, so the engine compiles the spline
// vertex factory's shaders into the mod's map. Seen in Eden 2026-10-23: a bent spline mesh with a masked
// creator material in Cave000 (run kadan-spline-1023c, export spline4 in mirror 3).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4SplineMaterialSlotsAutomation, "P4MapKit.Quality.SplineMeshMaterialSlots",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4SplineMaterialSlotsAutomation::RunTest(const FString& Parameters)
{
    TStrongObjectPtr<USplineMeshComponent> Spline(NewObject<USplineMeshComponent>(GetTransientPackage()));
    TStrongObjectPtr<UStaticMeshComponent> Plain(NewObject<UStaticMeshComponent>(GetTransientPackage()));
    TestTrue(TEXT("A spline mesh component's materials are restaged"), P4StaticMaterialFactoryComponent(Spline.Get()));
    TestTrue(TEXT("A static mesh component's materials are restaged"), P4StaticMaterialFactoryComponent(Plain.Get()));
    TStrongObjectPtr<UMaterial> Material(NewObject<UMaterial>(GetTransientPackage()));
    // The spline factory's hashed name, as material_factory.h records it.
    const uint64 SplineFactory = 0x872b1640e244ca06ull;
    TestTrue(TEXT("The spline factory is one Kadan gives a usage"), P4KnownMaterialFactory(SplineFactory));
    TestFalse(TEXT("A new material is not yet used with spline meshes"), P4MaterialUsageCovers(Material.Get(), SplineFactory));
    P4ApplyMaterialFactoryUsage(Material.Get(), {SplineFactory});
    TestTrue(TEXT("The spline usage is set"), Material->bUsedWithSplineMeshes != 0);
    TestTrue(TEXT("The usage covers the spline factory"), P4MaterialUsageCovers(Material.Get(), SplineFactory));
    return true;
}
#endif

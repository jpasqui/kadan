#if P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavModifierVolume.h"
#include "Lightmass/PrecomputedVisibilityVolume.h"
#include "Engine/CullDistanceVolume.h"
#include "Lightmass/LightmassImportanceVolume.h"
#include "Engine/BlockingVolume.h"
#include "GameFramework/PhysicsVolume.h"
#include "Engine/StaticMeshActor.h"
namespace P4LevelContent
{
KADAN_API bool ProjectionSupportedBrushClass(const UClass*);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ProjectionBrushClassesTest, "P4MapKit.NativeImport.ExplicitMapBrushVolumeClasses",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ProjectionBrushClassesTest::RunTest(const FString&)
{
    for (UClass* Class : {ANavMeshBoundsVolume::StaticClass(), ANavModifierVolume::StaticClass(),
                          APrecomputedVisibilityVolume::StaticClass(), ACullDistanceVolume::StaticClass(),
                          ALightmassImportanceVolume::StaticClass()})
        TestTrue(TEXT("An actual map brush role can reach exact source polygon qualification: ") + Class->GetPathName(),
                 P4LevelContent::ProjectionSupportedBrushClass(Class));
    for (UClass* Class : {AVolume::StaticClass(), ABlockingVolume::StaticClass(), APhysicsVolume::StaticClass(),
                          AStaticMeshActor::StaticClass()})
        TestFalse(TEXT("A different native actor role stays outside this brush admission: ") + Class->GetPathName(),
                  P4LevelContent::ProjectionSupportedBrushClass(Class));
    TestFalse(TEXT("An unresolved class cannot acquire a brush role"),
              P4LevelContent::ProjectionSupportedBrushClass(nullptr));
    return !HasAnyErrors();
}
#endif

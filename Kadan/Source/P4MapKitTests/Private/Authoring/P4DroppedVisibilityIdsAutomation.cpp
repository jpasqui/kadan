#if P4_MAPKIT_TESTS
#include "P4DroppedVisibilityIds.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4DroppedVisibilityIdsTest, "P4MapKit.Levels.DroppedVisibilityCacheIds",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4DroppedVisibilityIdsTest::RunTest(const FString&)
{
    auto* Package = CreatePackage(*(TEXT("/Temp/NativeVisibility_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    auto* World = NewObject<UWorld>(Package, TEXT("World"), RF_Transient);
    auto* Part = NewObject<UStaticMeshComponent>(World, TEXT("Part"), RF_Transient);
    const FString Path = Part->GetPathName(Package);
    const FString Id = Path + TEXT(" | visibilityid = 141");
    const TArray<FString> Imported{Id};
    Part->VisibilityId = INDEX_NONE;
    TArray<FString> Current;
    P4WithDroppedVisibilityIds(World, Imported, Current);
    TestTrue(TEXT("A genuine native cache slot discarded by editor load compares with its source baseline"),
             Current.Contains(Id));
    TestEqual(TEXT("Comparison does not rewrite the live cache"), Part->VisibilityId, INDEX_NONE);
    TestTrue(TEXT("Comparison does not rewrite the imported baseline"), Imported.Num() == 1 && Imported[0] == Id);
    Current = {Path + TEXT(" | relativeLocation = {\"X\":1.0}")};
    const FString CreatorMove = Current[0];
    P4WithDroppedVisibilityIds(World, Imported, Current);
    TestTrue(TEXT("A one-centimetre authored transform line remains an independent difference"),
             Current.Contains(CreatorMove));
    Part->VisibilityId = 42;
    Current.Reset();
    P4WithDroppedVisibilityIds(World, Imported, Current);
    TestTrue(TEXT("A different nonempty live ID is not hidden"), Current.Num() == 0);
    Part->VisibilityId = INDEX_NONE;
    Current = {Path + TEXT(" | visibilityid = -1")};
    P4WithDroppedVisibilityIds(World, Imported, Current);
    TestTrue(TEXT("An explicit cooked field is never replaced"), Current.Num() == 1 && !Current.Contains(Id));
    TArray<FString> Other{World->GetPathName(Package) + TEXT(" | visibilityid = 141")};
    Current.Reset();
    P4WithDroppedVisibilityIds(World, Other, Current);
    TestTrue(TEXT("An unrelated same-named field is not normalized"), Current.Num() == 0);
    Part->MarkPendingKill();
    World->MarkPendingKill();
    return !HasAnyErrors();
}
#endif

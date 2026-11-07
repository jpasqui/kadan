#if WITH_DEV_AUTOMATION_TESTS
#include "Components/PrimitiveComponent.h"
#include "Components/BoxComponent.h"
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "P4OriginalRecord.h"

namespace P4SweepCapture
{
FBox ExpandFloorFootprint(const FBox& Rooms, const TArray<FVector>& FloorAnchors,
    const TArray<UPrimitiveComponent*>& Parts, const TSharedRef<FJsonObject>& Report)
{
    if (!Rooms.IsValid || !FloorAnchors.Num())
        return Rooms;
    FBox Result = Rooms;
    const FVector Span = Rooms.GetSize();
    const float Limit = FMath::Max(Span.X, Span.Y);
    const FBox Nearby = Rooms.ExpandBy(Limit * .25f);
    int32 Included = 0;
    for (const UPrimitiveComponent* Part : Parts)
    {
        const FBox Box = Part->Bounds.GetBox();
        const FVector Size = Box.GetSize();
        const FVector Center = Box.GetCenter();
        // Discard distant or scene-sized backdrop geometry, while admitting
        // doorsteps, connectors and floor ends outside the room mesh bounds.
        if (Size.X > Limit * 1.5f || Size.Y > Limit * 1.5f || Size.Z > Limit || Center.X < Nearby.Min.X ||
            Center.X > Nearby.Max.X || Center.Y < Nearby.Min.Y || Center.Y > Nearby.Max.Y)
            continue;
        const bool NearFloor = FloorAnchors.ContainsByPredicate(
            [&](const FVector& Anchor) { return Anchor.Z >= Box.Min.Z - 50.f && Anchor.Z <= Box.Max.Z + 50.f; });
        if (!NearFloor)
            continue;
        Result += Box;
        ++Included;
    }
    Report->SetNumberField(TEXT("overview_near_floor_components"), Included);
    Report->SetStringField(TEXT("overview_room_bounds_min"), Rooms.Min.ToString());
    Report->SetStringField(TEXT("overview_room_bounds_max"), Rooms.Max.ToString());
    return Result;
}
bool Capture(UWorld*, const FString&, const TSharedRef<FJsonObject>&);
} // namespace P4SweepCapture

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OverviewFloorEnds, "P4MapKit.Editor.OverviewIncludesFloorEnds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OverviewFloorEnds::RunTest(const FString&)
{
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_OVERVIEW_FLOOR_ENDS")) != TEXT("1"))
    {
        AddWarning(TEXT("P4MapKit test not run. Select the owned clipped-floor fixture."));
        return true;
    }
    const FBox Rooms(FVector(-100, -100, -5), FVector(100, 100, 5));
    TArray<UPrimitiveComponent*> Parts;
    for (const FBox& Bounds :
        {FBox(FVector(-40, -160, -5), FVector(40, -60, 5)), FBox(FVector(-1000, -1000, -5), FVector(1000, 1000, 5)),
            FBox(FVector(-25, -25, 395), FVector(25, 25, 405)), FBox(FVector(975, -25, -5), FVector(1025, 25, 5))})
    {
        auto* Part = NewObject<UBoxComponent>(GetTransientPackage(), NAME_None, RF_Transient);
        Part->Bounds = FBoxSphereBounds(Bounds);
        Parts.Add(Part);
    }
    auto ControlReport = MakeShared<FJsonObject>();
    const FBox Expanded = P4SweepCapture::ExpandFloorFootprint(Rooms, {FVector::ZeroVector}, Parts, ControlReport);
    TestTrue(TEXT("Nearby floor ends expand the footprint exactly"),
        Expanded.Min == FVector(-100, -160, -5) && Expanded.Max == Rooms.Max);
    TestEqual(TEXT("Large backdrops, elevated geometry and distant parts do not expand it"),
        ControlReport->GetNumberField(TEXT("overview_near_floor_components")), 1.0);
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(
        TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave013/Cave013_F01/Cave013_F01_P"));
    if (!TestNotNull(TEXT("The original clipped-floor map opens"), World))
        return false;
    ON_SCOPE_EXIT
    {
        UEditorLoadingAndSavingUtils::NewBlankMap(false);
    };
    const auto Before = P4DiagnoseOriginalAsset(World);
    auto Report = MakeShared<FJsonObject>();
    if (!TestTrue(TEXT("The actual floor and its end pieces are captured"),
            P4SweepCapture::Capture(World, FPaths::ProjectSavedDir() / TEXT("P4MapKit/OverviewFloorEnds"), Report)))
        return false;
    FVector OldMin, OldMax, NewMin, NewMax;
    TestTrue(TEXT("The original room bounds are recorded"),
        OldMin.InitFromString(Report->GetStringField(TEXT("overview_room_bounds_min"))) &&
            OldMax.InitFromString(Report->GetStringField(TEXT("overview_room_bounds_max"))));
    TestTrue(TEXT("The expanded overview bounds are recorded"),
        NewMin.InitFromString(Report->GetStringField(TEXT("overview_bounds_min"))) &&
            NewMax.InitFromString(Report->GetStringField(TEXT("overview_bounds_max"))));
    TestTrue(TEXT("This fixture includes floor ends beyond the room-only bounds"),
        NewMax.Y - NewMin.Y > OldMax.Y - OldMin.Y + 100.f);
    const auto After = P4DiagnoseOriginalAsset(World);
    TestTrue(TEXT("The captured original map remains unchanged"),
        Before.Error.IsEmpty() && After.Error.IsEmpty() && Before.Comparison == EP4OriginalComparison::Equal &&
            After.Comparison == EP4OriginalComparison::Equal);
    return !HasAnyErrors();
}
#endif

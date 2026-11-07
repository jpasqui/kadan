#if P4_MAPKIT_TESTS
#include "P4GamePreviewStart.h"
#include "P4GamePreviewBootstrap.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/PlayerStartPIE.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GamePreviewStartTest, "P4MapKit.Game.PreviewStart",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GamePreviewStartTest::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false, TEXT("KadanPreviewStartTest"));
    if (!TestNotNull(TEXT("an isolated editor world exists"), World))
        return false;
    GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
    };
    // Actor iteration needs an active world level, just as it does in an editor.
    World->PersistentLevel->bIsVisible = true;
    FTransform At;
    FString Source, Error;
    TestFalse(TEXT("an imported level without Player Start retains the game start"),
        P4GamePreviewStart(World, false, At, Source, Error));
    TestTrue(TEXT("keeping the game start is not an error"), Error.IsEmpty());

    APlayerStart* Start = World->SpawnActor<APlayerStart>(FVector(0, 0, 120), FRotator(20, 73, 10));
    if (!TestNotNull(TEXT("a real Player Start is placed"), Start))
        return false;
    TestFalse(TEXT("an explicit Player Start over empty space is refused"),
        P4GamePreviewStart(World, false, At, Source, Error));
    TestFalse(TEXT("missing floor has a useful error"), Error.IsEmpty());

    AActor* Floor = World->SpawnActor<AActor>();
    UBoxComponent* Box = NewObject<UBoxComponent>(Floor);
    Floor->AddInstanceComponent(Box);
    Floor->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(600, 600, 10));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->RegisterComponent();
    Box->SetWorldLocation(FVector(0, 0, -10));
    TestTrue(TEXT("a Player Start above pawn-blocking floor is accepted"),
        P4GamePreviewStart(World, false, At, Source, Error));
    AddInfo(FString::Printf(TEXT("Player Start yaw %.9g degrees; preview yaw %.9g degrees."),
        Start->GetActorRotation().Yaw, At.Rotator().Yaw));
    // The actor and output each convert through float quaternions. The default
    // IsNearlyEqual tolerance is 1e-8, below a float's resolution at this angle.
    TestEqual(TEXT("native start yaw is preserved"), At.Rotator().Yaw, 73.f, 0.01f);
    TestTrue(TEXT("pitch and roll do not tilt the player"),
        FMath::IsNearlyZero(At.Rotator().Pitch) && FMath::IsNearlyZero(At.Rotator().Roll));
    TestTrue(TEXT("a clear elevated start retains its height"), FMath::IsNearlyEqual(At.GetLocation().Z, 120.f));
    Start->SetActorLocation(FVector(160, 0, 20));
    TestTrue(
        TEXT("moving the durable start changes the next Play"), P4GamePreviewStart(World, false, At, Source, Error));
    TestTrue(TEXT("the moved start's horizontal position is used"), FMath::IsNearlyEqual(At.GetLocation().X, 160.f));
    TestTrue(TEXT("a partly embedded marker is lifted above the floor"), At.GetLocation().Z >= 94.f);

    // Tracing from the top of the clearance capsule would hit this ceiling
    // first and incorrectly lift the player onto its roof. The marker's own
    // floor is below it, and its clearance must be refused under this ceiling.
    Start->SetActorLocation(FVector(0, 0, 120));
    AActor* Ceiling = World->SpawnActor<AActor>();
    if (!TestNotNull(TEXT("a low ceiling actor exists"), Ceiling))
        return false;
    UBoxComponent* CeilingBox = NewObject<UBoxComponent>(Ceiling);
    Ceiling->AddInstanceComponent(CeilingBox);
    Ceiling->SetRootComponent(CeilingBox);
    CeilingBox->SetBoxExtent(FVector(600, 600, 10));
    CeilingBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    CeilingBox->SetCollisionObjectType(ECC_WorldStatic);
    CeilingBox->SetCollisionResponseToAllChannels(ECR_Block);
    CeilingBox->RegisterComponent();
    CeilingBox->SetWorldLocation(FVector(0, 0, 170));
    TestFalse(TEXT("a low ceiling is rejected instead of becoming a floor above the marker"),
        P4GamePreviewStart(World, false, At, Source, Error));
    CeilingBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TestTrue(TEXT("removing ceiling collision restores the authored start"),
        P4GamePreviewStart(World, false, At, Source, Error));
    TestTrue(TEXT("the restored start did not move onto the ceiling"), FMath::IsNearlyEqual(At.GetLocation().Z, 120.f));

    Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TestFalse(TEXT("visible geometry without collision cannot make a safe floor"),
        P4GamePreviewStart(World, false, At, Source, Error));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Floor->SetFlags(RF_Transient);
    TestFalse(TEXT("transient preview geometry cannot make an empty map pass"),
        P4GamePreviewStart(World, false, At, Source, Error));
    Floor->ClearFlags(RF_Transient);
    APlayerStartPIE* FromHere = World->SpawnActor<APlayerStartPIE>(FVector(240, 0, 120), FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("a native Play From Here start exists"), FromHere))
        return false;
    TestTrue(TEXT("Play From Here is resolved"), P4GamePreviewStart(World, false, At, Source, Error));
    TestTrue(TEXT("native Play From Here takes priority over the other start"),
        FMath::IsNearlyEqual(At.GetLocation().X, 240.f));

    FP4GamePreviewStartResult FrozenStart;
    TestTrue(
        TEXT("the full validated start result resolves"), P4GamePreviewStart(World, false, FrozenStart, Source, Error));
    TestTrue(TEXT("the chosen start retains its source world and validated floor"),
        FrozenStart.World.Get() == World && FrozenStart.HasFloor);
    TestEqual(TEXT("floor contact comes from the chosen candidate"), FrozenStart.FloorPoint.X, 240.f, 0.01f);
    TestEqual(TEXT("elevated player centre remains separate from floor contact"), FrozenStart.Transform.GetLocation().Z,
        120.f, 0.01f);
    TestEqual(TEXT("floor contact remains on the actual collision surface"), FrozenStart.FloorPoint.Z, 0.f, 0.01f);
    const FString FrozenKey = FrozenStart.FingerprintText();
    const bool DirtyBeforeKey = World->GetOutermost()->IsDirty();
    FP4GamePreviewStartResult CameraMoved = FrozenStart;
    CameraMoved.Transform.AddToTranslation(FVector(0.125f, 0, 0));
    CameraMoved.FloorPoint.X += 0.125f;
    TestTrue(TEXT("camera-derived position changes invalidate the key without a map Modify"),
        CameraMoved.FingerprintText() != FrozenKey && World->GetOutermost()->IsDirty() == DirtyBeforeKey);
    FP4GamePreviewStartResult GroundChanged = FrozenStart;
    GroundChanged.FloorPoint.Z += 0.125f;
    TestTrue(TEXT("ground contact changes invalidate the bootstrap key independently"),
        GroundChanged.FingerprintText() != FrozenKey);
    GroundChanged = FrozenStart;
    GroundChanged.FloorNormal.X = 0.125f;
    TestTrue(TEXT("ground normal changes are also part of the key"), GroundChanged.FingerprintText() != FrozenKey);
    FromHere->SetActorLocation(FVector(320, 0, 120));
    FP4GamePreviewStartResult LaterStart;
    TestTrue(TEXT("a later start selection can differ"), P4GamePreviewStart(World, false, LaterStart, Source, Error));
    TestTrue(TEXT("later editor changes cannot overwrite the frozen Play choice"),
        FrozenStart.FingerprintText() == FrozenKey && LaterStart.FingerprintText() != FrozenKey);
    TestEqual(
        TEXT("the frozen candidate's position is unchanged"), FrozenStart.Transform.GetLocation().X, 240.f, 0.01f);

    FromHere->SetActorLocationAndRotation(FVector(200, -100, 150), FRotator(0, 37, 0));
    FP4GamePreviewStartResult ElevatedStart;
    TestTrue(TEXT("an intentionally elevated start retains a validated separate floor contact"),
        P4GamePreviewStart(World, false, ElevatedStart, Source, Error));
    FTransform BaseAnchor;
    TestTrue(TEXT("the base anchor is derived from that actual floor contact"),
        P4GamePreviewBaseAnchor(
            ElevatedStart.Transform, ElevatedStart.FloorPoint, ElevatedStart.FloorNormal, BaseAnchor, Error));
    TestEqual(TEXT("the captain retains the authored 150 cm centre height"), ElevatedStart.Transform.GetLocation().Z,
        150.f, 0.01f);
    TestEqual(TEXT("the base is on the real floor instead of at capsule-centre height"), BaseAnchor.GetLocation(),
        FVector(200, -100, 0), 0.01f);
    TestEqual(TEXT("the grounded base preserves the chosen yaw"), BaseAnchor.Rotator().Yaw, 37.f, 0.01f);
    TestFalse(TEXT("a side wall cannot be used as the base ground contact"),
        P4GamePreviewBaseAnchor(
            ElevatedStart.Transform, ElevatedStart.FloorPoint, FVector(1, 0, 0), BaseAnchor, Error));

    // Reproduce a clean loaded map's component-class migration. Keeping a new
    // mesh must not reinterpret that unchanged migration as a new source edit;
    // changing or reverting the same actor still requires a map refresh.
    FP4LevelChangesResult Baseline;
    Baseline.Package = TEXT("/Game/PreviewBaseline");
    Baseline.Level = TEXT("PreviewBaseline");
    FP4LevelActorChanges Environment;
    Environment.Actor = TEXT("Environment");
    Environment.Lines.Before = {
        TEXT("SunLight | class /Script/Engine.DirectionalLightComponent"), TEXT("skyatmosphere = Sky")};
    Environment.Lines.After = {TEXT("SunLight | class /Script/P4StubsCore.P4RecoveredDirectionalLightComponent")};
    Baseline.OtherChanged.Add(Environment);
    FP4LevelChangesResult Current = Baseline;
    Current.Added.Add(TEXT("KeptMesh"));
    TestTrue(TEXT("an unchanged loaded migration permits a new kept addition"),
        P4GamePreviewDeltaSinceBaseline(Baseline, Current, Error));
    TestEqual(TEXT("only the preexisting migration is subtracted"), Current.OtherChanged.Num(), 0);
    TestEqual(TEXT("the actual kept addition remains subject to live actor admission"), Current.Added.Num(), 1);
    Current = Baseline;
    Current.OtherChanged[0].Lines.After.Add(TEXT("SunLight | intensity = 2"));
    TestFalse(TEXT("a new original property edit cannot hide behind a preexisting migration"),
        P4GamePreviewDeltaSinceBaseline(Baseline, Current, Error));
    Current = Baseline;
    Current.OtherChanged.Reset();
    TestFalse(TEXT("reverting a baseline difference is still a source property change"),
        P4GamePreviewDeltaSinceBaseline(Baseline, Current, Error));
    Current = Baseline;
    Current.LevelChanges.After.Add(TEXT("streaming level = AnotherMap"));
    TestFalse(TEXT("a structural change is refused beside an unchanged migration"),
        P4GamePreviewDeltaSinceBaseline(Baseline, Current, Error));
    FP4LevelActorMove PriorMove;
    PriorMove.Actor = TEXT("OriginalGeometry");
    PriorMove.Location = FVector(100, 0, 0);
    Baseline.Moved.Add(PriorMove);
    Current = Baseline;
    Current.Moved[0].Location.X += 1;
    TestFalse(TEXT("a new original transform change cannot be subtracted as baseline drift"),
        P4GamePreviewDeltaSinceBaseline(Baseline, Current, Error));
    Baseline.Moved[0].Actor = Start->GetName();
    Current = Baseline;
    Current.Moved[0].Location.X += 1;
    TSet<FString> VerifiedStarts;
    VerifiedStarts.Add(Start->GetName());
    TestTrue(TEXT("verified native Player Starts can still move from their baseline"),
        P4GamePreviewDeltaSinceBaseline(Baseline, Current, Error, VerifiedStarts));
    TestEqual(TEXT("the changed start remains available to the start-only admission policy"), Current.Moved.Num(), 1);
    Current = Baseline;
    Current.Package += TEXT("_Copy");
    TestFalse(TEXT("a snapshot cannot be reused for a differently named source package"),
        P4GamePreviewDeltaSinceBaseline(Baseline, Current, Error));

    FP4GamePreviewBaseline Snapshot;
    Snapshot.Root = World;
    FP4GamePreviewBaseline::FSource SourceSnapshot;
    SourceSnapshot.World = World;
    SourceSnapshot.Level = World->PersistentLevel;
    SourceSnapshot.Changes.Package = World->GetOutermost()->GetName();
    Snapshot.Sources.Add(SourceSnapshot);
    TestTrue(TEXT("a source snapshot matches its world and level identity"), Snapshot.IsFor(World));
    Snapshot.Sources[0].Changes.Package += TEXT("_Copy");
    TestFalse(TEXT("a changed source path invalidates the session snapshot"), Snapshot.IsFor(World));
    Snapshot.Reset();
    TestFalse(TEXT("reset releases the session snapshot"), Snapshot.IsFor(World));
    return true;
}
#endif

#include "P4PhysicsContent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Serialization/MemoryWriter.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "P4LevelImportWorld.h"
#include "Components/BoxComponent.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace P4ImportPhysicsTests
{
TArray<uint8> BodySettings(FBodyInstance& Body)
{
    TArray<uint8> Bytes;
    FMemoryWriter Memory(Bytes, true);
    FObjectAndNameAsStringProxyArchive Writer(Memory, false);
    FBodyInstance::StaticStruct()->SerializeItem(Writer, &Body, nullptr);
    return Bytes;
}
} //namespace P4ImportPhysicsTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalImportWorldPhysicsTest, "P4MapKit.Physics.OriginalImportWorld",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalImportWorldPhysicsTest::RunTest(const FString&)
{
    const FStructProperty* ScaleProperty = FindFProperty<FStructProperty>(
        USceneComponent::StaticClass(), TEXT("RelativeScale3D"));
    if (!TestNotNull(TEXT("The source scale has its native property storage"), ScaleProperty))
        return false;
    // Repeated reconstruction/discard used to create a PhysX scene for every sublevel, despite not
    // performing collision queries. Keep the real registration/attachment/transform operations.
    for (int32 Pass = 0; Pass < 8; ++Pass)
    {
        const FString PackageName = TEXT("/Game/NativeImportPhysicsTest/World_") +
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        UPackage* Package = CreatePackage(*PackageName);
        const FName Name = Pass == 0 ? NAME_None : FName(TEXT("OriginalImportWorld"));
        UWorld* World = P4CreateOriginalImportWorld(Name, Package);
        if (!TestNotNull(TEXT("The reconstruction world exists"), World))
            return false;
        ON_SCOPE_EXIT
        {
            World->DestroyWorld(false);
            World->MarkPendingKill();
            Package->SetDirtyFlag(false);
        };
        TestTrue(TEXT("Reconstruction allocates no transient physics scene"), World->GetPhysicsScene() == nullptr);
        TestEqual(TEXT("The stock world name fallback is preserved"), World->GetFName(),
            Name.IsNone() ? FName(TEXT("Untitled")) : Name);
        TestTrue(TEXT("The engine's transactional world flag is preserved"), World->HasAnyFlags(RF_Transactional));
        TestFalse(TEXT("The reconstruction caller retains the stock no-root behavior"), World->IsRooted());
        TestTrue(TEXT("The package is still an ordinary map with its engine-made level"),
            Package->ContainsMap() && World->PersistentLevel != nullptr);
        TestTrue(TEXT("Rendering remains initialized for registered source components"), World->Scene != nullptr);
        TestFalse(TEXT("Reconstruction enables no editor collision-query override"), World->bEnableTraceCollision);
        AActor* Actor = World->SpawnActor<AActor>();
        if (!TestNotNull(TEXT("The source actor can be spawned normally"), Actor))
            return false;
        UBoxComponent* Root = NewObject<UBoxComponent>(Actor, TEXT("SourceRoot"), RF_Transactional);
        UBoxComponent* Child = NewObject<UBoxComponent>(Actor, TEXT("SourceCollider"), RF_Transactional);
        Actor->AddInstanceComponent(Root);
        Actor->AddInstanceComponent(Child);
        Actor->SetRootComponent(Root);
        Root->SetMobility(EComponentMobility::Movable);
        Child->SetMobility(EComponentMobility::Movable);
        Root->SetCollisionProfileName(TEXT("BlockAll"));
        Child->SetCollisionProfileName(TEXT("BlockAll"));
        Root->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Child->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Root->SetBoxExtent(FVector(11, 12, 13));
        Child->SetBoxExtent(FVector(21, 22, 23));
        Child->BodyInstance.bSimulatePhysics = true;
        Child->BodyInstance.MassScale = 2.5f;
        Child->BodyInstance.LinearDamping = 0.25f;
        Child->BodyInstance.AngularDamping = 0.5f;
        Child->SetupAttachment(Root);
        Root->RegisterComponent();
        Child->RegisterComponent();
        const TArray<uint8> RootBefore = P4ImportPhysicsTests::BodySettings(Root->BodyInstance);
        const TArray<uint8> ChildBefore = P4ImportPhysicsTests::BodySettings(Child->BodyInstance);
        const FVector Scales[] = {FVector(1, 2, 3), FVector(0, 0, 0), FVector(-0.0f, 1, 0), FVector(-1, 2, -3)};
        for (const FVector& Scale : Scales)
        {
            // Import writes decoded RelativeScale3D directly before settling attachments, rather
            // than using an interactive editor property change. Include exact zero and signed zero.
            FMemory::Memcpy(ScaleProperty->ContainerPtrToValuePtr<void>(Child), &Scale, sizeof(Scale));
            Actor->UpdateComponentTransforms();
            TestTrue(TEXT("The decoded relative scale bits survive transform settlement"),
                FMemory::Memcmp(ScaleProperty->ContainerPtrToValuePtr<void>(Child), &Scale, sizeof(Scale)) == 0);
            TestTrue(TEXT("Source collision stays enabled without native bodies"),
                Root->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics &&
                    Child->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);
            TestFalse(
                TEXT("Source root registration creates no temporary physics state"), Root->IsPhysicsStateCreated());
            TestFalse(
                TEXT("Source child registration creates no temporary physics state"), Child->IsPhysicsStateCreated());
            TestFalse(TEXT("Source child carries no PhysX body during property restoration"),
                Child->BodyInstance.IsValidBodyInstance());
            TestTrue(TEXT("All serialized source root collision settings survive"),
                P4ImportPhysicsTests::BodySettings(Root->BodyInstance) == RootBefore);
            TestTrue(TEXT("All serialized source child collision settings survive"),
                P4ImportPhysicsTests::BodySettings(Child->BodyInstance) == ChildBefore);
            Child->UnregisterComponent();
            Child->RegisterComponent();
            Actor->UpdateComponentTransforms();
            TestTrue(TEXT("Attachment and registration remain usable"),
                Child->IsRegistered() && Root->IsRegistered() && Child->GetAttachParent() == Root);
        }
    }
    // This is local to the reconstruction factory. A normal editor world must retain its physics
    // scene and must create a collision body using the same source component settings.
    UWorld* Editor = UWorld::CreateWorld(
        EWorldType::EditorPreview, false, TEXT("NormalPhysicsWorld"), GetTransientPackage());
    if (!TestNotNull(TEXT("The engine still creates a normal editor world"), Editor))
        return false;
    ON_SCOPE_EXIT
    {
        Editor->DestroyWorld(false);
    };
    TestTrue(TEXT("Normal editor initialization still creates a physics scene"), Editor->GetPhysicsScene() != nullptr);
    AActor* Actor = Editor->SpawnActor<AActor>();
    if (!TestNotNull(TEXT("The normal editor actor exists"), Actor))
        return false;
    UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
    Actor->AddInstanceComponent(Box);
    Actor->SetRootComponent(Box);
    Box->SetCollisionProfileName(TEXT("BlockAll"));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Box->RegisterComponent();
    TestTrue(TEXT("Ordinary editor collision bodies still work"),
        Box->IsPhysicsStateCreated() && Box->BodyInstance.IsValidBodyInstance());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PhysicsCollisionTableTest, "P4MapKit.Physics.CollisionTable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PhysicsCollisionTableTest::RunTest(const FString&)
{
    UPhysicsAsset* Source = NewObject<UPhysicsAsset>();
    UPhysicsAsset* Target = NewObject<UPhysicsAsset>();
    for (int32 Index = 0; Index < 4; ++Index)
    {
        USkeletalBodySetup* Body = NewObject<USkeletalBodySetup>(Target);
        Body->BoneName = FName(*FString::Printf(TEXT("bone%d"), Index));
        Target->SkeletalBodySetups.Add(Body);
    }
    Source->CollisionDisableTable.Add(FRigidBodyIndexPair(0, 2), false);
    Source->CollisionDisableTable.Add(FRigidBodyIndexPair(1, 3), true);
    TArray<uint8> Bytes;
    {
        FMemoryWriter W(Bytes, true);
        int32 ObjectId = 0;
        W << ObjectId;
        W << Source->CollisionDisableTable;
    }
    const std::vector<std::uint8_t> Data(Bytes.GetData(), Bytes.GetData() + Bytes.Num());
    auto Parsed = P4MapKit::ReadPhysicsAssetNative(Data, 0, Data.size());
    if (!TestTrue(TEXT("Stock engine collision map is decoded completely"),
            Parsed.Error.Code.empty() && Parsed.Disabled.size() == 2))
        return false;
    FString Error;
    if (!TestTrue(TEXT("Importer restores collision map"), P4FillPhysicsNative(Target, Parsed, Error)))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("Stored true and false collision values survive"),
        Target->CollisionDisableTable.Num() == 2 && !Target->CollisionDisableTable[FRigidBodyIndexPair(0, 2)] &&
            Target->CollisionDisableTable[FRigidBodyIndexPair(1, 3)]);
    TestEqual(TEXT("Body setup index map is rebuilt"), Target->FindBodyIndex(TEXT("bone3")), 3);
    TArray<uint8> Recooked;
    {
        FMemoryWriter W(Recooked, true);
        int32 ObjectId = 0;
        W << ObjectId;
        W << Target->CollisionDisableTable;
    }
    TestTrue(TEXT("Native collision map serialization is byte equal"), Bytes == Recooked);
    Parsed.Disabled[0].Second = 4;
    TestFalse(TEXT("Collision pair outside actual body list is refused"), P4FillPhysicsNative(Target, Parsed, Error));
    TArray<uint8> AfterFailure;
    {
        FMemoryWriter W(AfterFailure, true);
        int32 ObjectId = 0;
        W << ObjectId;
        W << Target->CollisionDisableTable;
    }
    TestTrue(TEXT("Refused collision map preserves the prior table"), AfterFailure == Recooked);
    Parsed.Disabled.clear();
    TestTrue(TEXT("Empty game collision table clears constructor defaults"),
        P4FillPhysicsNative(Target, Parsed, Error) && Target->CollisionDisableTable.Num() == 0);
    P4MapKit::FPhysicsBodyNative Primitive;
    Primitive.Guid = {{1, 2, 3, 4}};
    USkeletalBodySetup* Body = Target->SkeletalBodySetups[0];
    Body->bHasCookedCollisionData = true;
    TestTrue(TEXT("Primitive body restores its native source GUID"),
        P4FillPrimitivePhysicsBodyNative(Body, Primitive, Error) && Body->BodySetupGuid == FGuid(1, 2, 3, 4));
    TestFalse(TEXT("Primitive body retains no unused convex cooked data"), Body->bHasCookedCollisionData);
    Primitive.Error.Code = "invalid-source";
    TestFalse(TEXT("Invalid primitive body is refused"), P4FillPrimitivePhysicsBodyNative(Body, Primitive, Error));
    TestTrue(TEXT("Primitive refusal leaves the source GUID"), Body->BodySetupGuid == FGuid(1, 2, 3, 4));
    Source->MarkPendingKill();
    Target->MarkPendingKill();
    return !HasAnyErrors();
}
#endif

#include "P4PhysicsContent.h"
#include "PhysicsEngine/PhysicsAsset.h"

bool P4FillPhysicsNative(UObject* Object, const P4MapKit::FPhysicsAssetNative& Data, FString& Error)
{
    UPhysicsAsset* Physics = Cast<UPhysicsAsset>(Object);
    if (!IsInGameThread() || !Physics || !Data.Error.Code.empty())
    {
        Error = TEXT("physics-import-object-or-native-data-invalid");
        return false;
    }
    for (const auto& Pair : Data.Disabled)
        if (Pair.First < 0 || Pair.First >= Pair.Second || Pair.Second >= Physics->SkeletalBodySetups.Num())
        {
            Error = TEXT("physics-import-collision-pair-outside-body-list");
            return false;
        }
    TMap<FRigidBodyIndexPair, bool> Restored;
    for (const auto& Pair : Data.Disabled)
    {
        const FRigidBodyIndexPair Key(Pair.First, Pair.Second);
        if (Restored.Contains(Key))
        {
            Error = TEXT("physics-import-collision-pair-duplicate");
            return false;
        }
        Restored.Add(Key, Pair.Value);
    }
    Physics->CollisionDisableTable = MoveTemp(Restored);
    Physics->UpdateBodySetupIndexMap();
    return true;
}

bool P4FillPrimitivePhysicsBodyNative(UObject* Object, const P4MapKit::FPhysicsBodyNative& Data, FString& Error)
{
    USkeletalBodySetup* Body = Cast<USkeletalBodySetup>(Object);
    if (!IsInGameThread() || !Body || !Data.Error.Code.empty() || Data.Guid == std::array<std::uint32_t, 4>{} ||
        Body->AggGeom.ConvexElems.Num())
    {
        Error = TEXT("physics-import-body-or-primitive-data-invalid");
        return false;
    }
    Body->BodySetupGuid = FGuid(Data.Guid[0], Data.Guid[1], Data.Guid[2], Data.Guid[3]);
    Body->CookedFormatData.FlushData();
    Body->bHasCookedCollisionData = false;
    return true;
}

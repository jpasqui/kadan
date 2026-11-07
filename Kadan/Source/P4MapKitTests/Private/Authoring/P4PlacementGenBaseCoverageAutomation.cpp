#include "P4AuthoringRows.h"
#if WITH_DEV_AUTOMATION_TESTS && defined(P4_MAPKIT_TESTS) && P4_MAPKIT_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "Serialization/ObjectWriter.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/MetaData.h"
#include "UObject/UObjectHash.h"

namespace P4PlacementGenBaseCoverage
{
TArray<uint8> P4GenBaseBytes(UObject* Object)
{
    TArray<uint8> Bytes;
    FObjectWriter Writer(Object, Bytes, false, false, false, PPF_DuplicateVerbatim);
    return Bytes;
}
struct FNativeObjectState
{
    UObject* Object;
    UObject* Outer;
    UClass* Class;
    FString Path;
    EObjectFlags Flags;
    TArray<uint8> Bytes;
    explicit FNativeObjectState(UObject* In)
        : Object(In), Outer(In->GetOuter()), Class(In->GetClass()), Path(In->GetPathName()), Flags(In->GetFlags()),
          Bytes(P4GenBaseBytes(In))
    {}
    bool Same() const
    {
        return Object->GetOuter() == Outer && Object->GetClass() == Class && Object->GetPathName() == Path &&
            Object->GetFlags() == Flags && P4GenBaseBytes(Object) == Bytes;
    }
};
} //namespace P4PlacementGenBaseCoverage

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PlacementReconstructionRoot, "P4MapKit.Quality.PlacementReconstructionRoot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PlacementReconstructionRoot::RunTest(const FString& Parameters)
{
    (void)Parameters;
    UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.CakMusicPlayerActor"));
    if (!TestNotNull(TEXT("Captured music actor declaration is installed"), Class) ||
        !TestNotNull(TEXT("Music actor inherits placement data"), Class->FindPropertyByName(TEXT("GenerateInfo"))))
        return false;
    AActor* Default = Cast<AActor>(Class->GetDefaultObject());
    if (!TestNotNull(TEXT("Music actor default exists"), Default) ||
        !TestNull(TEXT("Source default has no root"), Default->GetRootComponent()))
        return false;
    const P4PlacementGenBaseCoverage::FNativeObjectState Before(Default);
    for (const EWorldType::Type Kind : {EWorldType::Inactive, EWorldType::Editor})
    {
        const FString PackageName = TEXT("/Game/P4PlacementRootCoverage/") +
            FGuid::NewGuid().ToString(EGuidFormats::Digits);
        UWorld* World = UWorld::CreateWorld(
            Kind, false, FName(TEXT("PlacementRootWorld")), CreatePackage(*PackageName));
        if (!TestNotNull(TEXT("Isolated native world exists"), World))
            return false;
        GEngine->CreateNewWorldContext(Kind).SetCurrentWorld(World);
        ON_SCOPE_EXIT
        {
            World->DestroyWorld(false);
            GEngine->DestroyWorldContext(World);
        };
        AActor* Actor = World->SpawnActor<AActor>(Class);
        if (!TestNotNull(TEXT("Native actor is spawned"), Actor))
            return false;
        if (Kind == EWorldType::Inactive)
        {
            TestNull(TEXT("Reconstruction retains the source's absent root"), Actor->GetRootComponent());
            TestEqual(TEXT("Reconstruction adds no instance components"), Actor->GetInstanceComponents().Num(), 0);
        }
        else
            TestNotNull(TEXT("Creator placement receives its transform root"), Actor->GetRootComponent());
    }
    TestTrue(TEXT("Creator and reconstruction spawns preserve the declaration default"), Before.Same());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PlacementGenBaseMissingActorSource,
    "P4MapKit.Quality.PlacementGenBaseMissingActorSource",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PlacementGenBaseMissingActorSource::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace P4PlacementGenBaseCoverage;
    UClass* GenBase = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.GenBase"));
    UClass* GActor = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.GActor"));
    UClass* GuardRail = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.GuardRailActor"));
    if (!TestNotNull(TEXT("Actual captured native GenBase class is installed"), GenBase) ||
        !TestTrue(TEXT("Actual GenBase is a native actor class"), GenBase->IsChildOf(AActor::StaticClass())) ||
        !TestNull(TEXT("Actual GenBase has no captured GenerateInfo source"),
            GenBase->FindPropertyByName(TEXT("GenerateInfo"))) ||
        !TestNull(TEXT("Actual GenBase has no captured AssetVersion source"),
            GenBase->FindPropertyByName(TEXT("AssetVersion"))) ||
        !TestNotNull(TEXT("Native engine is available"), GEngine) ||
        !TestTrue(TEXT("Actual native GuardRail class has real GActor ancestry"),
            GActor && GuardRail && GuardRail->IsChildOf(GActor)) ||
        !TestNotNull(TEXT("Native GActor descendant inherits real GenerateInfo"),
            GuardRail->FindPropertyByName(TEXT("GenerateInfo"))) ||
        !TestNull(TEXT("Native GActor descendant has no captured AssetVersion source"),
            GuardRail->FindPropertyByName(TEXT("AssetVersion"))))
        return false;

    const FString PackageName = TEXT("/Game/P4PlacementGenBaseCoverage/") +
        FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UPackage* Package = CreatePackage(*PackageName);
    UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, FName(TEXT("GenBaseWorld")), Package);
    if (!TestNotNull(TEXT("Genuine initialized isolated world"), World))
        return false;
    GEngine->CreateNewWorldContext(EWorldType::Editor).SetCurrentWorld(World);
    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
    };

    FActorSpawnParameters Spawn;
    Spawn.Name = FName(TEXT("AuthoredGenBase"));
    AActor* Actor = World->SpawnActor<AActor>(GenBase, FTransform::Identity, Spawn);
    if (!TestNotNull(TEXT("Public native SpawnActor supplies actual GenBase instance"), Actor))
        return false;
    Actor->Tags.Add(FName(TEXT("retain-native-generator-tags")));
    Actor->InitialLifeSpan = 13.25f;
    Actor->SetActorLabel(TEXT("Authored GenBase missing qualified placement source"));
    Spawn.Name = FName(TEXT("AuthoredGuardRail"));
    AActor* RailActor = World->SpawnActor<AActor>(GuardRail, FTransform::Identity, Spawn);
    if (!TestNotNull(TEXT("Public native SpawnActor supplies actual GActor descendant"), RailActor))
        return false;
    RailActor->Tags.Add(FName(TEXT("retain-native-rail-tags")));
    UMetaData* Metadata = Package->GetMetaData();
    AActor* Default = Cast<AActor>(GenBase->GetDefaultObject(false));
    AActor* RailDefault = Cast<AActor>(GuardRail->GetDefaultObject(false));
    if (!TestTrue(TEXT("Native spawns used existing actual CDOs"), Default && RailDefault))
        return false;

    const bool Dirty = Package->IsDirty();
    const uint32 PackageFlags = Package->GetPackageFlags();
    const TArray<AActor*> Actors = World->PersistentLevel->Actors;
    const TArray<ULevel*> Levels = World->GetLevels();
    const ULevel* Current = World->GetCurrentLevel();
    const FGuid Guid = Actor->GetActorGuid();
    const FTransform Pose = Actor->GetActorTransform();
    const TArray<FName> Tags = Actor->Tags;
    TArray<UObject*> Objects;
    GetObjectsWithOuter(Package, Objects, true);
    TArray<FNativeObjectState> Before;
    for (UObject* Object : Objects)
        Before.Emplace(Object);
    const FNativeObjectState DefaultBefore(Default);
    const FNativeObjectState RailDefaultBefore(RailDefault);

    // GenBase qualifies native ancestry, but its real schema lacks GenerateInfo.
    // An empty result is the independent expected outcome: identity alone may
    // not manufacture a generator row from an unrelated native actor class.
    TestTrue(TEXT("GenBase without GenerateInfo refuses placement projection"),
        UP4AuthoringTools::NativePlacementInfo(Actor).IsEmpty());
    TSharedPtr<FJsonObject> Verdict;
    const FString Json = UP4AuthoringTools::NativePlacementInfo(RailActor);
    TestTrue(TEXT("Native GActor descendant returns independently checked refusal JSON"),
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Verdict) && Verdict.IsValid());
    if (Verdict.IsValid())
    {
        FString Error, Identity;
        TestTrue(TEXT("Native refusal is specifically missing captured AssetVersion source"),
            Verdict->TryGetStringField(TEXT("error"), Error) &&
                Error ==
                    TEXT(
                        "Original generator field has no qualified actor source; constructor defaults are not recovered: AssetVersion"));
        TestTrue(TEXT("Native refusal preserves exact actual actor GUID in its diagnostic"),
            Verdict->TryGetStringField(TEXT("guid"), Identity) &&
                Identity == RailActor->GetActorGuid().ToString(EGuidFormats::DigitsWithHyphens));
        TestFalse(
            TEXT("Native descendant refusal cannot publish any default-filled row"), Verdict->HasField(TEXT("row")));
        const TArray<TSharedPtr<FJsonValue>>* Ancestors = nullptr;
        const bool HasAncestors = Verdict->TryGetArrayField(TEXT("ancestors"), Ancestors) && Ancestors &&
            Ancestors->Num() >= 2;
        TestTrue(TEXT("Native diagnostic includes descendant and GActor ancestry"),
            HasAncestors && (*Ancestors)[0]->AsString() == GuardRail->GetPathName() &&
                (*Ancestors)[1]->AsString() == GActor->GetPathName());
    }
    for (const FNativeObjectState& State : Before)
        TestTrue(TEXT("Refusal retains exact complete native world, actor, components and metadata"), State.Same());
    TestTrue(TEXT("Refusal preserves actual class CDO bytes and identity"),
        DefaultBefore.Same() && GenBase->GetDefaultObject(false) == Default);
    TestTrue(TEXT("Refusal preserves native descendant CDO bytes and identity"),
        RailDefaultBefore.Same() && GuardRail->GetDefaultObject(false) == RailDefault);
    TArray<UObject*> After;
    GetObjectsWithOuter(Package, After, true);
    TestEqual(TEXT("Refusal does not construct or publish package objects"), After.Num(), Objects.Num());
    for (UObject* Object : Objects)
        TestTrue(TEXT("Every original package object remains present"), After.Contains(Object));
    TestTrue(TEXT("Exact actual level/actor membership and current level retained"),
        World->PersistentLevel->Actors == Actors && World->GetLevels() == Levels &&
            World->GetCurrentLevel() == Current && Actor->GetLevel() == World->PersistentLevel);
    TestTrue(TEXT("Independent authored tag, pose, GUID and lifespan values retained"),
        Actor->Tags == Tags && Actor->GetActorTransform().Equals(Pose, 0) && Actor->GetActorGuid() == Guid &&
            Actor->InitialLifeSpan == 13.25f);
    TestTrue(TEXT("Actual package dirty state, flags and metadata pointer retained"),
        Package->IsDirty() == Dirty && Package->GetPackageFlags() == PackageFlags &&
            Package->GetMetaData() == Metadata);
    TestTrue(TEXT("Owned native engine context retains exact world"),
        GEngine->GetWorldContextFromWorld(World) && GEngine->GetWorldContextFromWorld(World)->World() == World);
    return !HasAnyErrors();
}
#endif

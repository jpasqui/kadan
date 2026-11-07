#if P4_MAPKIT_TESTS
#include "P4GameLighting.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/PointLight.h"
#include "Components/StaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Misc/AutomationTest.h"
#include "Misc/Base64.h"
#include "Misc/Compression.h"
#include "Misc/ScopeExit.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4OriginalLightingTest, "P4MapKit.Export.OriginalLightingIdentities",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4OriginalLightingTest::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false,
        FName(*(TEXT("OriginalLighting_") + FGuid::NewGuid().ToString(EGuidFormats::Digits))), GetTransientPackage());
    if (!TestNotNull(TEXT("The owned preview world exists"), World))
        return false;
    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false);
    };
    UMetaData* Metadata = World->GetOutermost()->GetMetaData();
    const auto Retain = [&](const FString& Lines)
    {
        const FTCHARToUTF8 Plain(*Lines);
        int32 Size = FCompression::CompressMemoryBound(NAME_Zlib, Plain.Length());
        TArray<uint8> Data;
        Data.SetNumUninitialized(Size);
        if (!FCompression::CompressMemory(NAME_Zlib, Data.GetData(), Size, Plain.Get(), Plain.Length()))
            return false;
        Data.SetNum(Size);
        Metadata->SetValue(World, TEXT("P4NativeLevelLighting"),
            *(FString::Printf(TEXT("%d:"), Plain.Length()) + FBase64::Encode(Data)));
        return true;
    };
    FP4OriginalLevelLighting Read;
    FString Error;
    TestFalse(TEXT("Missing original lighting remains unresolved"), P4ReadOriginalLevelLighting(World, Read, Error));
    const FString Component = World->GetName() + TEXT(".") + World->PersistentLevel->GetName() +
        TEXT(".RemovedMesh.StaticMeshComponent");
    const FString Registry = TEXT("registry\t/Game/Maps/Test_BuiltData.Test_BuiltData");
    const FString Lit = Component + TEXT("\t00000001 00000002 00000003 00000004");
    TestTrue(TEXT("The source lighting identities are retained"), Retain(Registry + TEXT("\n") + Lit));
    TestTrue(TEXT("The native source identities are readable"), P4ReadOriginalLevelLighting(World, Read, Error));
    TestTrue(
        TEXT("A removed actor is still classified by its source GUIDs"), Read.Actors.Contains(TEXT("RemovedMesh")));
    const TArray<FGuid>* Ids = Read.Components.Find(Component);
    TestTrue(TEXT("The exact original component GUID is retained"),
        Ids && Ids->Num() == 1 && (*Ids)[0] == FGuid(1, 2, 3, 4));
    TestTrue(TEXT("A known unlit source is retained"),
        Retain(TEXT("registry\t\n") + Component + TEXT("\t00000000 00000000 00000000 00000000")));
    TestTrue(
        TEXT("A known unlit level is distinct from missing evidence"), P4ReadOriginalLevelLighting(World, Read, Error));
    TestEqual(TEXT("Zero GUIDs do not guess static lighting"), Read.Actors.Num(), 0);
    Retain(TEXT("registry\t\n") + Lit);
    TestTrue(TEXT("A level with lighting ids but no lighting build reads"), P4ReadOriginalLevelLighting(World, Read, Error));
    TestEqual(TEXT("and none of its actors is lit by a build of the game's"), Read.Actors.Num(), 0);
    Retain(Registry + TEXT("\n") + Lit + TEXT("\n") + Lit);
    TestFalse(
        TEXT("Duplicate source component identities are rejected"), P4ReadOriginalLevelLighting(World, Read, Error));
    Retain(Registry + TEXT("\n") + Component + TEXT("\tunreadable"));
    TestFalse(TEXT("Malformed source lighting IDs are unresolved"), P4ReadOriginalLevelLighting(World, Read, Error));
    const FString LightKey = World->GetName() + TEXT(".") + World->PersistentLevel->GetName() +
        TEXT(".RemovedLamp.LightComponent");
    const FGuid LightId(9, 10, 11, 12);
    const FString LightRow = TEXT("light\t") + LightKey + TEXT("\t") + LightId.ToString(EGuidFormats::Digits);
    Retain(Registry + TEXT("\n") + LightRow);
    TestTrue(TEXT("An explicitly serialized original light identity is readable"),
        P4ReadOriginalLevelLighting(World, Read, Error));
    const FGuid* ReadLight = Read.Lights.Find(LightKey);
    TestTrue(TEXT("The exact original light Guid is retained"), ReadLight && *ReadLight == LightId);
    TestTrue(TEXT("A removed light is classified from the original source"), Read.Actors.Contains(TEXT("RemovedLamp")));
    Retain(Registry + TEXT("\n") + LightRow + TEXT("\n") + LightRow);
    TestFalse(TEXT("Duplicate original light identities are refused"), P4ReadOriginalLevelLighting(World, Read, Error));
    UMapBuildDataRegistry* Built = NewObject<UMapBuildDataRegistry>(GetTransientPackage());
    const TArray<FGuid> Lods{FGuid(1, 2, 3, 4), FGuid(5, 6, 7, 8), FGuid()};
    Built->AllocateMeshBuildData(Lods[0], false);
    TestFalse(
        TEXT("Rebuilding only the first LOD is insufficient"), P4ValidateRebuiltMeshLighting(Built, Lods, true, Error));
    Built->AllocateMeshBuildData(Lods[1], false);
    TestTrue(
        TEXT("Every valid LOD must have regenerated data"), P4ValidateRebuiltMeshLighting(Built, Lods, true, Error));
    TestFalse(TEXT("A removed actor cannot keep its old registry keys"),
        P4ValidateRebuiltMeshLighting(Built, Lods, false, Error));
    UMapBuildDataRegistry* Empty = NewObject<UMapBuildDataRegistry>(GetTransientPackage());
    TestTrue(
        TEXT("A removed actor's valid LOD keys are absent"), P4ValidateRebuiltMeshLighting(Empty, Lods, false, Error));
    TestFalse(
        TEXT("A retained light must have new registry data"), P4ValidateRebuiltLight(Built, LightId, true, Error));
    Built->FindOrAllocateLightBuildData(LightId, false);
    TestTrue(
        TEXT("The retained original light has newly built data"), P4ValidateRebuiltLight(Built, LightId, true, Error));
    TestFalse(
        TEXT("A removed light cannot retain its registry key"), P4ValidateRebuiltLight(Built, LightId, false, Error));
    TestTrue(TEXT("A removed light's registry key is absent"), P4ValidateRebuiltLight(Empty, LightId, false, Error));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4RemovedLightingBeforeGCTest, "P4MapKit.Export.RemovedLightingBeforeGC",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4RemovedLightingBeforeGCTest::RunTest(const FString&)
{
    if (!TestNotNull(TEXT("The engine can own a preview world"), GEngine))
        return false;
    UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false,
        FName(*(TEXT("RemovedLighting_") + FGuid::NewGuid().ToString(EGuidFormats::Digits))), GetTransientPackage());
    if (!TestNotNull(TEXT("The owned preview world exists"), World))
        return false;
    GEngine->CreateNewWorldContext(EWorldType::EditorPreview).SetCurrentWorld(World);
    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
    };
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!TestNotNull(TEXT("The native engine cube is available"), Cube))
        return false;
    AStaticMeshActor* Mesh = World->SpawnActor<AStaticMeshActor>(FVector::ZeroVector, FRotator::ZeroRotator);
    APointLight* Lamp = World->SpawnActor<APointLight>(FVector(0, 0, 200), FRotator::ZeroRotator);
    AStaticMeshActor* Retained = World->SpawnActor<AStaticMeshActor>(FVector(200, 0, 0), FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("The mesh actor is spawned"), Mesh) ||
        !TestNotNull(TEXT("The light actor is spawned"), Lamp) ||
        !TestNotNull(TEXT("A separate retained actor is spawned"), Retained))
        return false;
    ULevel* Level = Mesh->GetLevel();
    UStaticMeshComponent* Part = Mesh->GetStaticMeshComponent();
    ULightComponent* Light = Lamp->GetLightComponent();
    Part->SetStaticMesh(Cube);
    Part->SetMobility(EComponentMobility::Static);
    Light->SetMobility(EComponentMobility::Static);
    TestTrue(TEXT("The real mesh actor has static lighting"), Part->HasStaticLighting());
    TestTrue(TEXT("The real light actor has static lighting"), Light->HasStaticLighting());
    const TArray<FGuid> Ids{FGuid(21, 22, 23, 24)};
    const FGuid LightId(31, 32, 33, 34);
    Part->SetLODDataCount(Ids.Num(), Ids.Num());
    for (int32 Lod = 0; Lod < Ids.Num(); ++Lod)
        Part->LODData[Lod].MapBuildDataId = Ids[Lod];
    Light->LightGuid = LightId;
    const FString MeshPath = Part->GetPathName(Level), LightPath = Light->GetPathName(Level);
    const FString ActorName = Mesh->GetName();
    TestTrue(TEXT("The level initially retains the mesh component"), P4IsRetainedLightingObject(Level, Part));
    TestTrue(TEXT("The level initially retains the light component"), P4IsRetainedLightingObject(Level, Light));
    UMapBuildDataRegistry* Stale = NewObject<UMapBuildDataRegistry>(GetTransientPackage());
    for (const FGuid& Id : Ids)
        Stale->AllocateMeshBuildData(Id, false);
    Stale->FindOrAllocateLightBuildData(LightId, false);
    TestTrue(TEXT("The native editor actually destroys the lit mesh actor"), World->EditorDestroyActor(Mesh, false));
    TestTrue(TEXT("The native editor actually destroys the light actor"), World->EditorDestroyActor(Lamp, false));
    // No collection or manually supplied absence flag occurs between native
    // destruction and lookup. UE still retains these objects in its name hash.
    TestTrue(
        TEXT("FindObject still returns the destroyed actor before GC"), FindObject<AActor>(Level, *ActorName) == Mesh);
    TestTrue(TEXT("FindObject still returns the destroyed mesh before GC"),
        FindObject<UStaticMeshComponent>(Level, *MeshPath) == Part);
    TestTrue(TEXT("FindObject still returns the destroyed light before GC"),
        FindObject<ULightComponent>(Level, *LightPath) == Light);
    TestFalse(TEXT("The destroyed actor is no longer a level actor"), Level->Actors.Contains(Mesh));
    TestTrue(TEXT("The destroyed actor is pending kill"), Mesh->IsPendingKillPending());
    const bool MeshPresent = P4IsRetainedLightingObject(Level, FindObject<UStaticMeshComponent>(Level, *MeshPath));
    const bool LightPresent = P4IsRetainedLightingObject(Level, FindObject<ULightComponent>(Level, *LightPath));
    TestFalse(TEXT("The destroyed actor is not restored for the build"),
        P4IsRetainedLightingObject(Level, FindObject<AActor>(Level, *ActorName)));
    TestFalse(TEXT("The destroyed mesh is absent for fresh registry validation"), MeshPresent);
    TestFalse(TEXT("The destroyed light is absent for fresh registry validation"), LightPresent);
    FString Error;
    TestFalse(TEXT("Old data for the actually removed mesh is refused"),
        P4ValidateRebuiltMeshLighting(Stale, Ids, MeshPresent, Error));
    TestFalse(TEXT("Old data for the actually removed light is refused"),
        P4ValidateRebuiltLight(Stale, LightId, LightPresent, Error));
    UMapBuildDataRegistry* Fresh = NewObject<UMapBuildDataRegistry>(GetTransientPackage());
    TestTrue(TEXT("Fresh data omits the actually removed mesh"),
        P4ValidateRebuiltMeshLighting(Fresh, Ids, MeshPresent, Error));
    TestTrue(TEXT("Fresh data omits the actually removed light"),
        P4ValidateRebuiltLight(Fresh, LightId, LightPresent, Error));
    TestTrue(TEXT("A separate live actor is still retained"), P4IsRetainedLightingObject(Level, Retained));
    World->RemoveActor(Retained, false);
    TestTrue(TEXT("Removal from the actor list does not itself mark the actor pending kill"), IsValid(Retained));
    TestFalse(TEXT("An actor absent from the level list is not restored"), P4IsRetainedLightingObject(Level, Retained));
    TestFalse(TEXT("Its still loaded component is also absent"),
        P4IsRetainedLightingObject(Level, Retained->GetStaticMeshComponent()));
    World->EditorDestroyActor(Retained, false);
    return !HasAnyErrors();
}
#endif

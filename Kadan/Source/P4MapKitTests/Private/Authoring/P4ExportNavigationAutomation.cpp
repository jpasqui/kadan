#if P4_MAPKIT_TESTS
#include "P4ExportNavigation.h"
#include "P4LevelChanges.h"
#include "P4MeshSourceReconstruction.h"
#include "P4StaticMeshContent.h"
#include "P4MapKitCore/export_contract.h"
#include "BSPOps.h"
#include "Components/BrushComponent.h"
#include "Engine/Polys.h"
#include "Model.h"
#include "Builders/CubeBuilder.h"
#include "Components/StaticMeshComponent.h"
#include "AI/Navigation/NavRelevantInterface.h"
#include "Dom/JsonObject.h"
#include "Detour/DetourNavMesh.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "NavCollision.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavMesh/RecastNavMeshGenerator.h"
#include "Navigation/NavLinkProxy.h"
#include "NavigationSystem.h"
#include "PhysicsEngine/BodySetup.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

namespace P4ExportNavigationTest
{
namespace Nav = P4MapKit::NavigationExport;
const TCHAR* const Map = TEXT("/Game/Carrot4/Maps/NavigationTest/NavigationTest_P");

AStaticMeshActor* Box(UWorld* World, const TCHAR* Name, const FVector& At, const FVector& Scale)
{
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    FActorSpawnParameters Spawn;
    Spawn.Name = Name;
    AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(At, FRotator::ZeroRotator, Spawn);
    if (!Actor || !Cube)
        return nullptr;
    Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
    Actor->GetStaticMeshComponent()->SetStaticMesh(Cube);
    Actor->SetActorScale3D(Scale);
    return Actor;
}

// The .nav file's navigation meshes: magic, version, map, then each mesh's name and native payload.
bool Meshes(const TArray<uint8>& File, FString& Package, TMap<FString, Nav::BuiltMesh>& Out)
{
    std::size_t At = 0;
    const auto Take = [&](std::size_t Size, std::uint64_t& Value)
    {
        if (At + Size > std::size_t(File.Num()))
            return false;
        Value = 0;
        for (std::size_t I = 0; I < Size; ++I)
            Value |= std::uint64_t(File[int32(At + I)]) << (8 * I);
        At += Size;
        return true;
    };
    const auto Text = [&](std::uint64_t Size, FString& Value)
    {
        if (At + Size > std::size_t(File.Num()))
            return false;
        Value = FString(
            UTF8_TO_TCHAR(std::string(reinterpret_cast<const char*>(File.GetData()) + At, std::size_t(Size)).c_str()));
        At += std::size_t(Size);
        return true;
    };
    if (File.Num() < 8 || FMemory::Memcmp(File.GetData(), "P4MNAVM1", 8) != 0)
        return false;
    At = 8;
    std::uint64_t Version = 0, Length = 0, Count = 0;
    if (!Take(4, Version) || Version != 1 || !Take(2, Length) || !Text(Length, Package) || !Take(4, Count))
        return false;
    for (std::uint64_t I = 0; I < Count; ++I)
    {
        FString Name;
        std::uint64_t Size = 0;
        if (!Take(2, Length) || !Text(Length, Name) || !Take(4, Size) || At + Size > std::size_t(File.Num()))
            return false;
        const std::vector<std::uint8_t> Payload(File.GetData() + At, File.GetData() + At + Size);
        At += std::size_t(Size);
        Nav::BuiltMesh Mesh;
        std::string Error;
        if (!Nav::DecodePayload(Payload, Mesh, Error))
            return false;
        Out.Add(Name, MoveTemp(Mesh));
    }
    return At == std::size_t(File.Num());
}

// Whether a ground polygon of the mesh covers the point, seen from above (Recast x and z).
bool Covered(const Nav::BuiltMesh& Mesh, float X, float Z)
{
    for (const Nav::BuiltTile& Tile : Mesh.Tiles)
        for (std::size_t P = 0; P < std::size_t(Tile.Header.OffMeshBase) && P < Tile.Polys.size(); ++P)
        {
            const Nav::Poly& Poly = Tile.Polys[P];
            bool Inside = false;
            for (unsigned I = 0, J = Poly.VertCount - 1u; I < Poly.VertCount; J = I++)
            {
                const Nav::Vec3& A = Tile.Verts[Poly.Verts[I]];
                const Nav::Vec3& B = Tile.Verts[Poly.Verts[J]];
                if ((A[2] > Z) != (B[2] > Z) && X < (B[0] - A[0]) * (Z - A[2]) / (B[2] - A[2]) + A[0])
                    Inside = !Inside;
            }
            if (Inside)
                return true;
        }
    return false;
}
int32 Links(const Nav::BuiltMesh& Mesh)
{
    int32 Count = 0;
    for (const Nav::BuiltTile& Tile : Mesh.Tiles)
        Count += int32(Tile.PointLinks.size());
    return Count;
}
} //namespace P4ExportNavigationTest

// A level of the game's kind, made here so that the test needs no imported map: a floor parted by a wall,
// built as the game's navigation. The wall is taken out and a jump link placed, and export's navigation
// build makes the mod's .nav file from it, which the manifest names for the map.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ExportNavigationTest, "P4MapKit.Export.NavigationRemovedWallAndLink",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ExportNavigationTest::RunTest(const FString&)
{
    using namespace P4ExportNavigationTest;
    UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    if (!TestNotNull(TEXT("a new level opens in the editor"), World))
        return false;
    UNavigationSystemV1* System = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if (!TestNotNull(TEXT("the level has the editor's navigation system"), System))
        return false;
    // A floor of 20 by 20 metres, and a wall across all of it 3 metres east of the middle.
    AStaticMeshActor* Floor = Box(World, TEXT("Floor"), FVector(0, 0, -10), FVector(20, 20, 0.2f));
    AStaticMeshActor* Wall = Box(World, TEXT("Wall"), FVector(300, 0, 150), FVector(0.5f, 20, 3));
    ANavMeshBoundsVolume* Bounds = World->SpawnActor<ANavMeshBoundsVolume>(FVector(0, 0, 100), FRotator::ZeroRotator);
    if (!TestTrue(TEXT("the floor, the wall and the navigation bounds are placed"), Floor && Wall && Bounds))
        return false;
    UCubeBuilder* Builder = NewObject<UCubeBuilder>();
    Builder->X = 2400;
    Builder->Y = 2400;
    Builder->Z = 800;
    // What the editor's volume factory does: a brush model of the builder's shape.
    Bounds->PreEditChange(nullptr);
    Bounds->Brush = NewObject<UModel>(Bounds, NAME_None, RF_Transactional);
    Bounds->Brush->Initialize(nullptr, true);
    Bounds->Brush->Polys = NewObject<UPolys>(Bounds->Brush, NAME_None, RF_Transactional);
    Bounds->GetBrushComponent()->Brush = Bounds->Brush;
    Builder->Build(World, Bounds);
    FBSPOps::csgPrepMovingBrush(Bounds);
    Bounds->PostEditChange();
    System->OnNavigationBoundsUpdated(Bounds);
    // The game's navigation: the level built with the wall.
    std::vector<Nav::BakedActor> Built;
    FString Error;
    if (!TestTrue(TEXT("the level's navigation is built with the wall"),
            P4BakeWorldNavigation(World, false, Built, Error) && Built.size() > 0))
    {
        AddError(Error);
        return false;
    }
    std::vector<Nav::StockActor> Stock;
    for (const Nav::BakedActor& Actor : Built)
    {
        ARecastNavMesh* Native = nullptr;
        for (TActorIterator<ARecastNavMesh> It(World); It; ++It)
            if (IsValid(*It) && It->GetName() == UTF8_TO_TCHAR(Actor.Name.c_str()))
                Native = *It;
        if (!TestNotNull(TEXT("the synthetic stock records the actual native Recast class"), Native))
            return false;
        Nav::StockActor Game;
        Game.Name = Actor.Name;
        Game.Class = TCHAR_TO_UTF8(*Native->GetClass()->GetPathName());
        Game.Mesh = Actor.Mesh;
        Stock.push_back(MoveTemp(Game));
    }
    // Recast's x is Unreal's -X and its z Unreal's -Y: the wall stands at Recast x -300.
    const Nav::BuiltMesh& Before = Stock[0].Mesh;
    TestFalse(TEXT("the game's navigation has no ground where the wall stands"), Covered(Before, -300, 0));
    TestTrue(TEXT("the game's navigation has ground beside the wall"), Covered(Before, 300, 0));
    // The creator's edit: the wall taken out and a link put across the floor.
    World->EditorDestroyActor(Wall, true);
    ANavLinkProxy* Link = World->SpawnActorDeferred<ANavLinkProxy>(
        ANavLinkProxy::StaticClass(), FTransform(FVector(-500, 500, 0)));
    if (!TestNotNull(TEXT("a navigation link is placed"), Link))
        return false;
    Link->PointLinks.SetNum(1);
    Link->PointLinks[0].Left = FVector(-200, 0, 0);
    Link->PointLinks[0].Right = FVector(200, 0, 0);
    Link->PointLinks[0].Direction = ENavLinkDirection::BothWays;
    Link->FinishSpawning(FTransform(FVector(-500, 500, 0)));
    TestTrue(TEXT("export takes the link as navigation only"), P4IsNavigationOnlyActor(Link));
    TestFalse(TEXT("export takes the floor as an ordinary actor"), P4IsNavigationOnlyActor(Floor));
    struct FGenerationBefore
    {
        ARecastNavMesh* Actor;
        FRecastNavMeshGenerationProperties Settings;
        FVector Origin;
        explicit FGenerationBefore(ARecastNavMesh* In) : Actor(In), Settings(*In), Origin(In->NavMeshOriginOffset) {}
    };
    TArray<FGenerationBefore> GenerationBefore;
    for (TActorIterator<ARecastNavMesh> It(World); It; ++It)
        if (IsValid(*It))
            GenerationBefore.Emplace(*It);
    const bool DirtyBefore = World->GetOutermost()->IsDirty();
    const auto Restored = [&]()
    {
        TestTrue(TEXT("navigation export retains the editor's active native system"),
            World->GetNavigationSystem() == System);
        TestEqual(
            TEXT("navigation export preserves the world's dirty state"), World->GetOutermost()->IsDirty(), DirtyBefore);
        for (const FGenerationBefore& Entry : GenerationBefore)
        {
            const ARecastNavMesh* Actor = Entry.Actor;
            TestTrue(TEXT("map-specific Recast settings are restored after the scoped bake"),
                IsValid(Actor) && Actor->TileSizeUU == Entry.Settings.TileSizeUU &&
                    Actor->TilePoolSize == Entry.Settings.TilePoolSize &&
                    Actor->bFixedTilePoolSize == Entry.Settings.bFixedTilePoolSize &&
                    Actor->CellSize == Entry.Settings.CellSize && Actor->AgentHeight == Entry.Settings.AgentHeight &&
                    Actor->AgentMaxStepHeight == Entry.Settings.AgentMaxStepHeight &&
                    Actor->NavMeshOriginOffset == Entry.Origin);
        }
    };
    // A refusal after the first actor was configured must still restore it.
    std::vector<Nav::StockActor> Ambiguous = Stock;
    Ambiguous.push_back(Stock.front());
    FP4MapNavigation Refused;
    TestFalse(TEXT("duplicate source agent identities are refused"),
        P4BakeMapNavigation(World, Map, Ambiguous, Refused, Error));
    TestTrue(TEXT("duplicate source refusal explains the ambiguous agent"), Error.Contains(TEXT("ambiguously share")));
    Restored();
    FP4MapNavigation Made;
    if (!TestTrue(TEXT("export's navigation build converts the edited level for the game"),
            P4BakeMapNavigation(World, Map, Stock, Made, Error)))
    {
        AddError(Error);
        return false;
    }
    Restored();
    for (const FString& Line : Made.Told)
        AddInfo(TEXT("told: ") + Line);
    FString Package;
    TMap<FString, Nav::BuiltMesh> Meshes;
    if (!TestTrue(TEXT("the .nav file reads back"), P4ExportNavigationTest::Meshes(Made.Bytes, Package, Meshes)))
        return false;
    TestEqual(TEXT("the file is for the map"), Package, FString(Map));
    const Nav::BuiltMesh* After = Meshes.Find(UTF8_TO_TCHAR(Stock[0].Name.c_str()));
    if (!TestNotNull(TEXT("the file replaces the game's navigation mesh"), After))
        return false;
    TestTrue(TEXT("Pikmin can walk where the wall stood"), Covered(*After, -300, 0));
    TestEqual(TEXT("the game's navigation had no link"), Links(Before), 0);
    TestEqual(TEXT("the file has the placed link"), Links(*After), 1);
    // Written beside the manifest, which names it for the map.
    const FString Folder = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir() / TEXT("P4MapKit/ExportNavigationTest/romfs/tsuchi/mods/native.nav.test"));
    IFileManager::Get().DeleteDirectory(*Folder, false, true);
    P4MapKit::Export::Descriptor Mod;
    Mod.Format = 1;
    Mod.Id = "native.nav.test";
    Mod.Name = "Native Nav Test";
    Mod.Version = "1.0.0";
    P4MapKit::Export::MapEdit Edit;
    Edit.Package = TCHAR_TO_UTF8(Map);
    Edit.Navigation = true;
    std::map<std::string, std::string> Files;
    std::string Why;
    if (!TestTrue(TEXT("the manifest is made"), P4MapKit::Export::FrameworkFiles(Mod, Edit, Files, Why)))
    {
        AddError(UTF8_TO_TCHAR(Why.c_str()));
        return false;
    }
    FString Relative;
    if (!TestTrue(TEXT("the manifest names a navigation file for the map"),
            P4MapNavigationPath(UTF8_TO_TCHAR(Files["mod.json"].c_str()), Map, Relative)))
        return false;
    if (!TestTrue(TEXT("the .nav file is written where the manifest says"),
            P4WriteMapNavigation(Folder, Relative, Made.Bytes, Error)))
    {
        AddError(Error);
        return false;
    }
    TestFalse(TEXT("a path out of the maps folder is refused"),
        P4WriteMapNavigation(Folder, TEXT("maps/../mod.json"), Made.Bytes, Error));
    TSharedPtr<FJsonObject> Manifest;
    const TArray<TSharedPtr<FJsonValue>>* Maps = nullptr;
    const TSharedPtr<FJsonObject>* Entry = nullptr;
    FString Named, Entered;
    const bool Read = FJsonSerializer::Deserialize(
                          TJsonReaderFactory<>::Create(UTF8_TO_TCHAR(Files["mod.json"].c_str())), Manifest) &&
        Manifest.IsValid() && Manifest->TryGetArrayField(TEXT("maps"), Maps) && Maps->Num() == 1 &&
        (*Maps)[0]->TryGetObject(Entry) && (*Entry)->TryGetStringField(TEXT("map"), Entered) &&
        (*Entry)->TryGetStringField(TEXT("navigation"), Named);
    if (TestTrue(TEXT("mod.json names the navigation file for the map"), Read))
    {
        TestEqual(TEXT("the entry is the map"), Entered, FString(Map));
        TestEqual(TEXT("the navigation key names the file"), Named, FString(TEXT("maps/NavigationTest_P.nav")));
        TArray<uint8> Written;
        TestTrue(TEXT("the named file is the file written"),
            FFileHelper::LoadFileToArray(Written, *(Folder / Named)) && Written == Made.Bytes);
        TestTrue(TEXT("the named file is allowed in a mod"),
            P4MapKit::Export::AllowedFile(
                Mod, "romfs/tsuchi/mods/native.nav.test/" + std::string(TCHAR_TO_UTF8(*Named)), Why));
    }
    UEditorLoadingAndSavingUtils::NewBlankMap(false);
    return !HasAnyErrors();
}
// Opt-in scene diagnosis: read the retained native map; never build, save,
// change collision settings, or manufacture geometry for this receipt.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4NativeNavigationSceneDiagnostic, "P4MapKit.Export.NativeNavigationSceneDiagnostic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4NativeNavigationSceneDiagnostic::RunTest(const FString&)
{
    const FString Package = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_NAV_DIAG_MAP"));
    if (Package.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. ") TEXT("No retained map requested for native navigation diagnosis."));
        return true;
    }
    if (!TestTrue(TEXT("diagnostic map has an original canonical package"),
            Package.StartsWith(TEXT("/Game/Carrot4/Maps/")) && FPackageName::IsValidLongPackageName(Package)))
        return false;
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Package);
    if (!TestNotNull(TEXT("the retained map opens"), World))
        return false;
    UNavigationSystemV1* System = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
    if (!TestNotNull(TEXT("the retained map has a native navigation system"), System))
        return false;
    TMap<TWeakObjectPtr<UPackage>, bool> DirtyBefore;
    for (ULevel* Level : World->GetLevels())
        if (Level)
            DirtyBefore.FindOrAdd(Level->GetOutermost()) = Level->GetOutermost()->IsDirty();
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("package"), Package);
    Root->SetStringField(TEXT("system"), System->GetClass()->GetPathName());
    Root->SetNumberField(TEXT("registered_bounds"), System->GetNavigationBounds().Num());
    Root->SetBoolField(TEXT("anywhere_to_build"), System->IsThereAnywhereToBuildNavigation());
    TArray<TSharedPtr<FJsonValue>> DataRows, GroundRows;
    const bool RequireGeometry = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_NAV_DIAG_REQUIRE_GEOMETRY")) ==
        TEXT("1");
    UStaticMeshComponent* Repeated = nullptr;
    int32 RelevantSamples = 0;
    for (TActorIterator<ARecastNavMesh> It(World); It; ++It)
    {
        if (!IsValid(*It))
            continue;
        TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("name"), It->GetName());
        Row->SetStringField(TEXT("class"), It->GetClass()->GetPathName());
        Row->SetBoolField(TEXT("registered"), It->IsRegistered());
        Row->SetNumberField(TEXT("radius"), It->AgentRadius);
        Row->SetNumberField(TEXT("height"), It->AgentHeight);
        Row->SetNumberField(TEXT("step"), It->AgentMaxStepHeight);
        Row->SetNumberField(TEXT("cell"), It->CellSize);
        Row->SetNumberField(TEXT("pool"), It->TilePoolSize);
        Row->SetBoolField(TEXT("fixed_pool"), bool(It->bFixedTilePoolSize));
        TArray<FBox> Bounds;
        System->GetNavigationBoundsForNavData(**It, Bounds);
        TArray<TSharedPtr<FJsonValue>> BoundsRows;
        for (const FBox& Bound : Bounds)
        {
            TSharedRef<FJsonObject> Box = MakeShared<FJsonObject>();
            Box->SetStringField(TEXT("min"), Bound.Min.ToString());
            Box->SetStringField(TEXT("max"), Bound.Max.ToString());
            BoundsRows.Add(MakeShared<FJsonValueObject>(Box));
        }
        Row->SetArrayField(TEXT("agent_bounds"), BoundsRows);
        const dtNavMesh* Mesh = It->GetRecastMesh();
        int32 Tiles = 0, Polygons = 0;
        if (Mesh)
            for (int32 I = 0; I < Mesh->getMaxTiles(); ++I)
            {
                const dtMeshTile* Tile = Mesh->getTile(I);
                if (Tile && Tile->header)
                {
                    ++Tiles;
                    Polygons += Tile->header->polyCount;
                }
            }
        Row->SetNumberField(TEXT("tiles"), Tiles);
        Row->SetNumberField(TEXT("polygons"), Polygons);
        DataRows.Add(MakeShared<FJsonValueObject>(Row));
    }
    int32 Samples = 0;
    for (TActorIterator<AActor> It(World); It && Samples < 16; ++It)
    {
        if (!IsValid(*It) || !It->GetName().StartsWith(TEXT("CMA_T02_")))
            continue;
        ++Samples;
        TInlineComponentArray<UStaticMeshComponent*> Components(*It);
        for (UStaticMeshComponent* Part : Components)
        {
            if (Part->GetClass()->GetPathName() != TEXT("/Script/Carrot4.StaticCollisionComponent"))
                continue;
            TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("actor"), It->GetName());
            Row->SetStringField(TEXT("component"), Part->GetName());
            Row->SetBoolField(TEXT("registered"), Part->IsRegistered());
            Row->SetBoolField(TEXT("physics_state"), Part->IsPhysicsStateCreated());
            Row->SetBoolField(TEXT("navigation_relevant"), Part->IsNavigationRelevant());
            Row->SetBoolField(TEXT("can_affect_navigation"), Part->CanEverAffectNavigation());
            Row->SetStringField(TEXT("profile"), Part->GetCollisionProfileName().ToString());
            Row->SetNumberField(TEXT("pawn_response"), int32(Part->GetCollisionResponseToChannel(ECC_Pawn)));
            Row->SetNumberField(TEXT("custom_geometry"), int32(Part->HasCustomNavigableGeometry()));
            UStaticMesh* Mesh = Part->GetStaticMesh();
            UBodySetup* Body = Part->GetBodySetup();
            Row->SetStringField(TEXT("mesh"), Mesh ? Mesh->GetPathName() : FString());
            Row->SetBoolField(
                TEXT("render_triangle_collision_available"), Mesh && Mesh->ContainsPhysicsTriMeshData(true));
            if (Body)
            {
                Row->SetBoolField(TEXT("physics_meshes_created"), bool(Body->bCreatedPhysicsMeshes));
                Row->SetBoolField(TEXT("physics_meshes_failed"), bool(Body->bFailedToCreatePhysicsMeshes));
                Row->SetBoolField(TEXT("has_cooked_collision"), bool(Body->bHasCookedCollisionData));
#if PHYSICS_INTERFACE_PHYSX
                Row->SetNumberField(TEXT("physical_triangle_meshes"), Body->TriMeshes.Num());
#endif
            }
            const UNavCollision* Collision = Mesh ? Cast<UNavCollision>(Mesh->GetNavCollision()) : nullptr;
            Row->SetBoolField(TEXT("nav_collision_present"), Collision != nullptr);
            if (Collision)
            {
                Row->SetBoolField(TEXT("dynamic_obstacle"), Collision->IsDynamicObstacle());
                Row->SetBoolField(TEXT("nav_has_convex_geometry"), Collision->HasConvexGeometry());
                Row->SetNumberField(TEXT("nav_triangle_vertices"), Collision->GetTriMeshCollision().VertexBuffer.Num());
                Row->SetNumberField(TEXT("nav_triangle_indices"), Collision->GetTriMeshCollision().IndexBuffer.Num());
                Row->SetNumberField(TEXT("nav_convex_vertices"), Collision->GetConvexCollision().VertexBuffer.Num());
            }
            FNavigationRelevantData Geometry(*Part);
            FRecastNavMeshGenerator::ExportComponentGeometry(Part, Geometry);
            Row->SetNumberField(TEXT("engine_geometry_bytes"), Geometry.CollisionData.Num());
            int32 EngineFaces = 0;
            if (Geometry.CollisionData.Num() >= int32(sizeof(FRecastGeometryCache)))
            {
                FRecastGeometryCache::FHeader Header;
                FMemory::Memcpy(&Header, Geometry.CollisionData.GetData(), sizeof(Header));
                Row->SetNumberField(TEXT("engine_vertices"), Header.NumVerts);
                Row->SetNumberField(TEXT("engine_triangles"), Header.NumFaces);
                EngineFaces = Header.NumFaces;
            }
            if (Part->IsNavigationRelevant())
            {
                ++RelevantSamples;
                if (RequireGeometry)
                    TestTrue(
                        TEXT("ordinary cold-load source collider exports actual physical triangles"), EngineFaces > 0);
                if (!Repeated && Mesh && P4OriginalMeshBaselineUnchanged(Mesh))
                    Repeated = Part;
            }
            GroundRows.Add(MakeShared<FJsonValueObject>(Row));
        }
    }
    Root->SetArrayField(TEXT("navigation"), DataRows);
    Root->SetArrayField(TEXT("source_collision"), GroundRows);
    if (RequireGeometry)
        TestTrue(TEXT("source terrain includes navigation-relevant physical colliders"), RelevantSamples > 0);
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_NAV_DIAG_REBUILD_MESH")) == TEXT("1"))
    {
        if (!TestNotNull(
                TEXT("a source-backed collision mesh is available for repeated editor-build qualification"), Repeated))
            return false;
        UStaticMesh* Mesh = Repeated->GetStaticMesh();
        const FString State = P4StaticMeshState(Mesh);
        const bool MeshDirty = Mesh->GetOutermost()->IsDirty();
        if (!TestTrue(TEXT("the source collision mesh matches its immutable original baseline before rebuilding"),
                !State.IsEmpty() && P4OriginalMeshBaselineUnchanged(Mesh)))
            return false;
        for (int32 Round = 0; Round < 2; ++Round)
        {
            Mesh->PostEditChange();
            FString Error;
            if (P4HasMeshSourceReconstruction(Mesh))
                TestTrue(TEXT("repeated reconstructed editor mesh builds retain exact source layout"),
                    P4RestoreMeshSourceReconstruction(Mesh, Error));
            if (!Error.IsEmpty())
                AddError(Error);
            FNavigationRelevantData Geometry(*Repeated);
            FRecastNavMeshGenerator::ExportComponentGeometry(Repeated, Geometry);
            FRecastGeometryCache::FHeader Header{};
            if (Geometry.CollisionData.Num() >= int32(sizeof(FRecastGeometryCache)))
                FMemory::Memcpy(&Header, Geometry.CollisionData.GetData(), sizeof(Header));
            TestTrue(TEXT("normal original-mesh editor build lifecycle exposes real physical navigation triangles"),
                Header.NumVerts > 0 && Header.NumFaces > 0);
            TestTrue(TEXT("the qualified original collision cache may be refreshed idempotently"),
                P4RefreshOriginalMeshNavigation(Mesh));
            TestEqual(TEXT("source mesh content is unchanged after repeated native cache refresh"),
                P4StaticMeshState(Mesh), State);
            TestTrue(TEXT("original source mesh baseline remains qualified"), P4OriginalMeshBaselineUnchanged(Mesh));
        }
        Mesh->GetOutermost()->SetDirtyFlag(MeshDirty);
        UStaticMesh* Foreign = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
        TestFalse(TEXT("a foreign editor mesh cannot enter original-mesh navigation cache refresh"),
            P4RefreshOriginalMeshNavigation(Foreign));
        Root->SetNumberField(TEXT("repeated_mesh_builds"), 2);
    }
    const FString GameFiles = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_NAV_DIAG_GAME_FILES"));
    if (!GameFiles.IsEmpty())
    {
        const FP4LevelChangesResult Before = P4AnalyzeOriginalLevelChanges(World);
        if (!TestTrue(TEXT("short actual native bake starts from unchanged original map"),
                Before.IsValid() && Before.Unchanged))
        {
            AddError(Before.ErrorDetail);
            return false;
        }
        FP4MapNavigation Navigation;
        FString Error;
        if (!TestTrue(TEXT("actual source navigation is rebuilt and strictly converted"),
                P4BuildMapNavigation(Package, GameFiles, Navigation, Error)))
        {
            AddError(Error);
            return false;
        }
        TestFalse(TEXT("the short actual bake emits navigation instead of keeping stock"), Navigation.Kept);
        FString WrittenPackage;
        TMap<FString, P4ExportNavigationTest::Nav::BuiltMesh> Meshes;
        if (!TestTrue(TEXT("the actual native .nav bytes independently decode"),
                P4ExportNavigationTest::Meshes(Navigation.Bytes, WrittenPackage, Meshes)))
            return false;
        TestEqual(TEXT("the .nav targets the actual original map"), WrittenPackage, Package);
        TestTrue(TEXT("the actual native bake replaces source navigation meshes"), Meshes.Num() > 0);
        int32 Tiles = 0, Polygons = 0;
        for (const auto& Entry : Meshes)
        {
            TestTrue(TEXT("each emitted navigation mesh contains actual built tiles"), !Entry.Value.Tiles.empty());
            Tiles += int32(Entry.Value.Tiles.size());
            for (const auto& Tile : Entry.Value.Tiles)
                Polygons += int32(Tile.Polys.size());
        }
        TestTrue(TEXT("the native source bake contains actual tiles and polygons"), Tiles > 0 && Polygons > 0);
        const FP4LevelChangesResult After = P4AnalyzeOriginalLevelChanges(World);
        TestTrue(
            TEXT("actual navigation rebuild does not change original map content"), After.IsValid() && After.Unchanged);
        const FString File = FPaths::ProjectSavedDir() / TEXT("P4MapKit/NavigationDiagnostics/source-native.nav");
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
        TestTrue(TEXT("actual .nav is written for independent retained examination"),
            FFileHelper::SaveArrayToFile(Navigation.Bytes, *File));
        Root->SetNumberField(TEXT("emitted_meshes"), Meshes.Num());
        Root->SetNumberField(TEXT("emitted_tiles"), Tiles);
        Root->SetNumberField(TEXT("emitted_polygons"), Polygons);
        Root->SetNumberField(TEXT("nav_bytes"), Navigation.Bytes.Num());
    }
    FString Json;
    FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json));
    const FString File = FPaths::ProjectSavedDir() / TEXT("P4MapKit/NavigationDiagnostics/native-scene.json");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
    TestTrue(TEXT("bounded native navigation scene receipt is written"),
        FFileHelper::SaveStringToFile(Json, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    TestTrue(TEXT("actual native source collision components were inspected"), GroundRows.Num() > 0);
    for (const auto& Entry : DirtyBefore)
        if (Entry.Key.IsValid())
            TestEqual(TEXT("scene diagnosis does not change a loaded level's dirty state"), Entry.Key->IsDirty(),
                Entry.Value);
    return !HasAnyErrors();
}
namespace P4ExportNavigationTest
{
// Every part in which a baked tile differs from the game's; none when it is the same. Distance gets the largest distance
// of a vertex from the game's, where the two have as many.
TArray<FString> TileDifferences(const Nav::BuiltTile& Game, const Nav::BuiltTile& Baked, float& Distance)
{
    TArray<FString> Out;
    Distance = 0.f;
    if (Game.Verts.size() == Baked.Verts.size())
        for (std::size_t I = 0; I < Game.Verts.size(); ++I)
            for (int32 Axis = 0; Axis < 3; ++Axis)
                Distance = FMath::Max(Distance, FMath::Abs(Game.Verts[I][Axis] - Baked.Verts[I][Axis]));
    const Nav::TileHeader &A = Game.Header, &B = Baked.Header;
    const auto Note = [&Out](bool Differs, const TCHAR* What)
    {
        if (Differs)
            Out.Add(What);
    };
    Note(A.MaxLinkCount != B.MaxLinkCount, TEXT("header link count"));
    Note(A.OffMeshBase != B.OffMeshBase, TEXT("header ground polygon count"));
    Note(A.UserId != B.UserId, TEXT("header user id"));
    Note(A.WalkableHeight != B.WalkableHeight || A.WalkableRadius != B.WalkableRadius ||
            A.WalkableClimb != B.WalkableClimb || A.BvQuantFactor != B.BvQuantFactor,
        TEXT("header agent"));
    Note(A.Min[0] != B.Min[0] || A.Min[2] != B.Min[2] || A.Max[0] != B.Max[0] || A.Max[2] != B.Max[2],
        TEXT("header bounds across"));
    Note(A.Min[1] != B.Min[1] || A.Max[1] != B.Max[1], TEXT("header bounds height"));
    Note(Game.Verts.size() != Baked.Verts.size(), TEXT("vertex count"));
    Note(Game.Polys.size() != Baked.Polys.size(), TEXT("polygon count"));
    if (Game.Verts.size() == Baked.Verts.size())
        Note(Game.Verts != Baked.Verts, TEXT("vertices"));
    if (Game.Polys.size() == Baked.Polys.size())
    {
        bool Shape = false, Neighbors = false, Area = false, Flags = false, Type = false;
        for (std::size_t I = 0; I < Game.Polys.size(); ++I)
        {
            const Nav::Poly &P = Game.Polys[I], &Q = Baked.Polys[I];
            Shape |= P.Verts != Q.Verts || P.VertCount != Q.VertCount;
            Neighbors |= P.Neighbors != Q.Neighbors;
            Area |= P.Area != Q.Area;
            Flags |= P.Flags != Q.Flags;
            Type |= P.Type != Q.Type;
        }
        Note(Shape, TEXT("polygon vertices"));
        Note(Neighbors, TEXT("polygon neighbors"));
        Note(Area, TEXT("polygon areas"));
        Note(Flags, TEXT("polygon flags"));
        Note(Type, TEXT("polygon types"));
    }
    Note(Game.DetailVerts != Baked.DetailVerts, TEXT("detail vertices"));
    Note(Game.DetailTris != Baked.DetailTris, TEXT("detail triangles"));
    bool Detail = Game.DetailMeshes.size() != Baked.DetailMeshes.size();
    for (std::size_t I = 0; !Detail && I < Game.DetailMeshes.size(); ++I)
    {
        const Nav::DetailMesh &P = Game.DetailMeshes[I], &Q = Baked.DetailMeshes[I];
        Detail = P.VertBase != Q.VertBase || P.TriBase != Q.TriBase || P.VertCount != Q.VertCount ||
            P.TriCount != Q.TriCount;
    }
    Note(Detail, TEXT("detail meshes"));
    bool Tree = Game.BvTree.size() != Baked.BvTree.size();
    for (std::size_t I = 0; !Tree && I < Game.BvTree.size(); ++I)
        Tree = Game.BvTree[I].Bounds != Baked.BvTree[I].Bounds || Game.BvTree[I].Index != Baked.BvTree[I].Index;
    Note(Tree, TEXT("bounding volume tree"));
    bool Links = Game.PointLinks.size() != Baked.PointLinks.size();
    for (std::size_t I = 0; !Links && I < Game.PointLinks.size(); ++I)
    {
        const Nav::PointLink &P = Game.PointLinks[I], &Q = Baked.PointLinks[I];
        Links = P.Pos != Q.Pos || P.Radius != Q.Radius || P.Height != Q.Height || P.Polygon != Q.Polygon ||
            P.Flags != Q.Flags || P.Side != Q.Side || P.UserId != Q.UserId;
    }
    Note(Links, TEXT("point links"));
    Note(Game.SegmentLinks.size() != Baked.SegmentLinks.size(), TEXT("segment links"));
    Note(Game.Clusters != Baked.Clusters || Game.PolyClusters != Baked.PolyClusters, TEXT("clusters"));
    return Out;
}
} //namespace P4ExportNavigationTest

// Whether this editor's navigation build of an unchanged imported map gives the tiles the game's editor built: the
// export's own bake of the map (with the game's settings from its file), converted as export converts it, compared tile
// by tile with the game's. Writes Saved/P4MapKit/NavigationDiagnostics/rebake.json.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4NavigationRebakeMatchesGame, "P4MapKit.Export.NavigationRebakeMatchesGame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4NavigationRebakeMatchesGame::RunTest(const FString&)
{
    using namespace P4ExportNavigationTest;
    const FString Package = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_NAV_REBAKE_MAP"));
    if (Package.IsEmpty())
    {
        AddWarning(TEXT("P4MapKit test not run. No imported map named for the navigation rebake."));
        return true;
    }
    FString GameFiles = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_NAV_REBAKE_GAME_FILES"));
    if (GameFiles.IsEmpty())
        GameFiles = FPaths::ProjectSavedDir() / TEXT("Kadan/ConvertedOriginals/Carrot4/Content");
    if (!TestTrue(TEXT("the map is one of the game's"), Package.StartsWith(TEXT("/Game/"))))
        return false;
    const FString Base = GameFiles / Package.Mid(6);
    TArray<uint8> HeaderBytes, BodyBytes;
    if (!TestTrue(TEXT("the game's file of the map is there: ") + Base,
            FFileHelper::LoadFileToArray(HeaderBytes, *(Base + TEXT(".umap"))) &&
                FFileHelper::LoadFileToArray(BodyBytes, *(Base + TEXT(".uexp")))))
        return false;
    std::vector<Nav::StockActor> Stock;
    Nav::Problem Problem;
    if (!TestTrue(TEXT("the game's navigation is read"),
            Nav::ReadStockMap(std::vector<uint8>(HeaderBytes.GetData(), HeaderBytes.GetData() + HeaderBytes.Num()),
                std::vector<uint8>(BodyBytes.GetData(), BodyBytes.GetData() + BodyBytes.Num()), Stock, Problem)))
        return false;
    UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Package);
    if (!TestNotNull(TEXT("the imported map opens"), World))
        return false;
    const FP4LevelChangesResult Before = P4AnalyzeOriginalLevelChanges(World);
    if (!TestTrue(TEXT("the map is unchanged since its import"), Before.IsValid() && Before.Unchanged))
        return false;
    FP4MapNavigation Navigation;
    FString Error;
    const bool Built = P4BuildMapNavigation(Package, GameFiles, Navigation, Error);
    if (!TestTrue(TEXT("the export's navigation build runs: ") + Error, Built) ||
        !TestFalse(TEXT("the build is not kept as the game's"), Navigation.Kept))
        return false;
    FString Written;
    TMap<FString, Nav::BuiltMesh> Baked;
    if (!TestTrue(TEXT("the build decodes"), Meshes(Navigation.Bytes, Written, Baked)))
        return false;
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("package"), Package);
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (const Nav::StockActor& Actor : Stock)
    {
        const FString Name = UTF8_TO_TCHAR(Actor.Name.c_str());
        TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("actor"), Name);
        Row->SetNumberField(TEXT("game_tiles"), int32(Actor.Mesh.Tiles.size()));
        const Nav::BuiltMesh* Mine = Baked.Find(Name);
        Row->SetNumberField(TEXT("baked_tiles"), Mine ? int32(Mine->Tiles.size()) : -1);
        int32 Same = 0, Missing = 0, SameCounts = 0;
        float Farthest = 0.f;
        TMap<FString, int32> Firsts;
        TArray<TSharedPtr<FJsonValue>> Examples;
        for (const Nav::BuiltTile& Tile : Actor.Mesh.Tiles)
        {
            const Nav::BuiltTile* Match = nullptr;
            if (Mine)
                for (const Nav::BuiltTile& Other : Mine->Tiles)
                    if (Other.Header.X == Tile.Header.X && Other.Header.Y == Tile.Header.Y &&
                        Other.Header.Layer == Tile.Header.Layer)
                        Match = &Other;
            if (!Match)
            {
                ++Missing;
                continue;
            }
            float Distance = 0.f;
            const TArray<FString> Parts = TileDifferences(Tile, *Match, Distance);
            Farthest = FMath::Max(Farthest, Distance);
            SameCounts += Tile.Verts.size() == Match->Verts.size() && Tile.Polys.size() == Match->Polys.size();
            if (!Parts.Num())
            {
                ++Same;
                continue;
            }
            for (const FString& Part : Parts)
                ++Firsts.FindOrAdd(Part);
            if (Examples.Num() < 6)
                Examples.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("tile %d,%d,%d: %s"), Tile.Header.X,
                    Tile.Header.Y, Tile.Header.Layer, *FString::Join(Parts, TEXT(", ")))));
        }
        Row->SetNumberField(TEXT("same_tiles"), Same);
        Row->SetNumberField(TEXT("tiles_not_baked"), Missing);
        Row->SetNumberField(TEXT("same_vertex_and_polygon_counts"), SameCounts);
        Row->SetNumberField(TEXT("largest_vertex_distance"), Farthest);
        TSharedRef<FJsonObject> FirstRows = MakeShared<FJsonObject>();
        for (const auto& Pair : Firsts)
            FirstRows->SetNumberField(Pair.Key, Pair.Value);
        Row->SetObjectField(TEXT("tiles_differing_in"), FirstRows);
        Row->SetArrayField(TEXT("examples"), Examples);
        Rows.Add(MakeShared<FJsonValueObject>(Row));
        AddInfo(FString::Printf(TEXT("%s: %d of %d tiles the same (%d not baked, %d with the same counts)"), *Name, Same,
            int32(Actor.Mesh.Tiles.size()), Missing, SameCounts));
    }
    Root->SetArrayField(TEXT("actors"), Rows);
    FString Json;
    FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json));
    const FString File = FPaths::ProjectSavedDir() / TEXT("P4MapKit/NavigationDiagnostics/rebake.json");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
    TestTrue(TEXT("the comparison is written"), FFileHelper::SaveStringToFile(Json, *File));
    return !HasAnyErrors();
}
#endif

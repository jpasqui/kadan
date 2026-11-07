#if P4_MAPKIT_TESTS
#include "P4ExportContainer.h"
#include "P4GameLighting.h"
#include "P4LightingBakeResult.h"
#include "Components/LightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "LightingBuildOptions.h"
#include "RenderingThread.h"
#include "P4ExportDriver.h"
#include "P4ExportSelection.h"
#include "P4KadanCreatorReview.h"
#include "P4LevelChanges.h"
#include "AssetRegistryModule.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "PhysicsEngine/BodySetup.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"
#include <string>
#include <vector>

namespace P4ExportDriverTest
{
bool Request(
    FAutomationTestBase& Test, const FString& Id, const FString& Name, FP4ExportModRequest& Out, FP4ModNames& Names);
UObject* ImportOwn(FAutomationTestBase& Test, const FString& Root, const FString& Name, bool Mesh);
bool CookedNames(const FString& Header, std::vector<std::string>& Names);
bool Has(const std::vector<std::string>& Names, const char* Name);
TArray<FString> ContentFiles();
} //namespace P4ExportDriverTest

namespace P4AddedLightingBakeTest
{
// Rebake also takes out a game actor the game's own build lit, so the export's one lighting build rebuilds the game's
// lighting as well as lighting the added cube.
bool Run(FAutomationTestBase& Test, bool Rebake, bool ReviewOnly = false)
{
    using namespace P4ExportDriverTest;
    const TCHAR* const Cave000 = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/Cave000_F00_P");
    const TCHAR* const GameMaterial = TEXT(
        "/Game/Carrot4/Terrain/Cave/Type/Soil/Meshes/CMA_T02_DC_DirtPile01/MI_stg_Soil_Cmn_DirtPile01");
    if (!FPackageName::DoesPackageExist(Cave000))
    {
        Test.AddWarning(TEXT(
            "P4MapKit test not run. Run it in a project with an imported Cave000_F00, such as a copy of the cave000 fixture."));
        return true;
    }
    FP4ExportModRequest Request;
    FP4ModNames Names;
    FString Error;
    if (!P4ExportDriverTest::Request(Test, Rebake ? TEXT("native.bake.rebake.test") : TEXT("native.bake.test"),
            TEXT("Native Bake Test"), Request, Names))
        return false;
    UWorld* Map = UEditorLoadingAndSavingUtils::LoadMap(Cave000);
    if (!Test.TestNotNull(TEXT("The game map opens"), Map))
        return false;
    ON_SCOPE_EXIT
    {
        if (GEditor)
            UEditorLoadingAndSavingUtils::NewBlankMap(false);
    };
    ULevel* Area = nullptr;
    FString AreaPackage;
    for (ULevelStreaming* Streaming : Map->GetStreamingLevels())
        if (Streaming && Streaming->GetWorldAssetPackageName().EndsWith(TEXT("/Cave000_F00_GameArea")))
        {
            Area = Streaming->GetLoadedLevel();
            AreaPackage = Streaming->GetWorldAssetPackageName();
        }
    if (!Test.TestNotNull(TEXT("The game area is loaded with the map"), Area))
        return false;
    UWorld* AreaWorld = CastChecked<UWorld>(Area->GetOuter());
    if (!Test.TestTrue(TEXT("The game area starts as imported"), P4AnalyzeOriginalLevelChanges(AreaWorld).Unchanged))
        return false;
    UMaterialInterface* Material = LoadObject<UMaterialInterface>(
        nullptr, *(FString(GameMaterial) + TEXT(".") + FPackageName::GetShortName(GameMaterial)));
    if (!Test.TestNotNull(TEXT("The game's material instance for the cube is imported"), Material))
        return false;

    // The creator's cube, imported from a file with the importer's lightmap coordinates and given box collision.
    for (const TCHAR* Leaf : {TEXT("SM_BakeCube")})
        if (FindObject<UPackage>(nullptr, *(Names.PackageRoot / Leaf)) ||
            FPackageName::DoesPackageExist(Names.PackageRoot / Leaf))
        {
            Test.AddError(TEXT("The cube's package is already in the project. Run the test in a fresh fixture copy."));
            return false;
        }
    TStrongObjectPtr<UStaticMesh> Cube(
        Cast<UStaticMesh>(ImportOwn(Test, Names.PackageRoot, TEXT("SM_BakeCube"), true)));
    if (!Test.TestTrue(TEXT("The creator cube is imported"), Cube.IsValid()))
        return false;
    ON_SCOPE_EXIT
    {
        if (!Cube.IsValid())
            return;
        FAssetRegistryModule::AssetDeleted(Cube.Get());
        Cube->ClearFlags(RF_Public | RF_Standalone);
        Cube->GetOutermost()->SetDirtyFlag(false);
    };
    Cube->CreateBodySetup();
    FKBoxElem Box;
    Box.X = 100;
    Box.Y = 100;
    Box.Z = 100;
    Cube->BodySetup->AggGeom.BoxElems.Add(Box);
    Cube->BodySetup->InvalidatePhysicsData();
    Cube->BodySetup->CreatePhysicsMeshes();
    Cube->SetMaterial(0, Material);
    Cube->PostEditChange();
    Test.AddInfo(FString::Printf(TEXT("The cube's lightmap coordinate index is %d and its lightmap resolution %d."),
        Cube->LightMapCoordinateIndex, Cube->LightMapResolution));

    // Side by side on the floor of the passage ahead of the cave's start pod (the player arrives at about 1090 -837,
    // and the game's first camera looks along -X), so both cubes can be seen next to each other in the game.
    const FVector At(590, -837, -50);
    GEditor->BeginTransaction(FText::FromString(TEXT("Kadan lighting bake test")));
    const auto Place = [&](const TCHAR* Name, const FVector& Where, EComponentMobility::Type Mobility)
    {
        FActorSpawnParameters Spawn;
        Spawn.Name = Name;
        Spawn.OverrideLevel = Area;
        Spawn.ObjectFlags = RF_Transactional;
        const FTransform Transform(FRotator::ZeroRotator, Where);
        AStaticMeshActor* Actor = Cast<AStaticMeshActor>(
            Map->SpawnActor(AStaticMeshActor::StaticClass(), &Transform, Spawn));
        if (Actor)
        {
            Actor->GetStaticMeshComponent()->SetMobility(Mobility);
            Actor->GetStaticMeshComponent()->SetStaticMesh(Cube.Get());
        }
        return Actor;
    };
    AStaticMeshActor* Still = Place(TEXT("P4BakedCube"), At + FVector(0, 80, 0), EComponentMobility::Static);
    AStaticMeshActor* Moving = Place(TEXT("P4MovableCube"), At - FVector(0, 80, 0), EComponentMobility::Movable);
    // A game actor that the game's own build lit, taken out: the export then rebuilds the game's lighting too.
    const TCHAR* const LitGameActor = TEXT("CMA_T02_DC_Byoutake01_1");
    AActor* Removed = Rebake ? FindObject<AActor>(Area, LitGameActor) : nullptr;
    if (Removed)
        Removed->GetWorld()->EditorDestroyActor(Removed, true);
    GEditor->EndTransaction();
    bool Undone = false;
    const auto Undo = [&]()
    {
        if (Undone)
            return;
        Undone = true;
        GEditor->UndoTransaction();
        const FP4LevelChangesResult After = P4AnalyzeOriginalLevelChanges(AreaWorld);
        if (!After.Unchanged)
        {
            Test.AddInfo(TEXT("added after undo: ") + FString::Join(After.Added, TEXT(", ")));
            Test.AddInfo(TEXT("removed after undo: ") + FString::Join(After.Removed, TEXT(", ")));
            for (const FP4LevelActorMove& Move : After.Moved)
                Test.AddInfo(TEXT("moved after undo: ") + Move.Actor);
            for (const FP4LevelActorChanges& Changed : After.OtherChanged)
                Test.AddInfo(TEXT("changed after undo: ") + Changed.Actor + TEXT(" ") +
                    FString::Join(Changed.Lines.Before, TEXT(" / ")).Left(600) + TEXT(" => ") +
                    FString::Join(Changed.Lines.After, TEXT(" / ")).Left(600));
            Test.AddInfo(TEXT("level lines after undo: ") +
                FString::Join(After.LevelChanges.Before, TEXT(" / ")).Left(600) + TEXT(" => ") +
                FString::Join(After.LevelChanges.After, TEXT(" / ")).Left(600));
        }
        Test.TestTrue(TEXT("The test leaves the game area as imported"), After.Unchanged);
        Area->GetOutermost()->SetDirtyFlag(false);
    };
    ON_SCOPE_EXIT
    {
        Undo();
    };
    if (!Test.TestTrue(TEXT("Both cubes are placed"), Still && Moving))
        return false;
    if (Rebake)
    {
        const FP4LevelChangesResult Taken = P4AnalyzeOriginalLevelChanges(AreaWorld);
        // What the analysis says of the edit, for a refusal later on.
        Test.AddInfo(TEXT("removed: ") + FString::Join(Taken.Removed, TEXT(", ")));
        for (const FP4LevelActorChanges& Changed : Taken.OtherChanged)
            Test.AddInfo(TEXT("changed: ") + Changed.Actor + TEXT(" ") +
                FString::Join(Changed.Lines.Before, TEXT(" / ")).Left(800) + TEXT(" => ") +
                FString::Join(Changed.Lines.After, TEXT(" / ")).Left(800));
        if (!Test.TestTrue(TEXT("The game's lit actor is taken out, so the game's lighting is rebuilt"),
                Removed && Taken.LitByGame.Contains(LitGameActor)))
            return false;
    }
    if (Rebake)
    {
        FP4ExportGameInventory Inventory;
        if (!Test.TestTrue(TEXT("The game inventory is available for the lighting review"),
                P4ReadExportGameInventory(
                    Request.Tools, Request.GamePaksDirectory, Request.WorkDirectory, Inventory, Error)))
        {
            return false;
        }
        FP4ExportRoot ReviewRoot;
        ReviewRoot.Kind = EP4ExportRootKind::Package;
        ReviewRoot.Path = AreaPackage;
        TArray<FP4ExportRoot> ReviewRoots = {ReviewRoot};
        FP4ExportSelectionPreview Preview;
        const TMap<FName, FString> NoClaims;
        if (!Test.TestTrue(TEXT("The edited game level has a shipping preview"),
                P4PreviewExportSelection(Request.Mod, ReviewRoots, Inventory, NoClaims, Preview, Error)))
        {
            return false;
        }
        Test.TestTrue(
            TEXT("The lit actor removal is a real game-level edit"), Preview.GameEditCandidates.Contains(AreaPackage));
        const FP4KadanCreatorReview ClosedReview = P4ReviewCreatorSelection(
            Request.Mod, Preview, nullptr, &Inventory, nullptr, Request.ExtractedGameDirectory);
        Test.TestTrue(TEXT("A closed map's lighting requirement is visible before export"),
            ClosedReview.Blockers.ContainsByPredicate(
                [](const FString& Blocker) { return Blocker.Contains(TEXT("lighting changes")); }));
    }
    if (ReviewOnly)
    {
        Test.AddInfo(TEXT("The closed-map review blocked the changed original lighting before export."));
        return !Test.HasAnyErrors();
    }
    Test.AddInfo(FString::Printf(
        TEXT("The baked cube stands at %s in %s."), *Still->GetActorLocation().ToString(), *AreaPackage));

    // What export must leave as it is: the packages that are saved, the project's files, the map's levels and the
    // game's lighting data the editor holds.
    TArray<TWeakObjectPtr<UPackage>> Saved;
    for (TObjectIterator<UPackage> It; It; ++It)
        if (!It->IsDirty())
            Saved.Add(*It);
    const TArray<FString> FilesBefore = ContentFiles();
    const TArray<ULevel*> LevelsBefore = Map->GetLevels();
    ULevel* CurrentBefore = Map->GetCurrentLevel();
    const int32 UndoBefore = GEditor->Trans ? GEditor->Trans->GetQueueLength() : -1;
    struct FLevelLighting
    {
        ULevel* Level;
        UMapBuildDataRegistry* Registry;
        const void* Volume;
    };
    TArray<FLevelLighting> GameLighting;
    for (ULevel* Level : LevelsBefore)
        if (Level)
            GameLighting.Add({Level, Level->MapBuildData,
                Level->MapBuildData
                    ? Level->MapBuildData->GetLevelPrecomputedVolumetricLightmapBuildData(Level->LevelBuildDataId)
                    : nullptr});

    Request.MapPackage = Cave000;
    FP4ExportRoot Root;
    Root.Path = AreaPackage;
    Request.Roots.Add(Root);
    FP4ExportModResult Result;
    const double Started = FPlatformTime::Seconds();
    const bool Published = P4ExportMod(Request, Result, Error);
    Test.AddInfo(FString::Printf(TEXT("The export took %.0f seconds."), FPlatformTime::Seconds() - Started));
    for (const FString& Line : Result.Refused)
        Test.AddInfo(TEXT("refused: ") + Line);
    for (const FString& Line : Result.Told)
        Test.AddInfo(TEXT("told: ") + Line);
    Test.AddInfo(TEXT("packages: ") + FString::Join(Result.Packages, TEXT(", ")));
    if (!Test.TestTrue(TEXT("The export publishes the edit"), Published))
    {
        Test.AddError(Error);
        return false;
    }

    const FString Base = Names.PackageRoot + TEXT("/Maps/") + AreaPackage.Mid(6);
    const FString ModLevel = Base + TEXT("_Added"), LitLevel = Base + TEXT("_Lit"),
                  LitData = LitLevel + TEXT("_BuiltData");
    Test.TestTrue(
        TEXT("The still cube is the one baked"), Result.BakedActors == TArray<FString>({TEXT("P4BakedCube")}));
    Test.TestTrue(TEXT("The movable cube is exported without baked lighting"),
        Result.UnbakedActors == TArray<FString>({TEXT("P4MovableCube")}));
    Test.TestTrue(TEXT("The export says why the movable cube has no baked lighting"),
        Result.Told.Contains(TEXT(
            "P4MovableCube is exported without baked lighting. Its mesh can move, and the game lights what moves by itself.")));
    Test.TestTrue(TEXT("The export says the still cube is baked"),
        Result.Told.Contains(TEXT("P4BakedCube has baked lighting from the game's own lights.")));
    Test.TestTrue(TEXT("The build used the game's own Lightmass settings"),
        Result.Told.ContainsByPredicate([](const FString& Line)
            { return Line.StartsWith(TEXT("The lighting was built with the game's own Lightmass settings from ")); }));
    Test.TestTrue(TEXT("The mod has the movable cube's level and the lit level"),
        Result.Levels.Contains(ModLevel) && Result.Levels.Contains(LitLevel));
    Test.TestTrue(
        TEXT("The lit level's lighting data is the one baked"), Result.BakedLighting == TArray<FString>({LitData}));
    if (Rebake)
        Test.TestTrue(TEXT("The same build rebuilt the game's lighting"),
            Result.RebuiltLighting.Num() > 0 &&
                Result.Told.Contains(
                    TEXT("The edited map's lighting was rebuilt and is included at the game's lighting paths.")));
    else
        Test.TestTrue(
            TEXT("The game's lighting is not rebuilt for added actors alone"), Result.RebuiltLighting.Num() == 0);
    Test.TestTrue(TEXT("The container holds the lit level and its lighting data"),
        Result.Packages.Contains(LitLevel) && Result.Packages.Contains(LitData));

    // The cooked files, as they are packed.
    const FString Cooked = FPaths::ConvertRelativePathToFull(
        Request.WorkDirectory / (Request.Mod.Id + TEXT(".export-work")) / TEXT("cooked/Carrot4/Content"));
    const FString LevelFile = Cooked / LitLevel.Mid(6) + TEXT(".umap"),
                  DataFile = Cooked / LitData.Mid(6) + TEXT(".uasset");
    for (const FString& File : {LevelFile, FPaths::ChangeExtension(LevelFile, TEXT("uexp")), DataFile,
             FPaths::ChangeExtension(DataFile, TEXT("uexp"))})
        Test.TestTrue(TEXT("The cooked file is written: ") + FPaths::GetCleanFilename(File),
            IFileManager::Get().FileSize(*File) > 0);
    std::vector<std::string> LevelNames, DataNames;
    if (Test.TestTrue(TEXT("The cooked lit level is read back"), CookedNames(LevelFile, LevelNames)))
    {
        Test.TestTrue(
            TEXT("The cooked lit level names its own lighting data"), Has(LevelNames, TCHAR_TO_UTF8(*LitData)));
        Test.TestTrue(TEXT("The cooked lit level holds the still cube"), Has(LevelNames, "P4BakedCube"));
        Test.TestFalse(TEXT("The cooked lit level does not hold the movable cube"), Has(LevelNames, "P4MovableCube"));
    }
    if (Test.TestTrue(TEXT("The cooked lighting data is read back"), CookedNames(DataFile, DataNames)))
    {
        Test.TestTrue(TEXT("The cooked lighting data is a lighting registry"), Has(DataNames, "MapBuildDataRegistry"));
        Test.TestTrue(TEXT("The cooked lighting data holds a lightmap texture"), Has(DataNames, "LightMapTexture2D"));
        Test.TestFalse(TEXT("The cooked lighting data holds no reflection capture texture of the game's"),
            Has(DataNames, "TextureCube"));
    }
    // A build of the added level alone keeps the map's whole volumetric lightmap in that level's registry. A build of
    // every level gives it only the detail where its cube is, if any.
    Test.AddInfo(FString::Printf(
        TEXT("Lighting for the level as a whole was taken out of %d lit levels."), Result.BakedVolumesTakenOut));
    if (!Rebake)
        Test.TestTrue(TEXT("The lit level had lighting for the level as a whole, and it was taken out before the cook"),
            Result.BakedVolumesTakenOut == 1);

    // The project is as it was.
    int32 Changed = 0;
    for (const TWeakObjectPtr<UPackage>& Package : Saved)
        if (Package.IsValid() && Package->IsDirty())
        {
            ++Changed;
            if (Changed <= 10)
                Test.AddError(TEXT("Export left a package changed: ") + Package->GetName());
        }
    Test.TestEqual(TEXT("Export leaves every saved package saved"), Changed, 0);
    Test.TestTrue(TEXT("Export writes no file into the project's content"), ContentFiles() == FilesBefore);
    Test.TestTrue(TEXT("The map has the levels it had"), Map->GetLevels() == LevelsBefore);
    Test.TestTrue(TEXT("The map's current level is the one it was"), Map->GetCurrentLevel() == CurrentBefore);
    Test.TestTrue(TEXT("Export keeps what the person can undo"),
        UndoBefore > 0 && GEditor->Trans && GEditor->Trans->GetQueueLength() == UndoBefore);
    Test.TestFalse(TEXT("No lighting scenario is left on"), Map->GetActiveLightingScenario() != nullptr);
    Test.TestNull(TEXT("The lit level is not left in the editor"), FindObject<UPackage>(nullptr, *LitLevel));
    Test.TestNull(TEXT("Its lighting data is not left in the editor"), FindObject<UPackage>(nullptr, *LitData));
    Test.TestTrue(TEXT("The still cube is drawn in the editor again"), Still->GetStaticMeshComponent()->IsRegistered());
    for (const FLevelLighting& Each : Rebake ? TArray<FLevelLighting>() : GameLighting)
    {
        const void* Volume = Each.Level->MapBuildData
            ? Each.Level->MapBuildData->GetLevelPrecomputedVolumetricLightmapBuildData(Each.Level->LevelBuildDataId)
            : nullptr;
        Test.TestTrue(TEXT("The game's lighting data is unchanged for ") + Each.Level->GetOutermost()->GetName(),
            Each.Level->MapBuildData == Each.Registry && Volume == Each.Volume);
    }
    Undo();
    return !Test.HasAnyErrors();
}

} //namespace P4AddedLightingBakeTest

// A creator adds two cubes of their own to Cave000_F00's game area: one that does not move and one that can. Export
// lights the one that does not move inside the open map with the game's own lights, and cooks it in a level of the
// mod's own with its lighting data, which holds the cube's lightmap and no lighting for the level as a whole. The
// one that can move is exported without baked lighting and the export says why. The project is left as it was.
// Runs in a project with an imported game map, such as a copy of a kept fixture (kadan_fixture.py).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4AddedLightingBakeTest, "P4MapKit.Export.AddedLightingBake",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4AddedLightingBakeTest::RunTest(const FString&)
{
    // The whole export, P4ExportMod, with a lighting build: a heavy test (test_tiers.py).
    return P4AddedLightingBakeTest::Run(*this, false);
}

// The same with a lit game actor taken out as well: one lighting build lights the cube and rebuilds the game's
// lighting, and both are in the mod.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4AddedLightingWithGameRebakeTest, "P4MapKit.Export.AddedLightingWithGameRebake",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4AddedLightingWithGameRebakeTest::RunTest(const FString&)
{
    // The whole export, P4ExportMod, with a lighting build: a heavy test (test_tiers.py).
    return P4AddedLightingBakeTest::Run(*this, true);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4ClosedMapLightingReviewTest, "P4MapKit.Export.ClosedMapLightingReview",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4ClosedMapLightingReviewTest::RunTest(const FString&)
{
    return P4AddedLightingBakeTest::Run(*this, true, true);
}
// The bake setup's lights taken out after a real lighting build, while the level is the editor's open world and
// draws with the registry the clear changes. An earlier version of the clear hit a renderer assertion here
// (the lightmap clusters' uniform buffer), so the level is drawn again afterwards.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4LightingBakeClearAfterBuildTest, "P4MapKit.Quality.LightingBakeClearAfterBuild",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4LightingBakeClearAfterBuildTest::RunTest(const FString&)
{
    if (!GEditor || GEditor->IsLightingBuildCurrentlyRunning())
    {
        AddError(TEXT("The test needs an idle editor."));
        return false;
    }
    UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    if (!TestNotNull(TEXT("A blank map opens"), World))
        return false;
    ON_SCOPE_EXIT
    {
        if (GEditor)
            UEditorLoadingAndSavingUtils::NewBlankMap(false);
    };
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!TestNotNull(TEXT("The engine cube is available"), Cube))
        return false;
    const auto Mesh = [&](const FVector& At, const FVector& Scale)
    {
        AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(At, FRotator::ZeroRotator);
        if (Actor)
        {
            Actor->GetStaticMeshComponent()->SetStaticMesh(Cube);
            Actor->SetActorScale3D(Scale);
        }
        return Actor;
    };
    AStaticMeshActor* Floor = Mesh(FVector(0, 0, -50), FVector(10, 10, 0.5f));
    AStaticMeshActor* Box = Mesh(FVector(0, 0, 50), FVector(1));
    ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(FVector(0, 0, 500), FRotator(-45, 30, 0));
    APointLight* Lamp = World->SpawnActor<APointLight>(FVector(150, 0, 250), FRotator::ZeroRotator);
    if (!TestTrue(TEXT("The level's meshes and lights are placed"), Floor && Box && Sun && Lamp))
        return false;
    Sun->SetActorLabel(TEXT("P4BakeSun"));
    Sun->GetLightComponent()->SetMobility(EComponentMobility::Static);
    Lamp->SetActorLabel(TEXT("P4BakeLight"));
    Lamp->GetLightComponent()->SetMobility(EComponentMobility::Stationary);
    FLightingBuildOptions Options;
    Options.QualityLevel = Quality_Preview;
    FString Error;
    if (!TestTrue(TEXT("The lighting build of the level finishes and is applied"), P4RunLightingBuild(Options, Error)))
    {
        AddError(Error);
        return false;
    }
    ULevel* Level = World->PersistentLevel;
    UMapBuildDataRegistry* Registry = Level->MapBuildData;
    UStaticMeshComponent* Part = Box->GetStaticMeshComponent();
    if (!TestTrue(TEXT("The build lit the box"),
            Registry && Part->LODData.Num() && Registry->GetMeshBuildData(Part->LODData[0].MapBuildDataId)))
        return false;
    const bool HadVolume = Registry->GetLevelPrecomputedVolumetricLightmapBuildData(Level->LevelBuildDataId) != nullptr;
    AddInfo(FString::Printf(
        TEXT("The build made lighting for the level as a whole: %s."), HadVolume ? TEXT("yes") : TEXT("no")));
    // Drawn once with the build's lighting before the clear, as the editor draws it.
    GEditor->RedrawLevelEditingViewports(true);
    FlushRenderingCommands();
    const FP4LightingBakeResult Cleared = P4ClearLightingBakeNative(World);
    if (!TestTrue(TEXT("The clear succeeds"), Cleared.IsValid()))
    {
        AddError(Cleared.ErrorDetail);
        return false;
    }
    TestTrue(TEXT("Both setup lights are taken out"),
        Cleared.TakenOut.Contains(TEXT("P4BakeSun")) && Cleared.TakenOut.Contains(TEXT("P4BakeLight")) &&
            !Cleared.Left.Num());
    TestTrue(TEXT("The level's own volume lighting is taken out"),
        Cleared.BuiltLightingKnown && Cleared.VolumeLightingTakenOut == HadVolume &&
            !Registry->GetLevelPrecomputedVolumetricLightmapBuildData(Level->LevelBuildDataId));
    TestTrue(TEXT("The meshes keep their lighting"),
        Cleared.MeshesWithBuiltLighting >= 2 && Registry->GetMeshBuildData(Part->LODData[0].MapBuildDataId));
    // The frames after the clear draw the meshes with the registry's rebuilt lightmap clusters.
    for (int32 Frame = 0; Frame < 3; ++Frame)
    {
        GEditor->RedrawLevelEditingViewports(true);
        FlushRenderingCommands();
    }
    TestTrue(TEXT("The box is still drawn"), Part->IsRegistered() && Part->SceneProxy != nullptr);
    return !HasAnyErrors();
}
#endif

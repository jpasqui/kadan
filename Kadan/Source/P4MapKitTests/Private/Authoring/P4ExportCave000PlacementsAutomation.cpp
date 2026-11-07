#if P4_MAPKIT_TESTS
#include "P4ExportDriverTestShared.h"

// Cave000_F00 edited as a person edits it: a mesh of the creator's own and one of the game's added to the
// game area, one of the game's actors moved and one removed, then exported by the panel's export.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4AddedActorCave000Test,"P4MapKit.Export.AddedActorCave000",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4AddedActorCave000Test::RunTest(const FString&)
{
    using namespace P4ExportDriverTest;
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    if (Root.IsEmpty() || FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS")).IsEmpty())
    { AddWarning(TEXT("P4MapKit test not run. ") TEXT("AddedActorCave000 was not exercised. Set P4_NATIVE_ROOT and P4_NATIVE_ORIGINAL_CONTAINERS.")); return true; }
    FP4ExportModRequest Request; FP4ModNames Names; FString Error;
    if (!P4ExportDriverTest::Request(*this, TEXT("native.cave.test"), TEXT("Native Cave Test"), Request, Names)) return false;
    // The map is imported by the panel's importer, as P4MapKit.Kadan.ImportMapCave000 does, and everything the
    // import made is taken out of the project again at the end.
    const FString Game = Root / TEXT("host/Saved/P4MapKit/FullNativeCave000/game");
    for (const FString& Needed : {Game})
        if (!TestTrue(TEXT("The test's input is there: ") + Needed, IFileManager::Get().DirectoryExists(*Needed))) return false;
    FImported Imported(TEXT("ExportAddedActorCave000"));
    if (!TestTrue(TEXT("the test can keep its import cleanup list"), Imported.Ready)) return false;
    if (!TestFalse(TEXT("The project has no Cave000 before the test"), FPackageName::DoesPackageExist(Cave000))) return false;
    FP4KadanImportSettings ImportSettings;
    ImportSettings.GameFolder = Game;
    ImportSettings.GamePaks = Request.GamePaksDirectory;
    ImportSettings.Retoc = Request.Tools.Retoc;
    ImportSettings.Repak = Request.Tools.Repak;
    const FP4KadanImportResult Import = P4KadanImportMap(ImportSettings, Cave000, true);
    if (!KeepImport(*this, TEXT("AddedActorCave000"), Import)) return false;
    for (const FString& Problem : Import.Problems) AddInfo(TEXT("Not imported. ") + Problem);
    if (!TestTrue(TEXT("the panel's importer brings in all of Cave000_F00"), Import.Ok && !Import.Cancelled && FPackageName::DoesPackageExist(Cave000))) return false;
    UWorld* Persistent = UEditorLoadingAndSavingUtils::LoadMap(Cave000);
    if (!TestTrue(TEXT("the map opens in the editor"), Persistent != nullptr)) return false;
    ULevel* Area = nullptr; FString AreaPackage;
    for (ULevelStreaming* Streaming : Persistent->GetStreamingLevels())
        if (Streaming && Streaming->GetWorldAssetPackageName().EndsWith(TEXT("/Cave000_F00_GameArea")))
        { Area = Streaming->GetLoadedLevel(); AreaPackage = Streaming->GetWorldAssetPackageName(); }
    if (!TestTrue(TEXT("the game area is loaded with the map"), Area != nullptr)) return false;
    UWorld* AreaWorld = CastChecked<UWorld>(Area->GetOuter());
    if (!TestTrue(TEXT("the game area starts as imported"), P4AnalyzeOriginalLevelChanges(AreaWorld).Unchanged)) return false;
    // Opening/importing the map keeps its loaded game generators available. The explicit
    // user refresh is exercised only after startup and the complete import have finished.
    TestTrue(TEXT("the explicit game actor placement refresh completes"), UP4AuthoringTools::RefreshPlaceActors() > 0);
    TArray<FString> Palette;
    for (const FName Category : {FName(TEXT("P4Treasures")), FName(TEXT("P4Enemies")), FName(TEXT("P4Gameplay"))})
        Palette.Append(UP4AuthoringTools::PlaceActorPaths(Category));
    // The GameArea holds scenery; the actual source generator actors live in Teki.
    // Their stock GenerateInfo payload is source evidence, independent of row projection.
    ULevel* Teki = nullptr;
    for (ULevelStreaming* Streaming : Persistent->GetStreamingLevels())
        if (Streaming && Streaming->GetWorldAssetPackageName().EndsWith(TEXT("/Cave000_F00_Teki")))
        { Teki = Streaming->GetLoadedLevel(); break; }
    if (!TestNotNull(TEXT("the actual source generator sublevel is loaded"), Teki)) return false;
    AActor* Generator = nullptr;
    for (AActor* Actor : Teki->Actors)
        if (Actor && (Actor->GetName() == TEXT("GBaby2") || Actor->GetName() == TEXT("GBaby3") ||
            Actor->GetName() == TEXT("GBaby_2") || Actor->GetName() == TEXT("GSplineBaby_2")))
        { Generator = Actor; break; }
    if (!TestNotNull(TEXT("an actual stock Teki generator actor is present"), Generator)) return false;
    UClass* GeneratorClass = Generator->GetClass();
    TArray<FString> Ancestry; bool RecoveredParent = false;
    for (UClass* Parent = GeneratorClass; Parent; Parent = Parent->GetSuperClass())
    {
        Ancestry.Add(Parent->GetPathName());
        RecoveredParent |= Parent->GetOutermost()->GetName() == TEXT("/Script/Carrot4") &&
            (Parent->GetName() == TEXT("GActor") || Parent->GetName() == TEXT("GenBase"));
    }
    UObject* PaletteAsset = GeneratorClass->ClassGeneratedBy ? GeneratorClass->ClassGeneratedBy : static_cast<UObject*>(GeneratorClass);
    AddInfo(TEXT("source palette actor=") + Generator->GetPathName() + TEXT("; class=") + GeneratorClass->GetPathName() +
        FString::Printf(TEXT("; flags=%u; ancestry="), uint32(GeneratorClass->GetClassFlags())) + FString::Join(Ancestry, TEXT(" -> ")) +
        TEXT("; palette asset=") + PaletteAsset->GetPathName());
    TestTrue(TEXT("the actual source generator retains GenerateInfo"), GeneratorClass->FindPropertyByName(TEXT("GenerateInfo")) != nullptr);
    TestTrue(TEXT("the actual source generator retains recovered native ancestry"), RecoveredParent);
    TestFalse(TEXT("the actual source generator class is placeable"),
        GeneratorClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_NotPlaceable));
    TestTrue(TEXT("the actual source generator asset is listed in the placement palette"), Palette.Contains(PaletteAsset->GetPathName()));
    FOwn Own;
    if (!MakeOwn(*this, Names.PackageRoot, Own)) return false;
    const auto Named = [&](const TCHAR* Name) { return FindObject<AActor>(Area, Name); };
    AActor* Moved = Named(TEXT("CMA_T02_CP_1x1_3"));
    AActor* Removed = Named(TEXT("CMA_T02_DC_Byoutake01_1"));
    UStaticMesh* GameMesh = nullptr; AActor* Shaded = nullptr; UStaticMeshComponent* Shadow = nullptr; FString MeshReference;
    for (AActor* Actor : Area->Actors)
        if (Actor && Actor != Moved && Actor != Removed)
        {
            TInlineComponentArray<UStaticMeshComponent*> Components(Actor);
            for (UStaticMeshComponent* Component : Components)
                if (UStaticMesh* Mesh = Component->GetStaticMesh())
                    if (Mesh->GetOutermost()->GetName().StartsWith(TEXT("/Game/Carrot4/")))
                        for (TFieldIterator<FObjectPropertyBase> Field(Actor->GetClass()); Field; ++Field)
                            if (Field->ArrayDim == 1 && !CastField<FSoftObjectProperty>(*Field) && Field->GetObjectPropertyValue_InContainer(Actor) == Component)
                            { GameMesh = Mesh; Shaded = Actor; Shadow = Component; MeshReference = Field->GetName(); break; }
            if (GameMesh) break;
        }
    AWorldSettings* Settings = Area->GetWorldSettings(false);
    TestNotNull(TEXT("the source actor to move is in the game area"), Moved);
    TestNotNull(TEXT("the source actor to remove is in the game area"), Removed);
    TestNotNull(TEXT("a game actor's reflected mesh component is in the game area"), GameMesh);
    TestNotNull(TEXT("the game area's own world settings are present"), Settings);
    if (!Moved || !Removed || !GameMesh || !Settings) return false;
    if (!TestFalse(TEXT("the game actor has a reflected reference to its mesh component"), MeshReference.IsEmpty())) return false;
    AddInfo(TEXT("the changed game mesh component is ") + Shaded->GetName() + TEXT(".") + MeshReference);
    // Source creation is outside the actor-edit transaction: Undo restores game actor references,
    // rather than undoing creation/rebuilding a mesh while render proxies still reference it.
    IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    TStrongObjectPtr<UStaticMesh> ChangedMeshHeld(Cast<UStaticMesh>(Tools.DuplicateAsset(TEXT("SM_ChangedOnly"), Names.PackageRoot, Own.Mesh.Get())));
    UStaticMesh* ChangedMesh = ChangedMeshHeld.Get();
    if (ChangedMesh) Own.Made.Add(ChangedMesh);
    const FVector At = Moved->GetActorLocation();
    GEditor->BeginTransaction(FText::FromString(TEXT("Kadan export test")));
    const auto Place = [&](const TCHAR* Name, UStaticMesh* Mesh, const FVector& Where)
    {
        FActorSpawnParameters Spawn; Spawn.Name = Name; Spawn.OverrideLevel = Area; Spawn.ObjectFlags = RF_Transactional;
        const FTransform Transform(FRotator::ZeroRotator, Where);
        AStaticMeshActor* Actor = Cast<AStaticMeshActor>(Persistent->SpawnActor(AStaticMeshActor::StaticClass(), &Transform, Spawn));
        if (Actor) Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
        return Actor;
    };
    AStaticMeshActor* Mine = Place(TEXT("P4AddedOwn"), Own.Mesh.Get(), At + FVector(0, 0, 300));
    AStaticMeshActor* Theirs = Place(TEXT("P4AddedGame"), GameMesh, At + FVector(300, 0, 0));
    FP4OriginalLevelLighting OriginalLighting;
    if (!TestTrue(TEXT("The import retains original component lighting identities"), P4ReadOriginalLevelLighting(AreaWorld, OriginalLighting, Error))) return false;
    const bool RemovedWasLit = OriginalLighting.Actors.Contains(Removed->GetName());
    Moved->Modify(); Moved->SetActorLocation(At + FVector(125, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
    Removed->GetWorld()->EditorDestroyActor(Removed, true);
    const bool Casts = !Shadow->CastShadow;
    Shadow->Modify(); Shadow->CastShadow = Casts;
    if (ChangedMesh) Shadow->SetStaticMesh(ChangedMesh);
    Settings->Modify(); Settings->KillZ -= 100.0f;
    const float KillZ = Settings->KillZ;
    GEditor->EndTransaction();
    bool Undone = false;
    const auto Undo = [&]()
    {
        if (Undone) return;
        Undone = true; GEditor->UndoTransaction();
        TestTrue(TEXT("the test leaves the game area as imported"), P4AnalyzeOriginalLevelChanges(AreaWorld).Unchanged);
        Area->GetOutermost()->SetDirtyFlag(false);
    };
    ON_SCOPE_EXIT { Undo(); };
    if (!TestTrue(TEXT("both meshes are placed"), Mine && Theirs)) return false;
    if (!TestTrue(TEXT("a creator mesh is used only by a value changed on a game actor"), ChangedMesh != nullptr)) return false;
    const FP4LevelChangesResult Changes = P4AnalyzeOriginalLevelChanges(AreaWorld);
    TestTrue(TEXT("the edit reads as two added, one moved, one removed and two with changed values"), Changes.IsValid() && Changes.Added.Num() == 2 &&
        Changes.Moved.Num() == 1 && Changes.Removed == TArray<FString>({TEXT("CMA_T02_DC_Byoutake01_1")}) && Changes.OtherChanged.Num() == 2);
    TestTrue(TEXT("the review finds nothing that cannot be exported"), P4ExportLevelChangeIssue(AreaWorld, Changes).IsEmpty());
    TestEqual(TEXT("Removed actors retain their source lighting classification"),
        Changes.LitByGame.Contains(TEXT("CMA_T02_DC_Byoutake01_1")), RemovedWasLit);
    Request.MapPackage = Cave000;
    FP4ExportRoot ExportRoot; ExportRoot.Path = AreaPackage; Request.Roots.Add(ExportRoot);
    FP4ExportModResult Result;
    const bool Published = P4ExportMod(Request, Result, Error);
    for (const FString& Line : Result.Refused) AddInfo(TEXT("refused: ") + Line);
    if (!TestTrue(TEXT("the panel's export publishes the edit"), Published)) { AddError(Error); return false; }
    // Both added meshes do not move, so both are lit by the export's lighting build in the mod's lit level, and
    // no level of unlit added actors is made.
    const FString ModLevel = Names.PackageRoot + TEXT("/Maps/") + AreaPackage.Mid(6) + TEXT("_Lit");
    TestTrue(TEXT("the added actors are in one level of the mod's own"), Result.Levels == TArray<FString>({ModLevel}));
    TestTrue(TEXT("both added meshes have baked lighting"), Result.BakedActors.Contains(TEXT("P4AddedOwn")) &&
        Result.BakedActors.Contains(TEXT("P4AddedGame")) && Result.UnbakedActors.Num() == 0);
    TestTrue(TEXT("the lit level's lighting data is in the container"), Result.BakedLighting == TArray<FString>({ModLevel + TEXT("_BuiltData")}) &&
        Result.Packages.Contains(ModLevel + TEXT("_BuiltData")));
    AddInfo(TEXT("packages: ") + FString::Join(Result.Packages, TEXT(", ")));
    AddInfo(TEXT("files: ") + FString::Join(Result.Files, TEXT(", ")));
    for (const FString& Line : Result.Told) AddInfo(TEXT("told: ") + Line);
    TestTrue(TEXT("the container has the mod's level and the creator's mesh with what it uses"), Result.Packages.Contains(ModLevel) &&
        Result.Packages.Contains(Own.Mesh->GetOutermost()->GetName()) && Result.Packages.Contains(Own.Texture->GetOutermost()->GetName()) &&
        (!Own.Instance.IsValid() || Result.Packages.Contains(Own.Instance->GetOutermost()->GetName())));
    TestTrue(TEXT("the creator mesh named only by a changed game actor value is shipped"),
        Result.Packages.Contains(ChangedMesh->GetOutermost()->GetName()));
    const FString Folder = Result.Directory / TEXT("romfs/tsuchi/mods") / Request.Mod.Id;
    const TSharedPtr<FJsonObject> Manifest = ReadJson(Folder / TEXT("mod.json"));
    const TArray<TSharedPtr<FJsonValue>>* Maps = nullptr; const TArray<TSharedPtr<FJsonValue>>* Levels = nullptr;
    const TSharedPtr<FJsonObject>* Entry = nullptr; FString MapName, ChangesFile;
    const bool Listed = Manifest.IsValid() && Manifest->TryGetArrayField(TEXT("maps"), Maps) && Maps->Num() == 1 && (*Maps)[0]->TryGetObject(Entry) &&
        (*Entry)->TryGetStringField(TEXT("map"), MapName) && (*Entry)->TryGetArrayField(TEXT("levels"), Levels) &&
        (*Entry)->TryGetStringField(TEXT("changes"), ChangesFile);
    if (TestTrue(TEXT("mod.json has maps[].levels and changes"), Listed))
    {
        TestTrue(TEXT("the entry is for Cave000_F00"), MapName == Cave000);
        TestTrue(TEXT("its level is the mod's own"), Levels->Num() == 1 && (*Levels)[0]->AsString() == ModLevel);
        // A removed piece changes where Pikmin can walk: the map's navigation is built again and named. A
        // project without the game's navigation agents keeps the game's navigation and says so.
        FString Navigation;
        if ((*Entry)->TryGetStringField(TEXT("navigation"), Navigation))
            TestTrue(TEXT("the entry names the map's navigation file, which is written"),
                Navigation == TEXT("maps/Cave000_F00_P.nav") && IFileManager::Get().FileSize(*(Folder / Navigation)) > 0);
        else
        {
            AddInfo(TEXT("This project has none of the game's navigation agents, so the navigation build was not exercised here."));
            TestTrue(TEXT("export says why the game's navigation is kept"),
                Result.Told.ContainsByPredicate([](const FString& Line) { return Line.Contains(TEXT("none of the game's navigation agents")); }));
        }
        const TSharedPtr<FJsonObject> Edits = ReadJson(Folder / ChangesFile);
        const TSharedPtr<FJsonObject>* All = nullptr; const TSharedPtr<FJsonObject>* One = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Gone = nullptr; const TSharedPtr<FJsonObject>* Move = nullptr;
        TestTrue(TEXT("the changes file takes one actor out and moves one"), Edits.IsValid() && Edits->TryGetObjectField(TEXT("levels"), All) &&
            (*All)->TryGetObjectField(TEXT("Cave000_F00_GameArea"), One) && (*One)->TryGetArrayField(TEXT("remove"), Gone) &&
            Gone->Num() == 1 && (*Gone)[0]->AsString() == TEXT("CMA_T02_DC_Byoutake01_1") &&
            (*One)->TryGetObjectField(TEXT("move"), Move) && (*Move)->HasField(TEXT("CMA_T02_CP_1x1_3")));
        const TSharedPtr<FJsonObject>* Set = nullptr; const TSharedPtr<FJsonObject>* Lamp = nullptr; const TSharedPtr<FJsonObject>* World = nullptr;
        bool Shadows = !Casts; double Kill = 0;
        if (TestTrue(TEXT("the changes file sets the changed values"), One && (*One)->TryGetObjectField(TEXT("set"), Set) &&
            (*Set)->TryGetObjectField(Shaded->GetName(), Lamp) && (*Set)->TryGetObjectField(Settings->GetName(), World)))
        {
            for (const auto& Pair : (*Lamp)->Values) AddInfo(TEXT("set ") + Shaded->GetName() + TEXT(" ") + Pair.Key);
            for (const auto& Pair : (*World)->Values) AddInfo(TEXT("set ") + Settings->GetName() + TEXT(" ") + Pair.Key);
            TestTrue(TEXT("the mesh component's shadow is set through the actor's reference to it"),
                (*Lamp)->TryGetBoolField(MeshReference + TEXT(".CastShadow"), Shadows) && Shadows == Casts);
            FString MeshPath;
            TestTrue(TEXT("the changed mesh is set through its object path"),
                (*Lamp)->TryGetStringField(MeshReference + TEXT(".StaticMesh"), MeshPath) && MeshPath == ChangedMesh->GetPathName());
            TestTrue(TEXT("the world settings' kill height is set"), (*World)->TryGetNumberField(TEXT("KillZ"), Kill) && float(Kill) == KillZ);
        }
    }
    FP4ExportGameInventory Inventory; FP4ExportContainerCensus Census;
    const FString ContainerFolder = Result.Directory / TEXT("romfs/tsuchi/mods") / Request.Mod.Id;
    const FString Utoc = ContainerFolder / (Names.Container + TEXT("_P.utoc"));
    TestFalse(TEXT("No owned container is put in the global game Paks folder"),
        IFileManager::Get().DirectoryExists(*(Result.Directory / TEXT("romfs/Carrot4/Content/Paks"))));
    for (const TCHAR* Suffix : {TEXT(".pak"), TEXT(".ucas"), TEXT(".utoc")})
        TestTrue(TEXT("The complete owned container is beside the mod manifest"),
            IFileManager::Get().FileSize(*(ContainerFolder / (Names.Container + TEXT("_P") + Suffix))) > 0);
    if (TestTrue(TEXT("the game's inventory and the container are read"), P4ReadExportGameInventory(Request.Tools, Request.GamePaksDirectory, Request.WorkDirectory, Inventory, Error) &&
        P4InspectExportContainer(Request.Tools, Utoc, Request.WorkDirectory, Inventory, Census, Error)))
    {
        TestTrue(TEXT("the mod's level is in the container"), Census.PackageIds.Contains(P4ExportPackageId(ModLevel)));
        TestFalse(TEXT("the game area itself is not"), Census.PackageIds.Contains(P4ExportPackageId(AreaPackage)));
        TestEqual(TEXT("Only newly rebuilt lighting uses game package identities"), Census.GameClaims.Num(), Result.RebuiltLighting.Num());
        for (const FString& Package : Result.RebuiltLighting)
        {
            TestTrue(TEXT("Rebuilt lighting is present at its game path"), Census.GameClaims.Contains(P4ExportPackageId(Package)));
            TestTrue(TEXT("Rebuilt packages are lighting data"), Package.EndsWith(TEXT("_BuiltData")));
        }
        TestEqual(TEXT("the container holds exactly what was cooked"), Census.PackageIds.Num(), Result.Packages.Num());
    }
    else AddError(Error);
    Undo();
    return !HasAnyErrors();
}

// Cave000_F00 with three gameplay objects placed as a creator places them: a Dwarf Bulborb, a treasure and a
// pot whose drop was changed in the Details panel. Export puts them in a placement list of the mod's own,
// never in a level: the mark row first, the game's classes by path, the actors' places, numbers of the
// mod's own, and only the changed pot's AI blob written, in the game's format.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PlacementCave000Test,"P4MapKit.Export.PlacementsCave000",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PlacementCave000Test::RunTest(const FString&)
{
    using namespace P4ExportDriverTest;
    namespace PP = P4MapKit::Placement;
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    if (Root.IsEmpty() || FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS")).IsEmpty())
    { AddWarning(TEXT("P4MapKit test not run. ") TEXT("PlacementsCave000 was not exercised. Set P4_NATIVE_ROOT and P4_NATIVE_ORIGINAL_CONTAINERS.")); return true; }
    FP4ExportModRequest Request; FP4ModNames Names; FString Error;
    // The ID gives a container name whose last part before _P starts with a letter: Tsuchi takes leading digits there
    // for a patch number and then refuses to mount the container, so the export can be checked in the game.
    if (!P4ExportDriverTest::Request(*this, TEXT("native.placements.test"), TEXT("Native Placement Test"), Request, Names)) return false;
    // A project with the recovered Blueprints holds game tables whose row types the stub classes do not declare.
    // Loading them logs this; it says nothing of the export.
    AddExpectedError(TEXT("Missing RowStruct while"), EAutomationExpectedErrorFlags::Contains, 0);
    const FString Repo = FPaths::GetPath(FPaths::GetPath(Root));
    const FString Game = Root / TEXT("host/Saved/P4MapKit/FullNativeCave000/game");
    // The Bulborb and the treasures are not in Cave000: their Blueprints come from a surface map's game folder.
    FString Surface = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_PLACEMENT_GAME"));
    if (Surface.IsEmpty()) Surface = Repo / TEXT("private/fixtures/surface/game");
    for (const FString& Needed : {Game, Surface})
        if (!TestTrue(TEXT("The test's input is there: ") + Needed, IFileManager::Get().DirectoryExists(*Needed))) return false;
    const FString Kochappy = TEXT("/Game/Carrot4/Placeables/Teki/GKochappy"), Berry = TEXT("/Game/Carrot4/Placeables/Objects/Otakara/GOtaIchigo"),
        Potato = TEXT("/Game/Carrot4/Placeables/Objects/Otakara/GOtaPotato"), Pot = TEXT("/Game/Carrot4/Placeables/Gimmicks/CrackPot/GCrackPotS");
    FImported Imported(TEXT("ExportPlacementsCave000"));
    if (!TestTrue(TEXT("the test can keep its import cleanup list"), Imported.Ready)) return false;
    if (!TestFalse(TEXT("The project has no Cave000 before the test"), FPackageName::DoesPackageExist(Cave000))) return false;
    FP4KadanImportSettings ImportSettings; ImportSettings.GameFolder = Game; ImportSettings.GamePaks = Request.GamePaksDirectory; ImportSettings.Retoc = Request.Tools.Retoc;
    ImportSettings.Repak = Request.Tools.Repak;
    const FP4KadanImportResult Import = P4KadanImportMap(ImportSettings, Cave000, true);
    if (!KeepImport(*this, TEXT("PlacementsCave000"), Import)) return false;
    for (const FString& Problem : Import.Problems) AddInfo(TEXT("Not imported. ") + Problem);
    // The test needs the map and its game area. A sublevel the importer refuses (its report says why) is left out.
    if (!TestTrue(TEXT("the panel's importer brings in Cave000_F00"), FPackageName::DoesPackageExist(Cave000))) return false;
    ImportSettings.GameFolder = Surface;
    for (const FString& Package : {Kochappy, Berry, Potato, Pot})
    {
        const FP4KadanImportResult One = P4KadanImportMap(ImportSettings, Package, true);
        for (const FString& Problem : One.Problems) AddInfo(TEXT("Not imported. ") + Problem);
        if (!TestTrue(TEXT("the importer brings in the game's Blueprint ") + Package, One.Ok && FPackageName::DoesPackageExist(Package))) return false;
    }
    UWorld* Persistent = UEditorLoadingAndSavingUtils::LoadMap(Cave000);
    if (!TestTrue(TEXT("the map opens in the editor"), Persistent != nullptr)) return false;
    ULevel* Area = nullptr; FString AreaPackage;
    for (ULevelStreaming* Streaming : Persistent->GetStreamingLevels())
        if (Streaming && Streaming->GetWorldAssetPackageName().EndsWith(TEXT("/Cave000_F00_GameArea")))
        { Area = Streaming->GetLoadedLevel(); AreaPackage = Streaming->GetWorldAssetPackageName(); }
    if (!TestTrue(TEXT("the game area is loaded with the map"), Area != nullptr)) return false;
    // A level the import left in memory as its own world is loaded but never added to the map's world, and then
    // outlives the map when the editor leaves it.
    for (ULevelStreaming* Streaming : Persistent->GetStreamingLevels())
        if (Streaming && Streaming->GetLoadedLevel())
            TestTrue(TEXT("The map's world has its loaded level ") + Streaming->GetWorldAssetPackageName(),
                Persistent->GetLevels().Contains(Streaming->GetLoadedLevel()));
    UWorld* AreaWorld = CastChecked<UWorld>(Area->GetOuter());
    const auto ClassOf = [](const FString& Package) { return LoadObject<UClass>(nullptr, *(ObjectPathOf(Package) + TEXT("_C"))); };
    UClass* KochappyClass = ClassOf(Kochappy); UClass* BerryClass = ClassOf(Berry); UClass* PotatoClass = ClassOf(Potato); UClass* PotClass = ClassOf(Pot);
    if (!TestTrue(TEXT("the four classes load"), KochappyClass && BerryClass && PotatoClass && PotClass)) return false;
    // On the ground where it is: a trace down through the game area's collision, else the height given.
    const auto Ground = [&](float X, float Y, float Z)
    {
        FHitResult Hit; FCollisionQueryParams Query(TEXT("P4PlacementTest"), true);
        if (Persistent->LineTraceSingleByChannel(Hit, FVector(X, Y, Z + 300.f), FVector(X, Y, Z - 600.f), ECC_WorldStatic, Query)) return FVector(Hit.ImpactPoint);
        return FVector(X, Y, Z);
    };
    GEditor->BeginTransaction(FText::FromString(TEXT("Kadan placement test")));
    const auto Place = [&](const TCHAR* Name, UClass* Class, const FVector& At, float Yaw)
    {
        FActorSpawnParameters Spawn; Spawn.Name = Name; Spawn.OverrideLevel = Area; Spawn.ObjectFlags = RF_Transactional;
        const FTransform Transform(FRotator(0, Yaw, 0), At);
        return Persistent->SpawnActor(Class, &Transform, Spawn);
    };
    AActor* Bulborb = Place(TEXT("P4PlacedKochappy"), KochappyClass, Ground(60.f, 330.f, 10.f), 90.f);
    AActor* Treasure = Place(TEXT("P4PlacedBerry"), BerryClass, Ground(-110.f, 120.f, 0.f), 0.f);
    AActor* Crack = Place(TEXT("P4PlacedPot"), PotClass, Ground(-120.f, -10.f, 0.f), 45.f);
    GEditor->EndTransaction();
    bool Undone = false;
    const auto Undo = [&]() { if (Undone) return; Undone = true; GEditor->UndoTransaction(); Area->GetOutermost()->SetDirtyFlag(false); };
    ON_SCOPE_EXIT { Undo(); };
    if (!TestTrue(TEXT("the three gameplay objects are placed"), Bulborb && Treasure && Crack)) return false;
    // The pot's drop, changed as the Details panel changes it: one potato, always, kept as a generator.
    UActorComponent* PotAI = nullptr;
    TInlineComponentArray<UActorComponent*> Components(Crack);
    for (UActorComponent* One : Components)
        for (UClass* C = One ? One->GetClass() : nullptr; C && !PotAI; C = C->GetSuperClass())
            if (C->GetName() == TEXT("CrackPotAIComponent")) PotAI = One;
    if (!TestNotNull(TEXT("the pot has its AI component"), PotAI)) return false;
    {
        FStructProperty* Parameter = CastField<FStructProperty>(PotAI->GetClass()->FindPropertyByName(TEXT("ObjectAIParameter")));
        FStructProperty* Drop = Parameter ? CastField<FStructProperty>(Parameter->Struct->FindPropertyByName(TEXT("DropParameter"))) : nullptr;
        FArrayProperty* Items = Drop ? CastField<FArrayProperty>(Drop->Struct->FindPropertyByName(TEXT("DropItemParameter"))) : nullptr;
        FStructProperty* Item = Items ? CastField<FStructProperty>(Items->Inner) : nullptr;
        if (!TestTrue(TEXT("the pot's drop list is where the game has it"), Item != nullptr)) return false;
        PotAI->Modify();
        void* List = Items->ContainerPtrToValuePtr<void>(Drop->ContainerPtrToValuePtr<void>(Parameter->ContainerPtrToValuePtr<void>(PotAI)));
        FScriptArrayHelper Array(Items, List);
        const int32 At = Array.AddValue();
        uint8* One = Array.GetRawPtr(At);
        const auto Member = [&](UStruct* Type, void* In, const TCHAR* Name) { FProperty* F = Type->FindPropertyByName(Name); return TPair<FProperty*, void*>(F, F ? F->ContainerPtrToValuePtr<void>(In) : nullptr); };
        auto Min = Member(Item->Struct, One, TEXT("MinNum")); auto Max = Member(Item->Struct, One, TEXT("MaxNum"));
        auto Ratio = Member(Item->Struct, One, TEXT("DropRatio")); auto Keep = Member(Item->Struct, One, TEXT("bRegistGenerator"));
        auto Spawn = Member(Item->Struct, One, TEXT("SpawnMiniInfo"));
        FStructProperty* SpawnStruct = CastField<FStructProperty>(Spawn.Key);
        auto Actor = SpawnStruct ? Member(SpawnStruct->Struct, Spawn.Value, TEXT("DropActor")) : TPair<FProperty*, void*>(nullptr, nullptr);
        if (!TestTrue(TEXT("a drop has its count, chance, generator flag and actor"), Min.Key && Max.Key && Ratio.Key && Keep.Key && Actor.Key)) return false;
        CastField<FNumericProperty>(Min.Key)->SetIntPropertyValue(Min.Value, int64(1));
        CastField<FNumericProperty>(Max.Key)->SetIntPropertyValue(Max.Value, int64(1));
        CastField<FFloatProperty>(Ratio.Key)->SetPropertyValue(Ratio.Value, 1.f);
        CastField<FBoolProperty>(Keep.Key)->SetPropertyValue(Keep.Value, true);
        CastField<FObjectPropertyBase>(Actor.Key)->SetObjectPropertyValue(Actor.Value, PotatoClass);
    }
    const FP4LevelChangesResult Changes = P4AnalyzeOriginalLevelChanges(AreaWorld);
    TestTrue(TEXT("the edit reads as three added actors"), Changes.IsValid() && Changes.Added.Num() == 3);
    TestTrue(TEXT("the review finds nothing that cannot be exported"), P4ExportLevelChangeIssue(AreaWorld, Changes).IsEmpty());
    // A gameplay object in a level of the creator's own is refused, with that reason.
    {
        UPackage* OwnPackage = CreatePackage(*(Names.PackageRoot / TEXT("Maps/PlacementOwnLevel")));
        UWorld* OwnLevel = UWorld::CreateWorld(EWorldType::Editor, false, TEXT("PlacementOwnLevel"), OwnPackage);
        FActorSpawnParameters Spawn; Spawn.Name = TEXT("P4OwnLevelKochappy");
        AActor* Plain = OwnLevel ? OwnLevel->SpawnActor(KochappyClass, &FTransform::Identity, Spawn) : nullptr;
        const FP4ExportAvailability Own = OwnLevel ? P4ExportAssetAvailability(OwnLevel, Request.Mod) : FP4ExportAvailability();
        AddInfo(TEXT("own level: ") + Own.Reason);
        TestTrue(TEXT("a gameplay object left in a level of the creator's own is refused as one"), Plain && !Own.IsAvailable() && Own.Reason.Contains(TEXT("gameplay object")));
        if (OwnLevel) OwnLevel->DestroyWorld(false);
        OwnPackage->ClearFlags(RF_Public | RF_Standalone); OwnPackage->SetDirtyFlag(false); OwnPackage->MarkPendingKill();
    }
    Request.MapPackage = Cave000;
    FP4ExportRoot ExportRoot; ExportRoot.Path = AreaPackage; Request.Roots.Add(ExportRoot);
    FP4ExportModResult Result;
    const bool Published = P4ExportMod(Request, Result, Error);
    for (const FString& Line : Result.Refused) AddInfo(TEXT("refused: ") + Line);
    for (const FString& Line : Result.Told) AddInfo(TEXT("told: ") + Line);
    if (!TestTrue(TEXT("the panel's export publishes the edit"), Published)) { AddError(Error); return false; }
    AddInfo(TEXT("output: ") + Result.Directory);
    const FString List = Names.PackageRoot + TEXT("/Placements/Cave000_F00_P_Objects"), Holder = Names.PackageRoot + TEXT("/Placements/Cave000_F00_P_Placements");
    TestTrue(TEXT("one placement list, of the mod's own"), Result.Placements == TArray<FString>({List}));
    TestTrue(TEXT("no level of the mod's own: the gameplay objects are not put in one"), Result.Levels.Num() == 0);
    TestTrue(TEXT("the container holds the list and the list asset"), Result.Packages.Contains(List) && Result.Packages.Contains(Holder));
    // The rows.
    TestEqual(TEXT("three rows after the mark row"), Result.PlacementRows.Num(), 3);
    TSet<uint64> Ids; Ids.Add(Result.PlacementMark);
    TestFalse(TEXT("the mark row's number is the mod's own"), PP::GameRange(Result.PlacementMark));
    const FP4PlacementRow* Rows[3] = {nullptr, nullptr, nullptr};
    for (const FP4PlacementRow& Row : Result.PlacementRows)
    {
        AddInfo(FString::Printf(TEXT("row %s %s %016llx blobs %d"), *Row.Actor, *Row.Class, Row.Id, Row.Blobs.Num()));
        TestFalse(TEXT("a row's number is in the mod's range, never the game's: ") + Row.Actor, PP::GameRange(Row.Id));
        TestFalse(TEXT("every number is the list's once: ") + Row.Actor, Ids.Contains(Row.Id));
        Ids.Add(Row.Id);
        if (Row.Actor == Bulborb->GetName()) Rows[0] = &Row;
        if (Row.Actor == Treasure->GetName()) Rows[1] = &Row;
        if (Row.Actor == Crack->GetName()) Rows[2] = &Row;
    }
    if (!TestTrue(TEXT("each placed object has its row"), Rows[0] && Rows[1] && Rows[2])) return false;
    const AActor* Placed[3] = {Bulborb, Treasure, Crack};
    for (int32 I = 0; I < 3; ++I)
    {
        TestEqual(TEXT("the row names the game's class by path"), Rows[I]->Class, Placed[I]->GetClass()->GetPathName());
        TestTrue(TEXT("the row is where the actor is"), Rows[I]->Transform.Equals(Placed[I]->GetActorTransform(), 0.01f));
    }
    for (const auto& Blob : Rows[0]->Blobs) AddInfo(TEXT("Bulborb blob: ") + Blob.Key);
    for (const auto& Blob : Rows[1]->Blobs) AddInfo(TEXT("treasure blob: ") + Blob.Key);
    for (const auto& Blob : Rows[2]->Blobs) AddInfo(TEXT("pot blob: ") + Blob.Key);
    TestEqual(TEXT("the Bulborb's options were not changed, so its blobs stay empty"), Rows[0]->Blobs.Num(), 0);
    TestEqual(TEXT("the treasure's options were not changed, so its blobs stay empty"), Rows[1]->Blobs.Num(), 0);
    const TArray<uint8>* PotBlob = Rows[2]->Blobs.Find(TEXT("AI.Static"));
    TestTrue(TEXT("only the pot's AI blob is written"), PotBlob && Rows[2]->Blobs.Num() == 1);
    if (PotBlob)
    {
        PP::Context Context;
        for (UClass* C = PotAI->GetClass(); C; C = C->GetSuperClass()) if (C->HasAnyClassFlags(CLASS_Native)) Context.ComponentChain.push_back(TCHAR_TO_UTF8(*C->GetName()));
        PP::Blob Read; std::string Why;
        const bool Decoded = PP::Decode(PP::Slot::AIStatic, Context, std::vector<uint8_t>(PotBlob->GetData(), PotBlob->GetData() + PotBlob->Num()), PP::RowVersion, Read, Why);
        if (TestTrue(TEXT("the pot's blob reads back in the game's format: ") + FString(UTF8_TO_TCHAR(Why.c_str())), Decoded && Read.Exact()))
        {
            const PP::Value* Drops = Read.Root.Find("ObjectAIParameter.DropParameter.DropItemParameter");
            const PP::Value* Last = Drops && !Drops->Items.empty() ? &Drops->Items.back() : nullptr;
            const PP::Value* Spawn = Last ? Last->Find("SpawnMiniInfo") : nullptr;
            if (TestTrue(TEXT("the pot drops what the creator set"), Spawn != nullptr))
            {
                TestEqual(TEXT("the drop is the potato"), FString(UTF8_TO_TCHAR(PP::NameUtf8(*Spawn->Find("DropActor")).c_str())), PotatoClass->GetPathName());
                TestTrue(TEXT("one, always, kept as a generator"), Last->Find("MinNum")->Bits == 1 && Last->Find("MaxNum")->Bits == 1 &&
                    Last->Find("DropRatio")->Float() == 1.f && Last->Find("bRegistGenerator")->Set());
                TestTrue(TEXT("a gimmick's drop keeps the game's none as its number"), Last->Find("UniqueId")->Bits == ~uint64(0));
            }
        }
    }
    const FString Folder = Result.Directory / TEXT("romfs/tsuchi/mods") / Request.Mod.Id;
    const TSharedPtr<FJsonObject> Manifest = ReadJson(Folder / TEXT("mod.json"));
    const TArray<TSharedPtr<FJsonValue>>* Maps = nullptr; const TArray<TSharedPtr<FJsonValue>>* Lists = nullptr;
    const TSharedPtr<FJsonObject>* Entry = nullptr; FString MapName;
    const bool Listed = Manifest.IsValid() && Manifest->TryGetArrayField(TEXT("maps"), Maps) && Maps->Num() == 1 && (*Maps)[0]->TryGetObject(Entry) &&
        (*Entry)->TryGetStringField(TEXT("map"), MapName) && (*Entry)->TryGetArrayField(TEXT("placements"), Lists);
    if (TestTrue(TEXT("mod.json has maps[].placements"), Listed))
    {
        TestEqual(TEXT("the entry is for Cave000_F00"), MapName, FString(Cave000));
        TestTrue(TEXT("it names the list by its package"), Lists->Num() == 1 && (*Lists)[0]->AsString() == List);
        TestFalse(TEXT("there is no level of the mod's own"), (*Entry)->HasField(TEXT("levels")));
    }
    FP4ExportGameInventory Inventory; FP4ExportContainerCensus Census;
    const FString Utoc = Folder / (Names.Container + TEXT("_P.utoc"));
    if (TestTrue(TEXT("the game's inventory and the container are read"), P4ReadExportGameInventory(Request.Tools, Request.GamePaksDirectory, Request.WorkDirectory, Inventory, Error) &&
        P4InspectExportContainer(Request.Tools, Utoc, Request.WorkDirectory, Inventory, Census, Error)))
    {
        TestTrue(TEXT("the list is in the container"), Census.PackageIds.Contains(P4ExportPackageId(List)) && Census.PackageIds.Contains(P4ExportPackageId(Holder)));
        TestEqual(TEXT("no game package is in the container"), Census.GameClaims.Num(), 0);
        TestEqual(TEXT("the container holds exactly what was cooked"), Census.PackageIds.Num(), Result.Packages.Num());
    }
    else AddError(Error);
    TestFalse(TEXT("the list is not left in the project"), FPackageName::DoesPackageExist(List) || FindPackage(nullptr, *List) != nullptr);
    Undo();
    return !HasAnyErrors();
}

// Cave000_F00 imported as a creator imports it: each gameplay object holds what the game's placement list
// holds for it. A Bulborb Larva (GBaby) and a pot (GCrackPotS) are compared with their stock rows field by
// field. Exported again with nothing of theirs changed, the map writes no blob and no value for them.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PlacementOptionsCave000Test,"P4MapKit.Export.PlacementOptionsCave000",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PlacementOptionsCave000Test::RunTest(const FString&)
{
    using namespace P4ExportDriverTest;
    namespace PP = P4MapKit::Placement;
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    if (Root.IsEmpty() || FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS")).IsEmpty())
    { AddWarning(TEXT("P4MapKit test not run. ") TEXT("PlacementOptionsCave000 was not exercised. Set P4_NATIVE_ROOT and P4_NATIVE_ORIGINAL_CONTAINERS.")); return true; }
    FP4ExportModRequest Request; FP4ModNames Names; FString Error;
    if (!P4ExportDriverTest::Request(*this, TEXT("native.options.test"), TEXT("Native Options Test"), Request, Names)) return false;
    AddExpectedError(TEXT("Missing RowStruct while"), EAutomationExpectedErrorFlags::Contains, 0);
    const FString Game = Root / TEXT("host/Saved/P4MapKit/FullNativeCave000/game");
    if (!TestTrue(TEXT("The test's input is there: ") + Game, IFileManager::Get().DirectoryExists(*Game))) return false;
    // The stock rows, read from the game's files as the import reads them.
    TArray<FP4GamePlacementRow> Rows;
    const FString Lists = P4GamePlacementFolder(Game, Cave000);
    if (!TestTrue(TEXT("the map's placement lists are read: ") + Error, !Lists.IsEmpty() && P4ReadGamePlacementLists(Lists, Rows, Error) && Rows.Num() > 0)) return false;
    AddInfo(FString::Printf(TEXT("%d stock rows in %s"), Rows.Num(), *Lists));
    FImported Imported(TEXT("ExportPlacementOptionsCave000"));
    if (!TestTrue(TEXT("the test can keep its import cleanup list"), Imported.Ready)) return false;
    if (!TestFalse(TEXT("The project has no Cave000 before the test"), FPackageName::DoesPackageExist(Cave000))) return false;
    FP4KadanImportSettings ImportSettings; ImportSettings.GameFolder = Game; ImportSettings.GamePaks = Request.GamePaksDirectory; ImportSettings.Retoc = Request.Tools.Retoc;
    ImportSettings.Repak = Request.Tools.Repak;
    const FP4KadanImportResult Import = P4KadanImportMap(ImportSettings, Cave000, true);
    for (const FString& Problem : Import.Problems) AddInfo(TEXT("Not imported. ") + Problem);
    for (const FString& Warning : Import.Warnings) if (Warning.Contains(TEXT("placement"))) AddInfo(TEXT("told: ") + Warning);
    if (!KeepImport(*this, TEXT("PlacementOptionsCave000"), Import)) return false;
    if (!TestTrue(TEXT("the panel's importer brings in Cave000_F00"), FPackageName::DoesPackageExist(Cave000))) return false;
    UWorld* Persistent = UEditorLoadingAndSavingUtils::LoadMap(Cave000);
    if (!TestTrue(TEXT("the map opens in the editor"), Persistent != nullptr)) return false;
    ULevel* Teki = nullptr; ULevel* Objects = nullptr; FString TekiPackage, ObjectsPackage;
    for (ULevelStreaming* Streaming : Persistent->GetStreamingLevels())
    {
        if (!Streaming) continue;
        const FString Package = Streaming->GetWorldAssetPackageName();
        if (Package.EndsWith(TEXT("/Cave000_F00_Teki"))) { Teki = Streaming->GetLoadedLevel(); TekiPackage = Package; }
        if (Package.EndsWith(TEXT("/Cave000_F00_Objects"))) { Objects = Streaming->GetLoadedLevel(); ObjectsPackage = Package; }
    }
    if (!TestTrue(TEXT("the creature and object sublevels are loaded"), Teki && Objects)) return false;
    const auto Find = [&](ULevel* Level, const TCHAR* Class, const FP4GamePlacementRow*& Row) -> AActor*
    {
        for (AActor* Actor : Level->Actors)
            if (Actor && Actor->GetClass()->GetName() == Class && (Row = P4GamePlacementRowOf(Actor, Rows)) != nullptr) return Actor;
        return nullptr;
    };
    const FP4GamePlacementRow* LarvaRow = nullptr; const FP4GamePlacementRow* PotRow = nullptr;
    AActor* Larva = Find(Teki, TEXT("GBaby_C"), LarvaRow);
    AActor* Pot = Find(Objects, TEXT("GCrackPotS_C"), PotRow);
    if (!TestTrue(TEXT("a Bulborb Larva and a pot of the game's, each with its stock row"), Larva && Pot)) return false;
    for (const auto& Pair : {TPair<AActor*, const FP4GamePlacementRow*>(Larva, LarvaRow), TPair<AActor*, const FP4GamePlacementRow*>(Pot, PotRow)})
    {
        TArray<FString> Keys; Pair.Value->Blobs.GetKeys(Keys); Keys.Sort();
        AddInfo(FString::Printf(TEXT("%s row %llu in %s: %s"), *Pair.Key->GetName(), Pair.Value->Id, *Pair.Value->List, *FString::Join(Keys, TEXT(", "))));
        TestTrue(TEXT("the stock row has the AI blob: ") + Pair.Key->GetName(), Pair.Value->Blobs.Contains(TEXT("AI.Static")));
        const TArray<FString> Differences = P4PlacementOptionDifferences(Pair.Key, *Pair.Value);
        for (const FString& Line : Differences) AddInfo(Pair.Key->GetName() + TEXT(": ") + Line);
        TestEqual(TEXT("every option of the stock row is read and the actor holds it as the game has it: ") + Pair.Key->GetName(), Differences.Num(), 0);
    }
    // Two values looked up by hand: the Larva's territory radius and the pot's drop count, from the decoded blobs.
    {
        const auto Component = [](AActor* Actor, const TCHAR* Native) -> UActorComponent*
        {
            TInlineComponentArray<UActorComponent*> All(Actor);
            for (UActorComponent* One : All) for (UClass* C = One ? One->GetClass() : nullptr; C; C = C->GetSuperClass()) if (C->GetName() == Native) return One;
            return nullptr;
        };
        const auto Decoded = [&](AActor* Actor, UActorComponent* AI, const FP4GamePlacementRow& Row, PP::Blob& Out)
        {
            PP::Context Context;
            for (UClass* C = Actor->GetClass(); C; C = C->GetSuperClass()) if (C->HasAnyClassFlags(CLASS_Native)) Context.ActorChain.push_back(TCHAR_TO_UTF8(*C->GetName()));
            for (UClass* C = AI->GetClass(); C; C = C->GetSuperClass()) if (C->HasAnyClassFlags(CLASS_Native)) Context.ComponentChain.push_back(TCHAR_TO_UTF8(*C->GetName()));
            const TArray<uint8>& Bytes = Row.Blobs[TEXT("AI.Static")]; std::string Why;
            return PP::Decode(PP::Slot::AIStatic, Context, std::vector<uint8_t>(Bytes.GetData(), Bytes.GetData() + Bytes.Num()), Row.AssetVersion, Out, Why) && Out.Exact();
        };
        UActorComponent* LarvaAI = Component(Larva, TEXT("TekiAIComponent")); UActorComponent* PotAI = Component(Pot, TEXT("CrackPotAIComponent"));
        PP::Blob LarvaBlob, PotBlob;
        if (TestTrue(TEXT("both AI components are there and their stock blobs read exactly"), LarvaAI && PotAI && Decoded(Larva, LarvaAI, *LarvaRow, LarvaBlob) && Decoded(Pot, PotAI, *PotRow, PotBlob)))
        {
            FStructProperty* Parameter = CastField<FStructProperty>(LarvaAI->GetClass()->FindPropertyByName(TEXT("TekiAIParameter")));
            FStructProperty* Territory = Parameter ? CastField<FStructProperty>(Parameter->Struct->FindPropertyByName(TEXT("Territory"))) : nullptr;
            FFloatProperty* Radius = Territory ? CastField<FFloatProperty>(Territory->Struct->FindPropertyByName(TEXT("Radius"))) : nullptr;
            const PP::Value* Stock = LarvaBlob.Root.Find("TekiAIParameter.Territory");
            if (TestTrue(TEXT("the Larva's territory is where the game has it"), Radius && Stock && Stock->Find("Radius")))
            {
                const float Held = Radius->GetPropertyValue(Radius->ContainerPtrToValuePtr<void>(Territory->ContainerPtrToValuePtr<void>(Parameter->ContainerPtrToValuePtr<void>(LarvaAI))));
                AddInfo(FString::Printf(TEXT("Larva territory radius %g, stock %g"), Held, Stock->Find("Radius")->Float()));
                TestEqual(TEXT("the Larva's territory radius is the stock row's"), Held, Stock->Find("Radius")->Float());
            }
            FStructProperty* PotParameter = CastField<FStructProperty>(PotAI->GetClass()->FindPropertyByName(TEXT("ObjectAIParameter")));
            FStructProperty* Drop = PotParameter ? CastField<FStructProperty>(PotParameter->Struct->FindPropertyByName(TEXT("DropParameter"))) : nullptr;
            FArrayProperty* Items = Drop ? CastField<FArrayProperty>(Drop->Struct->FindPropertyByName(TEXT("DropItemParameter"))) : nullptr;
            const PP::Value* StockItems = PotBlob.Root.Find("ObjectAIParameter.DropParameter.DropItemParameter");
            if (TestTrue(TEXT("the pot's drop list is where the game has it"), Items && StockItems))
            {
                const int32 Held = FScriptArrayHelper(Items, Items->ContainerPtrToValuePtr<void>(Drop->ContainerPtrToValuePtr<void>(PotParameter->ContainerPtrToValuePtr<void>(PotAI)))).Num();
                AddInfo(FString::Printf(TEXT("pot drops %d, stock %d"), Held, int32(StockItems->Items.size())));
                TestEqual(TEXT("the pot drops as many things as the stock row says"), Held, int32(StockItems->Items.size()));
            }
        }
    }
    // Nothing changed: both levels are as imported, so neither object has anything to export.
    UWorld* TekiWorld = CastChecked<UWorld>(Teki->GetOuter()); UWorld* ObjectsWorld = CastChecked<UWorld>(Objects->GetOuter());
    TestTrue(TEXT("the creature sublevel is as imported"), P4AnalyzeOriginalLevelChanges(TekiWorld).Unchanged);
    TestTrue(TEXT("the object sublevel is as imported"), P4AnalyzeOriginalLevelChanges(ObjectsWorld).Unchanged);
    // An export of the map with one change elsewhere (the creature sublevel's kill height), so that it runs.
    AWorldSettings* Settings = Teki->GetWorldSettings(false);
    if (!TestNotNull(TEXT("the creature sublevel's world settings"), Settings)) return false;
    GEditor->BeginTransaction(FText::FromString(TEXT("Kadan placement options test")));
    Settings->Modify(); Settings->KillZ -= 100.0f;
    GEditor->EndTransaction();
    bool Undone = false;
    const auto Undo = [&]() { if (Undone) return; Undone = true; GEditor->UndoTransaction(); Teki->GetOutermost()->SetDirtyFlag(false); };
    ON_SCOPE_EXIT { Undo(); };
    Request.MapPackage = Cave000;
    for (const FString& Package : {TekiPackage, ObjectsPackage}) { FP4ExportRoot One; One.Path = Package; Request.Roots.Add(One); }
    FP4ExportModResult Result;
    const bool Published = P4ExportMod(Request, Result, Error);
    for (const FString& Line : Result.Refused) AddInfo(TEXT("refused: ") + Line);
    if (!TestTrue(TEXT("the export runs"), Published)) { AddError(Error); return false; }
    TestEqual(TEXT("no placement list: no gameplay object was added"), Result.Placements.Num(), 0);
    TestEqual(TEXT("no row and so no blob"), Result.PlacementRows.Num(), 0);
    const FString Folder = Result.Directory / TEXT("romfs/tsuchi/mods") / Request.Mod.Id;
    const TSharedPtr<FJsonObject> Manifest = ReadJson(Folder / TEXT("mod.json"));
    const TArray<TSharedPtr<FJsonValue>>* Maps = nullptr; const TSharedPtr<FJsonObject>* Entry = nullptr; FString ChangesFile;
    if (TestTrue(TEXT("mod.json has the map's changes"), Manifest.IsValid() && Manifest->TryGetArrayField(TEXT("maps"), Maps) && Maps->Num() == 1 &&
        (*Maps)[0]->TryGetObject(Entry) && (*Entry)->TryGetStringField(TEXT("changes"), ChangesFile)))
    {
        TestFalse(TEXT("the map has no placement list"), (*Entry)->HasField(TEXT("placements")));
        const TSharedPtr<FJsonObject> Edits = ReadJson(Folder / ChangesFile);
        const TSharedPtr<FJsonObject>* All = nullptr; const TSharedPtr<FJsonObject>* One = nullptr;
        if (TestTrue(TEXT("the changes file has the creature sublevel"), Edits.IsValid() && Edits->TryGetObjectField(TEXT("levels"), All) &&
            (*All)->TryGetObjectField(TEXT("Cave000_F00_Teki"), One)))
        {
            TestFalse(TEXT("nothing of the object sublevel"), (*All)->HasField(TEXT("Cave000_F00_Objects")));
            const TSharedPtr<FJsonObject>* Set = nullptr;
            TestTrue(TEXT("only the world settings are set"), (*One)->TryGetObjectField(TEXT("set"), Set) && (*Set)->Values.Num() == 1 && (*Set)->HasField(Settings->GetName()));
            TestFalse(TEXT("the Larva is not set, moved or removed"), (Set && (*Set)->HasField(Larva->GetName())) || (*One)->HasField(TEXT("move")) || (*One)->HasField(TEXT("remove")));
        }
    }
    Undo();
    return !HasAnyErrors();
}

// Cave000_F00 as a creator edits it: the game's pot GCrackPotS2 moved 300 cm along X and GCrackPotL2 deleted.
// The game builds both from rows of AP_Cave000_F00_P_Objects, so export writes them as placement_changes of
// that list, by the rows' numbers, the same entry as the hand-made proof mod (run placechanges-02), and no
// level change for them.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4PlacementChangesCave000Test,"P4MapKit.Export.PlacementChangesCave000",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4PlacementChangesCave000Test::RunTest(const FString&)
{
    using namespace P4ExportDriverTest;
    const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    if (Root.IsEmpty() || FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ORIGINAL_CONTAINERS")).IsEmpty())
    { AddWarning(TEXT("P4MapKit test not run. ") TEXT("PlacementChangesCave000 was not exercised. Set P4_NATIVE_ROOT and P4_NATIVE_ORIGINAL_CONTAINERS.")); return true; }
    FP4ExportModRequest Request; FP4ModNames Names; FString Error;
    if (!P4ExportDriverTest::Request(*this, TEXT("kadan.potchanges"), TEXT("Cave000 pot changes"), Request, Names)) return false;
    FString Game = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_CAVE_GAME"));
    if (Game.IsEmpty()) Game = Root / TEXT("host/Saved/P4MapKit/FullNativeCave000/game");
    if (!TestTrue(TEXT("The test's input is there: ") + Game, IFileManager::Get().DirectoryExists(*Game))) return false;
    Request.ExtractedGameDirectory = Game;
    FImported Imported(TEXT("ExportPlacementChangesCave000"));
    if (!TestTrue(TEXT("the test can keep its import cleanup list"), Imported.Ready)) return false;
    if (!TestFalse(TEXT("The project has no Cave000 before the test"), FPackageName::DoesPackageExist(Cave000))) return false;
    FP4KadanImportSettings ImportSettings; ImportSettings.GameFolder = Game; ImportSettings.GamePaks = Request.GamePaksDirectory; ImportSettings.Retoc = Request.Tools.Retoc;
    ImportSettings.Repak = Request.Tools.Repak;
    const FP4KadanImportResult Import = P4KadanImportMap(ImportSettings, Cave000, true);
    for (const FString& Problem : Import.Problems) AddInfo(TEXT("Not imported. ") + Problem);
    if (!KeepImport(*this, TEXT("PlacementChangesCave000"), Import)) return false;
    if (!TestTrue(TEXT("the panel's importer brings in Cave000_F00"), FPackageName::DoesPackageExist(Cave000))) return false;
    UWorld* Persistent = UEditorLoadingAndSavingUtils::LoadMap(Cave000);
    if (!TestTrue(TEXT("the map opens in the editor"), Persistent != nullptr)) return false;
    ULevel* Objects = nullptr; FString ObjectsPackage;
    for (ULevelStreaming* Streaming : Persistent->GetStreamingLevels())
        if (Streaming && Streaming->GetWorldAssetPackageName().EndsWith(TEXT("/Cave000_F00_Objects")))
        { Objects = Streaming->GetLoadedLevel(); ObjectsPackage = Streaming->GetWorldAssetPackageName(); }
    if (!TestTrue(TEXT("the object sublevel is loaded"), Objects != nullptr)) return false;
    AActor* Moved = nullptr; AActor* Removed = nullptr;
    for (AActor* Actor : Objects->Actors)
    {
        if (Actor && Actor->GetName() == TEXT("GCrackPotS2")) Moved = Actor;
        if (Actor && Actor->GetName() == TEXT("GCrackPotL2")) Removed = Actor;
    }
    if (!TestTrue(TEXT("the two pots of the proof are in the object sublevel"), Moved && Removed)) return false;
    AddInfo(TEXT("GCrackPotS2 was at ") + Moved->GetActorLocation().ToString() + TEXT(", GCrackPotL2 at ") + Removed->GetActorLocation().ToString());
    UWorld* ObjectsWorld = CastChecked<UWorld>(Objects->GetOuter());
    {
        GEditor->BeginTransaction(FText::FromString(TEXT("Kadan placement changes test")));
        ON_SCOPE_EXIT { GEditor->EndTransaction(); };
        Moved->Modify(); Moved->SetActorLocation(Moved->GetActorLocation() + FVector(300, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
        if (!TestTrue(TEXT("the editor deletes GCrackPotL2"), Persistent->EditorDestroyActor(Removed, true))) return false;
    }
    bool Undone = false;
    const auto Undo = [&]() { if (Undone) return; Undone = true; GEditor->UndoTransaction(); Objects->GetOutermost()->SetDirtyFlag(false); };
    ON_SCOPE_EXIT { Undo(); };
    const FP4LevelChangesResult Changes = P4AnalyzeOriginalLevelChanges(ObjectsWorld);
    if (!TestTrue(TEXT("the edit reads as one move and one removal"), Changes.IsValid() && Changes.Moved.Num() == 1 && Changes.Removed == TArray<FString>({TEXT("GCrackPotL2")}))) return false;
    const FP4RemovedActor* Gone = Changes.RemovedActors.Find(TEXT("GCrackPotL2"));
    TestTrue(TEXT("the removed pot's placement number is read from the level as imported"), Gone && Gone->PlacementNumber == UINT64_C(17179869268));
    // What the review says, before anything is written.
    {
        TArray<FP4GameRowEdit> Rows; bool Relight = true;
        if (TestTrue(TEXT("the review finds the pots' rows: ") + Error, P4ExportLevelRowEdits(ObjectsWorld, Changes, Game, Rows, Relight, Error)))
        {
            TestEqual(TEXT("both pots are rows of the game's lists"), Rows.Num(), 2);
            TestFalse(TEXT("moving or removing a pot built from its row rebuilds no lighting"), Relight);
        }
        Request.MapPackage = Cave000;
        FP4ExportRoot One; One.Path = ObjectsPackage; Request.Roots.Add(One);
        FP4KadanModReview Review; FString Why;
        if (TestTrue(TEXT("the export review runs: ") + Why, P4KadanReviewMod(Request, Persistent, Review, Why)))
        {
            const FP4ExportSelectionRow* Row = Review.Creator.Rows.FindByPredicate([&](const FP4ExportSelectionRow& R) { return R.Package == ObjectsPackage; });
            if (Row) AddInfo(TEXT("review: ") + Row->Reason);
            TestTrue(TEXT("the review says the pots are changed in the game's placement lists"),
                Row && Row->Reason.Contains(TEXT("1 you moved and 1 you removed are written as changes to their rows")));
        }
    }
    FP4ExportModResult Result;
    const bool Published = P4ExportMod(Request, Result, Error);
    for (const FString& Line : Result.Refused) AddInfo(TEXT("refused: ") + Line);
    for (const FString& Line : Result.Told) AddInfo(TEXT("told: ") + Line);
    if (!TestTrue(TEXT("the panel's export publishes the edit"), Published)) { AddError(Error); return false; }
    AddInfo(TEXT("output: ") + Result.Directory);
    const FString List = TEXT("/Game/Carrot4/Maps/Madori/Cave/Cave000/Cave000_F00/ActorPlacementInfo/AP_Cave000_F00_P_Objects");
    TestEqual(TEXT("two of the game's rows are changed"), Result.GameRowEdits.Num(), 2);
    TestTrue(TEXT("the export tells which row each pot is"), Result.Told.ContainsByPredicate([](const FString& Line)
        { return Line.Contains(TEXT("GCrackPotS2 is moved by a change to row 17179869271")); }) && Result.Told.ContainsByPredicate([](const FString& Line)
        { return Line.Contains(TEXT("GCrackPotL2 is removed by a change to row 17179869268")); }));
    TestEqual(TEXT("no lighting is rebuilt"), Result.RebuiltLighting.Num(), 0);
    const FString Folder = Result.Directory / TEXT("romfs/tsuchi/mods") / Request.Mod.Id;
    const TSharedPtr<FJsonObject> Manifest = ReadJson(Folder / TEXT("mod.json"));
    const TArray<TSharedPtr<FJsonValue>>* Maps = nullptr; const TSharedPtr<FJsonObject>* Entry = nullptr;
    const TSharedPtr<FJsonObject>* All = nullptr; const TSharedPtr<FJsonObject>* Rows = nullptr;
    if (!TestTrue(TEXT("mod.json has maps[].placement_changes for the objects list"), Manifest.IsValid() && Manifest->TryGetArrayField(TEXT("maps"), Maps) &&
        Maps->Num() == 1 && (*Maps)[0]->TryGetObject(Entry) && (*Entry)->TryGetObjectField(TEXT("placement_changes"), All) &&
        (*All)->Values.Num() == 1 && (*All)->TryGetObjectField(List, Rows))) return false;
    FString MapName; (*Entry)->TryGetStringField(TEXT("map"), MapName);
    TestEqual(TEXT("the entry is for Cave000_F00"), MapName, FString(Cave000));
    // The proof mod's entry: row 17179869271 at (121.0039, -49.9124, 0.0001), turned 28.6222, and 17179869268 removed.
    const TSharedPtr<FJsonObject>* Move = nullptr; const TSharedPtr<FJsonObject>* Pot = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Gone2 = nullptr;
    if (TestTrue(TEXT("the moved pot is row 17179869271"), (*Rows)->TryGetObjectField(TEXT("move"), Move) && (*Move)->Values.Num() == 1 &&
        (*Move)->TryGetObjectField(TEXT("17179869271"), Pot)))
    {
        const auto Three = [&](const TCHAR* Name, const FVector& Want)
        {
            const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
            if (!(*Pot)->TryGetArrayField(Name, Values) || Values->Num() != 3) return false;
            const FVector Got((*Values)[0]->AsNumber(), (*Values)[1]->AsNumber(), (*Values)[2]->AsNumber());
            AddInfo(FString::Printf(TEXT("%s %s"), Name, *Got.ToString()));
            return Got.Equals(Want, 0.001f);
        };
        TestTrue(TEXT("its location is the proof's"), Three(TEXT("location"), FVector(121.00393676757812, -49.912433624267578, 0.0000953674316)));
        TestTrue(TEXT("its rotation is the proof's"), Three(TEXT("rotation"), FVector(0, 28.62217903137207, 0)));
        TestTrue(TEXT("its scale is one"), Three(TEXT("scale"), FVector(1, 1, 1)));
    }
    TestTrue(TEXT("the removed pot is row 17179869268, as text"), (*Rows)->TryGetArrayField(TEXT("remove"), Gone2) && Gone2->Num() == 1 &&
        (*Gone2)[0]->Type == EJson::String && (*Gone2)[0]->AsString() == TEXT("17179869268"));
    // No level change names the pots, which the game would not apply to them.
    FString ChangesFile;
    if ((*Entry)->TryGetStringField(TEXT("changes"), ChangesFile))
    {
        FString Text; FFileHelper::LoadFileToString(Text, *(Folder / ChangesFile));
        TestFalse(TEXT("the level changes do not name the pots"), Text.Contains(TEXT("GCrackPot")));
    }
    TestFalse(TEXT("no placement list of the mod's own"), (*Entry)->HasField(TEXT("placements")));
    Undo();
    return !HasAnyErrors();
}
#endif

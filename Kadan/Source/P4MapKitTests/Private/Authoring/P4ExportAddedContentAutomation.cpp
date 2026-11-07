#if P4_MAPKIT_TESTS
#include "P4ExportDriverTestShared.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4AddedContentExport, "P4MapKit.Export.AddedTreasureAndCreature",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4AddedContentExport::RunTest(const FString&)
{
    using namespace P4ExportDriverTest;
    FP4ExportModRequest Request;
    FP4ModNames Names;
    FString Error;
    if (!P4ExportDriverTest::Request(*this, TEXT("native.content.test"), TEXT("Native Content Test"), Request, Names))
        return false;
    const FString NativeRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_ROOT"));
    const FString Repo = FPaths::GetPath(FPaths::GetPath(NativeRoot));
    FString Surface = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_PLACEMENT_GAME"));
    if (Surface.IsEmpty())
        Surface = Repo / TEXT("private/fixtures/surface/game");
    if (!TestTrue(TEXT("real stock parent files are available"), IFileManager::Get().DirectoryExists(*Surface)))
        return false;
    FImported Imported(TEXT("AddedContentExport"));
    if (!Imported.Ready)
        return false;
    FP4KadanImportSettings Settings;
    Settings.GameFolder = Surface;
    Settings.GamePaks = Request.GamePaksDirectory;
    Settings.Retoc = Request.Tools.Retoc;
    Settings.Repak = Request.Tools.Repak;
    struct FKind
    {
        const TCHAR* Parent;
        const TCHAR* Donor;
        const TCHAR* Short;
        EP4AddedContentKind Kind;
    };
    const FKind Kinds[] = {{TEXT("/Game/Carrot4/Placeables/Objects/Otakara/GOtaIchigo"), TEXT("OTAICHIGO"),
                               TEXT("ruby"), EP4AddedContentKind::Treasure},
        {TEXT("/Game/Carrot4/Placeables/Teki/GKochappy"), TEXT("KOCHAPPY"), TEXT("stonehog"),
            EP4AddedContentKind::Creature}};
    // One normal import owns all stock sources before any creator child exists. Later imports
    // reuse Saved/Kadan's extraction cache and must not invalidate a live parent's provenance.
    TSet<FString> Selected{Cave000};
    for (const FKind& Kind : Kinds)
        Selected.Add(Kind.Parent);
    AddExpectedError(TEXT("Missing RowStruct while"), EAutomationExpectedErrorFlags::Contains, 0);
    const auto Import = P4KadanRunImport(Settings, P4KadanPlanImport(Settings, Selected, false), false);
    if (!KeepImport(*this, TEXT("AddedContentAndMap"), Import) ||
        !TestTrue(TEXT("The normal importer supplies the complete map and stock parent closure"),
            Import.Ok && !Import.Cancelled))
        return false;
    struct FOwnContent
    {
        TArray<UObject*> Made;
        ~FOwnContent()
        {
            for (UObject* Asset : Made)
                if (IsValid(Asset))
                {
                    FAssetRegistryModule::AssetDeleted(Asset);
                    Asset->ClearFlags(RF_Public | RF_Standalone);
                    Asset->GetOutermost()->SetDirtyFlag(false);
                }
            P4FixtureRetirement::Roots(Made, Made);
        }
    } Own;
    TArray<FString> ChildPackages;
    UClass* RubyClass = nullptr;
    for (const FKind& Kind : Kinds)
    {
        UBlueprint* Parent = LoadObject<UBlueprint>(nullptr, *ObjectPathOf(Kind.Parent));
        if (!TestTrue(FString(TEXT("The actual stock parent class is available. ")) + Kind.Parent,
                Parent && Parent->GeneratedClass))
            return false;
        const FString Package = Names.PackageRoot / (FString(TEXT("BP_")) + Kind.Short);
        if (FindPackage(nullptr, *Package) || FPackageName::DoesPackageExist(Package))
            return false;
        UBlueprint* Child = FKismetEditorUtilities::CreateBlueprint(Parent->GeneratedClass, CreatePackage(*Package),
            FName(*FPackageName::GetLongPackageAssetName(Package)), BPTYPE_Normal, UBlueprint::StaticClass(),
            UBlueprintGeneratedClass::StaticClass());
        if (!Child)
            return false;
        FKismetEditorUtilities::CompileBlueprint(Child);
        FAssetRegistryModule::AssetCreated(Child);
        Own.Made.Add(Child);
        ChildPackages.Add(Package);
        if (Kind.Kind == EP4AddedContentKind::Treasure)
            RubyClass = Child->GeneratedClass;
        const FString DeclarationPackage = Names.PackageRoot / (FString(TEXT("Kind_")) + Kind.Short);
        UP4AddedContent* Definition = NewObject<UP4AddedContent>(CreatePackage(*DeclarationPackage),
            FName(*FPackageName::GetLongPackageAssetName(DeclarationPackage)), RF_Public | RF_Standalone);
        Own.Made.Add(Definition);
        Definition->Kind = Kind.Kind;
        Definition->ShortName = Kind.Short;
        Definition->From = FName(Kind.Donor);
        Definition->DisplayName = Kind.Kind == EP4AddedContentKind::Treasure ? TEXT("Ruby") : TEXT("Stonehog");
        Definition->ActorClass = Child->GeneratedClass;
        if (Kind.Kind == EP4AddedContentKind::Treasure)
            Definition->Sparklium = 120;
        else
            Definition->Life = 450.f;
        FAssetRegistryModule::AssetCreated(Definition);
        Request.Roots.Add({EP4ExportRootKind::Package, DeclarationPackage});
        TestTrue(TEXT("new declarations use the existing export route"),
            P4ExportAssetAvailability(Definition, Request.Mod).Route == EP4ExportAssetRoute::CreatorContent);
        TSharedPtr<FJsonObject> Declaration;
        if (!P4AddedContentDeclaration(Definition, Names.PackageRoot, Declaration, Error))
        {
            AddError(Error);
            return false;
        }
        TestFalse(TEXT("an untouched donor field is not copied into the manifest"),
            Declaration->HasField(TEXT("carriers")) || Declaration->HasField(TEXT("values")));
        Definition->ShortName = TEXT("BAD NAME");
        TestFalse(TEXT("invalid content names are refused before publication"),
            P4AddedContentDeclaration(Definition, Names.PackageRoot, Declaration, Error));
        Definition->ShortName = Kind.Short;
        const FName Donor = Definition->From;
        Definition->From = TEXT("BAD DONOR");
        TestFalse(TEXT("invalid donor IDs are refused before publication"),
            P4AddedContentDeclaration(Definition, Names.PackageRoot, Declaration, Error));
        Definition->From = Donor;
        if (Definition->Kind == EP4AddedContentKind::Creature)
        {
            for (float BadLife : {0.f, -0.5f, float(MAX_int32)})
            {
                Definition->Life = BadLife;
                TestFalse(TEXT("life outside the runtime contract is refused"),
                    P4AddedContentDeclaration(Definition, Names.PackageRoot, Declaration, Error));
            }
            Definition->Life = 450.f;
        }
    }
    FP4ExportModResult Exported;
    if (!TestTrue(
            TEXT("normal exporter publishes new treasure and creature content"), P4ExportMod(Request, Exported, Error)))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("only the two new Blueprint child packages are shipped"), Exported.Packages.Num(), 2);
    for (const FString& Package : ChildPackages)
        TestTrue(TEXT("creator child class is in the final container"), Exported.Packages.Contains(Package));
    auto Manifest = ReadJson(Exported.Directory / TEXT("romfs/tsuchi/mods/native.content.test/mod.json"));
    if (!Manifest || !Manifest->HasField(TEXT("content")))
        return false;
    auto Content = Manifest->GetObjectField(TEXT("content"));
    TestEqual(TEXT("new treasure value"),
        Content->GetObjectField(TEXT("treasures"))->GetObjectField(TEXT("ruby"))->GetNumberField(TEXT("sparklium")),
        120.0);
    TestEqual(TEXT("new creature life"),
        Content->GetObjectField(TEXT("creatures"))->GetObjectField(TEXT("stonehog"))->GetNumberField(TEXT("life")),
        450.0);
    if (HasAnyErrors())
        return false; // The declaration and actual child package admission must pass first.
    UWorld* Persistent = UEditorLoadingAndSavingUtils::LoadMap(Cave000);
    if (!TestNotNull(TEXT("The normal editor loader opens Cave000 for the new treasure"), Persistent))
        return false;
    ULevel* Area = nullptr;
    FString AreaPackage;
    for (ULevelStreaming* Streaming : Persistent->GetStreamingLevels())
        if (Streaming && Streaming->GetWorldAssetPackageName().EndsWith(TEXT("/Cave000_F00_GameArea")))
        {
            Area = Streaming->GetLoadedLevel();
            AreaPackage = Streaming->GetWorldAssetPackageName();
        }
    if (!TestTrue(TEXT("The original game area is loaded and unchanged before placing Ruby"),
            Area && P4AnalyzeOriginalLevelChanges(Cast<UWorld>(Area->GetOuter())).Unchanged))
        return false;
    FVector Location(-110.f, 120.f, 0.f);
    FHitResult Hit;
    FCollisionQueryParams Query(TEXT("P4RubyPlacement"), true);
    if (Persistent->LineTraceSingleByChannel(
            Hit, Location + FVector(0, 0, 300), Location - FVector(0, 0, 600), ECC_WorldStatic, Query))
        Location = Hit.ImpactPoint;
    const FTransform Transform(FRotator::ZeroRotator, Location);
    FActorSpawnParameters Spawn;
    Spawn.Name = TEXT("P4PlacedRuby");
    Spawn.OverrideLevel = Area;
    Spawn.ObjectFlags = RF_Transactional;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AActor* Ruby = Persistent->SpawnActor(RubyClass, &Transform, Spawn);
    ON_SCOPE_EXIT
    {
        if (IsValid(Ruby))
            Persistent->DestroyActor(Ruby);
        UEditorLoadingAndSavingUtils::NewBlankMap(false);
    };
    if (!TestTrue(TEXT("The creator Ruby child is placed as a gameplay treasure"), Ruby && P4IsGameplayActor(Ruby)))
        return false;
    const FString RubyName = Ruby->GetName(), RubyClassPath = RubyClass->GetPathName();
    Request.MapPackage = Cave000;
    Request.Roots.Add({EP4ExportRootKind::Package, AreaPackage});
    FP4ExportModResult Placed;
    if (!TestTrue(
            TEXT("The same normal exporter publishes the placed Ruby. ") + Error, P4ExportMod(Request, Placed, Error)))
        return false;
    TestEqual(TEXT("Exactly one new treasure placement row is exported"), Placed.PlacementRows.Num(), 1);
    if (Placed.PlacementRows.Num() == 1)
    {
        const FP4PlacementRow& Row = Placed.PlacementRows[0];
        TestEqual(TEXT("The row identifies the actual placed Ruby"), Row.Actor, RubyName);
        TestEqual(TEXT("The row names the shipped creator child class"), Row.Class, RubyClassPath);
        TestTrue(TEXT("The placement retains the editor's Ruby transform"), Row.Transform.Equals(Transform, 0.01f));
        TestFalse(TEXT("The Ruby placement ID belongs to the mod, not the stock generator range"),
            P4MapKit::Placement::GameRange(Row.Id));
    }
    TestEqual(
        TEXT("The treasure is placed through a placement list, with no added creator level"), Placed.Levels.Num(), 0);
    TestEqual(TEXT("Only the two child classes and two creator placement packages ship"), Placed.Packages.Num(), 4);
    for (const FString& Package : ChildPackages)
        TestTrue(TEXT("The placed export retains both new classes"), Placed.Packages.Contains(Package));
    for (const FString& Package : Placed.Packages)
        TestTrue(TEXT("Every shipped package belongs to this mod"),
            Package.StartsWith(Names.PackageRoot + TEXT("/"), ESearchCase::CaseSensitive));
    Manifest = ReadJson(Placed.Directory / TEXT("romfs/tsuchi/mods/native.content.test/mod.json"));
    const TArray<TSharedPtr<FJsonValue>>* Maps = nullptr;
    const TSharedPtr<FJsonObject>* Entry = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Lists = nullptr;
    FString MapName;
    TestTrue(TEXT("The real final manifest loads the treasure list in Cave000"),
        Manifest.IsValid() && Manifest->TryGetArrayField(TEXT("maps"), Maps) && Maps->Num() == 1 &&
            (*Maps)[0]->TryGetObject(Entry) && (*Entry)->TryGetStringField(TEXT("map"), MapName) &&
            MapName == Cave000 && (*Entry)->TryGetArrayField(TEXT("placements"), Lists) && Lists->Num() == 1 &&
            Placed.Placements.Contains((*Lists)[0]->AsString()));
    const TSharedPtr<FJsonObject>* FinalContent = nullptr;
    const TSharedPtr<FJsonObject>* Treasures = nullptr;
    const TSharedPtr<FJsonObject>* Creatures = nullptr;
    TestTrue(TEXT("The final manifest keeps the Ruby and Stonehog declarations"),
        Manifest.IsValid() && Manifest->TryGetObjectField(TEXT("content"), FinalContent) &&
            (*FinalContent)->TryGetObjectField(TEXT("treasures"), Treasures) && (*Treasures)->HasField(TEXT("ruby")) &&
            (*FinalContent)->TryGetObjectField(TEXT("creatures"), Creatures) &&
            (*Creatures)->HasField(TEXT("stonehog")));
    AddInfo(TEXT("Placed Ruby output: ") + Placed.Directory + TEXT("/romfs"));
    AddInfo(FString::Printf(
        TEXT("Ruby transform %.3f,%.3f,%.3f; class %s"), Location.X, Location.Y, Location.Z, *RubyClassPath));
    return !HasAnyErrors();
}
#endif

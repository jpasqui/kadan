#if P4_MAPKIT_TESTS
#include "P4ExportDriverTestShared.h"
#include "P4GameNames.h"
#include "HAL/PlatformStackWalk.h"
#include "Components/InstancedStaticMeshComponent.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FP4EveryMapImportExport, "P4MapKit.Export.EveryMapImportExport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
void FP4EveryMapImportExport::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
    // P4_NATIVE_SWEEP_MAPS (several maps, separated by ';') imports them one after another in this one
    // editor, as a creator imports every map into one project; P4_NATIVE_SWEEP_MAP names one.
    TArray<FString> Maps;
    FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_MAPS")).ParseIntoArray(Maps, TEXT(";"));
    if (!Maps.Num())
        Maps.Add(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_MAP")));
    for (int32 Index = 0; Index < Maps.Num(); ++Index)
    {
        const FString Map = Maps[Index].TrimStartAndEnd();
        // Numbered so the session keeps the given order.
        if (Map.StartsWith(TEXT("/Game/")) && FPackageName::IsValidLongPackageName(Map))
        {
            Names.Add(Maps.Num() > 1
                    ? FString::Printf(TEXT("%03d_%s"), Index, *FPackageName::GetLongPackageAssetName(Map))
                    : FPackageName::GetLongPackageAssetName(Map));
            Commands.Add(Map);
        }
    }
}
bool FP4EveryMapImportExport::RunTest(const FString& Map)
{
    using namespace P4ExportDriverTest;
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit/EveryMap");
    FString Variant = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_VARIANT"));
    if (Variant.IsEmpty())
        Variant = TEXT("day");
    auto Report = MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("map"), Map);
    Report->SetStringField(TEXT("variant"), Variant);
    Report->SetBoolField(TEXT("night_activated"), false);
    Report->SetBoolField(TEXT("hero_activated"), false);
    Report->SetBoolField(TEXT("game_seen"), false);
    Report->SetBoolField(TEXT("imported"), false);
    Report->SetBoolField(TEXT("unchanged_exported"), false);
    Report->SetBoolField(TEXT("edited_exported"), false);
    // P4_NATIVE_SWEEP_KEEP: one project for many maps, as a creator works. What earlier maps imported
    // stays and the import resumes over it; the sweep retries what failed, so each map still reports
    // its own refusals. P4_NATIVE_SWEEP_IMPORT_ONLY: stop once the map is imported, opened and every
    // original layer reads unchanged (the export checks run in a second pass).
    const bool Keep = !FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_KEEP")).IsEmpty();
    const bool ImportOnly = !FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_IMPORT_ONLY")).IsEmpty();
    Report->SetBoolField(TEXT("kept_project"), Keep);
    Report->SetBoolField(TEXT("import_only"), ImportOnly);
    const double Started = FPlatformTime::Seconds();
    Report->SetStringField(TEXT("started_utc"), FDateTime::UtcNow().ToIso8601());
    auto Times = MakeShared<FJsonObject>();
    Report->SetObjectField(TEXT("phase_seconds"), Times);
    // Each map of a session has its own reply file; result.json is the latest one.
    const FString Own = Folder / (FPackageName::GetLongPackageAssetName(Map) + TEXT("_") + Variant + TEXT(".json"));
    const auto Save = [&](const TCHAR* Name)
    {
        Times->SetNumberField(Name, FMath::RoundToDouble((FPlatformTime::Seconds() - Started) * 10.0) / 10.0);
        const FString Text = P4TableManifestJson(Report);
        return IFileManager::Get().MakeDirectory(*Folder, true) &&
            FFileHelper::SaveStringToFile(
                Text, *(Folder / TEXT("result.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) &&
            FFileHelper::SaveStringToFile(Text, *Own, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    };
    // The map's levels with unsaved changes at each phase, so a level an unchanged map leaves dirty is traced to a step.
    auto DirtyLevels = MakeShared<FJsonObject>();
    Report->SetObjectField(TEXT("dirty_levels_by_phase"), DirtyLevels);
    const auto Phase = [&](const TCHAR* Name)
    {
        Report->SetStringField(TEXT("phase"), Name);
        TArray<TSharedPtr<FJsonValue>> Dirty;
        for (TObjectIterator<UWorld> It; It; ++It)
            if (It->GetOutermost()->IsDirty() && It->GetOutermost()->GetName().StartsWith(TEXT("/Game/")))
                Dirty.Add(MakeShared<FJsonValueString>(It->GetOutermost()->GetName()));
        DirtyLevels->SetArrayField(Name, Dirty);
        return Save(Name);
    };
    ON_SCOPE_EXIT
    {
        Save(TEXT("ended"));
    };
    if (!TestTrue(TEXT("The sweep can record its current phase"), Phase(TEXT("preparing"))))
        return false;
    if (!TestTrue(TEXT("The requested layer mode is day, night or Hero"),
            Variant == TEXT("day") || Variant == TEXT("night") || Variant == TEXT("hero")))
        return false;
    TOptional<FImported> Owned;
    if (!Keep)
        Owned.Emplace(TEXT("EveryMapImportExport"));
    ON_SCOPE_EXIT
    {
        if (Keep && GEditor)
        {
            TraceWorldsBeforeMapCleanup();
            UEditorLoadingAndSavingUtils::NewBlankMap(false);
        }
    };
    const bool Preexisting = FPackageName::DoesPackageExist(Map);
    Report->SetBoolField(TEXT("map_preexisting"), Preexisting);
    if (!TestTrue(TEXT("The clean project cleanup list is ready"), Keep || Owned->Ready) ||
        !TestTrue(
            TEXT("The selected map is absent before normal import (a kept project may hold it from an earlier pass)"),
            Keep || !Preexisting))
        return false;
    FP4ExportModRequest Request;
    FP4ModNames ModNames;
    FString Error;
    if (!P4ExportDriverTest::Request(*this, TEXT("native.sweep.test"), TEXT("Native Map Sweep"), Request, ModNames))
        return false;
    FP4KadanImportSettings Settings;
    Settings.GameFolder = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_GAME"));
    Settings.GamePaks = Request.GamePaksDirectory;
    Settings.Retoc = Request.Tools.Retoc;
    Settings.Repak = Request.Tools.Repak;
    if (!TestTrue(
            TEXT("The sweep input source folder exists"), IFileManager::Get().DirectoryExists(*Settings.GameFolder)))
        return false;
    // The export reads the game's placement lists from the same folder, as the panel does.
    Request.ExtractedGameDirectory = Settings.GameFolder;
    if (!Phase(TEXT("importing")))
        return false;
    const FString StockEnemy = TEXT("/Game/Carrot4/Placeables/Teki/GKochappy");
    const FP4KadanImportPlan Plan = P4KadanPlanImport(Settings, TSet<FString>{Map, StockEnemy}, Keep);
    if (!Phase(TEXT("planned")))
        return false;
    // Diagnostic (P4_NATIVE_SWEEP_DIRTY_STACKS=1): the call stack that last marked each map level dirty during the
    // import. An unchanged map should end its import with no level dirty.
    auto DirtiedBy = MakeShared<FJsonObject>();
    const bool WatchDirty = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_DIRTY_STACKS")) == TEXT("1");
    const FDelegateHandle DirtyWatch = !WatchDirty ? FDelegateHandle() : UPackage::PackageMarkedDirtyEvent.AddLambda(
        [DirtiedBy](UPackage* Package, bool)
        {
            if (!Package || !Package->ContainsMap() || (!DirtiedBy->HasField(Package->GetName()) && DirtiedBy->Values.Num() > 16))
                return;
            ANSICHAR Stack[16384] = {};
            FPlatformStackWalk::StackWalkAndDump(Stack, UE_ARRAY_COUNT(Stack), 1);
            DirtiedBy->SetStringField(Package->GetName(), FString(ANSI_TO_TCHAR(Stack)).Left(6000));
        });
    const FP4KadanImportResult Imported = P4KadanRunImport(Settings, Plan, Keep);
    if (WatchDirty)
        UPackage::PackageMarkedDirtyEvent.Remove(DirtyWatch);
    Report->SetObjectField(TEXT("levels_dirtied_by"), DirtiedBy);
    if (Keep)
    {
        // The map's own import report, for the sweep's ledger (Saved/Kadan/import-result.json is the latest run's).
        FFileHelper::SaveStringToFile(Imported.Report,
            *(Folder / (FPackageName::GetLongPackageAssetName(Map) + TEXT("_") + Variant + TEXT(".import.json"))),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        // What this map failed leaves the editor and the disk, so the next map that needs it tries it
        // again and reports its own refusal rather than finding an unfinished package.
        TArray<TSharedPtr<FJsonValue>> Unretired;
        int32 Retired = 0;
        for (const FString& Package : Imported.NotImported)
        {
            if (UPackage* Made = FindPackage(nullptr, *Package))
            {
                if (P4FixtureRetirement::Package(Made))
                    ++Retired;
                else
                {
                    Unretired.Add(MakeShared<FJsonValueString>(Package));
                    continue;
                }
            }
            FString File;
            for (const TCHAR* Extension : {TEXT(".uasset"), TEXT(".umap")})
                if (FPackageName::TryConvertLongPackageNameToFilename(Package, File, Extension) &&
                    IFileManager::Get().FileExists(*File))
                    for (const TCHAR* Part :
                        {TEXT(".uasset"), TEXT(".umap"), TEXT(".uexp"), TEXT(".ubulk"), TEXT(".uptnl")})
                        IFileManager::Get().Delete(*FPaths::ChangeExtension(File, Part), false, true, true);
        }
        if (Retired)
            CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        Report->SetNumberField(TEXT("failed_packages_retired"), Retired);
        Report->SetArrayField(TEXT("failed_packages_not_retired"), Unretired);
    }
    Report->SetBoolField(TEXT("imported"), Imported.Ok && !Imported.Cancelled && FPackageName::DoesPackageExist(Map));
    Report->SetNumberField(TEXT("objects"), Imported.LevelObjects);
    Report->SetNumberField(TEXT("assets"), Imported.Imported.Num());
    Report->SetNumberField(TEXT("missing_assets"), Imported.NotImported.Num());
    Report->SetNumberField(TEXT("reference_only_assets"), Imported.ReferenceOnly.Num());
    TArray<TSharedPtr<FJsonValue>> MissingPackages, ReferencePackages;
    for (const FString& Package : Imported.NotImported)
        MissingPackages.Add(MakeShared<FJsonValueString>(Package));
    for (const FString& Package : Imported.ReferenceOnly)
        ReferencePackages.Add(MakeShared<FJsonValueString>(Package));
    Report->SetArrayField(TEXT("missing_packages"), MissingPackages);
    Report->SetArrayField(TEXT("reference_only_packages"), ReferencePackages);
    int32 Levels = 0;
    TArray<TSharedPtr<FJsonValue>> ImportedLevels;
    for (const auto& Asset : Imported.Imported)
        if (Asset.Value == TEXT("World"))
        {
            ++Levels;
            ImportedLevels.Add(MakeShared<FJsonValueString>(Asset.Key));
        }
    Report->SetArrayField(TEXT("imported_level_packages"), ImportedLevels);
    Report->SetNumberField(TEXT("levels"), Levels);
    TArray<TSharedPtr<FJsonValue>> Problems;
    for (const FString& Problem : Imported.Problems)
        Problems.Add(MakeShared<FJsonValueString>(Problem));
    Report->SetArrayField(TEXT("problems"), Problems);
    if (!Phase(TEXT("import_complete")))
        return false;
    const FString CaptureRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_CAPTURE"));
    UWorld* World = nullptr;
    if (!CaptureRoot.IsEmpty())
    {
        if (!Phase(TEXT("capturing")))
            return false;
        World = FPackageName::DoesPackageExist(Map) ? UEditorLoadingAndSavingUtils::LoadMap(Map) : nullptr;
        Report->SetBoolField(TEXT("capture_root_loaded"), World != nullptr);
        if (!World)
        {
            // A refused persistent level must not conceal surviving chunks. Assemble a
            // transient diagnostic scene, clearly distinguished from an imported root.
            World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
            Report->SetStringField(TEXT("capture_scene"),
                TEXT("incomplete preview of surviving imported day levels; persistent map unavailable"));
            P4SweepCapture::AddSavedDayLevels(World, Map, Imported, Report);
        }
        else
        {
            TSet<FName> Expected;
            if (!TestTrue(TEXT("The captured map activates its selected original variant: ") + Error,
                    P4ActivateOriginalMapEditorLayers(World, Variant, Expected, Error)))
                return false;
            P4HideOriginalCollisionHelpers(World);
            Report->SetStringField(
                TEXT("capture_scene"), TEXT("saved persistent map, selected original editor variant: ") + Variant);
        }
        const bool Captured = P4SweepCapture::Capture(
            World, CaptureRoot / (FPackageName::GetLongPackageAssetName(Map) + TEXT("_") + Variant), Report);
        Report->SetNumberField(TEXT("capture_loaded_levels"), World ? World->GetLevels().Num() : 0);
        if (!Captured)
        {
            Report->SetBoolField(TEXT("imported"), false);
            Save(TEXT("capture_failed"));
            return false;
        }
        if (!Phase(TEXT("captured")))
            return false;
    }
    if (!TestTrue(TEXT("The normal importer brings in the complete selected map"),
            Imported.Ok && !Imported.Cancelled && FPackageName::DoesPackageExist(Map)))
        return false;
    if (!World)
        World = UEditorLoadingAndSavingUtils::LoadMap(Map);
    if (!TestNotNull(TEXT("The imported map opens through the normal editor loader"), World))
        return false;
    Report->SetNumberField(TEXT("loaded_levels"), World->GetLevels().Num());
    TArray<TSharedPtr<FJsonValue>> InitialLoaded;
    for (ULevel* Level : World->GetLevels())
        if (Level)
            InitialLoaded.Add(MakeShared<FJsonValueString>(Level->GetOutermost()->GetName()));
    Report->SetArrayField(TEXT("initial_loaded_level_packages"), InitialLoaded);
    Report->SetArrayField(TEXT("loaded_level_packages"), InitialLoaded);
    if (!Phase(TEXT("loaded")))
        return false;
    Request.MapPackage = Map;
    TSharedPtr<FJsonObject> GraphReply;
    const TSharedPtr<FJsonObject>* Graph = nullptr;
    bool GraphValid = false;
    if (!TestTrue(TEXT("The normal map graph includes all retained original layer identities"),
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(UP4AuthoringTools::NativeWorldStreamingGraph(
                                             World, ModNames.PackageRoot + TEXT("/ExportValidation"))),
                GraphReply) &&
                GraphReply.IsValid() && GraphReply->TryGetBoolField(TEXT("valid"), GraphValid) && GraphValid &&
                GraphReply->TryGetObjectField(TEXT("graph"), Graph)))
        return false;
    TArray<FString> GraphPackages;
    (*Graph)->Values.GetKeys(GraphPackages);
    GraphPackages.Sort();
    TArray<TSharedPtr<FJsonValue>> OriginalLayers;
    for (const FString& Package : GraphPackages)
    {
        Request.Roots.Add({EP4ExportRootKind::Package, Package});
        OriginalLayers.Add(MakeShared<FJsonValueString>(Package));
        UWorld* LevelWorld = LoadObject<UWorld>(nullptr, *ObjectPathOf(Package));
        const FP4LevelChangesResult Start = LevelWorld ? P4AnalyzeOriginalLevelChanges(LevelWorld)
                                                       : FP4LevelChangesResult();
        if (LevelWorld && !Start.Unchanged)
            AddInfo(TEXT("Changes read at the start. ") + P4LevelChangesJson(Start).Left(6000));
        if (!TestTrue(
                TEXT("Every retained original layer starts unchanged. ") + Package, LevelWorld && Start.Unchanged))
            return false;
    }
    Report->SetArrayField(TEXT("original_layer_packages"), OriginalLayers);
    if (ImportOnly)
    {
        if (!HasAnyErrors())
            Phase(TEXT("import_verified"));
        return !HasAnyErrors();
    }
    FP4ExportModResult Unchanged;
    if (!Phase(TEXT("exporting_unchanged")))
        return false;
    const bool UnchangedOk = P4ExportMod(Request, Unchanged, Error);
    Report->SetBoolField(TEXT("unchanged_exported"), UnchangedOk);
    Report->SetStringField(TEXT("unchanged_error"), Error);
    if (!UnchangedOk)
    {
        // What sent the unchanged map down the edit route: packages with unsaved changes, and each layer's comparison.
        TArray<TSharedPtr<FJsonValue>> Dirty;
        for (TObjectIterator<UPackage> It; It; ++It)
            if (It->IsDirty() && !It->HasAnyFlags(RF_Transient) && It->GetName().StartsWith(TEXT("/Game/")) && Dirty.Num() < 40)
                Dirty.Add(MakeShared<FJsonValueString>(It->GetName()));
        Report->SetArrayField(TEXT("unchanged_dirty_packages"), Dirty);
        TArray<TSharedPtr<FJsonValue>> Compared;
        for (const auto& Root : Request.Roots)
            if (UWorld* Layer = LoadObject<UWorld>(nullptr, *ObjectPathOf(Root.Path)))
            {
                const FP4OriginalDiagnostic Diagnostic = P4DiagnoseOriginalAsset(Layer);
                Compared.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%s comparison %d dirty %d %s"), *Root.Path,
                    int32(Diagnostic.Comparison), int32(Layer->GetOutermost()->IsDirty()), *Diagnostic.Error.Left(300))));
            }
        Report->SetArrayField(TEXT("unchanged_layer_comparisons"), Compared);
    }
    if (!Phase(TEXT("unchanged_export_complete")) ||
        !TestTrue(TEXT("The normal exporter handles the unchanged original map. ") + Error, UnchangedOk))
        return false;
    TestEqual(TEXT("An unchanged map ships no game packages"), Unchanged.Packages.Num(), 0);
    // Select only actual retained game layer identities. No scene route is fabricated here;
    // this is editor streaming activation, and the report always keeps game_seen=false.
    const auto LayerMode = [](const FString& Package) -> FString
    {
        const FString Leaf = FPackageName::GetLongPackageAssetName(Package);
        return Leaf.Contains(TEXT("_Hero"), ESearchCase::IgnoreCase) ? TEXT("hero")
            : Leaf.Contains(TEXT("_Night"), ESearchCase::IgnoreCase) ? TEXT("night")
                                                                     : TEXT("day");
    };
    TSet<FString> ActiveLayers;
    if (Variant == TEXT("day"))
    {
        for (ULevel* Level : World->GetLevels())
            if (Level && Level->bIsVisible && LayerMode(Level->GetOutermost()->GetName()) == Variant)
                ActiveLayers.Add(Level->GetOutermost()->GetName());
    }
    else
        for (const FString& Package : GraphPackages)
            if (LayerMode(Package) == Variant)
                ActiveLayers.Add(Package);
    if (!TestTrue(TEXT("The requested variant has retained original layer identities"), ActiveLayers.Num() > 0))
        return false;
    if (Variant != TEXT("day"))
    {
        for (const FString& Package : GraphPackages)
        {
            UWorld* Owner = LoadObject<UWorld>(nullptr, *ObjectPathOf(Package));
            if (!Owner)
                continue;
            for (ULevelStreaming* Streaming : Owner->GetStreamingLevels())
            {
                if (!Streaming)
                    continue;
                const FString Child = Streaming->GetWorldAssetPackageName(), Mode = LayerMode(Child);
                const bool Controlled = Mode != TEXT("day") ||
                    FPackageName::GetLongPackageAssetName(Child).Contains(TEXT("_Day"), ESearchCase::IgnoreCase);
                if (!Controlled)
                    continue;
                const bool Enabled = Mode == Variant;
                Streaming->SetShouldBeLoaded(Enabled);
                Streaming->SetShouldBeVisible(Enabled);
                Streaming->SetShouldBeVisibleInEditor(Enabled);
            }
        }
        World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
    }
    TArray<AActor*> Candidates;
    TArray<TSharedPtr<FJsonValue>> LoadedLayers, VisibleLayers, ActivatedLayers;
    TSet<FString> VisiblePackages;
    for (ULevel* Level : World->GetLevels())
    {
        if (!Level)
            continue;
        const FString Package = Level->GetOutermost()->GetName();
        LoadedLayers.Add(MakeShared<FJsonValueString>(Package));
        if (!Level->bIsVisible)
            continue;
        VisibleLayers.Add(MakeShared<FJsonValueString>(Package));
        VisiblePackages.Add(Package);
        if (!ActiveLayers.Contains(Package))
            continue;
        ActivatedLayers.Add(MakeShared<FJsonValueString>(Package));
        // A child actor is part of the actor whose component spawned it: a creator moves or removes that actor.
        for (AActor* Actor : Level->Actors)
            if (Actor && Actor->GetRootComponent() && !Actor->IsPendingKill() && !Actor->IsA<AWorldSettings>() &&
                !Actor->IsChildActor())
                Candidates.Add(Actor);
    }
    Report->SetArrayField(TEXT("loaded_level_packages"), LoadedLayers);
    Report->SetArrayField(TEXT("visible_level_packages"), VisibleLayers);
    Report->SetNumberField(TEXT("loaded_levels"), LoadedLayers.Num());
    Report->SetArrayField(TEXT("activated_layer_packages"), ActivatedLayers);
    bool Activated = true;
    for (const FString& Package : ActiveLayers)
        Activated &= VisiblePackages.Contains(Package);
    Report->SetBoolField(TEXT("variant_activated"), Activated);
    Report->SetBoolField(TEXT("night_activated"), Activated && Variant == TEXT("night"));
    Report->SetBoolField(TEXT("hero_activated"), Activated && Variant == TEXT("hero"));
    if (!Phase(TEXT("variant_activation")) ||
        !TestTrue(TEXT("The requested actual variant layers are loaded and visible"), Activated))
        return false;
    // Prefer gameplay actors so a regular movement/removal can be checked without an unnecessary
    // scenery rebake. Maps with only scenery still exercise the normal rebake export path.
    Candidates.Sort(
        [](const AActor& A, const AActor& B)
        {
            const bool First = P4IsGameplayActor(&A), Second = P4IsGameplayActor(&B);
            return First != Second ? First : A.GetPathName() < B.GetPathName();
        });
    if (!TestTrue(TEXT("The map has two original actors for movement and removal"), Candidates.Num() >= 2))
        return false;
    UClass* EnemyClass = nullptr;
    for (AActor* Actor : Candidates)
    {
        TInlineComponentArray<UActorComponent*> Parts(Actor);
        for (UActorComponent* Part : Parts)
            for (UClass* Class = Part ? Part->GetClass() : nullptr; Class; Class = Class->GetSuperClass())
                if (Class->GetName() == TEXT("TekiAIComponent") && P4PlacementIssue(Actor).IsEmpty())
                {
                    EnemyClass = Actor->GetClass();
                    break;
                }
        if (EnemyClass)
            break;
    }
    if (!EnemyClass)
    {
        EnemyClass = LoadObject<UClass>(nullptr, *(ObjectPathOf(StockEnemy) + TEXT("_C")));
    }
    if (!TestNotNull(TEXT("A real stock enemy class is available"), EnemyClass))
        return false;
    AActor* Moved = Candidates[0];
    AActor* Removed = Candidates[1];
    // A game run can ask for two named pieces it can show (P4_NATIVE_SWEEP_MOVE, P4_NATIVE_SWEEP_REMOVE).
    const FString MoveName = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_MOVE"));
    const FString RemoveName = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_REMOVE"));
    for (ULevel* Level : World->GetLevels())
        if (Level)
            for (AActor* Actor : Level->Actors)
            {
                if (Actor && !MoveName.IsEmpty() && Actor->GetName() == MoveName)
                    Moved = Actor;
                if (Actor && !RemoveName.IsEmpty() && Actor->GetName() == RemoveName)
                    Removed = Actor;
            }
    // Or the two pieces of a class (P4_NATIVE_SWEEP_NEAR_CLASS, a class name) nearest a point (P4_NATIVE_SWEEP_NEAR, "x,y,z").
    {
        TArray<FString> Parts;
        FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_NEAR")).ParseIntoArray(Parts, TEXT(","));
        const FString NearClass = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_NEAR_CLASS"));
        if (Parts.Num() == 3 && !NearClass.IsEmpty())
        {
            const FVector Point(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]));
            TArray<AActor*> Near;
            for (ULevel* Level : World->GetLevels())
                if (Level && Level->bIsVisible)
                    for (AActor* Actor : Level->Actors)
                    {
                        bool Kind = false;
                        for (const UClass* Class = Actor ? Actor->GetClass() : nullptr; Class && !Kind;
                            Class = Class->GetSuperClass())
                            Kind = Class->GetName() == NearClass;
                        if (Kind && !Actor->IsPendingKill() && Actor->GetAttachParentActor() == nullptr)
                            Near.Add(Actor);
                    }
            Near.Sort(
                [&](const AActor& A, const AActor& B)
                {
                    return FVector::DistSquared(A.GetActorLocation(), Point) <
                        FVector::DistSquared(B.GetActorLocation(), Point);
                });
            if (!TestTrue(TEXT("Two pieces of the asked class are near the asked point"), Near.Num() >= 2))
                return false;
            Moved = Near[0];
            Removed = Near[1];
        }
    }
    if (!TestTrue(TEXT("The asked pieces are in the map"),
            (MoveName.IsEmpty() || Moved->GetName() == MoveName) &&
                (RemoveName.IsEmpty() || Removed->GetName() == RemoveName) && Moved != Removed))
        return false;
    const bool PlaceEnemy = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_NO_ENEMY")).IsEmpty();
    FVector Offset(125, 0, 0);
    {
        TArray<FString> Parts;
        FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_MOVE_BY")).ParseIntoArray(Parts, TEXT(","));
        if (Parts.Num() == 3)
            Offset = FVector(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]));
    }
    const FVector Before = Moved->GetActorLocation();
    Report->SetStringField(TEXT("moved_from"), Before.ToString());
    Report->SetStringField(TEXT("moved_to"), (Before + Offset).ToString());
    Report->SetStringField(TEXT("removed_at"), Removed->GetActorLocation().ToString());
    const FString MovedName = Moved->GetName(), RemovedName = Removed->GetName();
    Report->SetStringField(TEXT("moved_actor"), Moved->GetPathName());
    Report->SetStringField(TEXT("removed_actor"), Removed->GetPathName());
    const FString MovedLevel = FPackageName::GetLongPackageAssetName(Moved->GetLevel()->GetOutermost()->GetName());
    const FString RemovedLevel = FPackageName::GetLongPackageAssetName(Removed->GetLevel()->GetOutermost()->GetName());
    Request.Roots = {{EP4ExportRootKind::Package, Moved->GetLevel()->GetOutermost()->GetName()}};
    if (Moved->GetLevel() != Removed->GetLevel())
        Request.Roots.Add({EP4ExportRootKind::Package, Removed->GetLevel()->GetOutermost()->GetName()});
    {
        GEditor->BeginTransaction(FText::FromString(TEXT("Kadan map sweep edits")));
        ON_SCOPE_EXIT
        {
            GEditor->EndTransaction();
        };
        Moved->Modify();
        Moved->SetActorLocation(Before + Offset, false, nullptr, ETeleportType::TeleportPhysics);
        if (!TestTrue(TEXT("The editor removes the selected original actor"), World->EditorDestroyActor(Removed, true)))
            return false;
        if (PlaceEnemy)
        {
            FActorSpawnParameters Spawn;
            Spawn.Name = TEXT("P4SweepEnemy");
            Spawn.OverrideLevel = Moved->GetLevel();
            Spawn.ObjectFlags = RF_Transactional;
            const FTransform At(FRotator::ZeroRotator, Before + FVector(0, 200, 0));
            AActor* Added = World->SpawnActor(EnemyClass, &At, Spawn);
            if (!TestNotNull(TEXT("The editor places a real stock enemy"), Added))
                return false;
        }
    }
    Report->SetStringField(TEXT("placed_enemy_class"), EnemyClass->GetPathName());
    if (!Phase(TEXT("exporting_edits")))
        return false;
    FP4ExportModResult Edited;
    const bool EditedOk = P4ExportMod(Request, Edited, Error);
    Report->SetBoolField(TEXT("edited_exported"), EditedOk);
    Report->SetStringField(TEXT("edited_error"), Error);
    Report->SetNumberField(TEXT("placement_rows"), Edited.PlacementRows.Num());
    if (!Phase(TEXT("edited_export_complete")) ||
        !TestTrue(TEXT("The normal exporter publishes movement removal and enemy placement. ") + Error, EditedOk))
        return false;
    TestEqual(
        TEXT("The edited map contains one added enemy placement row"), Edited.PlacementRows.Num(), PlaceEnemy ? 1 : 0);
    const auto Manifest = ReadJson(Edited.Directory / TEXT("romfs/tsuchi/mods/native.sweep.test/mod.json"));
    const TArray<TSharedPtr<FJsonValue>>* Maps = nullptr;
    if (!TestTrue(TEXT("The final manifest contains the selected map"),
            Manifest && Manifest->TryGetArrayField(TEXT("maps"), Maps) && Maps->Num() == 1))
        return false;
    const auto Entry = (*Maps)[0]->AsObject();
    FString Changes;
    if (!TestTrue(TEXT("The manifest names placements"), Entry && (!PlaceEnemy || Entry->HasField(TEXT("placements")))))
        return false;
    // A gameplay object the game builds from a row of its placement lists is moved or removed through that row;
    // anything else through the level changes file.
    const FP4GameRowEdit* MovedRow = Edited.GameRowEdits.FindByPredicate(
        [&](const FP4GameRowEdit& Row) { return Row.Actor == MovedName && !Row.Removed; });
    const FP4GameRowEdit* RemovedRow = Edited.GameRowEdits.FindByPredicate(
        [&](const FP4GameRowEdit& Row) { return Row.Actor == RemovedName && Row.Removed; });
    Report->SetNumberField(TEXT("game_row_edits"), Edited.GameRowEdits.Num());
    const TSharedPtr<FJsonObject>* RowLists = nullptr;
    if (MovedRow || RemovedRow)
        TestTrue(TEXT("The manifest names the game's placement rows it changes"),
            Entry->TryGetObjectField(TEXT("placement_changes"), RowLists));
    const auto RowListed = [&](const FP4GameRowEdit* Row)
    {
        const TSharedPtr<FJsonObject>* One = nullptr;
        const TSharedPtr<FJsonObject>* Moves = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Gone = nullptr;
        if (!RowLists || !(*RowLists)->TryGetObjectField(Row->List, One))
            return false;
        const FString Id = FString::Printf(TEXT("%llu"), Row->Id);
        if (!Row->Removed)
            return (*One)->TryGetObjectField(TEXT("move"), Moves) && (*Moves)->HasField(Id);
        bool Found = false;
        if ((*One)->TryGetArrayField(TEXT("remove"), Gone))
            for (const auto& Value : *Gone)
                Found |= Value->AsString() == Id;
        return Found;
    };
    TSharedPtr<FJsonObject> Changed;
    const TSharedPtr<FJsonObject>* LevelEdits = nullptr;
    if (!MovedRow || !RemovedRow)
    {
        if (!TestTrue(TEXT("The manifest names level changes"), Entry->TryGetStringField(TEXT("changes"), Changes)))
            return false;
        Changed = ReadJson(Edited.Directory / TEXT("romfs/tsuchi/mods/native.sweep.test") / Changes);
        if (!TestTrue(TEXT("The final changes list levels"),
                Changed && Changed->TryGetObjectField(TEXT("levels"), LevelEdits)))
            return false;
    }
    if (MovedRow)
        TestTrue(TEXT("The final placement changes move the selected original object's row"), RowListed(MovedRow));
    else
    {
        const TSharedPtr<FJsonObject>* MoveLevel = nullptr;
        const TSharedPtr<FJsonObject>* Moves = nullptr;
        TestTrue(TEXT("The final changes move the selected original actor"),
            (*LevelEdits)->TryGetObjectField(MovedLevel, MoveLevel) &&
                (*MoveLevel)->TryGetObjectField(TEXT("move"), Moves) && (*Moves)->HasField(MovedName));
    }
    if (RemovedRow)
        TestTrue(TEXT("The final placement changes remove the selected original object's row"), RowListed(RemovedRow));
    else
    {
        const TSharedPtr<FJsonObject>* RemoveLevel = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* RemovedActors = nullptr;
        bool FoundRemoved = false;
        if ((*LevelEdits)->TryGetObjectField(RemovedLevel, RemoveLevel) &&
            (*RemoveLevel)->TryGetArrayField(TEXT("remove"), RemovedActors))
            for (const auto& Actor : *RemovedActors)
                FoundRemoved |= Actor->AsString() == RemovedName;
        TestTrue(TEXT("The final changes remove the selected original actor"), FoundRemoved);
    }
    if (!HasAnyErrors())
        Phase(TEXT("editor_verified"));
    return !HasAnyErrors();
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FP4EveryMapWithGameReferencesImportExport,
    "P4MapKit.Export.EveryMapWithGameReferencesImportExport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
void FP4EveryMapWithGameReferencesImportExport::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
    TArray<FString> Maps;
    FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_MAPS")).ParseIntoArray(Maps, TEXT(";"));
    if (!Maps.Num())
        Maps.Add(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_MAP")));
    for (int32 Index = 0; Index < Maps.Num(); ++Index)
    {
        const FString Map = Maps[Index].TrimStartAndEnd();
        if (Map.StartsWith(TEXT("/Game/")) && FPackageName::IsValidLongPackageName(Map))
        {
            Names.Add(FString::Printf(TEXT("%03d_%s"), Index, *FPackageName::GetShortName(Map)));
            Commands.Add(Map);
        }
    }
}
bool FP4EveryMapWithGameReferencesImportExport::RunTest(const FString& Map)
{
    using namespace P4ExportDriverTest;
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("P4MapKit/EveryMap");
    FString Variant = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_VARIANT"));
    if (Variant.IsEmpty())
        Variant = TEXT("day");
    const bool Keep = !FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_KEEP")).IsEmpty();
    const bool ImportOnly = !FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_IMPORT_ONLY")).IsEmpty();
    const bool Fresh = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_FRESH_WORLD")) == TEXT("1");
    const bool RequireNative = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_REQUIRE_NATIVE")) ==
        TEXT("1");
    const bool Cube = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_CREATOR_CUBE")) == TEXT("1");
    const bool RequireGeometry = Fresh ||
        FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_REQUIRE_GEOMETRY")) == TEXT("1");
    auto Report = MakeShared<FJsonObject>();
    auto Times = MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("map"), Map);
    Report->SetStringField(TEXT("variant"), Variant);
    Report->SetStringField(TEXT("qualification"), TEXT("original-game-overlay-with-supported-editor-content"));
    Report->SetBoolField(TEXT("game_seen"), false);
    Report->SetBoolField(TEXT("kept_project"), Keep);
    Report->SetBoolField(TEXT("imported"), false);
    Report->SetBoolField(TEXT("unchanged_exported"), false);
    Report->SetBoolField(TEXT("edited_exported"), false);
    Report->SetObjectField(TEXT("phase_seconds"), Times);
    const double Started = FPlatformTime::Seconds();
    const FString Own = Folder / (FPackageName::GetShortName(Map) + TEXT("_") + Variant + TEXT(".json"));
    const auto Save = [&](const TCHAR* Phase)
    {
        Times->SetNumberField(Phase, FPlatformTime::Seconds() - Started);
        const FString Json = P4TableManifestJson(Report);
        return IFileManager::Get().MakeDirectory(*Folder, true) &&
            FFileHelper::SaveStringToFile(Json, *Own, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) &&
            FFileHelper::SaveStringToFile(
                Json, *(Folder / TEXT("result.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    };
    // The map's levels with unsaved changes at each phase, so a level an unchanged map leaves dirty is traced to a step.
    auto DirtyLevels = MakeShared<FJsonObject>();
    Report->SetObjectField(TEXT("dirty_levels_by_phase"), DirtyLevels);
    const auto Phase = [&](const TCHAR* Name)
    {
        Report->SetStringField(TEXT("phase"), Name);
        TArray<TSharedPtr<FJsonValue>> Dirty;
        for (TObjectIterator<UWorld> It; It; ++It)
            if (It->GetOutermost()->IsDirty() && It->GetOutermost()->GetName().StartsWith(TEXT("/Game/")))
                Dirty.Add(MakeShared<FJsonValueString>(It->GetOutermost()->GetName()));
        DirtyLevels->SetArrayField(Name, Dirty);
        return Save(Name);
    };
    ON_SCOPE_EXIT
    {
        Save(TEXT("ended"));
    };
    if (!TestTrue(TEXT("Map overlay qualification receipt is writable"), Phase(TEXT("preparing"))))
        return false;
    if (!TestTrue(TEXT("Requested original layer variant is valid"),
            Variant == TEXT("day") || Variant == TEXT("night") || Variant == TEXT("hero")))
        return false;
    TOptional<FImported> Owned;
    if (!Keep)
        Owned.Emplace(TEXT("EveryMapWithGameReferences"));
    ON_SCOPE_EXIT
    {
        if (Keep && GEditor)
        {
            TraceWorldsBeforeMapCleanup();
            UEditorLoadingAndSavingUtils::NewBlankMap(false);
        }
    };
    Report->SetBoolField(TEXT("map_preexisting"), FPackageName::DoesPackageExist(Map));
    Report->SetBoolField(TEXT("fresh_world_required"), Fresh);
    if (Fresh &&
        !TestFalse(TEXT("Fresh-map qualification refuses any saved or loaded previous persistent map"),
            FPackageName::DoesPackageExist(Map) || FindPackage(nullptr, *Map)))
        return false;
    if (!TestTrue(TEXT("Isolated fixture owns cleanup or keeps the existing qualified project"), Keep || Owned->Ready))
        return false;
    FP4ExportModRequest Request;
    FP4ModNames ModNames;
    FString Error;
    if (!P4ExportDriverTest::Request(
            *this, TEXT("native.sweep.references"), TEXT("Native Game Reference Map Sweep"), Request, ModNames))
        return false;
    FP4KadanImportSettings Settings;
    Settings.GameFolder = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_GAME"));
    Settings.GamePaks = Request.GamePaksDirectory;
    Settings.Retoc = Request.Tools.Retoc;
    Settings.Repak = Request.Tools.Repak;
    Settings.ConvertedCache = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_CONVERTED_CACHE"));
    Settings.AllowGameBackedLevels = !RequireNative;
    Report->SetBoolField(TEXT("require_native_levels"), RequireNative);
    Request.ExtractedGameDirectory = Settings.GameFolder;
    Request.MapPackage = Map;
    if (!TestTrue(
            TEXT("Retained actual source folder exists"), IFileManager::Get().DirectoryExists(*Settings.GameFolder)) ||
        !Phase(TEXT("importing")))
        return false;
    // P4_NATIVE_SWEEP_REIMPORT=1: in a kept project, import the map again as the Reimport button does
    // (unchanged game assets made again by this build) instead of resuming over what is there.
    const bool Reimport = Keep && FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_REIMPORT")) == TEXT("1");
    Report->SetBoolField(TEXT("reimport"), Reimport);
    const auto Plan = P4KadanPlanImport(Settings, TSet<FString>{Map}, Keep && !Reimport);
    if (Fresh)
        for (const auto& Asset : Plan.Assets)
            if (Asset->Kind == TEXT("World"))
                if (!TestFalse(
                        TEXT("Every selected original layer is newly constructed, never resumed: ") + Asset->Package,
                        FPackageName::DoesPackageExist(Asset->Package) || FindPackage(nullptr, *Asset->Package)))
                    return false;
    if (!Phase(TEXT("planned")))
        return false;
    const auto Imported = P4KadanRunImport(Settings, Plan, Keep && !Reimport);
    // A game map can stream a level the game does not ship: Area003_P names the bake level
    // /Game/Test/Maps/Area/Area010_NotCook/DES_Area003_BakeLights_Day. The game finds nothing there. The
    // editor, opening the map (the import's own open check, and this test), logs that it cannot find the
    // package and drops the entry, and the level's change analysis tells that entry as the game's. The
    // imported level keeps the game's entry, so this one engine message is taken back, for exactly the
    // packages the import reports as named by a soft reference of one of the map's levels and in neither
    // the game nor the project.
    TArray<FString> Unshipped;
    {
        TSet<FName> Levels;
        for (const auto& Asset : Plan.Assets)
            if (Asset.IsValid() && Asset->Kind == TEXT("World"))
                Levels.Add(FName(*Asset->Package));
        const FString Names = TEXT(" names "),
                      Soft = TEXT(
                          " by a soft reference, and that package is in neither the game folder nor the project");
        for (const FString& Warning : Imported.Warnings)
        {
            const int32 At = Warning.Find(Names, ESearchCase::CaseSensitive),
                        SoftAt = Warning.Find(Soft, ESearchCase::CaseSensitive);
            if (At == INDEX_NONE || SoftAt == INDEX_NONE || SoftAt < At || !Levels.Contains(FName(*Warning.Left(At))))
                continue;
            const FString Package = Warning.Mid(At + Names.Len(), SoftAt - At - Names.Len());
            if (FPackageName::IsValidLongPackageName(Package) && !FPackageName::DoesPackageExist(Package))
                Unshipped.AddUnique(Package);
        }
        Unshipped.Sort();
        TArray<TSharedPtr<FJsonValue>> Listed;
        for (const FString& Package : Unshipped)
            Listed.Add(MakeShared<FJsonValueString>(Package));
        Report->SetArrayField(TEXT("unshipped_streams"), Listed);
    }
    const auto TakeBackUnshipped = [this, &Unshipped]()
    {
        for (const FString& Package : Unshipped)
        {
            const FString Said = TEXT("Couldn't find file for package ") + Package + TEXT(".");
            ExecutionInfo.RemoveAllEvents(
                [&Said](FAutomationEvent& Event)
                {
                    return Event.Type == EAutomationEventType::Error &&
                        Event.Message.Contains(Said, ESearchCase::CaseSensitive);
                });
        }
    };
    TakeBackUnshipped();
    ON_SCOPE_EXIT
    {
        TakeBackUnshipped();
    };
    if (!TestTrue(TEXT("Actual normal import receipt is retained"),
            FFileHelper::SaveStringToFile(Imported.Report,
                *(Folder / (FPackageName::GetShortName(Map) + TEXT("_") + Variant + TEXT(".import.json"))),
                FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)))
        return false;
    Report->SetBoolField(TEXT("imported"), Imported.Ok && !Imported.Cancelled && FPackageName::DoesPackageExist(Map));
    Report->SetNumberField(TEXT("objects"), Imported.LevelObjects);
    Report->SetNumberField(TEXT("assets"), Imported.Imported.Num());
    Report->SetNumberField(TEXT("missing_assets"), Imported.NotImported.Num());
    Report->SetNumberField(TEXT("reference_only_assets"), Imported.ReferenceOnly.Num());
    TArray<TSharedPtr<FJsonValue>> Missing, References, Problems;
    for (const FString& Name : Imported.NotImported)
        Missing.Add(MakeShared<FJsonValueString>(Name));
    for (const FString& Name : Imported.ReferenceOnly)
        References.Add(MakeShared<FJsonValueString>(Name));
    for (const FString& Text : Imported.Problems)
        Problems.Add(MakeShared<FJsonValueString>(Text));
    Report->SetArrayField(TEXT("missing_packages"), Missing);
    Report->SetArrayField(TEXT("reference_only_packages"), References);
    Report->SetArrayField(TEXT("problems"), Problems);
    if (!Phase(TEXT("import_complete")))
        return false;
    UWorld* World = nullptr;
    const FString CaptureRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_CAPTURE"));
    if (!CaptureRoot.IsEmpty())
    {
        if (!Phase(TEXT("capturing")))
            return false;
        World = FPackageName::DoesPackageExist(Map) ? UEditorLoadingAndSavingUtils::LoadMap(Map) : nullptr;
        Report->SetBoolField(TEXT("capture_root_loaded"), World != nullptr);
        if (!World)
        {
            World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
            Report->SetStringField(TEXT("capture_scene"),
                TEXT("incomplete preview of surviving imported day levels; persistent map unavailable"));
            P4SweepCapture::AddSavedDayLevels(World, Map, Imported, Report);
        }
        else
        {
            TSet<FName> Expected;
            if (!TestTrue(TEXT("The captured projection activates its selected original variant: ") + Error,
                    P4ActivateOriginalMapEditorLayers(World, Variant, Expected, Error)))
                return false;
            P4HideOriginalCollisionHelpers(World);
            Report->SetStringField(TEXT("capture_scene"),
                TEXT("saved supported projection, selected original editor variant: ") + Variant);
        }
        const bool Captured = P4SweepCapture::Capture(
            World, CaptureRoot / (FPackageName::GetShortName(Map) + TEXT("_") + Variant), Report);
        Report->SetNumberField(TEXT("capture_loaded_levels"), World ? World->GetLevels().Num() : 0);
        if (!Captured)
        {
            Report->SetBoolField(TEXT("imported"), false);
            Save(TEXT("capture_failed"));
            return false;
        }
        if (!Phase(TEXT("captured")))
            return false;
    }
    if (!TestTrue(TEXT("Normal caller preserves the complete selected original map identity"),
            Imported.Ok && !Imported.Cancelled && FPackageName::DoesPackageExist(Map)))
        return false;
    if (!World)
        World = UEditorLoadingAndSavingUtils::LoadMap(Map);
    if (!TestNotNull(TEXT("Normal editor loader opens the supported projection"), World))
        return false;
    TSharedPtr<FJsonObject> Reply;
    const TSharedPtr<FJsonObject>* Graph = nullptr;
    bool Valid = false;
    if (!TestTrue(TEXT("Normal graph retains every source layer identity"),
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(UP4AuthoringTools::NativeWorldStreamingGraph(
                                             World, ModNames.PackageRoot / TEXT("Validation"))),
                Reply) &&
                Reply.IsValid() && Reply->TryGetBoolField(TEXT("valid"), Valid) && Valid &&
                Reply->TryGetObjectField(TEXT("graph"), Graph)))
        return false;
    TArray<FString> Layers;
    (*Graph)->Values.GetKeys(Layers);
    Layers.Sort();
    TArray<TSharedPtr<FJsonValue>> LayerValues;
    int32 Partial = 0, Editable = 0, OmittedCount = 0, QualifiedBounds = 0;
    for (const FString& Package : Layers)
    {
        UWorld* Layer = LoadObject<UWorld>(nullptr, *ObjectPathOf(Package));
        if (!TestNotNull(TEXT("Original streaming layer remains available: ") + Package, Layer))
            return false;
        if (Fresh)
        {
            FP4OriginalLevelLighting SourceLighting;
            FString LightingError;
            const bool LightingRead = P4ReadOriginalLevelLighting(Layer, SourceLighting, LightingError);
            if (!TestTrue(TEXT("Fresh full/partial layer retains actual source lighting identities: ") + Package +
                        TEXT(" ") + LightingError,
                    LightingRead))
                return false;
        }
        const auto Changes = P4AnalyzeOriginalLevelChanges(Layer);
        if (!Changes.IsValid() || !Changes.Unchanged)
        {
            const FString Delta = P4LevelChangesJson(Changes);
            Report->SetStringField(TEXT("initial_changed_layer"), Package);
            Report->SetStringField(TEXT("initial_layer_delta"), Delta);
            AddInfo(TEXT("Original projection changed after ordinary map opening: ") + Package + TEXT(" ") +
                Delta.Left(12000));
        }
        if (!TestTrue(TEXT("Original layer projection starts qualified and unchanged: ") + Package,
                Changes.IsValid() && Changes.Unchanged))
            return false;
        const auto Diagnostic = P4DiagnoseOriginalAsset(Layer);
        if (!Diagnostic.Error.IsEmpty() || Diagnostic.Comparison != EP4OriginalComparison::Equal)
        {
            Report->SetStringField(TEXT("initial_changed_layer"), Package);
            auto Detail = MakeShared<FJsonObject>();
            Detail->SetStringField(TEXT("error"), Diagnostic.Error);
            Detail->SetStringField(TEXT("current_digest"), Diagnostic.CurrentDigest);
            Detail->SetStringField(TEXT("recorded_digest"), Diagnostic.Provenance.Record.ComparisonDigest);
            Detail->SetStringField(TEXT("schema"), Diagnostic.Provenance.Record.ComparisonSchema);
            Detail->SetStringField(TEXT("first_changes_digest"), Changes.State);
            Detail->SetNumberField(TEXT("comparison"), int32(Diagnostic.Comparison));
            Report->SetObjectField(TEXT("initial_layer_diagnostic"), Detail);
            Report->SetStringField(
                TEXT("initial_layer_delta_after_diagnostic"), P4LevelChangesJson(P4AnalyzeOriginalLevelChanges(Layer)));
        }
        if (!TestTrue(TEXT("Original layer record matches actual editor state: ") + Package,
                Diagnostic.Error.IsEmpty() && Diagnostic.Comparison == EP4OriginalComparison::Equal))
            return false;
        TSet<FName> Omitted;
        FString Source, CustodyError;
        if (P4ReadGameBackedLevelCustody(Layer, Omitted, Source, CustodyError))
        {
            ++Partial;
            OmittedCount += Omitted.Num();
        }
        else if (!TestTrue(TEXT("A complete layer has no broken partial custody"),
                     CustodyError == TEXT("level-is-not-game-backed")))
            return false;
        TSet<FName> Qualified = P4ImportedLevelActors(Layer);
        for (AActor* Actor : Layer->PersistentLevel->Actors)
            if (IsValid(Actor) && Actor->IsA<ANavMeshBoundsVolume>() &&
                QualifiedOriginalFamilyActor(Actor, Qualified, Omitted))
                ++QualifiedBounds;
        if (AWorldSettings* SettingsActor = Layer->PersistentLevel->GetWorldSettings())
            Qualified.Remove(SettingsActor->GetFName());
        Editable += Qualified.Num();
        Request.Roots.Add({EP4ExportRootKind::Package, Package});
        LayerValues.Add(MakeShared<FJsonValueString>(Package));
    }
    Report->SetArrayField(TEXT("original_layer_packages"), LayerValues);
    Report->SetArrayField(TEXT("imported_level_packages"), LayerValues);
    Report->SetNumberField(TEXT("levels"), Layers.Num());
    Report->SetNumberField(TEXT("game_backed_levels"), Partial);
    if (RequireNative &&
        !TestEqual(TEXT("Native import/export acceptance includes zero partial map layers"), Partial, 0))
        return false;
    Report->SetNumberField(TEXT("editable_original_actors"), Editable);
    Report->SetNumberField(TEXT("uneditable_original_actors"), OmittedCount);
    Report->SetNumberField(TEXT("qualified_source_navigation_bounds"), QualifiedBounds);
    if (Cube && Map == Cave000 &&
        !TestTrue(TEXT("Actual cube map import retains source-qualified navigation bounds"), QualifiedBounds > 0))
        return false;
    Report->SetNumberField(TEXT("loaded_levels"), World->GetLevels().Num());
    TSet<FName> Expected;
    if (!TestTrue(TEXT("Normal editor presentation activates the actual source common/selected graph: ") + Error,
            P4ActivateOriginalMapEditorLayers(World, Variant, Expected, Error)))
        return false;
    TSet<FString> RequestedLayers;
    for (const FName Package : Expected)
    {
        if (!TestTrue(TEXT("An activated layer comes from the independently observed original graph"),
                Layers.Contains(Package.ToString())))
            return false;
        RequestedLayers.Add(Package.ToString());
    }
    TSet<FString> Visible;
    TArray<TSharedPtr<FJsonValue>> Loaded, Activated;
    for (ULevel* Layer : World->GetLevels())
        if (Layer)
        {
            const FString Package = Layer->GetOutermost()->GetName();
            Loaded.Add(MakeShared<FJsonValueString>(Package));
            if (Layer->bIsVisible)
                Visible.Add(Package);
            if (RequestedLayers.Contains(Package) && Layer->bIsVisible)
                Activated.Add(MakeShared<FJsonValueString>(Package));
        }
    bool Active = true;
    for (const FString& Package : RequestedLayers)
        Active &= Visible.Contains(Package);
    Report->SetArrayField(TEXT("loaded_level_packages"), Loaded);
    Report->SetArrayField(TEXT("activated_layer_packages"), Activated);
    Report->SetBoolField(TEXT("variant_activated"), Active);
    Report->SetBoolField(TEXT("night_activated"), Active && Variant == TEXT("night"));
    Report->SetBoolField(TEXT("hero_activated"), Active && Variant == TEXT("hero"));
    if (!Phase(TEXT("variant_activation")) ||
        !TestTrue(TEXT("Requested actual original layers become visible in the editor"), Active))
        return false;
    for (const FString& Package : Layers)
    {
        UWorld* Layer = LoadObject<UWorld>(nullptr, *ObjectPathOf(Package));
        const auto Changes = P4AnalyzeOriginalLevelChanges(Layer);
        if (!TestTrue(TEXT("Editor-only activation does not change original export state: ") + Package,
                Changes.IsValid() && Changes.Unchanged))
            return false;
    }
    if (RequireGeometry && Map == Cave000)
    {
        TSet<FString> GeometryKinds;
        for (const FString& Package : Layers)
        {
            UWorld* Layer = LoadObject<UWorld>(nullptr, *ObjectPathOf(Package));
            TSet<FName> Omitted;
            FString Source, CustodyError;
            if (!TestNotNull(TEXT("Fresh geometry layer remains loaded: ") + Package, Layer))
                return false;
            if (!P4ReadGameBackedLevelCustody(Layer, Omitted, Source, CustodyError) &&
                !TestTrue(TEXT("Complete geometry layers have no partial custody corruption"),
                    CustodyError == TEXT("level-is-not-game-backed")))
                return false;
            const TSet<FName> Qualified = P4ImportedLevelActors(Layer);
            for (AActor* Actor : Layer->PersistentLevel->Actors)
                if (QualifiedOriginalFamilyActor(Actor, Qualified, Omitted))
                    for (const TCHAR* Kind : {TEXT("RM"), TEXT("CF"), TEXT("CP"), TEXT("SL"), TEXT("WL"), TEXT("WY")})
                        if (Actor->GetClass()->GetPathName().StartsWith(TEXT("/Game/Carrot4/Terrain/Cave/")) &&
                            Actor->GetName().Contains(FString(TEXT("_")) + Kind + TEXT("_")))
                            GeometryKinds.Add(Kind);
        }
        for (const TCHAR* Kind : {TEXT("RM"), TEXT("CF"), TEXT("CP"), TEXT("SL"), TEXT("WL"), TEXT("WY")})
            if (!TestTrue(
                    FString(TEXT("Fresh normal import constructs source-qualified cave ")) + Kind + TEXT(" geometry"),
                    GeometryKinds.Contains(Kind)))
                return false;
        Report->SetNumberField(TEXT("qualified_geometry_kinds"), GeometryKinds.Num());
        Report->SetBoolField(TEXT("geometry_family_qualification_required"), true);
        if (!TestTrue(TEXT("Freshly saved map survives an ordinary editor warm reopen"),
                UEditorLoadingAndSavingUtils::LoadMap(Map) != nullptr))
            return false;
        World = GEditor->GetEditorWorldContext().World();
    }
    TakeBackUnshipped();
    if (ImportOnly)
    {
        Phase(TEXT("import_verified"));
        return !HasAnyErrors();
    }
    FP4ExportModResult Unchanged;
    if (!Phase(TEXT("exporting_unchanged")))
        return false;
    const bool UnchangedOk = P4ExportMod(Request, Unchanged, Error);
    Report->SetBoolField(TEXT("unchanged_exported"), UnchangedOk);
    Report->SetStringField(TEXT("unchanged_error"), Error);
    bool VerifiedNoOp = false;
    if (!UnchangedOk && Error == TEXT("Nothing of the map was changed"))
    {
        // A dirty flag alone can route an exactly unchanged original through map-edit
        // export. The production contract refuses that empty edit. Verify the complete
        // current map again; do not call a no-op refusal a published artifact.
        bool Audit = true;
        for (const FString& Package : Layers)
        {
            UWorld* Layer = LoadObject<UWorld>(nullptr, *ObjectPathOf(Package));
            const auto Changes = Layer ? P4AnalyzeOriginalLevelChanges(Layer) : FP4LevelChangesResult();
            const auto Compared = P4DiagnoseOriginalAsset(Layer);
            Audit &= TestTrue(TEXT("No-op refusal requires a completely unchanged qualified layer: ") + Package,
                Layer && Changes.IsValid() && Changes.Unchanged && Compared.Error.IsEmpty() &&
                    Compared.Comparison == EP4OriginalComparison::Equal);
        }
        const bool Empty = !Unchanged.Published && Unchanged.Directory.IsEmpty() && !Unchanged.Packages.Num() &&
            !Unchanged.Levels.Num() && !Unchanged.Placements.Num() && !Unchanged.Files.Num() &&
            !Unchanged.PlacementRows.Num() && !Unchanged.GameRowEdits.Num() && !Unchanged.RebuiltLighting.Num() &&
            !Unchanged.BakedLighting.Num() && !Unchanged.BakedActors.Num() && !Unchanged.UnbakedActors.Num() &&
            !Unchanged.BakedVolumesTakenOut && !Unchanged.Replacements.Num() && !Unchanged.ReplacementFiles.Num() &&
            !Unchanged.PlacementMark && !Unchanged.RuntimePaths.Num() && Unchanged.Refused.Num() == 1 &&
            Unchanged.Refused[0] == Error;
        VerifiedNoOp = Audit && TestTrue(TEXT("The exact no-op refusal has no publication or output payload"), Empty);
    }
    Report->SetBoolField(TEXT("unchanged_verified_noop"), VerifiedNoOp);
    Report->SetStringField(TEXT("unchanged_outcome"),
        VerifiedNoOp      ? TEXT("verified_no_changes")
            : UnchangedOk ? TEXT("exported")
                          : TEXT("failed"));
    // What an unchanged map's export carried, so a publication that is not a no-op names its contents.
    if (UnchangedOk)
    {
        const auto Strings = [](const TArray<FString>& Values)
        {
            TArray<TSharedPtr<FJsonValue>> Out;
            for (const FString& Value : Values)
                Out.Add(MakeShared<FJsonValueString>(Value));
            return Out;
        };
        TSharedRef<FJsonObject> Carried = MakeShared<FJsonObject>();
        Carried->SetArrayField(TEXT("packages"), Strings(Unchanged.Packages));
        Carried->SetArrayField(TEXT("levels"), Strings(Unchanged.Levels));
        Carried->SetArrayField(TEXT("placements"), Strings(Unchanged.Placements));
        Carried->SetArrayField(TEXT("files"), Strings(Unchanged.Files));
        Carried->SetArrayField(TEXT("rebuilt_lighting"), Strings(Unchanged.RebuiltLighting));
        Carried->SetArrayField(TEXT("replacements"), Strings(Unchanged.Replacements));
        Carried->SetArrayField(TEXT("told"), Strings(Unchanged.Told));
        Carried->SetNumberField(TEXT("placement_rows"), Unchanged.PlacementRows.Num());
        Carried->SetNumberField(TEXT("game_row_edits"), Unchanged.GameRowEdits.Num());
        Report->SetObjectField(TEXT("unchanged_carried"), Carried);
    }
    if (!Phase(TEXT("unchanged_export_complete")) ||
        !TestTrue(TEXT("Unchanged export either publishes or proves the exact no-op contract: ") + Error,
            UnchangedOk || VerifiedNoOp))
        return false;
    TestEqual(TEXT("Unchanged partial/full map ships no replacement original package"), Unchanged.Packages.Num(), 0);
    Error.Reset();
    FActorSpawnParameters Spawn;
    Spawn.Name = FName(*(TEXT("P4QualifiedAddition_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    Spawn.OverrideLevel = World->PersistentLevel;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    FOwn CubeAssets;
    AActor* Added = nullptr;
    TArray<FString> RowActorNames;
    if (Cube)
    {
        if (!MakeOwn(*this,
                ModNames.PackageRoot / (TEXT("FreshCube_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)),
                CubeAssets))
            return false;
        if (!TestTrue(TEXT("Creator cube uses a custom material instance with its custom texture"),
                CubeAssets.Instance.IsValid() && CubeAssets.Mesh->GetMaterial(0) == CubeAssets.Instance.Get() &&
                    CubeAssets.Instance->TextureParameterValues.ContainsByPredicate(
                        [&](const FTextureParameterValue& Value)
                        {
                            return Value.ParameterInfo.Name == TEXT("Diffuse_01") &&
                                Value.ParameterValue == CubeAssets.Texture.Get();
                        })))
            return false;
        Report->SetStringField(TEXT("cube_material"), CubeAssets.Instance->GetPathName());
        Report->SetStringField(TEXT("cube_texture"), CubeAssets.Texture->GetPathName());
        TArray<UPackage*> Packages;
        for (UObject* Asset : CubeAssets.Made)
            if (Asset)
                Packages.AddUnique(Asset->GetOutermost());
        if (!TestTrue(TEXT("Actual creator cube assets save through the normal editor"),
                UEditorLoadingAndSavingUtils::SavePackages(Packages, true)))
            return false;
        // P4_NATIVE_SWEEP_CUBE_AT ("x,y,z"): where the cube goes, to stand where a game run can see it.
        FVector CubeAt(0, 0, 150);
        CubeAt.InitFromString(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_CUBE_AT")));
        AStaticMeshActor* MeshActor = World->SpawnActor<AStaticMeshActor>(
            AStaticMeshActor::StaticClass(), FTransform(CubeAt), Spawn);
        if (MeshActor)
            MeshActor->GetStaticMeshComponent()->SetStaticMesh(CubeAssets.Mesh.Get());
        Added = MeshActor;
        // P4_NATIVE_SWEEP_CUBE_INSTANCED: the same mesh as instances beside it, which need another usage of the material.
        if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_CUBE_INSTANCED")) == TEXT("1"))
        {
            FActorSpawnParameters Beside = Spawn;
            Beside.Name = NAME_None;
            AActor* Instanced = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Beside);
            auto* Part = NewObject<UInstancedStaticMeshComponent>(Instanced, TEXT("Instances"), RF_Transactional);
            Part->SetStaticMesh(CubeAssets.Mesh.Get());
            Instanced->SetRootComponent(Part);
            Instanced->AddInstanceComponent(Part);
            Part->RegisterComponent();
            Instanced->SetActorLocation(CubeAt + FVector(-300, 0, 0));
            Part->AddInstance(FTransform::Identity);
            Part->AddInstance(FTransform(FVector(-250, 0, 0)));
        }
        // P4_NATIVE_SWEEP_MATERIAL_ROW=1: a row of cubes with everyday creator materials beside it, from
        // P4_NATIVE_SWEEP_MATERIAL_ROW_AT ("x,y,z") or 3 m to the cube's side.
        if (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_MATERIAL_ROW")) == TEXT("1"))
        {
            FVector RowAt = CubeAt + FVector(0, 300, 0);
            const FString RowText = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_MATERIAL_ROW_AT"));
            if (!RowText.IsEmpty())
                RowAt.InitFromString(RowText);
            TArray<FString> RowNames;
            TArray<AActor*> RowActors;
            if (!AddMaterialRow(*this, World, ModNames.PackageRoot / TEXT("MaterialRow"), RowAt, CubeAssets.Made,
                    RowNames, RowActors))
                return false;
            TArray<TSharedPtr<FJsonValue>> Placed;
            for (const FString& Name : RowNames)
                Placed.Add(MakeShared<FJsonValueString>(Name));
            Report->SetArrayField(TEXT("material_row"), Placed);
            // P4_NATIVE_SWEEP_PARTS: the partly working cases beside the row (P4ExportPartialChecks.cpp).
            const FString Parts = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_PARTS"));
            if (!Parts.IsEmpty())
            {
                TArray<FString> PartNames;
                if (!AddPartialChecks(*this, World, ModNames.PackageRoot / TEXT("MaterialRow"), RowAt, Parts, Request,
                        CubeAssets.Made, PartNames, RowActors))
                    return false;
                TArray<TSharedPtr<FJsonValue>> PartsPlaced;
                for (const FString& Name : PartNames)
                    PartsPlaced.Add(MakeShared<FJsonValueString>(Name));
                Report->SetArrayField(TEXT("partial_checks"), PartsPlaced);
            }
            for (AActor* Actor : RowActors)
                RowActorNames.Add(Actor->GetName());
        }
    }
    else
        Added = World->SpawnActor<APointLight>(APointLight::StaticClass(), FTransform(FVector(0, 0, 150)), Spawn);
    if (!TestNotNull(TEXT("Supported new native actor constructs even when originals are uneditable"), Added))
        return false;
    Report->SetStringField(TEXT("added_actor"), Added->GetName());
    Report->SetBoolField(TEXT("creator_cube"), Cube);
    // P4_NATIVE_SWEEP_WHOLE_LEVELS: the edited level ships whole, with an edit a list of changes refuses, a new part on
    // one of the game's own actors.
    // P4_NATIVE_SWEEP_NATIVE: the mod is cooked for the game without Tsuchi, which ships the edited level whole too.
    const bool Native = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_NATIVE")) == TEXT("1");
    const bool Whole = Native || FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_WHOLE_LEVELS")) == TEXT("1");
    if (Whole && Cube && FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_WHOLE_PART")) != TEXT("0"))
    {
        AActor* GameActor = nullptr;
        for (AActor* Each : World->PersistentLevel->Actors)
            if (Each && Each != Added && Each->GetRootComponent() && Each->FindComponentByClass<UStaticMeshComponent>() &&
                (!GameActor || Each->GetName() < GameActor->GetName()))
                GameActor = Each;
        if (!TestNotNull(TEXT("The level has a mesh actor of the game's"), GameActor))
            return false;
        UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(GameActor, TEXT("P4WholeLevelPart"), RF_Transactional);
        Part->SetStaticMesh(CubeAssets.Mesh.Get());
        Part->SetupAttachment(GameActor->GetRootComponent());
        Part->SetRelativeLocation(FVector(0, 0, 400));
        GameActor->AddInstanceComponent(Part);
        Part->RegisterComponent();
        GameActor->MarkPackageDirty();
        Report->SetStringField(TEXT("whole_level_part_actor"), GameActor->GetName());
        Report->SetStringField(TEXT("whole_level_part_location"), Part->GetComponentLocation().ToString());
    }
    // P4_NATIVE_SWEEP_NEAR ("x,y,z") and P4_NATIVE_SWEEP_NEAR_CLASS: in a whole level, the nearest piece of the class
    // is moved up by P4_NATIVE_SWEEP_MOVE_BY and the next nearest is removed, in whichever levels they are.
    TArray<FString> NearParts;
    FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_NEAR")).ParseIntoArray(NearParts, TEXT(","));
    const FString NearClass = FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_NEAR_CLASS"));
    if (Whole && NearParts.Num() == 3 && !NearClass.IsEmpty())
    {
        const FVector Point(FCString::Atof(*NearParts[0]), FCString::Atof(*NearParts[1]), FCString::Atof(*NearParts[2]));
        TArray<AActor*> Near;
        for (ULevel* Level : World->GetLevels())
            if (Level && Level->bIsVisible)
                for (AActor* Actor : Level->Actors)
                {
                    bool Kind = false;
                    for (const UClass* Class = Actor ? Actor->GetClass() : nullptr; Class && !Kind;
                         Class = Class->GetSuperClass())
                        Kind = Class->GetName() == NearClass;
                    if (Kind && !Actor->IsPendingKill() && Actor->GetAttachParentActor() == nullptr)
                        Near.Add(Actor);
                }
        Near.Sort([&](const AActor& A, const AActor& B)
            { return FVector::DistSquared(A.GetActorLocation(), Point) < FVector::DistSquared(B.GetActorLocation(), Point); });
        if (!TestTrue(TEXT("Two pieces of the asked class are near the asked point"), Near.Num() >= 2))
            return false;
        FVector By(0, 0, 400);
        By.InitFromString(FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_MOVE_BY")));
        TArray<UPackage*> Levels = {Near[0]->GetOutermost()};
        Levels.AddUnique(Near[1]->GetOutermost());
        Report->SetStringField(TEXT("whole_moved"), Near[0]->GetPathName());
        Report->SetStringField(TEXT("whole_moved_to"), (Near[0]->GetActorLocation() + By).ToString());
        Report->SetStringField(TEXT("whole_removed"), Near[1]->GetPathName());
        Near[0]->Modify();
        Near[0]->SetActorLocation(Near[0]->GetActorLocation() + By, false, nullptr, ETeleportType::TeleportPhysics);
        if (!TestTrue(TEXT("The editor removes the asked piece"), World->EditorDestroyActor(Near[1], true)))
            return false;
        for (UPackage* Level : Levels)
        {
            if (!Request.Roots.ContainsByPredicate([&](const FP4ExportRoot& Root) { return Root.Path == Level->GetName(); }))
                Request.Roots.Add({EP4ExportRootKind::Package, Level->GetName()});
            if (Level != World->GetOutermost() && !TestTrue(TEXT("The edited sublevel saves"),
                    UEditorLoadingAndSavingUtils::SavePackages({Level}, false)))
                return false;
        }
    }
    Report->SetBoolField(TEXT("whole_levels"), Whole);
    if (Cube)
    {
        const FString AddedName = Added->GetName();
        const FTransform Position = Added->GetActorTransform();
        if (!TestTrue(TEXT("Creator cube placement saves through normal map save"),
                UEditorLoadingAndSavingUtils::SaveMap(World, Map)))
            return false;
        // Do not retain strong/raw actor references across ordinary map teardown.
        Added = nullptr;
        World = UEditorLoadingAndSavingUtils::LoadMap(Map);
        if (!TestNotNull(TEXT("Fresh map with creator cube reopens normally"), World))
            return false;
        Added = FindObject<AActor>(World->PersistentLevel, *AddedName);
        if (!TestTrue(TEXT("Actual creator cube mesh and transform survive save/reopen"),
                Added && Cast<AStaticMeshActor>(Added) &&
                    CastChecked<AStaticMeshActor>(Added)->GetStaticMeshComponent()->GetStaticMesh() ==
                        CubeAssets.Mesh.Get() &&
                    Added->GetActorTransform().Equals(Position)))
            return false;
        if (!TestTrue(TEXT("Custom cube material survives map save/reopen"),
                CastChecked<AStaticMeshActor>(Added)->GetStaticMeshComponent()->GetMaterial(0) ==
                    CubeAssets.Instance.Get()))
            return false;
        Report->SetBoolField(TEXT("cube_saved_and_reopened"), true);
    }
    ON_SCOPE_EXIT
    {
        if (IsValid(Added))
            World->DestroyActor(Added);
    };
    const auto Delta = P4AnalyzeOriginalLevelChanges(World);
    if (!TestTrue(TEXT("Supported addition has the exact qualified overlay delta"),
            Delta.IsValid() && (Whole || Delta.Removed.Num() == 0) && Delta.Added.Num() == (FPlatformMisc::GetEnvironmentVariable(TEXT("P4_NATIVE_SWEEP_CUBE_INSTANCED")) == TEXT("1") ? 2 : 1) + RowActorNames.Num() &&
                Delta.Added.Contains(Added->GetName()) &&
                !RowActorNames.ContainsByPredicate([&](const FString& Name) { return !Delta.Added.Contains(Name); })))
        return false;
    FP4ExportModResult Edited;
    if (!Phase(TEXT("exporting_edited")))
        return false;
    Request.WholeLevels = Whole && !Native;
    Request.Native = Native;
    const bool EditedOk = P4ExportMod(Request, Edited, Error);
    Report->SetBoolField(TEXT("native"), Native);
    Report->SetBoolField(TEXT("edited_exported"), EditedOk);
    Report->SetStringField(TEXT("edited_error"), Error);
    if (!Phase(TEXT("edited_export_complete")) ||
        !TestTrue(TEXT("Normal exporter ships the supported addition: ") + Error, EditedOk))
        return false;
    if (!TestTrue(TEXT("Supported actor is carried in at least one actual mod package"), Edited.Packages.Num() != 0))
        return false;
    TArray<TSharedPtr<FJsonValue>> AddedPackages;
    for (const FString& Package : Edited.Packages)
        AddedPackages.Add(MakeShared<FJsonValueString>(Package));
    Report->SetArrayField(TEXT("added_packages"), AddedPackages);
    if (Whole)
        TestTrue(TEXT("The edited level is in the mod whole, at its game path"), Edited.Replacements.Contains(Map));
    else
        for (const FString& Package : Edited.Packages)
            TestFalse(TEXT("No original layer is replaced by a cooked empty/editor projection"), Layers.Contains(Package));
    {
        TArray<TSharedPtr<FJsonValue>> Told, Unbaked;
        for (const FString& Line : Edited.Told)
            Told.Add(MakeShared<FJsonValueString>(Line));
        for (const FString& Actor : Edited.UnbakedActors)
            Unbaked.Add(MakeShared<FJsonValueString>(Actor));
        Report->SetArrayField(TEXT("edited_told"), Told);
        Report->SetArrayField(TEXT("edited_unbaked_actors"), Unbaked);
    }
    if (Cube)
    {
        TArray<UObject*> Shipped = {CubeAssets.Mesh.Get(), CubeAssets.Instance.Get(), CubeAssets.Texture.Get()};
        for (UObject* Asset : CubeAssets.Made)
            Shipped.AddUnique(Asset);
        for (UObject* Asset : Shipped)
        {
            // A treasure or creature declaration is written into the manifest, not cooked.
            if (Asset && Asset->IsA<UP4AddedContent>())
                continue;
            // A material ships at the path export gives it, which RuntimePaths says.
            const FString* Runtime = Edited.RuntimePaths.Find(Asset->GetPathName());
            const FString Package = Runtime ? FPackageName::ObjectPathToPackageName(*Runtime) : Asset->GetOutermost()->GetName();
            TestTrue(TEXT("Published normal mod contains each cube mesh/material/texture package: ") + Package,
                Edited.Packages.Contains(Package));
        }
        // Cooked for the game without Tsuchi: one container in the game's Paks folder and nothing else.
        if (Native)
        {
            TestTrue(TEXT("A mod cooked without Tsuchi holds only a container for the game's Paks folder"),
                Edited.Files.Num() > 0 &&
                    !Edited.Files.ContainsByPredicate([](const FString& File)
                        { return !File.StartsWith(TEXT("romfs/" P4_GAME_PROJECT "/Content/Paks/")); }));
            Report->SetStringField(TEXT("export_directory"), Edited.Directory);
        }
        FString Manifest, Navigation;
        const FString ModFolder = Edited.Directory / TEXT("romfs/tsuchi/mods") / Request.Mod.Id;
        if (!Native)
        {
        if (!TestTrue(TEXT("The cube export manifest names actual rebuilt navigation for this map"),
                FFileHelper::LoadFileToString(Manifest, *(ModFolder / TEXT("mod.json"))) &&
                    P4MapNavigationPath(Manifest, Map, Navigation)))
            return false;
        TArray<uint8> Bytes;
        FString NavigationMap;
        TMap<FString, P4MapKit::NavigationExport::BuiltMesh> Meshes;
        if (!TestTrue(TEXT("The cube export writes a decodable .nav file for the map"),
                FFileHelper::LoadFileToArray(Bytes, *(ModFolder / Navigation)) &&
                    P4ExportNavigationTest::Meshes(Bytes, NavigationMap, Meshes) && NavigationMap == Map &&
                    Meshes.Num() > 0))
            return false;
        int32 Tiles = 0, Polygons = 0;
        for (const auto& Mesh : Meshes)
            for (const auto& Tile : Mesh.Value.Tiles)
            {
                ++Tiles;
                Polygons += int32(Tile.Polys.size());
            }
        if (!TestTrue(TEXT("The exported navigation has real rebuilt walkable tiles and polygons"),
                Tiles > 0 && Polygons > 0))
            return false;
        Report->SetStringField(TEXT("navigation_file"), Navigation);
        Report->SetNumberField(TEXT("navigation_bytes"), Bytes.Num());
        Report->SetNumberField(TEXT("navigation_tiles"), Tiles);
        Report->SetNumberField(TEXT("navigation_polygons"), Polygons);
        Report->SetStringField(TEXT("export_directory"), Edited.Directory);
        }
    }
    FP4ExportGameInventory Inventory;
    if (!TestTrue(TEXT("Actual original package inventory can be independently inspected"),
            P4ReadExportGameInventory(
                Request.Tools, Request.GamePaksDirectory, Request.WorkDirectory, Inventory, Error)))
        return false;
    bool ContainerFound = false;
    TArray<TSharedPtr<FJsonValue>> ContainerIds;
    for (const FString& File : Edited.Files)
        if (File.EndsWith(TEXT(".utoc")))
        {
            ContainerFound = true;
            FP4ExportContainerCensus Census;
            if (!TestTrue(TEXT("Published real added-actor container is inspectable"),
                    P4InspectExportContainer(
                        Request.Tools, Edited.Directory / File, Request.WorkDirectory, Inventory, Census, Error)))
                return false;
            if (!Whole)
                TestEqual(
                    TEXT("Published container claims no original game package or redirect"), Census.GameClaims.Num(), 0);
            for (const uint64 Id : Census.PackageIds)
                ContainerIds.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%llu"), Id)));
            for (const auto& Pair : Inventory.PackageIds())
                if (!Whole && Layers.Contains(Pair.Value.ToString()))
                    TestFalse(TEXT("Actual published container has no original map/layer ID"),
                        Census.PackageIds.Contains(Pair.Key));
        }
    Report->SetArrayField(TEXT("container_package_ids"), ContainerIds);
    TestTrue(TEXT("Actual supported addition produced a real container"), ContainerFound);
    TakeBackUnshipped();
    if (!HasAnyErrors())
        Phase(TEXT("editor_verified"));
    return !HasAnyErrors();
}
#endif

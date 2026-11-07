#if P4_MAPKIT_TESTS
#include "P4GamePreviewWorld.h"
#include "P4GameNames.h"
#include "P4GamePreviewContent.h"
#include "P4CreatorSource.h"
#include "P4OriginalRecord.h"
#include "AI/NavigationSystemConfig.h"
#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "IAssetTools.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/PackageName.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "NavigationSystem.h"
#include "Sound/ReverbEffect.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

namespace
{
// The recipe the preview template gives an area: the game's own scene and counts, renamed.
FP4GamePreviewSceneRecipe AreaRecipe(uint32 Area)
{
    const FString Name = FString::Printf(TEXT("Area%03u"), Area);
    FP4GamePreviewSceneRecipe Recipe;
    Recipe.World = Name + TEXT("_P");
    Recipe.Scene = TEXT("SD_") + Name;
    Recipe.SceneClass = TEXT("/Script/Carrot4.GameSceneData");
    Recipe.Counts = TEXT("APN_") + Name + TEXT("_P");
    Recipe.CountsClass = TEXT("/Script/Carrot4.ActorPlacedNumAsset");
    Recipe.ParentScene = TEXT("/Game/Carrot4/GameScene/Game/SD_Area.SD_Area");
    Recipe.CountsVersion = 8626647386ull;
    return Recipe;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GamePreviewWorldSettingsTest, "P4MapKit.Game.PreviewWorldSettings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GamePreviewWorldSettingsTest::RunTest(const FString&)
{
    UClass* GameClass = LoadObject<UClass>(nullptr, TEXT("/Script/Carrot4.MyWorldSettings"));
    if (!TestNotNull(TEXT("the actual game settings class is available"), GameClass))
        return false;
    FBoolProperty* Enabled = FindFProperty<FBoolProperty>(
        AWorldSettings::StaticClass(), TEXT("bEnableNavigationSystem"));
    FObjectPropertyBase* ConfigField = FindFProperty<FObjectPropertyBase>(
        AWorldSettings::StaticClass(), TEXT("NavigationSystemConfig"));
    FBoolProperty* Strict = FindFProperty<FBoolProperty>(
        UNavigationSystemModuleConfig::StaticClass(), TEXT("bStrictlyStatic"));
    if (!TestTrue(TEXT("native config fields are available"), Enabled && ConfigField && Strict))
        return false;
    AWorldSettings* Defaults = CastChecked<AWorldSettings>(GameClass->GetDefaultObject());
    const bool DefaultEnabled = Enabled->GetPropertyValue_InContainer(Defaults);
    UNavigationSystemConfig* DefaultConfig = Defaults->GetNavigationSystemConfig();
    const FString DefaultNavigation = DefaultConfig ? DefaultConfig->NavigationSystemClass.ToString() : FString();
    const bool DefaultStrict = DefaultConfig && DefaultConfig->IsA<UNavigationSystemModuleConfig>() &&
        Strict->GetPropertyValue_InContainer(DefaultConfig);
    UClass* EngineSelection = GEngine->WorldSettingsClass;

    const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UPackage* SourcePackage = CreatePackage(*(TEXT("/Temp/KadanSettingsSource/") + Id));
    TStrongObjectPtr<UWorld> Source(
        UWorld::CreateWorld(EWorldType::EditorPreview, false, TEXT("SourceWorld"), SourcePackage));
    if (!TestNotNull(TEXT("the isolated authored fixture exists"), Source.Get()))
        return false;
    ON_SCOPE_EXIT
    {
        Source->DestroyWorld(false);
    };
    const auto VanillaSettings = [](UWorld* World) -> AWorldSettings*
    {
        AWorldSettings* Before = World->PersistentLevel->GetWorldSettings();
        if (Before->GetClass() == AWorldSettings::StaticClass())
            return Before;
        FActorSpawnParameters Spawn;
        Spawn.OverrideLevel = World->PersistentLevel;
        Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        AWorldSettings* Made = World->SpawnActor<AWorldSettings>(AWorldSettings::StaticClass(), Spawn);
        if (!Made)
            return nullptr;
        UEngine::FCopyPropertiesForUnrelatedObjectsParams Copy;
        Copy.bDoDelta = false;
        UEngine::CopyPropertiesForUnrelatedObjects(Before, Made, Copy);
        World->PersistentLevel->SetWorldSettings(Made);
        if (IsValid(Before))
            Before->Destroy();
        return Made;
    };
    AWorldSettings* Authored = VanillaSettings(Source.Get());
    if (!TestNotNull(TEXT("the fixture has exact vanilla settings without changing project defaults"), Authored))
        return false;
    Authored->KillZ = -7654.f;
    Authored->bGlobalGravitySet = true;
    Authored->GlobalGravityZ = -432.f;
    Authored->DefaultGameMode = AGameModeBase::StaticClass();
    Authored->LightmassSettings.StaticLightingLevelScale = 2.5f;
    AActor* Owner = Source->SpawnActor<AActor>();
    Authored->SetOwner(Owner);
    UReverbEffect* Reverb = NewObject<UReverbEffect>(Source.Get(), TEXT("AuthoredReverb"));
    Authored->DefaultReverbSettings.ReverbEffect = Reverb;
    UNavigationSystemModuleConfig* SourceConfig = NewObject<UNavigationSystemModuleConfig>(
        Authored, TEXT("AuthoredNavigationConfig"));
    SourceConfig->NavigationSystemClass = FSoftClassPath(TEXT("/Script/NavigationSystem.NavigationSystemV1"));
    SourceConfig->DefaultAgentName = TEXT("AuthoredAgent");
    Strict->SetPropertyValue_InContainer(SourceConfig, false);
    Enabled->SetPropertyValue_InContainer(Authored, true);
    ConfigField->SetObjectPropertyValue_InContainer(Authored, SourceConfig);
    UObjectRedirector* Reference = NewObject<UObjectRedirector>(Source.Get(), TEXT("SettingsReference"));
    Reference->DestinationObject = Authored;
    Source->ExtraReferencedObjects.Add(Reference);
    const bool SourceDirty = SourcePackage->IsDirty();

    const FString PreviewRoot = TEXT(P4_PREVIEW_PACKAGE_ROOT) + Id + TEXT("_0123456789abcdef");
    UPackage* Target = CreatePackage(*(PreviewRoot / TEXT("Area005_GameArea")));
    FObjectDuplicationParameters Duplicate(Source.Get(), Target);
    Duplicate.DestName = FName(TEXT("Area005_GameArea"));
    Duplicate.DuplicateMode = EDuplicateMode::World;
    Duplicate.FlagMask = RF_AllFlags & ~RF_Transient;
    TStrongObjectPtr<UWorld> Preview(Cast<UWorld>(StaticDuplicateObjectEx(Duplicate)));
    if (!TestNotNull(TEXT("native duplication makes a separate preview world"), Preview.Get()))
        return false;
    ON_SCOPE_EXIT
    {
        Preview->DestroyWorld(false);
        Target->SetDirtyFlag(false);
    };
    AWorldSettings* Before = VanillaSettings(Preview.Get());
    if (!TestNotNull(TEXT("the preview starts with exact vanilla settings"), Before))
        return false;
    // The real clone scope can retain the same object through both native
    // duplicate records and package enumeration. Pending-kill references are
    // still nulled by GC even through these strong guards.
    TArray<TStrongObjectPtr<AWorldSettings>> RetiredGuards;
    RetiredGuards.Emplace(Before);
    RetiredGuards.Emplace(Before);
    UObjectRedirector* CopiedReference = FindObject<UObjectRedirector>(Preview.Get(), TEXT("SettingsReference"));
    // The fixture's native class repair can replace the initial actor, so make
    // its incoming reference explicit before exercising the product migration.
    if (!TestNotNull(TEXT("the private world owns its own reference holder"), CopiedReference))
        return false;
    CopiedReference->DestinationObject = Before;
    AActor* CopiedOwner = Before->GetOwner();
    UReverbEffect* CopiedReverb = Before->DefaultReverbSettings.ReverbEffect;
    FString Error;
    TestFalse(TEXT("the authored world itself is never a permitted migration target"),
        P4PrepareGamePreviewWorld(Source.Get(), Source.Get(), Error));
    TestFalse(TEXT("a target outside the generated preview namespace is refused"),
        P4PrepareGamePreviewWorld(Source.Get(), Preview.Get(), Error));
    if (!TestTrue(TEXT("the actual native settings replacement succeeds on the private clone"),
            P4PrepareGamePreviewWorld(Preview.Get(), Source.Get(), Error)))
    {
        AddError(Error);
        return false;
    }
    AWorldSettings* Settings = Preview->GetWorldSettings();
    UNavigationSystemConfig* Config = Settings->GetNavigationSystemConfig();
    int32 LiveSettings = 0;
    for (AActor* Actor : Preview->PersistentLevel->Actors)
        if (IsValid(Actor) && Actor->IsA<AWorldSettings>())
            ++LiveSettings;
    TestTrue(TEXT("the game's actor replaces vanilla settings in the native level slot"),
        Settings != Before && Settings->GetClass() == GameClass && Preview->PersistentLevel->Actors[0] == Settings &&
            !IsValid(Before) && LiveSettings == 1);
    TestTrue(TEXT("authored scalar, lighting and class properties survive the native migration"),
        Settings->KillZ == -7654.f && Settings->bGlobalGravitySet && Settings->GlobalGravityZ == -432.f &&
            Settings->LightmassSettings.StaticLightingLevelScale == 2.5f &&
            Settings->DefaultGameMode == AGameModeBase::StaticClass());
    TestTrue(TEXT("shared authored object references stay inside their private world"),
        Settings->GetOwner() == CopiedOwner && CopiedOwner != Owner &&
            Settings->DefaultReverbSettings.ReverbEffect == CopiedReverb && CopiedReverb != Reverb &&
            CopiedReference->DestinationObject == Settings);
    TestTrue(TEXT("the game nav config is owned by this settings actor with the preserved agent setting"),
        Config && Config != SourceConfig && Config != DefaultConfig && Config->GetOuter() == Settings &&
            Config->DefaultAgentName == FName(TEXT("AuthoredAgent")) &&
            Config->NavigationSystemClass.ToString() == TEXT("/Script/Carrot4.CarrotNavigationSystem") &&
            Strict->GetPropertyValue_InContainer(Config) && !Enabled->GetPropertyValue_InContainer(Settings));
    if (!TestTrue(TEXT("preparing the same private game settings is idempotent"),
            P4PrepareGamePreviewWorld(Preview.Get(), Source.Get(), Error)))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("idempotence preserves settings and config identity"),
        Preview->GetWorldSettings() == Settings && Settings->GetNavigationSystemConfig() == Config);
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("native GC nulls duplicate strong guards for the retired settings actor"),
        !RetiredGuards[0].IsValid() && !RetiredGuards[1].IsValid() && RetiredGuards[0].Get() == nullptr &&
            RetiredGuards[1].Get() == nullptr);
    TestTrue(TEXT("source settings, references, nav flags and dirty state are unchanged"),
        Source->GetWorldSettings() == Authored && Reference->DestinationObject == Authored &&
            Authored->DefaultReverbSettings.ReverbEffect == Reverb && SourceConfig->GetOuter() == Authored &&
            SourceConfig->NavigationSystemClass.ToString() == TEXT("/Script/NavigationSystem.NavigationSystemV1") &&
            !Strict->GetPropertyValue_InContainer(SourceConfig) && Enabled->GetPropertyValue_InContainer(Authored) &&
            SourcePackage->IsDirty() == SourceDirty);
    TestTrue(TEXT("the engine selection and game class defaults are untouched"),
        GEngine->WorldSettingsClass == EngineSelection &&
            Enabled->GetPropertyValue_InContainer(Defaults) == DefaultEnabled &&
            Defaults->GetNavigationSystemConfig() == DefaultConfig &&
            (!DefaultConfig || DefaultConfig->NavigationSystemClass.ToString() == DefaultNavigation) &&
            (!DefaultConfig || !DefaultConfig->IsA<UNavigationSystemModuleConfig>() ||
                Strict->GetPropertyValue_InContainer(DefaultConfig) == DefaultStrict));

    // A creator map saved with Engine.WorldSettings, in a project whose settings class is the game's. The
    // engine's PostLoad repair runs when such a level is duplicated (Play's clone) or loaded (the editor
    // opening the map) and leaves the old settings actor beside the new one. Neither case may give the
    // clone two settings actors, and the authored values must survive.
    const auto SettingsActors = [](UWorld* World)
    {
        int32 Count = 0;
        for (AActor* Actor : World->PersistentLevel->Actors)
            if (IsValid(Actor) && Actor->IsA<AWorldSettings>())
                ++Count;
        return Count;
    };
    const auto Clone = [&](UWorld* From, const FString& Name) -> UWorld*
    {
        UPackage* Package = CreatePackage(*(PreviewRoot / Name));
        FObjectDuplicationParameters Copy(From, Package);
        Copy.DestName = FName(*Name);
        Copy.DuplicateMode = EDuplicateMode::World;
        Copy.FlagMask = RF_AllFlags & ~RF_Transient;
        return Cast<UWorld>(StaticDuplicateObjectEx(Copy));
    };
    if (EngineSelection == GameClass)
    {
        const auto Authored_ = [&](AWorldSettings* Actor)
        {
            return Actor && Actor->KillZ == -7654.f && Actor->bGlobalGravitySet && Actor->GlobalGravityZ == -432.f &&
                Actor->LightmassSettings.StaticLightingLevelScale == 2.5f &&
                Actor->DefaultGameMode == AGameModeBase::StaticClass();
        };
        // The clone of a level whose current settings are the engine's class.
        TStrongObjectPtr<UWorld> Repaired(Clone(Source.Get(), TEXT("Area005_Repaired")));
        if (!TestNotNull(TEXT("native duplication makes the repaired preview world"), Repaired.Get()))
            return false;
        ON_SCOPE_EXIT
        {
            Repaired->DestroyWorld(false);
            Repaired->GetOutermost()->SetDirtyFlag(false);
        };
        AddInfo(FString::Printf(TEXT("after duplication the clone has %d settings actors, current %s"),
            SettingsActors(Repaired.Get()), *Repaired->GetWorldSettings()->GetClass()->GetName()));
        TestTrue(TEXT("duplicating plain settings runs the engine's repair, which leaves the old actor"),
            SettingsActors(Repaired.Get()) == 2);
        if (!TestTrue(TEXT("Play prepares the clone of a plain-settings level"),
                P4PrepareGamePreviewWorld(Repaired.Get(), Source.Get(), Error)))
        {
            AddError(Error);
            return false;
        }
        AWorldSettings* Made = Repaired->GetWorldSettings();
        TestTrue(TEXT("the clone of a plain-settings level keeps one settings actor of the game's class"),
            SettingsActors(Repaired.Get()) == 1 && Made->GetClass() == GameClass &&
                Repaired->PersistentLevel->Actors[0] == Made);
        TestTrue(TEXT("its authored values survive"), Authored_(Made));
        TestTrue(TEXT("its navigation config is the game's"),
            Made->GetNavigationSystemConfig() && Made->GetNavigationSystemConfig()->GetOuter() == Made &&
                Made->GetNavigationSystemConfig()->NavigationSystemClass.ToString() ==
                    TEXT("/Script/Carrot4.CarrotNavigationSystem"));

        // The clone of a level the editor repaired when it opened it: the source itself keeps the old actor.
        UPackage* LoadedPackage = CreatePackage(*(TEXT("/Temp/KadanSettingsLoaded/") + Id));
        FObjectDuplicationParameters Load(Source.Get(), LoadedPackage);
        Load.DestName = FName(TEXT("LoadedWorld"));
        Load.DuplicateMode = EDuplicateMode::World;
        Load.FlagMask = RF_AllFlags & ~RF_Transient;
        TStrongObjectPtr<UWorld> Loaded(Cast<UWorld>(StaticDuplicateObjectEx(Load)));
        if (!TestNotNull(TEXT("the opened creator level exists"), Loaded.Get()))
            return false;
        ON_SCOPE_EXIT
        {
            Loaded->DestroyWorld(false);
            Loaded->GetOutermost()->SetDirtyFlag(false);
        };
        TestTrue(TEXT("the opened creator level keeps the old actor beside the game's"),
            SettingsActors(Loaded.Get()) == 2 && Loaded->GetWorldSettings()->GetClass() == GameClass);
        TStrongObjectPtr<UWorld> FromLoaded(Clone(Loaded.Get(), TEXT("Area005_FromLoaded")));
        if (!TestNotNull(TEXT("native duplication makes the opened level's preview world"), FromLoaded.Get()))
            return false;
        ON_SCOPE_EXIT
        {
            FromLoaded->DestroyWorld(false);
            FromLoaded->GetOutermost()->SetDirtyFlag(false);
        };
        if (!TestTrue(TEXT("Play prepares the clone of a level the editor repaired"),
                P4PrepareGamePreviewWorld(FromLoaded.Get(), Loaded.Get(), Error)))
        {
            AddError(Error);
            return false;
        }
        AWorldSettings* Kept = FromLoaded->GetWorldSettings();
        TestTrue(TEXT("the clone of a repaired level keeps one settings actor, the current one"),
            SettingsActors(FromLoaded.Get()) == 1 && Kept->GetClass() == GameClass &&
                FromLoaded->PersistentLevel->Actors[0] == Kept && Authored_(Kept));
        TestEqual(TEXT("the opened creator level itself is not changed"), SettingsActors(Loaded.Get()), 2);
    }
    else
        AddInfo(
            TEXT("This project's settings class is not the game's, so the engine repair cases were not exercised."));

    // The game's arrival code indexes every category without checking Find's
    // result. Exercise real native TMap lookups on the actually created scene
    // asset, rather than accepting an empty map or inspecting its JSON text.
    FP4GamePreviewSceneAssets Scene;
    if (!TestTrue(TEXT("a native preview scene and its count asset can be created"),
            Scene.Create(PreviewRoot, PreviewRoot / TEXT("Area005_P.Area005_P"), AreaRecipe(5), Error)))
    {
        AddError(Error);
        return false;
    }
    UObject* Counts = FindObject<UObject>(nullptr, *Scene.CountsObjectPath());
    FStructProperty* CountInfo = Counts ? FindFProperty<FStructProperty>(Counts->GetClass(), TEXT("ActorPlacedNumInfo"))
                                        : nullptr;
    FMapProperty* CountMap = CountInfo ? FindFProperty<FMapProperty>(CountInfo->Struct, TEXT("ExploreRateActorNum"))
                                       : nullptr;
    FUInt64Property* Version = CountInfo ? FindFProperty<FUInt64Property>(CountInfo->Struct, TEXT("AssetVersion"))
                                         : nullptr;
    FIntProperty* CountNumber = CountMap ? CastField<FIntProperty>(CountMap->ValueProp) : nullptr;
    if (!TestTrue(TEXT("the native game count map has byte keys and int32 values"),
            Counts && CountInfo && CountMap && Version && CountNumber &&
                CountMap->KeyProp->ElementSize == int32(sizeof(uint8))))
        return false;
    void* CountData = CountInfo->ContainerPtrToValuePtr<void>(Counts);
    FScriptMapHelper NativeCounts(CountMap, CountMap->ContainerPtrToValuePtr<void>(CountData));
    TestEqual(TEXT("the count asset retains the exact game format version"),
        Version->GetPropertyValue_InContainer(CountData), uint64(8626647386ull));
    TestEqual(TEXT("the empty world still has every runtime category"), NativeCounts.Num(), 8);
    for (uint8 Category = 0; Category < 8; ++Category)
    {
        const int32 Entry = NativeCounts.FindMapIndexWithKey(&Category);
        TestTrue(FString::Printf(TEXT("runtime category %u has a valid native hash lookup"), uint32(Category)),
            Entry != INDEX_NONE);
        if (Entry != INDEX_NONE)
            TestEqual(FString::Printf(TEXT("runtime category %u starts at zero"), uint32(Category)),
                CountNumber->GetPropertyValue(NativeCounts.GetValuePtr(Entry)), 0);
    }
    FEnumProperty* EnumKey = CastField<FEnumProperty>(CountMap->KeyProp);
    FByteProperty* ByteKey = CastField<FByteProperty>(CountMap->KeyProp);
    UEnum* CountEnum = EnumKey ? EnumKey->GetEnum() : ByteKey ? ByteKey->Enum : nullptr;
    if (!TestNotNull(TEXT("the native count key enum is available"), CountEnum))
        return false;
    const int64 TerminalValue = CountEnum->GetValueByNameString(TEXT("Max"));
    const int64 NoneValue = CountEnum->GetValueByNameString(TEXT("None"));
    if (!TestEqual(TEXT("the native Max sentinel is eight"), TerminalValue, int64(8)) ||
        !TestEqual(TEXT("the native None sentinel is nine"), NoneValue, int64(9)))
        return false;
    uint8 Terminal = uint8(TerminalValue), None = uint8(NoneValue);
    TestEqual(
        TEXT("the Max sentinel is not a gameplay category"), NativeCounts.FindMapIndexWithKey(&Terminal), INDEX_NONE);
    TestEqual(
        TEXT("the None sentinel is not a gameplay category"), NativeCounts.FindMapIndexWithKey(&None), INDEX_NONE);
    return true;
}

// What Play sends: the open level's closure only, with a copied game asset counted as the creator's
// (as export counts it), a clear refusal for gameplay objects in a level of the creator's own, and
// no placeholder left at the game's SD_Area path. In-memory fixtures only; nothing is saved.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FP4GamePreviewContentScopeTest, "P4MapKit.Game.PreviewContentScope",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FP4GamePreviewContentScopeTest::RunTest(const FString&)
{
    const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Root = TEXT("/Game/__KadanPreviewScopeTest_") + Id;
    TArray<TStrongObjectPtr<UPackage>> Made;
    TArray<TStrongObjectPtr<UWorld>> Worlds;
    ON_SCOPE_EXIT
    {
        for (const auto& World : Worlds)
            if (World.IsValid())
                World->DestroyWorld(false);
        for (const auto& Held : Made)
        {
            UPackage* Package = Held.Get();
            if (!IsValid(Package))
                continue;
            TArray<UObject*> Objects;
            GetObjectsWithOuter(Package, Objects, false);
            for (UObject* Object : Objects)
            {
                if (Object->IsAsset())
                    FAssetRegistryModule::AssetDeleted(Object);
                Object->ClearFlags(RF_Public | RF_Standalone);
                Object->SetFlags(RF_Transient);
            }
            Package->SetDirtyFlag(false);
            Package->Rename(
                *(TEXT("/Temp/KadanPreviewScopeReleased/") + FGuid::NewGuid().ToString(EGuidFormats::Digits)), nullptr,
                REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);
            Package->MarkPendingKill();
        }
    };
    const auto Instance = [&](const FString& Name) -> UMaterialInstanceConstant*
    {
        UPackage* Package = CreatePackage(*(Root / Name));
        Made.Emplace(Package);
        UMaterialInstanceConstant* Made_ = NewObject<UMaterialInstanceConstant>(
            Package, FName(*Name), RF_Public | RF_Standalone);
        FAssetRegistryModule::AssetCreated(Made_);
        return Made_;
    };
    const auto WorldAt = [&](const FString& PackageName) -> UWorld*
    {
        UPackage* Package = CreatePackage(*PackageName);
        Made.Emplace(Package);
        UWorld* World = UWorld::CreateWorld(
            EWorldType::Editor, false, FName(*FPackageName::GetShortName(PackageName)), Package);
        if (World)
            Worlds.Emplace(World);
        return World;
    };
    const auto Uses = [&](UWorld* World, const TArray<UMaterialInterface*>& Materials)
    {
        FActorSpawnParameters Spawn;
        Spawn.OverrideLevel = World->PersistentLevel;
        AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Spawn);
        if (Actor)
            Actor->GetStaticMeshComponent()->OverrideMaterials = Materials;
        return Actor;
    };

    // The game's own asset at its own path, and an ordinary Unreal copy of it in a creator folder.
    UMaterialInstanceConstant* Game = Instance(TEXT("GameMaterial"));
    FP4OriginalRecord Record;
    Record.Package = Game->GetOutermost()->GetName();
    Record.Class = Game->GetClass()->GetPathName();
    Record.Kind = TEXT("object");
    FString Error;
    if (!TestTrue(TEXT("the game fixture gets its original record"), P4MarkOriginalAsset(Game, Record, Error)))
    {
        AddError(Error);
        return false;
    }
    IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    UObject* Copy = Tools.DuplicateAsset(TEXT("CopiedGameMaterial"), Root, Game);
    if (!TestNotNull(TEXT("an ordinary Unreal duplicate of the game asset exists"), Copy))
        return false;
    Made.Emplace(Copy->GetOutermost());
    TestTrue(TEXT("the game asset is the game's for Play"), P4IsGamePreviewGameSource(Game));
    TestTrue(
        TEXT("the copy carries copied game data, which Play used to treat as the game's"), P4HasCopiedGameData(Copy));
    TestFalse(TEXT("the copy is not the game's for Play"), P4IsGamePreviewGameSource(Copy));
    TestTrue(TEXT("export counts the copy as the creator's"), P4IsCreatorAsset(Copy));

    // Finding 1: an asset the open level uses goes into Play; an unrelated one that export would refuse
    // (an instance without a parent) does not, and does not block Play.
    UMaterialInstanceConstant* Used = Instance(TEXT("UsedMaterial"));
    UMaterialInstanceConstant* Unrelated = Instance(TEXT("UnrelatedBrokenMaterial"));
    TestTrue(TEXT("the unrelated fixture is a creator asset export would refuse"),
        P4IsCreatorAsset(Unrelated) && Unrelated->Parent == nullptr);
    UWorld* Map = WorldAt(Root / TEXT("GameMap"));
    if (!TestNotNull(TEXT("the open level fixture exists"), Map) ||
        !TestNotNull(TEXT("the level uses three materials"),
            Uses(Map, {Copy ? CastChecked<UMaterialInterface>(Copy) : nullptr, Used, Game})))
        return false;
    TArray<FString> Packages;
    if (!TestTrue(
            TEXT("the open level's closure is gathered"), P4GatherGamePreviewCreatorClosure(Map, Packages, Error)))
    {
        AddError(Error);
        return false;
    }
    TArray<FString> Expected{Copy->GetOutermost()->GetName(), Used->GetOutermost()->GetName()};
    Expected.Sort();
    TestEqual(TEXT("Play sends the copy and the used asset, not the game's asset or unrelated content"),
        FString::Join(Packages, TEXT(",")), FString::Join(Expected, TEXT(",")));

    // Finding 2: a level of the creator's own sends the copy and leaves only the real original to the game.
    UWorld* Scratch = WorldAt(TEXT("/Temp/KadanPreviewScopeScratch/") + Id);
    if (!TestNotNull(TEXT("the scratch level fixture exists"), Scratch) ||
        !TestNotNull(TEXT("the scratch level uses the copy and the original"),
            Uses(Scratch, {CastChecked<UMaterialInterface>(Copy), Game})))
        return false;
    FP4GamePreviewWorldPlan Plan;
    if (!TestTrue(
            TEXT("the scratch level's preview world is gathered"), P4GatherGamePreviewWorld(Scratch, Plan, Error)))
    {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("the copied game asset is prepared as creator content"),
        Plan.SourcePackages.Contains(Copy->GetOutermost()->GetName()) &&
            !Plan.GamePackages.Contains(Copy->GetOutermost()->GetName()));
    TestTrue(TEXT("only the game's own asset is left to the game"),
        Plan.GamePackages.Contains(Game->GetOutermost()->GetName()) &&
            !Plan.SourcePackages.Contains(Game->GetOutermost()->GetName()));

    // Finding 7: a gameplay object in a level of the creator's own fails Play with a clear message naming it.
    UClass* Gameplay = FindObject<UClass>(nullptr, TEXT("/Script/Carrot4.GActor"));
    UWorld* WithGameplay = WorldAt(TEXT("/Temp/KadanPreviewScopeGameplay/") + Id);
    if (TestNotNull(TEXT("the game's gameplay actor class is available"), Gameplay) &&
        TestNotNull(TEXT("the gameplay level fixture exists"), WithGameplay))
    {
        FActorSpawnParameters Spawn;
        Spawn.OverrideLevel = WithGameplay->PersistentLevel;
        Spawn.ObjectFlags = RF_Transactional;
        AActor* Creature = WithGameplay->SpawnActor<AActor>(Gameplay, FTransform::Identity, Spawn);
        if (TestNotNull(TEXT("a gameplay actor is placed in the creator's level"), Creature))
        {
            Creature->SetActorLabel(TEXT("KadanScopeCreature"));
            FP4GamePreviewWorldPlan Refused;
            TestFalse(TEXT("Play refuses the level"), P4GatherGamePreviewWorld(WithGameplay, Refused, Error));
            TestTrue(TEXT("the refusal names the object and says what to do: ") + Error,
                Error.Contains(TEXT("KadanScopeCreature")) && Error.Contains(TEXT("gameplay objects")) &&
                    Error.Contains(TEXT("game map")));
        }
    }

    // Finding 5: a placeholder made for the game's SD_Area leaves that path when preparation ends.
    const FString ParentPackage = TEXT("/Game/Carrot4/GameScene/Game/SD_Area");
    const bool ParentExisted = FindPackage(nullptr, *ParentPackage) || FPackageName::DoesPackageExist(ParentPackage);
    {
        const FString PreviewRoot = TEXT(P4_PREVIEW_PACKAGE_ROOT) + Id + TEXT("_fedcba9876543210");
        FP4GamePreviewSceneAssets Scene;
        if (!TestTrue(TEXT("a preview scene can be created"),
                Scene.Create(PreviewRoot, PreviewRoot / TEXT("Area007_P.Area007_P"), AreaRecipe(7), Error)))
            AddError(Error);
    }
    if (!ParentExisted)
        TestTrue(TEXT("no placeholder remains at the game's SD_Area path"),
            !FindPackage(nullptr, *ParentPackage) &&
                !FindObject<UObject>(nullptr, *(ParentPackage + TEXT(".SD_Area"))));
    else
        TestTrue(TEXT("the project's own SD_Area is left in place"),
            FindPackage(nullptr, *ParentPackage) || FPackageName::DoesPackageExist(ParentPackage));
    return true;
}
#endif
